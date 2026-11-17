#include "ToshiPCH.h"
#include "TInputDualSenseRumble_Win.h"

#ifdef BARNYARD_COMMUNITY_PATCH
#include <hidsdi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <wdmguid.h>
#include <cstring>
#include <cwchar>

#pragma comment( lib, "hid.lib" )
#pragma comment( lib, "cfgmgr32.lib" )

TOSHI_NAMESPACE_START

// Report layout: Linux hid-playstation.c (dualsense_output_report_usb) and
// SDL_hidapi_ps5.c. The legacy vibration mode works without a firmware query.
// Only motor-valid bits are set: LEDs, triggers, microphone and volume stay alone.
static constexpr DWORD DUALSENSE_USB_REPORT_SIZE = 63;

struct TDualSenseRumbleContext
{
	volatile LONG iReferences;
	volatile LONG iDesired;
	volatile LONG iClosing;
	volatile LONG iFinished;
	unsigned int uiGeneration;
	bool bFailed;
	HWND hOwnWindow;
	HANDLE hWake;
	wchar_t wszPath[ MAX_PATH ];
};

static LONG ReadRumbleAtomic( volatile LONG* a_pValue )
{
	return InterlockedCompareExchange( a_pValue, 0, 0 );
}

static void ReleaseRumbleContext( TDualSenseRumbleContext* a_pContext )
{
	if ( InterlockedDecrement( &a_pContext->iReferences ) == 0 )
	{
		CloseHandle( a_pContext->hWake );
		HeapFree( GetProcessHeap(), 0, a_pContext );
	}
}

static unsigned char RumbleMagnitude( float a_fValue )
{
	// Reject NaN/negative values. Legacy rumble is halved as in SDL to match
	// Xbox motor strength; infinity and values above one saturate.
	if ( !( a_fValue > 0.0f ) ) return 0;
	if ( a_fValue >= 1.0f ) return 127;
	return static_cast<unsigned char>( a_fValue * 255.0f ) >> 1;
}

bool TInputDualSenseRumble::IsOutputDisabled()
{
	const wchar_t* pCommandLine = GetCommandLineW();
	return wcsstr( pCommandLine, L"-background" ) || wcsstr( pCommandLine, L"-devnodipads" );
}

static bool IsPhysicalDualSenseUsb( const wchar_t* a_pPath )
{
	wchar_t wszInstance[ MAX_DEVICE_ID_LEN ] = {};
	DEVPROPTYPE eType = 0;
	ULONG uiSize = sizeof( wszInstance );
	if ( CM_Get_Device_Interface_PropertyW( a_pPath, &DEVPKEY_Device_InstanceId, &eType,
	     reinterpret_cast<PBYTE>( wszInstance ), &uiSize, 0 ) != CR_SUCCESS || eType != DEVPROP_TYPE_STRING )
		return false;
	wszInstance[ MAX_DEVICE_ID_LEN - 1 ] = 0;
	DEVINST iNode;
	if ( CM_Locate_DevNodeW( &iNode, wszInstance, CM_LOCATE_DEVNODE_NORMAL ) != CR_SUCCESS )
		return false;

	// Inspect only this interface's ancestry, never search other pads by VID/PID.
	// Virtual Sony devices and Bluetooth must not acquire a native output handle.
	for ( unsigned int i = 0; i < 4; ++i )
	{
		GUID oBus = {};
		uiSize = sizeof( oBus );
		if ( CM_Get_DevNode_PropertyW( iNode, &DEVPKEY_Device_BusTypeGuid, &eType,
		     reinterpret_cast<PBYTE>( &oBus ), &uiSize, 0 ) == CR_SUCCESS &&
		     eType == DEVPROP_TYPE_GUID && uiSize == sizeof( oBus ) && IsEqualGUID( oBus, GUID_BUS_TYPE_USB ) )
		{
			uiSize = sizeof( wszInstance );
			if ( CM_Get_DevNode_PropertyW( iNode, &DEVPKEY_Device_InstanceId, &eType,
			     reinterpret_cast<PBYTE>( wszInstance ), &uiSize, 0 ) != CR_SUCCESS || eType != DEVPROP_TYPE_STRING )
				return false;
			wszInstance[ MAX_DEVICE_ID_LEN - 1 ] = 0;
			constexpr wchar_t s_wszPrefix[] = L"USB\\VID_054C&PID_0CE6";
			constexpr size_t iLength = sizeof( s_wszPrefix ) / sizeof( wchar_t ) - 1;
			if ( _wcsnicmp( wszInstance, s_wszPrefix, iLength ) == 0 &&
			     ( wszInstance[ iLength ] == L'&' || wszInstance[ iLength ] == L'\\' ) )
				return true;
		}
		DEVINST iParent;
		if ( CM_Get_Parent( &iParent, iNode, 0 ) != CR_SUCCESS ) break;
		iNode = iParent;
	}
	return false;
}

static bool HasDualSenseUsbReports( HANDLE a_hDevice )
{
	HIDD_ATTRIBUTES oAttributes = {};
	oAttributes.Size = sizeof( oAttributes );
	if ( !HidD_GetAttributes( a_hDevice, &oAttributes ) || oAttributes.VendorID != 0x054C || oAttributes.ProductID != 0x0CE6 )
		return false;
	PHIDP_PREPARSED_DATA pData = nullptr;
	if ( !HidD_GetPreparsedData( a_hDevice, &pData ) ) return false;
	HIDP_CAPS oCaps = {};
	NTSTATUS iResult = HidP_GetCaps( pData, &oCaps );
	HidD_FreePreparsedData( pData );
	// Windows WriteFile uses the longest output report length, including ID.
	// SDL's 48-byte payload is zero-padded to this USB descriptor's 63 bytes.
	return iResult == HIDP_STATUS_SUCCESS && oCaps.UsagePage == 1 && oCaps.Usage == 5 &&
	       oCaps.InputReportByteLength == 64 && oCaps.OutputReportByteLength == DUALSENSE_USB_REPORT_SIZE;
}

static bool RumbleSessionClosing( TDualSenseRumbleContext* a_pContext )
{
	return ReadRumbleAtomic( &a_pContext->iClosing ) || TInputDualSenseRumble::IsOutputDisabled() ||
	       GetForegroundWindow() != a_pContext->hOwnWindow;
}

static void RunRumbleSession( TDualSenseRumbleContext* a_pContext, HANDLE a_hDevice, HANDLE a_hIoEvent )
{
	OVERLAPPED oIo = {};
	oIo.hEvent = a_hIoEvent;
	unsigned char aReport[ DUALSENSE_USB_REPORT_SIZE ] = {};
	HANDLE aEvents[] = { a_pContext->hWake, a_hIoEvent };
	bool bPending = false;
	bool bCancelRequested = false;
	bool bOwnRumble = false;
	bool bStopSubmitted = false;
	LONG iSubmitted = 0;
	LONG iLastCompleted = 0;
	DWORD uiSubmittedAt = 0;

	for ( ;; )
	{
		bool bClosing = RumbleSessionClosing( a_pContext );
		if ( bPending && GetTickCount() - uiSubmittedAt >= 250 ) bClosing = true;
		if ( bClosing ) InterlockedExchange( &a_pContext->iClosing, 1 );
		if ( bPending )
		{
			// A stop report also has a bounded attempt; cancellation still needs
			// completion before the buffer/OVERLAPPED/handle may be released.
			if ( bClosing && !bCancelRequested && ( iSubmitted != 0 || GetTickCount() - uiSubmittedAt >= 250 ) )
			{
				CancelIoEx( a_hDevice, &oIo );
				bCancelRequested = true;
			}
			DWORD uiWritten = 0;
			BOOL bComplete = GetOverlappedResult( a_hDevice, &oIo, &uiWritten, FALSE );
			DWORD uiError = bComplete ? ERROR_SUCCESS : GetLastError();
			if ( !bComplete && uiError == ERROR_IO_INCOMPLETE )
			{
				WaitForMultipleObjects( 2, aEvents, FALSE, 25 );
				continue;
			}
			bPending = false;
			if ( bComplete && uiWritten == sizeof( aReport ) )
			{
				iLastCompleted = iSubmitted;
				if ( iSubmitted == 0 ) bOwnRumble = false;
			}
			else if ( uiError != ERROR_OPERATION_ABORTED )
			{
				a_pContext->bFailed = true;
				break;
			}
			if ( bStopSubmitted ) break;
		}

		LONG iDesired = bClosing ? 0 : ReadRumbleAtomic( &a_pContext->iDesired );
		if ( bClosing && ( !bOwnRumble || TInputDualSenseRumble::IsOutputDisabled() ) ) break;
		if ( bClosing || iDesired != iLastCompleted )
		{
			// Recheck immediately before output, including the owned stop path.
			if ( TInputDualSenseRumble::IsOutputDisabled() ) break;
			if ( !bClosing && RumbleSessionClosing( a_pContext ) ) continue;
			std::memset( aReport, 0, sizeof( aReport ) );
			aReport[ 0 ] = 0x02;
			// Like SDL, release the emulated-rumble bits on stop so audio
			// haptics is not left disabled after our session loses focus.
			aReport[ 1 ] = iDesired ? 0x03 : 0x00;
			aReport[ 3 ] = static_cast<unsigned char>( iDesired );
			aReport[ 4 ] = static_cast<unsigned char>( iDesired >> 8 );
			ResetEvent( a_hIoEvent );
			iSubmitted = iDesired;
			uiSubmittedAt = GetTickCount();
			bCancelRequested = false;
			bStopSubmitted = bClosing;
			DWORD uiWritten = 0;
			BOOL bComplete = WriteFile( a_hDevice, aReport, sizeof( aReport ), &uiWritten, &oIo );
			if ( !bComplete && GetLastError() != ERROR_IO_PENDING )
			{
				a_pContext->bFailed = true;
				break;
			}
			// Pending nonzero output might reach the device before cancellation.
			// Only a session that submitted its own rumble may subsequently stop it.
			if ( iDesired != 0 ) bOwnRumble = true;
			if ( bComplete )
			{
				if ( uiWritten != sizeof( aReport ) ) { a_pContext->bFailed = true; break; }
				iLastCompleted = iDesired;
				if ( bClosing ) break;
			}
			else bPending = true;
		}
		else WaitForSingleObject( a_pContext->hWake, 25 );
	}
}

static DWORD WINAPI RumbleWorker( void* a_pData )
{
	auto pContext = static_cast<TDualSenseRumbleContext*>( a_pData );
	SetThreadPriority( GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL );
	// PnP queries, opening and all output happen away from the game's thread.
	// The two hard-disable flags prevent even an initialization/stop write.
	if ( !RumbleSessionClosing( pContext ) && IsPhysicalDualSenseUsb( pContext->wszPath ) && !RumbleSessionClosing( pContext ) )
	{
		HANDLE hDevice = CreateFileW( pContext->wszPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
		                             nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr );
		if ( hDevice != INVALID_HANDLE_VALUE )
		{
			if ( HasDualSenseUsbReports( hDevice ) )
			{
				HANDLE hEvent = CreateEventW( nullptr, TRUE, FALSE, nullptr );
				if ( hEvent )
				{
					RunRumbleSession( pContext, hDevice, hEvent );
					CloseHandle( hEvent );
				}
				else pContext->bFailed = true;
			}
			else pContext->bFailed = true;
			CloseHandle( hDevice );
		}
		else pContext->bFailed = true;
	}
	else if ( !RumbleSessionClosing( pContext ) ) pContext->bFailed = true;
	InterlockedExchange( &pContext->iFinished, 1 );
	ReleaseRumbleContext( pContext );
	return 0;
}

TInputDualSenseRumble::~TInputDualSenseRumble()
{
	Stop();
	if ( m_pContext ) ReleaseRumbleContext( m_pContext );
	// The worker keeps its own reference until canceled I/O actually completes.
}

void TInputDualSenseRumble::Stop()
{
	if ( m_pContext )
	{
		InterlockedExchange( &m_pContext->iClosing, 1 );
		SetEvent( m_pContext->hWake );
	}
}

void TInputDualSenseRumble::Reset()
{
	Stop();
	++m_uiGeneration;
	m_bUnavailable = false;
}

void TInputDualSenseRumble::SetVibration( HWND a_hOwnWindow, const wchar_t* a_pDevicePath, float a_fRight, float a_fLeft )
{
	LONG iDesired = RumbleMagnitude( a_fRight ) | ( LONG( RumbleMagnitude( a_fLeft ) ) << 8 );
	if ( IsOutputDisabled() || !a_hOwnWindow || GetForegroundWindow() != a_hOwnWindow || !iDesired )
	{
		Stop();
		return;
	}
	if ( m_pContext && ReadRumbleAtomic( &m_pContext->iFinished ) )
	{
		if ( m_pContext->uiGeneration == m_uiGeneration ) m_bUnavailable = m_pContext->bFailed;
		ReleaseRumbleContext( m_pContext );
		m_pContext = nullptr;
	}
	if ( m_bUnavailable || !a_pDevicePath || !a_pDevicePath[ 0 ] || wcslen( a_pDevicePath ) >= MAX_PATH ) return;
	if ( m_pContext )
	{
		// No new session can race the preceding session's pending stop report.
		if ( ReadRumbleAtomic( &m_pContext->iClosing ) ) return;
		InterlockedExchange( &m_pContext->iDesired, iDesired );
		SetEvent( m_pContext->hWake );
		return;
	}
	auto pContext = static_cast<TDualSenseRumbleContext*>( HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof( TDualSenseRumbleContext ) ) );
	if ( !pContext ) return;
	pContext->hWake = CreateEventW( nullptr, FALSE, FALSE, nullptr );
	if ( !pContext->hWake ) { HeapFree( GetProcessHeap(), 0, pContext ); return; }
	pContext->iReferences = 2;
	pContext->iDesired = iDesired;
	pContext->uiGeneration = m_uiGeneration;
	pContext->hOwnWindow = a_hOwnWindow;
	wcscpy_s( pContext->wszPath, a_pDevicePath );
	HANDLE hThread = CreateThread( nullptr, 0, RumbleWorker, pContext, 0, nullptr );
	if ( !hThread )
	{
		ReleaseRumbleContext( pContext );
		ReleaseRumbleContext( pContext );
		return;
	}
	CloseHandle( hThread );
	m_pContext = pContext;
}

TOSHI_NAMESPACE_END
#endif

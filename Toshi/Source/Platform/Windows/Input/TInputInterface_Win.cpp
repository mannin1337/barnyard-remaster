#include "ToshiPCH.h"
#include "TInputInterface_Win.h"
#include "Render/TBackend.h"
#include "TInputDeviceController_Win.h"
#include "TInputDeviceController_XInput.h"
#include "TInputDeviceMouse_Win.h"
#include "TInputDeviceKeyboard_Win.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include "Core/TMemoryDebugOn.h"

TOSHI_NAMESPACE_START

// NOTE: GetClass 006d7380, CreateObject 006d73b0, CreateObjectInPlace 006d73f0
TDEFINE_CLASS( TInputDXInterface );

#ifdef BARNYARD_COMMUNITY_PATCH
// For the logged duration of the device scans
static TDOUBLE GetMilliseconds()
{
	LARGE_INTEGER oFrequency, oCounter;
	QueryPerformanceFrequency( &oFrequency );
	QueryPerformanceCounter( &oCounter );
	return TDOUBLE( oCounter.QuadPart ) * 1000.0 / TDOUBLE( oFrequency.QuadPart );
}
#endif

// $Barnyard: FUNCTION 006d7770
TBOOL TInputDXInterface::Initialise()
{
	TASSERT( TNULL == m_poDirectInput8 );
	HRESULT hRes = DirectInput8Create( GetModuleHandle( NULL ), DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID*)&m_poDirectInput8, NULL );

	if ( SUCCEEDED( hRes ) )
	{
		if ( m_hMainWindow == NULL )
		{
			m_hMainWindow = FindWindowA( STRINGIFY( TRenderInterfaceHAL ), NULL );
		}

#ifdef BARNYARD_COMMUNITY_PATCH
		CreateXInputControllers();
		const TDOUBLE fScanStart = GetMilliseconds();
#endif

		hRes = m_poDirectInput8->EnumDevices( DI8DEVCLASS_ALL, TInputDXInterface::EnumerateDeviceCallback, this, DIEDFL_ATTACHEDONLY );

#ifdef BARNYARD_COMMUNITY_PATCH
		// The controllers are looked for again only after a device change (RequestControllerScan)
		m_uiLastControllerScan = GetTickCount();
		TINFO( "Input: device scan at start took %.1f ms, controllers are looked for again on device changes\n", GetMilliseconds() - fScanStart );
#endif

		if ( hRes == DI_OK )
		{
			return TTRUE;
		}
		else
		{
			Deinitialise();
		}
	}

	return TFALSE;
}

// $Barnyard: FUNCTION 006d7390
TBOOL TInputDXInterface::Deinitialise()
{
	TInputInterface::ReleaseAllDevices();

	if ( m_poDirectInput8 != NULL )
	{
		m_poDirectInput8->Release();
		m_poDirectInput8 = NULL;
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	TInputXInputDeviceController::UnloadXInput();
#endif

	return TTRUE;
}

// $Barnyard: FUNCTION 006d77e0
void TInputDXInterface::SetExclusiveMode( TBOOL a_bIsExclusive )
{
	auto pMouse = GetDeviceByIndex<TInputDXDeviceMouse>( 0 );
	pMouse->Unacquire();
	pMouse->SetExclusive( m_hMainWindow, a_bIsExclusive );
	pMouse->Acquire();
	TInputInterface::SetExclusiveMode( a_bIsExclusive );
}

TBOOL TInputDXInterface::LostDevice()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	RequestControllerScan();
	m_uiLastControllerScan = GetTickCount() - CONTROLLER_RESCAN_INTERVAL_MS;
	RefreshDirect();
	return TFALSE;
#else
	auto input = GetDirectInput();

	if ( input != NULL )
	{
		return input->EnumDevices( DI8DEVCLASS_ALL, EnumerateDeviceCallback, this, DIEDFL_ATTACHEDONLY ) != DI_OK;
	}

	return TFALSE;
#endif
}

// $Barnyard: FUNCTION 006d7470
BOOL TInputDXInterface::EnumerateDeviceCallback( LPCDIDEVICEINSTANCEA a_poDeviceInstance, LPVOID poDXInputInterface )
{
	TInputDXInterface* pInputInterface = TSTATICCAST( TInputDXInterface, poDXInputInterface );

	TASSERT( poDXInputInterface != NULL );
	TASSERT( a_poDeviceInstance != NULL );

	auto pInput8 = pInputInterface->m_poDirectInput8;

	DWORD uiDevType = GET_DIDEVICE_TYPE( a_poDeviceInstance->dwDevType );

#ifdef BARNYARD_COMMUNITY_PATCH
	// Detect PS5/PS4 controllers
	if ( ( a_poDeviceInstance->dwDevType & DIDEVTYPE_HID ) && a_poDeviceInstance->wUsagePage == 0x01 &&
	     ( a_poDeviceInstance->wUsage == 0x04 || a_poDeviceInstance->wUsage == 0x05 ) )
		uiDevType = DI8DEVTYPE_GAMEPAD;
#endif

	switch ( uiDevType )
	{
		case DI8DEVTYPE_MOUSE:
		{
#ifdef BARNYARD_COMMUNITY_PATCH
			if ( pInputInterface->GetDeviceByIndex<TInputDXDeviceMouse>( 0 ) ) return DIENUM_CONTINUE;
#endif
			LPDIRECTINPUTDEVICE8A pInputDevice;
			auto                  pMouse      = pInputInterface->GetDeviceByIndex<TInputDXDeviceMouse>( 0 );
			TBOOL                 bIsNewMouse = TFALSE;

			if ( pInput8->CreateDevice( GUID_SysMouse, &pInputDevice, NULL ) != DI_OK )
				return DIENUM_CONTINUE;

			if ( pMouse == TNULL )
			{
				pMouse = new TInputDXDeviceMouse;

				if ( pMouse == TNULL )
				{
					pInputDevice->Release();
					return DIENUM_CONTINUE;
				}

				bIsNewMouse = TTRUE;
			}
			else if ( pMouse->GetDeviceInstance().dwDevType & DIDEVTYPE_HID )
			{
				pInputDevice->Release();
				return DIENUM_CONTINUE;
			}

			if ( !pMouse->BindToDIDevice( pInputInterface->m_hMainWindow, a_poDeviceInstance, pInputDevice, pInputInterface->m_bExclusive ) )
			{
				if ( !bIsNewMouse )
					pInputInterface->RemoveDevice( pMouse );

				delete pMouse;
				return DIENUM_CONTINUE;
			}

			DIPROPDWORD oProperty;
			oProperty.diph.dwSize       = sizeof( DIPROPDWORD );
			oProperty.diph.dwHeaderSize = sizeof( DIPROPHEADER );
			oProperty.diph.dwObj        = 0;
			oProperty.diph.dwHow        = DIPH_DEVICE;
			oProperty.dwData            = 0x20;

			if ( FAILED( pInputDevice->SetProperty( DIPROP_BUFFERSIZE, &oProperty.diph ) ) )
				return DIENUM_STOP;

			if ( bIsNewMouse == TTRUE )
			{
				pMouse->Initialise();
				pMouse->Acquire();
				pInputInterface->AddDevice( pMouse );
			}

			return DIENUM_CONTINUE;
		}
		case DI8DEVTYPE_KEYBOARD:
		{
#ifdef BARNYARD_COMMUNITY_PATCH
			if ( pInputInterface->GetDeviceByIndex<TInputDXDeviceKeyboard>( 0 ) ) return DIENUM_CONTINUE;
#endif
			LPDIRECTINPUTDEVICE8A pInputDevice;

			if ( pInput8->CreateDevice( a_poDeviceInstance->guidInstance, &pInputDevice, NULL ) != DI_OK )
				return DIENUM_CONTINUE;

			TInputDXDeviceKeyboard* pKeyboard = new TInputDXDeviceKeyboard();

			if ( pKeyboard == TNULL )
			{
				pInputDevice->Release();
				return DIENUM_CONTINUE;
			}

			if ( !pKeyboard->BindToDIDevice( pInputInterface->GetMainWindow(), a_poDeviceInstance, pInputDevice ) )
			{
				delete pKeyboard;
				return DIENUM_CONTINUE;
			}

			DIPROPDWORD oProperty;
			oProperty.diph.dwSize       = sizeof( DIPROPDWORD );
			oProperty.diph.dwHeaderSize = sizeof( DIPROPHEADER );
			oProperty.diph.dwObj        = 0;
			oProperty.diph.dwHow        = DIPH_DEVICE;
			oProperty.dwData            = 0x20;

			if ( FAILED( pInputDevice->SetProperty( DIPROP_BUFFERSIZE, &oProperty.diph ) ) )
				return DIENUM_STOP;

			HRESULT hRes = pInputDevice->SetCooperativeLevel(
			    pInputInterface->GetMainWindow(),
			    pInputInterface->GetExclusiveMode() == TTRUE ? ( DISCL_NOWINKEY | DISCL_FOREGROUND | DISCL_EXCLUSIVE ) : ( DISCL_NOWINKEY | DISCL_FOREGROUND | DISCL_NONEXCLUSIVE )
			);

			if ( hRes != DI_OK )
			{
				delete pKeyboard;
				return DIENUM_CONTINUE;
			}

			pKeyboard->Initialise();
			pKeyboard->Acquire();
			pInputInterface->AddDevice( pKeyboard );

			return DIENUM_CONTINUE;
		}
		case DI8DEVTYPE_JOYSTICK:
		case DI8DEVTYPE_GAMEPAD:
		{
#ifdef BARNYARD_COMMUNITY_PATCH
#  ifndef TOSHI_FINAL
			// Development: "-devnodipads" leaves the DirectInput pads of the PC alone (tests with the scripted XInput pads
			// of "-devkeys" give the same result with or without them)
			static const TBOOL s_bDevNoDIPads = T2String8::FindString( GetCommandLineA(), "-devnodipads" ) != TNULL;
			if ( s_bDevNoDIPads ) return DIENUM_CONTINUE;
#  endif
			TInputDXDeviceController* pController = TNULL;
			for ( auto it = pInputInterface->GetDeviceList().Begin(); it != pInputInterface->GetDeviceList().End(); ++it )
			{
				if ( it->IsA( &TGetClass( TInputDXDeviceController ) ) )
				{
					auto pKnown = TSTATICCAST( TInputDXDeviceController, it.operator->() );
					if ( IsEqualGUID( pKnown->GetInstanceGuid(), a_poDeviceInstance->guidInstance ) )
					{
						pController = pKnown;
						pController->MarkEnumerated( TTRUE );
						if ( pController->HasNativeDevice() ) return DIENUM_CONTINUE;
						break;
					}
				}
			}
			LPDIRECTINPUTDEVICE8A pInputDevice = TNULL;
			if ( pInput8->CreateDevice( a_poDeviceInstance->guidInstance, &pInputDevice, NULL ) != DI_OK )
				return DIENUM_CONTINUE;
			if ( TInputXInputDeviceController::IsXInputLoaded() &&
			     TInputXInputDeviceController::IsXInputDevice( a_poDeviceInstance->guidProduct, pInputDevice ) )
			{
				pInputDevice->Release();
				return DIENUM_CONTINUE;
			}

			TBOOL bNewController = pController == TNULL;
			// AInputHandler has eight identity slots: four XInput + four retained DI.
			if ( bNewController && pInputInterface->m_iNextControllerPort >= 8 )
			{
				pInputDevice->Release();
				return DIENUM_CONTINUE;
			}
			if ( bNewController ) pController = new TInputDXDeviceController();
			if ( !pController )
			{
				pInputDevice->Release();
				return DIENUM_CONTINUE;
			}
			if ( !pController->BindToDIDevice( pInputInterface->m_hMainWindow, a_poDeviceInstance, pInputDevice ) )
			{
				pController->Release();
				if ( bNewController ) delete pController;
				return DIENUM_CONTINUE;
			}
			if ( bNewController )
			{
				pController->SetPortIndex( pInputInterface->m_iNextControllerPort++ );
				pInputInterface->AddDevice( pController );
			}
			pController->MarkEnumerated( TTRUE );
			// Publish after registration, before input polling: co-op can bind P2 here.
			pController->SetConnected( TTRUE );
			pController->Acquire();
			return DIENUM_CONTINUE;
#else
			LPDIRECTINPUTDEVICE8A pInputDevice;

			if ( pInput8->CreateDevice( a_poDeviceInstance->guidInstance, &pInputDevice, NULL ) != DI_OK )
				return DIENUM_CONTINUE;

			TInputDXDeviceController* pController = new TInputDXDeviceController();

			if ( pController == TNULL )
			{
				pInputDevice->Release();
				return DIENUM_CONTINUE;
			}

			if ( !pController->BindToDIDevice( pInputInterface->m_hMainWindow, a_poDeviceInstance, pInputDevice ) )
			{
				delete pController;
				return DIENUM_CONTINUE;
			}

			TINFO( "TInputDXInterface: added controller '%s'\n", a_poDeviceInstance->tszProductName );

			pController->Initialise();
			pInputInterface->AddDevice( pController );
			pController->Acquire();

			return DIENUM_CONTINUE;
#endif
		}
		default:
			break;
	}

	return DIENUM_CONTINUE;
}

#ifdef BARNYARD_COMMUNITY_PATCH

TBOOL TInputDXInterface::AcquireAll()
{
	RefreshDirect();
	return TInputInterface::AcquireAll();
}

void TInputDXInterface::RefreshDirect()
{
	// EnumDevices is slow (145 ms measured with one pad attached), so it isn't polled: the controllers are looked for after a
	// device change (RequestControllerScan, a few scans 1 s apart), and while a connected DirectInput pad has lost its
	// native handle (input lost, unplugged or a failed poll), which the scan binds again or finds disconnected
	if ( !m_poDirectInput8 || GetTickCount() - m_uiLastControllerScan < CONTROLLER_RESCAN_INTERVAL_MS ) return;

	const TCHAR* szReason = ( ms_iPendingControllerScans > 0 ) ? "device change" : TNULL;
	for ( auto it = GetDeviceList().Begin(); !szReason && it != GetDeviceList().End(); ++it )
	{
		if ( it->IsA( &TGetClass( TInputDXDeviceController ) ) )
		{
			auto pController = TSTATICCAST( TInputDXDeviceController, it.operator->() );
			if ( pController->IsConnected() && !pController->HasNativeDevice() ) szReason = "a pad lost its handle";
		}
	}

	if ( !szReason ) return;
	if ( ms_iPendingControllerScans > 0 ) ms_iPendingControllerScans--;

	for ( auto it = GetDeviceList().Begin(); it != GetDeviceList().End(); ++it )
	{
		if ( it->IsA( &TGetClass( TInputDXDeviceController ) ) )
			TSTATICCAST( TInputDXDeviceController, it.operator->() )->MarkEnumerated( TFALSE );
		// The empty XInput slots are probed again right away too (one at a time, see TInputXInputDeviceController::Acquire)
		else if ( it->IsA( &TGetClass( TInputXInputDeviceController ) ) )
			TSTATICCAST( TInputXInputDeviceController, it.operator->() )->RetryNow();
	}

	// Keyboard and mouse are singleton devices and are never rebuilt by hot-plug.
	const TDOUBLE fScanStart = GetMilliseconds();
	const HRESULT hRes = m_poDirectInput8->EnumDevices( DI8DEVCLASS_GAMECTRL, EnumerateDeviceCallback, this, DIEDFL_ATTACHEDONLY );

	// From the end of the scan: a slow one doesn't start again on the next frame
	m_uiLastControllerScan = GetTickCount();
	TINFO( "Input: controller scan (%s) took %.1f ms\n", szReason, GetMilliseconds() - fScanStart );

	if ( FAILED( hRes ) ) return;
	for ( auto it = GetDeviceList().Begin(); it != GetDeviceList().End(); ++it )
	{
		if ( it->IsA( &TGetClass( TInputDXDeviceController ) ) )
		{
			auto pController = TSTATICCAST( TInputDXDeviceController, it.operator->() );
			if ( !pController->WasEnumerated() ) pController->SetConnected( TFALSE );
		}
	}
}

void TInputDXInterface::CreateXInputControllers()
{
	if ( !TInputXInputDeviceController::LoadXInput() )
		return;

	for ( DWORD i = 0; i < XUSER_MAX_COUNT; i++ )
	{
		TInputXInputDeviceController* pController = new TInputXInputDeviceController();
		pController->BindToUserIndex( i );
		AddDevice( pController );
		pController->Acquire();
	}
}

#endif // BARNYARD_COMMUNITY_PATCH

TOSHI_NAMESPACE_END

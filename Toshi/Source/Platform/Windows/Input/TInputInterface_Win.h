#pragma once
#include "Includes.h"
#include "Input/TInputInterface.h"

TOSHI_NAMESPACE_START

class TInputDXInterface : public TInputInterface
{
public:
	TDECLARE_CLASS( TInputDXInterface, TInputInterface );

public:
	TInputDXInterface()
	{
		m_poDirectInput8 = NULL;
		m_hMainWindow    = NULL;
		m_bExclusive     = TTRUE;
#ifdef BARNYARD_COMMUNITY_PATCH
		m_uiLastControllerScan = 0;
		m_iNextControllerPort = 4;
#endif
	}

	// $Barnyard: FUNCTION 006d7430
	virtual ~TInputDXInterface() override
	{
		ReleaseAllDevices();

		if ( m_poDirectInput8 )
		{
			m_poDirectInput8->Release();
			m_poDirectInput8 = TNULL;
		}
	}

	virtual TBOOL Initialise() override;
	virtual TBOOL Deinitialise() override;
	virtual void  SetExclusiveMode( TBOOL a_bIsExclusive ) override;
#ifdef BARNYARD_COMMUNITY_PATCH
	// Time between two scans for game controllers (milliseconds)
	static constexpr DWORD CONTROLLER_RESCAN_INTERVAL_MS = 1000;

	// A device change is followed by this many scans: DirectInput can list a new device a bit after the notification
	static constexpr TINT NUM_CONTROLLER_RESCANS = 3;

	virtual TBOOL AcquireAll() override;
	virtual void  RefreshDirect();
	TBOOL HasInputFocus() const
	{
#  ifndef TOSHI_FINAL
		if ( ms_bDevInputFocus ) return TTRUE;
#  endif
		return m_hMainWindow && GetForegroundWindow() == m_hMainWindow;
	}

	// The window procedures call this on WM_DEVICECHANGE with DBT_DEVNODES_CHANGED (a device was added or removed, sent
	// to every top-level window). The controllers are only looked for then: DirectInput's enumeration is slow
	static void RequestControllerScan() { ms_iPendingControllerScans = NUM_CONTROLLER_RESCANS; }

#  ifndef TOSHI_FINAL
	// Development: the scripted XInput pads (TInputXInputDeviceController::Dev_SetUser) are read while another window
	// is in the foreground (hidden test runs)
	inline static TBOOL ms_bDevInputFocus = TFALSE;
#  endif
#else
	virtual void  RefreshDirect() {}
#endif

	TBOOL LostDevice();

	HWND GetMainWindow() const
	{
		return m_hMainWindow;
	}

	void SetMainWindow( HWND a_hMainWindow )
	{
		m_hMainWindow = a_hMainWindow;
	}

private:
#ifdef BARNYARD_COMMUNITY_PATCH
	void CreateXInputControllers();
#endif

	static BOOL CALLBACK EnumerateDeviceCallback( LPCDIDEVICEINSTANCEA a_poDeviceInstance, LPVOID poDXInputInterface );

	LPDIRECTINPUT8A GetDirectInput() const
	{
		return m_poDirectInput8;
	}

private:
	LPDIRECTINPUT8A m_poDirectInput8; // 0x30
	HWND            m_hMainWindow;    // 0x34
	TBOOL           m_bExclusive;     // 0x38
#ifdef BARNYARD_COMMUNITY_PATCH
	DWORD m_uiLastControllerScan; // GetTickCount at the end of the last scan
	TINT  m_iNextControllerPort;

	// Scans still to do after the last device change (RequestControllerScan)
	inline static TINT ms_iPendingControllerScans = 0;
#endif
};

TOSHI_NAMESPACE_END

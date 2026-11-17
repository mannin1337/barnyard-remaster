#include "ToshiPCH.h"
#include "TInputDeviceMouse_Win.h"
#include "Render/TBackend.h"
#include "Platform/Windows/Input/TInputInterface_Win.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include "Core/TMemoryDebugOn.h"

TOSHI_NAMESPACE_START

// NOTE: GetClass 006d3c20, CreateObject 006d4190, CreateObjectInPlace 006d41d0
TDEFINE_CLASS( TInputDXDeviceMouse );

#ifdef BARNYARD_COMMUNITY_PATCH

void TInputDXDeviceMouse::SetRawInputEnabled( TBOOL a_bEnabled )
{
	TInputInterface* pInput = TInputInterface::GetSingleton();
	TInputDXDeviceMouse* pMouse = pInput ? pInput->GetDeviceByIndex<TInputDXDeviceMouse>() : TNULL;
	const TBOOL bChanged = ms_bRawInputEnabled != a_bEnabled;
	if ( pMouse && bChanged ) pMouse->Unacquire();
	ms_bRawInputEnabled = a_bEnabled;
	if ( pMouse )
	{
		pMouse->ConfigureRawInput();
		if ( bChanged ) pMouse->Acquire();
	}
}

void TInputDXDeviceMouse::ConfigureRawInput()
{
	if ( ms_bRawInputEnabled && m_hMainWindow && !m_bRawInputRegistered )
	{
		// Flags zero means foreground only; legacy window messages remain available.
		RAWINPUTDEVICE device = { 0x01, 0x02, 0, m_hMainWindow };
		m_bRawInputRegistered = RegisterRawInputDevices( &device, 1, sizeof( device ) ) != FALSE;
		if ( m_bRawInputRegistered )
			TINFO( "[input/rawmouse] registered foreground mouse\n" );
		else
			TWARN( "[input/rawmouse] registration failed (%lu), using DirectInput motion\n", GetLastError() );
	}
	else if ( !ms_bRawInputEnabled && m_bRawInputRegistered )
	{
		RAWINPUTDEVICE device = { 0x01, 0x02, RIDEV_REMOVE, NULL };
		if ( RegisterRawInputDevices( &device, 1, sizeof( device ) ) )
		{
			m_bRawInputRegistered = TFALSE;
			TINFO( "[input/rawmouse] disabled, using DirectInput motion\n" );
		}
		else
			TWARN( "[input/rawmouse] removal failed (%lu); raw packets ignored\n", GetLastError() );
	}
}

void TInputDXDeviceMouse::ReleaseRawCursor()
{
	if ( m_bRawCursorClipped )
	{
		ClipCursor( NULL );
		m_bRawCursorClipped = TFALSE;
	}
}

void TInputDXDeviceMouse::ResetRawMotion()
{
	ReleaseRawCursor();
	const DWORD uiReleasedButtons = IsAcquired() ? m_dwButtonCurrent : 0;
	m_iRawDeltaX = m_iRawDeltaY = m_iRawDeltaZ = 0;
	m_iRawWheelRemainder = 0;
	m_dwRawButtons = m_dwButtonCurrent = m_dwButtonPrevious = 0;
	m_iNumRawButtonChanges = 0;
	StopAllRepeats();
	m_iDeltaPositionX = m_iDeltaPositionY = m_iDeltaPositionZ = 0;
	m_bAbsoluteRawValid = TFALSE;
	if ( uiReleasedButtons && GetInputInterface() )
	{
		for ( TINT i = 0; i < 5; i++ )
			if ( uiReleasedButtons & ( 1U << i ) )
				GetInputInterface()->GetInputEventEmitter().Throw(
				    TInputInterface::InputEvent( this, BUTTON_1 + i, TInputInterface::EVENT_TYPE_GONE_UP ) );
	}
}

void TInputDXDeviceMouse::AccumulateRawMotion( TINT a_iDX, TINT a_iDY )
{
	// Saturate malformed device data instead of overflowing a signed counter.
	TINT64 iX = TINT64( m_iRawDeltaX ) + a_iDX;
	TINT64 iY = TINT64( m_iRawDeltaY ) + a_iDY;
	TMath::Clip( iX, TINT64( -2147483647 ), TINT64( 2147483647 ) );
	TMath::Clip( iY, TINT64( -2147483647 ), TINT64( 2147483647 ) );
	m_iRawDeltaX = TINT( iX );
	m_iRawDeltaY = TINT( iY );
}

void TInputDXDeviceMouse::HandleRawInput( HWND a_hWindow, WPARAM a_wParam, LPARAM a_lParam )
{
	if ( !ms_bRawInputEnabled || !m_bRawInputRegistered || a_hWindow != m_hMainWindow ||
	     GET_RAWINPUT_CODE_WPARAM( a_wParam ) != RIM_INPUT || GetForegroundWindow() != a_hWindow )
		return;

	RAWINPUT input = {};
	UINT uiSize = sizeof( input );
	const UINT uiRead = GetRawInputData( reinterpret_cast<HRAWINPUT>( a_lParam ), RID_INPUT, &input, &uiSize, sizeof( RAWINPUTHEADER ) );
	if ( uiRead == UINT( -1 ) || uiRead < sizeof( RAWINPUTHEADER ) + sizeof( RAWMOUSE ) || input.header.dwType != RIM_TYPEMOUSE )
		return;

	const RAWMOUSE& mouse = input.data.mouse;
	// Button transitions stay buffered until ProcessEvents, preserving quick clicks between frames.
	for ( TINT iButton = 0; iButton < 5; iButton++ )
	{
		const TINT iMappedButton = iButton < 2 && m_bReverseButtons ? 1 - iButton : iButton;
		for ( TINT iUp = 0; iUp < 2; iUp++ )
		{
			if ( !( mouse.usButtonFlags & ( 1U << ( iButton * 2 + iUp ) ) ) ) continue;
			const TBOOL bWasDown = ( m_dwRawButtons & ( 1U << iMappedButton ) ) != 0;
			if ( bWasDown == ( iUp == 0 ) ) continue;
			if ( iUp ) m_dwRawButtons &= ~( 1U << iMappedButton );
			else m_dwRawButtons |= 1U << iMappedButton;
			if ( m_iNumRawButtonChanges < TARRAYSIZE( m_aiRawButtonChanges ) )
				m_aiRawButtonChanges[ m_iNumRawButtonChanges++ ] = ( iMappedButton + 1 ) * ( iUp ? -1 : 1 );
		}
	}
	if ( mouse.usButtonFlags & RI_MOUSE_WHEEL )
		m_iRawDeltaZ += static_cast<SHORT>( mouse.usButtonData );

	if ( mouse.usFlags & MOUSE_MOVE_ABSOLUTE )
	{
		// Absolute raw devices (e.g. remote desktops) need a baseline, not a 65535-count jump.
		const TBOOL bVirtualDesktop = ( mouse.usFlags & MOUSE_VIRTUAL_DESKTOP ) != 0;
		const TINT iWidth  = GetSystemMetrics( bVirtualDesktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN );
		const TINT iHeight = GetSystemMetrics( bVirtualDesktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN );
		const TINT iX = MulDiv( mouse.lLastX, iWidth, 65535 );
		const TINT iY = MulDiv( mouse.lLastY, iHeight, 65535 );
		if ( m_bAbsoluteRawValid && m_hAbsoluteRawDevice == input.header.hDevice )
			AccumulateRawMotion( iX - m_iAbsoluteRawX, iY - m_iAbsoluteRawY );
		m_hAbsoluteRawDevice = input.header.hDevice;
		m_iAbsoluteRawX = iX;
		m_iAbsoluteRawY = iY;
		m_bAbsoluteRawValid = TTRUE;
	}
	else
	{
		m_bAbsoluteRawValid = TFALSE;
		AccumulateRawMotion( mouse.lLastX, mouse.lLastY );
	}
}

#  ifndef TOSHI_FINAL
void TInputDXDeviceMouse::Dev_QueueRawMotion( TINT a_iDX, TINT a_iDY )
{
	m_bDevMotionScript = TTRUE;
	AccumulateRawMotion( a_iDX, a_iDY );
}
#  endif

#endif // BARNYARD_COMMUNITY_PATCH

// $Barnyard: FUNCTION 006d3dd0
TBOOL TInputDXDeviceMouse::Acquire()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( ms_bRawInputEnabled && m_bRawInputRegistered )
	{
		// DirectInput uses its own WM_INPUT target. Do not acquire it while our window owns raw input.
		const TBOOL bForeground = !m_bRawBackground && m_hMainWindow && IsWindowVisible( m_hMainWindow ) &&
		    GetForegroundWindow() == m_hMainWindow;
		if ( !bForeground )
		{
			ReleaseRawCursor();
			if ( IsAcquired() ) ResetRawMotion();
		}
		else if ( m_bRawExclusive )
		{
			// Preserve DirectInput's exclusive cursor confinement, only while our visible window is active.
			RECT client;
			if ( GetClientRect( m_hMainWindow, &client ) && !IsRectEmpty( &client ) )
			{
				MapWindowPoints( m_hMainWindow, NULL, reinterpret_cast<POINT*>( &client ), 2 );
				if ( ClipCursor( &client ) ) m_bRawCursorClipped = TTRUE;
				else ReleaseRawCursor();
			}
			else ReleaseRawCursor();
		}
		else ReleaseRawCursor();
		SetAcquired( bForeground );
		return bForeground;
	}
#endif
	HRESULT hr = m_poDXInputDevice->Acquire();

	if ( FAILED( hr ) )
	{
		SetAcquired( TFALSE );
		return TFALSE;
	}

	SetAcquired( TTRUE );

	if ( hr != S_FALSE )
	{
		RefreshDirect();
	}

	return TTRUE;
}

// $Barnyard: FUNCTION 006d3e00
TBOOL TInputDXDeviceMouse::Unacquire()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	ResetRawMotion();
#endif
	SetAcquired( TFALSE );
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( ms_bRawInputEnabled && m_bRawInputRegistered ) return TTRUE;
#endif
	HRESULT hr = m_poDXInputDevice->Unacquire();
	return SUCCEEDED( hr );
}

// $Barnyard: FUNCTION 006d3e20
void TInputDXDeviceMouse::Release()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( m_bRawInputRegistered )
	{
		RAWINPUTDEVICE device = { 0x01, 0x02, RIDEV_REMOVE, NULL };
		RegisterRawInputDevices( &device, 1, sizeof( device ) );
		m_bRawInputRegistered = TFALSE;
	}
	m_hMainWindow = NULL;
	ResetRawMotion();
#endif
	if ( m_poDXInputDevice )
	{
		m_poDXInputDevice->Release();
		m_poDXInputDevice = NULL;
	}
}

// $Barnyard: FUNCTION 006d3f30
void TInputDXDeviceMouse::Update( TFLOAT a_fDeltaTime )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	// An unacquired dev mouse must also clear the previous frame's deltas.
	m_iDeltaPositionX = m_iDeltaPositionY = m_iDeltaPositionZ = 0;
	const TBOOL bRawInput = ms_bRawInputEnabled && m_bRawInputRegistered;
	if ( IsAcquired() && !bRawInput )
#else
	if ( IsAcquired() )
#endif
	{
		m_iDeltaPositionX = 0;
		m_iDeltaPositionY = 0;
		m_iDeltaPositionZ = 0;

		HRESULT hRes = m_poDXInputDevice->Poll();

		if ( SUCCEEDED( hRes ) )
		{
			DIMOUSESTATE state;
			hRes = m_poDXInputDevice->GetDeviceState( sizeof( state ), &state );

			if ( SUCCEEDED( hRes ) )
			{
				m_iDeltaPositionX += state.lX;
				m_iPositionX += state.lX;
				m_iDeltaPositionY += state.lY;
				m_iPositionY += state.lY;
				m_iDeltaPositionZ += state.lZ;
				m_iPositionZ += state.lZ;

				if ( TRenderInterface::GetSingleton() )
				{
					auto pDisplayParams = TRenderInterface::GetSingleton()->GetCurrentDisplayParams();

					if ( !pDisplayParams->bWindowed )
					{
						TMath::Clip( m_iPositionX, 0, TINT( pDisplayParams->uiWidth ) );
						TMath::Clip( m_iPositionY, 0, TINT( pDisplayParams->uiHeight ) );
					}
				}
			}
		}
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	// Both physical raw packets and scripted motion drain here exactly once per frame.
	m_iDeltaPositionX += m_iRawDeltaX;
	m_iDeltaPositionY += m_iRawDeltaY;
	m_iDeltaPositionZ += m_iRawDeltaZ;
	m_iPositionZ += m_iRawDeltaZ;
	m_iPositionX += m_iRawDeltaX;
	m_iPositionY += m_iRawDeltaY;
	m_iRawDeltaX = m_iRawDeltaY = m_iRawDeltaZ = 0;

	if ( TRenderInterface* pRenderer = TRenderInterface::GetSingleton() )
	{
		auto pParams = pRenderer->GetCurrentDisplayParams();
		if ( pParams && !pParams->bWindowed )
		{
			TMath::Clip( m_iPositionX, 0, TINT( pParams->uiWidth ) );
			TMath::Clip( m_iPositionY, 0, TINT( pParams->uiHeight ) );
		}
	}
#endif
}

// $Barnyard: FUNCTION 006d3e40
TBOOL TInputDXDeviceMouse::Flush()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	ResetRawMotion();
	if ( ms_bRawInputEnabled && m_bRawInputRegistered ) return TTRUE;
#endif
	DWORD dwItems = INFINITE;

	if ( IsAcquired() )
	{
		HRESULT hr = m_poDXInputDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), NULL, &dwItems, 0 );

		if ( SUCCEEDED( hr ) )
		{
			RefreshDirect();
			return TTRUE;
		}
	}

	return TFALSE;
}

// $Barnyard: FUNCTION 006d4200
TINT TInputDXDeviceMouse::ProcessEvents( EventEmitter& a_rEmitter, TFLOAT a_fDeltaTime )
{
	m_dwButtonPrevious = m_dwButtonCurrent;
	TINT iNumProcessed = ProcessRepeats( a_rEmitter, a_fDeltaTime );

#ifdef BARNYARD_COMMUNITY_PATCH
	DIDEVICEOBJECTDATA dod[ 64 ] = {};
	DWORD dwItems = TARRAYSIZE( dod );
	const TBOOL bRawInput = ms_bRawInputEnabled && m_bRawInputRegistered;
	if ( bRawInput )
	{
		dwItems = m_iNumRawButtonChanges;
		for ( DWORD i = 0; i < dwItems; i++ )
		{
			const TINT iChange = m_aiRawButtonChanges[ i ];
			const TINT iButton = iChange < 0 ? -iChange - 1 : iChange - 1;
			dod[ i ].dwOfs = DIMOFS_BUTTON0 + iButton;
			dod[ i ].dwData = iChange > 0 ? 0x80 : 0;
		}
		m_iNumRawButtonChanges = 0;
	}
	else
	{
		const HRESULT hRes = m_poDXInputDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), dod, &dwItems, 0 );
		if ( FAILED( hRes ) )
		{
			// AcquireAll retries next frame. Scripted motion still emits below when unfocused.
			dwItems = 0;
			SetAcquired( TFALSE );
		}
	}
#else
	DWORD              dwItems = 32;
	DIDEVICEOBJECTDATA dod[ 32 ];
	TUtil::MemClear( dod, sizeof( dod ) );

	HRESULT hRes = m_poDXInputDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), dod, &dwItems, 0 );

	if ( FAILED( hRes ) && hRes == ( DIERR_NOTACQUIRED | DIERR_INPUTLOST ) )
	{
		Unacquire();
		Acquire();

		hRes = m_poDXInputDevice->GetDeviceData( sizeof( DIDEVICEOBJECTDATA ), dod, &dwItems, 0 );

		if ( hRes != DI_OK )
		{
			return 0;
		}
	}
#endif

	for ( DWORD i = 0; i < dwItems; i++ )
	{
		switch ( dod[ i ].dwOfs )
		{
			case DIMOFS_BUTTON0:
			case DIMOFS_BUTTON1:
			case DIMOFS_BUTTON2:
#ifdef BARNYARD_COMMUNITY_PATCH
			case DIMOFS_BUTTON3:
			case DIMOFS_BUTTON4:
#endif
			{
#ifdef BARNYARD_COMMUNITY_PATCH
				TINT iButton = dod[ i ].dwOfs - DIMOFS_BUTTON0;
				if ( !bRawInput && iButton < 2 && m_bReverseButtons ) iButton = 1 - iButton;
#else
				TINT iButton =
				    ( dod[ i ].dwOfs == DIMOFS_BUTTON0 ) ? ( !m_bReverseButtons ? 0 : 1 ) :
				    ( dod[ i ].dwOfs == DIMOFS_BUTTON1 ) ? ( !m_bReverseButtons ? 1 : 0 ) :
				                                           2;
#endif

				TUINT uiButtonFlag = ( 1 << iButton );

#ifdef BARNYARD_COMMUNITY_PATCH
				TASSERT( iButton < 5 );
#else
				TASSERT( iButton < INPUT_DEVICE_MOUSE_BUTTONS );
#endif

				if ( dod[ i ].dwData & 0x80 )
				{
					TASSERT( ( m_dwButtonCurrent & uiButtonFlag ) == 0 );

					if ( ( m_dwButtonCurrent & uiButtonFlag ) == 0 )
					{
						a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_1 + iButton, TInputInterface::EVENT_TYPE_GONE_DOWN ) );
						iNumProcessed += 1;
						m_dwButtonCurrent |= uiButtonFlag;
					}
				}
				else if ( ( m_dwButtonCurrent & uiButtonFlag ) != 0 )
				{
					a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_1 + iButton, TInputInterface::EVENT_TYPE_GONE_UP ) );
					iNumProcessed += 1;
					m_dwButtonCurrent &= ~uiButtonFlag;
				}

				break;
			}
		}
	}

#ifdef BARNYARD_COMMUNITY_PATCH
	// If a burst overflowed the transition queue, still reconcile the final held state.
	if ( bRawInput )
	{
		for ( TINT i = 0; i < 5; i++ )
		{
			const DWORD uiBit = 1U << i;
			if ( ( m_dwButtonCurrent ^ m_dwRawButtons ) & uiBit )
			{
				const TBOOL bDown = ( m_dwRawButtons & uiBit ) != 0;
				a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_1 + i,
				    bDown ? TInputInterface::EVENT_TYPE_GONE_DOWN : TInputInterface::EVENT_TYPE_GONE_UP ) );
				iNumProcessed++;
			}
		}
		m_dwButtonCurrent = m_dwRawButtons;
	}
#endif

#if defined( BARNYARD_COMMUNITY_PATCH ) && !defined( TOSHI_FINAL )
	if ( m_bDevMotionScript )
	{
		if ( m_iDeltaPositionX || m_iDeltaPositionY )
		{
			m_iDevConsumedX += m_iDeltaPositionX;
			m_iDevConsumedY += m_iDeltaPositionY;
			if ( !m_bDevMotionLogged )
				TINFO( "[input/rawmouse] dev motion reached device emitter: raw=%d acquired=%d\n", ms_bRawInputEnabled, IsAcquired() );
			m_bDevMotionLogged = TTRUE;
			m_fDevIdleTime = 0.0f;
		}
		else if ( m_bDevMotionLogged )
		{
			// Sparse subpixel-rate scripts have empty frames; only report after a real idle interval.
			m_fDevIdleTime += a_fDeltaTime;
			if ( m_fDevIdleTime >= 0.5f )
			{
				TINFO( "[input/rawmouse] dev consumed total=(%d,%d) raw=%d\n", m_iDevConsumedX, m_iDevConsumedY, ms_bRawInputEnabled );
				m_bDevMotionLogged = TFALSE;
			}
		}
	}
#endif

	if ( m_iDeltaPositionX != 0 || m_iDeltaPositionY != 0 )
	{
		a_rEmitter.Throw(
		    TInputInterface::InputEvent(
		        this,
		        AXIS_CURSOR,
		        TInputInterface::EVENT_TYPE_MOVED,
		        m_iDeltaPositionX,
		        m_iDeltaPositionY
		    )
		);

		iNumProcessed += 1;
	}

	if ( m_iDeltaPositionZ != 0 )
	{
		// One wheel notch is 120 (WHEEL_DELTA)
		a_rEmitter.Throw( TInputInterface::InputEvent( this, AXIS_WHEEL, TInputInterface::EVENT_TYPE_MOVED, 0.0f, m_iDeltaPositionZ * ( 1.0f / 120.0f ) ) );
		iNumProcessed += 1;

#ifdef BARNYARD_COMMUNITY_PATCH
		// Retail divides by zero if the device reports no wheel granularity
		const TINT iGranularity = bRawInput ? WHEEL_DELTA : m_iWheelGranularity;
		TINT iNumSteps;
		if ( bRawInput )
		{
			// High-resolution wheels can deliver less than one notch in each frame.
			m_iRawWheelRemainder += m_iDeltaPositionZ;
			iNumSteps = m_iRawWheelRemainder / WHEEL_DELTA;
			m_iRawWheelRemainder %= WHEEL_DELTA;
		}
		else iNumSteps = ( iGranularity != 0 ) ? m_iDeltaPositionZ / iGranularity : 0;
#else
		TINT iNumSteps = m_iDeltaPositionZ / m_iWheelGranularity;
#endif

		// Every step of the wheel is a press of the wheel button (held while the further steps repeat)
		if ( iNumSteps < 0 )
		{
			a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_WHEEL_BACKWARD, TInputInterface::EVENT_TYPE_GONE_DOWN ) );

			for ( TINT i = iNumSteps + 1; i < 0; i++ )
				a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_WHEEL_BACKWARD, TInputInterface::EVENT_TYPE_REPEAT ) );

			a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_WHEEL_BACKWARD, TInputInterface::EVENT_TYPE_GONE_UP ) );
			iNumProcessed += 1 - iNumSteps;
		}
		else if ( iNumSteps > 0 )
		{
			a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_WHEEL_FORWARD, TInputInterface::EVENT_TYPE_GONE_DOWN ) );

			for ( TINT i = 1; i < iNumSteps; i++ )
				a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_WHEEL_FORWARD, TInputInterface::EVENT_TYPE_REPEAT ) );

			a_rEmitter.Throw( TInputInterface::InputEvent( this, BUTTON_WHEEL_FORWARD, TInputInterface::EVENT_TYPE_GONE_UP ) );
			iNumProcessed += iNumSteps + 1;
		}
	}

	return iNumProcessed;
}

// $Barnyard: FUNCTION 006d4000
TINT TInputDXDeviceMouse::GetButtonCount() const
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( ms_bRawInputEnabled && m_bRawInputRegistered ) return 5;
#endif
	return m_DIDevCaps.dwButtons;
}

// $Barnyard: FUNCTION 006d4010
TINT TInputDXDeviceMouse::GetAxisCount() const
{
	return m_DIDevCaps.dwAxes;
}

// $Barnyard: FUNCTION 006d4020
TBOOL TInputDXDeviceMouse::IsDown( TINT a_iDoodad ) const
{
	if ( ( 0x30000 < a_iDoodad ) && ( a_iDoodad < 0x30009 ) )
	{
#if defined( BARNYARD_COMMUNITY_PATCH ) && !defined( TOSHI_FINAL )
		if ( Dev_IsButtonDown( a_iDoodad ) )
			return TTRUE;
#endif

		return ( m_dwButtonCurrent & ( 1 << ( ( a_iDoodad - 1 ) & 0x1F ) ) );
	}

	return TFALSE;
}

// $Barnyard: FUNCTION 006d40a0
TINT TInputDXDeviceMouse::GetAxisInt( Doodad a_iDoodad, TINT coord ) const
{
	if ( a_iDoodad == AXIS_CURSOR )
	{
		if ( coord == 0 )
		{
			return m_iDeltaPositionX;
		}

		if ( coord == 1 )
		{
			return m_iDeltaPositionY;
		}
	}

	return 0;
}

// $Barnyard: FUNCTION 006d40d0
TFLOAT TInputDXDeviceMouse::GetAxisFloat( Doodad a_iDoodad, TINT coord ) const
{
	if ( a_iDoodad == AXIS_CURSOR )
	{
		if ( coord == 0 )
		{
			return TFLOAT( m_iDeltaPositionX );
		}

		if ( coord == 1 )
		{
			return TFLOAT( m_iDeltaPositionY );
		}
	}
	else if ( a_iDoodad == AXIS_WHEEL )
	{
		return TFLOAT( m_iDeltaPositionZ );
	}

	return 0.0f;
}

TBOOL TInputDXDeviceMouse::IsEnabled() const
{
	return TTRUE;
}

// $Barnyard: FUNCTION 006d3c50
TBOOL TInputDXDeviceMouse::Initialise()
{
	m_bReverseButtons  = TFALSE;
	m_dwButtonCurrent  = 0;
	m_dwButtonPrevious = 0;

	POINT cursorPos;
	GetCursorPos( &cursorPos );

	m_iPositionX      = cursorPos.x;
	m_iPositionY      = cursorPos.y;
	m_iPositionZ      = 0;
	m_iDeltaPositionX = 0;
	m_iDeltaPositionY = 0;
	m_iDeltaPositionZ = 0;
	m_iUnk1           = 0;
	m_iUnk2           = 0;
	m_iUnk3           = 0;
	m_iUnk4           = 0;
	m_iUnk5           = 0;
	m_iUnk6           = -1;
	m_iUnk7           = -1;

	DIPROPDWORD dwordProperty;
	dwordProperty.diph.dwSize       = sizeof( dwordProperty );
	dwordProperty.diph.dwHeaderSize = sizeof( DIPROPHEADER );
	dwordProperty.diph.dwObj        = DIMOFS_Z;
	dwordProperty.diph.dwHow        = DIPH_BYOFFSET;

	HRESULT hr = m_poDXInputDevice->GetProperty( DIPROP_GRANULARITY, &dwordProperty.diph );

	if ( hr != DIERR_OBJECTNOTFOUND )
	{
		m_iWheelGranularity = ( hr == DI_OK ) ? dwordProperty.dwData : 0;
		return TTRUE;
	}

	m_iWheelGranularity = 0;
	return TTRUE;
}

TBOOL TInputDXDeviceMouse::Deinitialise()
{
	Release();
	return TTRUE;
}

// $Barnyard: FUNCTION 006d3e80
void TInputDXDeviceMouse::RefreshDirect()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( ms_bRawInputEnabled && m_bRawInputRegistered ) return;
#endif
	if ( IsAcquired() )
	{
		m_dwButtonPrevious = m_dwButtonCurrent;
		m_dwButtonCurrent  = 0;

		if ( SUCCEEDED( m_poDXInputDevice->Poll() ) )
		{
			DIMOUSESTATE mouseState;
			HRESULT      hRes = m_poDXInputDevice->GetDeviceState( sizeof( DIMOUSESTATE ), &mouseState );

			if ( SUCCEEDED( hRes ) )
			{
				if ( mouseState.rgbButtons[ 0 ] & 0x80 ) // LeftClick
					m_dwButtonCurrent |= m_bReverseButtons ? 2 : 1;

				if ( mouseState.rgbButtons[ 1 ] & 0x80 ) // RightClick
					m_dwButtonCurrent |= m_bReverseButtons ? 1 : 2;

				if ( mouseState.rgbButtons[ 2 ] & 0x80 ) // Wheeling yeeet
					m_dwButtonCurrent |= INPUT_DEVICE_MOUSE_WHEEL;
			}
		}
	}
}

// $Barnyard: FUNCTION 006d4060
TBOOL TInputDXDeviceMouse::WasDown( TINT a_iDoodad ) const
{
	if ( ( 0x30000 < a_iDoodad ) && ( a_iDoodad < 0x30009 ) )
	{
		return ( m_dwButtonPrevious & ( ( a_iDoodad - 1 ) & 0x1F ) );
	}

	return TFALSE;
}

// $Barnyard: FUNCTION 006d4110
void TInputDXDeviceMouse::SetExclusive( HWND a_hWindow, TBOOL a_bExclusive )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	m_bRawExclusive = a_bExclusive;
	if ( !a_bExclusive ) ReleaseRawCursor();
#endif
	m_poDXInputDevice->SetCooperativeLevel(
	    a_hWindow,
	    a_bExclusive ? ( DISCL_EXCLUSIVE | DISCL_FOREGROUND ) : ( DISCL_NONEXCLUSIVE | DISCL_FOREGROUND )
	);
}

// $Barnyard: FUNCTION 006d4140
void TInputDXDeviceMouse::SetCurrentPosition( TINT a_iX, TINT a_iY )
{
	auto pDisplayParams = TRenderInterface::GetSingleton()->GetCurrentDisplayParams();

	TMath::Clip( m_iPositionX, 0, TINT( pDisplayParams->uiWidth ) );
	TMath::Clip( m_iPositionY, 0, TINT( pDisplayParams->uiHeight ) );
}

// $Barnyard: FUNCTION 006d3d00
TBOOL const TInputDXDeviceMouse::BindToDIDevice( HWND a_hMainWindow, LPCDIDEVICEINSTANCEA a_poDeviceInstance, IDirectInputDevice8A* a_poDXInputDevice, TBOOL a_bExclusive )
{
	TASSERT( a_poDeviceInstance != NULL );
	TASSERT( a_poDXInputDevice != NULL );

	Release();
	TUtil::MemCopy( &m_oDeviceInstance, a_poDeviceInstance, sizeof( DIDEVICEINSTANCEA ) );

	m_poDXInputDevice = a_poDXInputDevice;

	m_DIDevCaps = { sizeof( DIDEVCAPS ) };
	m_poDXInputDevice->GetCapabilities( &m_DIDevCaps );

	HRESULT hRes = m_poDXInputDevice->SetDataFormat( &c_dfDIMouse );

	if ( hRes != DI_OK )
		return TFALSE;

	if ( a_hMainWindow )
	{
		m_poDXInputDevice->SetCooperativeLevel( a_hMainWindow, DISCL_FOREGROUND | ( a_bExclusive ? DISCL_EXCLUSIVE : DISCL_NONEXCLUSIVE ) );
	}

	DIPROPDWORD dipdw{};
	dipdw.diph.dwSize       = sizeof( DIPROPDWORD );
	dipdw.diph.dwHeaderSize = sizeof( DIPROPHEADER );
	dipdw.diph.dwObj        = 0;
	dipdw.diph.dwHow        = DIPH_DEVICE;
	dipdw.dwData            = DIPROPAXISMODE_REL;
	hRes                    = m_poDXInputDevice->SetProperty( DIPROP_AXISMODE, &dipdw.diph );

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( hRes == DI_OK )
	{
		m_hMainWindow = a_hMainWindow;
		m_bRawExclusive = a_bExclusive;
		m_bRawBackground = T2String8::FindString( GetCommandLineA(), "-background" ) != TNULL;
		ConfigureRawInput();
	}
#endif

	return hRes == DI_OK;
}

TOSHI_NAMESPACE_END

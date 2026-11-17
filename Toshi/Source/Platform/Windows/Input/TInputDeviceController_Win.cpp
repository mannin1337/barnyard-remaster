#include "ToshiPCH.h"
#include "TInputDeviceController_Win.h"
#ifdef BARNYARD_COMMUNITY_PATCH
#  include "TInputDeviceController_XInput.h"
#endif

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include "Core/TMemoryDebugOn.h"

TOSHI_NAMESPACE_START

// NOTE: GetClass 006d4560, CreateObject 006d4940, CreateObjectInPlace 006d49a0
TDEFINE_CLASS_NORUNTIME( TInputDXDeviceController );

// $Barnyard: FUNCTION 006d4570
TInputDXDeviceController::TInputDXDeviceController()
{
	m_poDXInputDevice = TNULL;
	TUtil::MemClear( &m_oJoyState, sizeof( m_oJoyState ) );
	TUtil::MemClear( &m_oOldJoyState, sizeof( m_oOldJoyState ) );
	m_bPolled = TFALSE;
#ifdef BARNYARD_COMMUNITY_PATCH
	m_bConnected = m_bEnumerated = TFALSE;
	m_iPortIndex = -1;
	m_hMainWindow = NULL;
	TUtil::MemClear( &m_oDeviceInstance, sizeof( m_oDeviceInstance ) );
	ClearState( TFALSE );
#endif
}

// $Barnyard: FUNCTION 006d49f0
TInputDXDeviceController::~TInputDXDeviceController()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	Release();
#endif
}

// $Barnyard: FUNCTION 006d4aa0
TBOOL TInputDXDeviceController::Acquire()
{
	m_bPolled = TFALSE;
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( !m_poDXInputDevice || !m_bConnected || !m_hMainWindow || GetForegroundWindow() != m_hMainWindow )
	{
		Unacquire();
		return TFALSE;
	}
	HRESULT hResult = m_poDXInputDevice->Acquire();
	if ( FAILED( hResult ) )
	{
		StopNativeRumble();
		ClearState( IsAcquired() );
		SetAcquired( TFALSE );
		// A rapid unplug/replug can occur between discovery scans. Recreate
		// the native handle on input loss while retaining this player identity.
		if ( hResult == DIERR_INPUTLOST || hResult == DIERR_UNPLUGGED ) Release();
		return TFALSE;
	}
	SetAcquired( TTRUE );
	return TTRUE;
#else
	if ( FAILED( m_poDXInputDevice->Acquire() ) )
	{
		if ( IsAcquired() )
		{
			TInputInterface::DeviceStatusEvent oEvent = { this, TInputInterface::DEVICESTATUS_DISCONNECTED };
			GetInputInterface()->GetDeviceStatusEmitter().Throw( &oEvent );
		}

		SetAcquired( TFALSE );
		return TFALSE;
	}

	if ( !IsAcquired() )
	{
		TInputInterface::DeviceStatusEvent oEvent = { this, TInputInterface::DEVICESTATUS_CONNECTED };
		GetInputInterface()->GetDeviceStatusEmitter().Throw( &oEvent );
	}

	SetAcquired( TTRUE );
	return TTRUE;
#endif
}

// $Barnyard: FUNCTION 006d4b20
TBOOL TInputDXDeviceController::Unacquire()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	StopNativeRumble();
	ClearState( IsAcquired() );
	SetAcquired( TFALSE );
	return !m_poDXInputDevice || SUCCEEDED( m_poDXInputDevice->Unacquire() );
#else
	SetAcquired( TFALSE );
	m_bPolled = TFALSE;

	return SUCCEEDED( m_poDXInputDevice->Unacquire() );
#endif
}

// $Barnyard: FUNCTION 006d45c0
void TInputDXDeviceController::Release()
{
#ifdef BARNYARD_COMMUNITY_PATCH
	StopNativeRumble();
	m_oNativeRumble.Reset();
	m_bRumblePathQueried = TFALSE;
	m_wszRumblePath[ 0 ] = 0;
#endif
	if ( m_poDXInputDevice )
	{
#ifdef BARNYARD_COMMUNITY_PATCH
		m_poDXInputDevice->Unacquire();
#endif
		m_poDXInputDevice->Release();
		m_poDXInputDevice = TNULL;
	}
}

// $Barnyard: FUNCTION 006d4cf0
void TInputDXDeviceController::Update( TFLOAT a_fDeltaTime )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( !CanUseNativeRumble() ) StopNativeRumble();
	m_bPolled = TFALSE;
	DIJOYSTATE oState;
	if ( !m_poDXInputDevice || FAILED( m_poDXInputDevice->Poll() ) ||
	     FAILED( m_poDXInputDevice->GetDeviceState( sizeof( oState ), &oState ) ) )
	{
		ClearState( IsAcquired() );
		SetAcquired( TFALSE );
		Release();
		return;
	}
	m_oOldJoyState = m_oJoyState;
	m_oJoyState = oState;
	if ( IsSonyController() )
		RemapSonyState( m_oJoyState );
#else
	if ( FAILED( m_poDXInputDevice->Poll() ) )
		return;
	m_oOldJoyState = m_oJoyState;
	if ( FAILED( m_poDXInputDevice->GetDeviceState( sizeof( DIJOYSTATE ), &m_oJoyState ) ) )
		return;
#endif

	m_afSticks[ 0 ] = NormaliseStick( m_oJoyState.lX );
	m_afSticks[ 1 ] = NormaliseStick( m_oJoyState.lY );
	m_afSticks[ 2 ] = NormaliseStick( m_oJoyState.lZ );
	m_afSticks[ 3 ] = NormaliseStick( m_oJoyState.lRz );

	UpdateVirtualButtons();
	m_bPolled = TTRUE;
}

// $Barnyard: FUNCTION 006d4be0
TINT TInputDXDeviceController::ProcessEvents( EventEmitter& a_rEmitter, TFLOAT a_fDeltaTime )
{
	if ( !m_bPolled )
		return 0;

	TINT iNumProcessed = ProcessRepeats( a_rEmitter, a_fDeltaTime );
	iNumProcessed += ProcessVirtualButtons( a_rEmitter, a_fDeltaTime );

	for ( TINT i = 0; i < NUM_DX_BUTTONS; i++ )
	{
		if ( ( m_oJoyState.rgbButtons[ i ] & 0x80 ) && !( m_oOldJoyState.rgbButtons[ i ] & 0x80 ) )
		{
			a_rEmitter.Throw( TInputInterface::InputEvent( this, TranslateDXButtonToDoodad( i ), TInputInterface::EVENT_TYPE_GONE_DOWN ) );
			iNumProcessed++;
		}
		else if ( !( m_oJoyState.rgbButtons[ i ] & 0x80 ) && ( m_oOldJoyState.rgbButtons[ i ] & 0x80 ) )
		{
			a_rEmitter.Throw( TInputInterface::InputEvent( this, TranslateDXButtonToDoodad( i ), TInputInterface::EVENT_TYPE_GONE_UP ) );
			iNumProcessed++;
		}
	}

	a_rEmitter.Throw( TInputInterface::InputEvent( this, STICK_LEFT, TInputInterface::EVENT_TYPE_VIRTUAL, m_afSticks[ 0 ], m_afSticks[ 1 ] ) );
	a_rEmitter.Throw( TInputInterface::InputEvent( this, STICK_RIGHT, TInputInterface::EVENT_TYPE_VIRTUAL, m_afSticks[ 2 ], m_afSticks[ 3 ] ) );

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( IsSonyController() )
	{
		a_rEmitter.Throw( TInputInterface::InputEvent( this, TRIGGER_LEFT, TInputInterface::EVENT_TYPE_VIRTUAL, GetAxisFloat( TRIGGER_LEFT, 0 ) ) );
		a_rEmitter.Throw( TInputInterface::InputEvent( this, TRIGGER_RIGHT, TInputInterface::EVENT_TYPE_VIRTUAL, GetAxisFloat( TRIGGER_RIGHT, 0 ) ) );
		iNumProcessed += 2;
	}
#endif
	return iNumProcessed + 2;
}

// $Barnyard: FUNCTION 006d47c0
TBOOL TInputDXDeviceController::IsDown( Doodad a_iDoodad ) const
{
	TINT iButton = TranslateDoodadToDXButton( a_iDoodad );

	if ( iButton < 0 )
		return TInputDeviceController::IsDown( a_iDoodad );

	return m_oJoyState.rgbButtons[ iButton ] >> 7;
}

// $Barnyard: FUNCTION 006d47f0
TBOOL TInputDXDeviceController::WasDown( Doodad a_iDoodad ) const
{
	TINT iButton = TranslateDoodadToDXButton( a_iDoodad );

	if ( iButton < 0 )
		return TInputDeviceController::WasDown( a_iDoodad );

	return m_oOldJoyState.rgbButtons[ iButton ] >> 7;
}

// $Barnyard: FUNCTION 006d4820
TINT TInputDXDeviceController::GetAxisInt( Doodad a_iDoodad, TINT a_iAxis ) const
{
#ifdef BARNYARD_COMMUNITY_PATCH
	TINT iValue = GetAxisInt2( a_iDoodad, a_iAxis );
	if ( a_iDoodad == TRIGGER_LEFT || a_iDoodad == TRIGGER_RIGHT )
		return TMath::Max( iValue - 30, 0 );
	return ( iValue <= STICK_DEADZONE && iValue >= -STICK_DEADZONE ) ? 0 : iValue;
#else
	TINT iValue;

	if ( a_iDoodad == STICK_LEFT )
	{
		if ( a_iAxis == 0 )
			iValue = m_oJoyState.lX;
		else if ( a_iAxis == 1 )
			iValue = m_oJoyState.lY;
		else
			return 0;
	}
	else if ( a_iDoodad == STICK_RIGHT )
	{
		if ( a_iAxis == 0 )
			iValue = m_oJoyState.lZ;
		else if ( a_iAxis == 1 )
			iValue = m_oJoyState.lRz;
		else
			return 0;
	}
	else
	{
		return 0;
	}

	// NOTE: retail centres the axes on STICK_SATURATE, not STICK_CENTRE
	TINT iResult = iValue - STICK_SATURATE;

	if ( iResult <= STICK_DEADZONE && iResult >= -STICK_DEADZONE )
		return 0;

	return iResult;
#endif
}

// $Barnyard: FUNCTION 006d48b0
TINT TInputDXDeviceController::GetAxisInt2( Doodad a_iDoodad, TINT a_iAxis ) const
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( a_iAxis == 0 && IsSonyController() )
	{
		// Native Sony USB and Bluetooth HID: X/Y = left, Z/Rz = right,
		// Rx/Ry = independent L2/R2. DirectInput scales each to 0..65535.
		if ( a_iDoodad == TRIGGER_LEFT ) return TMath::Min( TMath::Max( TINT( m_oJoyState.lRx / 257 ), 0 ), 255 );
		if ( a_iDoodad == TRIGGER_RIGHT ) return TMath::Min( TMath::Max( TINT( m_oJoyState.lRy / 257 ), 0 ), 255 );
	}
	TINT iValue;
	if ( a_iDoodad == STICK_LEFT && a_iAxis == 0 ) iValue = m_oJoyState.lX;
	else if ( a_iDoodad == STICK_LEFT && a_iAxis == 1 ) iValue = m_oJoyState.lY;
	else if ( a_iDoodad == STICK_RIGHT && a_iAxis == 0 ) iValue = m_oJoyState.lZ;
	else if ( a_iDoodad == STICK_RIGHT && a_iAxis == 1 ) iValue = m_oJoyState.lRz;
	else return 0;
	iValue -= STICK_CENTRE;
	TMath::Clip( iValue, -STICK_SATURATE, STICK_SATURATE );
	return iValue;
#else
	if ( a_iDoodad == STICK_LEFT )
	{
		if ( a_iAxis == 0 ) return m_oJoyState.lX - STICK_SATURATE;
		if ( a_iAxis == 1 ) return m_oJoyState.lY - STICK_SATURATE;
	}
	else if ( a_iDoodad == STICK_RIGHT )
	{
		if ( a_iAxis == 0 ) return m_oJoyState.lZ - STICK_SATURATE;
		if ( a_iAxis == 1 ) return m_oJoyState.lRz - STICK_SATURATE;
	}
	return 0;
#endif
}

// $Barnyard: FUNCTION 006d4890
TFLOAT TInputDXDeviceController::GetAxisFloat( Doodad a_iDoodad, TINT a_iAxis ) const
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( a_iDoodad == TRIGGER_LEFT || a_iDoodad == TRIGGER_RIGHT )
		return GetAxisInt( a_iDoodad, a_iAxis ) * ( 1.0f / 225.0f );
	if ( ( a_iDoodad == STICK_LEFT || a_iDoodad == STICK_RIGHT ) && ( a_iAxis == 0 || a_iAxis == 1 ) )
		return NormaliseStick( GetAxisInt2( a_iDoodad, a_iAxis ) + STICK_CENTRE );
	return 0.0f;
#else
	return GetAxisInt( a_iDoodad, a_iAxis ) * ( 1.0f / STICK_SATURATE );
#endif
}

// $Barnyard: FUNCTION 006d4920
TFLOAT TInputDXDeviceController::GetAxisFloat2( Doodad a_iDoodad, TINT a_iAxis ) const
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( a_iDoodad == TRIGGER_LEFT || a_iDoodad == TRIGGER_RIGHT )
		return GetAxisInt2( a_iDoodad, a_iAxis ) * ( 1.0f / 255.0f );
#endif
	return GetAxisInt2( a_iDoodad, a_iAxis ) * ( 1.0f / STICK_SATURATE );
}

// $Barnyard: FUNCTION 006d4a20
TBOOL const TInputDXDeviceController::BindToDIDevice( HWND a_hMainWindow, LPCDIDEVICEINSTANCEA a_poDeviceInstance, IDirectInputDevice8A* a_poDXInputDevice )
{
	Initialise();
#ifdef BARNYARD_COMMUNITY_PATCH
	Release();
	m_hMainWindow = a_hMainWindow;
#endif
	TUtil::MemCopy( &m_oDeviceInstance, a_poDeviceInstance, sizeof( DIDEVICEINSTANCEA ) );
	m_poDXInputDevice = a_poDXInputDevice;

	m_oDIDevCaps.dwSize = sizeof( DIDEVCAPS );
	a_poDXInputDevice->GetCapabilities( &m_oDIDevCaps );

	if ( m_poDXInputDevice->SetDataFormat( &c_dfDIJoystick ) != DI_OK )
		return TFALSE;

#ifdef BARNYARD_COMMUNITY_PATCH
	if ( !a_hMainWindow || FAILED( m_poDXInputDevice->SetCooperativeLevel( a_hMainWindow, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE ) ) )
		return TFALSE;
	ClearState( TFALSE );
#endif
	m_poDXInputDevice->EnumObjects( EnumerateObjectCallback, this, DIDFT_ALL );
	m_bPolled = TFALSE;

	return TTRUE;
}

// $Barnyard: FUNCTION 006d45e0
TInputDevice::Doodad TInputDXDeviceController::TranslateDXButtonToDoodad( TINT a_iButton )
{
	switch ( a_iButton )
	{
		case 0: return BUTTON_CONTEXT_1;
		case 1: return BUTTON_CONTEXT_2;
		case 2: return BUTTON_CONTEXT_3;
		case 3: return BUTTON_CONTEXT_4;
		case 4: return BUTTON_SHOULDER_LEFT_2;
		case 5: return BUTTON_SHOULDER_RIGHT_2;
		case 6: return BUTTON_SHOULDER_LEFT_1;
		case 7: return BUTTON_SHOULDER_RIGHT_1;
		case 8: return BUTTON_SELECT;
		case 9: return BUTTON_STICK_LEFT;
		case 10: return BUTTON_STICK_RIGHT;
		case 11: return BUTTON_START;
		case 12: return BUTTON_DPAD_UP;
		case 13: return BUTTON_DPAD_RIGHT;
		case 14: return BUTTON_DPAD_DOWN;
		case 15: return BUTTON_DPAD_LEFT;
		default: return 0;
	}
}

// $Barnyard: FUNCTION 006d46c0
TINT TInputDXDeviceController::TranslateDoodadToDXButton( Doodad a_iDoodad )
{
	switch ( a_iDoodad )
	{
		case BUTTON_DPAD_UP: return 12;
		case BUTTON_DPAD_RIGHT: return 13;
		case BUTTON_DPAD_DOWN: return 14;
		case BUTTON_DPAD_LEFT: return 15;
		case BUTTON_SELECT: return 8;
		case BUTTON_START: return 11;
		case BUTTON_STICK_LEFT: return 9;
		case BUTTON_STICK_RIGHT: return 10;
		case BUTTON_CONTEXT_1: return 0;
		case BUTTON_CONTEXT_2: return 1;
		case BUTTON_CONTEXT_3: return 2;
		case BUTTON_CONTEXT_4: return 3;
		case BUTTON_SHOULDER_LEFT_1: return 6;
		case BUTTON_SHOULDER_LEFT_2: return 4;
		case BUTTON_SHOULDER_RIGHT_1: return 7;
		case BUTTON_SHOULDER_RIGHT_2: return 5;
		default: return -1;
	}
}

// $Barnyard: FUNCTION 006d4b40
TFLOAT TInputDXDeviceController::NormaliseStick( TINT a_iValue )
{
	a_iValue -= STICK_CENTRE;

	if ( a_iValue < 0 )
	{
		a_iValue = TMath::Min( a_iValue, -STICK_DEADZONE );
		a_iValue = TMath::Max( a_iValue, -STICK_SATURATE );
		return ( a_iValue + STICK_DEADZONE ) * ( 1.0f / ( STICK_SATURATE - STICK_DEADZONE ) );
	}

	a_iValue = TMath::Max( a_iValue, STICK_DEADZONE );
	a_iValue = TMath::Min( a_iValue, STICK_SATURATE );
	return ( a_iValue - STICK_DEADZONE ) * ( 1.0f / ( STICK_SATURATE - STICK_DEADZONE ) );
}

// $Barnyard: FUNCTION 006d45b0
BOOL CALLBACK TInputDXDeviceController::EnumerateObjectCallback( LPCDIDEVICEOBJECTINSTANCEA a_poDeviceInstance, LPVOID a_pData )
{
#ifdef BARNYARD_COMMUNITY_PATCH
	if ( a_poDeviceInstance->dwType & DIDFT_ABSAXIS )
	{
		auto pController = static_cast<TInputDXDeviceController*>( a_pData );
		DIPROPRANGE oRange = {};
		oRange.diph.dwSize = sizeof( oRange );
		oRange.diph.dwHeaderSize = sizeof( DIPROPHEADER );
		oRange.diph.dwObj = a_poDeviceInstance->dwType;
		oRange.diph.dwHow = DIPH_BYID;
		oRange.lMin = 0;
		oRange.lMax = 65535;
		pController->m_poDXInputDevice->SetProperty( DIPROP_RANGE, &oRange.diph );
		DIPROPDWORD oDeadzone = {};
		oDeadzone.diph = oRange.diph;
		oDeadzone.diph.dwSize = sizeof( oDeadzone );
		pController->m_poDXInputDevice->SetProperty( DIPROP_DEADZONE, &oDeadzone.diph );
	}
#endif
	return DIENUM_CONTINUE;
}

#ifdef BARNYARD_COMMUNITY_PATCH

void TInputDXDeviceController::ClearState( TBOOL a_bSendReleaseEvents )
{
	m_oOldJoyState = m_oJoyState;
	TUtil::MemClear( &m_oJoyState, sizeof( m_oJoyState ) );
	m_oJoyState.lX = m_oJoyState.lY = m_oJoyState.lZ = m_oJoyState.lRz = STICK_CENTRE;
	for ( TINT i = 0; i < 4; i++ ) m_oJoyState.rgdwPOV[ i ] = 0xFFFFFFFF;
	TUtil::MemClear( m_afSticks, sizeof( m_afSticks ) );
	StopAllRepeats();
	UpdateVirtualButtons();
	if ( a_bSendReleaseEvents && GetInputInterface() )
	{
		m_bPolled = TTRUE;
		ProcessEvents( GetInputInterface()->GetInputEventEmitter(), 0.0f );
	}
	m_oOldJoyState = m_oJoyState;
	ResetVirtualButtons();
	m_bPolled = TFALSE;
}

void TInputDXDeviceController::SetConnected( TBOOL a_bConnected )
{
	if ( m_bConnected == a_bConnected ) return;
	if ( !a_bConnected )
	{
		Unacquire();
		Release();
	}
	m_bConnected = a_bConnected;
	TINFO( "Input: DirectInput port %d %s '%s' (object retained)\n", m_iPortIndex, a_bConnected ? "connected" : "disconnected", m_oDeviceInstance.tszProductName );
	if ( GetInputInterface() )
	{
		TInputInterface::DeviceStatusEvent oEvent( this, a_bConnected ? TInputInterface::DEVICESTATUS_CONNECTED : TInputInterface::DEVICESTATUS_DISCONNECTED );
		GetInputInterface()->GetDeviceStatusEmitter().Throw( &oEvent );
	}
}

TBOOL TInputDXDeviceController::CanUseNativeRumble()
{
	if ( TInputDualSenseRumble::IsOutputDisabled() || !m_bConnected || !IsAcquired() || !m_poDXInputDevice ||
	     !m_hMainWindow || GetForegroundWindow() != m_hMainWindow ||
	     m_oDeviceInstance.guidProduct.Data1 != MAKELONG( 0x054C, 0x0CE6 ) || !GetInputInterface() )
		return TFALSE;

	// A physical Sony pad and its emulated Xbox pad cannot be correlated here.
	// Conservatively prefer XInput whenever any of its slots is connected. This
	// also disables native rumble for a mixed physical Xbox + DualSense pair.
	for ( TUINT i = 0; i < 4; ++i )
	{
		auto pController = GetInputInterface()->GetDeviceByIndex<TInputXInputDeviceController>( i );
		if ( pController && pController->IsConnected() ) return TFALSE;
	}
	return TTRUE;
}

TBOOL TInputDXDeviceController::IsForceFeedbackDevice()
{
	return CanUseNativeRumble();
}

void TInputDXDeviceController::StopNativeRumble()
{
	m_afVibration[ 0 ] = m_afVibration[ 1 ] = 0.0f;
	m_oNativeRumble.Stop();
}

void TInputDXDeviceController::SetVibration( VIBRATION_ACTUATOR a_eActuator, TFLOAT a_fValue, TBOOL )
{
	if ( a_eActuator != VIBRATION_ACTUATOR_0 && a_eActuator != VIBRATION_ACTUATOR_1 ) return;
	if ( !CanUseNativeRumble() )
	{
		StopNativeRumble();
		return;
	}
	m_afVibration[ a_eActuator ] = a_fValue;
	if ( !( m_afVibration[ 0 ] > 0.0f ) && !( m_afVibration[ 1 ] > 0.0f ) )
	{
		m_oNativeRumble.Stop();
		return;
	}
	if ( !m_bRumblePathQueried )
	{
		m_bRumblePathQueried = TTRUE;
		DIPROPGUIDANDPATH oPath = {};
		oPath.diph.dwSize = sizeof( oPath );
		oPath.diph.dwHeaderSize = sizeof( DIPROPHEADER );
		oPath.diph.dwHow = DIPH_DEVICE;
		if ( SUCCEEDED( m_poDXInputDevice->GetProperty( DIPROP_GUIDANDPATH, &oPath.diph ) ) )
		{
			oPath.wszPath[ MAX_PATH - 1 ] = 0;
			// Never route native HID output to an XInput interface.
			for ( const WCHAR* p = oPath.wszPath; *p; ++p )
				if ( _wcsnicmp( p, L"IG_", 3 ) == 0 ) return;
			wcscpy_s( m_wszRumblePath, oPath.wszPath );
		}
	}
	if ( m_wszRumblePath[ 0 ] )
		m_oNativeRumble.SetVibration( m_hMainWindow, m_wszRumblePath, m_afVibration[ 0 ], m_afVibration[ 1 ] );
}

TBOOL TInputDXDeviceController::IsSonyController() const
{
	if ( LOWORD( m_oDeviceInstance.guidProduct.Data1 ) != 0x054C )
		return TFALSE;

	// DualSense, DualSense Edge, DualShock 4 v1/v2 and its wireless adapter
	switch ( HIWORD( m_oDeviceInstance.guidProduct.Data1 ) )
	{
		case 0x0CE6:
		case 0x0DF2:
		case 0x05C4:
		case 0x09CC:
		case 0x0BA0:
			return TTRUE;
		default:
			return TFALSE;
	}
}

void TInputDXDeviceController::RemapSonyState( DIJOYSTATE& a_rState )
{
	// DualSense/DualShock 4 button index -> retail layout index (-1: unused, PS and touchpad)
	static constexpr TINT s_aRemap[] = {
		3,  // Square
		2,  // Cross
		1,  // Circle
		0,  // Triangle
		6,  // L1
		7,  // R1
		4,  // L2
		5,  // R2
		8,  // Create/Share -> Select
		11, // Options -> Start
		9,  // L3
		10, // R3
		-1, // PS
		-1, // Touchpad
	};

	BYTE aButtons[ NUM_DX_BUTTONS ] = {};

	for ( TINT i = 0; i < TARRAYSIZE( s_aRemap ); i++ )
	{
		if ( s_aRemap[ i ] >= 0 )
			aButtons[ s_aRemap[ i ] ] = a_rState.rgbButtons[ i ];
	}

	// Treat analog trigger pressure consistently with XInput, while preserving
	// the HID digital trigger bits for firmware that supplies only buttons.
	if ( a_rState.lRx > 30 * 257 ) aButtons[ 4 ] = 0x80;
	if ( a_rState.lRy > 30 * 257 ) aButtons[ 5 ] = 0x80;

	// D-pad comes as a hat in hundredths of degrees, diagonals press both directions
	DWORD uiPOV = a_rState.rgdwPOV[ 0 ];

	if ( uiPOV < 36000 && LOWORD( uiPOV ) != 0xFFFF )
	{
		if ( uiPOV >= 31500 || uiPOV <= 4500 ) aButtons[ 12 ] = 0x80;
		if ( uiPOV >= 4500 && uiPOV <= 13500 ) aButtons[ 13 ] = 0x80;
		if ( uiPOV >= 13500 && uiPOV <= 22500 ) aButtons[ 14 ] = 0x80;
		if ( uiPOV >= 22500 && uiPOV <= 31500 ) aButtons[ 15 ] = 0x80;
	}

	TUtil::MemCopy( a_rState.rgbButtons, aButtons, sizeof( aButtons ) );
	TUtil::MemClear( a_rState.rgbButtons + NUM_DX_BUTTONS, sizeof( a_rState.rgbButtons ) - NUM_DX_BUTTONS );
}

#endif // BARNYARD_COMMUNITY_PATCH

TOSHI_NAMESPACE_END

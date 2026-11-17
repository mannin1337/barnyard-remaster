#pragma once
#include "Includes.h"
#include "Input/TInputDeviceController.h"
#ifdef BARNYARD_COMMUNITY_PATCH
#  include "TInputDualSenseRumble_Win.h"
#endif

TOSHI_NAMESPACE_START

// DirectInput joystick/gamepad (created by TInputDXInterface::EnumerateDeviceCallback for DI8DEVTYPE_JOYSTICK and DI8DEVTYPE_GAMEPAD)
class TInputDXDeviceController : public TInputDeviceController
{
public:
	TDECLARE_CLASS( TInputDXDeviceController, TInputDeviceController );

	static constexpr TINT NUM_DX_BUTTONS = 16;

	static constexpr TINT STICK_CENTRE   = 32768;
	static constexpr TINT STICK_DEADZONE = 7500;
	static constexpr TINT STICK_SATURATE = 32000;

public:
	TInputDXDeviceController();
	~TInputDXDeviceController();

	//-----------------------------------------------------------------------------
	// TInputDevice
	//-----------------------------------------------------------------------------
	virtual TBOOL Acquire() override;
	virtual TBOOL Unacquire() override;
	virtual void  Release() override;
	virtual void  Update( TFLOAT a_fDeltaTime ) override;
	// RETURNS_TRUE
	virtual TBOOL Flush() override { return TTRUE; }
	virtual TINT  ProcessEvents( EventEmitter& a_rEmitter, TFLOAT a_fDeltaTime ) override;
	// $Barnyard: FUNCTION 006d47a0
#ifdef BARNYARD_COMMUNITY_PATCH
	virtual TINT GetButtonCount() const override { return 16; }
#else
	virtual TINT GetButtonCount() const override { return 12; }
#endif
	// NOTE: folded with TD3DAdapter::GetNumSupportedDevices (006d47b0)
#ifdef BARNYARD_COMMUNITY_PATCH
	virtual TINT   GetAxisCount() const override { return IsSonyController() ? 6 : 4; }
#else
	virtual TINT   GetAxisCount() const override { return 2; }
#endif
	virtual TBOOL  IsDown( Doodad a_iDoodad ) const override;
	virtual TINT   GetAxisInt( Doodad a_iDoodad, TINT a_iAxis ) const override;
	virtual TINT   GetAxisInt2( Doodad a_iDoodad, TINT a_iAxis ) const override;
	virtual TFLOAT GetAxisFloat( Doodad a_iDoodad, TINT a_iAxis ) const override;
	virtual TFLOAT GetAxisFloat2( Doodad a_iDoodad, TINT a_iAxis ) const override;
	// RETURNS_TRUE
	virtual TBOOL IsEnabled() const override { return TTRUE; }

	//-----------------------------------------------------------------------------
	// TInputDeviceController
	//-----------------------------------------------------------------------------
	virtual TBOOL WasDown( Doodad a_iDoodad ) const override;
	// NOTE: the folded stub 004ee130
#ifdef BARNYARD_COMMUNITY_PATCH
	virtual TINT GetPortIndex() override { return m_iPortIndex; }
	virtual TBOOL IsConnected() const override { return m_bConnected; }
	virtual TBOOL IsForceFeedbackDevice() override;
	virtual void SetVibration( VIBRATION_ACTUATOR a_eActuator, TFLOAT a_fValue, TBOOL a_bUnknown ) override;
	void SetPortIndex( TINT a_iPortIndex ) { m_iPortIndex = a_iPortIndex; }
	const GUID& GetInstanceGuid() const { return m_oDeviceInstance.guidInstance; }
	TBOOL HasNativeDevice() const { return m_poDXInputDevice != TNULL; }
	void SetConnected( TBOOL a_bConnected );
	void MarkEnumerated( TBOOL a_bPresent ) { m_bEnumerated = a_bPresent; }
	TBOOL WasEnumerated() const { return m_bEnumerated; }
#else
	virtual TINT GetPortIndex() override { return -1; }
#endif

	//-----------------------------------------------------------------------------
	// Own methods
	//-----------------------------------------------------------------------------
	// RETURNS_TRUE
	virtual TBOOL Initialise() { return TTRUE; } // +0x74
	// RETURNS_TRUE
	virtual TBOOL Deinitialise() { return TTRUE; } // +0x78

	TBOOL const BindToDIDevice( HWND a_hMainWindow, LPCDIDEVICEINSTANCEA a_poDeviceInstance, IDirectInputDevice8A* a_poDXInputDevice );

	static Doodad TranslateDXButtonToDoodad( TINT a_iButton );
	static TINT   TranslateDoodadToDXButton( Doodad a_iDoodad );

private:
	friend class TInputXInputDeviceController;

	static TFLOAT        NormaliseStick( TINT a_iValue );
	static BOOL CALLBACK EnumerateObjectCallback( LPCDIDEVICEOBJECTINSTANCEA a_poDeviceInstance, LPVOID a_pData );

#ifdef BARNYARD_COMMUNITY_PATCH
	void        ClearState( TBOOL a_bSendReleaseEvents );
	TBOOL       IsSonyController() const;
	TBOOL       CanUseNativeRumble();
	void        StopNativeRumble();
	static void RemapSonyState( DIJOYSTATE& a_rState );
#endif

private:
	DIDEVICEINSTANCEA     m_oDeviceInstance; // 0x54
	DIDEVCAPS             m_oDIDevCaps;      // 0x298
	IDirectInputDevice8A* m_poDXInputDevice; // 0x2C4
	DIJOYSTATE            m_oJoyState;       // 0x2C8
	DIJOYSTATE            m_oOldJoyState;    // 0x318
	TFLOAT                m_afSticks[ 4 ];   // 0x368, normalised X/Y of the left stick and Z/RZ of the right stick
	TBOOL                 m_bPolled;         // 0x378
#ifdef BARNYARD_COMMUNITY_PATCH
	TBOOL m_bConnected;
	TBOOL m_bEnumerated;
	TINT  m_iPortIndex;
	HWND  m_hMainWindow;
	TInputDualSenseRumble m_oNativeRumble;
	TFLOAT m_afVibration[ 2 ] = {};
	WCHAR m_wszRumblePath[ MAX_PATH ] = {};
	TBOOL m_bRumblePathQueried = TFALSE;
#endif
};

TOSHI_NAMESPACE_END

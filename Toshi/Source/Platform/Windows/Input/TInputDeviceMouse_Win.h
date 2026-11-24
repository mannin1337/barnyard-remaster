#pragma once
#include "Includes.h"
#include "Input/TInputDeviceMouse.h"

TOSHI_NAMESPACE_START

class TInputDXDeviceMouse : public TInputDeviceMouse
{
public:
	TDECLARE_CLASS( TInputDXDeviceMouse, TInputDeviceMouse );

public:
	// $Barnyard: FUNCTION 006d3c30
	TInputDXDeviceMouse()
	{
		m_poDXInputDevice = NULL;
	}

	// $Barnyard: FUNCTION 006d4530
	virtual ~TInputDXDeviceMouse()
	{
		Deinitialise();
	}

	virtual TBOOL  Acquire() override;
	virtual TBOOL  Unacquire() override;
	virtual void   Release() override;
	virtual void   Update( TFLOAT a_fDeltaTime ) override;
	virtual TBOOL  Flush() override;
	virtual TINT   ProcessEvents( EventEmitter& a_rEmitter, TFLOAT a_fDeltaTime ) override;
	virtual TINT   GetButtonCount() const override;
	virtual TINT   GetAxisCount() const override;
	virtual TBOOL  IsDown( Doodad a_iDoodad ) const override;
	virtual TINT   GetAxisInt( Doodad a_iDoodad, TINT axis ) const override;
	virtual TFLOAT GetAxisFloat( Doodad a_iDoodad, TINT axis ) const override;
	virtual TBOOL  IsEnabled() const override;

	virtual TBOOL Initialise();
	virtual TBOOL Deinitialise();
	virtual void  RefreshDirect();
	virtual TBOOL WasDown( Doodad a_iDoodad ) const;

	TBOOL const BindToDIDevice( HWND a_hMainWindow, LPCDIDEVICEINSTANCEA a_poDeviceInstance, IDirectInputDevice8A* a_poDXInputDevice, TBOOL a_bExclusive );

	void SetExclusive( HWND a_hWindow, TBOOL a_bExclusive );
	void SetCurrentPosition( TINT a_iX, TINT a_iY );

#ifdef BARNYARD_COMMUNITY_PATCH
	static void  SetRawInputEnabled( TBOOL a_bEnabled );
	static TBOOL IsRawInputEnabled() { return ms_bRawInputEnabled; }
	TBOOL IsRawInputExclusive() const { return ms_bRawInputEnabled && m_bRawInputRegistered && m_bRawExclusive && IsAcquired(); }

	// Called only by the game's window procedure; never receives background input.
	void HandleRawInput( HWND a_hWindow, WPARAM a_wParam, LPARAM a_lParam );
	void PumpRawInputForFrame();
	void ResetRawMotion();

#  ifndef TOSHI_FINAL
	// Scripted counts use the same accumulator and event path as WM_INPUT.
	void  Dev_QueueRawMotion( TINT a_iDX, TINT a_iDY );
	TBOOL Dev_IsMotionScriptActive() const { return m_bDevMotionScript; }
#  endif
#endif

	TINT GetPositionX() const { return m_iPositionX; } // 0x48
	TINT GetPositionY() const { return m_iPositionY; } // 0x4c

	const DIDEVICEINSTANCEA& GetDeviceInstance() const
	{
		return m_oDeviceInstance;
	}

private:
	TBOOL                 m_bReverseButtons;
	DWORD                 m_dwButtonCurrent;
	DWORD                 m_dwButtonPrevious;
	TINT                  m_iPositionX;
	TINT                  m_iPositionY;
	TINT                  m_iPositionZ;
	TINT                  m_iDeltaPositionX;
	TINT                  m_iDeltaPositionY;
	TINT                  m_iDeltaPositionZ;
	TINT                  m_iUnk1;
	TINT                  m_iUnk2;
	TINT                  m_iUnk3;
	TINT                  m_iUnk4;
	TINT                  m_iUnk5;
	TINT                  m_iUnk6;
	TINT                  m_iUnk7;
	TINT                  m_iWheelGranularity; // 0x7c, DIPROP_GRANULARITY of the wheel
	DIDEVICEINSTANCEA     m_oDeviceInstance;
	DIDEVCAPS             m_DIDevCaps;
	IDirectInputDevice8A* m_poDXInputDevice;

#ifdef BARNYARD_COMMUNITY_PATCH
	void ConfigureRawInput();
	void ReleaseRawCursor();
	void AccumulateRawMotion( TINT a_iDX, TINT a_iDY );

	inline static TBOOL ms_bRawInputEnabled = TTRUE;
	HWND   m_hMainWindow          = NULL;
	TBOOL  m_bRawInputRegistered   = TFALSE;
	TBOOL  m_bRawExclusive         = TFALSE;
	TBOOL  m_bRawBackground        = TFALSE;
	TBOOL  m_bRawCursorClipped     = TFALSE;
	TINT   m_iRawDeltaX            = 0;
	TINT   m_iRawDeltaY            = 0;
	TINT   m_iRawDeltaZ            = 0;
	TINT   m_iRawWheelRemainder    = 0;
	DWORD  m_dwRawButtons          = 0;
	TINT   m_aiRawButtonChanges[ 64 ];
	TINT   m_iNumRawButtonChanges  = 0;
	HANDLE m_hAbsoluteRawDevice    = NULL;
	TINT   m_iAbsoluteRawX         = 0;
	TINT   m_iAbsoluteRawY         = 0;
	TBOOL  m_bAbsoluteRawValid     = TFALSE;
#  ifndef TOSHI_FINAL
	TBOOL m_bDevMotionScript = TFALSE;
	TBOOL m_bDevMotionLogged = TFALSE;
	TFLOAT m_fDevIdleTime    = 0.0f;
	TINT  m_iDevConsumedX    = 0;
	TINT  m_iDevConsumedY    = 0;
#  endif
#endif
};

TOSHI_NAMESPACE_END

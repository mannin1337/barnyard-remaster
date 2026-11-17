#pragma once

#ifdef BARNYARD_COMMUNITY_PATCH
#include <windows.h>

TOSHI_NAMESPACE_START

struct TDualSenseRumbleContext;

// USB-only, normal DualSense. Bluetooth output would switch report modes and
// break the existing DirectInput reader. Never opens a device for a zero request.
class TInputDualSenseRumble
{
public:
	TInputDualSenseRumble() = default;
	~TInputDualSenseRumble();
	TInputDualSenseRumble( const TInputDualSenseRumble& ) = delete;
	TInputDualSenseRumble& operator=( const TInputDualSenseRumble& ) = delete;

	static bool IsOutputDisabled();
	void SetVibration( HWND a_hOwnWindow, const wchar_t* a_pDevicePath, float a_fRight, float a_fLeft );
	void Stop();
	void Reset(); // a new DirectInput binding may retry an unavailable device

private:
	TDualSenseRumbleContext* m_pContext = nullptr;
	unsigned int m_uiGeneration = 0;
	bool m_bUnavailable = false;
};

TOSHI_NAMESPACE_END
#endif

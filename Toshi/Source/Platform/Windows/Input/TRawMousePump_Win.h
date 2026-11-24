#pragma once
#include <windows.h>

namespace TRawMousePump
{
inline void BeforeInput( HWND window )
{
	// Inspect the normal queue head. Filtering for WM_INPUT alone could pull
	// a click past an earlier focus change, pause, close or another window.
	// Legacy mouse messages can be interleaved with the same raw packets.
	MSG msg;
	for ( unsigned i = 0; i < 256 && PeekMessageA( &msg, nullptr, 0, 0, PM_NOREMOVE ); ++i )
	{
		if ( msg.hwnd != window || ( msg.message != WM_INPUT &&
		     ( msg.message < WM_MOUSEFIRST || msg.message > WM_MOUSELAST ) ) ) break;

		if ( !PeekMessageA( &msg, window, msg.message, msg.message, PM_REMOVE ) ) break;
		if ( msg.message == WM_QUIT )
		{
			// Sent messages may run during PeekMessage. Preserve a new quit
			// request if one arrived between the peek and removal.
			PostQuitMessage( static_cast<int>( msg.wParam ) );
			break;
		}
		DispatchMessageA( &msg );
	}
}
}

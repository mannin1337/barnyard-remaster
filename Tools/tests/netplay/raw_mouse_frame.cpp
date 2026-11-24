#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../../../Toshi/Source/Platform/Windows/Input/TRawMousePump_Win.h"

namespace
{
int edges[ 16 ], edgeCount;
LRESULT CALLBACK OnMessage( HWND window, UINT message, WPARAM value, LPARAM data )
{
	if ( message == WM_INPUT )
	{
		// A posted test token exercises scheduling without accessing hardware.
		edges[ edgeCount++ ] = static_cast<int>( value );
		return 0;
	}
	return DefWindowProcA( window, message, value, data );
}

void RenderPump()
{
	MSG msg;
	while ( PeekMessageA( &msg, nullptr, 0, 0, PM_REMOVE ) ) DispatchMessageA( &msg );
}
}

int main()
{
	WNDCLASSA wc{};
	wc.lpfnWndProc = OnMessage;
	wc.hInstance = GetModuleHandleA( nullptr );
	wc.lpszClassName = "BarnyardRawFrameTest";
	assert( RegisterClassA( &wc ) );
	auto create = [&]() {
		return CreateWindowExA( 0, wc.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE,
		    nullptr, wc.hInstance, nullptr );
	};
	const HWND game = create(), other = create();
	assert( game && other );
	RenderPump();

	// Previous ordering: both tokens arrive after input polling, at render.
	assert( PostMessageA( game, WM_INPUT, 1, 0 ) && PostMessageA( game, WM_INPUT, 2, 0 ) );
	assert( edgeCount == 0 );
	RenderPump();
	assert( edgeCount == 2 );
	edgeCount = 0;

	// Current input sample: a complete click, including the intervening legacy mouse message.
	assert( PostMessageA( game, WM_INPUT, 1, 0 ) && PostMessageA( game, WM_MOUSEMOVE, 0, 0 ) );
	assert( PostMessageA( game, WM_INPUT, 2, 0 ) );
	TRawMousePump::BeforeInput( game );
	std::printf( "old current-frame edges=0 next=2; early current-frame edges=%d\n", edgeCount );
	std::fflush( stdout );
	if ( edgeCount != 2 || edges[ 0 ] != 1 || edges[ 1 ] != 2 ) return 1;
	RenderPump();
	assert( edgeCount == 2 );

	// A queued focus/pause/close or another window must run in normal order.
	for ( UINT barrier : { UINT( WM_KILLFOCUS ), UINT( WM_ACTIVATEAPP ), UINT( WM_CLOSE ), UINT( WM_APP ) } )
	{
		const HWND target = barrier == WM_APP ? other : game;
		assert( PostMessageA( target, barrier, 0, 0 ) && PostMessageA( game, WM_INPUT, 3, 0 ) );
		TRawMousePump::BeforeInput( game );
		assert( edgeCount == 2 );
		MSG msg;
		assert( PeekMessageA( &msg, nullptr, 0, 0, PM_REMOVE ) && msg.hwnd == target && msg.message == barrier );
		TRawMousePump::BeforeInput( game );
		assert( edgeCount == 3 && edges[ 2 ] == 3 );
		edgeCount = 2;
	}
	assert( PostMessageA( other, WM_INPUT, 4, 0 ) );
	TRawMousePump::BeforeInput( game );
	assert( edgeCount == 2 );
	RenderPump();
	assert( edgeCount == 3 && edges[ 2 ] == 4 );
	PostQuitMessage( 23 );
	TRawMousePump::BeforeInput( game );
	MSG msg;
	assert( PeekMessageA( &msg, nullptr, 0, 0, PM_REMOVE ) && msg.message == WM_QUIT && msg.wParam == 23 );
	DestroyWindow( other );
	DestroyWindow( game );
	std::puts( "PASS quick click, message ordering, other windows and quit" );
}

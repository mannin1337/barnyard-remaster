#include "pch.h"
#include "AHighAddressTest.h"

#if defined( TOSHI_SKU_WINDOWS ) && !defined( TOSHI_FINAL )
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace AHighAddressTest
{
// Plain zero-initialised data: this runs before C++ static constructors.
static Stats s_stats;
static void* s_reservations[ 512 ];

bool ReserveLowAddressSpace( const char* commandLine )
{
	const char* flag = "-devhighaddress";
	const std::size_t length = std::strlen( flag );
	const char* match = commandLine;
	while ( ( match = std::strstr( match, flag ) ) != nullptr )
	{
		const bool left = match == commandLine || match[ -1 ] == ' ' || match[ -1 ] == '\t' || match[ -1 ] == '"';
		const char right = match[ length ];
		if ( left && ( right == '\0' || right == ' ' || right == '\t' || right == '"' ) )
			break;
		match += length;
	}
	if ( !match || s_stats.enabled )
		return true;

	SYSTEM_INFO info;
	GetSystemInfo( &info );
	constexpr std::uintptr_t limit = 0x80000000u;
	if ( reinterpret_cast<std::uintptr_t>( info.lpMaximumApplicationAddress ) < limit )
		return false; // Requires an LAA x86 executable on 64-bit Windows.

	s_stats.enabled = true;
	const std::uintptr_t granularity = info.dwAllocationGranularity;
	for ( std::uintptr_t address = granularity; address < limit; )
	{
		MEMORY_BASIC_INFORMATION region;
		if ( !VirtualQuery( reinterpret_cast<void*>( address ), &region, sizeof( region ) ) )
			return false;
		const std::uintptr_t end = reinterpret_cast<std::uintptr_t>( region.BaseAddress ) + region.RegionSize;
		const std::uintptr_t next = end < limit ? end : limit;
		if ( next <= address )
			return false;
		if ( region.State == MEM_FREE )
		{
			const std::uintptr_t start = ( address + granularity - 1 ) & ~( granularity - 1 );
			if ( start < next )
			{
				if ( s_stats.blockCount == sizeof( s_reservations ) / sizeof( s_reservations[ 0 ] ) )
					return false;
				// MEM_RESERVE only: no RAM/pagefile backing and no writes to these pages.
				void* block = VirtualAlloc( reinterpret_cast<void*>( start ), next - start, MEM_RESERVE, PAGE_NOACCESS );
				if ( block )
				{
					s_reservations[ s_stats.blockCount++ ] = block;
					s_stats.reservedBytes += next - start;
				}
				else
					++s_stats.failedReservations;
			}
		}
		address = next;
	}
	return s_stats.failedReservations == 0;
}

void ReleaseLowAddressSpace()
{
	while ( s_stats.blockCount )
		VirtualFree( s_reservations[ --s_stats.blockCount ], 0, MEM_RELEASE );
	s_stats = {};
}

const Stats& GetStats()
{
	return s_stats;
}
}
#endif

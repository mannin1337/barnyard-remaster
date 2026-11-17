#pragma once
#include <cstddef>

// Opt-in stress test; never changes the machine's allocation policy.
namespace AHighAddressTest
{
struct Stats
{
	bool enabled;
	std::size_t reservedBytes;
	unsigned blockCount;
	unsigned failedReservations;
};

bool ReserveLowAddressSpace( const char* commandLine );
void ReleaseLowAddressSpace();
const Stats& GetStats();
}

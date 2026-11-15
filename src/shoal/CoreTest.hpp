#pragma once
// Test-only hooks into shoal::Core, defined in Firmware.cpp (CoreImpl.inc).
// Only the tests include this header.
#include <cstdint>

#include "Core.hpp"

// True when no shoal::Context is current on the calling thread.
bool shoal_test_current_is_null();

namespace shoal {
struct CoreTest {
	// Set the core's tick counters (the firmware's 32-bit one and the
	// shim's 64-bit one) as if `ticks` ticks had run.
	static void setTicks(Core& c, uint64_t ticks);
	// The firmware's millis() as seen by core c.
	static uint32_t millis(Core& c);
};
}

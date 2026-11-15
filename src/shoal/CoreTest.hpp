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
	// Ticks core c has run since boot (the shim's 64-bit counter).
	static uint64_t ticks(Core& c);
	// OC::ADC::pitch_value(ch): the pitch the firmware reads on CV input ch
	// (12 << 7 per octave).
	static int32_t pitchIn(Core& c, int ch);
	// OC::ADC::smoothed_raw_value(ch): the firmware's smoothed 12-bit code.
	static uint32_t smoothedCode(Core& c, int ch);
	// The setting index (as in Core::setting) under the menu cursor of
	// CopierMaschine (app 0); -1 for any other app.
	static int menuSetting(Core& c);
};
}

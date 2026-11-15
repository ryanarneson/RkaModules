#pragma once
// Test-only hooks into shoal::Core, defined in Firmware.cpp (CoreImpl.inc).
// Only the tests include this header.
#include <cstddef>
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
	// The firmware's storage pages inside a state image: the global settings
	// page or the app data page (page 0 of each, where a first save puts it).
	// pageOffset: the page header; dataOffset, dataSize: the stored data.
	static void pageLayout(bool appData, size_t& pageOffset, size_t& dataOffset, size_t& dataSize);
	// Recompute, with the firmware's own checksum, the checksum in the
	// header of that page of `image` (so edited data still loads).
	static void fixChecksum(uint8_t* image, bool appData);
	// User data the firmware keeps outside the apps' settings (slot indices
	// as the firmware's: 4 user scales, 4 patterns, 8 x 4 chords).
	// Image offsets of user scale `slot`'s span (int16), note count (uint32)
	// and first note (int16), for forging a global settings page.
	static void userScaleImageOffsets(int slot, size_t& span, size_t& numNotes, size_t& notes);
	// Feed `inputs` (ADC codes) in turn to a fresh OC::Input_Map set up with
	// the firmware's map for `numSlots` slots and `range` (0: 5 V, 1: 10 V);
	// slots[i] receives the slot chosen for inputs[i].
	static void inputMapRun(int numSlots, int range, const int32_t* inputs, int n, int8_t* slots);
	static int userScaleSlots();
	static int userPatternSlots();
	static int userChordSlots();
	static int16_t& userScaleNote(Core& c, int slot, int note);   // 16 notes
	static uint32_t& userScaleNumNotes(Core& c, int slot);
	static int16_t& userPatternNote(Core& c, int slot, int note); // 16 notes
	static int8_t& userChordField(Core& c, int slot, int field);  // 0..4: quality, inversion, voicing, base note, octave
};
}

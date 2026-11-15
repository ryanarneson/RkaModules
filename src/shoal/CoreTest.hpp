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
	// The setting index (as in Core::setting) under the menu cursor of the
	// current app, on the selected channel and the page shown: -1 when the
	// cursor's row is past the end of the list, in Automatonnetz's cell view
	// and on Piqued's envelope page.
	static int menuSetting(Core& c);
	// The menu cursor's row (cursor_pos).
	static int menuRow(Core& c);
	// Whether that cursor is in edit mode.
	static bool menuEditing(Core& c);
	// The firmware's selected channel (0 for an app with one).
	static int selectedChannel(Core& c);
	// The firmware's menu, as settings in row order, for the selected channel
	// and the page shown, of every app but Quadraturia and Low-rents (whose
	// cursor is a setting index): up to `max` into out; the count, or -1 for
	// those two.
	static int enabledSettings(Core& c, int* out, int max);
	// The menu page shown: Piqued 0 (envelope segments) or 1 (settings);
	// Sequins and Acid Curds 0 (parameters) or 1 (CV mapping), of the
	// selected channel; -1 for the other apps.
	static int menuPage(Core& c);
	// Piqued's Euclidean editor: whether its L encoder edits the length
	// (true) or the fill.
	static bool piquedEuclidLength(Core& c);
	// One UI timer pass (the encoders' quadrature and OC::Ui::Poll) with no
	// main pass: what the panel did is queued, not yet handed to the app.
	static void pollUi(Core& c);
	// Whether the current app's friendly adapter is on its settings menu
	// (its accepting(); true for an adapter without one).
	static bool friendlyAccepting(Core& c);
	// Hand one event straight to the current app's handler, with no debounce
	// and no acceleration (what OC::Ui::DispatchEvents would send it): an
	// encoder event carrying `delta` (nothing for 0), or a short or long
	// press. The friendly-mode tests use these as "what the encoders would
	// do". Unlike the hardware, an R or UP long press reaches the app too.
	static void encoderEvent(Core& c, Encoder e, int delta);
	// Whether app `app` has a friendly-mode adapter of its own (one that
	// takes edits); every app has one.
	static bool friendlyHasAdapter(int app);
	// Friendly mode through a stand-in for the Low-rents adapter with one
	// thing wrong (Friendly.inc, namespace fake); c must be on Low-rents.
	// Applies *e if given (the result; false without one), then builds the
	// stand-in's model into *out if given.
	enum FriendlyFake {
		NotAccepting,   // its accepting() is false: as with a sub-editor open
		TabsNoSelect,   // reports two tabs, has no selectTab
		SelectFails,    // reports two tabs (the first selected); its selectTab does nothing
		TooManyRows,    // lists kMaxRows + 5 action rows
		ReachFails      // its edit asks replaySet for a menu row past the menu's end
	};
	static bool friendlyFake(Core& c, FriendlyFake fake, const FriendlyEdit* e, FriendlyModel* out);
	// friendly::Text::fixed, the status texts' printf("%*.*f") stand-in.
	static void fixedText(double value, int decimals, int width, char (&out)[24]);
	static void buttonEvent(Core& c, Button b, bool longPress);
	// The app menu round trip as OC::Ui handles the events: an R long press
	// (DispatchEvents opens the app menu; the app gets SUSPEND), then an R
	// push to the menu (AppMenuHandle closes it on the same app; RESUME).
	static void appMenuEvents(Core& c);
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
	// Feed `pitches` in turn to a fresh, zeroed braids::Quantizer (as the
	// firmware's, which live in zero-initialised objects) configured with a
	// 16-note scale, span 1536, notes 96 * i, all enabled; out[i] receives
	// Process(pitches[i], 0, 0).
	static void quantizerRun(const int32_t* pitches, int n, int32_t* out);
	// The same with the 16 notes `notes` (span 1536, all enabled), as given:
	// unsorted notes are allowed (the firmware's Configure does not sort).
	static void quantizerRun(const int16_t* notes, const int32_t* pitches, int n, int32_t* out);
	// Clock a util::IntegerSequence (the firmware's IntSq source) for core c:
	// sequence `seq`, loop start `start`, length `length`, stride 1, no
	// Brownian motion, modulus 256 (no wrap); out[i] receives the i-th
	// Clock() (the value << 8).
	static void intSeqRun(Core& c, int seq, int start, int length, int n, uint16_t* out);
	static int userScaleSlots();
	static int userPatternSlots();
	static int userChordSlots();
	static int16_t& userScaleNote(Core& c, int slot, int note);   // 16 notes
	static uint32_t& userScaleNumNotes(Core& c, int slot);
	static int16_t& userPatternNote(Core& c, int slot, int note); // 16 notes
	static int8_t& userChordField(Core& c, int slot, int field);  // 0..4: quality, inversion, voicing, base note, octave
};
}

#pragma once
// Shoal core: the Ornament & Crime v1.3.6 firmware behind a fake-hardware
// shim. The only header the rest of the plugin and the tests include; it
// hides every firmware type. Rack-free, C++11-compatible.
//
// Threading: all non-static members of one Core (inputs, tick, controls,
// draws, settings, state) must be called from one thread at a time; the
// caller serialises them. Different Cores are independent and may run on
// different threads at once. The static members (pixel, appName) are safe
// from any thread.
#include <cstdint>

#include "Friendly.hpp"

namespace shoal {
constexpr int kNumApps = 13, kNumChannels = 4;
constexpr int kFrameW = 128, kFrameH = 64, kFrameBytes = 1024;
constexpr int kStateBytes = 2048;
constexpr double kTickHz = 1e6 / 60.0;
constexpr uint32_t kZeroVoltCode = 24551;  // DAC code of 0 V

// Converts engine samples into 60 us firmware ticks (kTickHz, 16,666.67 Hz).
struct TickClock {
	double ratio = kTickHz / 48000.0;
	double phase = 0.0;

	void setSampleRate(double hz) {
		ratio = kTickHz / (hz > 0.0 ? hz : 48000.0);
	}
	// Ticks due for one sample. Fractional remainder carries over.
	int advance() {
		phase += ratio;
		int n = (int) phase;
		phase -= n;
		return n;
	}
};

enum Button : uint8_t { BUTTON_UP, BUTTON_DOWN, BUTTON_L, BUTTON_R };
enum Encoder : uint8_t { ENCODER_L, ENCODER_R };
enum class InputRange : uint8_t { Hardware, Bipolar5 };
struct SettingInfo { const char* name; int min, max, def; const char* const* valueNames; };

class Core {
public:
	Core();  ~Core();
	Core(const Core&) = delete;  Core& operator=(const Core&) = delete;
	// Inputs. Call at any time between ticks; a rising gate edge is latched until the next tick.
	void setCv(int ch, float volts);
	void setGate(int ch, float volts);
	void setInputRange(InputRange r);
	void tick();                       // one 60 µs firmware tick, plus UI poll and main-loop pass when due
	float out(int ch) const;           // volts; 0 V on a new Core until the app writes
	uint32_t outCode(int ch) const;    // raw 16-bit DAC code
	// Controls, through the firmware's own debounce (a press registers after
	// 7 ms, a long press is held 1 s) and encoder acceleration. Detents queue;
	// each plays out over two 1 ms UI polls (at most 256 queued per encoder;
	// a turn past that is dropped).
	void setButton(Button b, bool down);
	void turnEncoder(Encoder e, int detents);  // > 0 clockwise
	// Whether a turn has not yet reached the app's handler: detents still
	// queued, or the event of one played since the last main-loop pass (at
	// most 17 ticks later). The host holds a friendly edit until this is
	// false, so turns and edits reach the app in the order they were made.
	bool encodersPending() const;
	// As the app menu's UP button: both encoders; on at boot. Automatonnetz
	// turns its L encoder's acceleration off on resume; calling this while it
	// runs overrides that until its next resume (app switch or app menu).
	void setEncoderAcceleration(bool on);
	// Screens, drawn on request (at any rate or never; see Threading above). drawMenu: the app's menu, or the app-selection menu while it is
	// open. drawScope: the app's screensaver view. Each draws into its own
	// frame, so neither disturbs the other or the firmware's UI state.
	void drawMenu(uint8_t* dst);       // dst is kFrameBytes
	void drawScope(uint8_t* dst);      // dst is kFrameBytes
	// Frame layout (weegfx, the SH1106's page order): 8 pages of 8 rows,
	// 128 bytes per page; pixel (x, y) is bit (y % 8) of byte
	// (y / 8) * 128 + x, set = lit. (0, 0) is top left. False off-frame.
	static bool pixel(const uint8_t* frame, int x, int y);
	// Apps.
	int app() const;
	void selectApp(int index);         // as choosing it in the app menu; closes that menu
	static const char* appName(int index);
	bool appMenuOpen() const;          // opened by an R long press
	// Settings of the current app (raw: range-clamped, no app guards).
	int channels() const;
	int settingCount() const;
	SettingInfo settingInfo(int index) const;
	int setting(int channel, int index) const;
	bool setSettingRaw(int channel, int index, int value);
	// Friendly mode (Friendly.hpp): the current app's settings as a list, and
	// edits on it, carried out through the app's own encoder and button
	// handlers. Neither allocates or locks. An edit is dropped (false) when
	// its app is not the current one, the app is not on its settings menu
	// (the app menu or a sub-editor is open: the model's kModelBusy), its tab
	// does not exist or the app would not select it, its row is not in that
	// tab's list now, or the row does not take that type of edit. True means
	// it was handed to the app, which may still refuse or clamp it: the next
	// model shows the result.
	void friendlyModel(FriendlyModel& out) const;
	bool friendlyEdit(const FriendlyEdit& e);
	// State.
	void save(uint8_t* out);           // kStateBytes
	// restore and resetToDefaults reboot the firmware as a new Core would:
	// the outputs return to 0 V (kZeroVoltCode) until the app writes them,
	// and only the input range, the held inputs and buttons, the random
	// state and the clocks carry over. restore accepts an image only if both
	// its global settings page and its app data page load.
	bool restore(const uint8_t* in);   // kStateBytes; false = rejected, a new Core's defaults
	void resetToDefaults();
	void seed(uint32_t s);             // all firmware random sources
private:
	struct Impl;  Impl* impl_;
	friend struct CoreTest;            // test-only hooks (CoreTest.hpp)
};
}

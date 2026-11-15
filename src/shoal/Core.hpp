#pragma once
// Shoal core: the Ornament & Crime v1.3.6 firmware behind a fake-hardware
// shim. The only header the rest of the plugin and the tests include; it
// hides every firmware type. Rack-free, C++11-compatible.
#include <cstdint>

namespace shoal {
constexpr int kNumApps = 13, kNumChannels = 4;
constexpr int kFrameW = 128, kFrameH = 64, kFrameBytes = 1024;
constexpr int kStateBytes = 2048;
constexpr double kTickHz = 1e6 / 60.0;
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
	float out(int ch) const;           // volts
	uint32_t outCode(int ch) const;    // raw 16-bit DAC code
	// Controls.
	void setButton(Button b, bool down);
	void turnEncoder(Encoder e, int detents);
	void setEncoderAcceleration(bool on);
	// Screens. dst is kFrameBytes; see pixel().
	void drawMenu(uint8_t* dst);
	void drawScope(uint8_t* dst);
	static bool pixel(const uint8_t* frame, int x, int y);
	// Apps.
	int app() const;
	void selectApp(int index);
	static const char* appName(int index);
	bool appMenuOpen() const;
	// Settings of the current app (raw: range-clamped, no app guards).
	int channels() const;
	int settingCount() const;
	SettingInfo settingInfo(int index) const;
	int setting(int channel, int index) const;
	bool setSettingRaw(int channel, int index, int value);
	// State.
	void save(uint8_t* out);           // kStateBytes
	bool restore(const uint8_t* in);   // kStateBytes; false = rejected, defaults loaded
	void resetToDefaults();
	void seed(uint32_t s);             // all firmware random sources
private:
	struct Impl;  Impl* impl_;
};
}

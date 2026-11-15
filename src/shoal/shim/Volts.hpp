#pragma once
// Voltage <-> hardware-code conversions and the trigger/clock primitives the
// Shoal core needs. Header-only, Rack-free, no firmware includes.
#include <cmath>
#include <cstdint>

namespace shoal {

// DAC output: volts = (code - 24551) / 6553.6 (16-bit code, 0 V at 24551).
inline float dacToVolts(uint32_t code) {
	return ((float) code - 24551.f) / 6553.6f;
}

// ADC input: 12-bit code = offset - volts * 409.6, rounded, clamped to
// 0..4095. offset is 2730 (Hardware range) or 2048 (+-5 V range).
// NaN reads as 0 V; infinities clamp.
inline int voltsToAdc12(float volts, int offset) {
	if (std::isnan(volts))
		volts = 0.f;
	float code = (float) offset - volts * 409.6f;
	if (code < 0.f)
		code = 0.f;
	if (code > 4095.f)
		code = 4095.f;
	return (int) std::lround(code);
}

// Schmitt trigger: goes high above 1 V, low below 0.1 V.
// process() returns true on the rising edge only; `high` is the level.
struct Schmitt {
	bool high = false;
	bool process(float v) {
		if (high) {
			if (v < 0.1f)
				high = false;
		}
		else if (v > 1.f) {
			high = true;
			return true;
		}
		return false;
	}
};

// Converts engine samples into 60 us firmware ticks (16,666.67 Hz).
struct TickClock {
	static constexpr double kTickHz = 1e6 / 60.0;
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

} // namespace shoal

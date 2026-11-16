#pragma once
#include "Voice.hpp"

// Rack-independent brightness for Sway's rate light, which follows one
// channel's cycle: lit for the first half of each cycle, dark for the
// second. Above BLINK_MAX_HZ a blink can no longer be read, so the light
// stays at half brightness instead. Meant to be cheap to remove.
namespace rka {

struct RateLight {
	static constexpr double BLINK_MAX_HZ = 10.0;

	// phase01: the channel's phase in [0, 1). hz: the rate it runs at;
	// 0 when stopped (held, or a finished one-shot). Returns 0..1.
	float process(double phase01, double hz) const {
		// Written so NaN fails every comparison and lands on dark.
		if (!(hz >= 0.0))
			return 0.f;
		if (hz > BLINK_MAX_HZ)
			return 0.5f;
		return phase01 < 0.5 ? 1.f : 0.f;
	}
};

// The rate the light is given for one channel: 0 Hz while it has stopped
// (HOLD high, or a one-shot that has finished its cycle), so the light stays
// as its phase left it; otherwise the rate the voice really ran at this
// sample, clocked or free. Read with the voice's own phase (the cycle EOC
// counts), not the PHASE-shifted one the waves read.
inline double rateLightHz(const Voice& voice, bool hold) {
	return hold || voice.phasor.parked ? 0.0 : voice.freq;
}

} // namespace rka

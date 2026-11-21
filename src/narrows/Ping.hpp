#pragma once
// Narrows' Ping: a rising edge at the jack (0.1 V / 1 V Schmitt trigger)
// gives one short, smooth pulse into the filter. Rack-free.
//
// The pulse is 16 p^2 (1 - p)^2 over its length: zero and flat at both
// ends, 1 at the middle (the engine scales it to PING_V). An edge that arrives while a pulse is
// running is not a new pulse, so pulses never pile up.
#include "NarrowsTuning.hpp"

namespace rka {
namespace narrows {

struct PingPulse {
	void setSampleRate(float fs) {
		if (!(fs > 0.f))
			return;
		const float n = PING_S * fs;
		step = n > 2.f ? 1.f / n : 0.5f;
	}

	void reset() {
		high = false;
		phase = 1.f;
	}

	float process(float gateVolts) {
		// NaN compares false both ways: the state holds.
		if (high) {
			if (gateVolts <= 0.1f)
				high = false;
		}
		else if (gateVolts >= 1.f) {
			high = true;
			if (phase >= 1.f)
				phase = 0.f;
		}
		if (phase >= 1.f)
			return 0.f;
		phase += step;
		if (phase >= 1.f)
			return 0.f;
		const float q = phase * (1.f - phase);
		return 16.f * q * q;
	}

private:
	bool high = false;
	float phase = 1.f;
	float step = 1.f / 48.f;
};

} // namespace narrows
} // namespace rka

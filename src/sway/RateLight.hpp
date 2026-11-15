#pragma once

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

} // namespace rka

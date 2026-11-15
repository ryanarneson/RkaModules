#pragma once
// Helpers shared by the Shoal core tests (src/shoal/test_*.cpp).
#include <cmath>
#include <cstdint>

#include "Core.hpp"

// Run the core for `seconds` of firmware time (rounded to whole ticks).
inline void run(shoal::Core& c, double seconds) {
	const long n = std::lround(seconds * shoal::kTickHz);
	for (long i = 0; i < n; i++)
		c.tick();
}

// Raise gate `ch` to 10 V for `highSeconds`, then drop it to 0 V.
inline void pulse(shoal::Core& c, int ch, double highSeconds = 0.002) {
	c.setGate(ch, 10.f);
	run(c, highSeconds);
	c.setGate(ch, 0.f);
}

// True when every output voltage is a finite number.
inline bool allFinite(shoal::Core& c) {
	for (int ch = 0; ch < shoal::kNumChannels; ch++)
		if (!std::isfinite(c.out(ch)))
			return false;
	return true;
}

// Small deterministic generator for test inputs.
struct Lcg {
	uint32_t state = 12345;
	uint32_t next() {
		state = state * 1664525u + 1013904223u;
		return state;
	}
	// Uniform in [lo, hi].
	float volts(float lo, float hi) {
		return lo + (hi - lo) * (float) (next() >> 8) / (float) 0xffffff;
	}
};

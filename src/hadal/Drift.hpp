#pragma once
// Hadal's Drift wander (spec: "Drift: slow movement"): a smooth random value
// in [-1, 1] that wanders over tens of seconds and never repeats. Random
// targets, each held for 8-30 s, are followed through two one-pole glides
// (6 s, then 3 s), so the value moves smoothly with no corners; it is
// advanced every 32 samples and interpolated in between. Filtered noise,
// not an LFO: no period, no pulse. The engine uses one per layer per side
// for level and colour, and one for crackle's density.
#include "Random.hpp"

namespace rka {
namespace hadal {

struct Wander {
	static const int STEP = 32;
	Random rng;
	float fs = 48000.f;
	float target = 0.f, a = 0.f, b = 0.f;
	float prev = 0.f, cur = 0.f;
	int hold = 0, phase = 0;
	float c1 = 0.f, c2 = 0.f;

	void setRate(float rate) {
		const float old = fs;
		fs = rate > 0.f ? rate : 48000.f;
		// The pending target's hold counts control steps: rescale it so it
		// still ends at the same time in seconds.
		hold = (int) ((double) hold * fs / old);
		// One-pole coefficients per control step: 1 - e^(-STEP / (tau fs)),
		// as STEP / (tau fs) (tau >> STEP / fs here, so the error is tiny).
		c1 = (float) STEP / (6.f * fs);
		c2 = (float) STEP / (3.f * fs);
		// At absurd rates (Rack allows 1 Hz) a step would overshoot and grow.
		c1 = c1 < 1.f ? c1 : 1.f;
		c2 = c2 < 1.f ? c2 : 1.f;
	}

	void seed(uint32_t s) {
		rng.seed(s);
		// Start somewhere in the range, already settled.
		target = rng.uniform();
		a = b = prev = cur = target;
		hold = 0;
		phase = 0;
	}

	float next() {
		if (phase == 0) {
			if (--hold <= 0) {
				target = rng.uniform();
				hold = (int) ((8.f + 22.f * rng.unit()) * fs / STEP);
			}
			a += (target - a) * c1;
			b += (a - b) * c2;
			prev = cur;
			cur = b;
		}
		const float t = (float) phase / STEP;
		phase = phase + 1 < STEP ? phase + 1 : 0;
		return prev + t * (cur - prev);
	}
};

} // namespace hadal
} // namespace rka

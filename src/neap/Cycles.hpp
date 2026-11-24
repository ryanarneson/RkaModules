#pragma once
// Neap's bank of eight slow cycles: double phases, a triangle/sine crossfade
// and optional slow drift. Rack-free; reset and drift rules are in the plan
// (docs/plans/2026-11-24-neap.md, Global Constraints).
//
// Cycle indices are 0-based (cycle 1 is i = 0), so cycle i's drift phasor runs
// at 1 / (60 + 9 i) Hz: 60 s for cycle 1 up to 123 s for cycle 8.
#include "FastMath.hpp"
#include "NeapRate.hpp"
#include "sway/Phasor.hpp"
#include "sway/RandomGen.hpp"

#include <cstdint>

namespace rka { namespace neap {

enum Wave { WAVE_TRIANGLE = 0, WAVE_SINE = 1 };

struct Cycles {
	Cycles() {
		for (int i = 0; i < kCycles; i++) {
			gen[i] = RandomGen(uint64_t(1000 + i));
			// First target, so the drift glides from the first cycle on.
			gen[i].advance();
		}
	}

	// All phases 0 and the crossfade finished; the drift state is kept.
	void reset() {
		for (int i = 0; i < kCycles; i++)
			main[i].reset();
		mix = target;
	}

	// Starts a 10 ms linear crossfade toward w (mid-fade it reverses).
	void setWave(Wave w) {
		target = (w == WAVE_SINE) ? 1.f : 0.f;
	}

	// One sample: advance each cycle by its Hz (this sample's drift in) and
	// fill x[kCycles] in +-1. A NaN e1 keeps the last rate.
	void step(float e1, bool drift, float sampleTime, float x[kCycles]) {
		// Base Hz only when e1 changes (8 exponentials); drift multiplies after.
		if (e1 == e1 && (!haveHz || e1 != lastE1)) {
			for (int i = 0; i < kCycles; i++)
				baseHz[i] = cycleHz(i, e1, 0.f);
			lastE1 = e1;
			haveHz = true;
		}
		if (!haveHz) {
			for (int i = 0; i < kCycles; i++)
				x[i] = 0.f;
			return;
		}
		const double dt = double(sampleTime);

		const float fade = float(dt / kFadeSeconds);
		if (mix < target) {
			mix += fade;
			if (mix > target) mix = target;
		} else if (mix > target) {
			mix -= fade;
			if (mix < target) mix = target;
		}

		for (int i = 0; i < kCycles; i++) {
			double d = 0.0;
			if (drift) {
				if (driftPh[i].step(1.0 / (60.0 + 9.0 * i), dt, false, false))
					gen[i].advance();
				d = kDriftDepth * double(gen[i].value(readPhase(driftPh[i].phase, 0.f), 1.f));
			}
			main[i].step(baseHz[i] * (1.0 + d), dt, false, false);
			const float p = readPhase(main[i].phase, 0.f);
			const float t = triangle(p);
			x[i] = mix <= 0.f ? t : mix >= 1.f ? sine(p) : (1.f - mix) * t + mix * sine(p);
		}
	}

	double phase(int i) const {
		return main[i < 0 ? 0 : i >= kCycles ? kCycles - 1 : i].phase;
	}

	// Triangle of phase p in [0, 1): 0 rising, +1 at 0.25, 0 at 0.5, -1 at 0.75.
	static float triangle(float p) {
		return p < 0.25f ? 4.f * p : p < 0.75f ? 2.f - 4.f * p : 4.f * p - 4.f;
	}
	static float sine(float p) {
		return fastSin2pi(p);
	}

private:
	static constexpr double kFadeSeconds = 0.010;
	static constexpr double kDriftDepth = 0.005;

	Phasor main[kCycles];
	Phasor driftPh[kCycles];
	RandomGen gen[kCycles];
	double baseHz[kCycles] = {};
	float lastE1 = 0.f;
	bool haveHz = false;
	// 0 = triangle, 1 = sine.
	float mix = 0.f;
	float target = 0.f;
};

}} // namespace rka::neap

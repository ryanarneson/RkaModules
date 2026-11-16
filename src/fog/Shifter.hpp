#pragma once

// Fog's octave-up shifter (ICE), shared by the in-loop shimmer and the
// feed-forward octave on the wet (Network.hpp). Rack-free, C++11.
//
// - Band-limit: two one-pole high-passes (80 Hz: the bass stays out of the
//   octave) and two one-pole low-passes (5 kHz: -16 dB at a quarter of the
//   rate, so little of the 2x read's aliasing gets through), each with
//   gain <= 1 at every frequency.
// - Shift: the band-limited signal goes into a buffer, read back at twice
//   speed by two taps half a 50 ms grain apart, with triangular windows that
//   sum to 1 (integer reads: no interpolation). Each grain starts at a random
//   extra delay of 0-12 ms (an LCG per shifter), drawn where that tap's
//   window is 0, so the draw cannot click. With a fixed grain the two taps
//   sat a fixed 25 ms apart and the octave's carrier cancelled at 40n + 20 Hz
//   (220, 260, 300 Hz...); the jitter breaks that fixed phase.
// - The output is, at each sample, a convex combination of two past
//   band-limited samples: never above the largest of them, and over a long
//   stretch its RMS is at most the band-limited input's (the window weights
//   bound it in RMS, not per sample).
//
// Only allocate() allocates. process() calls no libm.
#include "DelayLine.hpp"
#include <cmath>
#include <cstdint>

namespace rka {
namespace fog {

struct OctaveShifter {
	static float grainMs() { return 50.f; }
	static float jitterMs() { return 12.f; }
	static float lpHz() { return 5000.f; }
	static float hpHz() { return 80.f; }

	DelayLine buf;
	float hp1 = 0.f, hp2 = 0.f, lp1 = 0.f, lp2 = 0.f; // the filters' one-pole states
	float hpCoef = 0.f, lpCoef = 0.f;
	int grain = 4, k = 0;
	float invGrain = 0.25f;
	int jitterMax = 0;    // samples
	int jitA = 0, jitB = 0; // each tap's extra delay this grain
	uint32_t seed = 1u, seed0 = 1u;

	// The buffer, in samples, at a rate: the grain, the jitter and a margin.
	static int bufferSamples(float rate) {
		return (int) ((grainMs() + jitterMs()) * 0.001f * rate) + 4;
	}

	// Never call from process().
	void allocate(float rate, uint32_t stagger) {
		buf.allocate(bufferSamples(rate >= 1.f ? rate : 1.f), stagger);
	}

	// Coefficients for a rate (libm: not per sample); seedStart makes
	// shifters' jitter independent of each other. Restarts.
	void configure(float rate, uint32_t seedStart) {
		rate = rate >= 1.f ? rate : 1.f;
		const float pi = 3.14159265358979f;
		hpCoef = 1.f - std::exp(-2.f * pi * hpHz() / rate);
		const float lp = lpHz() < 0.4f * rate ? lpHz() : 0.4f * rate;
		lpCoef = 1.f - std::exp(-2.f * pi * lp / rate);
		jitterMax = (int) (jitterMs() * 0.001f * rate);
		int g = (int) (grainMs() * 0.001f * rate);
		const int hi = buf.maxDelay() - 2 - jitterMax;
		g = g > hi ? hi : g;
		g &= ~1;
		grain = g < 4 ? 4 : g;
		invGrain = 1.f / (float) grain;
		seed0 = seedStart;
		restart();
	}

	// As a fresh shifter starts: grain phase 0, filters empty, the jitter's
	// sequence from its start. The buffer is left (see clear()).
	void restart() {
		k = 0;
		hp1 = hp2 = lp1 = lp2 = 0.f;
		seed = seed0;
		jitA = jitB = 0;
	}

	void clear() {
		buf.clear();
		restart();
	}

	static float flush(float x) {
		return (x > 1e-20f || x < -1e-20f) ? x : 0.f; // denormals and NaN to 0
	}

	// One sample in, the octave out.
	float process(float x) {
		hp1 = flush(hp1 + hpCoef * (x - hp1));
		const float h1 = x - hp1;
		hp2 = flush(hp2 + hpCoef * (h1 - hp2));
		lp1 = flush(lp1 + lpCoef * ((h1 - hp2) - lp1));
		lp2 = flush(lp2 + lpCoef * (lp1 - lp2));
		buf.write(lp2);
		const int half = grain >> 1;
		if (k == 0 || k == half) {
			seed = seed * 1664525u + 1013904223u;
			const int j = (int) ((uint64_t) (seed >> 8) * (uint32_t) (jitterMax + 1) >> 24);
			if (k == 0)
				jitA = j;
			else
				jitB = j;
		}
		const int k2 = k + half < grain ? k + half : k + half - grain;
		const float w1 = k < half ? 2.f * (float) k * invGrain : 2.f - 2.f * (float) k * invGrain;
		const float s = w1 * buf.readInt(grain - k + jitA) + (1.f - w1) * buf.readInt(grain - k2 + jitB);
		k = k + 1 < grain ? k + 1 : 0;
		return s;
	}
};

// The in-loop shimmer's crossfade on the u component: (1 - a) m + a s, a in
// [0, 1]. Its RMS is at most (1 - a) |m| + a |s| (Minkowski), whatever the
// phase between s and m.
inline float shimmerCrossfade(float m, float s, float a) {
	return m + a * (s - m);
}

} // namespace fog
} // namespace rka

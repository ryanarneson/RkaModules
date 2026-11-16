#pragma once

// Fog's Bloom: a slow swell on the signal fed into the network (spec,
// "Bloom: how the wash arrives"). A gain rises toward 1 with time constant
// tau while the input is present, and falls (twice as fast) when it has
// gone; the feed is x times that gain. A held input then reaches within 3 dB
// of steady (gain 0.708) after 1.231 tau, so tau is set from the targets:
// Bloom 0 about 12 ms to -3 dB (tau 10 ms), 0.5 -> 1 s (tau 0.812 s) and 1
// -> 4 s (tau 3.25 s), log-spaced between. The gain is smooth, so the swell
// itself adds no click.
//
// "Present" is a peak follower of |x| (release 250 ms) above 1 mV. The
// dry path never passes through here. No libm; set() works out tau only
// when its arguments change, with fastExp2.
#include "FastMath.hpp"

namespace rka {
namespace fog {

struct Bloom {
	float rate = 48000.f;
	float riseCoeff = 1.f, fallCoeff = 1.f, relCoeff = 0.f;
	float level = 0.f; // peak follower of |x|
	float gain = 0.f;  // the swell, 0..1
	float lastBloom = -1.f, lastBias = -1.f;

	static float presentV() { return 0.001f; }
	static float tauMin() { return 0.010f; }
	static float tauMid() { return 0.8122f; }  // 1 s / 1.231
	static float tauMax() { return 3.2488f; }  // 4 s / 1.231

	void setup(float r) {
		rate = r >= 1.f ? r : 1.f;
		relCoeff = fastExp2(-1.f / (0.25f * rate * 0.693147180559945f));
		lastBloom = -1.f; // recompute the time constants
		set(0.f, 0.f);
		clear();
	}

	// bloom in [0, 1]; bias >= 0 stretches tau by (1 + bias).
	void set(float bloom, float bias) {
		bloom = bloom >= 0.f ? (bloom <= 1.f ? bloom : 1.f) : 0.f;
		bias = bias >= 0.f ? (bias <= 4.f ? bias : 4.f) : 0.f;
		if (bloom == lastBloom && bias == lastBias)
			return;
		lastBloom = bloom;
		lastBias = bias;
		float tau;
		if (bloom <= 0.5f)
			tau = tauMin() * fastExp2(2.f * bloom * 6.34376f); // log2(tauMid / tauMin)
		else
			tau = tauMid() * fastExp2((2.f * bloom - 1.f) * 2.f); // x4 over the upper half
		tau *= 1.f + bias;
		riseCoeff = 1.f - fastExp2(-1.f / (tau * rate * 0.693147180559945f));
		fallCoeff = 1.f - fastExp2(-2.f / (tau * rate * 0.693147180559945f));
	}

	float process(float x) {
		const float a = x >= 0.f ? x : -x;
		level = a > level * relCoeff ? a : level * relCoeff;
		level = level > 1e-20f ? level : 0.f; // the follower's tail must not go denormal
		if (level > presentV())
			gain += riseCoeff * (1.f - gain);
		else
			gain -= fallCoeff * gain;
		gain = gain > 1e-12f ? gain : 0.f;
		return x * gain;
	}

	void clear() {
		level = 0.f;
		gain = 0.f;
	}
};

} // namespace fog
} // namespace rka

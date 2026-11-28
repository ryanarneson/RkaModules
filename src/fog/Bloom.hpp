#pragma once

// Fog's Bloom: a slow swell on the signal fed into the network (spec,
// "Bloom: how the wash arrives"). A gain rises toward 1 with time constant
// tau while the input is present, and falls (twice as fast) when it has
// gone; the feed is x times that gain. A held input then reaches within 3 dB
// of steady (gain 0.708) after 1.231 tau, so tau is set from the targets:
// Bloom 0 about 12 ms to -3 dB (tau 10 ms), 0.5 -> 1 s (tau 0.812 s) and 1
// -> 4 s (tau 3.25 s), log-spaced between. The gain is smooth, so the swell
// itself adds no click. It is held in double: a float one-pole towards 1
// stalls once its step is under half an ulp of 1, which at Bloom 1 in FRET
// (tau 6.5 s) left the gain at 0.991 at 48 kHz and 0.962 at 192 kHz for good
// (test_engine's bloom_opens_fully).
//
// "Present" is a peak follower of |x| (release 250 ms) above 1 mV. The
// dry path never passes through here. No libm; set() works out tau only
// when its arguments change, with fastExp2.
//
// The rise and fall coefficients are 1 - e^(-a), a = 1 / (tau x rate) (and
// twice that), in double from the series a - a^2 / 2 + a^3 / 6 (a is under
// 0.005 at every rate from 44.1 kHz). As a float 1 - 2^(-x) they came only
// in steps of 6e-8, the spacing of floats under 1: at 768 kHz and Bloom 1
// in FRET the rise's 2.0e-7 became 1.8e-7, and the swell took 9.0 s to come
// within 3 dB for 8.0 s (test_engine's bloom_times_at_high_rates).
#include "FastMath.hpp"

namespace rka {
namespace fog {

struct Bloom {
	float rate = 48000.f;
	double riseCoeff = 1.0, fallCoeff = 1.0;
	float relCoeff = 0.f;
	float level = 0.f; // peak follower of |x|
	double gain = 0.0; // the swell, 0..1
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
		riseCoeff = onePoleCoeff(1.0 / ((double) tau * (double) rate));
		fallCoeff = onePoleCoeff(2.0 / ((double) tau * (double) rate));
	}

	// 1 - e^(-a) for a >= 0: the series while a is small (its next term,
	// a^4 / 24, is under 3e-10 of it at a = 0.02), fastExp2 above that (a
	// very low rate), where a float has the digits.
	static double onePoleCoeff(double a) {
		if (a < 0.02)
			return a * (1.0 - a * (0.5 - a * (1.0 / 6.0)));
		const float c = 1.f - fastExp2((float) (-a * 1.4426950408889634));
		return c < 1.f ? (double) c : 1.0;
	}

	float process(float x) {
		const float a = x >= 0.f ? x : -x;
		level = a > level * relCoeff ? a : level * relCoeff;
		level = level > 1e-20f ? level : 0.f; // the follower's tail must not go denormal
		if (level > presentV())
			gain += riseCoeff * (1.0 - gain);
		else
			gain -= fallCoeff * gain;
		gain = gain > 1e-12 ? gain : 0.0;
		return x * (float) gain;
	}

	void clear() {
		level = 0.f;
		gain = 0.0;
	}
};

} // namespace fog
} // namespace rka

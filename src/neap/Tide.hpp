#pragma once
// Neap's TIDE: the weighted sum of the eight cycles, a glided Weight and a
// soft limiter; and the HIGH gate that watches it. Rack-free, C++11; values
// are in the plan (docs/plans/2026-11-24-neap.md, Global Constraints).
#include "FastMath.hpp"
#include "NeapRate.hpp"

namespace rka { namespace neap {

// Starting gain ahead of the limiter; to be tuned by ear.
constexpr float kTideGain = 2.0f;

// x (27 + x^2) / (27 + 9 x^2) for |x| < 3, else +-1 (continuous at 3, slope 1
// at 0). NaN gives 0.
inline float tideLimit(float x) {
	if (x != x)
		return 0.f;
	if (x >= 3.f)
		return 1.f;
	if (x <= -3.f)
		return -1.f;
	const float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

struct Tide {
	void reset() {
		w = 0.f;
		target = 0.f;
		haveWeights = false;
	}

	// weightTarget is already clamped to [-1, 1] (a NaN keeps the previous
	// target). The glided weight follows it with a 50 ms time constant. Returns
	// TIDE in +-5 V.
	float step(const float x[kCycles], float weightTarget, float sampleTime) {
		if (weightTarget == weightTarget)
			target = weightTarget;
		if (sampleTime != coefDt) {
			// 1 - exp(-dt / 50 ms); NaN or negative dt freezes the glide.
			coef = sampleTime > 0.f ? 1.f - fastExp2(-sampleTime * (1.f / kGlideSeconds) * 1.44269504088896f) : 0.f;
			coefDt = sampleTime;
		}
		float nw = w + (target - w) * coef;
		// Settled: stop recomputing the weights. The one-pole stalls short of
		// its target once the step is under half a float's spacing (about 7e-5
		// away at 48 kHz, 1e-3 at 768 kHz), so a threshold alone never fires
		// there: snap when within 1e-4 or when the step changed nothing.
		if ((nw - target < 1e-4f && target - nw < 1e-4f) || nw == w)
			nw = target;
		if (nw != w || !haveWeights) {
			w = nw;
			float sum = 0.f;
			for (int j = 0; j < kCycles; j++) {
				weights[j] = fastExp2(w * (float(j) - 3.5f));
				sum += weights[j];
			}
			invSum = 1.f / sum;
			haveWeights = true;
		}
		float acc = 0.f;
		for (int j = 0; j < kCycles; j++)
			acc += weights[j] * x[j];
		const float raw = kTideGain * acc * invSum;
		return 5.f * tideLimit(raw);  // a NaN cycle gives 0 V
	}

	float weight() const { return w; }

private:
	static constexpr float kGlideSeconds = 0.05f;
	float w = 0.f;
	float target = 0.f;
	float coef = 0.f;
	float coefDt = -1.f;
	float weights[kCycles] = {};
	float invSum = 1.f;
	bool haveWeights = false;
};

struct HighGate {
	bool high = false;

	// High above mark + 0.05 V, low below mark - 0.05 V, else unchanged (a NaN
	// tide or mark too). Returns the state.
	bool step(float tideVolts, float markVolts) {
		if (tideVolts > markVolts + 0.05f)
			high = true;
		else if (tideVolts < markVolts - 0.05f)
			high = false;
		return high;
	}
};

}} // namespace rka::neap

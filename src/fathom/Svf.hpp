#pragma once
#include <algorithm>
#include <cmath>

// Rack-independent state-variable filter, topology-preserving transform
// (Zavalishin, The Art of VA Filter Design, ch. 4; Cytomic's "SVF Linear
// Trapezoidal" paper is the compact reference). One pass gives LP, BP and
// HP; the damping k = 1/Q is set directly so later phases can map a
// resonance knob onto it. Stable under per-sample cutoff changes, which a
// biquad is not.
namespace rka {

// k for a Butterworth (Q = 0.707) response: -3 dB at the cutoff.
static const float BUTTERWORTH_K = 1.41421356f;

struct Svf {
	// Integrator states.
	float ic1 = 0.f;
	float ic2 = 0.f;
	// Coefficients; a fresh filter is 1 kHz Butterworth at 48 kHz.
	float k = BUTTERWORTH_K;
	float a1 = 0.f;
	float a2 = 0.f;
	float a3 = 0.f;

	struct Out {
		float lp;
		float bp;
		float hp;
	};

	Svf() {
		setCoefficients(1000.f, BUTTERWORTH_K, 48000.f);
	}

	void reset() {
		ic1 = ic2 = 0.f;
	}

	// Cutoff clamped to [1 Hz, 0.45 fs] so tan() stays finite.
	void setCoefficients(float cutoffHz, float damping, float sampleRate) {
		const float fc = std::max(1.f, std::min(cutoffHz, 0.45f * sampleRate));
		const float g = std::tan(float(M_PI) * fc / sampleRate);
		k = damping;
		a1 = 1.f / (1.f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}

	Out process(float in) {
		const float v3 = in - ic2;
		const float v1 = a1 * ic1 + a2 * v3;
		const float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		Out o;
		o.lp = v2;
		o.bp = v1;
		o.hp = in - k * v1 - v2;
		return o;
	}
};

} // namespace rka

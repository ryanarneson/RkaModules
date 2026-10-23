#pragma once
#include <cmath>

// Rack-independent LFO engine: one phase accumulator and its waveforms.
// Kept free of rack.hpp so test_lfo_core.cpp can drive it without libRack.
namespace rka {

struct LfoCore {
	// Normalized phase in [0, 1).
	float phase = 0.f;

	void reset() {
		phase = 0.f;
	}

	// Advance by one sample. freq in Hz, sampleTime in seconds.
	void step(float freq, float sampleTime) {
		phase += freq * sampleTime;
		// floor() rather than a single subtract keeps phase in range even
		// when freq * sampleTime exceeds 1 (huge FM, tiny sample rates).
		if (phase >= 1.f || phase < 0.f)
			phase -= std::floor(phase);
	}

	// All waveforms are normalized to [-1, 1]; the module scales to volts.
	// SIN and TRI start at 0 rising, so they stay in phase with each other.
	float sin() const {
		return std::sin(2.f * float(M_PI) * phase);
	}

	float tri() const {
		if (phase < 0.25f)
			return 4.f * phase;
		if (phase < 0.75f)
			return 2.f - 4.f * phase;
		return 4.f * phase - 4.f;
	}

	float saw() const {
		return 2.f * phase - 1.f;
	}

	float sqr(float pw) const {
		return phase < pw ? 1.f : -1.f;
	}
};

// Rate knob value is log2(Hz); V/OCT input adds to it.
inline float lfoFrequency(float rateParam, float fmVolts) {
	const float pitch = rateParam + fmVolts;
	// Clamp the exponent so a wild CV can't produce inf or denormals.
	const float clamped = pitch < -16.f ? -16.f : (pitch > 12.f ? 12.f : pitch);
	return std::exp2(clamped);
}

// Bipolar: [-1, 1] -> [-5, 5] V. Unipolar: [-1, 1] -> [0, 10] V.
inline float lfoVolts(float x, bool unipolar) {
	return unipolar ? 5.f * (x + 1.f) : 5.f * x;
}

} // namespace rka

#pragma once
#include <algorithm>
#include <cmath>

// Rack-independent rate mapping: RATE knob + FM volts -> Hz, per range.
namespace rka {

enum Range {
	RANGE_SLOW = 0,
	RANGE_NORMAL = 1,
	RANGE_AUDIO = 2,
};

// log2(Hz). RATE runs -7..7; FM adds 1 V/oct in every range.
// Slow: 2^-14..1 Hz. Normal: 2^-7..2^7 Hz. Audio: C4 +/- 5 octaves.
inline float rateExponent(float rate, float fm, Range range) {
	switch (range) {
		case RANGE_SLOW: return rate - 7.f + fm;
		case RANGE_AUDIO: return std::log2(261.6256f) + rate * 5.f / 7.f + fm;
		default: return rate + fm;
	}
}

// Keep frequencies below Nyquist with some margin.
inline double clampHz(double hz, float sampleRate) {
	return std::min(hz, 0.45 * sampleRate);
}

// Clamp the exponent so wild CV can't produce inf or denormals.
inline double expToHz(float exponent, float sampleRate) {
	const float e = std::max(-24.f, std::min(exponent, 14.f));
	return clampHz(std::exp2(double(e)), sampleRate);
}

// Bipolar: [-1, 1] -> [-5, 5] V. Unipolar: [-1, 1] -> [0, 10] V.
inline float lfoVolts(float x, bool unipolar) {
	return unipolar ? 5.f * (x + 1.f) : 5.f * x;
}

} // namespace rka

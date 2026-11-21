#pragma once

// Rack-free 2x resampling pair, for running a nonlinear filter at twice the
// engine rate. A polyphase IIR halfband: two allpass branches A and B, each
// a three-section cascade run at the base rate, forming
// H(z) = (1/2)(A(z^2) + z^-1 B(z^2)). The passband below the base-rate
// Nyquist is flat within 1e-7 dB and the stopband above it is 78 dB down.
//
// Adapted from VCV Fundamental's VCF.cpp (GPL-3.0-or-later). A template on
// the sample type: float (Fathom, Fog) or rka::Float4 (Narrows, four
// signals at once). Upsampler2x and Downsampler2x are the float ones.
#include "../Float4.hpp"

namespace rka {

// One sample through a cascade of first-order allpass sections,
// H(z) = (a + z^-1) / (1 + a z^-1) each; `state` is updated in place.
template <typename T>
inline T allpassCascade(T x, T* state, const float* coef, int sections) {
	for (int i = 0; i < sections; i++) {
		const float a = coef[i];
		const T y = a * x + state[i];
		state[i] = x - a * y;
		x = y;
	}
	return x;
}

static const float HALFBAND_2X_A[3] = {0.062822416060049985f, 0.4243808557204406f, 0.7818614603969013f};
static const float HALFBAND_2X_B[3] = {0.22380733034648345f, 0.61653443504951111f, 0.92747359487482584f};

// One sample in, two oversampled-rate samples out.
template <typename T>
struct Upsampler2xT {
	T stateA[3];
	T stateB[3];

	Upsampler2xT() {
		reset();
	}

	void reset() {
		for (int i = 0; i < 3; i++)
			stateA[i] = stateB[i] = splat<T>(0.f);
	}

	void process(T in, T out[2]) {
		out[0] = allpassCascade(in, stateA, HALFBAND_2X_A, 3);
		out[1] = allpassCascade(in, stateB, HALFBAND_2X_B, 3);
	}
};

// Two oversampled-rate samples in, one sample out. The branches are swapped
// from the upsampler (A takes in[1], B takes in[0]) so a round trip has
// unity gain in the passband.
template <typename T>
struct Downsampler2xT {
	T stateA[3];
	T stateB[3];

	Downsampler2xT() {
		reset();
	}

	void reset() {
		for (int i = 0; i < 3; i++)
			stateA[i] = stateB[i] = splat<T>(0.f);
	}

	T process(const T in[2]) {
		const T a = allpassCascade(in[1], stateA, HALFBAND_2X_A, 3);
		const T b = allpassCascade(in[0], stateB, HALFBAND_2X_B, 3);
		return 0.5f * (a + b);
	}
};

typedef Upsampler2xT<float> Upsampler2x;
typedef Downsampler2xT<float> Downsampler2x;

} // namespace rka

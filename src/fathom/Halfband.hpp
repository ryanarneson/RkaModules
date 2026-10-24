#pragma once

// Rack-free 2x resampling pair, for running a nonlinear filter at twice the
// engine rate. A polyphase IIR halfband: two allpass branches A and B, each
// a three-section cascade run at the base rate, forming
// H(z) = (1/2)(A(z^2) + z^-1 B(z^2)). The passband below the base-rate
// Nyquist is flat within 1e-7 dB and the stopband above it is 78 dB down.
//
// Adapted from VCV Fundamental's VCF.cpp (GPL-3.0-or-later), scalar float.
namespace rka {

// One sample through a cascade of first-order allpass sections,
// H(z) = (a + z^-1) / (1 + a z^-1) each; `state` is updated in place.
inline float allpassCascade(float x, float* state, const float* coef, int sections) {
	for (int i = 0; i < sections; i++) {
		const float a = coef[i];
		const float y = a * x + state[i];
		state[i] = x - a * y;
		x = y;
	}
	return x;
}

static const float HALFBAND_2X_A[3] = {0.062822416060049985f, 0.4243808557204406f, 0.7818614603969013f};
static const float HALFBAND_2X_B[3] = {0.22380733034648345f, 0.61653443504951111f, 0.92747359487482584f};

// One sample in, two oversampled-rate samples out.
struct Upsampler2x {
	float stateA[3] = {0.f, 0.f, 0.f};
	float stateB[3] = {0.f, 0.f, 0.f};

	void reset() {
		for (int i = 0; i < 3; i++)
			stateA[i] = stateB[i] = 0.f;
	}

	void process(float in, float out[2]) {
		out[0] = allpassCascade(in, stateA, HALFBAND_2X_A, 3);
		out[1] = allpassCascade(in, stateB, HALFBAND_2X_B, 3);
	}
};

// Two oversampled-rate samples in, one sample out. The branches are swapped
// from the upsampler (A takes in[1], B takes in[0]) so a round trip has
// unity gain in the passband.
struct Downsampler2x {
	float stateA[3] = {0.f, 0.f, 0.f};
	float stateB[3] = {0.f, 0.f, 0.f};

	void reset() {
		for (int i = 0; i < 3; i++)
			stateA[i] = stateB[i] = 0.f;
	}

	float process(const float in[2]) {
		const float a = allpassCascade(in[1], stateA, HALFBAND_2X_A, 3);
		const float b = allpassCascade(in[0], stateB, HALFBAND_2X_B, 3);
		return 0.5f * (a + b);
	}
};

} // namespace rka

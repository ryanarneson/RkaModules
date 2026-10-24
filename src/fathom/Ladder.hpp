#pragma once
#include "Halfband.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Rack-free four-pole transistor ladder, 2x oversampled, with the stage
// outputs mixable into low-pass 24, low-pass 12, band-pass and high-pass.
//
// Structure after VCV Fundamental's VCF.cpp (GPL-3.0-or-later): the ladder
// model of Huovilainen, "Non-linear digital implementation of the Moog
// ladder filter" (DAFx 2004); trapezoidal integrators from Zavalishin,
// "The Art of VA Filter Design"; the per-stage tanh linearisation from
// Teemu "Mystran" Voipio (KVR, 2012); the rational tan and tanh(x)/x
// approximations and the halfband pair are Fundamental's. The output mixing
// table is the Oberheim Xpander's, via Electric Druid's write-up.
//
// Two things on top of Fundamental's: the input is boosted by (1 + k) in the
// low-pass types so the passband stays at unity as resonance rises (the
// loop's DC gain is 1/(1 + k)), and a tiny noise term starts self-
// oscillation from silence.
namespace rka {

// tan(x) by a degree (3,4) rational function; |x| < pi/2, 2.8e-5 relative.
inline float tan34(float x) {
	const float x2 = x * x;
	const float num = 1.f + x2 * -0.09776575533683811f;
	const float den = 1.f + x2 * (-0.43119539396382f + x2 * 0.0105011966117302f);
	return x * num / den;
}

// tanh(x)/x by a degree (4,6) rational function; |x| <= 4, 4.2e-6 relative.
inline float tanhXdX(float x) {
	const float x2 = x * x;
	const float num = 1.f + x2 * (0.121953514066257f + x2 * 0.00204623480007919f);
	const float den = 1.f + x2 * (0.455305674254515f + x2 * (0.0204552909446164f + x2 * 9.48027717633287e-05f));
	return num / den;
}

enum class LadderType { LP24, LP12, BP, HP };

// The feedback at the top of RES: just past the structure's exact
// self-oscillation point at 4.
static const float LADDER_K_MAX = 4.1f;
// How much of the 1/(1 + k) passband loss the low-pass types make up: 1 is
// all of it.
static const float BASS_COMP = 1.f;
// White noise into the loop so self-oscillation starts from silence.
static const float LADDER_NOISE = 1e-6f;

struct Ladder {
	Upsampler2x up;
	Downsampler2x down;
	// Trapezoidal integrator states and each pole's last output; yPrev[3]
	// is the resonance feedback.
	float s[4] = {0.f, 0.f, 0.f, 0.f};
	float yPrev[4] = {0.f, 0.f, 0.f, 0.f};
	// Coefficients.
	float g = 0.f;
	float k = 0.f;
	float comp = 1.f;
	LadderType type = LadderType::LP24;

	Ladder() {
		setCoefficients(1000.f, 0.f, LadderType::LP24, 48000.f);
	}

	void reset() {
		for (int i = 0; i < 4; i++)
			s[i] = yPrev[i] = 0.f;
		uPrev = 0.f;
		up.reset();
		down.reset();
	}

	// Per-pole frequency in Hz, clamped to [1, 0.45 fs]; k is the feedback
	// (0 ... LADDER_K_MAX); the bilinear prewarp is at the oversampled rate.
	void setCoefficients(float cutoffHz, float feedback, LadderType t, float sampleRate) {
		const float fc = std::max(1.f, std::min(cutoffHz, 0.45f * sampleRate));
		g = tan34(float(M_PI) * fc / (2.f * sampleRate));
		k = feedback;
		type = t;
		comp = (t == LadderType::LP24 || t == LadderType::LP12) ? 1.f + BASS_COMP * k : 1.f;
	}

	// One base-rate sample in and out; the core runs twice inside.
	float process(float x) {
		float os[2];
		up.process(x, os);
		os[0] = core(os[0]);
		os[1] = core(os[1]);
		return down.process(os);
	}

	// One oversampled-rate sample.
	float core(float x) {
		// Per-stage tanh slope from the previous output, floored so every
		// stage keeps damping itself; the input tanh's slope from the
		// previous loop input.
		const float tMin = 0.01f;
		const float t0 = std::max(tanhXdX(yPrev[0]), tMin);
		const float t1 = std::max(tanhXdX(yPrev[1]), tMin);
		const float t2 = std::max(tanhXdX(yPrev[2]), tMin);
		const float t3 = std::max(tanhXdX(yPrev[3]), tMin);
		const float tu = std::max(tanhXdX(uPrev), tMin);

		// With the slopes fixed, each stage is y_i = a_i * in_i + b_i, so the
		// last stage is y3 = P * u_sat + Q and the feedback loop can be
		// solved for this sample's u instead of using the last one's
		// (Zavalishin's zero-delay feedback): self-oscillation then sits at
		// the pole frequency at any sample rate.
		const float a0 = g / (1.f + g * t0), b0 = s[0] / (1.f + g * t0);
		const float a1 = g / (1.f + g * t1), b1 = s[1] / (1.f + g * t1);
		const float a2 = g / (1.f + g * t2), b2 = s[2] / (1.f + g * t2);
		const float a3 = g / (1.f + g * t3), b3 = s[3] / (1.f + g * t3);
		const float P = a3 * t2 * a2 * t1 * a1 * t0 * a0;
		const float Q = a3 * t2 * (a2 * t1 * (a1 * t0 * b0 + b1) + b2) + b3;
		const float u0 = (comp * x - k * Q + LADDER_NOISE * white()) / (1.f + k * P * tu);
		const float u0Clamped = std::max(-4.f, std::min(u0, 4.f));
		const float u0Sat = tanhXdX(u0Clamped) * u0Clamped;
		uPrev = u0Clamped;

		const float y0 = a0 * u0Sat + b0;
		const float y1 = a1 * t0 * y0 + b1;
		const float y2 = a2 * t1 * y1 + b2;
		const float y3 = a3 * t2 * y2 + b3;

		s[0] = 2.f * y0 - s[0];
		s[1] = 2.f * y1 - s[1];
		s[2] = 2.f * y2 - s[2];
		s[3] = 2.f * y3 - s[3];
		yPrev[0] = y0;
		yPrev[1] = y1;
		yPrev[2] = y2;
		yPrev[3] = y3;

		// Xpander mix of the post-feedback node and the four poles.
		float v;
		switch (type) {
			case LadderType::LP12: v = y1; break;
			case LadderType::BP: v = 4.f * y1 - 8.f * y2 + 4.f * y3; break;
			case LadderType::HP: v = u0Clamped - 4.f * y0 + 6.f * y1 - 4.f * y2 + y3; break;
			default: v = y3; break;
		}
		// Soft limit to +/-2: 2 tanh(v/2).
		return v * tanhXdX(0.5f * v);
	}

private:
	// Last loop input, for the input tanh's slope.
	float uPrev = 0.f;
	uint64_t noise = 7;

	// Uniform in [-1, 1], splitmix64 as Drift.hpp.
	float white() {
		uint64_t z = (noise += 0x9e3779b97f4a7c15ull);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		z ^= z >> 31;
		return float(z >> 40) / float((1 << 24) - 1) * 2.f - 1.f;
	}
};

} // namespace rka

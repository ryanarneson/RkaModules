#pragma once
#include "Halfband.hpp"
#include "LadderCore.hpp"
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
// The stage arithmetic is in LadderCore.hpp, shared with Narrows.
//
// Two things on top of Fundamental's: the input is boosted by (1 + k) in the
// low-pass types so the passband stays at unity as resonance rises (the
// loop's DC gain is 1/(1 + k)), and a tiny noise term starts self-
// oscillation from silence.
namespace rka {

enum class LadderType { LP24, LP12, BP, HP };

// The feedback at the top of RES: just past the structure's exact
// self-oscillation point at 4.
static const float LADDER_K_MAX = 4.1f;
// How much of the 1/(1 + k) passband loss the low-pass types make up: 1 is
// all of it.
static const float BASS_COMP = 1.f;

struct Ladder {
	Upsampler2x up;
	Downsampler2x down;
	// The poles and the loop.
	LadderCore<float> ladder;
	// Coefficients.
	float g = 0.f;
	float k = 0.f;
	float comp = 1.f;
	LadderType type = LadderType::LP24;

	Ladder() {
		setCoefficients(1000.f, 0.f, LadderType::LP24, 48000.f);
	}

	void reset() {
		ladder.reset();
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
		const LadderNodes<float> n = ladder.step(x, g, k, comp, white());
		// Xpander mix of the post-feedback node and the four poles.
		float v;
		switch (type) {
			case LadderType::LP12: v = n.y1; break;
			case LadderType::BP: v = 4.f * n.y1 - 8.f * n.y2 + 4.f * n.y3; break;
			case LadderType::HP: v = n.u - 4.f * n.y0 + 6.f * n.y1 - 4.f * n.y2 + n.y3; break;
			default: v = n.y3; break;
		}
		return ladderLimit(v);
	}

private:
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

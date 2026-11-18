#pragma once
// Pan's core (spec: docs/specs/2026-11-17-pan-design.md): a Ballast
// expander, Rack-free. Pan.cpp only supplies readFrame() (its params and
// inputs), idle() (how long since Ballast last linked it) and LINK.
//
// Ballast calls linked() once per sample: the core reads its controls,
// smooths each knob (one-pole, about 10 ms) and forms each channel's pan
//   position = clamp(smoothed knob + CV / 10 V x attenuverter, 0, 1)
//   p = 2 position - 1 (-1 hard left, 0 centre, +1 hard right)
// The CV is not smoothed. After a gap (idle() above 2, or the first link),
// the knob smoothing restarts from centre, so attaching a panned Pan glides
// in rather than clicking. Then postLevel() pans each channel, by one of two
// pan laws (the user's choice, after a hard pan drove a hot mix into
// Ballast's soft limit):
//   no boost (law 0, the default): no sample leaves louder than the
//     louder side went in, so panning can never clip a mix that was clean
//   constant loudness (law 1): equal power, up to +3 dB at the sides
// Mono (BallastSource MONO), t = (p + 1) pi / 4:
//   law 1: gL = sqrt 2 cos t, gR = sqrt 2 sin t (unity both sides at
//     centre, +3 dB hard over); law 0: the same, each held to 1
// Stereo, true pan (mode 0), for p >= 0, t = p pi / 2, s = sin t:
//   L' = L cos t, R' = R (1 - (1 - k) s) + L k s, mirrored for p < 0;
//   k = 1/2 (law 0: hard right is (L + R) / 2) or 1 / sqrt 2 (law 1)
// Stereo, balance (mode 1): for p >= 0, L' = L cos t; mirrored (never
//   boosts, under either law)
// p = 0 returns at once, so a centred Pan is the identity, bit for bit.
//
// Threads: all of this runs inside Ballast's calls (BallastExpander.hpp);
// no allocation and no libm per sample (FastMath).
#include "FastMath.hpp"
#include "ballast/BallastEngine.hpp"

#include <cmath>

namespace rka {

struct PanFrame {
	float knob[4] = {0.5f, 0.5f, 0.5f, 0.5f}, att[4] = {};
	BallastPort cv[4];
	int stereoMode = 0;   // 0 true pan, 1 balance
	int panLaw = 0;       // 0 no boost, 1 constant loudness (+3 dB at the sides)
};

class PanCore : public BallastExpander {
public:
	static const int KIND = 1;

	int kind() const override {
		return KIND;
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;
		smooth_ = (float) (1.0 - std::exp(-1.0 / (0.01 * fs)));
	}

	void linked() override {
		readFrame(frame_);
		const bool restart = first_ || idle() > 2;
		first_ = false;
		for (int c = 0; c < 4; c++) {
			const float knob = ballastFinite(frame_.knob[c]);
			if (restart)
				knobS_[c] = 0.5f;
			// Snap the last 1e-4: a float one-pole stalls about 3e-5 short
			// (the step falls under half an ulp), so hard left or right
			// would never quite arrive.
			const float d = knob - knobS_[c];
			knobS_[c] = (d > 1e-4f || d < -1e-4f) ? knobS_[c] + d * smooth_ : knob;
			const float pos = knobS_[c] + frame_.cv[c].first() * 0.1f * ballastFinite(frame_.att[c]);
			pos_[c] = pos > 0.f ? (pos < 1.f ? pos : 1.f) : 0.f;
		}
		balance_ = frame_.stereoMode == 1;
		constantLoudness_ = frame_.panLaw == 1;
	}

	void postLevel(int ch, float& l, float& r, BallastSource src) override {
		const float p = 2.f * pos_[ch] - 1.f;
		if (p == 0.f)
			return;
		if (src == MONO) {
			// l == r: the mono signal.
			const float t = (p + 1.f) * 0.125f;   // theta / 2 pi
			const float m = l;
			float gl = 1.41421356f * fastSin2pi(t + 0.25f), gr = 1.41421356f * fastSin2pi(t);
			if (!constantLoudness_) {
				gl = gl < 1.f ? gl : 1.f;
				gr = gr < 1.f ? gr : 1.f;
			}
			l = m * gl;
			r = m * gr;
			return;
		}
		const float q = p >= 0.f ? p : -p;
		const float c = fastSin2pi(q * 0.25f + 0.25f), s = fastSin2pi(q * 0.25f);
		const float k = constantLoudness_ ? 0.70710678f : 0.5f;
		const float keep = 1.f - (1.f - k) * s, fold = k * s;
		if (p > 0.f) {
			if (!balance_)
				r = r * keep + l * fold;
			l *= c;
		}
		else {
			if (!balance_)
				l = l * keep + r * fold;
			r *= c;
		}
	}

	// The pan position (0 hard left, 0.5 centre, 1 hard right), for tests.
	float position(int ch) const {
		return pos_[ch];
	}

protected:
	virtual void readFrame(PanFrame& f) = 0;
	// Pan's process() calls since Ballast last linked it (0 when unknown).
	virtual int idle() const {
		return 0;
	}

private:
	PanFrame frame_;
	float knobS_[4] = {0.5f, 0.5f, 0.5f, 0.5f};
	float pos_[4] = {0.5f, 0.5f, 0.5f, 0.5f};
	float smooth_ = 0.002f;
	bool balance_ = false;
	bool constantLoudness_ = false;
	bool first_ = true;
};

} // namespace rka

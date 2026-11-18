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
// in rather than clicking. Then postLevel() pans each channel:
//   mono (BallastSource MONO): equal power, gL = sqrt 2 cos t, gR =
//     sqrt 2 sin t, t = (p + 1) pi / 4: unity both sides at centre
//   stereo, true pan (mode 0): for p >= 0, t = p pi / 2: L' = L cos t,
//     R' = R + L sin t; mirrored for p < 0
//   stereo, balance (mode 1): for p >= 0: L' = L cos(p pi / 2); mirrored
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
	}

	void postLevel(int ch, float& l, float& r, BallastSource src) override {
		const float p = 2.f * pos_[ch] - 1.f;
		if (p == 0.f)
			return;
		if (src == MONO) {
			// l == r: the mono signal.
			const float t = (p + 1.f) * 0.125f;   // theta / 2 pi
			const float m = l;
			l = m * 1.41421356f * fastSin2pi(t + 0.25f);
			r = m * 1.41421356f * fastSin2pi(t);
			return;
		}
		const float q = p >= 0.f ? p : -p;
		const float c = fastSin2pi(q * 0.25f + 0.25f), s = fastSin2pi(q * 0.25f);
		if (p > 0.f) {
			if (!balance_)
				r += l * s;
			l *= c;
		}
		else {
			if (!balance_)
				l += r * s;
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
	bool first_ = true;
};

} // namespace rka

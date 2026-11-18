#pragma once
// Ballast's engine (spec: docs/specs/2026-11-17-ballast-design.md).
// Rack-free; Ballast.cpp only maps ports, params, the menu and the expander
// chain onto it.
//
// Per sample, when either output is patched:
//   each channel: IN L (summed), IN R (summed, or IN L's copy when
//     unpatched); expanders' preLevel; x Level (the knob's curve, smoothed)
//     x its CV (VCA law: clamp(CV / 10 V, 0, 1), 1 when unpatched, not
//     smoothed); expanders' postLevel; summed per side
//   + CHAIN L and R (R normalled to L), at unity; expanders' intoMix
//   x Master (its curve, smoothed) x its CV (the same VCA law)
//   L: the left mix, or (L + R) / sqrt 2 when R is unpatched (crossfaded
//     over 10 ms); R: the right mix
//   then, per side, the DC blocker (if on) and the soft limit.
// The knob curve is 2 p^2.4094 (ln 2 / ln 4/3): silence at 0, unity at 0.75, +6 dB at 1; read
// from a table built on construction. With neither output patched nothing
// is computed.
//
// No allocation and no libm in process(); std::pow and std::exp run on
// construction and on a sample-rate change. Unusable sample rates (not
// finite, <= 0, above 768 kHz) are ignored and the last good one kept.
#include "BallastExpander.hpp"
#include "FastMath.hpp"

#include <cmath>

namespace rka {

// NaN and +-inf read 0 (NaN fails both comparisons).
inline float ballastFinite(float x) {
	return (x > -1e30f && x < 1e30f) ? x : 0.f;
}

struct BallastPort {
	const float* v = nullptr;
	int channels = 0;
	bool patched() const {
		return channels > 0 && v;
	}
	// Every channel added; 0 when unpatched.
	float sum() const {
		float s = 0.f;
		if (v)
			for (int c = 0; c < channels && c < 16; c++)
				s += ballastFinite(v[c]);
		return s;
	}
	float first() const {
		return patched() ? ballastFinite(v[0]) : 0.f;
	}
};

struct BallastFrame {
	enum { LEVEL_1, LEVEL_2, LEVEL_3, LEVEL_4, MASTER, PARAMS_LEN };
	float params[PARAMS_LEN] = {};
	BallastPort inL[4], inR[4], cv[4], masterCv, chainL, chainR;
	bool outL = false, outR = false, dcBlock = true;
};

struct BallastResult {
	float l = 0, r = 0;
	bool clip = false;
};

class BallastEngine {
public:
	static const int TABLE = 256;   // intervals; 0.75 falls on a point
	static constexpr float KNEE = 8.f, CEIL = 10.f;

	BallastEngine() {
		for (int i = 0; i <= TABLE; i++)
			table_[i] = curveExact((float) i / TABLE);
		setSampleRate(48000.f);
	}

	// The knob curve with libm, for construction and tests: 2 p^k with
	// k = ln 2 / ln (4/3), about 2.41, so 0.75 is exactly unity.
	static float curveExact(float p) {
		return p > 0.f ? (float) (2.0 * std::pow((double) p, 2.4094208396532095)) : 0.f;
	}

	// The knob curve from the table: p clamped to [0, 1], NaN reads 0.
	float curve(float p) const {
		const float x = (p > 0.f ? (p < 1.f ? p : 1.f) : 0.f) * TABLE;
		int i = (int) x;
		i = i < TABLE ? i : TABLE - 1;
		const float t = x - i;
		return table_[i] + t * (table_[i + 1] - table_[i]);
	}

	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;   // keep the last good rate (48 kHz from construction)
		rate_ = fs;
		smooth_ = (float) (1.0 - std::exp(-1.0 / (0.01 * fs)));
		fadeStep_ = 1.f / (0.01f * fs);
		snap_ = true;
	}

	void reset() {
		snap_ = true;
		work_ = 0;
	}

	void process(const BallastFrame& f, BallastResult& out, BallastExpander* const* chain = nullptr,
	             int chainLen = 0) {
		const float monoTarget = f.outR ? 0.f : 1.f;
		if (snap_) {
			for (int p = 0; p < BallastFrame::PARAMS_LEN; p++)
				pos_[p] = ballastFinite(f.params[p]);
			mono_ = monoTarget;
			snap_ = false;
		}
		for (int p = 0; p < BallastFrame::PARAMS_LEN; p++)
			pos_[p] += (ballastFinite(f.params[p]) - pos_[p]) * smooth_;
		mono_ = ramp(mono_, monoTarget);
		if (!f.outL && !f.outR) {
			out = BallastResult();
			return;
		}
		work_++;

		float mixL = 0.f, mixR = 0.f;
		for (int c = 0; c < 4; c++) {
			const bool hasL = f.inL[c].patched(), hasR = f.inR[c].patched();
			if (!hasL && !hasR && chainLen == 0)
				continue;
			float l = f.inL[c].sum();
			float r = hasR ? f.inR[c].sum() : l;
			for (int k = 0; k < chainLen; k++)
				chain[k]->preLevel(c, l, r);
			const float g = curve(pos_[c]) * vca(f.cv[c]);
			l *= g;
			r *= g;
			const bool mono = hasL && !hasR;
			for (int k = 0; k < chainLen; k++)
				chain[k]->postLevel(c, l, r, mono);
			mixL += l;
			mixR += r;
		}
		const float chL = f.chainL.sum();
		mixL += chL;
		mixR += f.chainR.patched() ? f.chainR.sum() : chL;
		for (int k = 0; k < chainLen; k++)
			chain[k]->intoMix(mixL, mixR);
		const float m = curve(pos_[BallastFrame::MASTER]) * vca(f.masterCv);
		mixL *= m;
		mixR *= m;
		float l = mono_ * (mixL + mixR) * 0.70710678f + (1.f - mono_) * mixL;
		float r = mixR;
		out.l = l;
		out.r = r;
		out.clip = false;
	}

	long work() const {
		return work_;
	}

private:
	// The VCA law: an unpatched CV is fully open.
	static float vca(const BallastPort& cv) {
		if (!cv.patched())
			return 1.f;
		const float g = cv.first() * 0.1f;
		return g > 0.f ? (g < 1.f ? g : 1.f) : 0.f;
	}

	float ramp(float v, float target) const {
		if (v < target)
			return v + fadeStep_ < target ? v + fadeStep_ : target;
		if (v > target)
			return v - fadeStep_ > target ? v - fadeStep_ : target;
		return v;
	}

	float table_[TABLE + 1];
	float rate_ = 48000.f;
	float smooth_ = 0.002f, fadeStep_ = 0.002f;
	float pos_[BallastFrame::PARAMS_LEN] = {};
	float mono_ = 0.f;
	bool snap_ = true;
	long work_ = 0;
};

} // namespace rka

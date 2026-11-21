#pragma once
// Narrows: the control laws, from knob positions and volts to the numbers
// the engine works in, and the words the tooltips show. Rack-free.
#include "../FastMath.hpp"
#include "NarrowsTuning.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace rka {
namespace narrows {

// The Cutoff knob (0 .. 1) in hertz, for display and typed values.
inline float cutoffHz(float knob) {
	return CUTOFF_BASE_HZ * std::exp2(CUTOFF_OCTAVES * knob);
}

inline float cutoffKnob(float hz) {
	if (!(hz > CUTOFF_BASE_HZ))
		return 0.f;
	const float k = std::log2(hz / CUTOFF_BASE_HZ) / CUTOFF_OCTAVES;
	return k < 1.f ? k : 1.f;
}

// One side's cutoff in octaves above CUTOFF_BASE_HZ. side 0 is left (Spread
// takes it down), 1 is right (up).
inline float octaves(float knob, float voct, float fmVolts, float fmKnob, float spread, int side) {
	return CUTOFF_OCTAVES * knob + voct + fmVolts * fmKnob + (side ? 0.5f : -0.5f) * SPREAD_OCTAVES * spread;
}

// Octaves to hertz without libm, for every sample.
inline float hzFromOctaves(float oct) {
	return CUTOFF_BASE_HZ * fastExp2(oct);
}

// The cutoff the filter is given: CUTOFF_MIN_HZ up to the lower of
// CUTOFF_MAX_HZ and CUTOFF_MAX_FS of the sample rate. NaN gives the floor.
// At a rate so low that the floor is past the ceiling, the ceiling.
inline float clampHz(float hz, float fs) {
	const float top = CUTOFF_MAX_FS * fs < CUTOFF_MAX_HZ ? CUTOFF_MAX_FS * fs : CUTOFF_MAX_HZ;
	const float floor = CUTOFF_MIN_HZ < top ? CUTOFF_MIN_HZ : top;
	if (!(hz > floor))
		return floor;
	return hz < top ? hz : top;
}

// Res (0 .. 1) to the ladder's feedback: Fathom's square law up to
// RES_ONSET (feedback 4, where self-oscillation begins), then a straight
// line to RES_K_MAX.
inline float ladderFeedback(float res) {
	if (res <= RES_ONSET) {
		const float r = res * (1.f / RES_ONSET);
		return RES_K_ONSET * r * r;
	}
	return RES_K_ONSET + (RES_K_MAX - RES_K_ONSET) * (res - RES_ONSET) * (1.f / (1.f - RES_ONSET));
}

// Drive (0 .. 1) to the gain into the core.
inline float driveGain(float drive) {
	return drive > 0.f ? fastExp2(DRIVE_OCTAVES * drive) : 1.f;
}

// Comb (-1 .. 1) to the comb's feedback: 0 at noon, +/-COMB_MAX at the
// ends, and quick off noon so the middle of each half is useful.
inline float combFeedback(float c) {
	c = c < -1.f ? -1.f : c > 1.f ? 1.f : c;
	const float a = c < 0.f ? -c : c;
	const float f = COMB_MAX * (1.f - (1.f - a) * (1.f - a));
	return c < 0.f ? -f : f;
}

// Mode (0 LP, 0.5 BP, 1 HP): straight blends between neighbours.
struct ModeWeights {
	float lp, bp, hp;
};

inline ModeWeights modeWeights(float m) {
	ModeWeights w;
	if (!(m > 0.f))
		m = 0.f;   // NaN too
	if (m > 1.f)
		m = 1.f;
	if (m <= 0.5f) {
		w.bp = 2.f * m;
		w.lp = 1.f - w.bp;
		w.hp = 0.f;
	}
	else {
		w.hp = 2.f * m - 1.f;
		w.bp = 1.f - w.hp;
		w.lp = 0.f;
	}
	return w;
}

// The ladder's mix: weights on its loop input and four poles (the Xpander
// rows Fathom uses for low-pass 24, band-pass and high-pass), and the
// low-pass weight, which scales the passband make-up.
struct LadderMix {
	float w[5];
	float lowpass;
};

inline LadderMix ladderMix(float m) {
	static const float ROWS[3][5] = {{0.f, 0.f, 0.f, 0.f, 1.f}, {0.f, 0.f, 4.f, -8.f, 4.f}, {1.f, -4.f, 6.f, -4.f, 1.f}};
	const ModeWeights mw = modeWeights(m);
	LadderMix out;
	for (int i = 0; i < 5; i++)
		out.w[i] = mw.lp * ROWS[0][i] + mw.bp * ROWS[1][i] + mw.hp * ROWS[2][i];
	out.lowpass = mw.lp;
	return out;
}

// "LP", "BP", "HP", or the blend: "LP 60 % / BP 40 %".
inline std::string modeName(float m) {
	const ModeWeights w = modeWeights(m);
	const int lp = (int) std::lround(100.f * w.lp), bp = (int) std::lround(100.f * w.bp),
	          hp = (int) std::lround(100.f * w.hp);
	if (lp == 100)
		return "LP";
	if (bp == 100)
		return "BP";
	if (hp == 100)
		return "HP";
	char text[40];
	if (lp > 0)
		std::snprintf(text, sizeof(text), "LP %d %% / BP %d %%", lp, bp);
	else
		std::snprintf(text, sizeof(text), "BP %d %% / HP %d %%", bp, hp);
	return text;
}

// A typed frequency: "440", "440 Hz", "2.5 k", "2.5 kHz". False (and hz
// untouched) for anything else, and for values that are not positive.
inline bool parseHz(const std::string& text, float& hz) {
	const char* s = text.c_str();
	char* end = nullptr;
	const double v = std::strtod(s, &end);
	if (end == s || !(v > 0.0) || !(v < 1e9))
		return false;
	while (*end == ' ')
		end++;
	double scale = 1.0;
	if (*end == 'k' || *end == 'K') {
		scale = 1000.0;
		end++;
	}
	if ((end[0] == 'H' || end[0] == 'h') && (end[1] == 'z' || end[1] == 'Z'))
		end += 2;
	while (*end == ' ')
		end++;
	if (*end != '\0')
		return false;
	hz = (float) (v * scale);
	return true;
}

} // namespace narrows
} // namespace rka

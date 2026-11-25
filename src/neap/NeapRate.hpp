#pragma once
// Rack-independent tables and rate law for Neap's eight slow cycles.
//
// Cycle indices are 0-based (cycle 1 is i = 0). e1 is log2 of cycle 1's
// frequency in Hz; cycle i runs at 2^e_i * (1 + drift) with
// e_i = e1 - L_i + s_i * (e1 - e1n): the steps between cycles are fixed
// "awkward" ratios, and a tiny slope s_i detunes them with the rate so no pair
// sits on a simple ratio anywhere in the range (ratiosClearOfLocks).
//
// formatPeriod rounding: ms below 1 s (3 significant figures: "850 ms",
// "42.5 ms"), then s below 60 s, min below 60 min, h beyond, each with one
// decimal ("1.6 s", "4.5 min", "4.8 h").
// A value that would print as 1000 ms / 60.0 s / 60.0 min rolls to the next
// unit, so the text never shows a full unit's worth. NaN or negative reads 0.
#include "FastMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rka { namespace neap {

constexpr int kCycles = 8;

// Cycle i to i+1.
static const float kSteps[kCycles - 1] = {2.16f, 1.93f, 2.25f, 1.93f, 1.93f, 2.23f, 2.17f};
// s_i, s_1 = 0.
static const float kSlopes[kCycles] = {0.f, 0.0014f, 0.0011f, 0.0009f, 0.0004f, 0.0002f, 0.0008f, 0.0007f};
// L_i = log2 of the product of the steps before cycle i (precomputed: no libm
// per sample).
static const float kOffsets[kCycles] = {
	0.f, 1.111031312f, 2.059632160f, 3.229557161f, 4.178158009f, 5.126758856f, 6.283802566f, 7.401497609f};

// log2(40) and the noon exponent log2(40) - 6.
static const float kLog2Top = 5.321928095f;
static const float kNoonExponent = 5.321928095f - 6.f;

inline float cycleOffset(int i) {
	return kOffsets[std::max(0, std::min(i, kCycles - 1))];
}

// e1: RATE 0..1 spans 12 octaves, the attenuated CV adds 1 V/oct, clamped to
// 14 octaves below / 2 above log2(40). NaN reads as 0
// (CV, attenuverter) or noon (knob).
inline float rateExponent(float rate, float att, float cvVolts) {
	if (!(rate == rate)) rate = 0.5f;
	if (!(att == att)) att = 0.f;
	if (!(cvVolts == cvVolts)) cvVolts = 0.f;
	rate = std::max(0.f, std::min(rate, 1.f));
	att = std::max(-1.f, std::min(att, 1.f));
	// A huge CV times att = 0 must not become inf * 0; clamp the CV first.
	cvVolts = std::max(-100.f, std::min(cvVolts, 100.f));
	const float e = kLog2Top - 12.f * (1.f - rate) + att * cvVolts;
	return std::max(kLog2Top - 14.f, std::min(e, kLog2Top + 2.f));
}

inline float cycleExponent(int i, float e1) {
	i = std::max(0, std::min(i, kCycles - 1));
	return e1 - kOffsets[i] + kSlopes[i] * (e1 - kNoonExponent);
}

inline double cycleHz(int i, float e1, float drift) {
	return fastExp2D(double(cycleExponent(i, e1))) * (1.0 + double(drift));
}

// Relative distance |r - n| / r from r to the nearest integer, or half-integer
// when r <= 6.
inline double lockMargin(double r) {
	const double grid = (r <= 6.0) ? 0.5 : 1.0;
	const double n = std::floor(r / grid + 0.5) * grid;
	return std::fabs(r - n) / r;
}

// The no-locking rule at e1: over every pair (i < j) with r = f_j / f_i <= 16
// the relative distance |r - n| / r to every integer n (and, for r <= 6, every
// half-integer) is at least 3 %. Offline check: uses libm.
inline bool ratiosClearOfLocks(float e1, float* worstMargin) {
	double worst = 1.0;
	for (int i = 0; i < kCycles; i++) {
		for (int j = i + 1; j < kCycles; j++) {
			// f_i / f_j > 1: slower cycle in the denominator.
			const double r = std::exp2(double(cycleExponent(i, e1)) - double(cycleExponent(j, e1)));
			if (!(r <= 16.0)) continue;
			worst = std::min(worst, lockMargin(r));
		}
	}
	if (worstMargin) *worstMargin = float(worst);
	return worst >= 0.03;
}

inline void formatPeriod(double seconds, char* buf, int n) {
	if (n <= 0) return;
	if (!(seconds > 0.0)) seconds = 0.0;
	if (seconds > 1e9) seconds = 1e9;
	if (seconds < 0.9995) {
		const double ms = seconds * 1000.0;
		if (ms >= 99.95) std::snprintf(buf, size_t(n), "%.0f ms", ms);
		else std::snprintf(buf, size_t(n), "%.1f ms", ms);
		// 999.5 ms .. 999.95 ms prints as "1000 ms"; the guard above avoids it.
	} else if (seconds < 59.95) {
		std::snprintf(buf, size_t(n), "%.1f s", seconds);
	} else if (seconds < 3597.0) {
		std::snprintf(buf, size_t(n), "%.1f min", seconds / 60.0);
	} else {
		std::snprintf(buf, size_t(n), "%.1f h", seconds / 3600.0);
	}
}

// "1: <period> · 8: <period>" (UTF-8 middle dot) of f_1 and f_8, spread
// included, drift not.
inline void rateTooltip(float rate, float att, float cvVolts, char* buf, int n) {
	if (n <= 0) return;
	const float e1 = rateExponent(rate, att, cvVolts);
	char a[24], b[24];
	formatPeriod(1.0 / cycleHz(0, e1, 0.f), a, sizeof a);
	formatPeriod(1.0 / cycleHz(kCycles - 1, e1, 0.f), b, sizeof b);
	std::snprintf(buf, size_t(n), "1: %s \xC2\xB7 8: %s", a, b);
}

}} // namespace rka::neap

#pragma once
// Rack-free control laws for Doldrums: grain sizes, the CV rule, the Stretch
// dial and its tooltip, the lag a Position asks for, the comb's root and
// width, Tilt, Shift, Mix, the staging gap and the RUN dip. Every number is
// in the plan's Global Constraints.
//
// Safe per sample (no libm; FastMath.hpp or plain arithmetic): cvValue,
// rootHz, widthCents, tiltDbPerOctave, shiftRatio, mixGains, lagFrames,
// grainFrames, tapeFrames, stepGap, runDipFrames, paceFromDial (fastExp2D,
// 1e-15). Control rate only: stretchText (snprintf, for the tooltip).
//
// Bad input: NaN and infinite voltages read as 0 V, a NaN knob as its lowest
// end (Mix and Shift: no change), so a stray sample never reaches the sound.
#include "FastMath.hpp"
#include <algorithm>
#include <cstdio>

namespace rka { namespace doldrums {

constexpr float kTapeSeconds = 30.f;
enum GrainSize { GRAIN_SHORT = 0, GRAIN_MEDIUM = 1, GRAIN_LONG = 2 };

namespace detail {

// x with NaN and infinity read as 0, then clamped to +-limit.
inline float volts(float v, float limit) {
	if (!(v > -1e30f && v < 1e30f)) return 0.f;
	return std::max(-limit, std::min(v, limit));
}

// x clamped to [lo, hi]; NaN reads as lo.
inline float clampLo(float x, float lo, float hi) {
	return x > lo ? (x < hi ? x : hi) : lo;
}

// Nearest whole number, halves up; for values far inside the int range.
inline long long roundHalfUp(double x) {
	return (long long) std::floor(x + 0.5);
}

} // namespace detail

// The power of two nearest (in log terms) to seconds * rate, capped at 131072:
// Short, Medium, Long are 0.17, 0.34, 0.68 s. Compares against sqrt 2 times
// the power, so there is no log.
inline int grainFrames(GrainSize g, float rate) {
	static const double kSeconds[3] = {0.17, 0.34, 0.68};
	const int idx = std::max(0, std::min(int(g), 2));
	const double want = kSeconds[idx] * double(detail::clampLo(rate, 0.f, 1e9f));
	int p = 1;
	while (p < 131072 && want >= double(p) * 1.4142135623730951)
		p *= 2;
	return p;
}

// round(30 * rate) frames.
inline long long tapeFrames(float rate) {
	return detail::roundHalfUp(double(kTapeSeconds) * double(detail::clampLo(rate, 0.f, 1e9f)));
}

// clamp(knob + att * volts / 10, 0, 1). A NaN knob or attenuverter reads 0,
// NaN or infinite volts read 0 V.
inline float cvValue(float knob, float att, float v) {
	knob = detail::clampLo(knob, 0.f, 1.f);
	att = detail::volts(att, 1.f);
	v = detail::volts(v, 100.f);
	return detail::clampLo(knob + att * v * 0.1f, 0.f, 1.f);
}

// The read point's pace (1 / stretch) for the dial fraction t after CV.
// t <= 5/6: stretch 2^(6t). Between 5/6 and 1: stretch 32 / (1 - u)^2 with
// u = 6t - 5, written as pace = 36 (1 - t)^2 / 32 so it is exact near the
// stop. t >= 1: exactly 0, held. NaN and t < 0 read 0.
inline double paceFromDial(float t) {
	if (!(t > 0.f)) return 1.0;
	if (t >= 1.f) return 0.0;
	const double d = double(t);
	if (d <= 5.0 / 6.0) return fastExp2D(-6.0 * d);
	const double r = 1.0 - d;
	return 1.125 * r * r;
}

// The tooltip: "x8.0" under 100, "x128" from 100, "held" at the stop.
inline void stretchText(double pace, char* buf, int n) {
	if (!buf || n <= 0) return;
	if (!(pace > 0.0)) {
		std::snprintf(buf, size_t(n), "held");
		return;
	}
	const double stretch = 1.0 / pace;
	// 99.96 and up would print as "x100.0"; it prints as "x100".
	if (stretch < 99.95)
		std::snprintf(buf, size_t(n), "x%.1f", stretch);
	else
		std::snprintf(buf, size_t(n), "x%.0f", stretch);
}

// L = round((1 - position) * (capacity - grain)); position 1 is now. NaN
// reads as position 0; a grain bigger than the tape leaves no room (0).
inline long long lagFrames(float position, long long capacity, int grain) {
	const long long room = capacity - grain;
	if (room <= 0) return 0;
	const double p = double(detail::clampLo(position, 0.f, 1.f));
	return std::min(room, detail::roundHalfUp((1.0 - p) * double(room)));
}

// 261.6256 * 2^(rootSemis / 12 + voct): C4 is ROOT 0. NaN and infinite
// inputs read 0; the voltage is held within +-12 V, so the result is finite
// and above 0 for any input.
inline float rootHz(float rootSemis, float voctVolts) {
	const float semis = detail::volts(rootSemis, 120.f);
	const float v = detail::volts(voctVolts, 12.f);
	return 261.6256f * fastExp2(semis * (1.f / 12.f) + v);
}

// The comb's tooth width: 5 * 60^width cents, 5 to 300.
inline float widthCents(float width) {
	return 5.f * fastExp2(detail::clampLo(width, 0.f, 1.f) * 5.906890596f);  // log2(60)
}

inline float tiltDbPerOctave(float tilt) {
	return 6.f * detail::clampLo(detail::volts(tilt, 1.f), -1.f, 1.f);
}

// 2^(semitones / 12), exactly 1 inside +-0.25 (the detent) and for NaN.
inline float shiftRatio(float semis) {
	if (!(semis > 0.25f || semis < -0.25f)) return 1.f;
	return fastExp2(detail::volts(semis, 24.f) * (1.f / 12.f));
}

// Equal power: dry = cos(m pi / 2), wet = sin(m pi / 2), exact at 0 and 1.
// m is held to [0, 1]; NaN reads 0 (all dry).
inline void mixGains(float m, float& dry, float& wet) {
	m = detail::clampLo(m, 0.f, 1.f);
	dry = fastSin2pi(0.25f - 0.25f * m);
	wet = fastSin2pi(0.25f * m);
}

// Samples between the engine's staged steps: max(64, round(64 rate / 48000)).
inline int stepGap(float rate) {
	const double r = double(detail::clampLo(rate, 0.f, 1e9f));
	return int(std::max<long long>(64, detail::roundHalfUp(64.0 * r / 48000.0)));
}

// The 1 ms dip on RUN at a boundary: max(1, round(0.001 rate)) samples.
inline int runDipFrames(float rate) {
	const double r = double(detail::clampLo(rate, 0.f, 1e9f));
	return int(std::max<long long>(1, detail::roundHalfUp(0.001 * r)));
}

}} // namespace rka::doldrums

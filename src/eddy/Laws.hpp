#pragma once
// Eddy's control laws: what the knobs and CV make of speed, gains, the loop
// window and the dub gains. Pure functions, called at control rate by the
// engine, which owns the glide and the V/OCT smoothing. Rack-free; no libm
// (FastMath.hpp). Any input may be NaN or infinite; results are finite and
// in range (NaN volts and knobs read as their rest value).
#include "FastMath.hpp"
#include "eddy/EddyTuning.hpp"

namespace rka {
namespace eddy {

namespace detail {
inline float volts(float v) {
	return clampKeepNan(v != v ? 0.f : v, -100.f, 100.f);
}
inline double unit(double x) {   // 0..1, NaN -> 0
	return x >= 0.0 ? (x <= 1.0 ? x : 1.0) : 0.0;
}
// v0 at a, v1 at b, straight between.
inline float lerpSeg(float x, float a, float b, float v0, float v1) {
	return v0 + (v1 - v0) * ((x - a) / (b - a));
}
} // namespace detail

// The knob -2..+2 as speed: exactly -1, 0 and +1 within the flat stretches,
// straight lines between them, so it is monotonic and continuous. A NaN
// knob reads 1 (the default).
inline float speedKnob(float knob) {
	if (knob != knob)
		return 1.f;
	const float w = SPEED_FLAT_WIDTH, e = SPEED_KNOB_MAX;
	const float x = clampKeepNan(knob, -e, e);
	const float ax = x < 0.f ? -x : x;
	const float sign = x < 0.f ? -1.f : 1.f;
	float a;   // |speed|
	if (ax <= w)
		a = 0.f;
	else if (ax < 1.f - w)
		a = detail::lerpSeg(ax, w, 1.f - w, 0.f, 1.f);
	else if (ax <= 1.f + w)
		a = 1.f;
	else
		a = detail::lerpSeg(ax, 1.f + w, e, 1.f, 2.f);
	return sign * a;
}

// The knob plus Speed CV x attenuverter (-1..1) x 0.4 per volt: the value
// the glide follows.
inline float speedTarget(float knob, float cvVolts, float att) {
	const float a = clampKeepNan(att != att ? 0.f : att, -1.f, 1.f);
	return speedKnob(knob) + detail::volts(cvVolts) * a * SPEED_CV_PER_VOLT;
}

// The glided speed times 2^(V/OCT volts), held to +-4.
inline float speedFinal(float glided, float voctVolts) {
	const float g = clampKeepNan(glided != glided ? 0.f : glided, -64.f, 64.f);
	const float s = g * fastExp2(detail::volts(voctVolts));
	return clampKeepNan(s, -SPEED_HOLD, SPEED_HOLD);
}

// 0 at |speed| 0, rising linearly to 1 at 1/64.
inline float outputGain(float speed) {
	const float a = speed < 0.f ? -speed : speed;
	if (!(a > 0.f))
		return 0.f;
	const float g = a / OUT_FULL_SPEED;
	return g < 1.f ? g : 1.f;
}

// 0 up to |speed| 1/16, rising linearly to 1 at 1/8.
inline float writeGain(float speed) {
	const float a = speed < 0.f ? -speed : speed;
	if (!(a > WRITE_OFF_SPEED))
		return 0.f;
	const float g = (a - WRITE_OFF_SPEED) / (WRITE_FULL_SPEED - WRITE_OFF_SPEED);
	return g < 1.f ? g : 1.f;
}

// The LENGTH law: knob k (0..1, already held there) as a length,
// lmin x (tape / lmin)^k, held to [lmin, tape]; lmin is the shortest length
// (10 ms, or the tape if that is shorter), in the same unit as tape. The
// window (below) and the window's mouse (Select.hpp) both use it.
inline double lengthLaw(double k, double tape, double lmin) {
	if (k <= 0.0)
		return lmin;
	if (k >= 1.0)
		return tape;
	const double L = lmin * fastExp2D(k * (double) fastLog2((float) (tape / lmin)));
	return L < lmin ? lmin : (L > tape ? tape : L);
}

// The loop window, all in frames. start: where it begins; length: L;
// minFade: m = min(F, L/4); overlap: o; turn: how long a turn of the loop is
// (L - (o - t), t = the part of m the tape holds past the window's end).
// An empty tape (or one without a rate) gives all zeros.
struct Window {
	double start, length, minFade, overlap, turn;
};

// tailAfter: frames of audio the tape holds after the window's end (0 if it
// wraps). Start is held at most one frame inside the tape (a window past the
// end wraps to the beginning).
inline Window window(float startKnob, float startCvVolts, float lengthKnob, float smear, double tapeFrames,
                     double tailAfter, float fs) {
	Window w = {0.0, 0.0, 0.0, 0.0, 0.0};
	if (!(tapeFrames >= 1.0) || !(tapeFrames < 1e12))
		return w;
	const double rate = (fs > 0.f && fs < 1e8f) ? (double) fs : 48000.0;
	const double F = rate * FADE_S;
	double lmin = rate * LENGTH_MIN_S;
	if (lmin > tapeFrames)
		lmin = tapeFrames;

	const double p = detail::unit((double) startKnob + (double) detail::volts(startCvVolts) / START_CV_VOLTS);
	double start = p * tapeFrames;
	if (start > tapeFrames - 1.0)
		start = tapeFrames - 1.0;

	const double L = lengthLaw(detail::unit(lengthKnob), tapeFrames, lmin);

	const double m = F < L / 4.0 ? F : L / 4.0;
	const double s = detail::unit(smear);
	const double half = s * L / 2.0;
	const double o = half > m ? half : m;
	double t = tailAfter == tailAfter && tailAfter > 0.0 ? tailAfter : 0.0;
	if (t > m)
		t = m;

	w.start = start;
	w.length = L;
	w.minFade = m;
	w.overlap = o;
	w.turn = L - (o - t);
	return w;
}

// Dub Level d: the new layer's gain min(1, 2d) and the old tape's 1 - d^2.
inline void dubGains(float level, float& add, float& keep) {
	const float d = (float) detail::unit((double) level);
	const float a = 2.f * d;
	add = a < 1.f ? a : 1.f;
	keep = 1.f - d * d;
}

} // namespace eddy
} // namespace rka

#pragma once
// The mouse in Eddy's window (the TAPE screen): what a drag in it makes of
// the START and LENGTH knobs. The knobs stay the truth: the module sets the
// two params from these, as a turn of the knobs would. Rack-free. The UI
// thread's: it calls libm, so never on the audio path.
//
// All positions are shares of the tape (0..1). A drag:
// - a new selection (GRAB_NEW): the press is one end, the mouse the other,
//   either way round;
// - the start line taken (GRAB_START): the start follows the mouse, the end
//   stays (so the length changes);
// - the end line taken (GRAB_END): the length follows the mouse, the start
//   stays (the START knob untouched, to the bit).
// A taken line moves by as much as the mouse has since the press (no jump
// to the mouse within the grab distance). An end dragged past the other
// swaps roles. The selection is held inside the tape and to what LENGTH can
// say: at least 10 ms (a shorter one is 10 ms from the end that stays, or
// from the press, toward the mouse), at most the tape. It never wraps: an
// end of a window that wraps past the tape's end (made by the knobs or CV)
// is dragged the same way, the other end staying where it is drawn, so the
// window becomes the stretch between that end and the mouse.
//
// START CV: the window is START + CV; the drag sets the knob so that it
// plus the CV as it is now puts the start where the mouse is, held to the
// knob's range (so a CV that pushes past the tape's end leaves the start
// short of the mouse).
#include <cmath>

#include "TapeGesture.hpp"
#include "eddy/Laws.hpp"

namespace rka {
namespace eddy {

// How far short of the tape's end a selection that reaches it stops, as a
// share of the tape (dragWindow).
constexpr double END_MARGIN = 1e-6;

struct WindowKnobs {
	float start = 0.f, length = 1.f;
};

namespace detail {
// A tape's seconds the window can use: finite and above 0.
inline bool usableSeconds(double seconds) {
	return seconds > 0.0 && seconds < 1e9;
}
inline float unitOr(float x, float rest) {
	return x == x ? (x < 0.f ? 0.f : x > 1.f ? 1.f : x) : rest;
}
} // namespace detail

// The window's length as a share of the tape for LENGTH knob k, on a tape
// `seconds` long: the law of Laws.hpp (lengthLaw). The whole tape on one
// of 10 ms or less, or one that is not usable.
inline double lengthShare(float k, double seconds) {
	if (!detail::usableSeconds(seconds))
		return 1.0;
	const double lmin = LENGTH_MIN_S < seconds ? LENGTH_MIN_S : seconds;
	return lengthLaw(detail::unit(k), seconds, lmin) / seconds;
}

// The LENGTH knob for a window `share` of a tape `seconds` long, the
// inverse of lengthShare: 0 at 10 ms or less, 1 at the whole tape (and on a
// tape of 10 ms or less, or one that is not usable, where any knob is the
// whole tape). A NaN share is the whole tape.
inline float lengthKnobFor(double share, double seconds) {
	if (!detail::usableSeconds(seconds) || !(seconds > LENGTH_MIN_S) || !(share < 1.0))
		return 1.f;
	const double L = share * seconds;
	if (!(L > LENGTH_MIN_S))
		return 0.f;
	// The denominator as the law has it (fastLog2 of a float), so the round
	// trip is exact but for the knob's float.
	const double k = std::log2(L / LENGTH_MIN_S) / (double) fastLog2((float) (seconds / LENGTH_MIN_S));
	return k > 0.0 ? (k < 1.0 ? (float) k : 1.f) : 0.f;
}

// The START and LENGTH knobs for a drag: `grab` (TapeGesture.hpp), the
// press and the mouse now, `before` the knobs as they were at the press,
// `cvShare` START CV now as a share of the tape (volts / 5), `seconds` the
// tape's length. Any input may be NaN or infinite (a NaN press or mouse
// reads 0); with no usable tape the knobs are `before`. Always finite and
// inside the knobs' ranges.
inline WindowKnobs dragWindow(TapeGrab grab, float press, float now, WindowKnobs before, float cvShare, double seconds) {
	WindowKnobs k;
	k.start = detail::unitOr(before.start, 0.f);
	k.length = detail::unitOr(before.length, 1.f);
	if (!detail::usableSeconds(seconds))
		return k;
	const double cv = cvShare == cvShare ? (cvShare < -20.f ? -20.0 : cvShare > 20.f ? 20.0 : (double) cvShare) : 0.0;
	const double p = detail::unit(press), n = detail::unit(now);
	const double m = LENGTH_MIN_S < seconds ? LENGTH_MIN_S / seconds : 1.0;
	// The window as it was: its start, and its end drawn inside the tape.
	const double s = detail::unit((double) k.start + cv);
	double e = s + lengthShare(k.length, seconds);
	if (e > 1.0)
		e -= 1.0;
	// The end that stays, and the one that moves.
	double fixed, moving;
	if (grab == GRAB_START) {
		fixed = e;
		moving = detail::unit(s + (n - p));
	}
	else if (grab == GRAB_END) {
		fixed = s;
		moving = detail::unit(e + (n - p));
	}
	else {
		fixed = p;
		moving = n;
	}
	double a = fixed < moving ? fixed : moving, b = fixed < moving ? moving : fixed;
	if (b - a < m) {
		if (moving >= fixed) {
			a = fixed;
			b = fixed + m;
			if (b > 1.0) {
				b = 1.0;
				// Short of the end by END_MARGIN (below), unless it is the tape.
				a = m < 1.0 ? (1.0 - END_MARGIN - m > 0.0 ? 1.0 - END_MARGIN - m : 0.0) : 0.0;
			}
		}
		else {
			b = fixed;
			a = fixed - m;
			if (a < 0.0) {
				a = 0.0;
				b = m;
			}
		}
	}
	// The end line moved, the start where it was: START is not touched.
	if (!(grab == GRAB_END && a == fixed)) {
		const double knob = a - cv;
		k.start = knob > 0.0 ? (knob < 1.0 ? (float) knob : 1.f) : 0.f;
	}
	k.length = lengthKnobFor(b - a, seconds);
	// A selection to the tape's end must not pass it by a rounding (the
	// window would wrap, and its end line be drawn at the tape's start):
	// short of it by END_MARGIN of the tape (a few frames of 5 minutes).
	// The whole tape (start 0, LENGTH 1) is exact as it is.
	const double s1 = detail::unit((double) k.start + cv);
	if (b > 1.0 - END_MARGIN && !(s1 == 0.0 && k.length == 1.f))
		for (int i = 0; i < 64 && k.length > 0.f && s1 + lengthShare(k.length, seconds) > 1.0 - END_MARGIN; i++)
			k.length = std::nextafter(k.length, 0.f);
	return k;
}

} // namespace eddy
} // namespace rka

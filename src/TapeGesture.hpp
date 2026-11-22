#pragma once
// The mouse in a window on a tape (TapeWindow, widgets.hpp), Rack-free:
// which end of the window a press takes hold of, and when a press has
// become a drag. Positions are in the widget's own units, px at 100 % zoom.
// What a drag makes of a module's knobs is the module's (Eddy:
// eddy/Select.hpp).
#include <cmath>

namespace rka {

// A drag in the window: a new selection, or one end of the window taken.
enum TapeGrab { GRAB_NEW, GRAB_START, GRAB_END };

// An end line within this of the press is taken hold of.
constexpr float TAPE_GRAB_PX = 4.f;
// A press that has moved less than this, sideways, is a click (it changes
// nothing); from there on it is a drag.
constexpr float TAPE_CLICK_PX = 2.f;

// What a press at x takes: the nearer of the window's end lines (at startX
// and endX) within TAPE_GRAB_PX, else a new selection. Lines equally near
// (a window so short its lines meet): the end if the press is right of the
// start line, else the start. Any NaN: a new selection.
inline TapeGrab tapeGrabAt(float x, float startX, float endX) {
	const float ds = std::fabs(x - startX), de = std::fabs(x - endX);
	const bool s = ds <= TAPE_GRAB_PX, e = de <= TAPE_GRAB_PX;
	if (!s && !e)
		return GRAB_NEW;
	if (s != e)
		return s ? GRAB_START : GRAB_END;
	if (ds != de)
		return ds < de ? GRAB_START : GRAB_END;
	return x > startX ? GRAB_END : GRAB_START;
}

// The mouse at x has moved far enough from the press at pressX to be a drag.
inline bool tapeDragged(float pressX, float x) {
	return std::fabs(x - pressX) >= TAPE_CLICK_PX;
}

} // namespace rka

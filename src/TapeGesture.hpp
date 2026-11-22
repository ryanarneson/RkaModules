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

// One gesture in the window, as the widget follows it: what to tell the
// module at a press, a move and a release. `usable`: the window can be set
// now (a tape, not empty, not recording); `tape`: which tape it is (only
// compared). A drag changes the knobs only while the window stays usable
// and on the tape it was pressed on: from the moment it is not (recording
// begins, the tape is cleared or replaced) the drag stops for good, even if
// the window becomes usable again. What it set until then stays, and its
// release still ends it (one undo step).
struct TapeDrag {
	enum Step {
		NOTHING,   // nothing to tell
		BEGIN,     // the press has become a drag: begin, then select
		SELECT     // select
	};
	bool pressed = false;    // a press being followed
	bool dragging = false;   // it has become a drag that has not ended
	bool stopped = false;    // the window stopped being usable during it
	float pressX = 0.f;
	TapeGrab grab = GRAB_NEW;
	const void* tape = nullptr;

	// A press at x; startX, endX: the window's end lines. Returns true if a
	// drag before it never ended (its release was lost): end that one
	// first.
	bool press(float x, bool usableNow, const void* tapeNow, float startX, float endX) {
		const bool unended = dragging;
		pressed = usableNow;
		dragging = stopped = false;
		pressX = x;
		tape = tapeNow;
		grab = usableNow ? tapeGrabAt(x, startX, endX) : GRAB_NEW;
		return unended;
	}

	// The mouse at x.
	Step move(float x, bool usableNow, const void* tapeNow) {
		if (!pressed || stopped)
			return NOTHING;
		if (!usableNow || tapeNow != tape) {
			stopped = true;
			return NOTHING;
		}
		if (dragging)
			return SELECT;
		if (!tapeDragged(pressX, x))
			return NOTHING;
		dragging = true;
		return BEGIN;
	}

	// The release. Returns true if a drag is to be ended.
	bool release() {
		const bool end = dragging;
		pressed = dragging = stopped = false;
		return end;
	}
};

} // namespace rka

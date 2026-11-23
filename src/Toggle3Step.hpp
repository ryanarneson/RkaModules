#pragma once
// How a click or a drag moves rka::Toggle3 (widgets.hpp), the three-position
// switch: by direction, never wrapping. Value 0 is the handle up (Beacon's
// GATE), 2 down (HOLD). Rack-free and C++11, so it is tested offline
// (src/beacon/test_switch.cpp).
//
// A click (press and release with less than one step of drag) on the upper
// half moves one position up, on the lower half one down, held at the ends:
// from 0 an upper click stays at 0, so the switch never passes through HOLD
// on its way from TRIG to GATE. A vertical drag moves one position per
// TOGGLE3_DRAG_PX of travel, down is down, also held at the ends; after a
// whole step of drag (moved or held at an end) the release adds no click.

namespace rka {

// Mouse travel (px, as Rack's DragMoveEvent gives it) per position.
constexpr float TOGGLE3_DRAG_PX = 12.f;

inline int toggle3Clamp(int v) {
	return v < 0 ? 0 : v > 2 ? 2 : v;
}

// Whether a press at y (widget coordinates, 0 at the top) is on the upper half.
inline bool toggle3UpperHalf(float y, float height) {
	return y < 0.5f * height;
}

// The position after a click: up (toward 0) on the upper half, down (toward
// 2) on the lower, clamped.
inline int toggle3Next(int value, bool upperHalf) {
	return toggle3Clamp(toggle3Clamp(value) + (upperHalf ? -1 : 1));
}

// One press-drag-release on the switch.
struct Toggle3Gesture {
	int start = 0;        // the value at the press
	int value = 0;        // the value now
	bool upper = false;   // pressed on the upper half
	bool dragged = false; // a whole step of drag seen (no click on release)
	float accum = 0.f;    // drag travel not yet a whole position

	void press(int v, bool upperHalf) {
		start = value = toggle3Clamp(v);
		upper = upperHalf;
		dragged = false;
		accum = 0.f;
	}

	// dy: vertical travel, positive downward. Returns the value now. Travel
	// past an end is dropped, so a drag back moves at once.
	int move(float dy) {
		if (!(dy == dy))
			return value;
		accum += dy;
		while (accum >= TOGGLE3_DRAG_PX) {
			accum -= TOGGLE3_DRAG_PX;
			dragged = true;
			if (value >= 2) {
				accum = 0.f;
				break;
			}
			value++;
		}
		while (accum <= -TOGGLE3_DRAG_PX) {
			accum += TOGGLE3_DRAG_PX;
			dragged = true;
			if (value <= 0) {
				accum = 0.f;
				break;
			}
			value--;
		}
		return value;
	}

	// The value at the release: the click's if no drag moved it.
	int release() {
		if (!dragged)
			value = toggle3Next(start, upper);
		return value;
	}
};

} // namespace rka

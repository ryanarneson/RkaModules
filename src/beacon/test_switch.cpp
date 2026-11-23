// Beacon's MODE switch, rka::Toggle3's stepping (src/Toggle3Step.hpp): a
// click moves it by the half clicked, a drag by its direction, never
// wrapping (the final review's finding 1: a wrapping switch went
// TRIG -> HOLD -> GATE). Expected values come from the ruling's table,
// written out here, not from the helper.
#include "Toggle3Step.hpp"
#include "testing.hpp"

#include <cmath>

namespace {

using rka::Toggle3Gesture;
using rka::TOGGLE3_DRAG_PX;

enum { GATE = 0, TRIG = 1, HOLD = 2 };
const char* name(int v) {
	return v == GATE ? "GATE" : v == TRIG ? "TRIG" : v == HOLD ? "HOLD" : "?";
}

void click_table() {
	struct { int from; bool upper; int want; } t[] = {
		{GATE, true, GATE}, {GATE, false, TRIG},
		{TRIG, true, GATE}, {TRIG, false, HOLD},
		{HOLD, true, TRIG}, {HOLD, false, HOLD},
	};
	for (auto& c : t) {
		const int got = rka::toggle3Next(c.from, c.upper);
		CHECK(got == c.want, "%s, %s half: %s (want %s)", name(c.from), c.upper ? "upper" : "lower", name(got), name(c.want));
		Toggle3Gesture g;
		g.press(c.from, c.upper);
		g.move(3.f);
		g.move(-5.f);   // jitter under a step: still a click
		const int rel = g.release();
		CHECK(rel == c.want, "a click gesture from %s (%s half) ends %s (want %s)", name(c.from), c.upper ? "upper" : "lower", name(rel), name(c.want));
	}
	// Out of range reads as the nearest end.
	CHECK(rka::toggle3Next(-4, false) == TRIG && rka::toggle3Next(9, true) == TRIG, "out-of-range values clamp first");
}

// Clicking one half over and over walks to that end and stays; TRIG to GATE
// is one click, never through HOLD.
void no_wrap() {
	int v = GATE;
	const int down[4] = {TRIG, HOLD, HOLD, HOLD};
	for (int i = 0; i < 4; i++) {
		v = rka::toggle3Next(v, false);
		CHECK(v == down[i], "lower click %d: %s (want %s)", i + 1, name(v), name(down[i]));
	}
	const int up[4] = {TRIG, GATE, GATE, GATE};
	for (int i = 0; i < 4; i++) {
		v = rka::toggle3Next(v, true);
		CHECK(v == up[i], "upper click %d: %s (want %s)", i + 1, name(v), name(up[i]));
	}
}

void halves() {
	const float h = 18.9f;
	CHECK(rka::toggle3UpperHalf(0.f, h) && rka::toggle3UpperHalf(0.49f * h, h), "the top is the upper half");
	CHECK(!rka::toggle3UpperHalf(0.5f * h, h) && !rka::toggle3UpperHalf(h, h), "the middle and the bottom are the lower half");
}

void drag() {
	const float px = TOGGLE3_DRAG_PX;
	{
		Toggle3Gesture g;
		g.press(GATE, true);
		CHECK(g.move(px * 0.9f) == GATE, "under a step: no move");
		CHECK(g.move(px * 0.2f) == TRIG, "a step down: TRIG");
		CHECK(g.move(px) == HOLD, "another: HOLD");
		CHECK(g.move(px * 10.f) == HOLD, "held at HOLD");
		CHECK(g.move(-px) == TRIG, "travel past the end is dropped: one step back up is TRIG");
		CHECK(g.release() == TRIG, "a drag's release adds no click (%s)", name(g.value));
	}
	{
		// A trackpad: many small deltas add up.
		Toggle3Gesture g;
		g.press(HOLD, false);
		for (int i = 0; i < 50; i++)
			g.move(-0.5f);   // 25 px up: two steps
		CHECK(g.value == GATE, "small deltas add up (%s)", name(g.value));
		CHECK(g.release() == GATE, "release keeps the drag's value");
	}
	{
		// A drag held at an end is still a drag: no click on release.
		Toggle3Gesture g;
		g.press(HOLD, true);
		g.move(px * 1.5f);
		CHECK(g.release() == HOLD, "a drag down at HOLD, pressed on the upper half, stays HOLD (%s)", name(g.value));
	}
	{
		Toggle3Gesture g;
		g.press(TRIG, false);
		g.move(std::nanf(""));
		CHECK(g.value == TRIG && g.release() == HOLD, "NaN travel is ignored (a click)");
		g.press(TRIG, false);
		CHECK(g.move(INFINITY) == HOLD && g.move(-INFINITY) == GATE, "infinite travel goes to an end and stops");
	}
}

} // namespace

int main() {
	click_table();
	no_wrap();
	halves();
	drag();
	return rkatest::finish("beacon/test_switch");
}

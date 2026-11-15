// rka::DetentCounter: drag and scroll-wheel arithmetic of the encoder widget.
#include "Detents.hpp"
#include "testing.hpp"

using rka::DetentCounter;

static void testDrag() {
	const float px = rka::DRAG_PX_PER_DETENT;
	DetentCounter d;
	CHECK(d.add(5.f, px) == 0, "5 px is not a detent");
	CHECK(d.add(5.f, px) == 1, "10 px completes one");
	CHECK(d.accum == 2.f, "2 px carry, got %g", d.accum);
	CHECK(d.add(-12.f, px) == -1, "back 12 px: -10 px is one detent anticlockwise");
	CHECK(d.accum == -2.f, "the remainder keeps its sign, got %g", d.accum);
	CHECK(d.add(-6.f, px) == -1, "-8 px: one more");
	CHECK(d.accum == 0.f, "nothing left, got %g", d.accum);
	CHECK(d.add(40.f, px) == 5, "a fast move gives several detents at once");

	DetentCounter fine;
	const float fpx = rka::FINE_DRAG_PX_PER_DETENT;
	CHECK(fine.add(31.f, fpx) == 0 && fine.add(1.f, fpx) == 1, "fine drag: 32 px per detent");
}

static void testScroll(bool mac) {
	const float notch = rka::scrollUnitsPerNotch(mac);
	const char* os = mac ? "macOS" : "other";
	DetentCounter s;
	CHECK(s.add(notch, notch) == 1, "%s: one notch, one detent", os);
	CHECK(s.add(-notch, notch) == -1, "%s: one notch back, one detent back", os);
	CHECK(s.add(3 * notch, notch) == 3, "%s: three notches in one event", os);
	CHECK(s.accum == 0.f, "%s: nothing left after whole notches", os);
	// A trackpad's quarter notches add up.
	int n = 0;
	for (int i = 0; i < 3; i++)
		n += s.add(notch / 4, notch);
	CHECK(n == 0, "%s: three quarters is no detent yet", os);
	CHECK(s.add(notch / 4, notch) == 1, "%s: the fourth quarter completes one", os);
	n = 0;
	for (int i = 0; i < 4; i++)
		n += s.add(-notch / 4, notch);
	CHECK(n == -1, "%s: four quarters back, one detent back", os);
	CHECK(s.add(notch / 2, notch) == 0, "%s: half a notch", os);
	s.clear();
	CHECK(s.add(notch / 2, notch) == 0, "%s: a cleared remainder does not complete a detent", os);
}

// rka::TurnBacklog: refused detents wait and add up.
static void testBacklog() {
	rka::TurnBacklog b;
	CHECK(b.add(3) == 3, "a turn is offered whole");
	b.clear();
	CHECK(b.unsent == 0 && b.add(0) == 0, "taken: nothing waits");
	// Refused twice, then one back: the sum is offered.
	CHECK(b.add(2) == 2 && b.add(4) == 6 && b.add(-1) == 5, "refused turns add up, got %d", b.unsent);
	CHECK(b.add(-5) == 0, "turning back cancels what waits");
	CHECK(b.add(1000) == rka::TURN_BACKLOG_MAX, "held to the maximum, got %d", b.unsent);
	CHECK(b.add(-5000) == -rka::TURN_BACKLOG_MAX, "and to the minimum, got %d", b.unsent);
	CHECK(b.add(-2147483647 - 1) == -rka::TURN_BACKLOG_MAX, "no overflow");
	// The engine stopped while the encoder kept turning: what it gets on
	// resuming is a short turn.
	CHECK(rka::TURN_BACKLOG_MAX == 16, "the backlog is capped at 16 detents, got %d", rka::TURN_BACKLOG_MAX);
	b.clear();
	for (int i = 0; i < 500; i++)
		b.add(1);
	CHECK(b.unsent == 16, "500 refused detents wait as 16, got %d", b.unsent);
	CHECK(b.add(-3) == 13, "and turning back counts from the cap, got %d", b.unsent);
}

int main() {
	testDrag();
	testBacklog();
	CHECK(rka::scrollUnitsPerNotch(true) == 10.f, "macOS: Rack scales a notch to 10");
	CHECK(rka::scrollUnitsPerNotch(false) == 50.f, "elsewhere: Rack scales a notch to 50");
	testScroll(true);
	testScroll(false);
	return rkatest::finish("test_detents");
}

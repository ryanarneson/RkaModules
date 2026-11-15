#pragma once
// Turning mouse travel and scroll-wheel deltas into whole encoder detents,
// for rka::Encoder (widgets.hpp). Rack-free and C++11, so it is tested
// offline (src/shoal/test_detents.cpp).

namespace rka {

// Mouse travel per detent: plain drag, and fine drag (Ctrl/Cmd).
constexpr float DRAG_PX_PER_DETENT = 8.f;
constexpr float FINE_DRAG_PX_PER_DETENT = 32.f;

// Rack's scrollDelta for one wheel notch. Rack 2's Window.cpp
// (scrollCallback) scales GLFW's offset by 10 on macOS and 50 elsewhere.
constexpr float scrollUnitsPerNotch(bool mac) {
	return mac ? 10.f : 50.f;
}

// Adds signed movement and hands back the whole detents it completes
// (truncated toward zero); the signed remainder carries to the next add, so
// small deltas (a trackpad's) add up.
struct DetentCounter {
	float accum = 0.f;

	int add(float delta, float unitsPerDetent) {
		accum += delta;
		const int n = int(accum / unitsPerDetent);
		accum -= n * unitsPerDetent;
		return n;
	}

	// Drop a part-detent remainder.
	void clear() {
		accum = 0.f;
	}
};

// Detents whose receiver refused them (a full event queue), kept so that the
// turn is offered again instead of lost. Turns add up; the sum is held to
// +-TURN_BACKLOG_MAX, so an engine stopped while the encoder was turned
// resumes with a short turn, not a burst of everything turned meanwhile.
constexpr int TURN_BACKLOG_MAX = 16;

struct TurnBacklog {
	int unsent = 0;

	// Adds a turn and returns everything now waiting, to offer in one go.
	int add(int detents) {
		long total = (long) unsent + detents;
		if (total > TURN_BACKLOG_MAX)
			total = TURN_BACKLOG_MAX;
		if (total < -TURN_BACKLOG_MAX)
			total = -TURN_BACKLOG_MAX;
		unsent = (int) total;
		return unsent;
	}

	// The receiver took what add() returned.
	void clear() {
		unsent = 0;
	}
};

}

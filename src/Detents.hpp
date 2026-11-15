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

}

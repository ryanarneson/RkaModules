#pragma once
#include <algorithm>
#include <cmath>

// Rack-independent clock follower: measures the time between clock edges
// and decides when a synced LFO should restart its cycle.
namespace rka {

// Ratio of LFO cycles per clock, selected by index -7..7.
static const float CLOCK_RATIOS[15] = {
	1.f / 16, 1.f / 12, 1.f / 8, 1.f / 6, 1.f / 4, 1.f / 3, 1.f / 2,
	1.f,
	2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f,
};

// RATE (plus FM volts, one step per volt) rounded to a ratio index.
inline int ratioIndex(float rate, float fm) {
	const int i = int(std::lround(rate + fm));
	return std::max(-7, std::min(i, 7));
}

inline float clockRatio(int index) {
	return CLOCK_RATIOS[index + 7];
}

struct ClockSync {
	// Call every sample with whether a clock edge arrived. Returns true when
	// the LFO should restart its cycle now: on every edge for x1 and faster,
	// on every Nth edge for /N. Never before two edges have set a period.
	bool process(bool edge, double dt, int index) {
		elapsed += dt;
		if (!edge)
			return false;
		if (edges > 0)
			period = elapsed;
		elapsed = 0.0;
		if (edges < 2)
			edges++;
		const float ratio = clockRatio(index);
		bool resync = true;
		if (ratio < 1.f) {
			const int n = int(std::lround(1.f / ratio));
			resync = ++count >= n;
			if (resync)
				count = 0;
		}
		return resync && locked();
	}

	bool locked() const {
		return edges >= 2;
	}

	// LFO frequency in Hz; only meaningful once locked().
	double frequency(int index) const {
		return clockRatio(index) / period;
	}

	// Restart the /N edge count (on RESET).
	void resetCount() {
		count = 0;
	}

	// Forget the clock entirely (cable removed).
	void clear() {
		elapsed = 0.0;
		period = 0.0;
		edges = 0;
		count = 0;
	}

private:
	double elapsed = 0.0;
	double period = 0.0;
	int edges = 0;
	int count = 0;
};

} // namespace rka

#pragma once
#include <cmath>

// Rack-independent phase accumulator. The phase is a double: at slow rates
// the per-sample step (f / fs, ~1e-13 at 2^-24 Hz) is far below a float's
// resolution near 1, which made a float phase run fast or freeze.
namespace rka {

// Where a finished one-shot rests: the very end of the cycle, so SAW reads
// ~+1 and SIN/TRI ~0 rather than jumping back to the cycle's start.
static const double PARK_PHASE = 1.0 - 1e-9;

struct Phasor {
	// Normalized phase in [0, 1).
	double phase = 0.0;
	// A one-shot cycle has finished; stays put until reset().
	bool parked = false;

	void reset() {
		phase = 0.0;
		parked = false;
	}

	void park() {
		phase = PARK_PHASE;
		parked = true;
	}

	// Advance by one sample; returns true when the cycle wraps. hold freezes
	// the phase. With oneShot the cycle parks at its end instead of wrapping;
	// turning oneShot off releases a parked phasor.
	bool step(double freq, double dt, bool hold, bool oneShot) {
		if (!oneShot)
			parked = false;
		if (hold || parked)
			return false;
		phase += freq * dt;
		if (phase < 1.0)
			return false;
		if (oneShot) {
			park();
			return true;
		}
		// floor() rather than a single subtract keeps phase in range even
		// when freq * dt exceeds 1.
		phase -= std::floor(phase);
		return true;
	}
};

} // namespace rka

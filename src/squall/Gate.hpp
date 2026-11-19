#pragma once
// Squall's gate (spec: Outputs, GATE): high for a length the caller has
// already clamped. An event while it is high drops it for RETRIGGER_S, then
// raises it for the new length, so every event is its own rising edge.
#include "squall/SquallTuning.hpp"

namespace rka {
namespace squall {

class Gate {
public:
	void setSampleRate(float fs) {
		if (fs > 0.f && fs <= 768000.f)
			fs_ = fs;
	}

	// lowSeconds: the retrigger's drop (RETRIGGER_S, shorter for events
	// closer together than that, so a fast clock still gets its edges).
	void fire(float seconds, float lowSeconds = RETRIGGER_S) {
		const long long n = samples(seconds);
		if (lowLeft_ > 0) {
			// Already dropped for a retrigger: rise as planned, for this
			// length (a fast clock must not hold the gate low for ever).
			pending_ = n;
			return;
		}
		if (highLeft_ > 0) {
			const long long low = samples(lowSeconds);
			lowLeft_ = low;
			pending_ = n;
			highLeft_ = 0;
		}
		else {
			highLeft_ = n;
		}
	}

	bool process() {
		if (lowLeft_ > 0) {
			if (--lowLeft_ == 0)
				highLeft_ = pending_;
			return false;
		}
		if (highLeft_ > 0) {
			highLeft_--;
			return true;
		}
		return false;
	}

	// High, or between a retrigger's low and its new high.
	bool high() const {
		return highLeft_ > 0 || lowLeft_ > 0;
	}

	void clear() {
		highLeft_ = lowLeft_ = pending_ = 0;
	}

private:
	long long samples(float s) const {
		const double n = (double) s * fs_ + 0.5;
		return n >= 1.0 ? (long long) n : 1;
	}

	float fs_ = 48000.f;
	long long highLeft_ = 0, lowLeft_ = 0, pending_ = 0;
};

} // namespace squall
} // namespace rka

#pragma once
// Rigging's strike (docs/specs/2026-11-19-rigging-design.md): on a
// trigger, each string gets a burst of noise about one of its periods long
// (as a pluck fills a string once), string 1 first and string 6 Spread
// later, the others evenly between. A new trigger starts a new rake and
// drops the strikes the old one still owed. Rack-free, C++11, no libm.
#include <cstdint>

#include "hadal/Random.hpp"
#include "rigging/Chords.hpp"
#include "rigging/RiggingTuning.hpp"

namespace rka {
namespace rigging {

class Strike {
public:
	void seed(uint32_t s) { rng_.seed(s); }

	void setSampleRate(float fs) {
		fs_ = fs > 0.f ? fs : 48000.f;
	}
	// From string 1's strike to string 6's, in seconds.
	void setSpread(float seconds) {
		spread_ = seconds > 0.f ? seconds : 0.f;
	}
	// A string's period in samples: how long its burst lasts (8 at least).
	void setPeriod(int string, float samples) {
		if (string < 0 || string >= kStrings)
			return;
		long n = (long) (samples + 0.5f);
		burst_[string] = n < 8 ? 8 : n > 2000000 ? 2000000 : n;
	}

	// Nothing due, nothing sounding.
	void clear() {
		for (int k = 0; k < kStrings; k++) {
			wait_[k] = -1;
			left_[k] = 0;
		}
		pending_ = 0;
	}

	void trigger() {
		for (int k = 0; k < kStrings; k++)
			wait_[k] = (long) ((double) spread_ * fs_ * k / (kStrings - 1) + 0.5);
		pending_ = kStrings;
	}

	// One sample of burst for each string; 0 for a string not being struck.
	void process(float out[kStrings]) {
		for (int k = 0; k < kStrings; k++) {
			if (wait_[k] >= 0 && wait_[k]-- == 0) {
				left_[k] = burst_[k];
				lp_[k] = 0.f;
				pending_--;
			}
			if (left_[k] > 0) {
				left_[k]--;
				// Smoothed noise, never exactly 0 while the burst lasts.
				lp_[k] += STRIKE_SMOOTH * (rng_.uniform() - lp_[k]);
				const float v = STRIKE_LEVEL * lp_[k];
				out[k] = v != 0.f ? v : 1e-9f;
			}
			else {
				out[k] = 0.f;
			}
		}
	}

	// A strike is due or sounding.
	bool active() const {
		if (pending_ > 0)
			return true;
		for (int k = 0; k < kStrings; k++)
			if (left_[k] > 0)
				return true;
		return false;
	}

private:
	rka::hadal::Random rng_;
	float fs_ = 48000.f, spread_ = 0.f;
	long burst_[kStrings] = {8, 8, 8, 8, 8, 8};
	long wait_[kStrings] = {-1, -1, -1, -1, -1, -1};
	long left_[kStrings] = {0, 0, 0, 0, 0, 0};
	float lp_[kStrings] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
	int pending_ = 0;
};

} // namespace rigging
} // namespace rka

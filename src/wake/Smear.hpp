#pragma once
// One side's Diffuse: four all-pass filters in series, which spread a
// repeat's energy over a few milliseconds without changing its level or its
// spectrum. In the loop, the spread adds up with every pass.
//
// At amount 0 it is out of the path exactly (though still fed, so it can
// come in without a step). Above 0 it is in, with an
// all-pass gain that rises with the amount; it then delays the signal by
// latencySamples() on average, which the engine takes off the echo time.
// Going to and from 0 fades over a few milliseconds.
#include "DelayLine.hpp"
#include "wake/WakeTuning.hpp"

#include <cstdint>

namespace rka {
namespace wake {

struct Smear {
	static const int STAGES = 4;
	static constexpr float MAX_GAIN = 0.7f;

	// Allocates; not from process(). The seed sets the side's own delays.
	void setSampleRate(float fs, uint32_t seed) {
		if (!(fs > 0.f))
			return;
		// Mutually prime-ish lengths, ms; the second side's are 7 % longer.
		static const float ms[STAGES] = {0.9f, 1.7f, 2.6f, 3.7f};
		const float scale = seed % 2u ? 1.07f : 1.f;
		latency_ = 0;
		for (int s = 0; s < STAGES; s++) {
			int d = (int) (ms[s] * 0.001f * fs * scale + 0.5f);
			d = d >= 1 ? d : 1;
			delay_[s] = d;
			latency_ += d;
			lines_[s].allocate(d + 1, (uint32_t) (seed * 4u + (uint32_t) s));
		}
		fadeStep_ = 1.f / (SWITCH_S * fs > 1.f ? SWITCH_S * fs : 1.f);
		gainStep_ = fadeStep_;   // the knob's own moves, smoothed as fast
	}

	void clear() {
		for (int s = 0; s < STAGES; s++)
			lines_[s].clear();
	}

	void setAmount(float amount01) {
		const float a = amount01 >= 0.f ? (amount01 <= 1.f ? amount01 : 1.f) : 0.f;
		in_ = a > 0.f;
		// Fading out, the gain stays where it was: only the mix moves.
		if (in_)
			gainTarget_ = MAX_GAIN * a;
	}

	// True while the smear is in the path (the engine shortens the echo
	// time by latencySamples() then).
	bool engaged() const {
		return in_;
	}
	int latencySamples() const {
		return latency_;
	}

	float process(float x) {
		// The filters run even when out of the path, so that they hold the
		// present sound when they come in (empty, they would step).
		gain_ += (gainTarget_ - gain_) * gainStep_;
		float y = x;
		for (int s = 0; s < STAGES; s++) {
			const float old = lines_[s].readInt(delay_[s] - 1);
			const float out = old - gain_ * y;
			float w = y + gain_ * out;
			if (w < 1e-20f && w > -1e-20f)
				w = 0.f;
			lines_[s].write(w);
			y = out;
		}
		if (in_)
			mix_ = mix_ + fadeStep_ < 1.f ? mix_ + fadeStep_ : 1.f;
		else
			mix_ = mix_ - fadeStep_ > 0.f ? mix_ - fadeStep_ : 0.f;
		return mix_ > 0.f ? x + mix_ * (y - x) : x;
	}

private:
	DelayLine lines_[STAGES];
	int delay_[STAGES] = {1, 1, 1, 1};
	int latency_ = 4;
	float gain_ = 0.f, gainTarget_ = 0.f, gainStep_ = 1.f, mix_ = 0.f, fadeStep_ = 1.f;
	bool in_ = false;
};

} // namespace wake
} // namespace rka

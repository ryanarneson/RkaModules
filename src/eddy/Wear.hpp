#pragma once
// Eddy's wear and the write path's guard. Wear is what each pass of the head
// costs the tape: the engine calls Wear::process for every frame the write
// head passes and puts the result back on the tape, so the loss builds pass
// over pass. WriteGuard is the DC blocker and soft bound that everything
// written to the tape goes through. Rack-free; no libm (FastMath.hpp); no
// allocation. Any input may be NaN or infinite; results are finite.
#include <cstdint>

#include "FastMath.hpp"
#include "eddy/EddyTuning.hpp"
#include "hadal/Random.hpp"

namespace rka {
namespace eddy {

namespace detail {

// What a tape or a guard may be handed: NaN reads as silence, the rest is
// held to +-100 V (the bound's reach is far inside that).
inline float tameVolts(float x) {
	return x != x ? 0.f : clampKeepNan(x, -100.f, 100.f);
}

// The write path's soft bound: the exact identity up to 8 V (bit for bit),
// then tanh-shaped into a ceiling of 10 V. The input must not be NaN.
inline float softBound(float x) {
	const float a = x >= 0.f ? x : -x;
	if (a <= 8.f)
		return x;
	const float y = 8.f + 2.f * fastTanh((a - 8.f) * 0.5f);
	return x >= 0.f ? y : -y;
}

inline float onePoleCoeff(float hz, float fs) {
	return 1.f - fastExp2(-6.28318530717959f * hz / fs * 1.44269504088896f);
}

inline float flushTiny(float x) {
	return (x < 1e-20f && x > -1e-20f) ? 0.f : x;
}

} // namespace detail

// The DC blocker (5 Hz) and the bound every write to the tape passes.
struct WriteGuard {
	void setSampleRate(float fs) {
		fs = fs == fs ? clampKeepNan(fs, 1000.f, 4e6f) : 48000.f;
		a_ = detail::onePoleCoeff(WRITE_DC_HZ, fs);
	}
	void reset() { dc_[0] = dc_[1] = 0.f; }

	float process(int c, float x) {
		float& dc = dc_[c & 1];
		x = detail::tameVolts(x);
		dc += a_ * (x - dc);
		dc = detail::flushTiny(dc);
		return detail::softBound(x - dc);
	}

private:
	float a_ = 6.5e-4f;
	float dc_[2] = {0.f, 0.f};
};

struct Wear {
	explicit Wear(uint32_t seed) {
		hiss_[0].seed(seed);
		hiss_[1].seed(seed ^ 0x5bd1e995u);
		drop_.seed(seed ^ 0xc2b2ae35u);
		budget_ = drawBudget();
		setSampleRate(48000.f);
		set(0.f);
	}

	void setSampleRate(float fs) {
		fs_ = fs == fs ? clampKeepNan(fs, 1000.f, 4e6f) : 48000.f;
		lpA_ = detail::onePoleCoeff(WEAR_HIGHS_HZ, fs_);
		edge_ = WEAR_DROPOUT_EDGE_S * fs_;
	}

	// The knob, 0..1 (NaN reads 0), per control tick.
	void set(float amount) {
		const float w = amount == amount ? clampKeepNan(amount, 0.f, 1.f) : 0.f;
		const float w2 = w * w;
		active_ = w > 0.f;
		gain_ = fastDbToGain(WEAR_LEVEL_DB * w2);
		highs_ = WEAR_HIGHS_AMOUNT * w2;
		drive_ = WEAR_DRIVE_BASE + w;
		// Uniform noise of this RMS: the amplitude is sqrt(3) times it.
		hissAmp_ = 1.7320508f * WEAR_HISS_REF_VOLTS * fastDbToGain(WEAR_HISS_DB) * w;
		dropRate_ = WEAR_DROPOUT_RATE * w2;
	}

	bool active() const { return active_; }

	// One frame of side c as the head passes it: what goes back on the tape.
	// `frames` is how much tape has passed since the last call (its size, in
	// either direction; held to 64): the dropout clock counts it. Both sides
	// share the dropout, and the clock moves on the call for side 0, so call
	// side 0 first for each frame. The low-pass state follows the head.
	float process(int c, float old, float frames) {
		const int s = c & 1;
		float x = detail::tameVolts(old);
		if (!active_)
			return x;
		if (s == 0)
			advance(frames);
		// Highs: this pass moves the tape part of the way to its low-pass.
		lp_[s] = detail::flushTiny(lp_[s] + lpA_ * (x - lp_[s]));
		x += highs_ * (lp_[s] - x);
		// Saturation: the bound at drive x, scaled back, so it never adds.
		x = detail::softBound(x * drive_) / drive_;
		x *= gain_ * dipGain();
		return x + hissAmp_ * hiss_[s].uniform();
	}

private:
	// Expected-dropout units to wait: Exp(1), so the rate may move freely.
	float drawBudget() {
		const float u = 1.f - drop_.unit();   // (0, 1]
		return -0.693147181f * fastLog2(u < 1e-7f ? 1e-7f : u);
	}

	void advance(float frames) {
		const float f = frames != frames ? 0.f : clampKeepNan(frames < 0.f ? -frames : frames, 0.f, 64.f);
		if (inDip_) {
			dipPos_ += f;
			if (dipPos_ >= dipLen_) {
				inDip_ = false;
				budget_ = drawBudget();
			}
		} else {
			budget_ -= dropRate_ * f / fs_;
			if (budget_ <= 0.f) {
				inDip_ = true;
				dipPos_ = 0.f;
				dipLen_ = fs_ * (WEAR_DROPOUT_MIN_S + (WEAR_DROPOUT_MAX_S - WEAR_DROPOUT_MIN_S) * drop_.unit());
				dipDepth_ = WEAR_DROPOUT_GAIN_MIN + (WEAR_DROPOUT_GAIN_MAX - WEAR_DROPOUT_GAIN_MIN) * drop_.unit();
			}
		}
	}

	// 1, down to the dip's depth along 2 ms edges, and back.
	float dipGain() const {
		if (!inDip_)
			return 1.f;
		float r = dipPos_ / edge_;
		const float rEnd = (dipLen_ - dipPos_) / edge_;
		if (rEnd < r)
			r = rEnd;
		r = r > 1.f ? 1.f : r < 0.f ? 0.f : r;
		return 1.f - (1.f - dipDepth_) * r;
	}

	hadal::Random hiss_[2], drop_;
	float fs_ = 48000.f, lpA_ = 0.4f, edge_ = 96.f;
	float gain_ = 1.f, highs_ = 0.f, drive_ = 1.f, hissAmp_ = 0.f, dropRate_ = 0.f;
	float lp_[2] = {0.f, 0.f};
	float budget_ = 1.f, dipPos_ = 0.f, dipLen_ = 0.f, dipDepth_ = 1.f;
	bool active_ = false, inDip_ = false;
};

} // namespace eddy
} // namespace rka

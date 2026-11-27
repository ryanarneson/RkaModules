#pragma once
// Eddy's wear and the write path's guards. Wear is what each pass of the head
// costs the tape: the engine calls Wear::process for every frame the write
// head passes and puts the result back on the tape, so the loss builds pass
// over pass. InputDcBlock takes DC off what a dub adds (the input), Wear
// takes off the DC its own saturation makes, and writeBound is the soft
// bound everything written to the tape goes through.
// Only what is added is DC-blocked, never the tape going round again: a
// filter on the recirculated tape would act once a pass and its loss would
// build pass over pass (the final review's I2: 15 dB at 50 Hz in 300 passes
// of Wear at 2 %). Rack-free; no libm (FastMath.hpp); no allocation. Any
// input may be NaN or infinite; results are finite.
#include <cstdint>

#include "FastMath.hpp"
#include "SoftBound.hpp"
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
// Shared with Doldrums (SoftBound.hpp).
inline float softBound(float x) {
	return rka::softBound(x);
}

inline float onePoleCoeff(float hz, float fs) {
	return 1.f - fastExp2(-6.28318530717959f * hz / fs * 1.44269504088896f);
}

inline float flushTiny(float x) {
	return (x < 1e-20f && x > -1e-20f) ? 0.f : x;
}

} // namespace detail

// The DC blocker on the dub's input (WRITE_DC_HZ, a one-pole high-pass with
// unity gain in its passband: the output is the input less the DC as it
// stood before this sample). The engine runs it on the input every sample,
// dubbing or not, so it has settled when a dub starts. Held to +-100 V
// first, so it cannot overflow.
struct InputDcBlock {
	void setSampleRate(float fs) {
		fs = fs == fs ? clampKeepNan(fs, 1000.f, 4e6f) : 48000.f;
		a_ = detail::onePoleCoeff(WRITE_DC_HZ, fs);
	}
	void reset() { dc_[0] = dc_[1] = 0.f; }

	float process(int c, float x) {
		float& dc = dc_[c & 1];
		const float y = detail::tameVolts(x) - dc;
		dc = detail::flushTiny(dc + a_ * y);
		return y;
	}

private:
	float a_ = 6.5e-4f;
	float dc_[2] = {0.f, 0.f};
};

// The bound every write to the tape passes: the soft bound (exact to 8 V, a
// ceiling of 10 V; NaN as silence).
inline float writeBound(float x) {
	return detail::softBound(detail::tameVolts(x));
}

// The bound as a voice writes through it. Lopsided material synced to the
// loop and dubbed pass after pass at a low Dub Level is driven far past the
// ceiling, clipped harder on one side, and the clipping leaves volts of DC
// on the tape (the final re-review's R1: -3.9 V from a 1 V 25 % pulse at
// Dub 5 %). Taking off only the DC of the change the bound makes cannot fix
// that (the clipped frames sit at the ceiling, which eats the correction);
// what does is what 943c0d0's 5 Hz blocker did, taking the written signal's
// DC off before the bound, which centres what is clipped. But that blocker,
// acting all the time, cost the tape its bass pass after pass (I2). So here
// it acts only while the bound does: the share of frames the bound is
// clipping (above 8 V either way, low-passed at WRITE_DC_HZ) sets how much
// of the written signal's DC (low-passed at WRITE_DC_HZ) is taken off before
// the bound, in full from a 5 % share. Where nothing is clipped the share
// and the gate are exactly 0 and the write is the bound itself: the tape
// going round below 8 V is not filtered (an offset recorded on it stays).
// One per voice; its state goes on from the voice a new one replaces on the
// same tape (Voices::start: on a short loop an estimate begun again at
// every join would never settle) and is fresh (reset()) where none precedes
// it, so nothing carries from one tape to the next.
struct WriteBound {
	void setSampleRate(float fs) {
		fs = fs == fs ? clampKeepNan(fs, 1000.f, 4e6f) : 48000.f;
		a_ = detail::onePoleCoeff(WRITE_DC_HZ, fs);
	}
	void reset() {
		dc_[0] = dc_[1] = 0.f;
		clip_[0] = clip_[1] = 0.f;
	}

	float process(int c, float x) {
		const int s = c & 1;
		const float in = detail::tameVolts(x);
		const bool clips = in > 8.f || in < -8.f;
		clip_[s] = detail::flushTiny(clip_[s] + a_ * ((clips ? 1.f : 0.f) - clip_[s]));
		const float g = clip_[s] * WRITE_CLIP_GATE;
		const float gate = g < 1.f ? g : 1.f;
		dc_[s] = detail::flushTiny(dc_[s] + a_ * (in - dc_[s]));
		return detail::softBound(in - gate * dc_[s]);
	}

private:
	float a_ = 6.5e-4f;
	float dc_[2] = {0.f, 0.f}, clip_[2] = {0.f, 0.f};
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
		dcA_ = detail::onePoleCoeff(WRITE_DC_HZ, fs_);
		edge_ = WEAR_DROPOUT_EDGE_S * fs_;
	}

	// The knob, 0..1 (NaN reads 0), per control tick.
	void set(float amount) {
		const float w = amount == amount ? clampKeepNan(amount, 0.f, 1.f) : 0.f;
		const float w2 = w * w;
		active_ = w > 0.f;
		// At 0 the saturation's DC estimate is not kept: process() does not
		// update it there, and carried as it stood (takeSatDc) it would be
		// written onto whatever the tape holds when Wear comes back (the
		// fourth re-review's F4-1: +311 mV on a 50 ms loop dubbed meanwhile).
		if (!active_)
			resetSatDc();
		gain_ = fastDbToGain(WEAR_LEVEL_DB * w2);
		highs_ = WEAR_HIGHS_AMOUNT * w2;
		drive_ = WEAR_DRIVE_BASE + w;
		// Uniform noise of this RMS: the amplitude is sqrt(3) times it.
		hissAmp_ = 1.7320508f * WEAR_HISS_REF_VOLTS * fastDbToGain(WEAR_HISS_DB) * w;
		dropRate_ = WEAR_DROPOUT_RATE * w2;
	}

	bool active() const { return active_; }

	// The saturation's DC estimate belongs to the tape, not to the voice: on
	// a loop shorter than the estimate takes to settle (32 ms at 5 Hz) one
	// that began at 0 with every voice never would, and what escapes each
	// pass builds on the tape through a dub (the third re-review's R3-1:
	// about 1 V from a 1 V sine and 2nd harmonic at Dub 5 %, Wear 30 %, on a
	// 50 ms loop). So a voice takes it from the voice it replaces on the
	// same tape (Voices::start), and starts at 0 otherwise, and again when
	// Wear has been at 0 (set()). Only this goes on: the hiss, the dropouts
	// and the highs' state are each voice's own.
	void resetSatDc() { satDc_[0] = satDc_[1] = 0.f; }
	void takeSatDc(const Wear& from) {
		satDc_[0] = from.satDc_[0];
		satDc_[1] = from.satDc_[1];
	}

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
		// Saturation: the bound at drive x, scaled back, so it never adds;
		// less the DC of what it changes (on asymmetric material it shifts
		// the mean; the tape's own DC is the tape's, and is not touched).
		const float e = detail::softBound(x * drive_) / drive_ - x - satDc_[s];
		satDc_[s] = detail::flushTiny(satDc_[s] + dcA_ * e);
		x += e;
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
	float dcA_ = 6.5e-4f, satDc_[2] = {0.f, 0.f};
	float budget_ = 1.f, dipPos_ = 0.f, dipLen_ = 0.f, dipDepth_ = 1.f;
	bool active_ = false, inDip_ = false;
};

} // namespace eddy
} // namespace rka

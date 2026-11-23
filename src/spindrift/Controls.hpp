#pragma once
// Spindrift's panel behaviour, ported from Beads' hardware glue (not
// vendored): the pot laws and smoothing of cv_reader.h (PotChannel) and the
// button logic of ui.cc. Rack-free, no allocation.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace spindrift {

// PotChannel::LAW_ATTENUVERTER (cv_reader.h:110-113): centred, a dead zone of
// 0.03 either side of centre, +-1 at the ends.
inline float attenuverterLaw(float knob01) {
	float v = knob01 - 0.5f;
	return (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
}

// PotChannel::LAW_DETUNE (cv_reader.h:114-130): semitones, +-24, a plateau
// on each of 0, 1, 3, 4, 7, 12, 24 (the first third of every sixth of the
// half-range), smooth-stepped between them.
inline float detuneLaw(float knob01) {
	float detune = 2.0f * knob01 - 1.0f;
	float sign = detune < 0.0f ? -1.0f : 1.0f;
	detune = detune * sign * 6.0f;
	int32_t integral = static_cast<int32_t>(detune);
	float fractional = detune - static_cast<float>(integral);
	static const float intervals[8] = {0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f};
	fractional = std::max((fractional - 0.33333333f) * 1.5f, 0.0f);
	float s = fractional * fractional * (3.0f - 2.0f * fractional); // stmlib SmoothStep
	float a = intervals[integral];
	float b = intervals[integral + 1];
	return (a + (b - a) * s) * sign; // stmlib Crossfade
}

// PotChannel's smoothing: a one-pole low pass (starting at 0), then
// stmlib::HysteresisFilter. CvReader::Init uses lp 0.05, hysteresis 0 (0.001
// for the Time pot).
struct PotFilter {
	float lp = 0.f, value = 0.f, coeff = 0.05f, threshold = 0.f;
	void init(float lpCoeff, float hysteresis) {
		coeff = lpCoeff;
		threshold = hysteresis;
		lp = 0.f;
		value = 0.f;
	}
	float process(float v) {
		lp += coeff * (v - lp);
		if (threshold == 0.f) {
			value = lp;
		}
		else {
			float error = lp - value;
			if (error > threshold)
				value = lp - threshold;
			else if (error < -threshold)
				value = lp + threshold;
		}
		return value;
	}
};

// Beads' Processor::Media order.
enum Quality { DIRTY_TAPE = 0, CLEAN_TAPE = 1, VINTAGE = 2, DIGITAL = 3 };

// ui.cc. Beads polls its buttons at 1 kHz (system_clock milliseconds); here
// durations count ticks of Beads' 4 kHz control rate, so ui.cc's
// long_press_duration of 4000 ms (Seed) is 16,000 ticks and 1500 ms (Quality)
// is 6,000 ticks.
struct Buttons {
	static constexpr uint32_t kSeedHoldTicks = 16000;
	static constexpr uint32_t kQualityLongTicks = 6000;

	bool frozenButton = false, seedLatched = true, seedHeld = false;
	Quality quality = DIGITAL, pending = DIGITAL; // pending != quality while frozen
	bool autoGainRequest = false;                 // set by the long press, cleared by the engine

	void reset() {
		*this = Buttons();
	}

	// SEED + FREEZE latches Seed; Freeze does not toggle (ui.cc:248-255).
	void freezePress() {
		if (seedHeld) {
			seedLatched = true;
			seedHeld = false;
			ignoreSeedRelease = true;
			return;
		}
		frozenButton = !frozenButton;
	}

	// A press unlatches Seed (ui.cc:337-340).
	void seedDown() {
		seedLatched = false;
		seedHeld = true;
		ignoreSeedRelease = false;
		seedTicks = 0;
	}
	void seedUp(uint32_t heldTicks) {
		if (!ignoreSeedRelease && heldTicks >= kSeedHoldTicks)
			seedLatched = true;
		seedHeld = false;
		ignoreSeedRelease = false;
	}

	void qualityDown() {}
	// A long press restarts the automatic gain and does not step the quality
	// (ui.cc:402-418); a short one steps (media + 3) % 4: Digital, Vintage,
	// Clean tape, Dirty tape. Frozen, the choice waits in `pending`.
	void qualityUp(uint32_t heldTicks) {
		if (heldTicks >= kQualityLongTicks) {
			autoGainRequest = true;
			return;
		}
		pending = static_cast<Quality>((pending + 3) % 4);
		if (!frozenButton)
			quality = pending;
	}

	// The engine applies the choice made while frozen (beads.cc:352).
	bool hasPending() const {
		return pending != quality;
	}
	void applyPending() {
		quality = pending;
	}

	// 4 kHz: the 4 s Seed hold latches while still held (ui.cc:103-108).
	void tick() {
		if (seedHeld && !ignoreSeedRelease && ++seedTicks >= kSeedHoldTicks) {
			seedLatched = true;
			seedHeld = false;
			ignoreSeedRelease = true;
		}
	}

	// ui.cc:204-210: latched, a triangle breathing (ms & 2047) >> 2 folded to
	// 0..255, scaled 3/4 plus 64; else lit while held. 0..1.
	float seedLight(uint32_t ms) const {
		if (seedLatched) {
			int triangle = static_cast<int>((ms & 2047) >> 2);
			triangle = triangle >= 256 ? 511 - triangle : triangle;
			return static_cast<float>((triangle * 3 >> 2) + 64) / 255.f;
		}
		return seedHeld ? 1.f : 0.f;
	}

private:
	bool ignoreSeedRelease = false;
	uint32_t seedTicks = 0;
};

} // namespace spindrift

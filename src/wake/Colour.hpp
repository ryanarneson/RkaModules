#pragma once
// One side's pass through Wake's loop: Tone, the style's band limits and
// drive, a DC block, and the bound that keeps Feedback above 100 % in
// check. Rack-free; no libm (set() runs at control rate on the audio
// thread).
//
// Order: Tone (low-pass or high-pass) -> the style's low-pass and high-pass
// -> drive -> DC block -> bound. Every filter is one pole, so a pass is
// gentle and many passes add up. A filter that is "none" is out of the path
// exactly: Tone at noon and Digital's band limits change nothing.
#include "FastMath.hpp"
#include "wake/WakeTuning.hpp"

namespace rka {
namespace wake {

// The blend of two styles' constants (while a style change ramps); the
// crossfade flag is `b`'s once past half way.
inline StyleConstants blendStyles(const StyleConstants& a, const StyleConstants& b, float t) {
	const float u = t >= 0.f ? (t <= 1.f ? t : 1.f) : 0.f;
	StyleConstants c;
	c.lowCutHz = a.lowCutHz + u * (b.lowCutHz - a.lowCutHz);
	// A corner of 0 means none: blend through the top of the band.
	const float as = a.highCutHzShort > 0.f ? a.highCutHzShort : 20000.f, bs = b.highCutHzShort > 0.f ? b.highCutHzShort : 20000.f;
	const float al = a.highCutHzLong > 0.f ? a.highCutHzLong : 20000.f, bl = b.highCutHzLong > 0.f ? b.highCutHzLong : 20000.f;
	c.highCutHzShort = (u >= 1.f && b.highCutHzShort <= 0.f) || (u <= 0.f && a.highCutHzShort <= 0.f) ? 0.f : as + u * (bs - as);
	c.highCutHzLong = (u >= 1.f && b.highCutHzLong <= 0.f) || (u <= 0.f && a.highCutHzLong <= 0.f) ? 0.f : al + u * (bl - al);
	c.drive = a.drive + u * (b.drive - a.drive);
	c.glideSeconds = a.glideSeconds + u * (b.glideSeconds - a.glideSeconds);
	c.glideOctaves = a.glideOctaves + u * (b.glideOctaves - a.glideOctaves);
	c.crossfade = u >= 0.5f ? b.crossfade : a.crossfade;
	return c;
}

struct Colour {
	void setSampleRate(float fs) {
		if (!(fs > 0.f))
			return;
		fs_ = fs;
		dc_ = coefficient(LOOP_DC_HZ);
		clear();
	}

	void clear() {
		toneLp_ = toneHp_ = styleLp_ = styleHp_ = dcState_ = 0.f;
	}

	// tone 0..1 (noon flat); `seconds` is this side's echo time, which
	// sets how dark a style with two high-cut corners is.
	void set(float tone, const StyleConstants& c, float seconds) {
		const float t = tone >= 0.f ? (tone <= 1.f ? tone : 1.f) : 0.5f;
		// Tone: flat within 1 % of noon either side.
		aToneLp_ = 1.f;
		aToneHp_ = 0.f;
		if (t < 0.49f)
			aToneLp_ = coefficient(logBetween(TONE_OPEN_HZ, TONE_DARK_HZ, (0.49f - t) / 0.49f));
		else if (t > 0.51f)
			aToneHp_ = coefficient(logBetween(TONE_FULL_HZ, TONE_THIN_HZ, (t - 0.51f) / 0.49f));
		// The style's high cut: from its short-time corner at 10 ms to its
		// long-time corner at HIGH_CUT_LONG_S, on a log scale of time.
		aStyleLp_ = 1.f;
		if (c.highCutHzShort > 0.f && c.highCutHzLong > 0.f) {
			float u = fastLog2((seconds > TIME_MIN_S ? seconds : TIME_MIN_S) / TIME_MIN_S) / fastLog2(HIGH_CUT_LONG_S / TIME_MIN_S);
			u = u <= 1.f ? u : 1.f;
			aStyleLp_ = coefficient(logBetween(c.highCutHzShort, c.highCutHzLong, u));
		}
		aStyleHp_ = c.lowCutHz > 0.f ? coefficient(c.lowCutHz) : 0.f;
		// Drive 1 saturates softly at +-10 V, 2 at +-5 V; under 0.05, clean.
		satLevel_ = c.drive > 0.05f ? 10.f / c.drive : 0.f;
	}

	float process(float x) {
		// Tone.
		if (aToneLp_ < 1.f) {
			toneLp_ += aToneLp_ * (x - toneLp_);
			x = toneLp_;
		}
		else {
			toneLp_ = x;
		}
		x = highPass(x, toneHp_, aToneHp_);
		// The style's band.
		if (aStyleLp_ < 1.f) {
			styleLp_ += aStyleLp_ * (x - styleLp_);
			x = styleLp_;
		}
		else {
			styleLp_ = x;
		}
		x = highPass(x, styleHp_, aStyleHp_);
		// Drive: unity gain for small signals.
		if (satLevel_ > 0.f)
			x = satLevel_ * fastTanh(x / satLevel_);
		// DC out of the loop, always.
		dcState_ += dc_ * (x - dcState_);
		x -= dcState_;
		// The bound; and nothing subnormal goes round.
		const float mag = x >= 0.f ? x : -x;
		if (mag > LOOP_KNEE) {
			const float room = LOOP_CEIL - LOOP_KNEE;
			const float b = LOOP_KNEE + room * fastTanh((mag - LOOP_KNEE) / room);
			x = x >= 0.f ? b : -b;
		}
		else if (mag < 1e-20f) {
			x = 0.f;
			if (dcState_ < 1e-20f && dcState_ > -1e-20f)
				dcState_ = 0.f;
		}
		if (!(x == x)) {   // a NaN must not live in the loop
			clear();
			x = 0.f;
		}
		return x;
	}

private:
	// One-pole coefficient for a corner: 1 - e^(-2 pi f / fs).
	float coefficient(float hz) const {
		const float w = 6.2831853f * hz / fs_;
		if (w >= 20.f)
			return 1.f;
		return 1.f - fastExp2(-w * 1.44269504f);
	}

	static float logBetween(float a, float b, float u) {
		return a * fastExp2(u * fastLog2(b / a));
	}

	// x minus its one-pole low-pass. With no corner the state drains, so
	// nothing is left standing when the filter comes back.
	static float highPass(float x, float& state, float a) {
		if (a > 0.f) {
			state += a * (x - state);
			return x - state;
		}
		if (state != 0.f) {
			state *= 0.99f;
			if (state < 1e-20f && state > -1e-20f)
				state = 0.f;
			return x - state;
		}
		return x;
	}

	float fs_ = 48000.f;
	float aToneLp_ = 1.f, aToneHp_ = 0.f, aStyleLp_ = 1.f, aStyleHp_ = 0.f, satLevel_ = 0.f, dc_ = 0.f;
	float toneLp_ = 0.f, toneHp_ = 0.f, styleLp_ = 0.f, styleHp_ = 0.f, dcState_ = 0.f;
};

} // namespace wake
} // namespace rka

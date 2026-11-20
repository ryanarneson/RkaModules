#pragma once
// A moving read point over a rka::DelayLine that the caller writes. Shared
// by the modules that read a long buffer at a changing place (Wake; later
// Eddy and Spindrift). Rack-free, no allocation, no libm in process().
//
// Three ways to move:
// - Glide: the delay slides to its target, so pitch bends and settles, as a
//   tape machine changing speed. The slide is exponential (a time constant)
//   and its slope is held so pitch stays within a set number of octaves.
// - Crossfade: the output fades from the old delay to the new one, so there
//   is no bend. A target that arrives during a fade waits; the latest wins.
// - Frozen: the line is no longer written, and the tap plays its last
//   `delay()` samples round and round. The join fades from the loop's end
//   into the audio just before its start, which runs straight on into the
//   start. Leaving fades back to the live read.
//
// `mod` (samples, from the caller's Drift) is added to the delay. Frozen,
// there is no live delay to add it to, so its slope sets the loop's speed
// instead: the same pitch movement either way.
#include "DelayLine.hpp"
#include "FastMath.hpp"

#include <cmath>

namespace rka {

struct DelayTap {
	void setSampleRate(float fs) {
		if (!(fs > 0.f))
			return;
		fs_ = fs;
		setGlide(glideSeconds_, glideOctaves_);
		setCrossfade(fadeSeconds_);
	}

	// The delays the tap may take, in samples: keep `hi` inside the line,
	// with room for the frozen loop's join (see freeze()).
	void setRange(float lo, float hi) {
		lo_ = lo >= 1.f ? lo : 1.f;
		hi_ = hi >= lo_ ? hi : lo_;
		d_ = clampD(d_);
		target_ = clampD(target_);
	}

	void setGlide(float seconds, float octaves) {
		glideSeconds_ = seconds > 1e-4f ? seconds : 1e-4f;
		glideOctaves_ = octaves >= 0.f ? (octaves <= 6.f ? octaves : 6.f) : 0.f;
		coef_ = 1.0 - std::exp(-1.0 / ((double) glideSeconds_ * fs_));
		// Pitch ratio is 1 - (the delay's change per sample).
		up_ = 1.0 - std::pow(2.0, -(double) glideOctaves_);
		down_ = std::pow(2.0, (double) glideOctaves_) - 1.0;
	}

	void setCrossfade(float seconds) {
		fadeSeconds_ = seconds > 0.f ? seconds : 0.f;
		const float n = fadeSeconds_ * fs_;
		fadeStep_ = n > 1.f ? 1.f / n : 1.f;
	}

	void snap(float delaySamples) {
		d_ = target_ = clampD(delaySamples);
		fading_ = pending_ = frozen_ = false;
	}

	// The delay to move to; call as often as wanted. Frozen, it is kept
	// for the release.
	void setTarget(float delaySamples, bool crossfade) {
		const double t = clampD(delaySamples);
		if (frozen_ || !crossfade) {
			target_ = t;
			crossfadeTarget_ = crossfade;
			return;
		}
		crossfadeTarget_ = true;
		if (fading_) {
			pendingD_ = t;
			pending_ = true;
			return;
		}
		if (t != d_)
			startFade(t);
		target_ = d_;
	}

	float process(const DelayLine& line, float mod) {
		if (!(mod > -1e9f && mod < 1e9f))
			mod = 0.f;
		const float slope = mod - lastMod_;
		lastMod_ = mod;
		if (frozen_)
			return loopRead(line, slope);
		if (d_ != target_) {
			const double gap = target_ - d_;
			double step = gap * coef_;
			step = step > up_ ? up_ : step < -down_ ? -down_ : step;
			// Landed: the step no longer moves the delay, or what is left
			// is under a thousandth of a sample.
			if (std::fabs(gap) < 1e-3 || d_ + step == d_)
				d_ = target_;
			else
				d_ += step;
		}
		const float live = line.read((float) d_ + mod);
		if (!fading_)
			return live;
		const float old = fromLoop_ ? loopRead(line, slope) : line.read((float) fadeFrom_ + mod);
		// Equal power: the two are different stretches of audio.
		const float a = fade_;
		const float out = old * fastSin2pi(0.25f + 0.25f * a) + live * fastSin2pi(0.25f * a);
		fade_ += fadeStep_;
		if (fade_ >= 1.f) {
			fading_ = fromLoop_ = false;
			if (pending_) {
				pending_ = false;
				if (pendingD_ != d_) {
					startFade(pendingD_);
					target_ = d_;
				}
			}
		}
		return out;
	}

	// Loop the last `loopSamples` (or, if 0, the last delay() samples), to
	// the nearest sample, so the loop repeats exactly. The caller stops
	// writing the line. Playback carries straight on from the last read.
	// The join is `joinSeconds` long, or a quarter of the loop if that is
	// shorter, and reads up to that much beyond the loop: the line needs
	// maxDelay of at least the longest loop plus the join plus 4.
	void freeze(float joinSeconds, float loopSamples = 0.f) {
		if (frozen_)
			return;
		// A fade in progress ends here: the loop is the audio at the delay
		// being faded to.
		fading_ = pending_ = fromLoop_ = false;
		loop_ = std::floor((loopSamples >= 1.f ? (double) loopSamples : d_) + 0.5);
		if (loop_ < d_)
			loop_ = std::floor(d_ + 0.5);   // never shorter than where the read is
		if (loop_ < 4.0)
			loop_ = 4.0;
		double join = std::floor((double) joinSeconds * fs_);
		if (join > loop_ * 0.25)
			join = std::floor(loop_ * 0.25);
		join_ = join >= 1.0 ? join : 1.0;
		// The last read was `d_` back; the line now stands still, so the
		// next sample is one nearer: delay d_ - 1 = loop + 1 - q. The loop
		// spans delays loop + 1 .. 2 (two clear of the newest sample, for
		// the interpolator).
		q_ = loop_ + 2.0 - d_;
		while (q_ >= loop_)
			q_ -= loop_;
		if (q_ < 0.0)
			q_ = 0.0;
		frozen_ = true;
	}

	// Back to the live read at the target, by a fade from the loop.
	void release() {
		if (!frozen_)
			return;
		frozen_ = false;
		fromLoop_ = true;
		fading_ = true;
		fade_ = 0.f;
		pending_ = false;
		if (crossfadeTarget_)
			d_ = target_;   // no bend; the fade covers the move
	}

	bool frozen() const {
		return frozen_;
	}
	float delay() const {
		return (float) d_;
	}
	bool moving() const {
		return fading_ || d_ != target_;
	}

private:
	double clampD(double d) const {
		if (!(d >= lo_))
			return lo_;   // NaN too
		return d <= hi_ ? d : hi_;
	}

	void startFade(double to) {
		fadeFrom_ = d_;
		d_ = to;
		fade_ = 0.f;
		fading_ = true;
		fromLoop_ = false;
	}

	// One sample of the frozen loop. q runs 0 .. loop; the read is at delay
	// loop + 1 - q. Over the last `join` samples it fades to the read one
	// loop older (the audio before the loop's start), which at the wrap is
	// continuous with the start.
	float loopRead(const DelayLine& line, float slope) {
		const double at = loop_ + 1.0 - q_;
		float out = line.read((float) at);
		const double left = loop_ - q_;   // samples to the wrap
		if (left < join_) {
			const float w = (float) (1.0 - left / join_);   // 0 -> 1 across the join
			out += w * (line.read((float) (at + loop_)) - out);
		}
		// Pitch follows the slope of mod: a delay shrinking by 1 % a
		// sample is audio 1 % fast.
		double rate = 1.0 - (double) slope;
		rate = rate < 0.25 ? 0.25 : rate > 4.0 ? 4.0 : rate;
		q_ += rate;
		if (q_ >= loop_)
			q_ -= loop_;
		return out;
	}

	float fs_ = 48000.f;
	float lo_ = 1.f, hi_ = 1.f;
	float glideSeconds_ = 0.15f, glideOctaves_ = 2.f, fadeSeconds_ = 0.04f;
	double coef_ = 1.0, up_ = 0.75, down_ = 3.0;
	double d_ = 1.0, target_ = 1.0;
	// The crossfade.
	bool fading_ = false, pending_ = false, fromLoop_ = false, crossfadeTarget_ = false;
	double fadeFrom_ = 1.0, pendingD_ = 1.0;
	float fade_ = 0.f, fadeStep_ = 1.f;
	// The frozen loop.
	bool frozen_ = false;
	double loop_ = 4.0, join_ = 1.0, q_ = 0.0;
	float lastMod_ = 0.f;
};

} // namespace rka

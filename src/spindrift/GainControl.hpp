#pragma once
// Beads' input gain, gain_control.h/.cc (eurorack f4cfa76; hardware glue,
// not vendored), ported Rack-free. Runs at Beads' codec rate, a block at a
// time, between the edge converter and the Processor, as beads.cc runs it
// between the codec and the Processor. Upstream's logic is kept line for
// line; the differences, all at its edges:
//
// - Input is float (+-1 full scale) rather than the codec's int32: the
//   engine clips it to +-1 first, as the ADC does.
// - The gain setting: upstream's uint8 `gain` (0 automatic, 1..255 a
//   manual gain of 1 + 39 (s / 255)^4) becomes autoGain plus the manual
//   gain itself, clamped to Beads' range [1, 40]. Spindrift's Fixed is the
//   manual path at gain 1, where +-10 V in is Beads' full scale.
// - manual_gain_ starts at 1 (kMinGain). Upstream it is a zeroed global
//   that Init never sets, so the hardware's manual gain fades in from 0
//   over its first ~100 blocks; here Fixed is exactly 1 from the first
//   sample (the one-pole then holds it at 1 exactly), so the dry path is
//   Beads' 0.75 from the start.
// - time_ and the other members start at 0, as upstream's globals do.
//
// Threads: the engine's.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "stmlib/stmlib.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/parameter_interpolator.h"
#include "beads/dsp/frame.h"

namespace spindrift {

class GainControl {
public:
	enum SoftClippingMode {
		SOFT_CLIPPING_MODE_OFF,
		SOFT_CLIPPING_MODE_ON,
		SOFT_CLIPPING_MODE_DYNAMIC
	};

	static float minGain() { return 1.0f; }   // kMinGain
	static float maxGain() { return 40.0f; }  // kMaxGain

	void Init(float sample_rate, SoftClippingMode soft_clipping_mode, bool reset) {
		const float kIgnoreTime = 1.0f;  // second
		const float kActiveTime = 5.0f;  // second
		if (reset) {
			stable_agc_peak_ = agc_peak_ = peak_ = 0.5f;
			gain_ = 1.0f;
			soft_clipping_amount_ = 0.0f;
		}
		soft_clipping_mode_ = soft_clipping_mode;

		ignore_time_ = size_t(sample_rate * kIgnoreTime);

		bool no_readjust = !reset && time_ && time_ >= active_time_;
		active_time_ = size_t(sample_rate * (kIgnoreTime + kActiveTime));
		time_ = no_readjust ? active_time_ : 0;
		one_hertz_ = 1.0f / sample_rate;
	}

	void Restart() {
		time_ = ignore_time_;
	}

	// active: false while the output is muted (beads.cc: !codec_transition_frames).
	// manualGain: the manual path's gain (Fixed: 1), clamped to [1, 40].
	// left/right_normalization: the jack is unpatched; the other side is copied.
	void Process(bool active, bool autoGain, float manualGain, bool left_normalization,
	             bool right_normalization, const beads::FloatFrame* in, beads::FloatFrame* out,
	             size_t size) {
		using namespace stmlib;
		// Nothing is patched in the module, we can safely fill the output buffer with
		// zeroes and take care of the mess later.
		if (left_normalization && right_normalization) {
			beads::FloatFrame silence;
			silence.l = 0.0f;
			silence.r = 0.0f;
			std::fill(&out[0], &out[size], silence);

			peak_ = 0.0f;
			agc_peak_ = stable_agc_peak_ = 0.5f;
			gain_ = 1.0f;
			time_ = 0;
			return;
		}

		const float hz = float(size) * one_hertz_;
		if (time_ < ignore_time_) {
			agc_peak_ = 0.5f;
		}
		else if (time_ < active_time_) {
			SLOPE(agc_peak_, peak_,
			      10.0f * hz,  // Reduce gain in 0.1s if a peak is detected
			      2.0f * hz);  // Adjust gain in 0.5s if the level is too low
			stable_agc_peak_ = agc_peak_;
		}
		else {
			SLOPE(agc_peak_, peak_,
			      5.0f * hz,  // Reduce gain in 0.2s if a peak is detected
			      // Recover from a peak reduction in 2.0s as long as the observed level
			      // isn't lower than what we measured during the learning phase.
			      // Otherwise, pump the gain in 100s.
			      (peak_ > stable_agc_peak_ ? 0.5f : 0.01f) * hz);
		}

		if (active) {
			if (time_ > ignore_time_ || peak_ > 0.001f) {
				time_ += size;
			}
			if (time_ >= active_time_) {
				time_ = active_time_;
			}
		}

		auto_gain_ = autoGain;

		// Upstream: the uint8 setting through 1 + 39 (s / 255)^4; here the gain.
		float manual_gain = manualGain == manualGain ? manualGain : minGain();
		CONSTRAIN(manual_gain, minGain(), maxGain());
		ONE_POLE(manual_gain_, manual_gain, 0.01f);

		const float optimal_gain = std::min(0.9f / std::min(agc_peak_, 0.9f), maxGain());
		const float gain = auto_gain_ ? optimal_gain : manual_gain_;

		float soft_clipping_amount;
		if (soft_clipping_mode_ == SOFT_CLIPPING_MODE_OFF) {
			soft_clipping_amount = 0.0f;
		}
		else if (soft_clipping_mode_ == SOFT_CLIPPING_MODE_ON) {
			soft_clipping_amount = 1.0f;
		}
		else {
			soft_clipping_amount = 2.0f * (manual_gain_ / optimal_gain - 0.75f);
			CONSTRAIN(soft_clipping_amount, 0.0f, 1.0f)
		}

		ParameterInterpolator gain_modulation(&gain_, gain, size);
		ParameterInterpolator soft_clipping_amount_modulation(&soft_clipping_amount_,
		                                                      soft_clipping_amount, size);
		while (size--) {
			float l = left_normalization ? in->r : in->l;
			float r = right_normalization ? in->l : in->r;

			if (active) {
				const float peak = std::max(std::fabs(l), std::fabs(r));
				SLOPE(peak_, peak, 0.1f, 2.0f * one_hertz_);
			}

			const float g = gain_modulation.Next();

			l *= g;
			r *= g;

			if (!auto_gain_) {
				if (soft_clipping_mode_ == SOFT_CLIPPING_MODE_OFF) {
					CONSTRAIN(l, -1.0f, +1.0f)
					CONSTRAIN(r, -1.0f, +1.0f)
				}
				else {
					// When the manual gain is enabled, crossfade between the clean, but
					// possibly hard-clipped signal, and the soft-clipped signal, depending
					// on the value of the soft_clipping_mode flag.
					const float amount = soft_clipping_amount_modulation.Next();
					const float bias = 0.05f;
					const float soft_clipping_l = (1.0f - bias) * SoftClip(l + bias) - bias;
					const float soft_clipping_r = (1.0f - bias) * SoftClip(r + bias) - bias;

					CONSTRAIN(l, -1.0f, +1.0f)
					CONSTRAIN(r, -1.0f, +1.0f)
					l = Crossfade(l, soft_clipping_l, amount);
					r = Crossfade(r, soft_clipping_r, amount);
				}
			}

			out->l = l;
			out->r = r;

			++out;
			++in;
		}
	}

	// The input level the gain gives, 0..255, as Beads' VU light.
	uint8_t vu_meter() const {
		float intensity = peak_ * gain_;
		if (intensity > 1.0f) {
			return 255;
		}
		return uint8_t(192.0f * intensity * intensity);
	}

	// The automatic gain is still learning (Beads blinks its VU light).
	bool adjusting() const {
		return time_ < active_time_ && time_ > 0 && auto_gain_;
	}

	float gain() const {
		return gain_;
	}

private:
	float peak_ = 0.0f;
	float agc_peak_ = 0.0f;
	float stable_agc_peak_ = 0.0f;
	float gain_ = 0.0f;

	float soft_clipping_amount_ = 0.0f;
	SoftClippingMode soft_clipping_mode_ = SOFT_CLIPPING_MODE_OFF;

	bool auto_gain_ = false;
	float manual_gain_ = 1.0f;  // upstream: 0 (see the top of the file)

	float one_hertz_ = 0.0f;

	size_t time_ = 0;
	size_t ignore_time_ = 0;
	size_t active_time_ = 0;
};

}  // namespace spindrift

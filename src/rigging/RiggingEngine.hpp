#pragma once
// Rigging's engine (docs/specs/2026-11-19-rigging-design.md), Rack-free.
// Rigging.cpp only maps params, ports and the menu onto it.
//
// Each sample: a strike if STRUM rose (or, with nothing in STRUM, if V/OCT
// jumped to a new note); the audio input, high-passed, into the strings
// (1, 3, 5 from L; 2, 4, 6 from R) with each string's share and its burst;
// the six strings, each times its level, panned and summed; DC blocked,
// soft-limited; mixed with the dry input. The controls are read every 16
// samples; libm is used only when one of them moved.
#include <cmath>
#include <cstdint>

#include "FastMath.hpp"
#include "rigging/Chords.hpp"
#include "rigging/RiggingTuning.hpp"
#include "rigging/Strike.hpp"
#include "rigging/String.hpp"

namespace rka {

struct RiggingFrame {
	enum { LEVEL_1, LEVEL_2, LEVEL_3, LEVEL_4, LEVEL_5, LEVEL_6, TUNE, CHORD, RING, DAMP, SPREAD, MIX, PARAMS_LEN };
	// Levels 0..1; Tune in semitones, -24..24; Chord 0..6; the rest 0..1.
	// Defaults: every level full, C2, Fifths, Ring 4 s, Damp a third,
	// Spread 60 ms, Mix all strings.
	float params[PARAMS_LEN] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 0.f, 2.f, 0.57666f, 0.33f, 0.342f, 1.f};
	float voct = 0.f, chordCv = 0.f, ringCv = 0.f, dampCv = 0.f, strum = 0.f;
	float inL = 0.f, inR = 0.f;
	bool inLPatched = false, inRPatched = false, strumPatched = false;
	bool freePitch = false;   // Tune not snapped to semitones
};

struct RiggingResult {
	float l = 0.f, r = 0.f;
	bool light = false;
};

class RiggingEngine {
public:
	explicit RiggingEngine(uint32_t seed) {
		strike_.seed(seed);
		for (int k = 0; k < rigging::kStrings; k++) {
			const float a = (rigging::PAN[k] + 1.f) * 0.7853981633974483f;
			panL_[k] = std::cos(a);
			panR_[k] = std::sin(a);
		}
		setSampleRate(48000.f);
	}

	// Allocates (the strings' delays): not from process(). Clears the strings.
	void setSampleRate(float fs) {
		if (!(fs > 0.f && fs <= 768000.f))
			return;
		fs_ = fs;
		for (int k = 0; k < rigging::kStrings; k++)
			strings_[k].setSampleRate(fs);
		strike_.setSampleRate(fs);
		inputHp_ = 1.f - (float) std::exp(-6.283185307179586 * rigging::INPUT_HIGHPASS_HZ / fs);
		outputHp_ = 1.f - (float) std::exp(-6.283185307179586 * rigging::OUT_DC_HZ / fs);
		smooth_ = 1.f - (float) std::exp(-1.0 / (rigging::SMOOTH_S * fs));
		lightSamples_ = (long) (rigging::LIGHT_S * fs + 0.5f);
		noteStep_ = rigging::NOTE_STEP_V / (rigging::NOTE_WINDOW_S * fs);
		holdSamples_ = (long) (rigging::NOTE_HOLD_S * fs + 0.5f);
		reset();
	}

	// Silence: the strings cleared, nothing owed.
	void reset() {
		for (int k = 0; k < rigging::kStrings; k++)
			strings_[k].clear();
		inL_ = inR_ = outL_ = outR_ = 0.f;
		countdown_ = 0;
		first_ = true;
		tune_ = voct_ = ring_ = damp_ = mix_ = -1e9f;
		lastChord_ = -1;
	}

	void process(const RiggingFrame& f, RiggingResult& r) {
		using namespace rigging;
		if (--countdown_ <= 0) {
			countdown_ = kControl;
			controls(f);
		}

		// A strike: STRUM's rising edge, or with nothing in STRUM a new note.
		const float voct = finite(f.voct);
		bool hit = false;
		if (f.strumPatched) {
			const float s = finite(f.strum);
			if (strumHigh_) {
				if (s < 0.1f)
					strumHigh_ = false;
			}
			else if (s >= 1.f) {
				strumHigh_ = true;
				hit = true;
			}
			noteRef_ = voct;
			hold_ = 0;
		}
		else {
			strumHigh_ = false;
			if (hold_ > 0) {
				hold_--;
				noteRef_ = voct;
			}
			else {
				// The reference follows V/OCT at most NOTE_STEP_V a
				// millisecond: a glide stays with it, a new note leaves it behind.
				const float d = voct - noteRef_;
				noteRef_ = d > noteStep_ ? noteRef_ + noteStep_ : d < -noteStep_ ? noteRef_ - noteStep_ : voct;
				const float gap = voct - noteRef_;
				if (gap > NOTE_STEP_V || gap < -NOTE_STEP_V) {
					hit = true;
					noteRef_ = voct;
					hold_ = holdSamples_;
				}
			}
		}
		if (hit) {
			strike_.trigger();
			strikes_++;
			lightLeft_ = lightSamples_;
		}
		float burst[kStrings];
		strike_.process(burst);

		// The input: either jack alone is both sides.
		const float rawL = f.inLPatched ? finite(f.inL) : f.inRPatched ? finite(f.inR) : 0.f;
		const float rawR = f.inRPatched ? finite(f.inR) : rawL;
		// What the strings hear is kept to +-INPUT_MAX volts: a wild input
		// (1e30 V) would otherwise sit in the high-pass for seconds after
		// it had gone. The dry path passes the input as it is.
		const float heardL = rawL > INPUT_MAX ? INPUT_MAX : rawL < -INPUT_MAX ? -INPUT_MAX : rawL;
		const float heardR = rawR > INPUT_MAX ? INPUT_MAX : rawR < -INPUT_MAX ? -INPUT_MAX : rawR;
		inL_ += inputHp_ * (heardL - inL_);
		inR_ += inputHp_ * (heardR - inR_);
		if (inL_ < 1e-20f && inL_ > -1e-20f)
			inL_ = 0.f;
		if (inR_ < 1e-20f && inR_ > -1e-20f)
			inR_ = 0.f;
		const float feed[2] = {(heardL - inL_) * INPUT_GAIN, (heardR - inR_) * INPUT_GAIN};

		float wetL = 0.f, wetR = 0.f;
		for (int k = 0; k < kStrings; k++) {
			const float y = strings_[k].process(feed[k & 1] * share_[k] + burst[k]);
			level_[k] = glide(level_[k], levelTarget_[k]);
			const float v = y * level_[k];
			wetL += v * panL_[k];
			wetR += v * panR_[k];
		}
		// No DC; then the soft limit.
		outL_ += outputHp_ * (wetL - outL_);
		outR_ += outputHp_ * (wetR - outR_);
		if (outL_ < 1e-20f && outL_ > -1e-20f)
			outL_ = 0.f;
		if (outR_ < 1e-20f && outR_ > -1e-20f)
			outR_ = 0.f;
		wetL = limit((wetL - outL_) * OUTPUT_GAIN);
		wetR = limit((wetR - outR_) * OUTPUT_GAIN);

		dry_ = glide(dry_, dryTarget_);
		wet_ = glide(wet_, wetTarget_);
		r.l = rawL * dry_ + wetL * wet_;
		r.r = rawR * dry_ + wetR * wet_;
		r.light = lightLeft_ > 0;
		if (lightLeft_ > 0)
			lightLeft_--;
	}

	// Ring's law: 0.1 s to 60 s over the knob. libm: for the tooltip, and
	// here only when Ring moved.
	static float ringSeconds(float knob01) {
		knob01 = !(knob01 > 0.f) ? 0.f : knob01 > 1.f ? 1.f : knob01;
		return 0.1f * (float) std::pow(600.0, (double) knob01);
	}
	// Spread's law: 0 to 1.5 s, fine near 0.
	static float spreadSeconds(float knob01) {
		knob01 = !(knob01 > 0.f) ? 0.f : knob01 > 1.f ? 1.f : knob01;
		return 1.5f * knob01 * knob01 * knob01;
	}

	// For the tests and the tooltip: where string k is now, the chord, and
	// how many strikes there have been.
	float stringHz(int k) const { return strings_[k < 0 ? 0 : k >= rigging::kStrings ? rigging::kStrings - 1 : k].frequency(); }
	int chord() const { return chord_ < 0 ? 0 : chord_; }
	long strikes() const { return strikes_; }

private:
	enum { kControl = 16 };

	static float finite(float v) {
		return (v - v == 0.f) ? v : 0.f;
	}
	static float clamp01(float x) {
		return x > 0.f ? (x < 1.f ? x : 1.f) : 0.f;
	}

	// One-pole toward the target, arriving exactly.
	float glide(float v, float target) const {
		const float d = target - v;
		if (d < 1e-5f && d > -1e-5f)
			return target;
		return v + smooth_ * d;
	}

	// Ballast's curve: untouched to the knee, never above the ceiling.
	static float limit(float x) {
		using namespace rigging;
		const float a = x >= 0.f ? x : -x;
		if (a <= OUT_KNEE)
			return x;
		const float room = OUT_CEIL - OUT_KNEE;
		const float y = OUT_KNEE + room * fastTanh((a - OUT_KNEE) / room);
		return x >= 0.f ? y : -y;
	}

	void controls(const RiggingFrame& f) {
		using namespace rigging;
		// Pitch: Tune (snapped unless free), V/OCT and the chord.
		float tune = finite(f.params[RiggingFrame::TUNE]);
		tune = tune < -24.f ? -24.f : tune > 24.f ? 24.f : tune;
		if (!f.freePitch)
			tune = (float) (int) (tune + (tune >= 0.f ? 0.5f : -0.5f));
		const float voct = finite(f.voct);
		chord_ = chordSetting(f.params[RiggingFrame::CHORD], f.chordCv, chord_);
		if (chord_ != lastChord_) {
			for (int k = 0; k < kStrings; k++)
				ratio_[k] = chordRatio(chord_, k);
		}
		if (tune != tune_ || voct != voct_ || chord_ != lastChord_) {
			const float f1 = string1Hz(tune, voct);
			for (int k = 0; k < kStrings; k++)
				strings_[k].setFrequency(clampHz(f1 * ratio_[k], fs_));
			tune_ = tune;
			voct_ = voct;
			lastChord_ = chord_;
		}
		const float ring = clamp01(finite(f.params[RiggingFrame::RING]) + finite(f.ringCv) * 0.1f);
		if (ring != ring_) {
			ring_ = ring;
			const float t60 = ringSeconds(ring);
			for (int k = 0; k < kStrings; k++)
				strings_[k].setRing(t60);
		}
		const float damp = clamp01(finite(f.params[RiggingFrame::DAMP]) + finite(f.dampCv) * 0.1f);
		if (damp != damp_) {
			damp_ = damp;
			for (int k = 0; k < kStrings; k++)
				strings_[k].setDamp(damp);
		}
		strike_.setSpread(spreadSeconds(finite(f.params[RiggingFrame::SPREAD])));
		// Equal power, exact at both ends.
		const float mix = clamp01(finite(f.params[RiggingFrame::MIX]));
		if (mix != mix_) {
			mix_ = mix;
			dryTarget_ = mix <= 0.f ? 1.f : mix >= 1.f ? 0.f : fastSin2pi((1.f - mix) * 0.25f);
			wetTarget_ = mix <= 0.f ? 0.f : mix >= 1.f ? 1.f : fastSin2pi(mix * 0.25f);
		}
		for (int k = 0; k < kStrings; k++)
			levelTarget_[k] = clamp01(finite(f.params[RiggingFrame::LEVEL_1 + k]));
		if (first_) {
			// As it starts, everything is where the controls are.
			first_ = false;
			for (int k = 0; k < kStrings; k++) {
				strings_[k].snap();
				level_[k] = levelTarget_[k];
			}
			dry_ = dryTarget_;
			wet_ = wetTarget_;
			noteRef_ = voct;
		}
		for (int k = 0; k < kStrings; k++) {
			strike_.setPeriod(k, strings_[k].periodSamples());
			share_[k] = strings_[k].inputShare();
		}
	}

	rigging::String strings_[rigging::kStrings];
	rigging::Strike strike_;
	float fs_ = 48000.f;
	float panL_[rigging::kStrings], panR_[rigging::kStrings];
	float ratio_[rigging::kStrings] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	float share_[rigging::kStrings] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
	float level_[rigging::kStrings] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f}, levelTarget_[rigging::kStrings] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	float tune_ = -1e9f, voct_ = -1e9f, ring_ = -1e9f, damp_ = -1e9f, mix_ = -1e9f;
	float dry_ = 0.f, wet_ = 1.f, dryTarget_ = 0.f, wetTarget_ = 1.f;
	float inputHp_ = 0.f, outputHp_ = 0.f, smooth_ = 0.01f, noteStep_ = 1e-3f;
	float inL_ = 0.f, inR_ = 0.f, outL_ = 0.f, outR_ = 0.f, noteRef_ = 0.f;
	long lightSamples_ = 4800, lightLeft_ = 0, holdSamples_ = 480, hold_ = 0, strikes_ = 0;
	int chord_ = -1, lastChord_ = -1, countdown_ = 0;
	bool first_ = true, strumHigh_ = false;
};

} // namespace rka

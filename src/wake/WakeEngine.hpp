#pragma once
// Wake's engine (docs/specs/2026-11-20-wake-design.md), Rack-free. Wake.cpp
// only maps params, ports, the Style button and patch state onto it.
//
// Two sides, left and right, each a delay line read by a moving tap
// (DelayTap). Each sample, per side:
//   wet   = the tap's read (Drift wavers it)
//   x     = the input plus Feedback x the wet signal that feeds this side
//           (its own; in Ping Pong the left takes the right's, and the right
//           takes the left's at full level and no input)
//   write = colour(smear(x))           Diffuse, then Tone and the style
// So the line holds exactly what is heard: the first echo has been through
// the loop's colour once, each repeat once more, and a frozen loop is the
// sound that was playing, DC-blocked and bounded like the rest.
// Frozen, nothing is written and each tap loops its last stretch.
// The outputs are the equal-power mix of the input and the wet signal,
// soft-limited; with R unpatched, L carries the average of the two.
//
// The controls are read every 16 samples. No allocation and no libm in
// process(), except when a style change moves the glide's constants.
#include <cmath>
#include <cstdint>
#include <vector>

#include "DelayLine.hpp"
#include "DelayTap.hpp"
#include "FastMath.hpp"
#include "hadal/Drift.hpp"
#include "wake/Colour.hpp"
#include "wake/Smear.hpp"
#include "wake/Timing.hpp"
#include "wake/WakeTuning.hpp"

namespace rka {

struct WakeFrame {
	enum { TIME, TIME_ATT, FEEDBACK, TONE, RATIO, MIX, DIFFUSE, DRIFT, PARAMS_LEN };
	// Time, Feedback, Tone, Mix, Diffuse, Drift 0..1; the attenuverter
	// -1..1; Ratio 0..6. Defaults: 300 ms, 40 %, flat, equal sides, 35 %
	// wet, no Diffuse, Drift 15 %.
	float params[PARAMS_LEN] = {0.4924f, 0.f, 0.3636f, 0.5f, 3.f, 0.35f, 0.f, 0.15f};
	float timeCv = 0.f, feedbackCv = 0.f, mixCv = 0.f, clock = 0.f, freezeGate = 0.f;
	float inL = 0.f, inR = 0.f;
	bool inLPatched = false, inRPatched = false, clockPatched = false, outRPatched = false;
	int style = 0;                          // 0 Tape, 1 Analog, 2 Digital
	bool freeze = false, pingPong = false;  // the latches
};

struct WakeResult {
	float l = 0.f, r = 0.f;
	bool frozen = false;
};

class WakeEngine {
public:
	explicit WakeEngine(uint32_t seed) : seed_(seed) {
		setSampleRate(48000.f);
	}

	// Allocates (the delay lines): not from process(). Clears the delay.
	void setSampleRate(float fs) {
		using namespace wake;
		if (!(fs > 0.f && fs <= 768000.f))
			return;
		fs_ = fs;
		double want = (double) TIME_MAX_S * fs;
		if (want > (double) BUFFER_MAX_SAMPLES)
			want = (double) BUFFER_MAX_SAMPLES;
		maxSamples_ = want >= 2.0 ? (int) want : 2;
		tMax_ = (float) maxSamples_ / fs;
		const int join = (int) (JOIN_S * fs) + 1;
		const int ringLen = maxSamples_ / CONTROL_SAMPLES + 4;
		for (int s = 0; s < 2; s++) {
			line_[s].allocate(maxSamples_ + join + 8, (uint32_t) s);
			tap_[s].setSampleRate(fs);
			tap_[s].setRange(1.f, (float) maxSamples_);
			tap_[s].setCrossfade(CROSSFADE_S);
			colour_[s].setSampleRate(fs);
			smear_[s].setSampleRate(fs, (uint32_t) s);
			// The wander: Hadal's, run fast (a new heading every half second
			// or so), stepped once a control tick.
			wander_[s].setRate(fs / (CONTROL_SAMPLES * DRIFT_SPEEDUP));
			wander_[s].seed(seed_ * 2654435761u + 17u + (uint32_t) s);
			run_[s].assign((size_t) ringLen, 0.0);
		}
		ringLen_ = ringLen;
		smooth_ = 1.f - (float) std::exp(-1.0 / ((double) SMOOTH_S * fs));
		switchStep_ = 1.f / (SWITCH_S * fs > 1.f ? SWITCH_S * fs : 1.f);
		switchSamples_ = (int) (SWITCH_S * fs) > 1 ? (int) (SWITCH_S * fs) : 1;
		styleStep_ = (float) CONTROL_SAMPLES / (STYLE_RAMP_S * fs > 1.f ? STYLE_RAMP_S * fs : 1.f);
		glideSeconds_ = glideOctaves_ = -1.f;
		reset();
	}

	// Silence: the delay cleared, nothing frozen.
	void reset() {
		for (int s = 0; s < 2; s++) {
			line_[s].clear();
			colour_[s].clear();
			smear_[s].clear();
			tap_[s].snap(tap_[s].delay());
			for (size_t i = 0; i < run_[s].size(); i++)
				run_[s][i] = 0.0;
			sum_[s] = 0.0;
			mod_[s] = modStep_[s] = 0.f;
		}
		ringPos_ = 0;
		clock_.reset();
		frozen_ = gateHigh_ = false;
		// A gate already high is not taken until it falls: the line is
		// empty, and freezing it would hold silence.
		gateArmed_ = false;
		for (int s = 0; s < 2; s++)
			cont_[s] = 0;
		writeGain_ = 1.f;
		written_ = 0;
		clockWasPatched_ = false;
		snap_ = true;
		countdown_ = 0;
	}

	void process(const WakeFrame& f, WakeResult& r) {
		using namespace wake;
		// Unplugged, the clock is forgotten: plugged back in, it needs two
		// fresh edges.
		if (f.clockPatched)
			clock_.process(finite(f.clock));
		else if (clockWasPatched_)
			clock_.reset();
		clockWasPatched_ = f.clockPatched;

		// Freeze: the latch, or the gate.
		const float gate = finite(f.freezeGate);
		if (gateHigh_) {
			if (gate <= 0.1f)
				gateHigh_ = false;
		}
		else if (!gateArmed_) {
			gateArmed_ = gate <= 0.1f;
		}
		else if (gate >= 1.f) {
			gateHigh_ = true;
		}
		const bool wantFrozen = f.freeze || gateHigh_;

		if (--countdown_ <= 0) {
			countdown_ = CONTROL_SAMPLES;
			controls(f, wantFrozen);
		}
		// A freeze waits for a crossfade to finish (40 ms at most), which
		// it would otherwise cut short with a step: so a gate that drops
		// for a moment, or a freeze just after Time moved, does not click.
		if (wantFrozen != frozen_ && !(wantFrozen && (tap_[0].fading() || tap_[1].fading()))) {
			frozen_ = wantFrozen;
			for (int s = 0; s < 2; s++) {
				if (frozen_) {
					tap_[s].freeze(JOIN_S, seconds_[s] * fs_, true);
				}
				else {
					tap_[s].release();
					// Writing starts again at another place in the sound.
					// The line's end is joined to the audio one loop older,
					// as the loop that was heard joins its end to its
					// start, and what is written crossfades from the
					// loop's continuation into the new sound: so the line
					// holds no step, and no hole, to play back one echo
					// later. Only what was written since the last release
					// is joined (never the same samples twice, and never
					// more work than there were writes).
					cont_[s] = (int) tap_[s].loopSamples();
					line_[s].joinNewest(written_ < switchSamples_ ? written_ : switchSamples_, cont_[s]);
				}
			}
			if (!frozen_) {
				writeGain_ = 0.f;
				written_ = 0;
			}
		}

		// The input: either jack alone is both sides.
		const float rawL = f.inLPatched ? finite(f.inL) : f.inRPatched ? finite(f.inR) : 0.f;
		const float rawR = f.inRPatched ? finite(f.inR) : rawL;
		const float inL = rawL > INPUT_MAX ? INPUT_MAX : rawL < -INPUT_MAX ? -INPUT_MAX : rawL;
		const float inR = rawR > INPUT_MAX ? INPUT_MAX : rawR < -INPUT_MAX ? -INPUT_MAX : rawR;

		float y[2];
		for (int s = 0; s < 2; s++) {
			mod_[s] += modStep_[s];
			y[s] = tap_[s].process(line_[s], mod_[s]);
		}

		feedback_ = glide(feedback_, feedbackTarget_);
		pp_ = ramp(pp_, ppTarget_);
		if (!frozen_) {
			writeGain_ = ramp(writeGain_, 1.f);
			const float mono = 0.5f * (inL + inR);
			const float xL = inL + pp_ * (mono - inL) + feedback_ * (y[0] + pp_ * (y[1] - y[0]));
			const float xR = (1.f - pp_) * (inR + feedback_ * y[1]) + pp_ * y[0];
			const float wL = colour_[0].process(smear_[0].process(xL));
			const float wR = colour_[1].process(smear_[1].process(xR));
			if (writeGain_ < 1.f) {
				// From the frozen loop's continuation into the new sound.
				const float cL = cont_[0] > 0 ? line_[0].readInt(cont_[0] - 1) : 0.f;
				const float cR = cont_[1] > 0 ? line_[1].readInt(cont_[1] - 1) : 0.f;
				line_[0].write(cL + writeGain_ * (wL - cL));
				line_[1].write(cR + writeGain_ * (wR - cR));
			}
			else {
				line_[0].write(wL);
				line_[1].write(wR);
			}
			if (written_ < switchSamples_)
				written_++;
		}
		else {
			// Nothing is written; the filters run on silence, so they
			// hold nothing stale when writing starts again.
			for (int s = 0; s < 2; s++)
				colour_[s].process(smear_[s].process(0.f));
		}
		const float wetL = y[0], wetR = y[1];

		dry_ = glide(dry_, dryTarget_);
		wet_ = glide(wet_, wetTarget_);
		const float outL = rawL * dry_ + wetL * wet_;
		const float outR = rawR * dry_ + wetR * wet_;
		mono_ = ramp(mono_, f.outRPatched ? 0.f : 1.f);
		r.l = limit(outL + mono_ * (0.5f * (outL + outR) - outL));
		r.r = limit(outR);
		r.frozen = frozen_;
	}

	// Bypassed (no engine state): the dry input by the same rules as
	// process(): either jack alone is both sides, and with R unpatched L
	// carries the average of the two. Not limited (Mix fully left, the
	// same as process() for inputs within the limit's 8 V knee).
	static void bypass(const WakeFrame& f, WakeResult& r) {
		const float l = f.inLPatched ? finite(f.inL) : f.inRPatched ? finite(f.inR) : 0.f;
		const float rr = f.inRPatched ? finite(f.inR) : l;
		r.l = f.outRPatched ? l : 0.5f * (l + rr);
		r.r = rr;
		r.frozen = false;
	}

	// The longest echo at this sample rate, seconds.
	float maxSeconds() const {
		return tMax_;
	}
	// A side's echo time now, seconds (0 left, 1 right).
	float seconds(int side) const {
		return seconds_[side ? 1 : 0];
	}
	// The clock multiple in use, or -1 when Time is in seconds.
	int clockStep() const {
		return clockStep_;
	}
	// For the tests: a side's tap, and Drift's offset in samples.
	const DelayTap& tap(int side) const {
		return tap_[side ? 1 : 0];
	}
	float driftSamples(int side) const {
		return mod_[side ? 1 : 0];
	}

private:
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

	// A straight ramp at the switches' rate.
	float ramp(float v, float target) const {
		if (v < target)
			return v + switchStep_ < target ? v + switchStep_ : target;
		if (v > target)
			return v - switchStep_ > target ? v - switchStep_ : target;
		return v;
	}

	// Ballast's curve: untouched to the knee, never above the ceiling.
	static float limit(float x) {
		using namespace wake;
		const float a = x >= 0.f ? x : -x;
		if (a <= OUT_KNEE)
			return x;
		const float room = OUT_CEIL - OUT_KNEE;
		const float y = OUT_KNEE + room * fastTanh((a - OUT_KNEE) / room);
		return x >= 0.f ? y : -y;
	}

	void controls(const WakeFrame& f, bool wantFrozen) {
		using namespace wake;
		// The style, ramping from where it was.
		const int style = f.style < 0 ? 0 : f.style > 2 ? 2 : f.style;
		if (style != styleTo_ || snap_) {
			styleFrom_ = snap_ ? STYLES[style] : style_;
			styleTo_ = style;
			styleT_ = snap_ ? 1.f : 0.f;
		}
		if (styleT_ < 1.f)
			styleT_ = styleT_ + styleStep_ < 1.f ? styleT_ + styleStep_ : 1.f;
		style_ = blendStyles(styleFrom_, STYLES[styleTo_], styleT_);
		if (style_.glideSeconds != glideSeconds_ || style_.glideOctaves != glideOctaves_) {
			glideSeconds_ = style_.glideSeconds;
			glideOctaves_ = style_.glideOctaves;
			for (int s = 0; s < 2; s++)
				tap_[s].setGlide(glideSeconds_, glideOctaves_);   // libm, only while a style ramps
		}

		// Time, Ratio and Ping Pong: held while frozen.
		if (!wantFrozen || snap_) {
			const float knob = clamp01(finite(f.params[WakeFrame::TIME]));
			float cv = finite(f.timeCv) * finite(f.params[WakeFrame::TIME_ATT]);
			cv = cv > 12.f ? 12.f : cv < -12.f ? -12.f : cv;
			float t;
			if (f.clockPatched && clock_.locked()) {
				clockStep_ = wake::clockStep(knob, cv);
				t = fitTime(clock_.periodSamples() / fs_ * clockMultiple(clockStep_), tMax_);
			}
			else {
				clockStep_ = -1;
				t = timeSeconds(knob) * fastExp2(cv);
				t = t < TIME_MIN_S ? TIME_MIN_S : t > tMax_ ? tMax_ : t;
			}
			float ratio = finite(f.params[WakeFrame::RATIO]);
			ratio = ratio < 0.f ? 0.f : ratio > 6.f ? 6.f : ratio;   // before the cast: a huge value is not an int
			ratioTimes((int) (ratio + 0.5f), t, seconds_[0], seconds_[1]);
			ppTarget_ = f.pingPong ? 1.f : 0.f;
		}

		const float diffuse = clamp01(finite(f.params[WakeFrame::DIFFUSE]));
		const float tone = clamp01(finite(f.params[WakeFrame::TONE]));
		// Drift: each side's tape runs a little fast or slow, by the
		// wander. A delay under such a tape is shorter by the speed summed
		// over its own length, so the offset is that sum (kept as a running
		// total in a ring), and the pitch moves by at most twice the
		// wander's depth.
		const float depth = clamp01(finite(f.params[WakeFrame::DRIFT])) * DRIFT_DEPTH;
		ringPos_ = ringPos_ + 1 < ringLen_ ? ringPos_ + 1 : 0;
		for (int s = 0; s < 2; s++) {
			// The tap reads before this sample is written, so its delay is
			// one short of the echo time; and the smear, when in, delays by
			// its own latency.
			float samples = seconds_[s] * fs_ - 1.f;
			smear_[s].setAmount(diffuse);
			if (smear_[s].engaged())
				samples -= (float) smear_[s].latencySamples();
			if (snap_)
				tap_[s].snap(samples);
			else
				tap_[s].setTarget(samples, style_.crossfade);
			colour_[s].set(tone, style_, seconds_[s]);

			sum_[s] += (double) wander_[s].next() * CONTROL_SAMPLES;
			run_[s][(size_t) ringPos_] = sum_[s];
			// The total as it was one echo ago.
			const float back = seconds_[s] * fs_ / CONTROL_SAMPLES;
			int whole = (int) back;
			whole = whole < 1 ? 1 : whole > ringLen_ - 2 ? ringLen_ - 2 : whole;
			const float frac = back - (float) (int) back;
			int i0 = ringPos_ - whole, i1 = i0 - 1;
			i0 = i0 < 0 ? i0 + ringLen_ : i0;
			i1 = i1 < 0 ? i1 + ringLen_ : i1;
			const double then = run_[s][(size_t) i0] + (double) (frac < 1.f ? frac : 0.f) * (run_[s][(size_t) i1] - run_[s][(size_t) i0]);
			const float target = depth > 0.f ? -depth * (float) (sum_[s] - then) : 0.f;
			// The offset moves no faster than the wander itself can move
			// it. A change of Time or Ratio changes how far back the sum
			// looks, and so the target, at once: the offset slews to it,
			// with no more pitch than Drift ever gives. (A floor, so the
			// offset still comes home when Drift is turned to 0.)
			const float fastest = 2.2f * (depth > 0.1f * DRIFT_DEPTH ? depth : 0.1f * DRIFT_DEPTH);
			float step = (target - mod_[s]) / CONTROL_SAMPLES;
			step = step > fastest ? fastest : step < -fastest ? -fastest : step;
			modStep_[s] = snap_ ? 0.f : step;
			if (snap_)
				mod_[s] = target;
			// Keep the running total small (it only matters as differences).
			if (sum_[s] > 1e9 || sum_[s] < -1e9) {
				for (size_t i = 0; i < run_[s].size(); i++)
					run_[s][i] -= sum_[s];
				sum_[s] = 0.0;
			}
		}

		feedbackTarget_ = FEEDBACK_MAX * clamp01(finite(f.params[WakeFrame::FEEDBACK]) + 0.1f * finite(f.feedbackCv));
		const float mix = clamp01(finite(f.params[WakeFrame::MIX]) + 0.1f * finite(f.mixCv));
		dryTarget_ = fastSin2pi(0.25f + 0.25f * mix);
		wetTarget_ = fastSin2pi(0.25f * mix);
		if (snap_) {
			feedback_ = feedbackTarget_;
			dry_ = dryTarget_;
			wet_ = wetTarget_;
			pp_ = ppTarget_;
			mono_ = f.outRPatched ? 0.f : 1.f;
			snap_ = false;
		}
	}

	uint32_t seed_;
	float fs_ = 48000.f, tMax_ = 10.f;
	int maxSamples_ = 480000, ringLen_ = 4, ringPos_ = 0;
	DelayLine line_[2];
	DelayTap tap_[2];
	wake::Colour colour_[2];
	wake::Smear smear_[2];
	hadal::Wander wander_[2];
	std::vector<double> run_[2];
	double sum_[2] = {0.0, 0.0};
	float mod_[2] = {0.f, 0.f}, modStep_[2] = {0.f, 0.f};
	wake::ClockFollower clock_;
	wake::StyleConstants style_ = wake::STYLES[0], styleFrom_ = wake::STYLES[0];
	int styleTo_ = 0, clockStep_ = -1;
	float styleT_ = 1.f, styleStep_ = 1.f, glideSeconds_ = -1.f, glideOctaves_ = -1.f;
	float seconds_[2] = {0.3f, 0.3f};
	float feedback_ = 0.f, feedbackTarget_ = 0.f, dry_ = 1.f, dryTarget_ = 1.f, wet_ = 0.f, wetTarget_ = 0.f;
	float pp_ = 0.f, ppTarget_ = 0.f, mono_ = 0.f, writeGain_ = 1.f;
	int switchSamples_ = 480, written_ = 0;
	bool clockWasPatched_ = false;
	float smooth_ = 1.f, switchStep_ = 1.f;
	bool frozen_ = false, gateHigh_ = false, gateArmed_ = false, snap_ = true;
	int cont_[2] = {0, 0};   // the released loop's length: what the write crossfades from
	int countdown_ = 0;
};

} // namespace rka

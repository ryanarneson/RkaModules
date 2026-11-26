#pragma once
// One Beacon voice: the stage machine (ADSR and AD modes), Hold's slew,
// the stage gates and TRIG. Rack-free; no libm and no allocation in
// process(). Phase, its step and the level are double: a 60 s stage at
// 768 kHz is 46 M samples, past float's resolution.
//
// Each sample: a stage that finished on the last sample hands on to the next
// one, then edges and mode changes may start a stage, then the stage runs.
// So every stage, even one that ends at once, holds its gate for at least one
// sample, and a stage of time T lasts ceil(T fs) samples.
//
// ADSR mode (GATE_MODE): Attack and Decay while the gate is high, then
// Sustain; the gate's fall starts Release from wherever the envelope is.
// AD mode (AD_MODE): a one-shot, as Maths' trigger; Attack always runs to
// 10 V, then Decay falls to 0 V, whatever the gate does. Sustain and
// Release take no part.
//
// An Attack begun part-way (a retrigger, a gate back during Release)
// picks up its full curve, 0 to 10 V over the Attack time in its Shape, at
// the point with the present level, and finishes it: from a level it takes
// the share of Attack left on that curve, as an RC envelope does. Decay and
// Release take their whole time from wherever they start.
//
// A Shape change mid-stage is a breakpoint (Tides' rule, as a retrigger):
// the rest of the stage becomes the new Shape's curve rescaled from the
// level and phase where it changed to the goal, over the time left. The
// level does not move and the stage still ends at its set time.
#include "Curve.hpp"
#include "FastMath.hpp"
#include "Timing.hpp"

namespace rka {
namespace beacon {

enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
enum Mode { GATE_MODE, AD_MODE, HOLD_MODE };   // the switch: ADSR, AD, HOLD

struct VoiceIn {                  // one channel, one sample
	float attack, decay, release; // seconds, already through stageSecondsCv
	float sustain;                // volts 0..10 (ADSR/AD) or the Hold input, +/-10
	float shape;                  // 0..1
	bool gate, retrig;            // after Schmitt (edges are the voice's job)
	Mode mode;
};

struct VoiceOut {
	float level;                  // OUT
	bool stageGate[4];            // A, D, S, R
	bool trig;                    // TRIG high this sample
};

struct Voice {
	Voice() {
		setSampleRate(48000.f);
		reset();
	}

	// Keeps stage, phase and level; a zero, negative or NaN rate is ignored.
	void setSampleRate(float fs) {
		if (!(fs >= 1.f && fs <= 1e8f))
			return;
		dt_ = 1.0 / fs;
		const int len = int(0.001 * fs + 0.5);
		pulseLen_ = len > 1 ? len : 1;
		if (pulse_ > pulseLen_)
			pulse_ = pulseLen_;
		// 2 ms one-pole: 1 - e^(-1 / (0.002 fs)).
		sustainCoef_ = 1.0 - fastExp2D(-1.4426950408889634 / (0.002 * fs));
	}

	// IDLE at 0 V, pulse off, edges forgotten (a gate held high restarts).
	void reset() {
		stage_ = prevStage_ = IDLE;
		phase_ = prevPhase_ = 0.0;
		level_ = start_ = goal_ = 0.0;
		bpPhase_ = 0.0;
		bpLevel_ = 0.0;
		bpScale_ = 1.0;
		done_ = false;
		pulse_ = 0;
		prevGate_ = prevRetrig_ = false;
		prevMode_ = GATE_MODE;
		mode_ = GATE_MODE;
		time_[0] = 0.01f;
		time_[1] = 0.3f;
		time_[2] = 1.f;
		sustain_ = 0.f;
		shape_ = stageShape_ = 0.75f;
	}

	void process(const VoiceIn& in, VoiceOut& out) {
		const Mode mode = in.mode == HOLD_MODE ? HOLD_MODE : in.mode == AD_MODE ? AD_MODE : GATE_MODE;
		const double ta = pickTime(in.attack, time_[0]);
		const double td = pickTime(in.decay, time_[1]);
		const double tr = pickTime(in.release, time_[2]);
		if (in.shape >= 0.f)
			shape_ = in.shape <= 1.f ? in.shape : 1.f;
		else if (in.shape < 0.f)
			shape_ = 0.f;                                  // NaN keeps the last
		const bool rise = in.gate && !prevGate_, fall = !in.gate && prevGate_;
		const bool retrigRise = in.retrig && !prevRetrig_;
		prevGate_ = in.gate;
		prevRetrig_ = in.retrig;

		if (mode == HOLD_MODE) {
			runHold(in.sustain, ta, td);
		} else {
			if (in.sustain >= 0.f)
				sustain_ = in.sustain <= 10.f ? in.sustain : 10.f;
			else if (in.sustain < 0.f)
				sustain_ = 0.f;                            // NaN keeps the last
			const bool ad = mode == AD_MODE;
			mode_ = mode;   // begin() takes Decay's goal from it
			if (prevMode_ == HOLD_MODE) {
				begin(in.gate ? ATTACK : ad ? DECAY : RELEASE);
			} else if (done_) {
				if (stage_ == ATTACK)
					begin(ad || in.gate ? DECAY : RELEASE);
				else if (stage_ == DECAY)
					begin(ad ? IDLE : in.gate ? SUSTAIN : RELEASE);
				else if (stage_ == RELEASE)
					begin(IDLE);
			}
			// A switch between ADSR and AD retargets the fall. To AD, whose
			// only fall is Decay to 0 V: a Sustain, Release or Decay to
			// Sustain falls from its level over Decay. To ADSR: AD's Decay
			// heads for Sustain while the gate is high, else Release runs.
			if (ad && prevMode_ == GATE_MODE && (stage_ == SUSTAIN || stage_ == RELEASE || stage_ == DECAY))
				begin(DECAY);
			else if (!ad && prevMode_ == AD_MODE && stage_ == DECAY)
				begin(in.gate ? DECAY : RELEASE);
			if (rise || (retrigRise && in.gate))
				begin(ATTACK);
			else if (fall && !ad && stage_ != IDLE && stage_ != RELEASE)
				begin(RELEASE);   // AD ignores the gate's fall
			runStage(stage_ == ATTACK ? ta : stage_ == DECAY ? td : tr);
		}
		prevMode_ = mode;

		out.level = float(level_);
		for (int i = 0; i < 4; i++)
			out.stageGate[i] = stage_ == Stage(ATTACK + i);
		out.trig = pulse_ > 0;
		if (pulse_ > 0)
			pulse_--;
	}

	Stage stage() const { return stage_; }
	double phase() const { return phase_; }   // 0..1 within the stage; Hold: 0
	float level() const { return float(level_); }
	// The running stage's start and goal levels (volts), latched when it
	// began; Hold leaves them as they were.
	double start() const { return start_; }
	double goal() const { return goal_; }
	// The stage that ran before the running one (IDLE at first, and on
	// leaving Hold, whose stages are no envelope's). A gate low as Attack
	// ends goes straight to Release: Release after Attack.
	Stage prevStage() const { return prevStage_; }
	// The running stage's breakpoint: the phase (0..1) and level (volts) its
	// curve runs from, the stage's start (phase 0) until Shape changes
	// mid-stage, then where it last changed. Hold leaves them as they were.
	double bpPhase() const { return bpPhase_; }
	double bpLevel() const { return bpLevel_; }
	// How far that stage had got when it was cut off, 0..1: Attack's or
	// Decay's phase then (1 when it completed); Sustain, which has no
	// phase, 1; 0 with prevStage IDLE.
	double prevPhase() const { return prevPhase_; }

private:
	Stage stage_, prevStage_;
	double phase_, prevPhase_, level_, start_, goal_;
	double bpPhase_, bpLevel_, bpScale_;   // the breakpoint; bpScale_ = 1 / (1 - bpPhase_)
	double dt_ = 1.0 / 48000.0, sustainCoef_ = 0.0;
	bool done_;
	int pulse_ = 0, pulseLen_ = 48;
	bool prevGate_, prevRetrig_;
	Mode prevMode_, mode_ = GATE_MODE;
	float time_[3];       // the last good Attack, Decay, Release times
	float sustain_;       // the last good sustain level (ADSR/AD)
	float shape_;         // the last good Shape
	float stageShape_;    // the Shape the running stage's curve was set with

	// A time in kMinSeconds..kMaxSeconds; NaN keeps the last good one.
	static double pickTime(float t, float& last) {
		if (t >= kMinSeconds)
			last = t <= kMaxSeconds ? t : kMaxSeconds;
		else if (t < kMinSeconds)
			last = kMinSeconds;
		return last;
	}

	void begin(Stage s) {
		prevStage_ = prevMode_ == HOLD_MODE ? IDLE : stage_;
		prevPhase_ = prevStage_ == ATTACK || prevStage_ == DECAY ? phase_ : prevStage_ == IDLE ? 0.0 : 1.0;
		stage_ = s;
		phase_ = 0.0;
		start_ = level_;
		goal_ = s == ATTACK ? 10.0 : s == DECAY && mode_ != AD_MODE ? double(sustain_) : 0.0;
		bpPhase_ = 0.0;
		bpLevel_ = start_;
		bpScale_ = 1.0;
		stageShape_ = shape_;
		done_ = false;
		pulse_ = pulseLen_;
		// Attack from a level in 0..10 V resumes its full curve there: the
		// curve runs from 0 V at phase 0, and the phase starts where it is
		// at the level. (Leaving Hold, a level outside that range keeps the
		// stage's curve rescaled from it, over the full time.)
		if (s == ATTACK && level_ >= 0.0 && level_ < 10.0) {
			phase_ = curvePhase(level_ * 0.1, shape_);
			bpLevel_ = 0.0;
		}
	}

	// The phase at which Attack's rising curve, in this Shape, reaches
	// `frac` of the way (0..1): bisection (the curve rises monotonically),
	// no libm. Called only when a stage begins.
	static double curvePhase(double frac, float shape) {
		double lo = 0.0, hi = 1.0;
		for (int i = 0; i < 40; i++) {
			const double mid = 0.5 * (lo + hi);
			if (double(curveRise(float(mid), shape)) < frac)
				lo = mid;
			else
				hi = mid;
		}
		return lo;
	}

	void runStage(double t) {
		switch (stage_) {
		case IDLE:
			break;
		case SUSTAIN:
			level_ += (double(sustain_) - level_) * sustainCoef_;
			break;
		default:
			if (done_)
				break;
			// Shape moved: a breakpoint here, at the level of the last sample
			// (phase_ < 1 while the stage runs, so the division is safe).
			if (shape_ != stageShape_) {
				bpPhase_ = phase_;
				bpLevel_ = level_;
				bpScale_ = 1.0 / (1.0 - phase_);
				stageShape_ = shape_;
			}
			// A phase within a thousandth of a sample of 1 counts as 1, so a
			// time of a whole number of samples (1 ms at 48 kHz: 0.001f is
			// 5e-8 long) takes that many, not one more.
			const double step = dt_ / t;
			phase_ += step;
			if (phase_ >= 1.0 - 1e-3 * step || start_ == goal_) {
				phase_ = 1.0;
				level_ = goal_;
				done_ = true;
			} else {
				const float p = float((phase_ - bpPhase_) * bpScale_);
				const float c = goal_ > bpLevel_ ? curveRise(p, shape_) : curveFall(p, shape_);
				level_ = bpLevel_ + (goal_ - bpLevel_) * double(c);
			}
		}
	}

	// Slew toward x at 10 V / Attack up and 10 V / Decay down. A, D or S by
	// whether it is limiting up, down or settled, with the hysteresis.
	void runHold(float in, double ta, double td) {
		double x = in;
		if (!(x == x))
			x = level_;                                    // NaN holds the level
		x = x < -10.0 ? -10.0 : x > 10.0 ? 10.0 : x;
		const double up = 10.0 * dt_ / ta, down = 10.0 * dt_ / td;
		const double diff = x - level_;
		if (diff > up)
			level_ += up;
		else if (diff < -down)
			level_ -= down;
		else
			level_ = x;
		const double err = x > level_ ? x - level_ : level_ - x;
		// Settled within 1 mV; leaves settled only beyond 2 mV.
		const bool wasSettled = prevMode_ == HOLD_MODE && stage_ == SUSTAIN;
		const Stage s = err <= (wasSettled ? 0.002 : 0.001) ? SUSTAIN : x > level_ ? ATTACK : DECAY;
		if (s != stage_)
			pulse_ = pulseLen_;
		stage_ = s;
		phase_ = 0.0;
		done_ = false;
	}
};

} // namespace beacon
} // namespace rka

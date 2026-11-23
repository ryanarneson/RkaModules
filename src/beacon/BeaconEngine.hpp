#pragma once
// Beacon: the polyphonic engine. Rack-free: a frame of knob positions and
// per-channel port voltages in, per-channel volts out; Beacon.cpp only maps
// params, ports and lights onto it.
//
// Each sample: MODE (rounded, 0..2), the channel count (GATE's in GATE and
// TRIG, the Sustain CV's in HOLD; 1 unpatched; at most 16), then per
// channel: GATE and RETRIG through Schmitt triggers (0.1 V / 1 V; PUSH or's
// into every channel's gate, and is the gate when GATE is unpatched), the
// times (knob + CV / 10 through stageSecondsCv), the sustain volts, Shape
// through a 5 ms one-pole, and the Voice. A polyphonic cable gives each
// channel its own volts (0 V past its count), a mono one every channel.
// Channels that appear (the count rising) start idle at 0 V; channels past
// the count read 0 V. Channel 1's stage, phase, level, the running
// stage's start and goal levels and the stage before it are published for
// the window with relaxed stores. BeaconLights (below) turns channel 1's
// stage gates into lights.
//
// No libm and no allocation in process(). Non-copyable (the atomics).
#include "FastMath.hpp"
#include "Timing.hpp"
#include "Voice.hpp"

#include <atomic>

namespace rka {

// One port: its voltages (16 readable when channels > 1) and how many
// channels the cable carries (0: unpatched).
struct BeaconPort {
	const float* v = nullptr;
	int channels = 0;
};

struct BeaconFrame {
	enum { SHAPE, MODE, ATTACK, DECAY, SUSTAIN, RELEASE, PUSH, PARAMS_LEN };
	float params[PARAMS_LEN] = {0.75f, 0.f, 0.2093f, 0.5184f, 0.5f, 0.6279f, 0.f};
	BeaconPort attackCv, decayCv, sustainCv, releaseCv, gate, retrig;
};

struct BeaconResult {
	int channels = 1;
	float out[16] = {}, inv[16] = {}, stageGate[4][16] = {}, trig[16] = {};   // volts
};

// Channel 1, for the window and lights; written by process().
struct BeaconPublished {
	std::atomic<int> stage{0};
	std::atomic<float> phase{0.f}, level{0.f};
	std::atomic<int> mode{0};
	std::atomic<float> start{0.f}, goal{0.f};   // the running stage's, volts
	std::atomic<int> prevStage{0};              // the stage that ran before it
};

// The four stage lights, from channel 1's stage gates: note() every sample,
// update() every DIVISION samples (the module's clock divider). A stage seen
// lit since the last update lights its light. A hold of HOLD_SECONDS starts
// on the stage's first update and counts down from there, so a stage of one
// sample still shows for a UI frame, while a longer one goes out by the
// first update after it ends (one light at a time at a stage change).
struct BeaconLights {
	static const int DIVISION = 16;
	static constexpr float HOLD_SECONDS = 0.04f;   // over a 30 Hz frame

	BeaconLights() { reset(); }
	void reset() {
		for (int k = 0; k < 4; k++) {
			seen_[k] = wasSeen_[k] = false;
			left_[k] = 0.f;
		}
	}
	void note(int k, bool on) {
		seen_[k] = seen_[k] || on;
	}
	// `seconds`: the time since the last update. Brightness 0 or 1.
	void update(float seconds, float bright[4]) {
		for (int k = 0; k < 4; k++) {
			if (seen_[k] && !wasSeen_[k])
				left_[k] = HOLD_SECONDS;      // the stage's first update
			else if (left_[k] > 0.f)
				left_[k] -= seconds;
			bright[k] = seen_[k] || left_[k] > 0.f ? 1.f : 0.f;
			wasSeen_[k] = seen_[k];
			seen_[k] = false;
		}
	}

private:
	bool seen_[4];      // lit since the last update
	bool wasSeen_[4];   // lit in the division before
	float left_[4];     // seconds of hold left
};

class BeaconEngine {
public:
	static const int MAX_CHANNELS = 16;

	BeaconEngine() {
		setSampleRate(48000.f);
		reset();
	}
	BeaconEngine(const BeaconEngine&) = delete;
	BeaconEngine& operator=(const BeaconEngine&) = delete;

	// Keeps every voice's stage, phase and level; a bad rate is ignored.
	void setSampleRate(float fs) {
		if (!(fs >= 1.f && fs <= 1e8f))
			return;
		for (int c = 0; c < MAX_CHANNELS; c++)
			voices_[c].setSampleRate(fs);
		// 5 ms one-pole: 1 - e^(-1 / (0.005 fs)).
		shapeCoef_ = 1.0 - fastExp2D(-1.4426950408889634 / (0.005 * double(fs)));
	}

	// Every voice idle at 0 V, gates low; Shape snaps on the next sample.
	void reset() {
		for (int c = 0; c < MAX_CHANNELS; c++) {
			voices_[c].reset();
			gateHigh_[c] = retrigHigh_[c] = false;
			shape_[c] = 0.75f;
		}
		shapeTarget_ = 0.75f;
		snapShape_ = true;
		channels_ = MAX_CHANNELS;   // every voice is fresh: none to reset again
		pub_.stage.store(beacon::IDLE, std::memory_order_relaxed);
		pub_.phase.store(0.f, std::memory_order_relaxed);
		pub_.level.store(0.f, std::memory_order_relaxed);
		pub_.start.store(0.f, std::memory_order_relaxed);
		pub_.goal.store(0.f, std::memory_order_relaxed);
		pub_.prevStage.store(beacon::IDLE, std::memory_order_relaxed);
	}

	void process(const BeaconFrame& f, BeaconResult& out) {
		typedef BeaconFrame F;
		const float m = f.params[F::MODE];
		const beacon::Mode mode = m >= 1.5f ? beacon::HOLD_MODE : m >= 0.5f ? beacon::TRIG_MODE : beacon::GATE_MODE;
		const BeaconPort& countPort = mode == beacon::HOLD_MODE ? f.sustainCv : f.gate;
		int n = countPort.channels;
		n = n < 1 ? 1 : n > MAX_CHANNELS ? MAX_CHANNELS : n;

		const float sh = f.params[F::SHAPE];
		if (sh >= 0.f)
			shapeTarget_ = sh <= 1.f ? sh : 1.f;
		else if (sh < 0.f)
			shapeTarget_ = 0.f;                            // NaN keeps the last
		if (snapShape_) {
			for (int c = 0; c < MAX_CHANNELS; c++)
				shape_[c] = shapeTarget_;
			snapShape_ = false;
		}
		// Channels that appear start idle at 0 V.
		for (int c = channels_; c < n; c++) {
			voices_[c].reset();
			gateHigh_[c] = retrigHigh_[c] = false;
			shape_[c] = shapeTarget_;
		}
		channels_ = n;

		const bool push = f.params[F::PUSH] >= 0.5f;
		const float sKnob = f.params[F::SUSTAIN];
		for (int c = 0; c < n; c++) {
			beacon::VoiceIn in;
			in.attack = beacon::stageSecondsCv(f.params[F::ATTACK], read(f.attackCv, c));
			in.decay = beacon::stageSecondsCv(f.params[F::DECAY], read(f.decayCv, c));
			in.release = beacon::stageSecondsCv(f.params[F::RELEASE], read(f.releaseCv, c));
			const float s = read(f.sustainCv, c);
			if (mode == beacon::HOLD_MODE) {
				in.sustain = clampKeepNan(s + 10.f * sKnob, -10.f, 10.f);           // NaN: the voice holds
			} else {
				in.sustain = 10.f * clampKeepNan(sKnob + s * 0.1f, 0.f, 1.f);      // NaN: keeps the last
			}
			// In double the pole settles within 1e-13 of the target, so
			// float(y) lands on it exactly.
			double& y = shape_[c];
			y += (double(shapeTarget_) - y) * shapeCoef_;
			in.shape = float(y);
			// Unpatched GATE reads 0 V, so its Schmitt falls and a replug
			// inside the hysteresis is no edge.
			const bool g = schmitt(read(f.gate, c), gateHigh_[c]);
			in.gate = g || push;
			in.retrig = schmitt(read(f.retrig, c), retrigHigh_[c]);
			in.mode = mode;
			beacon::VoiceOut o;
			voices_[c].process(in, o);
			out.out[c] = o.level;
			out.inv[c] = -o.level;
			for (int k = 0; k < 4; k++)
				out.stageGate[k][c] = o.stageGate[k] ? 10.f : 0.f;
			out.trig[c] = o.trig ? 10.f : 0.f;
		}
		for (int c = n; c < MAX_CHANNELS; c++) {
			out.out[c] = out.inv[c] = out.trig[c] = 0.f;
			for (int k = 0; k < 4; k++)
				out.stageGate[k][c] = 0.f;
		}
		out.channels = n;

		const beacon::Voice& v = voices_[0];
		pub_.stage.store(int(v.stage()), std::memory_order_relaxed);
		pub_.phase.store(float(v.phase()), std::memory_order_relaxed);
		pub_.level.store(v.level(), std::memory_order_relaxed);
		pub_.mode.store(int(mode), std::memory_order_relaxed);
		pub_.start.store(float(v.start()), std::memory_order_relaxed);
		pub_.goal.store(float(v.goal()), std::memory_order_relaxed);
		pub_.prevStage.store(int(v.prevStage()), std::memory_order_relaxed);
	}

	const BeaconPublished& published() const { return pub_; }

private:
	beacon::Voice voices_[MAX_CHANNELS];
	bool gateHigh_[MAX_CHANNELS], retrigHigh_[MAX_CHANNELS];
	double shape_[MAX_CHANNELS];  // each voice's smoothed Shape (in float the
	                              // pole would stall short of the target)
	float shapeTarget_ = 0.75f;
	double shapeCoef_ = 0.0;
	bool snapShape_ = true;
	int channels_ = MAX_CHANNELS;
	BeaconPublished pub_;

	// Mono to every channel, poly per channel (0 V past its count), 0 V
	// unpatched.
	static float read(const BeaconPort& p, int c) {
		if (p.channels <= 0 || !p.v)
			return 0.f;
		if (p.channels == 1)
			return p.v[0];
		return c < p.channels ? p.v[c] : 0.f;
	}

	// A Schmitt trigger at 0.1 V and 1 V; NaN reads as low.
	static bool schmitt(float v, bool& high) {
		if (high) {
			if (!(v >= 0.1f))
				high = false;
		} else if (v >= 1.f) {
			high = true;
		}
		return high;
	}
};

} // namespace rka

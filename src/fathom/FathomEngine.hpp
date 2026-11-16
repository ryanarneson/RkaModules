#pragma once
#include "DroneVoice.hpp"
#include <algorithm>
#include <cmath>

// Rack-free per-sample logic of the Fathom module: everything its process()
// does between reading the ports and writing them. The module fills a
// FathomFrame from its params, ports and menu settings; the bench
// (src/bench/bench.cpp) and the tests fill one directly, so all three run
// the same code.
namespace rka {

struct FathomFrame {
	enum Param {
		PITCH, SUB, DETUNE, SPREAD, DRIFT, CUTOFF, DETUNE_ATT, DRIFT_ATT, CUTOFF_ATT,
		RES, DRIVE, RES_ATT, DRIVE_ATT, PARAMS_LEN
	};
	enum Input { VOCT, DETUNE_IN, DRIFT_IN, CUTOFF_IN, RES_IN, DRIVE_IN, INPUTS_LEN };
	enum Output { LEFT, RIGHT, SUB_OUT, OUTPUTS_LEN };

	float params[PARAMS_LEN] = {};
	// Channel 0 of each input; 0 V when unplugged.
	float inputs[INPUTS_LEN] = {};
	// Whether a cable is plugged into each output.
	bool outputConnected[OUTPUTS_LEN] = {};
	// Menu: 0 = one octave below, 1 = two.
	int subOctave = 0;
	// Menu: index into LadderType.
	int filterType = 0;
	// Menu: the sub goes through the filter instead of around it.
	bool subThroughFilter = false;
};

struct FathomEngine {
	// PITCH 0 and V/OCT 0 V is C2.
	static constexpr float C2_HZ = 65.4064f;

	DroneVoice voice;
	OutputStage stage;

	FathomEngine() {
		voice.reset(C2_HZ);
	}

	// The module's Initialize.
	void reset() {
		voice.reset(C2_HZ);
		stage.reset();
		pitchKnown = false;
		cutoffKnown = false;
	}

	// Knob plus attenuated CV, where +/-5 V at full attenuverter sweeps the
	// knob's whole range (every modulated knob here runs 0 ... 1).
	static float modulated(const FathomFrame& f, int param, int input, int att) {
		const float cv = f.inputs[input] / 10.f * f.params[att];
		return f.params[param] + cv * 1.f;
	}

	// rack::clamp, fmax(fmin(x, hi), lo), which sends NaN to hi; written
	// as comparisons, since GCC makes fmin and fmax libm calls.
	static float clampF(float x, float lo, float hi) {
		return x < lo ? lo : x <= hi ? x : hi;
	}

	static float clamp01(float x) {
		return clampF(x, 0.f, 1.f);
	}

	// out: volts for LEFT, RIGHT and SUB_OUT.
	void process(const FathomFrame& f, float sampleRate, float* out) {
		// The pitch's exp2 runs only when PITCH or V/OCT moves.
		const float semis = f.params[FathomFrame::PITCH] + 12.f * f.inputs[FathomFrame::VOCT];
		if (!pitchKnown || semis != lastSemis) {
			f0 = C2_HZ * std::exp2(semis / 12.f);
			lastSemis = semis;
			pitchKnown = true;
		}

		// Nothing patched to LEFT or RIGHT: only the sub runs, and the body
		// and output stage start from rest when a cable comes back.
		if (!f.outputConnected[FathomFrame::LEFT] && !f.outputConnected[FathomFrame::RIGHT]) {
			const DroneVoice::Out o = voice.processSubOnly(f0, f.subOctave + 1, sampleRate);
			out[FathomFrame::LEFT] = out[FathomFrame::RIGHT] = 0.f;
			out[FathomFrame::SUB_OUT] = 5.f * o.sub;
			stageIdle = true;
			return;
		}
		if (stageIdle) {
			stage.reset();
			stageIdle = false;
		}

		const float detune = clamp01(modulated(f, FathomFrame::DETUNE, FathomFrame::DETUNE_IN, FathomFrame::DETUNE_ATT));
		const float drift = clamp01(modulated(f, FathomFrame::DRIFT, FathomFrame::DRIFT_IN, FathomFrame::DRIFT_ATT));
		// The stage reads cutoff, resonance and drive once a block.
		if (stage.readsControls(sampleRate)) {
			const float cutoffOct = clampF(f.params[FathomFrame::CUTOFF] + f.params[FathomFrame::CUTOFF_ATT] * f.inputs[FathomFrame::CUTOFF_IN], 0.f, 10.f);
			if (!cutoffKnown || cutoffOct != lastCutoffOct) {
				controls.cutoffHz = 20.f * std::exp2(cutoffOct);
				lastCutoffOct = cutoffOct;
				cutoffKnown = true;
			}
			controls.resonance = clamp01(modulated(f, FathomFrame::RES, FathomFrame::RES_IN, FathomFrame::RES_ATT));
			controls.drive = clamp01(modulated(f, FathomFrame::DRIVE, FathomFrame::DRIVE_IN, FathomFrame::DRIVE_ATT));
		}
		controls.type = LadderType(f.filterType);
		controls.subThroughFilter = f.subThroughFilter;

		const DroneVoice::Out o = voice.process(f0, detune, f.params[FathomFrame::SPREAD], drift,
		                                        f.params[FathomFrame::SUB], f.subOctave + 1, sampleRate);
		float l = o.left, r = o.right;
		stage.process(controls, o.subMix, sampleRate, l, r);
		out[FathomFrame::LEFT] = 5.f * l;
		out[FathomFrame::RIGHT] = 5.f * r;
		out[FathomFrame::SUB_OUT] = 5.f * o.sub;
	}

private:
	bool pitchKnown = false;
	float lastSemis = 0.f;
	bool cutoffKnown = false;
	float lastCutoffOct = 0.f;
	float f0 = C2_HZ;
	OutputStage::Params controls;
	// The output stage was skipped (no body outputs patched).
	bool stageIdle = false;
};

} // namespace rka

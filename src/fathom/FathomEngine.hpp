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
	}

	// Knob plus attenuated CV, where +/-5 V at full attenuverter sweeps the
	// knob's whole range (every modulated knob here runs 0 ... 1).
	static float modulated(const FathomFrame& f, int param, int input, int att) {
		const float cv = f.inputs[input] / 10.f * f.params[att];
		return f.params[param] + cv * 1.f;
	}

	// rack::clamp: NaN lands on 0.
	static float clamp01(float x) {
		return std::fmax(std::fmin(x, 1.f), 0.f);
	}

	// out: volts for LEFT, RIGHT and SUB_OUT.
	void process(const FathomFrame& f, float sampleRate, float* out) {
		const float semis = f.params[FathomFrame::PITCH] + 12.f * f.inputs[FathomFrame::VOCT];
		const float f0 = C2_HZ * std::exp2(semis / 12.f);
		const float detune = clamp01(modulated(f, FathomFrame::DETUNE, FathomFrame::DETUNE_IN, FathomFrame::DETUNE_ATT));
		const float drift = clamp01(modulated(f, FathomFrame::DRIFT, FathomFrame::DRIFT_IN, FathomFrame::DRIFT_ATT));
		const float cutoffOct = std::fmax(std::fmin(f.params[FathomFrame::CUTOFF] + f.params[FathomFrame::CUTOFF_ATT] * f.inputs[FathomFrame::CUTOFF_IN], 10.f), 0.f);
		OutputStage::Params p;
		p.cutoffHz = 20.f * std::exp2(cutoffOct);
		p.resonance = clamp01(modulated(f, FathomFrame::RES, FathomFrame::RES_IN, FathomFrame::RES_ATT));
		p.drive = clamp01(modulated(f, FathomFrame::DRIVE, FathomFrame::DRIVE_IN, FathomFrame::DRIVE_ATT));
		p.type = LadderType(f.filterType);
		p.subThroughFilter = f.subThroughFilter;

		const DroneVoice::Out o = voice.process(f0, detune, f.params[FathomFrame::SPREAD], drift,
		                                        f.params[FathomFrame::SUB], f.subOctave + 1, sampleRate);
		float l = o.left, r = o.right;
		stage.process(p, o.subMix, sampleRate, l, r);
		out[FathomFrame::LEFT] = 5.f * l;
		out[FathomFrame::RIGHT] = 5.f * r;
		out[FathomFrame::SUB_OUT] = 5.f * o.sub;
	}
};

} // namespace rka

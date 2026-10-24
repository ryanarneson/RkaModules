#pragma once
#include "Drift.hpp"
#include "SuperSaw.hpp"
#include "Svf.hpp"
#include "sway/Waves.hpp"
#include <algorithm>
#include <cmath>

// Rack-independent drone voice: a sine sub under a drifting supersaw body,
// high-passed at the fundamental so the detuned saws don't pile up under
// the root, mixed to stereo. Then an output stage: low-pass, soft clip, DC
// block. Everything is nominal +/-1; the module scales to volts.
namespace rka {

static const float F0_MIN = 8.f;
static const float F0_MAX = 4000.f;
// One-pole on the pitch so V/Oct steps don't click.
static const float PITCH_SLEW_SEC = 0.005f;
// Body and sub gains, tuned so the default settings peak at about 0.95
// before the soft clip (test_voice testDefaultLevel).
static const float BODY_GAIN = 0.8f;
static const float SUB_GAIN = 0.6f;
// Coefficient-heavy work runs once per this many samples.
static const int BLOCK = 16;

struct DroneVoice {
	SuperSaw saws;
	Drift drift;
	Svf hpLeft, hpRight;
	double subPhase = 0.0;
	// Slewed fundamental, Hz.
	float f0 = 65.4064f;
	int block = 0;

	struct Out {
		float left;
		float right;
		// The raw sub sine, +/-1, before SUB and the output stage.
		float sub;
	};

	// Start at f0Hz with no slew, phases at 0.
	void reset(float f0Hz) {
		saws.reset();
		subPhase = 0.0;
		f0 = clampF0(f0Hz);
		hpLeft.reset();
		hpRight.reset();
		hpLeft.setCoefficients(f0, BUTTERWORTH_K, lastRate > 0.f ? lastRate : 48000.f);
		hpRight = hpLeft;
		block = 0;
	}

	Out process(float f0Hz, float detune, float spread, float driftAmount, float subLevel, int subOctaves, float sampleRate) {
		const float dt = 1.f / sampleRate;
		if (sampleRate != lastRate) {
			slew = 1.f - std::exp(-dt / PITCH_SLEW_SEC);
			lastRate = sampleRate;
			block = 0;
		}
		f0 += (clampF0(f0Hz) - f0) * slew;
		if (block == 0) {
			hpLeft.setCoefficients(f0, BUTTERWORTH_K, sampleRate);
			hpRight.setCoefficients(f0, BUTTERWORTH_K, sampleRate);
		}
		block = (block + 1) % BLOCK;

		saws.setDetune(detune);
		drift.process(driftAmount, dt);
		float l, r;
		saws.process(f0, drift.pitchRatio, drift.levelGain, std::max(0.f, std::min(spread, 1.f)), dt, l, r);
		l = hpLeft.process(l).hp * BODY_GAIN;
		r = hpRight.process(r).hp * BODY_GAIN;

		subPhase += f0 / (subOctaves == 2 ? 4.f : 2.f) * dt;
		subPhase -= std::floor(subPhase);
		const float sub = waveSin(float(subPhase));
		const float subMix = std::max(0.f, std::min(subLevel, 1.f)) * SUB_GAIN * drift.subGain * sub;

		Out o;
		o.left = l + subMix;
		o.right = r + subMix;
		o.sub = sub;
		return o;
	}

private:
	float slew = 1.f;
	float lastRate = 0.f;

	static float clampF0(float hz) {
		return std::max(F0_MIN, std::min(hz, F0_MAX));
	}
};

// One-pole DC blocker: y = x - x1 + coef * y1.
struct DcBlock {
	float x1 = 0.f;
	float y1 = 0.f;

	float process(float x, float coef) {
		const float y = x - x1 + coef * y1;
		x1 = x;
		y1 = y;
		return y;
	}
};

// Drive into the soft clip: the low-pass takes the saws' harmonics with it,
// so the voice's +/-1 arrives here nearer +/-0.5 at the default cutoff.
// 2.2 lands the defaults near 4 V with mild saturation (test_voice
// testModuleLevelAtDefaults).
static const float CLIP_DRIVE = 2.2f;
// One-pole on the cutoff, in octaves, so knob and CV steps don't zipper.
static const float CUTOFF_SLEW_SEC = 0.02f;

// Cutoff low-pass, tanh soft clip, DC block at ~2 Hz; in and out +/-1.
struct OutputStage {
	Svf lpLeft, lpRight;
	DcBlock dcLeft, dcRight;
	int block = 0;

	void process(float cutoffHz, float sampleRate, float& left, float& right) {
		if (sampleRate != lastRate) {
			dcCoef = std::exp(-2.f * float(M_PI) * 2.f / sampleRate);
			// The cutoff is smoothed once per block.
			cutoffCoef = 1.f - std::exp(-BLOCK / (sampleRate * CUTOFF_SLEW_SEC));
			lastRate = sampleRate;
			block = 0;
			cutoffOct = std::log2(std::max(cutoffHz, 1.f));
		}
		if (block == 0) {
			cutoffOct += (std::log2(std::max(cutoffHz, 1.f)) - cutoffOct) * cutoffCoef;
			const float fc = std::exp2(cutoffOct);
			lpLeft.setCoefficients(fc, BUTTERWORTH_K, sampleRate);
			lpRight.setCoefficients(fc, BUTTERWORTH_K, sampleRate);
		}
		block = (block + 1) % BLOCK;
		left = dcLeft.process(std::tanh(CLIP_DRIVE * lpLeft.process(left).lp), dcCoef);
		right = dcRight.process(std::tanh(CLIP_DRIVE * lpRight.process(right).lp), dcCoef);
	}

private:
	float dcCoef = 0.f;
	float cutoffCoef = 1.f;
	float cutoffOct = 0.f;
	float lastRate = 0.f;
};

} // namespace rka

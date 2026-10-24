#pragma once
#include "Drift.hpp"
#include "Ladder.hpp"
#include "SuperSaw.hpp"
#include "Svf.hpp"
#include "sway/Waves.hpp"
#include <algorithm>
#include <cmath>

// Rack-independent drone voice: a sine sub under a drifting supersaw body,
// high-passed at the fundamental so the detuned saws don't pile up under
// the root, mixed to stereo. Then an output stage: ladder filter with the
// sub joining before or after it, soft clip, DC block. Everything is
// nominal +/-1; the module scales to volts.
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
		// The body only; the output stage adds the sub.
		float left;
		float right;
		// The sub at its mix level (SUB knob, gain, drift), mono.
		float subMix;
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
		Out o;
		o.left = l;
		o.right = r;
		o.subMix = std::max(0.f, std::min(subLevel, 1.f)) * SUB_GAIN * drift.subGain * sub;
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

// Drive into the soft clip: the ladder takes the saws' harmonics with it,
// so the voice's +/-1 arrives here well under that at the default cutoff.
// Tuned so the defaults land near 4 V with mild saturation (test_voice
// testModuleLevelAtDefaults).
static const float CLIP_DRIVE = 2.2f;
// One-poles on the cutoff (in octaves), the resonance and the drive, so
// knob and CV steps don't zipper.
static const float CUTOFF_SLEW_SEC = 0.02f;
static const float RES_SLEW_SEC = 0.02f;
// DRIVE 0 ... 1 is a gain of 2^(DRIVE_OCTAVES * drive - 1) into the ladder:
// 0.5x at 0, unity at 25 %, 8x at 100 %.
static const float DRIVE_OCTAVES = 4.f;
// Above unity the output is scaled by gain^-MAKEUP_EXP so DRIVE changes the
// tone far more than the level. A driven ladder puts out a near-square
// wave at about 1.9 peak whatever the gain, so the makeup has to take most
// of the gain back out again: 0.75 holds full drive within 2 dB RMS of the
// default after the clip.
static const float MAKEUP_EXP = 0.75f;

// Ladder filter per side with the sub joining before or after it, drive
// makeup, tanh soft clip, DC block at ~2 Hz; in and out +/-1.
struct OutputStage {
	struct Params {
		float cutoffHz = 1200.f;
		// 0 ... 1, the RES knob.
		float resonance = 0.f;
		// 0 ... 1, the DRIVE knob.
		float drive = 0.25f;
		LadderType type = LadderType::LP24;
		// The sub goes through the ladder (true) or joins after it.
		bool subThroughFilter = false;
	};

	Ladder ladderLeft, ladderRight;
	DcBlock dcLeft, dcRight;
	int block = 0;

	void reset() {
		ladderLeft.reset();
		ladderRight.reset();
		dcLeft = DcBlock();
		dcRight = DcBlock();
		block = 0;
		lastRate = 0.f;
	}

	void process(const Params& p, float subMix, float sampleRate, float& left, float& right) {
		if (sampleRate != lastRate) {
			dcCoef = std::exp(-2.f * float(M_PI) * 2.f / sampleRate);
			// The controls are smoothed once per block.
			cutoffCoef = 1.f - std::exp(-BLOCK / (sampleRate * CUTOFF_SLEW_SEC));
			resCoef = 1.f - std::exp(-BLOCK / (sampleRate * RES_SLEW_SEC));
			lastRate = sampleRate;
			block = 0;
			cutoffOct = std::log2(std::max(p.cutoffHz, 1.f));
			k = targetK(p);
			driveOct = targetDriveOct(p);
			ladderLeft.reset();
			ladderRight.reset();
		}
		if (block == 0) {
			cutoffOct += (std::log2(std::max(p.cutoffHz, 1.f)) - cutoffOct) * cutoffCoef;
			k += (targetK(p) - k) * resCoef;
			// The drive is smoothed in octaves, like the cutoff, so a step
			// ramps geometrically instead of jumping most of the way at once.
			driveOct += (targetDriveOct(p) - driveOct) * resCoef;
			const float fc = std::exp2(cutoffOct);
			ladderLeft.setCoefficients(fc, k, p.type, sampleRate);
			ladderRight.setCoefficients(fc, k, p.type, sampleRate);
			driveGain = std::exp2(driveOct);
			makeup = driveOct < 0.f ? std::exp2(-driveOct) : std::exp2(-MAKEUP_EXP * driveOct);
		}
		block = (block + 1) % BLOCK;

		const float subIn = p.subThroughFilter ? subMix : 0.f;
		const float subOut = p.subThroughFilter ? 0.f : subMix;
		float l = ladderLeft.process(driveGain * (left + subIn)) * makeup + subOut;
		float r = ladderRight.process(driveGain * (right + subIn)) * makeup + subOut;
		left = dcLeft.process(std::tanh(CLIP_DRIVE * l), dcCoef);
		right = dcRight.process(std::tanh(CLIP_DRIVE * r), dcCoef);
	}

private:
	float dcCoef = 0.f;
	float cutoffCoef = 1.f;
	float resCoef = 1.f;
	float cutoffOct = 0.f;
	float k = 0.f;
	float driveOct = 0.f;
	float driveGain = 1.f;
	float makeup = 1.f;
	float lastRate = 0.f;

	static float targetK(const Params& p) {
		const float res = std::max(0.f, std::min(p.resonance, 1.f));
		return LADDER_K_MAX * res * res;
	}

	// log2 of the drive gain.
	static float targetDriveOct(const Params& p) {
		const float drive = std::max(0.f, std::min(p.drive, 1.f));
		return DRIVE_OCTAVES * drive - 1.f;
	}
};

} // namespace rka

#pragma once
#include "sway/Waves.hpp"
#include <algorithm>
#include <cmath>

// Rack-independent seven-saw supersaw. The detune ratios and the centre /
// side volume curves are adapted from Befaco Oneiroi's StereoSuperSaw.h
// (Roberto Noris, GPL-3.0-or-later), itself after Adam Szabo, "How to
// Emulate the Super Saw" (2010), the analysis of the Roland JP-8000. The
// oscillator is ours: polyBLEP saws from Sway's Waves.hpp with double
// phase accumulators, panned across stereo by a spread amount.
namespace rka {

struct SuperSaw {
	static const int N = 7;
	double phase[N];
	// Per-saw frequency multiplier and level, from setDetune().
	float ratio[N];
	float volume[N];
	// Overall gain, falling slightly as the detune widens.
	float gain;

	SuperSaw() {
		reset();
		setDetune(0.f);
	}

	// Coherent start: at detune 0 the seven saws are exactly one saw.
	void reset() {
		for (int i = 0; i < N; i++)
			phase[i] = 0.0;
	}

	// detune 0..1 (clamped). 0 is one clean saw; 1 is the JP-8000's widest.
	void setDetune(float detune) {
		const float d = std::max(0.f, std::min(detune, 1.f));
		const float spread = 0.4f * d;
		ratio[0] = 1.f - spread * 0.11002313f;
		ratio[1] = 1.f - spread * 0.06288439f;
		ratio[2] = 1.f - spread * 0.01952356f;
		ratio[3] = 1.f;
		ratio[4] = 1.f + spread * 0.01991221f;
		ratio[5] = 1.f + spread * 0.06216538f;
		ratio[6] = 1.f + spread * 0.10745242f;
		const float v = std::max(0.005f, std::min(0.5f * d, 0.5f));
		const float side = -0.73764f * v * v + 1.2841f * v + 0.044372f;
		for (int i = 0; i < N; i++)
			volume[i] = side;
		volume[3] = -0.55366f * v + 0.99785f;
		gain = 0.3f * (1.4f - spread);
	}

	// One sample at fundamental f0Hz. pitchRatio / levelGain are per-saw
	// multipliers (drift) or null for 1. Saw i sits at spread * (i - 3) / 3
	// across the stereo field with equal-power panning, so at spread 0 each
	// side carries sqrt(1/2) of the mono sum.
	void process(float f0Hz, const float* pitchRatio, const float* levelGain, float spread, float dt, float& left, float& right) {
		left = right = 0.f;
		for (int i = 0; i < N; i++) {
			const float pr = pitchRatio ? pitchRatio[i] : 1.f;
			const float lg = levelGain ? levelGain[i] : 1.f;
			const float dti = f0Hz * ratio[i] * pr * dt;
			phase[i] += dti;
			phase[i] -= std::floor(phase[i]);
			const float s = sawBlep(float(phase[i]), dti) * volume[i] * lg * gain;
			const float pos = spread * float(i - 3) / 3.f;
			const float a = (pos + 1.f) * float(M_PI) / 4.f;
			left += s * std::cos(a);
			right += s * std::sin(a);
		}
	}
};

} // namespace rka

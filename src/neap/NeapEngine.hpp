#pragma once
// Neap's engine: a frame of params and inputs in, the eight cycles, TIDE,
// HIGH and the lights out. Rack-free, C++11; values are in the plan
// (docs/plans/2026-11-24-neap.md, Global Constraints). The module copies
// cycle[] to ALL and applies nothing else; Spring gets shaped[].
//
// Level: +-5 V mode outputs 5 x; 0-10 V mode adds 5 V to the cycles and TIDE.
// HIGH compares in the +-5 V domain (the same comparison as MARK + 5 against
// TIDE + 5) and is 10 V or 0 V whatever Level says.
#include "Cycles.hpp"
#include "NeapRate.hpp"
#include "Tide.hpp"
#include "sway/Triggers.hpp"

namespace rka {

struct NeapFrame {
	float sampleTime = 1.f / 48000.f;
	float rate = 0.5f, rateAtt = 0.f, rateCv = 0.f;
	float weight = 0.f, weightCv = 0.f, mark = 2.f;
	float reset = 0.f;                               // volts
	bool drift = true, unipolar = false;
	neap::Wave wave = neap::WAVE_TRIANGLE;
};

struct NeapResult {
	float cycle[8] = {};    // output volts after Level
	float shaped[8] = {};   // +-5 V before Level, for Spring
	float tide = 0.f, high = 0.f;    // volts after Level (HIGH is 0 or 10 V)
	float light[8] = {};    // 0..1
};

struct NeapEngine {
	// Phases to 0, tide and HIGH cleared; drift state is kept.
	void reset() {
		cycles.reset();
		tide.reset();
		gate.high = false;
		resetTrig = SchmittTrigger();
	}

	void process(const NeapFrame& f, NeapResult& r) {
		// Never hand the cycles a NaN or non-positive step.
		if (f.sampleTime > 0.f && f.sampleTime < 1.f)
			dt = f.sampleTime;

		const float resetV = f.reset == f.reset ? f.reset : 0.f;
		if (resetTrig.process(resetV, 0.1f, 1.f))
			cycles.reset();

		cycles.setWave(f.wave);
		const float e1 = neap::rateExponent(f.rate, f.rateAtt, f.rateCv);
		float x[neap::kCycles];
		cycles.step(e1, f.drift, dt, x);

		const float cv = f.weightCv == f.weightCv ? f.weightCv : 0.f;
		float w = f.weight == f.weight ? f.weight : 0.f;
		w += cv * 0.2f;
		w = w < -1.f ? -1.f : w > 1.f ? 1.f : w;
		const float t = tide.step(x, w, dt);

		const float mark = f.mark;
		const bool high = gate.step(t, mark);

		const float off = f.unipolar ? 5.f : 0.f;
		for (int i = 0; i < neap::kCycles; i++) {
			r.shaped[i] = 5.f * x[i];
			r.cycle[i] = r.shaped[i] + off;
			r.light[i] = (x[i] + 1.f) * 0.5f;
		}
		r.tide = t + off;
		r.high = high ? 10.f : 0.f;
	}

private:
	neap::Cycles cycles;
	neap::Tide tide;
	neap::HighGate gate;
	SchmittTrigger resetTrig;
	float dt = 1.f / 48000.f;
};

} // namespace rka

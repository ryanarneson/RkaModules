#pragma once
// Hadal's Crackle layer (spec: "Crackle: what it is"): sparse random ticks.
//
// - Times: a Poisson process whose rate wanders slowly (new targets every
//   0.25-1 s, glided), so ticks cluster and never fall into a rhythm. The
//   mean rate is `density` (10/s by default; Drift moves it).
// - Sizes: u^2 for u uniform in [0, 1): mostly small, occasionally large,
//   and bounded.
// - Shape: an impulse through a critically damped two-pole low-pass, whose
//   response t e^(-t/tau) is a click. tau sets brightness and length at
//   once: 0.06 ms at 0 km (a bright, short tick), 2 ms at 11 km (a dark,
//   longer thud), on a log scale; Drift's colour scales it by 2^-octaves.
// - Energy: every tick of size 1 carries the same energy (1 V^2 s, in unit
//   terms) whatever tau and the sample rate, so Depth never changes the
//   long-term level; with sizes u^2 and constant-power random pans, the
//   long-term RMS per side is exactly 1.0 at density 10/s.
// - Peaks: a lone tick's peak is 2 / (e sqrt(tau)) for size 1; the
//   brightest tau Drift can reach (0.03 ms) bounds it at PEAK. Overlapping
//   ticks are kept under PEAK by a soft limit that only acts above 0.8 PEAK.
//
// The engine takes shape() of the output (a soft clip, then normalised) and
// scales it by 5 V, so the largest tick reaches +-5 V.
#include "FastMath.hpp"
#include "Random.hpp"

namespace rka {
namespace hadal {

struct Crackle {
	static constexpr float TAU_SHALLOW = 0.06e-3f, TAU_DEEP = 2.0e-3f;
	// 2 / (e sqrt(0.03 ms)), the brightest lone tick, rounded up.
	static constexpr float PEAK = 140.f;

	Random rng;
	float fs = 48000.f;
	float depth = 0.5f, colour = 0.f, density = 10.f;
	double p = 0.9, pp = 0.81, norm = 1.0;
	double y1[2] = {0, 0}, y2[2] = {0, 0};
	// The clustering wander.
	float mod = 0.5f, modTarget = 0.5f;
	int modHold = 0, ctl = 0;
	bool tickedNow = false;
	float panNow = 0.5f;

	void setRate(float rate) {
		fs = rate;
		for (int c = 0; c < 2; c++)
			y1[c] = y2[c] = 0;
		update();
	}
	// A seed restarts the whole layer: random stream, clustering wander and
	// the click filters (the tau and gain tables are kept).
	void seed(uint32_t s) {
		rng.seed(s);
		mod = modTarget = 0.5f;
		modHold = ctl = 0;
		tickedNow = false;
		panNow = 0.5f;
		for (int c = 0; c < 2; c++)
			y1[c] = y2[c] = 0;
	}
	void setDepth(float d01) {
		depth = d01 >= 0.f ? (d01 <= 1.f ? d01 : 1.f) : 0.f;
		update();
	}
	void setColour(float octaves) {
		colour = octaves >= -1.f ? (octaves <= 1.f ? octaves : 1.f) : -1.f;
		update();
	}
	void setDepthColour(float d01, float octaves) {
		depth = d01 >= 0.f ? (d01 <= 1.f ? d01 : 1.f) : 0.f;
		colour = octaves >= -1.f ? (octaves <= 1.f ? octaves : 1.f) : -1.f;
		update();
	}
	void setDensity(float perSecond) {
		density = perSecond >= 0.f ? (perSecond <= 50.f ? perSecond : 50.f) : 0.f;
	}

	void update() {
		// log2 tau: shallow to deep across Depth, minus Drift's octaves.
		const float l0 = -14.024f, l1 = -8.966f;  // log2(0.06 ms), log2(2 ms)
		const float tau = fastExp2(l0 + depth * (l1 - l0) - colour);
		// p = e^(-1 / (tau fs))
		p = fastExp2(-1.4426950f / (tau * fs));
		pp = p * p;
		// Sum over n of ((n + 1) p^n)^2 = (1 + p^2) / (1 - p^2)^3; scale the
		// impulse so the tick's energy in time is 1: sum h^2 / fs = 1.
		const double q = 1.0 - pp;
		norm = std::sqrt(fs * q * q * q / (1.0 + pp));
	}

	bool ticked() const {
		return tickedNow;
	}
	float lastPan() const {
		return panNow;
	}

	// The output shaper (option D, chosen at the listening checkpoint): a
	// unit-layer sample to [-1, 1], tanh(DRIVE x / PEAK) / tanh(DRIVE).
	// Small and mid ticks come up about 3x, the biggest are rounded under
	// the cap: about +9 dB long-term at noon Depth (+7.5 at 0 km, +9.5 at
	// 11 km, since bright ticks peak higher and are squeezed more).
	static constexpr float DRIVE = 3.f;
	static float shape(float x) {
		return fastTanh(x * (DRIVE / PEAK)) * (1.f / 0.99505475f);   // 1 / tanh(3)
	}

	static float softLimit(float x) {
		const float knee = 0.8f * PEAK, room = PEAK - knee;
		const float a = x >= 0.f ? x : -x;
		if (a <= knee)
			return x;
		const float y = knee + room * fastTanh((a - knee) / room);
		return x >= 0.f ? y : -y;
	}

	void process(float& l, float& r) {
		// The clustering wander, at control rate.
		if (++ctl >= 32) {
			ctl = 0;
			if (--modHold <= 0) {
				modTarget = rng.unit();
				modHold = (int) ((0.25f + 0.75f * rng.unit()) * fs / 32.f);
			}
			const float glide = 32.f / (0.2f * fs);
			mod += (modTarget - mod) * (glide < 1.f ? glide : 1.f);   // never overshoots, at any rate
		}
		// Rate 0.15x to 1.85x the density, averaging 1x.
		const float rate = density * (0.15f + 1.7f * mod);
		double xl = 0, xr = 0;
		tickedNow = rng.unit() < rate / fs;
		if (tickedNow) {
			const float u = rng.unit();
			const float size = u * u;
			panNow = rng.unit();
			// Constant-power pan.
			const float gl = fastSin2pi(0.25f * (1.f - panNow) + 0.f), gr = fastSin2pi(0.25f * panNow);
			// Long-term RMS 1.0 at 10/s: E[size^2] = 1/5 and E[g^2] = 1/2,
			// so the mean energy per side per tick is 1/10.
			const double a = size * norm;
			xl = a * gl;
			xr = a * gr;
		}
		double out[2];
		const double x[2] = {xl, xr};
		for (int c = 0; c < 2; c++) {
			double y = 2.0 * p * y1[c] - pp * y2[c] + x[c];
			if (y < 1e-25 && y > -1e-25)
				y = 0;
			y2[c] = y1[c];
			y1[c] = y;
			out[c] = y;
		}
		l = softLimit((float) out[0]);
		r = softLimit((float) out[1]);
	}
};

} // namespace hadal
} // namespace rka

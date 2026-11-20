#pragma once
// Hadal's two noise layers (spec: "Depth: how dark"):
//
// - Rumble: white noise, high-passed at 20 Hz (4th order) so nothing below
//   about 15 Hz eats headroom, low-passed (4th order) at a corner Depth moves
//   from 160 Hz (0 km) to 70 Hz (11 km), then soft-clipped, harder with
//   Depth, so the deep end stays audible on speakers without a sub. Stereo
//   but nearly mono: both sides share 90 % of their noise power, so L/R
//   correlation is about 0.9.
// - Hiss: independent white noise per side, high-passed at 250 Hz (2nd
//   order), low-passed (4th order) at a corner Depth moves from 12 kHz to
//   1.5 kHz.
//
// Corners move on a log scale; Drift adds `colour` octaves to the corner.
// Each layer's output RMS is 1.0 at every corner: the gain comes from a
// table, made on setRate, of the filters' noise power across the corner's
// range (exact for these filters: TPT state-variable sections are the
// bilinear transform of their analog prototypes, so the digital response is
// the analog one at the prewarped frequency). Both setters run at control
// rate (the engine calls them every 32 samples); process() calls no libm.
#include "FastMath.hpp"
#include "Random.hpp"

#include <cmath>

namespace rka {
namespace hadal {

// tan(pi x) for x in (0, 0.5), from FastMath's sine; no libm.
inline float tanPi(float x) {
	return fastSin2pi(0.5f * x) / fastSin2pi(0.5f * x + 0.25f);
}

// One TPT state-variable section (Simper), in double so a 20 Hz corner
// stays exact at 768 kHz.
struct Svf {
	double a1 = 0, a2 = 0, a3 = 0, k = 1.4142, ic1 = 0, ic2 = 0;

	void set(double g, double q) {
		k = 1.0 / q;
		a1 = 1.0 / (1.0 + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}
	void clear() {
		ic1 = ic2 = 0;
	}
	// Returns the low-pass; `high` gets the high-pass.
	double tick(double v0, double& high) {
		const double v3 = v0 - ic2;
		const double v1 = a1 * ic1 + a2 * v3;
		const double v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2 * v1 - ic1;
		ic2 = 2 * v2 - ic2;
		high = v0 - k * v1 - v2;
		return v2;
	}
	double lp(double v) {
		double h;
		return tick(v, h);
	}
	double hp(double v) {
		double h;
		tick(v, h);
		return h;
	}
};

// Butterworth 4th order as two sections.
const double BW4_Q1 = 0.5411961, BW4_Q2 = 1.3065630;

// A layer's shape: a high-pass (order 2 or 4) at a fixed corner and a 4th
// order low-pass whose corner moves; noise variance in is 1/3 (uniform).
struct LayerShape {
	double hpHz;
	int hpOrder;
	float lpShallowHz, lpDeepHz;
};

// Output variance for white noise of variance 1/3 through the shape with the
// low-pass at lpHz, at rate fs: (1/3) (2/fs) * integral over 0..fs/2 of |H|^2.
// libm here: called only from setRate's table.
inline double hpCorner(const LayerShape& s, double fs) {
	// Below Nyquist at any rate: at absurd rates (Rack allows 1 Hz) the
	// corner would otherwise wrap and the filter go unstable.
	return s.hpHz < 0.2 * fs ? s.hpHz : 0.2 * fs;
}

inline double shapeVariance(const LayerShape& s, double lpHz, double fs) {
	const double gl = std::tan(M_PI * lpHz / fs), gh = std::tan(M_PI * hpCorner(s, fs) / fs);
	const int N = 6000;
	const double f0 = 0.25, f1 = 0.5 * fs * 0.9999;
	double sum = 0, prevF = 0, prevV = 0;
	for (int i = 0; i <= N; i++) {
		const double f = f0 * std::pow(f1 / f0, (double) i / N);
		const double t = std::tan(M_PI * f / fs);
		const double x = t / gl, y = t / gh;
		auto lp2 = [](double w, double q) { return 1.0 / ((1 - w * w) * (1 - w * w) + (w / q) * (w / q)); };
		auto hp2 = [](double w, double q) {
			return w * w * w * w / ((1 - w * w) * (1 - w * w) + (w / q) * (w / q));
		};
		double h = lp2(x, BW4_Q1) * lp2(x, BW4_Q2);
		h *= s.hpOrder == 4 ? hp2(y, BW4_Q1) * hp2(y, BW4_Q2) : hp2(y, 0.70710678);
		if (i > 0)
			sum += 0.5 * (h + prevV) * (f - prevF);
		prevF = f;
		prevV = h;
	}
	return (1.0 / 3.0) * (2.0 / fs) * sum;
}

// The shared machinery of a layer: corner law, gain table, filters per side.
struct ShapedNoise {
	static const int TABLE = 65;
	LayerShape shape;
	float fs = 48000.f;
	float depth = 0.5f, colour = 0.f;
	// log2 of the low-pass corner across the table: the Depth range plus
	// an octave either side for Drift.
	float tableLo = 0, tableStep = 1;
	float gainTable[TABLE] = {};
	float gain = 1.f;
	Svf lp[2][2], hp[2][2];
	// log2 of the shallow and deep low-pass corners, once (no libm per
	// sample: update() runs at control rate on the audio thread).
	float log2Shallow, log2Deep;

	explicit ShapedNoise(const LayerShape& s)
	    : shape(s), log2Shallow((float) std::log2(s.lpShallowHz)), log2Deep((float) std::log2(s.lpDeepHz)) {}

	void setRate(float rate) {
		fs = rate;
		const float lo = log2Deep - 1.f, hi = log2Shallow + 1.f;
		tableLo = lo;
		tableStep = (hi - lo) / (TABLE - 1);
		for (int i = 0; i < TABLE; i++) {
			const double hz = std::min(std::pow(2.0, (double) (lo + i * tableStep)), 0.45 * fs);
			gainTable[i] = (float) (1.0 / std::sqrt(shapeVariance(shape, hz, fs)));
		}
		const double gh = std::tan(M_PI * hpCorner(shape, fs) / fs);
		for (int c = 0; c < 2; c++) {
			if (shape.hpOrder == 4) {
				hp[c][0].set(gh, BW4_Q1);
				hp[c][1].set(gh, BW4_Q2);
			}
			else {
				hp[c][0].set(gh, 0.70710678);
			}
			for (int j = 0; j < 2; j++) {
				lp[c][j].clear();
				hp[c][j].clear();
			}
		}
		update();
	}

	void setDepth(float d01) {
		depth = d01 >= 0.f ? (d01 <= 1.f ? d01 : 1.f) : 0.f;
		update();
	}

	void setColour(float octaves) {
		colour = octaves >= -1.f ? (octaves <= 1.f ? octaves : 1.f) : -1.f;
		update();
	}

	// Filter state to silence, tables kept (Initialize: no rebuild under
	// Rack's engine lock).
	void clear() {
		for (int c = 0; c < 2; c++)
			for (int j = 0; j < 2; j++) {
				lp[c][j].clear();
				hp[c][j].clear();
			}
	}

	// Both at once, for the engine's control tick: one update().
	void setDepthColour(float d01, float octaves) {
		depth = d01 >= 0.f ? (d01 <= 1.f ? d01 : 1.f) : 0.f;
		colour = octaves >= -1.f ? (octaves <= 1.f ? octaves : 1.f) : -1.f;
		update();
	}

	void update() {
		float lc = log2Shallow + depth * (log2Deep - log2Shallow) + colour;
		// Never above 0.45 fs (and the hiss never above 16 kHz).
		float hz = fastExp2(lc);
		const float cap = 0.45f * fs < 16000.f ? 0.45f * fs : 16000.f;
		if (hz > cap) {
			hz = cap;
		}
		const float g = tanPi(hz / fs);
		for (int c = 0; c < 2; c++) {
			lp[c][0].set(g, BW4_Q1);
			lp[c][1].set(g, BW4_Q2);
		}
		float pos = (lc - tableLo) / tableStep;
		pos = pos >= 0.f ? (pos <= TABLE - 1.001f ? pos : TABLE - 1.001f) : 0.f;
		const int i = (int) pos;
		const float t = pos - i;
		gain = gainTable[i] + t * (gainTable[i + 1] - gainTable[i]);
	}

	float filter(int c, float x) {
		double v = x;
		v = hp[c][0].hp(v);
		if (shape.hpOrder == 4)
			v = hp[c][1].hp(v);
		v = lp[c][0].lp(v);
		v = lp[c][1].lp(v);
		return (float) v * gain;
	}
};

struct Rumble {
	// The soft clip: tanh(drive x) on the unit-RMS filtered noise, then
	// scaled back to RMS 1.0. The drive rises with Depth on a log scale, so
	// a deep rumble grows overtones a small speaker can play. Peaks never
	// pass peak() (1 / the clipped RMS): 2.0 at the shallow drive, less deeper.
	static constexpr float DRIVE_SHALLOW = 0.66f, DRIVE_DEEP = 2.f;
	static const int DRIVE_TABLE = 33;

	Random rng;
	ShapedNoise n{LayerShape{20.0, 4, 160.f, 70.f}};
	float driveTable[DRIVE_TABLE], normTable[DRIVE_TABLE];
	float drive = DRIVE_SHALLOW, norm = 2.f;

	Rumble() {
		for (int i = 0; i < DRIVE_TABLE; i++) {
			const double d = (double) i / (DRIVE_TABLE - 1);
			const double a = DRIVE_SHALLOW * std::pow((double) DRIVE_DEEP / DRIVE_SHALLOW, d);
			// E[tanh^2(a g)] for a unit Gaussian g: the filtered noise is
			// Gaussian to well within the level tolerance.
			double sum = 0, weight = 0;
			for (int k = 0; k <= 2000; k++) {
				const double g = -8.0 + 16.0 * k / 2000.0, w = std::exp(-0.5 * g * g), t = std::tanh(a * g);
				sum += w * t * t;
				weight += w;
			}
			driveTable[i] = (float) a;
			normTable[i] = (float) (1.0 / std::sqrt(sum / weight));
		}
		updateDrive();
	}

	void setRate(float fs) {
		n.setRate(fs);
	}
	void seed(uint32_t s) {
		rng.seed(s);
	}
	void setDepth(float d01) {
		n.setDepth(d01);
		updateDrive();
	}
	void setColour(float octaves) {
		n.setColour(octaves);
	}
	void setDepthColour(float d01, float octaves) {
		n.setDepthColour(d01, octaves);
		updateDrive();
	}
	void clear() {
		n.clear();
	}
	// The largest sample process() can give, in units of its RMS.
	float peak() const {
		return norm;
	}
	// 90 % of each side's noise power is shared: correlation 0.9.
	void process(float& l, float& r) {
		const float c = rng.uniform(), a = rng.uniform(), b = rng.uniform();
		const float shared = 0.9486833f, own = 0.31622777f;
		l = fastTanh(drive * n.filter(0, shared * c + own * a)) * norm;
		r = fastTanh(drive * n.filter(1, shared * c + own * b)) * norm;
	}

private:
	// Table lookup only (control rate, on the audio thread).
	void updateDrive() {
		float pos = n.depth * (DRIVE_TABLE - 1);
		pos = pos <= DRIVE_TABLE - 1.001f ? pos : DRIVE_TABLE - 1.001f;
		const int i = (int) pos;
		const float t = pos - i;
		drive = driveTable[i] + t * (driveTable[i + 1] - driveTable[i]);
		norm = normTable[i] + t * (normTable[i + 1] - normTable[i]);
	}
};

struct Hiss {
	Random rng;
	ShapedNoise n{LayerShape{250.0, 2, 12000.f, 1500.f}};

	void setRate(float fs) {
		n.setRate(fs);
	}
	void seed(uint32_t s) {
		rng.seed(s);
	}
	void setDepth(float d01) {
		n.setDepth(d01);
	}
	void setColour(float octaves) {
		n.setColour(octaves);
	}
	void setDepthColour(float d01, float octaves) {
		n.setDepthColour(d01, octaves);
	}
	void clear() {
		n.clear();
	}
	void process(float& l, float& r) {
		l = n.filter(0, rng.uniform());
		r = n.filter(1, rng.uniform());
	}
};

} // namespace hadal
} // namespace rka

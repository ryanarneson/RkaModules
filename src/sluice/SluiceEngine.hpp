#pragma once
#include "FastMath.hpp"

#include <cfloat>

// Rack-free per-sample logic of the Sluice module (four VCAs): everything
// its process() does between reading the ports and writing them. The module
// fills a SluiceFrame from its params and ports; the bench and the tests
// fill one directly, so all of them run the same code.
//
// Per row: p = clamp(Level + CV / 10 V, 0, 1) per voice, gain curve(p). An
// unpatched input takes the row above's input (the rope), signal and
// channel count; row A's is silence. An unpatched output adds into the next
// row's output (the mix downward); a patched one stops the carry. A mono
// row spreads into every voice of a wider one; polyphonic rows mix voice by
// voice. Nothing clips and nothing scales the sum.
namespace rka {

// x, or 0 when x is NaN or infinite. The comparisons fail for NaN, so no
// libm call is needed.
inline float sluiceFinite(float x) {
	return x >= -FLT_MAX && x <= FLT_MAX ? x : 0.f;
}

// One port as Rack holds it: 16 voltages and a channel count (0 when
// unplugged).
struct SluicePort {
	const float* v = nullptr;
	int channels = 0;

	// rack::Input::getPolyVoltage: a mono cable drives every channel, and
	// channels beyond the count read 0. NaN and infinity read 0.
	float poly(int c) const {
		return sluiceFinite(channels == 1 ? v[0] : c < channels ? v[c] : 0.f);
	}
};

struct SluiceFrame {
	static const int ROWS = 4, MAX_CHANNELS = 16;
	// CURVE: 0 Lin, 1 Exp.
	enum Param { LEVEL_A, LEVEL_B, LEVEL_C, LEVEL_D, CURVE, PARAMS_LEN };

	float params[PARAMS_LEN] = {};
	SluicePort in[ROWS];
	SluicePort cv[ROWS];
	// Whether a cable is plugged into each output.
	bool outConnected[ROWS] = {};
};

struct SluiceResult {
	float out[SluiceFrame::ROWS][SluiceFrame::MAX_CHANNELS];
	int channels[SluiceFrame::ROWS];
	// Each row's light: the highest p among its voices, before the curve.
	float light[SluiceFrame::ROWS];
};

// The gain for a position p in [0, 1]. Lin: p. Exp: linear in decibels over
// 60 dB, times a linear ramp to zero over the bottom 5 % of travel, so it
// is 0 at p = 0, 1 at p = 1, monotonic and continuous.
inline float sluiceCurve(float p, bool exp) {
	if (!exp)
		return p;
	const float ramp = p < 0.05f ? p * 20.f : 1.f;
	return fastDbToGain(60.f * (p - 1.f)) * ramp;
}

struct SluiceEngine {
	static const int ROWS = SluiceFrame::ROWS, MAX_CHANNELS = SluiceFrame::MAX_CHANNELS;

private:
	// The Level smoother, held as its target and the smoothed value's offset
	// from it, so the offset decays geometrically down to the snap instead
	// of stalling a few float steps short of the target.
	float target[ROWS] = {};
	float offset[ROWS] = {};
	// The next process() snaps the smoothers to their targets.
	bool fresh = true;
	float knownSampleRate = 0.f;
	float coeff = 0.f;
	// The last p and its gain per voice, so the curve is worked out only
	// when p moves. knownExp is the curve they were worked out with.
	float knownP[ROWS][MAX_CHANNELS];
	float knownGain[ROWS][MAX_CHANNELS];
	bool knownExp = false;
	// Each row's own VCA output, before the mix.
	float own[ROWS][MAX_CHANNELS];
	int ownChannels[ROWS] = {};
	float carry[MAX_CHANNELS];

	void forgetGains() {
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < MAX_CHANNELS; c++) {
				knownP[r][c] = -1.f;  // no p is ever -1
				knownGain[r][c] = 0.f;
			}
	}

public:
	SluiceEngine() {
		forgetGains();
	}

	// The module's Initialize: the next process() starts the smoothers at
	// their targets.
	void reset() {
		fresh = true;
		forgetGains();
	}

	void process(const SluiceFrame& f, float sampleRate, SluiceResult& res) {
		if (sampleRate != knownSampleRate) {
			knownSampleRate = sampleRate;
			// One pole of 10 ms: 1 - e^(-1 / (0.010 fs)).
			coeff = 1.f - fastExp2(-1.f / (0.010f * sampleRate * 0.693147180559945f));
		}
		const bool exp = f.params[SluiceFrame::CURVE] > 0.5f;
		if (exp != knownExp) {
			knownExp = exp;
			forgetGains();
		}

		const SluicePort* src = nullptr;
		for (int r = 0; r < ROWS; r++) {
			// Level, smoothed.
			const float t = sluiceFinite(f.params[SluiceFrame::LEVEL_A + r]);
			if (fresh)
				offset[r] = 0.f;
			else {
				// Only when the knob moves: -funsafe-math-optimizations may
				// reassociate offset + (target - t) into (offset - t) +
				// target, which rounds the offset to steps of t's ulp, and
				// done every sample that stalls it short of the snap.
				if (t != target[r])
					offset[r] += target[r] - t;
				offset[r] *= 1.f - coeff;
				if (offset[r] < 1e-6f && offset[r] > -1e-6f)
					offset[r] = 0.f;
			}
			target[r] = t;
			const float level = t + offset[r];

			// The rope: an unpatched input takes the row above's.
			if (f.in[r].channels > 0)
				src = &f.in[r];
			const int inChannels = src ? src->channels : 0;
			const int voices = inChannels > 0 ? inChannels : 1;
			const SluicePort& cv = f.cv[r];

			float lightP = 0.f;
			for (int c = 0; c < voices; c++) {
				const float p = clampKeepNan(level + cv.poly(c) * 0.1f, 0.f, 1.f);
				lightP = p > lightP ? p : lightP;
				if (p != knownP[r][c]) {
					knownP[r][c] = p;
					knownGain[r][c] = sluiceCurve(p, exp);
				}
				const float x = inChannels > 0 ? src->poly(c) : 0.f;
				own[r][c] = x * knownGain[r][c];
			}
			ownChannels[r] = voices;
			res.light[r] = lightP;
		}
		fresh = false;

		// The mix downward: each output is its own row plus the carry, and a
		// patched output stops the carry. Unpatched outputs hold the same sum.
		int carryChannels = 0;  // 0: nothing carried
		for (int r = 0; r < ROWS; r++) {
			const int n = ownChannels[r];
			const int channels = n > carryChannels ? n : carryChannels;
			float* out = res.out[r];
			for (int c = 0; c < channels; c++) {
				const float a = n == 1 ? own[r][0] : c < n ? own[r][c] : 0.f;
				const float b = carryChannels == 1 ? carry[0] : c < carryChannels ? carry[c] : 0.f;
				out[c] = a + b;
			}
			res.channels[r] = channels;
			if (f.outConnected[r])
				carryChannels = 0;
			else {
				for (int c = 0; c < channels; c++)
					carry[c] = out[c];
				carryChannels = channels;
			}
		}
	}
};

} // namespace rka

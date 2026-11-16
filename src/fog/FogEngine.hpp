#pragma once

// Rack-free per-sample logic of the Fog module: everything its process()
// does between reading the ports and writing them. The module fills a
// FogFrame from its params, ports and menu; the bench and tests fill one
// directly, so all of them run the same code.
//
//   IN L, IN R (R normals to L; a poly cable sums to mono)
//     |-- dry ------------------------------------------------+
//     +-- low cut -> Bloom -> RateDivider -> Network -> limiter x mode fade
//                                                              |
//   out = cos(Mix pi/2) dry + sin(Mix pi/2) wet  <-------------+
//
// - Knobs: clamp(param + cv / 10 att, 0, 1): +/-5 V at full attenuverter
//   sweeps the knob's range, as on Fathom. Visibility has no CV.
// - The dry path never touches the RateDivider, so it has no delay at any
//   sample rate; above 96 kHz the wet arrives a few samples late.
// - Mix is an equal-power crossfade, exactly dry at 0 and exactly wet at 1.
//   Dry kill forces Mix to 1, over the knob and its CV. The effective Mix
//   ramps over 5 ms, so a CV step or the dry kill switch does not click.
// - Safety: every input and parameter is sanitised where it enters (NaN and
//   infinity read 0). The limiter is on the wet only: the identity up to
//   +/-9 V, then a tanh knee that ends at +/-10 V.
// - Level: the wet is trimmed by wetTrim() (-6 dB). At noon, +/-5 V saw, sine
//   and noise drones then stay under the limiter's knee (9 V), and square
//   waves may enter it (they stay within 10 V). Peaks of a tonal drone through
//   a drifting tail build up (crest about 3.5 against the static response's 2).
// - Mode: the voicing follows FogFrame::mode. A change fades the wet out
//   over 50 ms, clears and reconfigures the network, and fades it in.
// - Only setSampleRate() allocates. process() allocates nothing and calls
//   no libm.
#include "Bloom.hpp"
#include "FastMath.hpp"
#include "Network.hpp"
#include "RateDivider.hpp"
#include "Voicing.hpp"
#include <cfloat>
#include <cmath>

namespace rka {

inline float fogFinite(float x) {
	return x >= -FLT_MAX && x <= FLT_MAX ? x : 0.f;
}

// One port as Rack holds it: the voltages and a channel count (0 when
// unplugged).
struct FogPort {
	const float* v = nullptr;
	int channels = 0;

	// Every channel added; NaN and infinity read 0.
	float sum() const {
		float s = 0.f;
		for (int c = 0; c < channels && c < 16; c++)
			s += fogFinite(v[c]);
		return fogFinite(s);
	}
	// Channel 0, or 0 when there is none.
	float first() const {
		return channels > 0 ? fogFinite(v[0]) : 0.f;
	}
};

struct FogFrame {
	enum Param { VISIBILITY, SIZE, DAMP, BLOOM, DRIFT, MIX, SIZE_ATT, DAMP_ATT, BLOOM_ATT, DRIFT_ATT, MIX_ATT, PARAMS_LEN };
	float params[PARAMS_LEN] = {};
	FogPort inL, inR;
	FogPort cv[5]; // Size, Damp, Bloom, Drift, Mix
	int mode = 0;
	int driftRate = 0;
	int lowCut = 1; // 0 Off, 1 50 Hz, 2 100 Hz
	bool dryKill = false;
};

struct FogResult {
	float outL, outR;
};

struct FogEngine {
	static float limitKnee() { return 9.f; }
	static float limitCeil() { return 10.f; }
	static float maxRate() { return 768000.f; }
	// Fixed trim on the wet (a fraction), -6 dB.
	static float wetTrim() { return 0.501f; }

	// Test only: bypass the limiter.
	bool limiterEnabled = true;

	// Fathom's law, in [0, 1].
	static float knob(const FogFrame& f, int param, int cvIndex, int att) {
		const float k = fogFinite(f.params[param]) + f.cv[cvIndex].first() * 0.1f * fogFinite(f.params[att]);
		return k >= 0.f ? (k <= 1.f ? k : 1.f) : 0.f;
	}

	FogEngine() {
		setSampleRate(48000.f);
	}

	// A rate that is not finite or <= 0 is ignored (the last good one stays;
	// 48 kHz before any), and so is one above 768 kHz. May allocate.
	void setSampleRate(float hz) {
		if (!(hz > 0.f && hz <= maxRate()))
			return;
		if (hz == hostRate && ready)
			return;
		hostRate = hz;
		ready = true;
		divider.setHostRate(hz);
		net.allocate(divider.innerRate());
		bloomL.setup(hz);
		bloomR.setup(hz);
		fadeStep = 1.f / (0.05f * hz + 1.f);
		mixStep = 1.f / (0.005f * hz + 1.f);
		setLowCutCoeffs();
		reset();
	}

	// Silences everything and starts the current mode clean.
	void reset() {
		divider.reset();
		net.configure(fog::voicing(mode), divider.innerRate());
		bloomL.clear();
		bloomR.clear();
		for (int c = 0; c < 2; c++)
			for (int i = 0; i < 2; i++)
				cutZ[c][i] = 0.0;
		mixSnap = true;
		fade = 1.f;
		fadeState = 0;
	}

	void process(const FogFrame& f, FogResult& res) {
		// Mode: fade out, switch, fade in.
		int want = f.mode;
		want = (want >= 0 && want < fog::MODES_LEN) ? want : (int) fog::HAAR;
		if (want != (int) mode && fadeState == 0)
			fadeState = 1;
		if (fadeState == 1) {
			fade -= fadeStep;
			if (fade <= 0.f) {
				fade = 0.f;
				mode = (fog::Mode) want;
				divider.reset();
				net.configure(fog::voicing(mode), divider.innerRate());
				bloomL.clear();
				bloomR.clear();
				fadeState = 2;
			}
		}
		else if (fadeState == 2) {
			fade += fadeStep;
			if (fade >= 1.f) {
				fade = 1.f;
				fadeState = 0;
			}
		}

		// Inputs: IN R normals to IN L.
		const float dryL = f.inL.sum();
		const float dryR = f.inR.channels > 0 ? f.inR.sum() : dryL;

		// Knobs.
		const float vis = fogFinite(f.params[FogFrame::VISIBILITY]);
		const float size = knob(f, FogFrame::SIZE, 0, FogFrame::SIZE_ATT);
		const float damp = knob(f, FogFrame::DAMP, 1, FogFrame::DAMP_ATT);
		const float bloom = knob(f, FogFrame::BLOOM, 2, FogFrame::BLOOM_ATT);
		const float drift = knob(f, FogFrame::DRIFT, 3, FogFrame::DRIFT_ATT);
		const float mix = f.dryKill ? 1.f : knob(f, FogFrame::MIX, 4, FogFrame::MIX_ATT);
		const fog::Voicing& vo = fog::voicing(mode);
		const float dampK = damp + vo.dampBias * (1.f - damp);

		// The feed: low cut, then Bloom.
		const int cut = (f.lowCut == 1 || f.lowCut == 2) ? f.lowCut : 0;
		float feedL = dryL, feedR = dryR;
		if (cut) {
			feedL = highpass(0, cut, feedL);
			feedR = highpass(1, cut, feedR);
		}
		bloomL.set(bloom, vo.bloomBias);
		bloomR.set(bloom, vo.bloomBias);
		feedL = bloomL.process(feedL);
		feedR = bloomR.process(feedR);

		// The wet, at the inner rate.
		float wetL, wetR;
		const int rateSel = f.driftRate ? 1 : 0;
		divider.process(feedL, feedR, wetL, wetR, [&](float l, float r, float& ol, float& orr) {
			net.setControls(vis, size, dampK, drift, rateSel);
			net.process(l, r, ol, orr);
		});
		wetL = fogFinite(wetL) * wetTrim();
		wetR = fogFinite(wetR) * wetTrim();
		if (limiterEnabled) {
			wetL = limit(wetL);
			wetR = limit(wetR);
		}
		wetL *= fade;
		wetR *= fade;

		// Equal-power mix: cos and sin of Mix pi/2 (exactly 1 and 0 at Mix 0).
		// The effective Mix ramps linearly (5 ms) to its target, so a stepped
		// CV or the dry kill switch does not click; it lands exactly on it.
		if (mixSnap) {
			mixSm = mix;
			mixSnap = false;
		}
		if (mixSm < mix)
			mixSm = mixSm + mixStep < mix ? mixSm + mixStep : mix;
		else if (mixSm > mix)
			mixSm = mixSm - mixStep > mix ? mixSm - mixStep : mix;
		const float dg = fastSin2pi(0.25f * (1.f - mixSm));
		const float wg = fastSin2pi(0.25f * mixSm);
		res.outL = dg * dryL + wg * wetL;
		res.outR = dg * dryR + wg * wetR;
	}

	// ---- parts ----

	// x unchanged up to the knee, then a tanh that ends at the ceiling.
	static float limit(float x) {
		const float a = x >= 0.f ? x : -x;
		if (a <= limitKnee())
			return x;
		const float room = limitCeil() - limitKnee();
		const float y = limitKnee() + room * fastTanh((a - limitKnee()) / room);
		return x >= 0.f ? y : -y;
	}

	// 2nd-order Butterworth high-pass (cut 1: 50 Hz, 2: 100 Hz), transposed
	// direct form II in double, which keeps 50 Hz clean at 768 kHz.
	float highpass(int ch, int cut, float x) {
		const double* c = cutCoef[cut - 1];
		const double in = (double) x;
		const double y = c[0] * in + cutZ[ch][0];
		cutZ[ch][0] = c[1] * in - c[3] * y + cutZ[ch][1];
		cutZ[ch][1] = c[2] * in - c[4] * y;
		if (cutZ[ch][0] < 1e-30 && cutZ[ch][0] > -1e-30)
			cutZ[ch][0] = 0.0;
		if (cutZ[ch][1] < 1e-30 && cutZ[ch][1] > -1e-30)
			cutZ[ch][1] = 0.0;
		return (float) y;
	}

	void setLowCutCoeffs() {
		const double hz[2] = {50.0, 100.0};
		for (int k = 0; k < 2; k++) {
			const double f = hz[k] < 0.4 * hostRate ? hz[k] : 0.4 * hostRate;
			const double K = std::tan(3.14159265358979323846 * f / hostRate);
			const double q = 0.70710678118654752;
			const double n = 1.0 / (1.0 + K / q + K * K);
			cutCoef[k][0] = n;
			cutCoef[k][1] = -2.0 * n;
			cutCoef[k][2] = n;
			cutCoef[k][3] = 2.0 * (K * K - 1.0) * n;
			cutCoef[k][4] = (1.0 - K / q + K * K) * n;
		}
	}

	RateDivider divider;
	fog::Network net;
	fog::Bloom bloomL, bloomR;
	fog::Mode mode = fog::HAAR;
	float hostRate = 48000.f;
	bool ready = false;
	double cutCoef[2][5] = {};
	double cutZ[2][2] = {};
	float mixSm = 0.f, mixStep = 1.f;
	bool mixSnap = true; // the next process() starts the ramp at its target
	float fade = 1.f, fadeStep = 0.f;
	int fadeState = 0; // 0 steady, 1 fading out, 2 fading in
};

} // namespace rka

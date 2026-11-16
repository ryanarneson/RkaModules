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
// - Level: the wet is level-matched to the dry on average (full Mix is about as
//   loud as dry for noise) by wetMakeup(). Loud resonant drones may reach the
//   limiter's knee (9 V) and are soft-limited to 10 V.
// - Mode: the voicing follows FogFrame::mode. A change fades the wet out
//   over 50 ms, clears and reconfigures the network, and fades it in.
// - Sleep: once the feed into the network and the wet out of it (after the
//   make-up, so QUIET is -114 dB at the output) have both stayed under QUIET
//   (1e-5 V) for the hold (the longer of 0.25 s and twice the longest ring in
//   the network, so no stale ring contents survive a sleep and the sparse
//   echoes of CLEAR at Size 1 do not fall asleep between hits), the engine
//   skips the RateDivider and the network (wet 0) until a feed sample reaches
//   QUIET. Buffers are not cleared: their contents are already below QUIET.
//   reset(), a new rate and a mode change wake it.
// - The wet make-up follows its target (refreshed every 32 samples) through a
//   30 ms one-pole, so a Visibility or Size change does not step the level.
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
	// The wet's make-up gain (a fraction): level-matches the wet to the dry
	// for noise at Mix 1 (the network settles a few dB under unity, more at
	// high Visibility). A smooth function of Visibility and Size, scaled by
	// the voicing's own factor; polynomial, so no libm.
	static float wetMakeup(float vis, float size, float voicingMakeup) {
		// Measured (noise, +/-5 V, noon knobs): the gain that levels the wet at
		// Mix 1 is 1.125 at Visibility 0 up to 2.13 at 1 (a quadratic), and
		// falls about 0.27 per unit of Size around noon (1.78 at Size 0, 1.35
		// at Size 1, at Visibility 0.5); at low Visibility the large-Size fall is
		// weaker (Visibility 0, Size 1 needs 0.99, not 0.87), so a small cubic
		// in (1 - Visibility) is added above noon Size.
		const float v = vis >= 0.f ? (vis <= 1.f ? vis : 1.f) : 0.f;
		const float w = 1.f - v;
		const float s = size >= 0.f ? (size <= 1.f ? size : 1.f) : 0.f;
		const float sizeTerm = 1.f + 0.27f * (0.5f - s) + 0.11f * w * w * w * (s > 0.5f ? 2.f * s - 1.f : 0.f);
		return voicingMakeup * (1.125f + v * (0.755f + 0.246f * v)) * sizeTerm;
	}

	// Below this (V) the feed and the wet count as silence: -114 dB re 5 V.
	static float quiet() { return 1e-5f; }

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
		makeupCoef = 1.f / (0.03f * hz + 1.f);
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
		wake();
	}

	// Awake, with the quiet count at 0 and the hold refreshed.
	void wake() {
		asleep_ = false;
		makeupSnap = true;
		quietCount = 0;
		updateHold();
	}

	bool asleep() const {
		return asleep_;
	}

	// Host-rate samples of silence before sleeping: the longer of 0.25 s and
	// twice the longest ring in the network and its diffuser, so that a quiet
	// network has overwritten every ring with sub-QUIET values (nothing stale
	// survives to be read back after a wake at another Size). Set when the
	// rate or the network changes, never per sample.
	void updateHold() {
		const float floorS = 0.25f * hostRate;
		const float ring = 2.f * (float) net.ringSamples() * (float) divider.factor();
		const float h = ring > floorS ? ring : floorS;
		hold = (int) (h < 1e9f ? h : 1e9f);
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
				wake();
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
		// The low cut's tail times the falling Bloom gain would otherwise
		// feed denormals into the network: 1e-20 is -400 dB.
		feedL = (feedL > 1e-20f || feedL < -1e-20f) ? feedL : 0.f;
		feedR = (feedR > 1e-20f || feedR < -1e-20f) ? feedR : 0.f;

		// The wet, at the inner rate; skipped while asleep.
		float wetL = 0.f, wetR = 0.f;
		const float Q = quiet();
		const bool feedQuiet = feedL < Q && feedL > -Q && feedR < Q && feedR > -Q;
		if (asleep_ && !feedQuiet) {
			// The controls were not applied while asleep: apply them now, with
			// Size snapped (no slew from the Size of the last awake sample).
			wake();
			net.setControls(vis, size, dampK, drift, f.driftRate ? 1 : 0);
			net.snapSize();
		}
		if (!asleep_) {
			const int rateSel = f.driftRate ? 1 : 0;
			divider.process(feedL, feedR, wetL, wetR, [&](float l, float r, float& ol, float& orr) {
				net.setControls(vis, size, dampK, drift, rateSel);
				net.process(l, r, ol, orr);
			});
			// The make-up target is refreshed at most once per 32 samples (as
			// the network's gains are), and the applied make-up follows it
			// through a one-pole with Size's 30 ms slew, so a Visibility or
			// Size step (or audio-rate CV) does not step the level.
			if (makeupSnap || ++makeupCount >= 32) {
				makeupCount = 0;
				makeupTarget = wetMakeup(vis, size, vo.wetMakeup);
				if (makeupSnap) {
					makeup = makeupTarget;
					makeupSnap = false;
				}
			}
			makeup += (makeupTarget - makeup) * makeupCoef;
			wetL = fogFinite(wetL) * makeup;
			wetR = fogFinite(wetR) * makeup;
			if (feedQuiet && wetL < Q && wetL > -Q && wetR < Q && wetR > -Q) {
				if (++quietCount >= hold)
					asleep_ = true;
			}
			else
				quietCount = 0;
			if (limiterEnabled) {
				wetL = limit(wetL);
				wetR = limit(wetR);
			}
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
	float makeup = 1.f, makeupTarget = 1.f, makeupCoef = 1.f; // the applied make-up and its target
	int makeupCount = 0;
	bool makeupSnap = true; // the next awake sample starts the make-up at its target
	bool asleep_ = false;
	int quietCount = 0;
	int hold = 12000;
};

} // namespace rka

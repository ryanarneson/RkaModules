#pragma once

// Rack-free per-sample logic of the Fog module: everything its process()
// does between reading the ports and writing them. The module fills a
// FogFrame from its params, ports and menu; the bench and tests fill one
// directly, so all of them run the same code.
//
//   IN L, IN R (R normals to L; a poly cable sums to mono)
//     |-- dry ------------------------------------------------+
//     +-- low cut -> tone -> Bloom -> RateDivider -> Network -> make-up -> leveller -> limiter x mode fade
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
//   +/-9 V, then a tanh knee that ends at +/-10 V. With the leveller before
//   it, it catches only transients (the spikes a bright drone's edges make).
// - Level: the wet's level follows the decay time, as a room's does: full Mix
//   is about as loud as dry for noise at noon Visibility (MIST, T60 5.66 s),
//   and 10 log10(T60 / 5.66 s) dB from there (-11.5 dB at Visibility 0, +5.8
//   at 0.75), whatever the Size and mode, by Network::makeup. At 0.75 and up the
//   leveller holds loud material down.
// - Leveller: a slow automatic gain on the wet, after the make-up and before
//   the limiter, one gain for L and R. A peak follower of the louder side
//   (5 ms attack, 300 ms release) feeds a slower follower (10 ms attack, 3 s
//   release); while that is over 6.5 V the gain is 6.5 V / it, else exactly 1.
//   Held tones build up in the network to 4-8 dB over noise, with peaks to
//   20-50 V; the leveller turns them down instead of the limiter clipping
//   them. Noise's peaks are lone samples, which barely move the 5 ms
//   follower: +/-5 V noise at the matched level (noon Visibility) sits at
//   the threshold, louder settings are held to about it (+/-5 V noise reads
//   about 0 dB at Visibility 0.75 and 1, not the law's +5.8 and +11.5), and
//   anything quiet passes at a gain of exactly 1. While the
//   feed is absent (its own 5 ms / 50 ms peak follower under 10 mV), the
//   slow follower does not release: the gain is held, so a tail decays as
//   it would without the leveller instead of being lifted back up as fast
//   as it falls. A new feed lets it release again at 3 s. Its state starts
//   clean on every wake() (so a held gain ends when the engine sleeps).
// - Mode: the voicing follows FogFrame::mode. The first process() takes its
//   frame's mode at once (a saved patch loads in its mode, no fade). After
//   that, a change fades the wet out
//   over 50 ms, clears and reconfigures the network, the tone stage and
//   Bloom, and fades it in over 50 ms; the new mode starts clean. A further
//   change while fading waits for the fade, then the latest mode is taken.
// - Tone: the voicing's EQ on the feed (FRET's low-mid peak, ICE's high
//   shelf; HAAR and SMOKE have none), an RBJ biquad worked out on a mode or
//   rate change, never per sample.
// - Sleep: once the feed into the network and the wet out of it (after the
//   make-up, so QUIET is -114 dB at the output) have both stayed under QUIET
//   (1e-5 V) for the hold (the longer of 0.25 s and twice the longest ring in
//   the network, so no stale ring contents survive a sleep and the sparse
//   echoes of CLEAR at Size 1 do not fall asleep between hits), the engine
//   skips the RateDivider and the network (wet 0) until a feed sample reaches
//   QUIET. Buffers are not cleared: their contents are already below QUIET.
//   reset(), a new rate and a mode change wake it. The sleep test reads the
//   wet before the leveller.
// - The wet make-up is the network's own (Network::makeup, worked out with
//   its gains from the same smoothed Visibility and slewing Size: Network.hpp,
//   "Wet level"), taken the sample it changes with no smoothing here, so the
//   make-up and the network's own level, sqrt(1 - g^2), move as one. Earlier
//   forms, worked out here from the knobs, failed: from the raw Visibility
//   through a 30 ms one-pole, the make-up (0.30 at Visibility 0, 8.0 at 1)
//   ramped linearly while the network's level followed the smoothed
//   Visibility, +10 dB in 5 ms on a 0 -> 1 jump; from the network's smoothed
//   Visibility through that one-pole, the make-up lagged the network by a
//   second pole and a 1 -> 0 jump swelled the wet 5-8 dB over its start for
//   50-70 ms (test_engine's visibility_no_swell).
// - Where the wet gain goes (splitGain): the total, sqrt(1 - g^2) times the
//   make-up, is split between the network's input and its output; in steady
//   state the split makes no difference (the network is linear). A fall
//   goes on the output, so the echoes in the network fall with the new sound,
//   as with the whole gain on the output; a rise goes on the input, so it
//   reaches only the sound that comes in after it, and the echoes already in
//   the network go on from their level at the new decay rate, as a room's
//   would. All on the output, a 0 -> 1 jump within 150 ms of a note's end
//   lifted those echoes by the law's 11 dB, 4-13 dB over their level before
//   the jump on noise (issue #22; test_engine's visibility_rise_in_tail). All
//   on the input, a 1 -> 0 jump on a held tone swelled the wet up to 11 dB
//   (FRET, Drift noon): a held tone's energy in the lines can be well over
//   what the taps show, and a new decay rate lets it out. The input's share
//   is held to feedMax() (18 dB) over the output's, so repeated rises cannot
//   run it up without bound; past that a rise goes on the output. A clear or
//   waking network starts with it all on the output. The network then holds
//   up to feedMax() x its usual level, which in float costs nothing.
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
	// The most of the wet gain the network's input may carry over its output
	// (splitGain): 18 dB, a full Visibility rise (11 dB) and a Size one.
	static float feedMax() { return 8.f; }

	// Below this (V) the feed and the wet count as silence: -114 dB re 5 V.
	static float quiet() { return 1e-5f; }

	// The leveller (see the top of the file): its target (V) on the slow
	// follower, and its four times (s).
	static float levelTarget() { return 6.5f; }
	static float levelAttack() { return 0.005f; }
	static float levelRelease() { return 0.3f; }
	static float levelSlowAttack() { return 0.01f; } // 50 ms until the T60 law made the onsets hotter
	static float levelSlowRelease() { return 3.f; }
	// Under this (V) on the feed's follower the input counts as absent and
	// the leveller's gain is held: -54 dB re 5 V. The follower has the 5 ms
	// attack and a 50 ms release: it bridges a 20 Hz drone's zero crossings,
	// and a stopped +/-5 V input counts as absent 0.3 s later.
	static float levelFeedAbsent() { return 0.01f; }
	static float levelFeedRelease() { return 0.05f; }

	// Test only: bypass the limiter, or the leveller.
	bool limiterEnabled = true;
	bool levellerEnabled = true;

	// Bypassed (Fog::processBypass): the port each output copies, every
	// channel as it is (0 OUT L, 1 OUT R). IN R normals to IN L, as in
	// process(), so a mono source on IN L alone reaches both outputs. Rack's
	// configBypass routes one input to one output and cannot express the
	// normal: with it, OUT R was silent on a mono source (issue #13).
	static const FogPort& bypassSource(const FogFrame& f, int side) {
		return side && f.inR.channels > 0 ? f.inR : f.inL;
	}

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
		levelCoef[0] = 1.0 / ((double) levelAttack() * hz + 1.0);
		levelCoef[1] = 1.0 / ((double) levelRelease() * hz + 1.0);
		levelCoef[2] = 1.0 / ((double) levelSlowAttack() * hz + 1.0);
		levelCoef[3] = 1.0 / ((double) levelSlowRelease() * hz + 1.0);
		levelCoef[4] = 1.0 / ((double) levelFeedRelease() * hz + 1.0);
		setLowCutCoeffs();
		reset();
	}

	// Silences everything and starts the current mode clean.
	void reset() {
		divider.reset();
		net.configure(fog::voicing(mode), divider.innerRate());
		bloomL.clear();
		bloomR.clear();
		setTone();
		clearLowCut();
		mixSnap = true;
		fade = 1.f;
		fadeState = 0;
		wake();
	}

	// Awake, with the quiet count at 0, the hold refreshed and the leveller
	// clear (gain 1).
	void wake() {
		asleep_ = false;
		makeupSnap = true;
		levelEnv = levelSlow = levelFeed = 0.0;
		levelGain = 1.f;
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
		// Mode: fade out, switch, fade in. The first frame's mode (a patch's
		// saved one) is taken at once: nothing has played, so nothing to fade.
		int want = f.mode;
		want = (want >= 0 && want < fog::MODES_LEN) ? want : (int) fog::HAAR;
		if (!started) {
			started = true;
			if (want != (int) mode) {
				mode = (fog::Mode) want;
				reset();
			}
		}
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
				setTone();
				fadeState = 2;
				// A switch made asleep stays asleep (the new network is clear,
				// so there is nothing to play): the next feed wakes it as it
				// would any sleeping engine, which restarts ICE's shifter as a
				// fresh one starts.
				const bool wasAsleep = asleep_;
				wake();
				asleep_ = wasAsleep;
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

		// The feed: low cut, tone, then Bloom.
		const int cut = (f.lowCut == 1 || f.lowCut == 2) ? f.lowCut : 0;
		float feedL = dryL, feedR = dryR;
		if (cut != lastCut) {
			// Back on from Off: the filter has not run since it was switched
			// off, and what it held then is not the input's now. Start it
			// empty, so its first output is the input (x b0, b0 just under 1)
			// and the lows then fall away. Between 50 and 100 Hz it has been
			// running: its state stays.
			if (lastCut == 0)
				clearLowCut();
			lastCut = cut;
		}
		if (cut) {
			feedL = highpass(0, cut, feedL);
			feedR = highpass(1, cut, feedR);
		}
		if (toneOn) {
			feedL = tone(0, feedL);
			feedR = tone(1, feedR);
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
			// Size snapped (no slew from the Size of the last awake sample),
			// and ICE's shifter restarted as a fresh network's.
			wake();
			net.setControls(vis, size, damp, drift, f.driftRate ? 1 : 0);
			net.snapSize();
			net.restartShifter();
		}
		if (!asleep_) {
			const int rateSel = f.driftRate ? 1 : 0;
			// The make-up (see the top) is the network's, read between its
			// control step and its audio, so the total wet gain (the network's
			// sqrt(1 - g^2) times the make-up) is one smooth value.
			divider.process(feedL, feedR, wetL, wetR, [&](float l, float r, float& ol, float& orr) {
				net.setControls(vis, size, damp, drift, rateSel);
				net.advance();
				const bool snap = makeupSnap;
				makeupSnap = false;
				makeup = net.makeup;
				splitGain(net.wetGain * makeup, snap);
				net.run(l * feedGain, r * feedGain, ol, orr);
			});
			wetL = fogFinite(wetL) * outGain;
			wetR = fogFinite(wetR) * outGain;
			if (feedQuiet && wetL < Q && wetL > -Q && wetR < Q && wetR > -Q) {
				if (++quietCount >= hold)
					asleep_ = true;
			}
			else
				quietCount = 0;
			if (levellerEnabled)
				level(wetL, wetR, feedL, feedR);
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

	// The total wet gain t (the network's sqrt(1 - g^2) times the make-up)
	// split between the network's input and its output (see the top). A fall
	// is taken on the output, so the echoes in the network fall with the new
	// sound; a rise on the input (feedGain x the network's own sqrt(1 - g^2)),
	// so they are not lifted, up to feedMax() over the output's share, and
	// past that on the output. snap: the whole of t on the output (a clear or
	// sleeping network, so nothing in it to lift). Always outGain x
	// feedGain x net.wetGain = t; a division only when t changes.
	void splitGain(float t, bool snap) {
		if (t == wetTotal && !snap)
			return;
		if (!(t > 0.f))
			return; // never (sqrt(1 - g^2) > 0); keeps the last split
		if (snap || !(wetTotal > 0.f))
			outGain = t;
		else if (t < wetTotal)
			outGain *= t / wetTotal;
		else if (t > outGain * feedMax())
			outGain = t / feedMax();
		wetTotal = t;
		feedGain = makeup / outGain;
	}

	// The leveller: both followers, then one gain on L and R; the feed's
	// follower holds the slow one's release while the input is absent. The
	// inputs are finite (fogFinite above, and the feed is sanitised where it
	// enters), so the followers are; they are flushed to 0 under 1e-20 V, so a
	// long fade cannot leave them denormal.
	void level(float& l, float& r, float feedL, float feedR) {
		const float fl = feedL >= 0.f ? feedL : -feedL, fr = feedR >= 0.f ? feedR : -feedR;
		const double fa = fl > fr ? fl : fr;
		levelFeed += (fa - levelFeed) * (fa > levelFeed ? levelCoef[0] : levelCoef[4]);
		levelFeed = levelFeed > 1e-20 ? levelFeed : 0.0;
		const bool held = !(levelFeed >= (double) levelFeedAbsent());
		const float al = l >= 0.f ? l : -l, ar = r >= 0.f ? r : -r;
		const double a = al > ar ? al : ar;
		levelEnv += (a - levelEnv) * (a > levelEnv ? levelCoef[0] : levelCoef[1]);
		if (levelEnv > levelSlow)
			levelSlow += (levelEnv - levelSlow) * levelCoef[2];
		else if (!held)
			levelSlow += (levelEnv - levelSlow) * levelCoef[3];
		levelEnv = levelEnv > 1e-20 ? levelEnv : 0.0;
		levelSlow = levelSlow > 1e-20 ? levelSlow : 0.0;
		levelGain = levelSlow > (double) levelTarget() ? (float) ((double) levelTarget() / levelSlow) : 1.f;
		l *= levelGain;
		r *= levelGain;
	}

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

	// The voicing's tone stage: transposed direct form II, in double as the
	// low cut is, with its state flushed below 1e-30. It runs at the host
	// rate, where a 300 Hz section's poles sit close to 1: in float it
	// amplified its own rounding to -86 dB under the signal at 192 kHz and
	// -69 dB at 768 kHz (test_engine's tone_stage_clean).
	float tone(int ch, float x) {
		const double in = (double) x;
		const double y = toneB[0] * in + toneZ[ch][0];
		const double z0 = toneB[1] * in - toneA[0] * y + toneZ[ch][1];
		const double z1 = toneB[2] * in - toneA[1] * y;
		toneZ[ch][0] = (z0 > 1e-30 || z0 < -1e-30) ? z0 : 0.0;
		toneZ[ch][1] = (z1 > 1e-30 || z1 < -1e-30) ? z1 : 0.0;
		return (float) y;
	}

	// RBJ cookbook peaking or high-shelf coefficients for the current mode
	// at the host rate, and a clear state. Not per sample (libm).
	void setTone() {
		const fog::Voicing& v = fog::voicing(mode);
		for (int c = 0; c < 2; c++)
			toneZ[c][0] = toneZ[c][1] = 0.0;
		toneOn = (v.toneKind == fog::TONE_PEAK || v.toneKind == fog::TONE_HIGH_SHELF) && v.toneDb != 0.f && v.toneHz > 0.f &&
		         v.toneQ > 0.f;
		if (!toneOn)
			return;
		const double f0 = v.toneHz < 0.4 * hostRate ? v.toneHz : 0.4 * hostRate;
		const double A = std::pow(10.0, v.toneDb / 40.0);
		const double w = 2.0 * 3.14159265358979323846 * f0 / hostRate;
		const double cw = std::cos(w), alpha = std::sin(w) / (2.0 * v.toneQ);
		double b0, b1, b2, a0, a1, a2;
		if (v.toneKind == fog::TONE_PEAK) {
			b0 = 1.0 + alpha * A;
			b1 = -2.0 * cw;
			b2 = 1.0 - alpha * A;
			a0 = 1.0 + alpha / A;
			a1 = -2.0 * cw;
			a2 = 1.0 - alpha / A;
		}
		else {
			const double sq = 2.0 * std::sqrt(A) * alpha;
			b0 = A * ((A + 1.0) + (A - 1.0) * cw + sq);
			b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
			b2 = A * ((A + 1.0) + (A - 1.0) * cw - sq);
			a0 = (A + 1.0) - (A - 1.0) * cw + sq;
			a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
			a2 = (A + 1.0) - (A - 1.0) * cw - sq;
		}
		toneB[0] = b0 / a0;
		toneB[1] = b1 / a0;
		toneB[2] = b2 / a0;
		toneA[0] = a1 / a0;
		toneA[1] = a2 / a0;
	}

	void clearLowCut() {
		for (int c = 0; c < 2; c++)
			cutZ[c][0] = cutZ[c][1] = 0.0;
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
	int lastCut = 0; // the low cut the last process() had (0 Off)
	bool toneOn = false;
	double toneB[3] = {1.0, 0.0, 0.0}, toneA[2] = {};
	double toneZ[2][2] = {};
	float mixSm = 0.f, mixStep = 1.f;
	bool mixSnap = true; // the next process() starts the ramp at its target
	float fade = 1.f, fadeStep = 0.f;
	int fadeState = 0; // 0 steady, 1 fading out, 2 fading in
	float makeup = 1.f; // the make-up in use (the network's)
	// The total wet gain (net.wetGain x makeup) and its split (splitGain):
	// outGain on the network's output, feedGain on its input.
	float wetTotal = 0.f, outGain = 1.f, feedGain = 1.f;
	// The leveller's followers and their coefficients are double: a float
	// one-pole stops once its step is under half an ulp of its value, and the
	// 3 s release's step is small enough for that with the follower still
	// 0.04 V over the level at 48 kHz and over 1 V at 768 kHz, the gain 1.3 dB
	// low for as long as the level held (issue #15; test_engine's
	// leveller_releases_at_high_rates).
	double levelCoef[5] = {1.0, 1.0, 1.0, 1.0, 1.0}; // attack, release, slow attack, slow release, feed release
	double levelEnv = 0.0, levelSlow = 0.0;     // the two followers (V)
	double levelFeed = 0.0;                     // the feed's follower (V): under 10 mV, the gain is held
	float levelGain = 1.f;                      // the gain applied to the last wet sample
	bool makeupSnap = true; // the next awake sample puts the whole wet gain on the output (splitGain)
	bool asleep_ = false;
	bool started = false; // process() has run: later mode changes fade
	int quietCount = 0;
	int hold = 12000;
};

} // namespace rka

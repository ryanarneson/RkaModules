#pragma once
// Narrows: a polyphonic stereo filter. Rack-free: a frame of knob positions
// and per-channel port voltages in, per-voice left and right out.
//
// Per voice and side, in order: the input (and the Ping pulse) -> the comb
// -> Drive -> up to twice the rate -> the core (Fathom's ladder or the
// state-variable core), its outputs mixed by Mode -> the ladder's soft
// limit -> down again -> make-up -> the output limit.
//
// Voices go through the cores four sides at a time (rka::Float4): pass p
// holds voices 2p and 2p + 1 as lanes (L, R, L, R). Only the passes that
// hold a voice are run. Lanes never mix, so a voice's output does not
// depend on its neighbours.
//
// Only setSampleRate() allocates (the combs' delay lines).
#include "../FastMath.hpp"
#include "../Float4.hpp"
#include "../fathom/Halfband.hpp"
#include "../fathom/LadderCore.hpp"
#include "Comb.hpp"
#include "Laws.hpp"
#include "NarrowsTuning.hpp"
#include "Ping.hpp"
#include "StateCore.hpp"

#include <cmath>
#include <cstdint>

namespace rka {

// One port: how many channels its cable carries (0: nothing patched) and
// their voltages.
struct NarrowsPort {
	int channels = 0;
	float v[16] = {};
};

struct NarrowsFrame {
	static const int MAX_VOICES = 16;
	// COMB is -1 .. 1; FM is -1 .. 1; CORE is 1 for the ladder (the switch
	// up), 0 for the state core; the rest are 0 .. 1.
	enum Param { CUTOFF, DRIVE, FM, RES, COMB, SPREAD, CORE, MODE, PARAMS_LEN };

	float params[PARAMS_LEN] = {};
	NarrowsPort inL, inR, voct, fm, res, comb, mode, ping;
	// Whether a cable is plugged into the R output: without one, L carries
	// the average of the two sides.
	bool outRConnected = false;
};

struct NarrowsResult {
	int channels = 1;
	float l[NarrowsFrame::MAX_VOICES] = {};
	float r[NarrowsFrame::MAX_VOICES] = {};
};

struct NarrowsEngine {
	typedef NarrowsFrame F;
	static const int MAX_VOICES = F::MAX_VOICES;
	static const int PASSES = MAX_VOICES / 2;

	NarrowsEngine() {
		setRates(48000.f);
		reset();
	}

	// Allocates the combs' lines: never from process().
	void setSampleRate(float fs) {
		if (!(fs > 0.f))
			return;
		setRates(fs);
		for (int i = 0; i < 2 * MAX_VOICES; i++)
			comb_[i].allocate(fs_, (uint32_t) i);
		reset();
	}

	// Every voice to rest. The next frame's knob positions are taken at
	// once, not slid to.
	void reset() {
		for (int p = 0; p < PASSES; p++) {
			ladder_[p].reset();
			state_[p].reset();
			up_[p].reset();
			down_[p].reset();
		}
		for (int i = 0; i < 2 * MAX_VOICES; i++)
			comb_[i].reset();
		for (int v = 0; v < MAX_VOICES; v++) {
			ping_[v].reset();
			noise_[v] = 0x9e3779b9u * (uint32_t) (v + 1);
			modeFresh_[v] = true;
		}
		voices_ = 0;
		fresh_ = true;
		ladderOn_ = stateOn_ = false;
		kick_ = 0;
	}

	int voices() const {
		return voices_;
	}

	void process(const F& in, NarrowsResult& out) {
		// --- voices ---
		int n = in.inL.channels;
		n = in.inR.channels > n ? in.inR.channels : n;
		n = in.voct.channels > n ? in.voct.channels : n;
		n = in.ping.channels > n ? in.ping.channels : n;
		n = n < 1 ? 1 : n > MAX_VOICES ? MAX_VOICES : n;
		for (int v = voices_; v < n; v++)
			restVoice(v);
		voices_ = n;
		const int passes = (n + 1) / 2;

		// --- knobs, smoothed ---
		const float coreWant = param(in, F::CORE) >= 0.5f ? 1.f : 0.f;
		if (fresh_) {
			cutoff_ = param(in, F::CUTOFF);
			drive_ = param(in, F::DRIVE);
			fm_ = param(in, F::FM);
			res_ = param(in, F::RES);
			combKnob_ = param(in, F::COMB);
			spread_ = param(in, F::SPREAD);
			fade_ = coreWant;
			mono_ = in.outRConnected ? 0.f : 1.f;
			lastDrive_ = lastSpread_ = -1.f;
			fresh_ = false;
		}
		else {
			slide(cutoff_, param(in, F::CUTOFF));
			slide(drive_, param(in, F::DRIVE));
			slide(fm_, param(in, F::FM));
			slide(res_, param(in, F::RES));
			slide(combKnob_, param(in, F::COMB));
			slide(spread_, param(in, F::SPREAD));
			slide(mono_, in.outRConnected ? 0.f : 1.f);
		}
		if (drive_ != lastDrive_) {
			lastDrive_ = drive_;
			driveGain_ = narrows::driveGain(drive_);
			makeup_ = fastExp2(-narrows::DRIVE_MAKEUP * narrows::DRIVE_OCTAVES * drive_);
		}
		if (spread_ != lastSpread_) {
			lastSpread_ = spread_;
			// Exactly 1 at Spread 0, so the two sides are the same to the bit.
			const float half = 0.5f * narrows::SPREAD_OCTAVES * spread_;
			spreadDown_ = spread_ > 0.f ? fastExp2(-half) : 1.f;
			spreadUp_ = spread_ > 0.f ? fastExp2(half) : 1.f;
		}

		// --- the core switch ---
		if (fade_ < coreWant)
			fade_ = fade_ + fadeStep_ < 1.f ? fade_ + fadeStep_ : 1.f;
		else if (fade_ > coreWant)
			fade_ = fade_ - fadeStep_ > 0.f ? fade_ - fadeStep_ : 0.f;
		const bool needLadder = fade_ > 0.f, needState = fade_ < 1.f;
		// A core coming in from idle starts from rest, and for the length of
		// the fade is fed the other core's output on top of its input, so a
		// self-oscillating voice is handed over and not left to grow again
		// from its noise. Both cores already running (the switch flipped
		// back mid-fade): no kick, so the two can never feed each other in
		// turn.
		if (needLadder && !ladderOn_) {
			for (int p = 0; p < PASSES; p++)
				ladder_[p].reset();
			kick_ = stateOn_ ? fadeLength_ : 0;
			kickLadder_ = true;
		}
		if (needState && !stateOn_) {
			for (int p = 0; p < PASSES; p++)
				state_[p].reset();
			kick_ = ladderOn_ ? fadeLength_ : 0;
			kickLadder_ = false;
		}
		ladderOn_ = needLadder;
		stateOn_ = needState;
		const bool kicking = kick_ > 0 && ladderOn_ && stateOn_;
		if (kick_ > 0)
			kick_--;

		// --- per voice: inputs, ping, comb, drive; the settings per lane ---
		float x[2 * MAX_VOICES], hz[2 * MAX_VOICES], k[MAX_VOICES], res[MAX_VOICES], mode[MAX_VOICES];
		float white[MAX_VOICES][2];
		const bool hasL = in.inL.channels > 0, hasR = in.inR.channels > 0;
		const float pingGain = narrows::PING_V / narrows::VOLTS;
		const float modeKnob = param(in, F::MODE);
		for (int v = 0; v < n; v++) {
			// Either input alone feeds both sides.
			float l = hasL ? audio(in.inL, v) : hasR ? audio(in.inR, v) : 0.f;
			float r = hasR ? audio(in.inR, v) : l;
			const float knock = pingGain * ping_[v].process(cv(in.ping, v));
			l = l * (1.f / narrows::VOLTS) + knock;
			r = r * (1.f / narrows::VOLTS) + knock;

			const float centre = narrows::hzFromOctaves(clampf(narrows::CUTOFF_OCTAVES * cutoff_ + cv(in.voct, v) + cv(in.fm, v) * fm_, -8.f, 16.f));
			const float hzL = narrows::clampHz(centre * spreadDown_, fs_), hzR = narrows::clampHz(centre * spreadUp_, fs_);
			hz[2 * v] = hzL;
			hz[2 * v + 1] = hzR;

			const float feedback = narrows::combFeedback(combKnob_ + cv(in.comb, v) * 0.2f);
			if (feedback != 0.f) {
				l = comb_[2 * v].process(l, fs_ / hzL, feedback);
				r = comb_[2 * v + 1].process(r, fs_ / hzR, feedback);
			}
			else {
				l = comb_[2 * v].process(l, 0.f, 0.f);
				r = comb_[2 * v + 1].process(r, 0.f, 0.f);
			}
			x[2 * v] = l * driveGain_;
			x[2 * v + 1] = r * driveGain_;

			res[v] = clampf(res_ + cv(in.res, v) * 0.1f, 0.f, 1.f);
			k[v] = narrows::ladderFeedback(res[v]);
			// Mode is smoothed per voice, knob and CV together, so a stepped
			// CV does not click.
			const float modeWant = clampf(modeKnob + cv(in.mode, v) * 0.1f, 0.f, 1.f);
			if (modeFresh_[v]) {
				mode_[v] = modeWant;
				modeFresh_[v] = false;
			}
			else
				slide(mode_[v], modeWant);
			mode[v] = mode_[v];
			// One noise stream per voice, shared by its two sides.
			white[v][0] = noise(v);
			white[v][1] = noise(v);
		}
		if (n & 1) {
			// The odd voice's partner lanes: silence through an open, quiet
			// filter, with its neighbour's noise so nothing goes subnormal.
			x[2 * n] = x[2 * n + 1] = 0.f;
			hz[2 * n] = hz[2 * n + 1] = hz[2 * n - 1];
			res[n] = k[n] = 0.f;
			mode[n] = 0.f;
			white[n][0] = white[n - 1][0];
			white[n][1] = white[n - 1][1];
		}

		// --- per pass: four sides through the core at twice the rate ---
		const float outGain = makeup_ * narrows::VOLTS;
		for (int p = 0; p < passes; p++) {
			const int a = 2 * p, b = a + 1;   // the two voices of this pass
			const Float4 in4 = {x[2 * a], x[2 * a + 1], x[2 * b], x[2 * b + 1]};
			const Float4 hz4 = {hz[2 * a], hz[2 * a + 1], hz[2 * b], hz[2 * b + 1]};
			const Float4 k4 = {k[a], k[a], k[b], k[b]};
			const Float4 res4 = {res[a], res[a], res[b], res[b]};
			const narrows::ModeWeights wa = narrows::modeWeights(mode[a]), wb = narrows::modeWeights(mode[b]);
			const Float4 lp4 = {wa.lp, wa.lp, wb.lp, wb.lp}, bp4 = {wa.bp, wa.bp, wb.bp, wb.bp}, hp4 = {wa.hp, wa.hp, wb.hp, wb.hp};
			const Float4 g = tan34(hz4 * gScale_);

			// The ladder's mix: the Xpander rows blended by Mode, on the
			// loop input and the four poles. LP24 (0,0,0,0,1), BP
			// (0,0,4,-8,4), HP (1,-4,6,-4,1).
			const Float4 w0 = hp4, w1 = -4.f * hp4, w2 = 4.f * bp4 + 6.f * hp4, w3 = -8.f * bp4 - 4.f * hp4,
			             w4 = lp4 + 4.f * bp4 + hp4;
			const Float4 comp = 1.f + k4 * lp4;
			// The ladder's pitch falls as its oscillation grows, by an amount
			// set by the feedback above 4 (and a little less at high
			// cutoffs): raise its cutoff by as much.
			const Float4 excess = vmax(k4 - narrows::RES_K_ONSET, splat4(0.f));
			const Float4 shift = (narrows::PITCH_FIX_A + narrows::PITCH_FIX_B * excess) * excess
			                     * vmax(1.f - narrows::PITCH_FIX_G * g, splat4(0.f));
			const Float4 gLadder = g * (1.f + shift * (1.f + 0.5f * shift));

			Float4 os[2];
			up_[p].process(in4, os);
			for (int s = 0; s < 2; s++) {
				const Float4 w = {white[a][s], white[a][s], white[b][s], white[b][s]};
				Float4 fromLadder = splat4(0.f), fromState = splat4(0.f);
				if (ladderOn_ && !(kicking && kickLadder_)) {
					const LadderNodes<Float4> nodes = ladder_[p].step(os[s], gLadder, k4, comp, w);
					fromLadder = ladderLimit(w0 * nodes.u + w1 * nodes.y0 + w2 * nodes.y1 + w3 * nodes.y2 + w4 * nodes.y3);
				}
				if (stateOn_) {
					const Float4 feed = kicking && !kickLadder_ ? os[s] + fromLadder : os[s];
					const narrows::StateOut<Float4> o = state_[p].step(feed, g, res4, w);
					fromState = ladderLimit(lp4 * o.lp + bp4 * o.bp + hp4 * o.hp);
				}
				if (ladderOn_ && kicking && kickLadder_) {
					const LadderNodes<Float4> nodes = ladder_[p].step(os[s] + fromState, gLadder, k4, comp, w);
					fromLadder = ladderLimit(w0 * nodes.u + w1 * nodes.y0 + w2 * nodes.y1 + w3 * nodes.y2 + w4 * nodes.y3);
				}
				os[s] = fromState + fade_ * (fromLadder - fromState);
			}
			const Float4 y = down_[p].process(os) * outGain;

			// A NaN that got in (it cannot from the ports) does not stay.
			if (!(y[0] + y[1] + y[2] + y[3] == y[0] + y[1] + y[2] + y[3])) {
				ladder_[p].reset();
				state_[p].reset();
				up_[p].reset();
				down_[p].reset();
			}
			give(out, a, y[0], y[1]);
			if (b < n)
				give(out, b, y[2], y[3]);
		}
		out.channels = n;
	}

private:
	void setRates(float fs) {
		fs_ = fs;
		gScale_ = float(M_PI) / (2.f * fs_);
		smooth_ = 1.f - std::exp(-1.f / (narrows::SMOOTH_S * fs_));
		const float fade = narrows::CORE_FADE_S * fs_;
		fadeLength_ = fade > 1.f ? (int) fade : 1;
		fadeStep_ = 1.f / (float) fadeLength_;
		for (int i = 0; i < 2 * MAX_VOICES; i++)
			comb_[i].setSampleRate(fs_);
		for (int v = 0; v < MAX_VOICES; v++)
			ping_[v].setSampleRate(fs_);
	}

	static float clampf(float x, float lo, float hi) {
		if (!(x > lo))
			return lo;   // NaN too
		return x < hi ? x : hi;
	}

	// A knob position: NaN and infinities read 0.
	static float param(const F& in, int i) {
		const float x = in.params[i];
		return x > -100.f && x < 100.f ? x : 0.f;
	}

	// Channel v of a cable: a one-channel cable feeds every voice; past a
	// polyphonic cable's last channel there is 0 V. NaN and infinities
	// read 0.
	static float cv(const NarrowsPort& p, int v) {
		const float x = p.channels == 1 ? p.v[0] : v < p.channels ? p.v[v] : 0.f;
		return x > -1e6f && x < 1e6f ? x : 0.f;
	}

	static float audio(const NarrowsPort& p, int v) {
		return clampf(cv(p, v), -narrows::INPUT_LIMIT_V, narrows::INPUT_LIMIT_V);
	}

	// Toward the target with a time constant of SMOOTH_S, landing exactly.
	void slide(float& value, float target) const {
		const float gap = target - value;
		if (gap > -1e-5f && gap < 1e-5f)
			value = target;
		else
			value += smooth_ * gap;
	}

	// Uniform in [-1, 1).
	float noise(int v) {
		noise_[v] = noise_[v] * 1664525u + 1013904223u;
		return (float) (int32_t) noise_[v] * (1.f / 2147483648.f);
	}

	static void zero(Float4& x, int lane) {
		x[lane] = 0.f;
		x[lane + 1] = 0.f;
	}

	// A voice that joins starts from rest: its two lanes of every state, its
	// combs (at no cost: see Comb::reset) and its ping.
	void restVoice(int v) {
		const int p = v / 2, lane = 2 * (v & 1);
		for (int i = 0; i < 4; i++) {
			zero(ladder_[p].s[i], lane);
			zero(ladder_[p].yPrev[i], lane);
		}
		zero(ladder_[p].uPrev, lane);
		zero(state_[p].s1, lane);
		zero(state_[p].s2, lane);
		zero(state_[p].bpPrev, lane);
		for (int i = 0; i < 3; i++) {
			zero(up_[p].stateA[i], lane);
			zero(up_[p].stateB[i], lane);
			zero(down_[p].stateA[i], lane);
			zero(down_[p].stateB[i], lane);
		}
		comb_[2 * v].reset();
		comb_[2 * v + 1].reset();
		ping_[v].reset();
		modeFresh_[v] = true;
	}

	// The output's soft limit: straight to the knee, never past the ceiling.
	static float limit(float x) {
		const float a = x < 0.f ? -x : x;
		if (a <= narrows::OUT_KNEE_V)
			return x;
		const float room = narrows::OUT_CEIL_V - narrows::OUT_KNEE_V;
		const float y = narrows::OUT_KNEE_V + room * fastTanh((a - narrows::OUT_KNEE_V) / room);
		return x < 0.f ? -y : y;
	}

	void give(NarrowsResult& out, int v, float l, float r) const {
		// With nothing in the R output, L carries the average of the sides.
		out.l[v] = limit(l + mono_ * (0.5f * (l + r) - l));
		out.r[v] = limit(r);
	}

	float fs_ = 48000.f, gScale_ = 0.f, smooth_ = 1.f, fadeStep_ = 1.f;
	int fadeLength_ = 1;

	LadderCore<Float4> ladder_[PASSES];
	narrows::StateCore<Float4> state_[PASSES];
	Upsampler2xT<Float4> up_[PASSES];
	Downsampler2xT<Float4> down_[PASSES];
	narrows::Comb comb_[2 * MAX_VOICES];
	narrows::PingPulse ping_[MAX_VOICES];
	uint32_t noise_[MAX_VOICES];

	int voices_ = 0;
	bool fresh_ = true;
	// The knobs, smoothed.
	float cutoff_ = 0.f, drive_ = 0.f, fm_ = 0.f, res_ = 0.f, combKnob_ = 0.f, spread_ = 0.f, mono_ = 0.f;
	// Mode per voice (knob and CV), smoothed; fresh: take the next value at once.
	float mode_[MAX_VOICES] = {};
	bool modeFresh_[MAX_VOICES] = {};
	float lastDrive_ = -1.f, driveGain_ = 1.f, makeup_ = 1.f;
	float lastSpread_ = -1.f, spreadDown_ = 1.f, spreadUp_ = 1.f;
	// The core switch: 1 is all ladder, 0 all state.
	float fade_ = 1.f;
	bool ladderOn_ = false, stateOn_ = false;
	// Samples of kick left, and which core is receiving it.
	int kick_ = 0;
	bool kickLadder_ = false;
};

} // namespace rka

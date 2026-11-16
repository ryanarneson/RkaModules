#pragma once

// Fog's feedback delay network (spec, "Controls and behaviour").
//
//   L, R -> 8 channels -> Diffuser<8> --(into each line)--+
//                                                         v
//   +-> line i (Hermite read at Size x base +/- Drift) -> x g_i -> damp_i -+
//   |                                                                      |
//   +--- write line i+1 <-- Householder mix (I - 2/N 11^T), then a shift <-+
//
//   wet L, R = two orthogonal +/-1 tap patterns over the line reads,
//              times a level from the T60, above 300 Hz; below it, one
//              more tap through a phase pair (the low band, below).
//
// - The low band. At one frequency the network is a fixed linear map, so a
//   held tone comes out of the two tap patterns with whatever phase they
//   happen to have there: it changes sign every few Hz (20-200 Hz: RMS
//   correlation 0.3-0.4 at noon Drift, 0.7 at Drift 0), and Drift's swing,
//   a phase of 2 pi f x depth, is too small at low f to average it out. So
//   a 33 Hz drone came out at a correlation of -0.9 in one mode and +0.5 in
//   another, with the sides up to 10 dB apart; the shorter the tail, the
//   more of the held sound is the first, unblurred pass, and the worse.
//   Below 300 Hz each side's taps are cut (a 4th-order Butterworth
//   high-pass) and replaced by one third tap pattern (Hadamard row 15),
//   low-passed by the matching Butterworth (the two bands' powers sum to 1,
//   which suits uncorrelated bands) and fed through a phase pair: two
//   chains of two first-order allpasses whose phases differ by 80 +/- 7.5
//   degrees from 8 to 600 Hz (poles 6.965 and 159.461 Hz, 30.966 and 711.28
//   Hz, fitted minimax). The bass is then as loud on both sides, at a
//   correlation of cos 80 = +0.17 (+0.05 to +0.30), and its mono sum loses
//   about 2 dB, whatever the tone, mode, Drift or tail (test_stereo). The
//   T60s are untouched: every tap pattern decays at the network's rate.
//   300 Hz, not lower: the high-pass's leak of the sides' own (random)
//   phase spoils the targets for an octave below the split (at 200 Hz,
//   105-195 Hz missed at 96 of 152 points at Drift 0), and the pair holds
//   80 degrees to 600 Hz. The price is one shared signal under 300 Hz: its
//   level from note to note is one tap sum's (at Drift 0 a 5.6 dB spread
//   over 20-100 Hz, against the two sides' summed 3.3 dB before). No fixed
//   output stage can do better and keep the sides level on every tone:
//   level on every input means one source, and one source's level is one
//   sum's (test_stereo's bass_evenness; the fix round 1 report).
//
// The modes are voicings (Voicing.hpp): line count and lengths, diffusion,
// how Damp maps, and two in-loop extras:
// - SMOKE: an allpass after each line's read (Schroeder, coefficient
//   loopDiffuse, 6-22 ms), inside the loop, so each repeat is blurred more
//   than the one before. It is lossless, and its mean delay (its own length)
//   is added to the line's length when the gain is worked out, so the T60
//   holds.
// - ICE: an octave-up shimmer, two parts, each an OctaveShifter
//   (Shifter.hpp: band-limited, two jittered-grain taps at twice speed).
//   - In the loop, a small cascade: the feedback is projected on a unit
//     vector u (a +/-1 Hadamard row over the lines, orthogonal to the
//     Householder's all-ones) as m, shifted to s, and the u component of
//     the line inputs becomes (1 - a) m + a s (shimmerCrossfade()).
//     The bound, and its limits: s is at each sample a convex combination
//     of two past band-limited m, and the band-limit has gain <= 1 at every
//     frequency (its low-pass also keeps the 2x read's fs/4 aliasing down),
//     so over a long stretch s's RMS is at most m's (the window weights
//     bound it in RMS, not per sample), and by Minkowski the RMS of
//     (1 - a) m + a s is at most m's, whatever the phase between the octave
//     and the note's harmonics. The rest of the vector is untouched, so the
//     energy a pass is at most the line gains' g^2. That is a bound per
//     pass, not per second: the octave's share takes a longer path, up to
//     62 ms more (grain plus jitter, about 31 ms on average), and only a^2 /
//     N of the energy a pass goes that way, so the pass is on average
//     longer by at most a^2 / N x 31 ms, under 0.5 ms (a <= 0.5, N = 16)
//     against passes of 16-500 ms; at long T60s, where a is small, far less.
//     An earlier a s - b m (b = 0.6 a^2) was level only for an octave
//     uncorrelated with the note; on tones it reached 1 - b + a > 1 (880 Hz
//     held a T60 of 123 s for 80).
//   - The amount: the crossfade takes (2 a - a^2) / N of the lows' energy a
//     pass beyond the line gains, so a is set to make that a fixed fraction
//     (shimmer) of the loop's own loss a pass, 1 - g^2: ICE's T60 is about
//     13 % short at every setting (modes_t60), and the line gains are never
//     raised to make it up (that would void the bound). a is capped at 0.5.
//     So the in-loop octave fades as the tail lengthens (about -15 dB under
//     the note at noon, -34 dB at DENSE).
//   - After the loop, a fixed octave: each wet side goes through its own
//     shifter, and shimmerOut times that is added to the wet. It is shifted
//     once and never fed back, so it follows the tail's envelope (no T60
//     law to break) and stays at one level against the note at every
//     Visibility. It runs at the inner rate, inside the divider: the
//     shifter is tuned for rates up to 96 kHz, so at 192 kHz and up it
//     costs half or less, and the divider's halfbands then clean its
//     aliasing as they do the wet's.
// - Damp: d acts as bias + (1 - bias)(1 - brightness) d: FRET's bias darkens
//   from the start, ICE's brightness halves every setting; each still
//   follows Damp's law from its own baseline.
//
// - Decay: line i's gain is g_i = 10^(-3 len_i / (T60 rate)), from its own
//   current length, so every loop round the network loses 60 dB per T60
//   whatever Size is. The mix is orthogonal (lossless), so the gains alone
//   set the decay, and each is below 1: the tail always dies.
// - The mix: Householder reflection, then each output moves one line on.
//   The shift is still orthogonal, and removes the reflection's strong
//   diagonal (1 - 2/N), which would otherwise let each line feed mostly
//   itself as a comb; with it, energy walks round all the lines.
// - Damp: a first-order high shelf in each line (bilinear, corner 2 kHz,
//   unity at DC, levelling off at a gain G above), with G solved so the
//   gain at 4 kHz makes the 4 kHz T60 the T60 times ratio(Damp), with
//   log2 ratio = -(d + 2 d^2): 1 at 0, 1/2 at noon, 1/8 fully right. The
//   shelf's small dip at 250 Hz is made up in the line's gain, so the
//   250 Hz T60 is Visibility's at any Damp and Size. (A one-pole low-pass
//   could not: a deep 4 kHz cut dragged its corner down onto 250 Hz.) A
//   cut deeper than the shelf's floor (G = 0: about -7 dB a pass at
//   4 kHz) is not reached; that happens only for short T60s with long
//   lines, where the highs already die within a few passes.
// - Drift: each line's read swings by a sine of its own rate (0.1-0.5 Hz,
//   spread log-evenly over the lines, short to long; 5x slower on Very
//   slow) and phase. Depth Drift^2 x 8 ms (Slow) or 24 ms (Very slow, so
//   the pitch still swings tens of cents), capped per line at 0.45 of its
//   shortest length (Size 0) so a read never leaves it. Zero depth is
//   exactly static. The depth follows its target through a 40 ms one-pole
//   (per sample while it moves, then not at all), so a Drift step or a rate
//   switch does not jump the reads (unsmoothed, Drift noon to 0 moved them by
//   up to 96 samples at once: a 2 V step on a 1 V sine). The rate is not
//   smoothed: the phase is continuous, so a new rate bends the read's path
//   but never moves it (test_engine's drift_step_no_click).
// - Size: scale = 3^(2 Size - 1) (1/3x-3x of the base lengths), slewed by
//   a one-pole of 30 ms time constant (about 100 ms to settle).
// - Wet level: steady-state power through the loop grows as 1 / (1 - g^2)
//   for the mean loop gain g, so the wet is scaled by sqrt(1 - g^2) and a
//   constant calibrated by measurement (test_network's even_wet_level).
// - Visibility reaches the gains through a 30 ms one-pole, a step each 32
//   samples (the gains' own refresh): the T60 is there within about 100 ms.
//   Applied at once, a Visibility jump stepped sqrt(1 - g^2) (0.985 at 0,
//   0.132 at 1: -17 dB in one sample) while the engine's make-up took 30 ms
//   to follow, and stepped every line's gain (test_engine's
//   visibility_step_no_click).
//
// Only allocate() allocates. process() calls no libm (fastSin2pi, the
// Hermite reads) and touches no global. A Drift change updates only the
// modulation. The gains and damping are recomputed when the (smoothed)
// Visibility, Damp or the (slewing) length changes, at most once per 32
// samples: a change inside that window is applied when it ends, so a
// control moved every sample (CV) costs one recompute per 32 samples.
#include "DelayLine.hpp"
#include "Diffuser.hpp"
#include "Shifter.hpp"
#include "FastMath.hpp"
#include "Voicing.hpp"
#include <cmath>

namespace rka {
namespace fog {

struct Network {
	static const int MAX_LINES = 16;
	static const int GAIN_EVERY = 32;

	static float maxScale() { return 3.f; }
	static float slowModMs() { return 8.f; }
	static float verySlowModMs() { return 24.f; }
	static float maxModMs() { return 24.f; } // the larger of the two
	static float dampHz() { return 4000.f; }
	static float shelfHz() { return 2000.f; }
	static float lowHz() { return 250.f; } // where Visibility's T60 holds
	static const int LOOP_AP = 8; // in-loop allpasses (SMOKE's 8 lines)
	static float shimmerMaxA() { return 0.5f; }
	// The low band: the crossover, and the phase pair's poles (Hz), two
	// first-order allpasses a branch (see the top).
	static float lowSplitHz() { return 300.f; }
	static float pairHz(int branch, int k) {
		return branch == 0 ? (k == 0 ? 6.965f : 159.461f) : (k == 0 ? 30.966f : 711.28f);
	}
	// The wet level's constant, set by measurement (test_network's
	// even_wet_level, pink noise at noon): at 1 the wet RMS is within
	// 0.7-1.4 dB below the input RMS across the six words (0.0-1.2 before
	// the low band), so it stays 1; the engine's wetMakeup levels it.
	static float wetCal() { return 1.f; }

	DelayLine line[MAX_LINES];
	Diffuser<8> diffuser;
	DelayLine loopAp[LOOP_AP]; // SMOKE's in-loop allpasses
	OctaveShifter shifter;     // ICE's in-loop shifter
	OctaveShifter octL, octR;  // ICE's feed-forward octave on the wet

	int n = MAX_LINES;
	float rate = 48000.f;
	float baseSamples[MAX_LINES] = {};
	float meanBase = 1.f;
	float len[MAX_LINES] = {};   // current nominal read length, samples
	float gain[MAX_LINES] = {};  // per-line T60 gain
	float shelfB0[MAX_LINES] = {}, shelfB1[MAX_LINES] = {}; // shelf numerator
	float shelfZ[MAX_LINES] = {};                           // shelf state
	float shelfA1 = 0.f;                                    // shelf pole, fixed
	float phase[MAX_LINES] = {};
	float phaseInc[MAX_LINES] = {};
	float modDepth[MAX_LINES] = {};       // Drift swing applied, samples
	float modDepthTarget[MAX_LINES] = {}; // and the one it follows (updateModulation)
	bool depthMoving = false;             // modDepth is still on its way
	float depthCoef = 1.f;                // the swing's 40 ms one-pole
	float tapL[MAX_LINES] = {}, tapR[MAX_LINES] = {}, inSign[MAX_LINES] = {};
	float tapLow[MAX_LINES] = {}; // the low band's one tap
	// The low band (see the top): the crossover's two Butterworth sections
	// (b0, b1, b2, a1, a2) for the high-pass on each side and the low-pass on
	// tapLow, their states, and the phase pair's first-order allpasses.
	// Double: in float, a 300 Hz section (and a 7 Hz allpass) at 48-96 kHz
	// amplifies its own rounding to about -76 dB under the signal.
	double hpC[2][5] = {}, lpC[2][5] = {};
	double hpZ[2][2][2] = {}, lpZ[2][2] = {}; // [side][section][state], [section][state]
	double pairC[2][2] = {};                  // [branch][section] coefficient
	double pairX[2][2] = {}, pairY[2][2] = {}; // last input and output
	float wetGain = 0.f;
	// Prewarped (bilinear) frequencies: tan(pi f / rate).
	float wDamp = 1.f, wShelf = 1.f, wLow = 0.f;
	float slewCoeff = 1.f; // Size one-pole
	float inGain = 1.f;

	// Controls.
	float vis = 0.5f, size = 0.5f, damp = 0.f, drift = 0.f;
	float visSm = 0.5f;   // the Visibility the gains use: follows vis (30 ms)
	float visCoef = 1.f;  // its one-pole, a step per GAIN_EVERY samples
	int driftRate = 0;
	float t60 = 1.f, ratio = 1.f, depth = 0.f;
	float scaleTarget = 1.f, scaleOffset = 0.f;
	int gainCount = 0;         // samples until the gains may be recomputed
	bool gainsPending = false; // a change waits for gainCount
	int ringMax = 0;
	bool fresh = true; // cleared and not yet given controls: Size snaps

	// The voicing's extras.
	float dampBias = 0.f, brightness = 0.f;
	int loopN = 0; // lines with an in-loop allpass
	float apK = 0.f;
	int apDelay[LOOP_AP] = {};
	float meanAp = 0.f; // mean allpass delay over the n lines, samples
	float shimAmt = 0.f; // the voicing's shimmer
	float shimA = 0.f;   // the crossfade, from it and the loop gain (updateGains)
	float shimU[MAX_LINES] = {};
	float shimOut = 0.f; // the feed-forward octave's level

	// Buffers for every mode's longest line at the largest Size, plus the
	// Drift swing. Never call from process().
	void allocate(float innerRate) {
		const float r = innerRate >= 1.f ? innerRate : 1.f;
		float longest = 1.f;
		for (int m = 0; m < MODES_LEN; m++) {
			const Voicing& v = voicing((Mode) m);
			for (int i = 0; i < v.lines && i < MAX_LINES; i++)
				longest = v.baseMs[i] > longest ? v.baseMs[i] : longest;
		}
		const int maxD = (int) ((longest * maxScale() + maxModMs()) * 0.001f * r) + 8;
		for (int i = 0; i < MAX_LINES; i++)
			line[i].allocate(maxD, (uint32_t) (Diffuser<8>::MAX_STAGES * 8 + i));
		diffuser.allocate(r, 1.f, 0); // stagger 0..31; the network's lines take 32..47
		float apLongest = 1.f;
		for (int m = 0; m < MODES_LEN; m++)
			for (int i = 0; i < LOOP_AP; i++) {
				const float ms = voicing((Mode) m).loopMs[i];
				apLongest = ms > apLongest ? ms : apLongest;
			}
		const int apD = (int) (apLongest * 0.001f * r) + 4;
		for (int i = 0; i < LOOP_AP; i++)
			loopAp[i].allocate(apD, (uint32_t) (48 + i)); // stagger 48..55
		const int gD = OctaveShifter::bufferSamples(r);
		shifter.allocate(r, 56u); // stagger 56
		octL.allocate(r, 57u);    // 57, 58
		octR.allocate(r, 58u);
		// The buffers in series (line, allpass, in-loop shifter, then the
		// feed-forward shifter): the longest path a sample can ring round
		// before it is overwritten.
		int ring = maxD + apD + gD + gD;
		const int dd = (int) (Diffuser<8>::MAX_STAGE_MS * 0.001f * r) + 4;
		ring = dd > ring ? dd : ring;
		ringMax = ring;
	}

	void configure(const Voicing& v, float innerRate) {
		rate = innerRate >= 1.f ? innerRate : 1.f;
		n = v.lines >= 16 ? 16 : 8;
		float sum = 0.f;
		for (int i = 0; i < n; i++) {
			baseSamples[i] = v.baseMs[i] * 0.001f * rate;
			sum += baseSamples[i];
		}
		meanBase = sum / (float) n;
		diffuser.configure(rate, v.diffuseMs, v.diffuseStages, 1.f, 0x5eedu);
		// The filter frequencies, kept below Nyquist at low rates.
		wDamp = prewarp(dampHz());
		wShelf = prewarp(shelfHz());
		wLow = prewarp(lowHz());
		shelfA1 = (wShelf - 1.f) / (wShelf + 1.f);
		slewCoeff = 1.f / (0.03f * rate + 1.f);
		depthCoef = 1.f / (0.04f * rate + 1.f);
		// Visibility's 30 ms one-pole, taken one step a GAIN_EVERY samples.
		visCoef = (float) GAIN_EVERY / (0.03f * rate + (float) GAIN_EVERY);
		// Taps: rows 5 and 10 of the Sylvester Hadamard matrix (orthogonal
		// +/-1 patterns), and the input signs row 3, over n lines.
		for (int i = 0; i < MAX_LINES; i++) {
			tapL[i] = (popcount(i & 5) & 1) ? -1.f : 1.f;
			tapR[i] = (popcount(i & 10) & 1) ? -1.f : 1.f;
			inSign[i] = (popcount(i & 3) & 1) ? -1.f : 1.f;
			// The low band's tap: row 15 (row 7 over 8 lines), orthogonal to
			// the other three.
			tapLow[i] = (popcount(i & 15) & 1) ? -1.f : 1.f;
		}
		setLowBand();
		// The diffuser's 8 channels carry 4 (L^2 + R^2); spread over n lines.
		inGain = n == 16 ? 0.25f : 0.35355339f;
		// The voicing's extras.
		dampBias = unit(v.dampBias);
		brightness = unit(v.brightness);
		loopN = v.loopDiffuse > 0.f ? (n < LOOP_AP ? n : LOOP_AP) : 0;
		apK = v.loopDiffuse > 0.f ? (v.loopDiffuse < 0.9f ? v.loopDiffuse : 0.9f) : 0.f;
		float apSum = 0.f;
		for (int i = 0; i < LOOP_AP; i++) {
			int d = (int) (v.loopMs[i] * 0.001f * rate);
			const int hi = loopAp[i].maxDelay() - 2;
			d = d < 1 ? 1 : d > hi ? hi : d;
			apDelay[i] = d;
			if (i < loopN)
				apSum += (float) d;
		}
		meanAp = apSum / (float) n;
		shimAmt = v.shimmer > 0.f ? (v.shimmer < 1.f ? v.shimmer : 1.f) : 0.f;
		const float invSqrtN = n == 16 ? 0.25f : 0.35355339f;
		for (int i = 0; i < MAX_LINES; i++)
			shimU[i] = ((popcount(i & 6) & 1) ? -invSqrtN : invSqrtN);
		shimOut = v.shimmerOut > 0.f ? (v.shimmerOut < 1.f ? v.shimmerOut : 1.f) : 0.f;
		// Each shifter's own jitter sequence, so the two sides' octaves differ.
		shifter.configure(rate, 1u);
		octL.configure(rate, 2u);
		octR.configure(rate, 3u);
		setTargets();
		updateModulation();
		clear();
	}

	// The longest ring any line of the network or its diffuser can read back,
	// in samples at the inner rate (set by allocate()). The engine's sleep
	// hold is twice this, so that a quiet network has overwritten every ring.
	int ringSamples() const {
		return ringMax;
	}

	// Apply the current Size at once: no slew from an older Size, gains now.
	// For the engine's wake, after setControls() on a stale network.
	void snapSize() {
		scaleOffset = 0.f;
		gainCount = 0;
		gainsPending = false;
		updateLengths();
		updateGains();
		snapSmoothing();
	}

	// The smoothed controls (Drift's swing, the wet level) jump to their
	// targets: after a clear, a fresh network's first controls, or a wake.
	void snapSmoothing() {
		for (int i = 0; i < MAX_LINES; i++)
			modDepth[i] = modDepthTarget[i];
		depthMoving = false;
		if (visSm != vis) {
			visSm = vis;
			setTargets();
			updateGains();
		}
	}

	// Restart ICE's shifters (in the loop and on the wet) as a fresh network
	// starts them: grain phase 0, their filters empty; and empty the low
	// band's filters (their states can sit above the wet they feed: the
	// high-pass holds what it cuts). For the engine's wake, when everything it
	// holds is below QUIET anyway, so that waking is the same as starting fresh.
	void restartShifter() {
		clearLowBand();
		shifter.restart();
		octL.restart();
		octR.restart();
	}

	// Silences the network. The next setControls() sets Size at once, with
	// no slew, since there is no tail to swoop.
	void clear() {
		for (int i = 0; i < MAX_LINES; i++) {
			line[i].clear();
			shelfZ[i] = 0.f;
			// Decorrelated starting phases (golden-ratio steps).
			const float p = 0.61803398875f * (float) i;
			phase[i] = p - std::floor(p);
		}
		diffuser.clear();
		for (int i = 0; i < LOOP_AP; i++)
			loopAp[i].clear();
		shifter.clear();
		octL.clear();
		octR.clear();
		clearLowBand();
		scaleOffset = 0.f;
		fresh = true;
		gainCount = 0;
		gainsPending = false;
		updateLengths();
		updateGains();
		snapSmoothing();
	}

	// All in [0, 1] (NaN reads as 0); driftRate 0 = Slow, 1 = Very slow.
	void setControls(float visibility, float sz, float dmp, float drf, int rateSel) {
		visibility = unit(visibility);
		sz = unit(sz);
		dmp = unit(dmp);
		drf = unit(drf);
		rateSel = rateSel ? 1 : 0;
		const bool snap = fresh;
		fresh = false;
		if (visibility == vis && sz == size && dmp == damp && drf == drift && rateSel == driftRate)
			return;
		const bool sizeMoved = sz != size;
		const bool decayMoved = visibility != vis || dmp != damp;
		const bool driftMoved = drf != drift || rateSel != driftRate;
		vis = visibility;
		size = sz;
		damp = dmp;
		drift = drf;
		driftRate = rateSel;
		const float oldTarget = scaleTarget;
		setTargets();
		if (driftMoved)
			updateModulation();
		if (sizeMoved) {
			// Slew from where the scale is now (target + offset stays put),
			// unless just cleared.
			if (!snap)
				scaleOffset += oldTarget - scaleTarget;
			updateLengths();
		}
		if (sizeMoved || decayMoved)
			requestGains();
		if (snap)
			snapSmoothing();
	}

	void process(float inL, float inR, float& wetL, float& wetR) {
		inL = (inL > -1e9f && inL < 1e9f) ? inL : 0.f;
		inR = (inR > -1e9f && inR < 1e9f) ? inR : 0.f;

		if (scaleOffset != 0.f) {
			scaleOffset *= 1.f - slewCoeff;
			if (scaleOffset < 1e-6f && scaleOffset > -1e-6f)
				scaleOffset = 0.f;
			updateLengths();
			gainsPending = true;
		}
		if (gainCount > 0 && --gainCount == 0 && (gainsPending || visSm != vis)) {
			stepVisibility();
			requestGains();
		}

		// Drift's swing follows its target through a 40 ms one-pole (see the
		// top), and stops once there.
		if (depthMoving) {
			bool moving = false;
			for (int i = 0; i < n; i++) {
				const float d = modDepthTarget[i] - modDepth[i];
				if (d > 1e-3f || d < -1e-3f) {
					modDepth[i] += d * depthCoef;
					moving = true;
				}
				else
					modDepth[i] = modDepthTarget[i];
			}
			depthMoving = moving;
		}

		float r[MAX_LINES];
		if (depth > 0.f || depthMoving) {
			for (int i = 0; i < n; i++) {
				r[i] = line[i].read(len[i] + modDepth[i] * fastSin2pi(phase[i]));
				phase[i] += phaseInc[i];
				phase[i] = phase[i] >= 1.f ? phase[i] - 1.f : phase[i];
			}
		}
		else {
			for (int i = 0; i < n; i++)
				r[i] = line[i].read(len[i]);
		}
		// SMOKE: the allpass after each read, w = x + k w[-M], y = w[-M] - k w.
		for (int i = 0; i < loopN; i++) {
			const float d = loopAp[i].readInt(apDelay[i] - 1);
			float w = r[i] + apK * d;
			w = (w > 1e-20f || w < -1e-20f) ? w : 0.f;
			loopAp[i].write(w);
			r[i] = d - apK * w;
		}

		float l = 0.f, rr = 0.f, lo = 0.f;
		for (int i = 0; i < n; i++) {
			l += tapL[i] * r[i];
			rr += tapR[i] * r[i];
			lo += tapLow[i] * r[i];
		}
		// The low band: each side's taps high-passed, plus one low-passed tap
		// through the phase pair, a branch a side.
		const double hl = biquad(hpC[1], hpZ[0][1], biquad(hpC[0], hpZ[0][0], l));
		const double hr = biquad(hpC[1], hpZ[1][1], biquad(hpC[0], hpZ[1][0], rr));
		const double low = biquad(lpC[1], lpZ[1], biquad(lpC[0], lpZ[0], lo));
		double la = low, lb = low;
		for (int k = 0; k < 2; k++) {
			la = allpass(0, k, la);
			lb = allpass(1, k, lb);
		}
		wetL = (float) (hl + la) * wetGain;
		wetR = (float) (hr + lb) * wetGain;
		if (shimOut > 0.f) {
			// ICE: the feed-forward octave, shifted once, never fed back.
			wetL += shimOut * octL.process(wetL);
			wetR += shimOut * octR.process(wetR);
		}

		float x[8];
		for (int k = 0; k < 8; k++)
			x[k] = (k & 1) ? inR : inL;
		diffuser.process(x);

		float v[MAX_LINES];
		float sum = 0.f;
		for (int i = 0; i < n; i++) {
			// The shelf, transposed direct form II: one state.
			const float u = gain[i] * r[i];
			const float y = shelfB0[i] * u + shelfZ[i];
			float z = shelfB1[i] * u - shelfA1 * y;
			z = (z > 1e-20f || z < -1e-20f) ? z : 0.f; // denormals and NaN to 0
			shelfZ[i] = z;
			v[i] = y;
			sum += y;
		}
		const float h = sum * (2.f / (float) n);
		float fb[MAX_LINES];
		for (int j = 0; j < n; j++)
			fb[j] = v[j == 0 ? n - 1 : j - 1] - h;
		if (shimA > 0.f) {
			// ICE: the octave-up shifter on the feedback's u component.
			float m = 0.f;
			for (int j = 0; j < n; j++)
				m += shimU[j] * fb[j];
			const float sh = shifter.process(m);
			// The crossfade: u component (1 - a) m + a s.
			const float c = shimmerCrossfade(m, sh, shimA) - m;
			for (int j = 0; j < n; j++)
				fb[j] += c * shimU[j];
		}
		for (int j = 0; j < n; j++) {
			float w = fb[j] + inGain * inSign[j] * x[j & 7];
			w = (w > 1e-20f || w < -1e-20f) ? w : 0.f;
			line[j].write(w);
		}
	}

	// Line i's Drift swing, in samples either side of its length.
	float lineDepth(int i) const { return modDepth[i]; }

	// ---- the low band, per sample ----

	// Transposed direct form II; state flushed below 1e-20 (denormals, NaN).
	static double biquad(const double* c, double* z, double x) {
		const double y = c[0] * x + z[0];
		const double z0 = c[1] * x - c[3] * y + z[1];
		const double z1 = c[2] * x - c[4] * y;
		z[0] = (z0 > 1e-20 || z0 < -1e-20) ? z0 : 0.0;
		z[1] = (z1 > 1e-20 || z1 < -1e-20) ? z1 : 0.0;
		return y;
	}

	// First-order allpass y = c x + x[-1] - c y[-1].
	double allpass(int b, int k, double x) {
		double y = pairC[b][k] * (x - pairY[b][k]) + pairX[b][k];
		y = (y > 1e-20 || y < -1e-20) ? y : 0.0;
		pairX[b][k] = x;
		pairY[b][k] = y;
		return y;
	}

	void clearLowBand() {
		for (int s = 0; s < 2; s++)
			for (int k = 0; k < 2; k++) {
				hpZ[s][k][0] = hpZ[s][k][1] = 0.0;
				lpZ[k][0] = lpZ[k][1] = 0.0;
				pairX[s][k] = pairY[s][k] = 0.0;
			}
	}

	// ---- control maths (not per sample) ----

	// The crossover (4th-order Butterworth, two sections of Q 0.541 and
	// 1.307, so the two bands' powers add to 1) and the phase pair, at the
	// current rate; their states cleared.
	void setLowBand() {
		const double K = prewarpD(lowSplitHz());
		const double Q[2] = {0.54119610014619698, 1.3065629648763766};
		for (int k = 0; k < 2; k++) {
			const double nrm = 1.0 / (1.0 + K / Q[k] + K * K);
			const double a1 = 2.0 * (K * K - 1.0) * nrm, a2 = (1.0 - K / Q[k] + K * K) * nrm;
			hpC[k][0] = nrm;
			hpC[k][1] = -2.0 * nrm;
			hpC[k][2] = nrm;
			lpC[k][0] = K * K * nrm;
			lpC[k][1] = 2.0 * K * K * nrm;
			lpC[k][2] = K * K * nrm;
			hpC[k][3] = lpC[k][3] = a1;
			hpC[k][4] = lpC[k][4] = a2;
		}
		for (int b = 0; b < 2; b++)
			for (int k = 0; k < 2; k++) {
				const double t = prewarpD(pairHz(b, k));
				pairC[b][k] = (t - 1.0) / (t + 1.0);
			}
		clearLowBand();
	}

	static int popcount(int x) {
		int c = 0;
		for (; x; x >>= 1)
			c += x & 1;
		return c;
	}

	static float unit(float x) {
		return x >= 0.f ? (x <= 1.f ? x : 1.f) : 0.f;
	}

	double prewarpD(double hz) const {
		const double f = hz < 0.4 * rate ? hz : 0.4 * rate;
		return std::tan(3.14159265358979323846 * f / rate);
	}

	float prewarp(float hz) const {
		const float f = hz < 0.4f * rate ? hz : 0.4f * rate;
		return std::tan(3.14159265358979f * f / rate);
	}

	void setTargets() {
		t60 = fogT60(visSm);
		const float d = dampBias + (1.f - dampBias) * (1.f - brightness) * damp;
		ratio = fastExp2(-(d + 2.f * d * d));
		scaleTarget = fastExp2((2.f * size - 1.f) * 1.58496250072f); // log2(3)
	}

	// Drift only: the swing and the rates.
	void updateModulation() {
		const bool verySlow = driftRate != 0;
		const float ms = verySlow ? verySlowModMs() : slowModMs();
		depth = drift * drift * ms * 0.001f * rate;
		const float slow = verySlow ? 0.2f : 1.f;
		const float minScale = 1.f / maxScale();
		for (int i = 0; i < MAX_LINES; i++) {
			// 0.1 ... 0.5 Hz, log-evenly from the shortest line to the
			// longest, which has the most room to swing.
			const float k = n > 1 ? (float) (i < n ? i : n - 1) / (float) (n - 1) : 0.f;
			const float hz = 0.1f * fastExp2(2.32192809489f * k); // log2(5)
			phaseInc[i] = hz * slow / rate;
			const float room = 0.45f * baseSamples[i] * minScale;
			modDepthTarget[i] = depth < room ? depth : room;
			depthMoving = depthMoving || modDepthTarget[i] != modDepth[i];
		}
	}

	// One step of the Visibility the gains use towards vis (the one-pole);
	// on it once within 1e-4.
	void stepVisibility() {
		if (visSm == vis)
			return;
		const float d = vis - visSm;
		visSm = (d > 1e-4f || d < -1e-4f) ? visSm + d * visCoef : vis;
		setTargets();
	}

	// Recompute the gains now, or when the 32-sample window ends.
	void requestGains() {
		if (gainCount > 0) {
			gainsPending = true;
			return;
		}
		updateGains();
		gainsPending = false;
		gainCount = GAIN_EVERY;
	}

	void updateLengths() {
		const float s = scaleTarget + scaleOffset;
		for (int i = 0; i < n; i++)
			len[i] = baseSamples[i] * s;
	}

	// The per-line gain and damping from the current lengths, and the wet
	// level from the mean.
	void updateGains() {
		// log2 of 10^(-3 L / (T60 rate)) per sample of L.
		const float perSample = -3.f * 3.32192809489f / (t60 * rate);
		const float hfExtra = 1.f / ratio - 1.f;
		const float wd2 = wDamp * wDamp, ws2 = wShelf * wShelf, wl2 = wLow * wLow;
		for (int i = 0; i < n; i++) {
			// The allpass's mean delay is its length: part of the loop.
			const float e = perSample * (len[i] + (i < loopN ? (float) apDelay[i] : 0.f));
			const float g = fastExp2(e);
			// The shelf H(s) = (G s + ws) / (s + ws), |H|^2 = (G^2 w^2 +
			// ws^2) / (w^2 + ws^2): G from the wanted 4 kHz gain m.
			const float m = fastExp2(e * hfExtra);
			float G2 = (m * m * (wd2 + ws2) - ws2) / wd2;
			G2 = G2 > 0.f ? (G2 < 1.f ? G2 : 1.f) : 0.f;
			const float G = std::sqrt(G2);
			shelfB0[i] = (G + wShelf) / (1.f + wShelf);
			shelfB1[i] = (wShelf - G) / (1.f + wShelf);
			// Make up the shelf's dip at 250 Hz, so the 250 Hz loop gain is g.
			// It stays below 1: the dip is a small fraction of g's own loss,
			// and the cap halves the distance to 1 whatever happens.
			const float h250 = std::sqrt((G2 * wl2 + ws2) / (wl2 + ws2));
			const float gc = g / h250;
			const float cap = 0.5f * (1.f + g);
			gain[i] = gc < cap ? gc : cap;
		}
		const float gm = fastExp2(perSample * (meanBase * (scaleTarget + scaleOffset) + meanAp));
		const float p = 1.f - gm * gm;
		wetGain = wetCal() * std::sqrt(p > 0.f ? p : 0.f);
		// ICE: the crossfade from the loss a pass (see the top): (2 a - a^2) / N
		// = shimmer (1 - g^2), so a = 1 - sqrt(1 - shimmer N (1 - g^2)).
		const float q = shimAmt * (float) n * (p > 0.f ? p : 0.f);
		const float aMax = shimmerMaxA();
		const float qMax = 2.f * aMax - aMax * aMax;
		shimA = q > 0.f ? (q < qMax ? 1.f - std::sqrt(1.f - q) : aMax) : 0.f;
	}
};

} // namespace fog
} // namespace rka

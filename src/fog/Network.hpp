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
//              times a level from the T60.
//
// The modes are voicings (Voicing.hpp): line count and lengths, diffusion,
// how Damp maps, and two in-loop extras:
// - SMOKE: an allpass after each line's read (Schroeder, coefficient
//   loopDiffuse, 6-22 ms), inside the loop, so each repeat is blurred more
//   than the one before. It is lossless, and its mean delay (its own length)
//   is added to the line's length when the gain is worked out, so the T60
//   holds.
// - ICE: a +12-semitone granular shifter in the feedback.
//   - The shifter: the loop's feedback is projected on a unit vector u (a
//     +/-1 Hadamard row over the lines, orthogonal to the Householder's
//     all-ones) as m, band-passed (two one-pole high-passes at 80 Hz, two
//     one-pole low-passes at 5 kHz: -16 dB at a quarter of the rate, so
//     little of the 2x read's aliasing recirculates), written to a buffer,
//     and read back at twice speed by two taps half a 50 ms grain apart,
//     with triangular windows that sum to 1 (integer reads: no
//     interpolation). Each grain starts at a random extra delay of 0-12 ms
//     (an LCG per network), drawn when its window is 0. With a fixed grain
//     the two taps sat a fixed 25 ms apart, so the octave's carrier
//     cancelled at 40n + 20 Hz (220, 260, 300 Hz...) and left a pair of
//     sidebands beating at 40 Hz; the jitter breaks the fixed phase between
//     grains, so the octave is a slightly smeared octave at every pitch.
//   - The crossfade: the u component of the line inputs becomes
//     (1 - a) m + a s. The bound, whatever the phase between the octave and
//     the note's own harmonics (a held tone makes them coherent): s is, at
//     each sample, a convex combination of past band-passed m, and the
//     band-pass has gain <= 1 at every frequency, so over any stretch the
//     RMS of s is at most that of m, and by Minkowski the RMS of (1 - a) m +
//     a s is at most (1 - a) + a = 1 times m's. The u component never
//     gains, the rest of the vector is untouched, so the loop's energy a
//     pass is at most the line gains' g^2: no tail outlasts the T60 law. An
//     earlier a s - b m (b = 0.6 a^2) kept the energy level only for an
//     octave uncorrelated with the note; on tones it reached 1 - b + a > 1
//     and an 880 Hz tail held a T60 of 123 s for 80.
//   - The band-pass is not needed for that bound (it holds with gain 1 at
//     DC too); the high-pass keeps the bass out of the octave (a voicing
//     choice, test_modes' ice_bass_kept_out), the low-pass keeps it glassy
//     and the aliasing down.
//   - The amount: the crossfade takes 1 - (1 - a)^2 of the u component's
//     energy from the note a pass (and gives a^2 of it to the octave), so
//     the lows lose (2 a - a^2) / N of their energy a pass more than the
//     line gains say. a is set so that this is a fixed fraction (shimmer)
//     of the loop's own loss a pass, 1 - g^2 (g the mean loop gain): then
//     ICE's T60 is the same fraction short at every Visibility and Size
//     (about 13 % for shimmer 0.16, checked by modes_t60), and the line gains
//     are never raised to make it up (that would void the bound). a is
//     capped at 0.5. The cost: the octave's level against the note follows
//     the loss a pass, so it is stronger for short tails and large Size and
//     fainter for long tails (see Voicing.hpp).
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
//   exactly static.
// - Size: scale = 3^(2 Size - 1) (1/3x-3x of the base lengths), slewed by
//   a one-pole of 30 ms time constant (about 100 ms to settle).
// - Wet level: steady-state power through the loop grows as 1 / (1 - g^2)
//   for the mean loop gain g, so the wet is scaled by sqrt(1 - g^2) and a
//   constant calibrated by measurement (test_network's even_wet_level).
//
// Only allocate() allocates. process() calls no libm (fastSin2pi, the
// Hermite reads) and touches no global. A Drift change updates only the
// modulation. The gains and damping are recomputed when Visibility, Damp or
// the (slewing) length changes, at most once per 32 samples: a change
// inside that window is applied when it ends, so a control moved every
// sample (CV) costs one recompute per 32 samples.
#include "DelayLine.hpp"
#include "Diffuser.hpp"
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
	static float shimmerGrainMs() { return 50.f; }
	static float shimmerLpHz() { return 5000.f; }
	static float shimmerHpHz() { return 80.f; }
	static float shimmerJitterMs() { return 12.f; }
	static float shimmerMaxA() { return 0.5f; }
	// The wet level's constant, set by measurement (test_network's
	// even_wet_level, pink noise at noon): at 1 the wet RMS is within
	// 0.1-1.3 dB below the input RMS across the six words, so it stays 1.
	static float wetCal() { return 1.f; }

	DelayLine line[MAX_LINES];
	Diffuser<8> diffuser;
	DelayLine loopAp[LOOP_AP]; // SMOKE's in-loop allpasses
	DelayLine shiftBuf;        // ICE's shifter

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
	float modDepth[MAX_LINES] = {}; // Drift swing, samples
	float tapL[MAX_LINES] = {}, tapR[MAX_LINES] = {}, inSign[MAX_LINES] = {};
	float wetGain = 0.f;
	// Prewarped (bilinear) frequencies: tan(pi f / rate).
	float wDamp = 1.f, wShelf = 1.f, wLow = 0.f;
	float slewCoeff = 1.f; // Size one-pole
	float inGain = 1.f;

	// Controls.
	float vis = 0.5f, size = 0.5f, damp = 0.f, drift = 0.f;
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
	float shimLp1 = 0.f, shimLp2 = 0.f, shimLpCoef = 0.f; // the low-pass's two one-pole states
	float shimHp1 = 0.f, shimHp2 = 0.f, shimHpCoef = 0.f; // the high-pass's two one-pole states
	int grain = 4, grainK = 0;
	float invGrain = 0.25f;
	int jitterMax = 0;          // samples
	int jitterA = 0, jitterB = 0; // each tap's extra delay this grain
	uint32_t jitterSeed = 1u;

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
		const int gD = (int) ((shimmerGrainMs() + shimmerJitterMs()) * 0.001f * r) + 4;
		shiftBuf.allocate(gD, 56u); // stagger 56
		// The loop's buffers in series (line, allpass, shifter): the longest
		// path a sample can ring round before it is overwritten.
		int ring = maxD + apD + gD;
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
		// Taps: rows 5 and 10 of the Sylvester Hadamard matrix (orthogonal
		// +/-1 patterns), and the input signs row 3, over n lines.
		for (int i = 0; i < MAX_LINES; i++) {
			tapL[i] = (popcount(i & 5) & 1) ? -1.f : 1.f;
			tapR[i] = (popcount(i & 10) & 1) ? -1.f : 1.f;
			inSign[i] = (popcount(i & 3) & 1) ? -1.f : 1.f;
		}
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
		shimHpCoef = 1.f - std::exp(-2.f * 3.14159265358979f * shimmerHpHz() / rate);
		shimLpCoef = 1.f - std::exp(-2.f * 3.14159265358979f * (shimmerLpHz() < 0.4f * rate ? shimmerLpHz() : 0.4f * rate) / rate);
		{
			jitterMax = (int) (shimmerJitterMs() * 0.001f * rate);
			int g = (int) (shimmerGrainMs() * 0.001f * rate);
			const int hi = shiftBuf.maxDelay() - 2 - jitterMax;
			g = g > hi ? hi : g;
			g &= ~1;
			grain = g < 4 ? 4 : g;
			invGrain = 1.f / (float) grain;
		}
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
	}

	// Restart ICE's shifter as a fresh network starts it: grain phase 0, its
	// filters empty. For the engine's wake, when everything it holds is below
	// QUIET anyway, so that waking is the same as starting fresh.
	void restartShifter() {
		grainK = 0;
		shimLp1 = shimLp2 = shimHp1 = shimHp2 = 0.f;
		jitterSeed = 1u;
		jitterA = jitterB = 0;
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
		shiftBuf.clear();
		restartShifter();
		scaleOffset = 0.f;
		fresh = true;
		gainCount = 0;
		gainsPending = false;
		updateLengths();
		updateGains();
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
		if (gainCount > 0 && --gainCount == 0 && gainsPending)
			requestGains();

		float r[MAX_LINES];
		if (depth > 0.f) {
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

		float l = 0.f, rr = 0.f;
		for (int i = 0; i < n; i++) {
			l += tapL[i] * r[i];
			rr += tapR[i] * r[i];
		}
		wetL = l * wetGain;
		wetR = rr * wetGain;

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
			// Band-limit: two one-pole high-passes (80 Hz), then two one-pole
			// low-passes (5 kHz).
			shimHp1 += shimHpCoef * (m - shimHp1);
			shimHp1 = (shimHp1 > 1e-20f || shimHp1 < -1e-20f) ? shimHp1 : 0.f;
			const float hp1 = m - shimHp1;
			shimHp2 += shimHpCoef * (hp1 - shimHp2);
			shimHp2 = (shimHp2 > 1e-20f || shimHp2 < -1e-20f) ? shimHp2 : 0.f;
			shimLp1 += shimLpCoef * ((hp1 - shimHp2) - shimLp1);
			shimLp1 = (shimLp1 > 1e-20f || shimLp1 < -1e-20f) ? shimLp1 : 0.f;
			shimLp2 += shimLpCoef * (shimLp1 - shimLp2);
			shimLp2 = (shimLp2 > 1e-20f || shimLp2 < -1e-20f) ? shimLp2 : 0.f;
			shiftBuf.write(shimLp2);
			const int k = grainK, half = grain >> 1;
			// A tap's window is 0 where its grain starts: draw its jitter there.
			if (k == 0 || k == half) {
				jitterSeed = jitterSeed * 1664525u + 1013904223u;
				const int j = (int) ((uint64_t) (jitterSeed >> 8) * (uint32_t) (jitterMax + 1) >> 24);
				if (k == 0)
					jitterA = j;
				else
					jitterB = j;
			}
			const int k2 = k + half < grain ? k + half : k + half - grain;
			const float w1 = k < half ? 2.f * (float) k * invGrain : 2.f - 2.f * (float) k * invGrain;
			const float sh = w1 * shiftBuf.readInt(grain - k + jitterA) + (1.f - w1) * shiftBuf.readInt(grain - k2 + jitterB);
			grainK = k + 1 < grain ? k + 1 : 0;
			// The crossfade: u component (1 - a) m + a s.
			const float c = shimA * (sh - m);
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

	// ---- control maths (not per sample) ----

	static int popcount(int x) {
		int c = 0;
		for (; x; x >>= 1)
			c += x & 1;
		return c;
	}

	static float unit(float x) {
		return x >= 0.f ? (x <= 1.f ? x : 1.f) : 0.f;
	}

	float prewarp(float hz) const {
		const float f = hz < 0.4f * rate ? hz : 0.4f * rate;
		return std::tan(3.14159265358979f * f / rate);
	}

	void setTargets() {
		t60 = fogT60(vis);
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
			modDepth[i] = depth < room ? depth : room;
		}
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

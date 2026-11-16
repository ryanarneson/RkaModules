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
	// The wet level's constant, set by measurement (test_network's
	// even_wet_level, pink noise at noon): at 1 the wet RMS is within
	// 0.1-1.3 dB below the input RMS across the six words, so it stays 1.
	static float wetCal() { return 1.f; }

	DelayLine line[MAX_LINES];
	Diffuser<8> diffuser;

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
	bool fresh = true; // cleared and not yet given controls: Size snaps

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
		setTargets();
		updateModulation();
		clear();
	}

	// The longest current line length, in samples at the inner rate (control
	// rate: the engine asks every few dozen samples for its sleep hold).
	float longestLen() const {
		float m = 0.f;
		for (int i = 0; i < n; i++)
			m = len[i] > m ? len[i] : m;
		return m;
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
		for (int j = 0; j < n; j++) {
			const int i = j == 0 ? n - 1 : j - 1;
			float w = v[i] - h + inGain * inSign[j] * x[j & 7];
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
		ratio = fastExp2(-(damp + 2.f * damp * damp));
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
			const float e = perSample * len[i];
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
		const float gm = fastExp2(perSample * meanBase * (scaleTarget + scaleOffset));
		const float p = 1.f - gm * gm;
		wetGain = wetCal() * std::sqrt(p > 0.f ? p : 0.f);
	}
};

} // namespace fog
} // namespace rka

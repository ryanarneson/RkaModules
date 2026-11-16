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
// - Damp: a one-pole low-pass in each line, unity at DC, whose gain at
//   4 kHz is set so the 4 kHz T60 is the T60 times ratio(Damp), with
//   log2 ratio = -(d + 2 d^2): 1 at 0, 1/2 at noon, 1/8 fully right.
// - Drift: each line's read swings by a sine of its own rate (0.1-0.5 Hz,
//   spread log-evenly; 5x slower on Very slow) and phase, depth Drift^2 x
//   8 ms; zero depth is exactly static.
// - Size: scale = 3^(2 Size - 1) (1/3x-3x of the base lengths), slewed by
//   a one-pole of 30 ms time constant (about 100 ms to settle).
// - Wet level: steady-state power through the loop grows as 1 / (1 - g^2)
//   for the mean loop gain g, so the wet is scaled by sqrt(1 - g^2) and a
//   constant calibrated by measurement (test_network's even_wet_level).
//
// Only allocate() allocates. process() calls no libm (fastSin2pi, fastExp2)
// and touches no global. Gains are recomputed only when a control changes
// and, while Size slews, every 32 samples.
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
	static float maxModMs() { return 8.f; }
	static float dampHz() { return 4000.f; }
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
	float dampA[MAX_LINES] = {}; // one-pole coefficient
	float dampZ[MAX_LINES] = {}; // one-pole state
	float phase[MAX_LINES] = {};
	float phaseInc[MAX_LINES] = {};
	float tapL[MAX_LINES] = {}, tapR[MAX_LINES] = {}, inSign[MAX_LINES] = {};
	float wetGain = 0.f;
	float cosW = 1.f;      // cos of the damping frequency
	float slewCoeff = 1.f; // Size one-pole
	float inGain = 1.f;

	// Controls.
	float vis = 0.5f, size = 0.5f, damp = 0.f, drift = 0.f;
	int driftRate = 0;
	float t60 = 1.f, ratio = 1.f, depth = 0.f;
	float scaleTarget = 1.f, scaleOffset = 0.f;
	int gainCount = 0;
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
			line[i].allocate(maxD);
		diffuser.allocate(r, 1.f);
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
		// The damping frequency, kept below Nyquist at low rates.
		float f = dampHz();
		if (f > 0.4f * rate)
			f = 0.4f * rate;
		cosW = std::cos(6.28318530717959f * f / rate);
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
		setControlsNow();
		clear();
	}

	// Silences the network. The next setControls() sets Size at once, with
	// no slew, since there is no tail to swoop.
	void clear() {
		for (int i = 0; i < MAX_LINES; i++) {
			line[i].clear();
			dampZ[i] = 0.f;
			// Decorrelated starting phases (golden-ratio steps).
			const float p = 0.61803398875f * (float) i;
			phase[i] = p - std::floor(p);
		}
		diffuser.clear();
		scaleOffset = 0.f;
		fresh = true;
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
		vis = visibility;
		size = sz;
		damp = dmp;
		drift = drf;
		driftRate = rateSel;
		const float oldTarget = scaleTarget;
		setControlsNow();
		if (sizeMoved && !snap) {
			// Slew from where the scale is now: target + offset stays put.
			scaleOffset += oldTarget - scaleTarget;
			gainCount = 0;
		}
		updateLengths();
		updateGains();
	}

	void process(float inL, float inR, float& wetL, float& wetR) {
		inL = (inL > -1e9f && inL < 1e9f) ? inL : 0.f;
		inR = (inR > -1e9f && inR < 1e9f) ? inR : 0.f;

		if (scaleOffset != 0.f) {
			scaleOffset *= 1.f - slewCoeff;
			if (scaleOffset < 1e-6f && scaleOffset > -1e-6f)
				scaleOffset = 0.f;
			updateLengths();
			if (scaleOffset == 0.f || --gainCount <= 0) {
				updateGains();
				gainCount = GAIN_EVERY;
			}
		}

		float r[MAX_LINES];
		if (depth > 0.f) {
			for (int i = 0; i < n; i++) {
				r[i] = line[i].read(len[i] + depth * fastSin2pi(phase[i]));
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
			const float a = dampA[i];
			float z = (1.f - a) * (gain[i] * r[i]) + a * dampZ[i];
			z = (z > 1e-20f || z < -1e-20f) ? z : 0.f; // denormals and NaN to 0
			dampZ[i] = z;
			v[i] = z;
			sum += z;
		}
		const float h = sum * (2.f / (float) n);
		for (int j = 0; j < n; j++) {
			const int i = j == 0 ? n - 1 : j - 1;
			float w = v[i] - h + inGain * inSign[j] * x[j & 7];
			w = (w > 1e-20f || w < -1e-20f) ? w : 0.f;
			line[j].write(w);
		}
	}

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

	void setControlsNow() {
		t60 = fogT60(vis);
		ratio = fastExp2(-(damp + 2.f * damp * damp));
		scaleTarget = fastExp2((2.f * size - 1.f) * 1.58496250072f); // log2(3)
		depth = drift * drift * maxModMs() * 0.001f * rate;
		const float slow = driftRate ? 0.2f : 1.f;
		for (int i = 0; i < MAX_LINES; i++) {
			// 0.1 ... 0.5 Hz, log-evenly over the lines, shuffled so neighbours
			// differ.
			const int k = (i * 7) & 15;
			const float hz = 0.1f * fastExp2(2.32192809489f * (float) k / 15.f); // log2(5)
			phaseInc[i] = hz * slow / rate;
		}
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
		for (int i = 0; i < n; i++) {
			const float e = perSample * len[i];
			gain[i] = fastExp2(e);
			dampA[i] = onePoleFor(fastExp2(e * hfExtra));
		}
		const float gm = fastExp2(perSample * meanBase * (scaleTarget + scaleOffset));
		const float p = 1.f - gm * gm;
		wetGain = wetCal() * std::sqrt(p > 0.f ? p : 0.f);
	}

	// The coefficient a of (1 - a) / (1 - a z^-1) whose magnitude at the
	// damping frequency is m (unity at DC). From
	// (1 - a)^2 = m^2 (1 - 2 a c + a^2); capped so the pole stays clear of 1.
	float onePoleFor(float m) const {
		const float m2 = m * m;
		if (!(m2 < 0.9999999f))
			return 0.f;
		const float c = cosW;
		const float A = 1.f - m2 * c;
		float disc = m2 * (1.f - c) * (2.f - m2 - m2 * c);
		disc = disc > 0.f ? disc : 0.f;
		float a = (A - std::sqrt(disc)) / (1.f - m2);
		a = a > 0.f ? a : 0.f;
		return a < 0.995f ? a : 0.995f;
	}
};

} // namespace fog
} // namespace rka

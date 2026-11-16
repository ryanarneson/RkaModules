// Fog's stereo image on held low tones (the low band, Network.hpp). A held
// low tone used to come out of the network with whatever L/R phase the two
// tap patterns happened to have at its frequency: a 33 Hz sine at -0.89 in
// FRET, mono sums losing up to 12 dB, sides up to 10 dB apart. The targets
// are ValhallaRoom LV-426's on the same input (the user's testbed, 2026-11-16):
// with input present, L/R correlation -0.2 to +0.5 and a mono sum (L + R) / 2
// that loses at most 4 dB against the mean of the sides' powers; in the tail
// (0.5-3 s after the input stops) still decorrelated, -0.3 to +0.4; and on a
// mono input the sides within 1.5 dB. Every mode, Visibility 0.3 and 0.52, at
// 33 Hz, 65 Hz and on broadband noise, a stereo input of two decorrelated
// sources, the testbed's Fathom drone, and (so the fix is not tuned to two
// frequencies) a sweep of 20-450 Hz (1.5 x the 300 Hz split) at Drift noon
// and at Drift 0, with the bass's note-to-note level evenness over 20-100 Hz.
// Measured here in double precision with libm, never from the engine. Built
// with the plugin's floating-point flags (tests.mk). Run with `make test`.
#include "FogEngine.hpp"
#include "fathom/FathomEngine.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using rka::FogEngine;
using rka::FogFrame;
using rka::FogResult;

const float RATE = 48000.f;
const double PI = 3.14159265358979323846;
const char* MODE_NAMES[4] = {"HAAR", "FRET", "SMOKE", "ICE"};

// The sources: a mono sine (IN R normalled), mono noise, and two stereo
// pairs of decorrelated sources (independent noises; a sine on L and the
// same sine 90 degrees later on R, as a stereo drone's fundamental).
// FATHOM: the user's testbed drone (fog-testbed/base.json), stereo.
enum Source { SINE, NOISE, NOISE2, QUAD, FATHOM };

struct Stats {
	double corrS, corrT, monoLoss, balance;
	double level; // the sides' mean power while sustained, dB re 1 V^2
};

struct Lcg {
	uint32_t s;
	float next() { // [-1, 1)
		s = s * 1664525u + 1013904223u;
		return (float) (s >> 8) / 8388608.f - 1.f;
	}
};

double corr(double ab, double aa, double bb) {
	return aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0.0;
}

// The source gated on for gateS, then off for 3 s, into Fog at Mix 1, low
// cut off, every other knob at noon (Drift as given), 3 V peak. Sustain: the
// last 3 s of the gate (or of the gate after 3 s, if shorter); tail: 0.5-3 s
// after it. The means are ~0 (no DC in the sources), so the sums are
// Pearson's.
// tailS 0 skips the tail (corrT reads 0).
Stats run(int mode, float vis, Source src, double hz, float drift = 0.5f, double gateS = 10.0, double tailS = 3.0) {
	FogEngine* e = new FogEngine;
	e->setSampleRate(RATE);
	FogFrame f;
	for (int p = 0; p < FogFrame::PARAMS_LEN; p++)
		f.params[p] = p >= FogFrame::SIZE_ATT ? 0.f : 0.5f;
	f.params[FogFrame::MIX] = 1.f;
	f.params[FogFrame::VISIBILITY] = vis;
	f.params[FogFrame::DRIFT] = drift;
	float inL = 0.f, inR = 0.f, zero[16] = {};
	f.inL.v = &inL;
	f.inL.channels = 1;
	f.inR.v = &inR;
	const bool stereo = src == NOISE2 || src == QUAD || src == FATHOM;
	f.inR.channels = stereo ? 1 : 0;
	for (int i = 0; i < 5; i++) {
		f.cv[i].v = zero;
		f.cv[i].channels = 0;
	}
	f.mode = mode;
	f.driftRate = 0;
	f.lowCut = 0;
	f.dryKill = false;
	Lcg a{12345u}, b{67891u};
	// The testbed's Fathom (base.json): PITCH 0 (sub at 32.7 Hz), SUB 0.5,
	// DETUNE 0.25, SPREAD 0.6, DRIFT 0.25, CUTOFF 5.907, RES 0.2, DRIVE
	// 0.25, both outputs patched; it runs through the gate, as the testbed's
	// VCA does.
	rka::FathomEngine* fa = src == FATHOM ? new rka::FathomEngine : nullptr;
	rka::FathomFrame ff;
	const float fp[rka::FathomFrame::PARAMS_LEN] = {0.f, 0.5f, 0.25f, 0.6f, 0.25f, 5.90689f, 0.f, 0.f, 0.f, 0.2f, 0.25f, 0.f, 0.f};
	for (int i = 0; i < rka::FathomFrame::PARAMS_LEN; i++)
		ff.params[i] = fp[i];
	ff.outputConnected[rka::FathomFrame::LEFT] = ff.outputConnected[rka::FathomFrame::RIGHT] = true;
	const long gate = (long) (gateS * RATE), end = gate + (long) (tailS * RATE);
	const long s0 = gate - (long) (3 * RATE) > (long) (3 * RATE) ? gate - (long) (3 * RATE) : (long) (3 * RATE);
	const long t0 = gate + (long) (0.5 * RATE);
	double sLL = 0, sRR = 0, sLR = 0, tLL = 0, tRR = 0, tLR = 0;
	FogResult r;
	for (long t = 0; t < end; t++) {
		float l = 0.f, rr = 0.f;
		float fo[3] = {};
		if (fa)
			fa->process(ff, RATE, fo);
		if (t < gate) {
			const double ph = 2.0 * PI * std::fmod((double) t * hz / RATE, 1.0);
			switch (src) {
				case SINE: l = (float) (3.0 * std::sin(ph)); break;
				case NOISE: l = 3.f * a.next(); break;
				case NOISE2:
					l = 3.f * a.next();
					rr = 3.f * b.next();
					break;
				case QUAD:
					l = (float) (3.0 * std::sin(ph));
					rr = (float) (3.0 * std::cos(ph));
					break;
				case FATHOM:
					l = fo[0];
					rr = fo[1];
					break;
			}
		}
		inL = l;
		inR = rr;
		e->process(f, r);
		const double L = r.outL, R = r.outR;
		if (t >= s0 && t < gate) {
			sLL += L * L;
			sRR += R * R;
			sLR += L * R;
		}
		if (t >= t0) {
			tLL += L * L;
			tRR += R * R;
			tLR += L * R;
		}
	}
	delete e;
	delete fa;
	Stats st;
	st.corrS = corr(sLR, sLL, sRR);
	st.corrT = corr(tLR, tLL, tRR);
	// The mono sum's power against the mean of the two sides' powers.
	const double side = 0.5 * (sLL + sRR), mono = 0.25 * (sLL + sRR + 2.0 * sLR);
	st.monoLoss = mono > 0 ? 10.0 * std::log10(side / mono) : 999.0;
	st.balance = sLL > 0 && sRR > 0 ? 10.0 * std::log10(sRR / sLL) : 999.0;
	st.level = side > 0 ? 10.0 * std::log10(side / (double) (gate - s0)) : -999.0;
	return st;
}

bool inside(double x, double lo, double hi) {
	return x >= lo && x <= hi;
}

// Every mode, Visibility 0.3 and 0.52, a mono 33 Hz sine, 65 Hz sine and
// noise: the targets, sustain and tail.
void monoTargets() {
	struct In {
		const char* name;
		Source src;
		double hz;
	};
	const In ins[] = {{"33 Hz sine", SINE, 33.0}, {"65 Hz sine", SINE, 65.0}, {"noise", NOISE, 0.0}};
	for (int m = 0; m < 4; m++)
		for (float vis : {0.3f, 0.52f})
			for (const In& in : ins) {
				const Stats s = run(m, vis, in.src, in.hz);
				CHECK(inside(s.corrS, -0.2, 0.5), "%s v %g %s: sustain correlation %+.2f, want -0.2 to +0.5", MODE_NAMES[m], vis, in.name, s.corrS);
				CHECK(s.monoLoss <= 4.0, "%s v %g %s: the mono sum loses %.1f dB, want <= 4", MODE_NAMES[m], vis, in.name, s.monoLoss);
				CHECK(inside(s.corrT, -0.3, 0.4), "%s v %g %s: tail correlation %+.2f, want -0.3 to +0.4", MODE_NAMES[m], vis, in.name, s.corrT);
				CHECK(std::fabs(s.balance) <= 1.5, "%s v %g %s: R %+.1f dB re L, want within 1.5", MODE_NAMES[m], vis, in.name, s.balance);
				std::printf("  mono_targets %-5s v %.2f %-10s: sustain %+.2f, tail %+.2f, mono sum -%.1f dB, R %+.1f dB re L\n", MODE_NAMES[m], vis, in.name,
				            s.corrS, s.corrT, s.monoLoss, s.balance);
			}
}

// A stereo input of two decorrelated sources does not go anti-phase either:
// independent noises, and a 33 Hz sine with its quadrature on R.
void stereoInputs() {
	for (int m = 0; m < 4; m++)
		for (float vis : {0.3f, 0.52f})
			for (Source src : {NOISE2, QUAD}) {
				const char* name = src == NOISE2 ? "two noises" : "33 Hz quad";
				const Stats s = run(m, vis, src, 33.0);
				CHECK(inside(s.corrS, -0.2, 0.5), "%s v %g %s: sustain correlation %+.2f, want -0.2 to +0.5", MODE_NAMES[m], vis, name, s.corrS);
				CHECK(s.monoLoss <= 4.0, "%s v %g %s: the mono sum loses %.1f dB, want <= 4", MODE_NAMES[m], vis, name, s.monoLoss);
				CHECK(inside(s.corrT, -0.3, 0.4), "%s v %g %s: tail correlation %+.2f, want -0.3 to +0.4", MODE_NAMES[m], vis, name, s.corrT);
				std::printf("  stereo_inputs %-5s v %.2f %-10s: sustain %+.2f, tail %+.2f, mono sum -%.1f dB, R %+.1f dB re L\n", MODE_NAMES[m], vis, name,
				            s.corrS, s.corrT, s.monoLoss, s.balance);
			}
}

// The testbed's own source, the Fathom drone (sub at 32.7 Hz, spread 0.6),
// gated 10.13 s as there, at its two tails (Visibility 0.317 = 2.15 s and
// 0.521 = 6.32 s): in every mode the sustain and tail targets. (The review
// measured -0.63 to -0.86 and 6.3-11.7 dB of mono loss here at da9cccc.)
void fathomDrone() {
	for (int m = 0; m < 4; m++)
		for (float vis : {0.317f, 0.521f}) {
			const Stats s = run(m, vis, FATHOM, 0.0, 0.5f, 10.13);
			CHECK(inside(s.corrS, -0.2, 0.5), "%s v %g Fathom drone: sustain correlation %+.2f, want -0.2 to +0.5", MODE_NAMES[m], vis, s.corrS);
			CHECK(s.monoLoss <= 4.0, "%s v %g Fathom drone: the mono sum loses %.1f dB, want <= 4", MODE_NAMES[m], vis, s.monoLoss);
			CHECK(inside(s.corrT, -0.3, 0.4), "%s v %g Fathom drone: tail correlation %+.2f, want -0.3 to +0.4", MODE_NAMES[m], vis, s.corrT);
			std::printf("  fathom_drone %-5s v %.3f: sustain %+.2f, tail %+.2f, mono sum -%.1f dB, R %+.1f dB re L, level %.1f dB\n", MODE_NAMES[m], vis,
			            s.corrS, s.corrT, s.monoLoss, s.balance, s.level);
		}
}

bool strict(const Stats& s) {
	return inside(s.corrS, -0.2, 0.5) && s.monoLoss <= 4.0 && std::fabs(s.balance) <= 1.5;
}

// Not tuned to 33 and 65 Hz: a mono sine every 5 Hz from 20 to 100 Hz, then
// every 10 Hz to 450 Hz (1.5 x the split), every mode, Visibility 0.3 and
// 0.52, a 6 s gate, sustain only. The bounds:
// - Drift noon, up to 200 Hz (2/3 of the split): the targets at every point.
// - Drift noon, 210-450 Hz: the crossover and above, where the sides' own
//   taps take over and a sine's phase between them is the network's,
//   random. Correlation -0.45 to +0.6, mono sum <= 5.5 dB, sides <= 6 dB
//   apart: da9cccc's own scatter where it has no low band to blame (300-450
//   Hz: -0.41 to +0.47, 5.2 dB, 5.3 dB); 1844904 (split at 200 Hz) reached
//   -0.50 to +0.71 at 200-300 Hz.
// - Drift 0, up to 140 Hz: the targets at every point (the high-pass leaks
//   the unmodulated sides' phase from about half the split).
// - Drift 0, 150-300 Hz: without Drift nothing averages the sides' phase, so
//   no per-point bound holds near the split (da9cccc misses 165 of 168 points
//   at 200-300 Hz); at least 30 % of the points meet the targets (measured
//   41 %; 1844904, split at 200 Hz: 6 %; da9cccc: 2 %). Above 300 Hz at
//   Drift 0 the image is the network's own, as before the low band, and is
//   not bounded here.
// And the bass's level from note to note, over 20-100 Hz per mode and
// Visibility: the spread (standard deviation, dB) of the sides' summed level
// and the worst dip below its power mean. A shared low band is one tap sum,
// whose level follows one sum's statistics (Rayleigh: a 5.6 dB spread); the
// two sides summed were two (3.3 dB at Drift 0, 2.2 at noon, da9cccc). Any
// fixed output stage that keeps the sides level on every tone is one source
// (the fix round 1 report), so these are a regression guard at this
// design's measured values plus a margin, not da9cccc's: mean spread <= 3.6
// dB at noon and <= 6.2 at Drift 0, every worst dip above -13 and -33 dB
// (measured 3.2 and 5.8; -11.6 and -29.7).
void sweep() {
	int nNoon = 0, badNoon = 0, nZero = 0, badZero = 0, nNear = 0, okNear = 0;
	double loN = 1, hiN = -1, loF = 1, hiF = -1, lossF = 0, balF = 0;
	double spread[2] = {0, 0}, dip[2] = {0, 0};
	int nSpread[2] = {0, 0};
	for (int m = 0; m < 4; m++)
		for (float vis : {0.3f, 0.52f})
			for (int k = 0; k < 2; k++) {
				const float drift = k == 0 ? 0.5f : 0.f;
				double P[17];
				int nP = 0;
				for (double hz = 20.0; hz <= 450.0; hz += hz < 100.0 ? 5.0 : 10.0) {
					const Stats s = run(m, vis, SINE, hz, drift, 6.0, 0.0);
					if (hz <= 100.0)
						P[nP++] = s.level;
					if (k == 0 && hz <= 200.0) {
						nNoon++;
						const bool ok = strict(s);
						badNoon += ok ? 0 : 1;
						loN = std::fmin(loN, s.corrS);
						hiN = std::fmax(hiN, s.corrS);
						CHECK(ok, "%s v %g Drift noon, %g Hz sine: sustain correlation %+.2f, mono sum -%.1f dB, R %+.1f dB re L; want -0.2 to +0.5, <= 4, "
						      "within 1.5",
						      MODE_NAMES[m], vis, hz, s.corrS, s.monoLoss, s.balance);
					}
					else if (k == 0) {
						const bool ok = inside(s.corrS, -0.45, 0.6) && s.monoLoss <= 5.5 && std::fabs(s.balance) <= 6.0;
						loF = std::fmin(loF, s.corrS);
						hiF = std::fmax(hiF, s.corrS);
						lossF = std::fmax(lossF, s.monoLoss);
						balF = std::fmax(balF, std::fabs(s.balance));
						CHECK(ok, "%s v %g Drift noon, %g Hz sine: sustain correlation %+.2f, mono sum -%.1f dB, R %+.1f dB re L; want -0.45 to +0.6, <= 5.5, "
						      "within 6",
						      MODE_NAMES[m], vis, hz, s.corrS, s.monoLoss, s.balance);
					}
					else if (hz <= 140.0) {
						nZero++;
						const bool ok = strict(s);
						badZero += ok ? 0 : 1;
						CHECK(ok, "%s v %g Drift 0, %g Hz sine: sustain correlation %+.2f, mono sum -%.1f dB, R %+.1f dB re L; want -0.2 to +0.5, <= 4, "
						      "within 1.5",
						      MODE_NAMES[m], vis, hz, s.corrS, s.monoLoss, s.balance);
					}
					else if (hz <= 300.0) {
						nNear++;
						okNear += strict(s) ? 1 : 0;
					}
				}
				double pm = 0, mean = 0, worst = 0;
				for (int i = 0; i < nP; i++) {
					pm += std::pow(10.0, P[i] / 10.0);
					mean += P[i];
				}
				pm = 10.0 * std::log10(pm / nP);
				mean /= nP;
				double var = 0;
				for (int i = 0; i < nP; i++) {
					var += (P[i] - mean) * (P[i] - mean);
					worst = std::fmin(worst, P[i] - pm);
				}
				spread[k] += std::sqrt(var / nP);
				nSpread[k]++;
				dip[k] = std::fmin(dip[k], worst);
			}
	const double share = nNear ? (double) okNear / nNear : 0.0;
	CHECK(share >= 0.3, "Drift 0, 150-300 Hz: %d of %d points meet the targets (%.0f %%), want >= 30 %%", okNear, nNear, 100.0 * share);
	const double sN = spread[0] / nSpread[0], sZ = spread[1] / nSpread[1];
	CHECK(sN <= 3.6 && dip[0] >= -13.0, "bass evenness, Drift noon: mean spread %.1f dB, worst dip %.1f dB; want <= 3.6 and above -13", sN, dip[0]);
	CHECK(sZ <= 6.2 && dip[1] >= -33.0, "bass evenness, Drift 0: mean spread %.1f dB, worst dip %.1f dB; want <= 6.2 and above -33", sZ, dip[1]);
	std::printf("  sweep: Drift noon 20-200 Hz %d of %d outside the targets (correlation %+.2f to %+.2f); 210-450 Hz correlation %+.2f to %+.2f, mono sum "
	            "worst -%.1f dB, sides worst %.1f dB apart\n",
	            badNoon, nNoon, loN, hiN, loF, hiF, lossF, balF);
	std::printf("  sweep: Drift 0 20-140 Hz %d of %d outside; 150-300 Hz %d of %d (%.0f %%) meet the targets\n", badZero, nZero, okNear, nNear, 100.0 * share);
	std::printf("  bass_evenness (20-100 Hz): mean spread %.1f dB at noon, %.1f at Drift 0; worst dip %.1f, %.1f dB (da9cccc: 2.2, 3.3; -7.2, -10.8)\n", sN, sZ,
	            dip[0], dip[1]);
}

} // namespace

int main() {
	monoTargets();
	stereoInputs();
	fathomDrone();
	sweep();
	return rkatest::finish("fog test_stereo");
}

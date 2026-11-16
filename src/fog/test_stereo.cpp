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
// sources, and (so the fix is not tuned to two frequencies) a sweep of 20-100
// Hz at Drift noon and at Drift 0. Measured here in double precision with
// libm, never from the engine. Built with the plugin's floating-point flags
// (tests.mk). Run with `make test`.
#include "FogEngine.hpp"
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
enum Source { SINE, NOISE, NOISE2, QUAD };

struct Stats {
	double corrS, corrT, monoLoss, balance;
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
Stats run(int mode, float vis, Source src, double hz, float drift = 0.5f, double gateS = 10.0) {
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
	const bool stereo = src == NOISE2 || src == QUAD;
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
	const long gate = (long) (gateS * RATE), end = gate + (long) (3 * RATE);
	const long s0 = gate - (long) (3 * RATE) > (long) (3 * RATE) ? gate - (long) (3 * RATE) : (long) (3 * RATE);
	const long t0 = gate + (long) (0.5 * RATE);
	double sLL = 0, sRR = 0, sLR = 0, tLL = 0, tRR = 0, tLR = 0;
	FogResult r;
	for (long t = 0; t < end; t++) {
		float l = 0.f, rr = 0.f;
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
	Stats st;
	st.corrS = corr(sLR, sLL, sRR);
	st.corrT = corr(tLR, tLL, tRR);
	// The mono sum's power against the mean of the two sides' powers.
	const double side = 0.5 * (sLL + sRR), mono = 0.25 * (sLL + sRR + 2.0 * sLR);
	st.monoLoss = mono > 0 ? 10.0 * std::log10(side / mono) : 999.0;
	st.balance = sLL > 0 && sRR > 0 ? 10.0 * std::log10(sRR / sLL) : 999.0;
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

// Not tuned to 33 and 65 Hz: every 5 Hz from 20 to 100 Hz, every mode at
// Visibility 0.3 (the shortest tail tested, where the held sound is most the
// first pass), Drift noon and Drift 0 (no wander to average the phase), a 6
// s gate: the sustain targets and the balance.
void lowSweep() {
	int bad = 0, n = 0;
	double lo = 1, hi = -1, worstLoss = 0, worstBal = 0;
	for (int m = 0; m < 4; m++)
		for (float drift : {0.5f, 0.f})
			for (double hz = 20.0; hz <= 100.0; hz += 5.0) {
				const Stats s = run(m, 0.3f, SINE, hz, drift, 6.0);
				n++;
				const bool ok = inside(s.corrS, -0.2, 0.5) && s.monoLoss <= 4.0 && std::fabs(s.balance) <= 1.5;
				CHECK(ok, "%s v 0.3 Drift %g, %g Hz sine: sustain correlation %+.2f, mono sum -%.1f dB, R %+.1f dB re L; want -0.2 to +0.5, <= 4, within 1.5",
				      MODE_NAMES[m], drift, hz, s.corrS, s.monoLoss, s.balance);
				bad += ok ? 0 : 1;
				lo = std::fmin(lo, s.corrS);
				hi = std::fmax(hi, s.corrS);
				worstLoss = std::fmax(worstLoss, s.monoLoss);
				worstBal = std::fmax(worstBal, std::fabs(s.balance));
			}
	std::printf("  low_sweep: %d of %d (20-100 Hz, 4 modes, Drift noon and 0) outside; correlation %+.2f to %+.2f, mono sum worst -%.1f dB, sides worst "
	            "%.1f dB apart\n",
	            bad, n, lo, hi, worstLoss, worstBal);
}

} // namespace

int main() {
	monoTargets();
	stereoInputs();
	lowSweep();
	return rkatest::finish("fog test_stereo");
}

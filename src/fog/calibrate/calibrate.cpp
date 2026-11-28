// Fog's level calibration: measures the make-up the wet needs to follow the
// level law, and prints each voicing's wetMakeup and wetBuild (Voicing.hpp)
// fitted to it. Run it after anything that changes the network's level (a
// voicing's line lengths, diffuser stages, damping or tone, the delay reads,
// the low band), paste the two numbers it prints for each voicing into
// Voicing.hpp, and run it again: the second run's "fit" lines must repeat
// the first's, and its "now" column is the miss the plugin then has. Not
// part of `make test` (it takes a quarter of a minute): `make fog-calibrate`.
//
// The measurement is test_engine's wet_level_law: uniform noise, +/-1 V, into
// IN L, Mix 1, the other knobs at noon, 30 s, the RMS of both outputs over
// the last 20 s against the input's. The law wants 10 log10(T60(v) / T60(0.5))
// dB. With the leveller and the limiter off the wet is linear in the make-up,
// so the make-up that would have met the law is the one used times the miss,
// whatever constants the voicing has now: the fit does not depend on them.
//
// The fit, in the make-up's own form (Network.hpp, "Wet level"): the gain
// the lines' wet needs, without the law's own sqrt(T60) term, is wetMakeup x
// (1 + wetBuild g^2), g^2 the network's mean loop gain squared
// (Network::loopG2). A straight line in g^2 by least squares on the relative
// miss, over 15 points a voicing (Visibility 0, 0.25 ... 1 by Size 0, 0.5,
// 1). It is fitted with the voicing's early reflections switched off: the
// make-up allows for those by a rule with no constant of its own, and the
// "now" column, measured with them on as the plugin has them, is the check
// on that rule.
#include "FogEngine.hpp"
#include "Voicing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>

using rka::FogEngine;
using rka::FogFrame;
using rka::FogResult;

namespace {

const float FS = 48000.f;
const int NV = 5, NS = 3;
const float VS[NV] = {0.f, 0.25f, 0.5f, 0.75f, 1.f};
const float SS[NS] = {0.f, 0.5f, 1.f};

uint32_t rng = 12345u;
float uniform() {
	rng = rng * 1664525u + 1013904223u;
	return (float) (rng >> 8) / 8388608.f - 1.f;
}

// The law's term of the make-up, sqrt(T60(v) / T60(0.5)), and in dB.
double lawGain(double v) {
	return std::pow(200.0, 0.5 * (v - 0.5));
}
double lawDb(double v) {
	return 20.0 * std::log10(lawGain(v));
}

// Wet RMS against the input's, in dB, with the voicing's early reflections
// or without; *g2 gets the network's loop gain squared and *used the make-up
// it applied, both as settled.
double wetDb(int mode, float vis, float size, bool early, double* g2, double* used) {
	static FogEngine eng; // large: keep it off the stack
	eng.limiterEnabled = false;
	eng.levellerEnabled = false;
	eng.mode = (rka::fog::Mode) mode;
	eng.setSampleRate(FS);
	eng.reset();
	if (!early) {
		eng.net.setEarly(0.f, rka::fog::voicing((rka::fog::Mode) mode).diffuseStages);
		eng.net.updateGains();
	}
	float vL = 0.f;
	FogFrame f;
	f.inL.v = &vL;
	f.inL.channels = 1;
	f.mode = mode;
	f.params[FogFrame::VISIBILITY] = vis;
	f.params[FogFrame::SIZE] = size;
	f.params[FogFrame::DAMP] = 0.5f;
	f.params[FogFrame::BLOOM] = 0.5f;
	f.params[FogFrame::DRIFT] = 0.5f;
	f.params[FogFrame::MIX] = 1.f;
	rng = 12345u;
	const int N = (int) (30 * FS);
	double in = 0, out = 0;
	for (int i = 0; i < N; i++) {
		vL = uniform();
		FogResult o;
		eng.process(f, o);
		if (i >= N / 3) {
			in += (double) vL * vL;
			out += 0.5 * ((double) o.outL * o.outL + (double) o.outR * o.outR);
		}
	}
	*g2 = eng.net.loopG2;
	*used = eng.net.makeup;
	return 10.0 * std::log10(out / in);
}

} // namespace

int main() {
	const char* names[rka::fog::MODES_LEN] = {"HAAR", "FRET", "SMOKE", "ICE"};
	double worstNow = 0, worstFit = 0;
	for (int m = 0; m < rka::fog::MODES_LEN; m++) {
		// need: the make-up, without the law's term, that meets the law with
		// the early reflections off; off: the plugin's miss now, with them on.
		double g2[NV][NS], need[NV][NS], off[NV][NS];
		// Least squares of need = A + B g^2 on the relative miss (weights
		// 1 / need^2).
		double s1 = 0, sx = 0, sxx = 0, sy = 0, sxy = 0;
		for (int iv = 0; iv < NV; iv++)
			for (int is = 0; is < NS; is++) {
				double used, unused;
				off[iv][is] = wetDb(m, VS[iv], SS[is], true, &unused, &unused) - lawDb(VS[iv]);
				const double miss = wetDb(m, VS[iv], SS[is], false, &g2[iv][is], &used) - lawDb(VS[iv]);
				const double y = used / lawGain(VS[iv]) * std::pow(10.0, -miss / 20.0);
				need[iv][is] = y;
				const double w = 1.0 / (y * y), x = g2[iv][is];
				s1 += w;
				sx += w * x;
				sxx += w * x * x;
				sy += w * y;
				sxy += w * x * y;
			}
		const double B = (s1 * sxy - sx * sy) / (s1 * sxx - sx * sx), A = (sy - B * sx) / s1;
		const rka::fog::Voicing& vo = rka::fog::voicing((rka::fog::Mode) m);
		std::printf("fit  %-5s wetMakeup %.3f, wetBuild %.3f (now %.3f, %.3f)\n", names[m], A, B / A, (double) vo.wetMakeup, (double) vo.wetBuild);
		for (int iv = 0; iv < NV; iv++)
			for (int is = 0; is < NS; is++) {
				const double fit = 20.0 * std::log10((A + B * g2[iv][is]) / need[iv][is]);
				worstNow = std::fmax(worstNow, std::fabs(off[iv][is]));
				worstFit = std::fmax(worstFit, std::fabs(fit));
				std::printf("  %-5s v %.2f size %.1f (g^2 %.3f): needs %.3f; off the law now %+.2f dB; the fit, early reflections off, %+.2f dB\n", names[m], VS[iv],
				            SS[is], g2[iv][is], need[iv][is], off[iv][is], fit);
			}
	}
	std::printf("worst miss: now %.2f dB; the fit, early reflections off, %.2f dB (test_engine's wet_level_law allows 1.5)\n", worstNow, worstFit);
	return 0;
}

// Fog's feedback delay network, in its HAAR voicing. Every expected value
// comes from the spec (docs/specs/2026-11-15-fog-design.md: the T60 law and
// its words, the Damp ratios, the even wet level) or is measured here with
// test_measure.hpp, never from the network. Built with the plugin's
// floating-point flags (tests.mk). Run with `make test`.
#include "Network.hpp"
#include "Voicing.hpp"
#include "test_measure.hpp"
#include "testing.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using rka::fog::Network;
using fogtest::measureT60;
using fogtest::rms;

const float RATE = 48000.f;

// The spec's T60 law, with libm: 0.4 s x 200^v.
double specT60(double v) {
	return 0.4 * std::pow(200.0, v);
}

// The spec's words: name and v (the table in "Visibility").
struct Word {
	const char* name;
	float v;
};
const Word WORDS[] = {{"CLEAR", 0.042f}, {"HAZE", 0.304f}, {"MIST", 0.477f},
                      {"FOG", 0.642f},   {"THICK", 0.815f}, {"DENSE", 0.975f}};
const float FOG_V = 0.642f;

struct Controls {
	float vis = 0.5f, size = 0.5f, damp = 0.f, drift = 0.f;
	int driftRate = 0;
};

struct Rig {
	Network net;
	Rig(const Controls& c) {
		net.allocate(RATE);
		net.configure(rka::fog::voicing(rka::fog::HAAR), RATE);
		net.setControls(c.vis, c.size, c.damp, c.drift, c.driftRate);
	}
};

// Impulse into L (and R if both), seconds long; wet L and R.
void impulse(const Controls& c, double seconds, std::vector<float>& L, std::vector<float>& R, bool both = true) {
	Rig rig(c);
	const size_t n = (size_t) (seconds * RATE);
	L.resize(n);
	R.resize(n);
	for (size_t i = 0; i < n; i++) {
		const float x = i == 0 ? 1.f : 0.f;
		rig.net.process(x, both ? x : 0.f, L[i], R[i]);
	}
}

// How long an impulse response must run for the T30 fit: past -35 dB at
// the spec's T60 (with the missing tail added back by measureT60), plus the
// diffuser's spread and a margin.
double irSeconds(double t60) {
	const double s = 0.68 * t60 + 2.0;
	return s < 3.0 ? 3.0 : s;
}

double t60At(const Controls& c, float bandHz, double seconds) {
	std::vector<float> L, R;
	impulse(c, seconds, L, R);
	return measureT60(L, RATE, bandHz);
}

uint32_t rngState = 1;
float white() {
	rngState = rngState * 1664525u + 1013904223u;
	return ((float) (rngState >> 8) * (1.f / 16777216.f) - 0.5f) * 3.4641016f; // unit variance
}

// Paul Kellet's pink filter on white noise, scaled to rmsV.
std::vector<float> pink(size_t n, double rmsV) {
	std::vector<float> x(n);
	double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	for (size_t i = 0; i < n; i++) {
		const double w = white();
		b0 = 0.99886 * b0 + w * 0.0555179;
		b1 = 0.99332 * b1 + w * 0.0750759;
		b2 = 0.96900 * b2 + w * 0.1538520;
		b3 = 0.86650 * b3 + w * 0.3104856;
		b4 = 0.55000 * b4 + w * 0.5329522;
		b5 = -0.7616 * b5 - w * 0.0168980;
		x[i] = (float) (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362);
		b6 = w * 0.115926;
	}
	const double r = rms(x);
	for (size_t i = 0; i < n; i++)
		x[i] = (float) (x[i] * rmsV / r);
	return x;
}

double db(double ratio) {
	return 20.0 * std::log10(ratio);
}

// The helper itself, on synthetic decays of known T60 (noise times the
// exact exponential), including one cut short as DENSE's render is.
void measureSelfCheck() {
	const double t60s[] = {0.5, 12.0, 70.0};
	for (double t60 : t60s) {
		const double seconds = irSeconds(t60);
		const size_t n = (size_t) (seconds * RATE);
		std::vector<float> ir(n);
		for (size_t i = 0; i < n; i++)
			ir[i] = white() * (float) std::pow(10.0, -3.0 * (double) i / RATE / t60);
		for (float band : {250.f, 4000.f}) {
			const double got = measureT60(ir, RATE, band);
			CHECK(std::fabs(got / t60 - 1.0) < 0.03, "measureT60 on a synthetic %g s decay at %g Hz: %g", t60, band, got);
			std::printf("  measure self-check: T60 %5.1f s at %4.0f Hz -> %.3f\n", t60, band, got);
		}
	}
	// A dark tail, as Damp makes: lows (below 800 Hz) decaying with a 12 s
	// T60 beside highs (above 2.5 kHz) decaying 8x faster, each cut by three
	// 2nd-order sections so neither has content in the other's band. The
	// 4 kHz band must read the highs' 1.5 s, not the lows' 12 s, through its
	// skirts.
	const double tLo = 12.0, tHi = 1.5;
	const size_t n = (size_t) (irSeconds(tLo) * RATE);
	std::vector<float> lo(n), hi(n), ir(n);
	for (size_t i = 0; i < n; i++) {
		const double t = (double) i / RATE;
		lo[i] = white() * (float) std::pow(10.0, -3.0 * t / tLo);
		hi[i] = white() * (float) std::pow(10.0, -3.0 * t / tHi);
	}
	std::vector<double> lp(lo.begin(), lo.end()), hp(hi.begin(), hi.end());
	for (int k = 0; k < 3; k++) {
		lp = fogtest::biquad(lp, RATE, fogtest::LOW_PASS, 800.0, 0.7071);
		hp = fogtest::biquad(hp, RATE, fogtest::HIGH_PASS, 2500.0, 0.7071);
	}
	for (size_t i = 0; i < n; i++)
		ir[i] = (float) (lp[i] + hp[i]);
	const double gotLo = measureT60(ir, RATE, 250.f), gotHi = measureT60(ir, RATE, 4000.f);
	CHECK(std::fabs(gotLo / tLo - 1.0) < 0.05, "two-band decay at 250 Hz: %g, want %g", gotLo, tLo);
	CHECK(std::fabs(gotHi / tHi - 1.0) < 0.10, "two-band decay at 4 kHz: %g, want %g", gotHi, tHi);
	std::printf("  measure self-check: two-band decay (12 s lows, 1.5 s highs): 250 Hz -> %.3f, 4 kHz -> %.3f\n", gotLo,
	            gotHi);
}

void t60MatchesVisibility() {
	for (const Word& w : WORDS) {
		Controls c;
		c.vis = w.v;
		const double want = specT60(w.v);
		const double got = t60At(c, 250.f, irSeconds(want));
		CHECK(std::fabs(got / want - 1.0) <= 0.15, "%s: T60 %g s, want %g s +/-15%%", w.name, got, want);
		std::printf("  t60_matches_visibility %-5s v=%.3f: want %6.2f s, got %6.2f s (%+.1f%%)\n", w.name, w.v, want, got,
		            100.0 * (got / want - 1.0));
	}
}

void t60HoldsAcrossSize() {
	const double want = specT60(FOG_V);
	for (float size : {0.25f, 0.5f, 0.75f}) {
		Controls c;
		c.vis = FOG_V;
		c.size = size;
		const double got = t60At(c, 250.f, irSeconds(want));
		CHECK(std::fabs(got / want - 1.0) <= 0.15, "Size %g: T60 %g s, want %g s +/-15%%", size, got, want);
		std::printf("  t60_holds_across_size Size %.2f: want %.2f s, got %.2f s (%+.1f%%)\n", size, want, got,
		            100.0 * (got / want - 1.0));
	}
}

// Damp must not shorten the low decay: Visibility means the 250 Hz T60 at
// any Damp and Size, short decays and long lines included.
void t60LowHoldsWithDamp() {
	struct Case {
		const char* name;
		float v, size;
	};
	const Case cases[] = {{"HAZE", 0.304f, 0.5f}, {"MIST", 0.477f, 1.f}};
	for (const Case& k : cases) {
		Controls c;
		c.vis = k.v;
		c.size = k.size;
		c.damp = 1.f;
		const double want = specT60(k.v);
		const double got = t60At(c, 250.f, irSeconds(want));
		CHECK(std::fabs(got / want - 1.0) <= 0.15, "%s Damp 1 Size %g: 250 Hz T60 %g s, want %g s +/-15%%", k.name, k.size, got,
		      want);
		std::printf("  t60_low_holds_with_damp %-4s Damp 1 Size %.1f: want %.2f s, got %.2f s (%+.1f%%)\n", k.name, k.size, want,
		            got, 100.0 * (got / want - 1.0));
	}
}

void dampRatio() {
	const double want = specT60(FOG_V);
	struct Case {
		float damp;
		double lo, hi;
	};
	const Case cases[] = {{0.f, 0.8, 1e9}, {0.5f, 0.35, 0.65}, {1.f, 0.0, 0.18}};
	for (const Case& k : cases) {
		Controls c;
		c.vis = FOG_V;
		c.damp = k.damp;
		std::vector<float> L, R;
		impulse(c, irSeconds(want), L, R);
		const double lo = measureT60(L, RATE, 250.f), hi = measureT60(L, RATE, 4000.f);
		const double ratio = hi / lo;
		CHECK(lo > 0 && hi > 0 && ratio >= k.lo && ratio <= k.hi, "Damp %g: T60 4k/250 = %g / %g = %g, want [%g, %g]", k.damp,
		      hi, lo, ratio, k.lo, k.hi);
		std::printf("  damp_ratio Damp %.1f: T60 250 Hz %.2f s, 4 kHz %.2f s, ratio %.3f\n", k.damp, lo, hi, ratio);
	}
}

// Wet RMS (both sides) under held pink noise at 2 V RMS into both inputs,
// at noon for every other knob, measured over 2 s after 3 x T60 (capped at
// 20 s).
double heldWetRms(float vis, const std::vector<float>& in) {
	Controls c;
	c.vis = vis;
	c.damp = 0.5f;
	c.drift = 0.5f;
	Rig rig(c);
	double settle = 3.0 * specT60(vis);
	settle = settle > 20.0 ? 20.0 : settle;
	const size_t a = (size_t) (settle * RATE), b = a + (size_t) (2.0 * RATE);
	double s = 0;
	for (size_t i = 0; i < b; i++) {
		float l, r;
		rig.net.process(in[i], in[i], l, r);
		if (i >= a)
			s += (double) l * l + (double) r * r;
	}
	return std::sqrt(s / (2.0 * (double) (b - a)));
}

void evenWetLevel() {
	const std::vector<float> in = pink((size_t) (22.0 * RATE), 2.0);
	const double inRms = rms(in);
	const double ref = heldWetRms(0.5f, in);
	std::printf("  even_wet_level: input %.3f V RMS, wet at v=0.5 %.3f V (%+.2f dB re input)\n", inRms, ref, db(ref / inRms));
	CHECK(std::fabs(db(ref / inRms)) <= 3.0, "wet at v=0.5 %g V vs input %g V", ref, inRms);
	for (const Word& w : WORDS) {
		const double got = heldWetRms(w.v, in);
		CHECK(std::fabs(db(got / ref)) <= 3.0, "%s: wet %g V, %+.2f dB re v=0.5", w.name, got, db(got / ref));
		CHECK(std::fabs(db(got / inRms)) <= 3.0, "%s: wet %g V, %+.2f dB re input", w.name, got, db(got / inRms));
		std::printf("  even_wet_level %-5s: wet %.3f V, %+.2f dB re v=0.5, %+.2f dB re input\n", w.name, got, db(got / ref),
		            db(got / inRms));
	}
}

void stereoDecorrelated() {
	Controls c;
	c.vis = FOG_V;
	std::vector<float> L, R;
	impulse(c, 4.0, L, R, false);
	const double rl = rms(L), rr = rms(R), corr = fogtest::correlation(L, R);
	CHECK(rl > 0 && rr > 0 && std::fabs(db(rr / rl)) <= 3.0, "L-only impulse: wet L %g, wet R %g RMS", rl, rr);
	CHECK(corr < 0.5, "correlation(wetL, wetR) = %g", corr);
	std::printf("  stereo_decorrelated: wet R %+.2f dB re wet L, correlation %.4f\n", db(rr / rl), corr);
}

void alwaysDecays() {
	Controls c;
	c.vis = 1.f;
	c.damp = 0.f;
	c.drift = 1.f;
	Rig rig(c);
	const size_t noise = (size_t) (10.0 * RATE), win = (size_t) (5.0 * RATE);
	float l, r;
	for (size_t i = 0; i < noise; i++) {
		const float x = 2.f * white();
		rig.net.process(x, x, l, r);
	}
	double e[8];
	bool finite = true;
	for (int k = 0; k < 8; k++) {
		double s = 0;
		for (size_t i = 0; i < win; i++) {
			rig.net.process(0.f, 0.f, l, r);
			finite = finite && std::isfinite(l) && std::isfinite(r);
			s += (double) l * l + (double) r * r;
		}
		e[k] = s;
	}
	CHECK(finite, "non-finite output");
	bool mono = true;
	for (int k = 1; k < 8; k++)
		mono = mono && e[k] < e[k - 1];
	CHECK(mono, "energy per 5 s window does not fall monotonically");
	const double drop = 10.0 * std::log10(e[7] / e[0]);
	CHECK(drop <= -20.0, "after 40 s the tail is only %g dB down", drop);
	std::printf("  always_decays: last 5 s window of 40 s is %.1f dB below the first (spec law: %.1f dB)\n", drop,
	            -60.0 * 35.0 / specT60(1.0));
}

// Wet L under a held input, with a lead-in of `lead` zeros first (dropped).
std::vector<float> render(const Controls& c, const std::vector<float>& in, size_t lead) {
	Rig rig(c);
	std::vector<float> out(in.size());
	float l, r;
	for (size_t i = 0; i < lead; i++)
		rig.net.process(0.f, 0.f, l, r);
	for (size_t i = 0; i < in.size(); i++) {
		rig.net.process(in[i], in[i], l, r);
		out[i] = l;
	}
	return out;
}

// With Drift 0 the network is time-invariant: the same input, started at
// two different times (after a lead-in of silence), gives bit-identical
// output. Any modulation running in the background would differ.
void driftZeroIsStatic() {
	Controls c;
	c.vis = FOG_V;
	c.drift = 0.f;
	std::vector<float> in((size_t) (3.0 * RATE));
	for (size_t i = 0; i < in.size(); i++)
		in[i] = i < (size_t) (0.5 * RATE) ? white() : 0.f;
	const std::vector<float> a = render(c, in, 0), b = render(c, in, (size_t) (1.37 * RATE));
	size_t diff = 0;
	for (size_t i = 0; i < a.size(); i++)
		diff += a[i] != b[i];
	CHECK(diff == 0, "Drift 0: %zu of %zu samples differ between two renders", diff, a.size());
	// Sanity check that the comparison can see a difference: with Drift 1 the
	// two renders differ.
	c.drift = 1.f;
	const std::vector<float> a1 = render(c, in, 0), b1 = render(c, in, (size_t) (1.37 * RATE));
	size_t diff1 = 0;
	for (size_t i = 0; i < a1.size(); i++)
		diff1 += a1[i] != b1[i];
	CHECK(diff1 > 0, "Drift 1: the two renders are identical");
	std::printf("  drift_zero_is_static: Drift 0 %zu samples differ, Drift 1 %zu differ\n", diff, diff1);
}

// A held 1 kHz tone; the RMS spectral width of the wet output about 1 kHz,
// once the tail has built up.
double toneSpread(float drift) {
	Controls c;
	c.vis = FOG_V;
	c.drift = drift;
	const size_t settle = (size_t) (4.0 * RATE), n = (size_t) (2.0 * RATE);
	std::vector<float> in(settle + n);
	for (size_t i = 0; i < in.size(); i++)
		in[i] = (float) std::sin(2.0 * fogtest::PI * 1000.0 * (double) i / RATE);
	const std::vector<float> out = render(c, in, 0);
	const std::vector<float> tail(out.begin() + (long) settle, out.end());
	return fogtest::spectralSpread(tail, RATE, 1000.0, 100.0, 0.5);
}

void driftMoves() {
	const double s0 = toneSpread(0.f), s1 = toneSpread(1.f);
	CHECK(s1 > 2.0 * s0 && s1 > 2.0, "spectral spread at 1 kHz: Drift 0 %g Hz, Drift 1 %g Hz", s0, s1);
	std::printf("  drift_moves: RMS spread about 1 kHz, Drift 0 %.2f Hz, Drift 1 %.2f Hz\n", s0, s1);
}

// Drift fully right gives pitch deviations in the tens of cents at either
// rate (spec, "Drift"). A read swinging by d samples at f cycles per sample
// moves the pitch by up to 2 pi f d (relative), so the peak deviation is
// computed from the network's modulation parameters. Every swing must also
// stay inside its line at both ends of Size.
void driftDepthPerRate() {
	for (int rate = 0; rate < 2; rate++) {
		double peak = 0;
		bool inBounds = true;
		for (float size : {0.f, 1.f}) {
			Controls c;
			c.drift = 1.f;
			c.size = size;
			c.driftRate = rate;
			Rig rig(c);
			const Network& n = rig.net;
			for (int i = 0; i < n.n; i++) {
				const double rel = 2.0 * fogtest::PI * n.phaseInc[i] * n.lineDepth(i);
				const double cents = 1200.0 * std::log2(1.0 + rel);
				peak = cents > peak ? cents : peak;
				const double lo = n.len[i] - n.lineDepth(i), hi = n.len[i] + n.lineDepth(i);
				inBounds = inBounds && lo >= 1.0 && hi <= n.line[i].maxDelay() - 2;
			}
		}
		CHECK(peak >= 20.0 && peak < 100.0, "Drift 1, %s: peak deviation %g cents, want tens of cents",
		      rate ? "Very slow" : "Slow", peak);
		CHECK(inBounds, "Drift 1, %s: a swing leaves its line at Size 0 or 1", rate ? "Very slow" : "Slow");
		std::printf("  drift_depth_per_rate %-9s: peak %.1f cents, reads in bounds: %s\n", rate ? "Very slow" : "Slow", peak,
		            inBounds ? "yes" : "no");
	}
}

// Every delay line's write position sits in its own L1 cache set (index
// (pos >> 4) & 63), so the 57 lines (the diffuser's 32, the network's 16,
// SMOKE's 8 in-loop allpasses and ICE's shifter) do not evict each other as
// they advance in lockstep. Also after clear(), which must restore the
// staggered start.
void linesUseDistinctCacheSets() {
	Controls c;
	Rig rig(c);
	for (int pass = 0; pass < 2; pass++) {
		bool seen[64] = {};
		int count = 0, dup = 0;
		auto note = [&](const rka::DelayLine& d) {
			const int set = (int) ((d.pos >> 4) & 63u);
			if (seen[set])
				dup++;
			seen[set] = true;
			count++;
		};
		for (int i = 0; i < Network::MAX_LINES; i++)
			note(rig.net.line[i]);
		for (int s = 0; s < 4; s++)
			for (int i = 0; i < 8; i++)
				note(rig.net.diffuser.lines[s][i]);
		for (int i = 0; i < Network::LOOP_AP; i++)
			note(rig.net.loopAp[i]);
		note(rig.net.shiftBuf);
		CHECK(count == 57, "counted %d lines", count);
		CHECK(dup == 0, "pass %d: %d lines share a cache set with another", pass, dup);
		rig.net.clear(); // second pass: the start survives clear()
	}
	// The output is the same from the staggered start: an impulse still
	// comes back at its delay.
	rka::DelayLine d;
	d.allocate(1000, 5);
	CHECK(d.pos != 0, "stagger 5 left pos at 0");
	d.write(1.f);
	for (int k = 0; k < 99; k++)
		d.write(0.f);
	CHECK(d.readInt(99) == 1.f, "impulse not read back at its delay");
}

} // namespace

int main() {
	const auto t0 = std::chrono::steady_clock::now();
	measureSelfCheck();
	linesUseDistinctCacheSets();
	t60MatchesVisibility();
	t60HoldsAcrossSize();
	t60LowHoldsWithDamp();
	dampRatio();
	evenWetLevel();
	stereoDecorrelated();
	alwaysDecays();
	driftZeroIsStatic();
	driftMoves();
	driftDepthPerRate();
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  test_network ran in %.1f s\n", secs);
	return rkatest::finish("test_network");
}

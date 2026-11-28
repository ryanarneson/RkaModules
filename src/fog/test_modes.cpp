// Fog's four modes (spec, "Modes: voicings of one engine"): every mode keeps
// Visibility's T60 and each knob's direction; FRET is darker than HAAR, SMOKE
// is echo-led, ICE has an octave-up shimmer; switching modes while running
// does not click and starts the new mode clean; and every mode is as safe as
// HAAR. Expected values come from the spec or are measured here with
// test_measure.hpp (libm, double precision), never from the engine. Built
// with the plugin's floating-point flags (tests.mk). Run with `make test`.
#include "Bloom.hpp"
#include "FogEngine.hpp"
#include "Network.hpp"
#include "Shifter.hpp"
#include "Voicing.hpp"
#include "test_measure.hpp"
#include "testing.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using rka::FogEngine;
using rka::FogFrame;
using rka::FogResult;
using rka::fog::Network;
using fogtest::measureT60;
using fogtest::rms;

const float RATE = 48000.f;
const double PI = 3.14159265358979323846;
const float FOG_V = 0.642f;
const char* NAMES[4] = {"HAAR", "FRET", "SMOKE", "ICE"};

double specT60(double v) {
	return 0.4 * std::pow(200.0, v);
}

double db(double ratio) {
	return 20.0 * std::log10(ratio);
}

uint32_t rngState = 99u;
float white() { // unit variance
	rngState = rngState * 1664525u + 1013904223u;
	return ((float) (rngState >> 8) * (1.f / 16777216.f) - 0.5f) * 3.4641016f;
}

struct Controls {
	float vis = FOG_V, size = 0.5f, damp = 0.5f, drift = 0.5f;
};

// The network alone, in mode m (no Bloom, no low cut: an impulse goes in whole).
struct NetRig {
	Network net;
	NetRig(int m, const Controls& c, float rate = RATE) {
		net.allocate(rate);
		net.configure(rka::fog::voicing((rka::fog::Mode) m), rate);
		net.setControls(c.vis, c.size, c.damp, c.drift, 0);
	}
};

// Wet L of an impulse into both inputs.
std::vector<float> impulse(int m, const Controls& c, double seconds) {
	NetRig* rig = new NetRig(m, c); // large: on the heap
	const size_t n = (size_t) (seconds * RATE);
	std::vector<float> L(n);
	for (size_t i = 0; i < n; i++) {
		const float x = i == 0 ? 1.f : 0.f;
		float l, r;
		rig->net.process(x, x, l, r);
		L[i] = l;
	}
	delete rig;
	return L;
}

double irSeconds(double t60) {
	const double s = 0.68 * t60 + 2.0;
	return s < 3.0 ? 3.0 : s;
}

// An engine with one mono signal on L, noon knobs, Bloom 0, Mix 1.
struct Rig {
	FogEngine eng;
	FogFrame f;
	float vL = 0.f, vR = 0.f;
	float cv[5] = {};
	explicit Rig(int mode) {
		eng.setSampleRate(RATE);
		for (int p = FogFrame::VISIBILITY; p <= FogFrame::DRIFT; p++)
			f.params[p] = 0.5f;
		f.params[FogFrame::BLOOM] = 0.f;
		f.params[FogFrame::MIX] = 1.f;
		f.mode = mode;
		f.inL.v = &vL;
		f.inL.channels = 1;
		f.inR.v = &vR;
		f.inR.channels = 0;
		for (int i = 0; i < 5; i++) {
			f.cv[i].v = &cv[i];
			f.cv[i].channels = 0;
		}
	}
	FogResult step(float l) {
		vL = l;
		FogResult r;
		eng.process(f, r);
		return r;
	}
};

// ---- T60 and the knobs ----

// Visibility's T60 holds in every mode: 250 Hz at FOG, noon knobs, +/-20 %,
// at Size 0, noon and 1 (at Size 0, SMOKE's in-loop allpasses are a quarter
// of its loop: leaving their delay out of the gains reads +27 %); and at
// DENSE, Size 0, the most loop passes per T60 (ICE's shifter, at a fixed
// coupling, leaked its lows away there: 38 s for 70). ICE runs about 13 %
// short by design (its shimmer's crossfade, Network.hpp).
//
// Not tested: CLEAR (0.5 s) at Size 1. The lines are 140-500 ms long, so
// the tail is over in one to three passes, a sparse train of reflections
// that the T30 fit misreads: it reads -46 % in ICE, -38 % in SMOKE and
// -15 % in HAAR, the same with ICE's shimmer off. That is the measurement's
// limit on so sparse a response, not a decay running fast.
void modesT60() {
	struct Case {
		float vis, size;
	};
	const Case cases[] = {{FOG_V, 0.f}, {FOG_V, 0.5f}, {FOG_V, 1.f}, {0.975f, 0.f}};
	for (int m = 0; m < 4; m++)
		for (const Case& k : cases) {
			const double want = specT60(k.vis);
			const float size = k.size;
			Controls c;
			c.vis = k.vis;
			c.size = size;
			const std::vector<float> ir = impulse(m, c, irSeconds(want));
			const double got = measureT60(ir, RATE, 250.f);
			CHECK(got > 0 && std::fabs(got / want - 1.0) <= 0.20, "%s v %g Size %g: 250 Hz T60 %g s, want %g s +/-20%%", NAMES[m], k.vis, size,
			      got, want);
			std::printf("  modes_t60 %-5s v %.3f Size %.1f: want %.2f s, got %.2f s (%+.1f%%)\n", NAMES[m], k.vis, size, want, got,
			            100.0 * (got / want - 1.0));
		}
}

// SMOKE's late tail decays at Visibility's rate too. Its in-loop allpasses
// delay some frequencies up to four times as long as others (coefficient
// 0.6), so with the loop's loss taken once a pass, at the line's gain, the
// resonances that spend longest in the allpasses lost the least a second:
// at Size 0, where the allpasses are a quarter of the loop, every band read
// 3-6 % long and the last of the tail, which is those resonances, decayed
// 10-14 % slow (and failed the soak at DENSE once the reads stopped losing
// highs). The loss is now taken inside each allpass as well, on its own
// delay, so every resonance loses the same a second. Damp 0, Drift 0, an
// impulse, 0.9 x T60 + 2 s long: the T60 read from the fall in energy
// between 55-65 % and 85-95 % of the run is Visibility's within 5 %, at
// Size 0 and noon.
void smokeLateTail() {
	for (float size : {0.f, 0.5f})
		for (float vis : {0.5f, 0.75f}) {
			Controls c;
			c.vis = vis;
			c.size = size;
			c.damp = 0.f;
			c.drift = 0.f;
			const double want = specT60(vis);
			const std::vector<float> ir = impulse(rka::fog::SMOKE, c, 0.9 * want + 2.0);
			const size_t n = ir.size();
			auto energy = [&](double a, double b) {
				double sum = 0;
				for (size_t i = (size_t) (a * n); i < (size_t) (b * n); i++)
					sum += (double) ir[i] * ir[i];
				return sum;
			};
			const double dbPerS = 10.0 * std::log10(energy(0.85, 0.95) / energy(0.55, 0.65)) / (0.3 * (double) n / RATE);
			const double got = -60.0 / dbPerS;
			CHECK(std::fabs(got / want - 1.0) <= 0.05, "SMOKE v %g Size %g: the late tail's T60 is %g s, want %g s +/-5%%", vis, size, got, want);
			std::printf("  smoke_late_tail v %.2f Size %.1f: want %.2f s, got %.2f s (%+.1f%%)\n", vis, size, want, got, 100.0 * (got / want - 1.0));
		}
}

// ---- Early reflections (issue #21) ----

// The wet arrives at once in HAAR, FRET and ICE. Their lines' first echoes
// come 50-75 ms after a sound at noon Size, and up to 0.2 s at Size 1; with
// nothing before them the output took 0.12 s (HAAR), 0.15 s (FRET) and 0.08 s
// (ICE) to reach -20 dB of its sustained level (LV-426: 0.02 s). The
// diffuser's own build-up is now added to the wet ahead of them
// (Voicing::early). The issue's targets, at Bloom 0, a T60 of 6.32 s, noon
// Size, on +/-1 V noise from silence, Mix 1, the envelope in 5 ms windows
// against the RMS of the last third of 12 s, each of three noise seeds:
// - FRET reaches -20 dB within 0.04 s; HAAR and ICE within 0.10 s;
// - FRET's first 80 ms stays at least 6 dB under the sustain;
// - SMOKE has none (its first repeat is its start): its wet is exactly 0
//   for 130 ms (its shortest line is 137.9 ms) and -20 dB takes over 0.12 s.
// Also at Size 1, where the lines are latest: HAAR, FRET and ICE still reach
// -20 dB within 0.05 s.
void earlyOnset() {
	const float vis = 0.521f; // 6.32 s
	for (int m = 0; m < 4; m++)
		for (float size : {0.5f, 1.f}) {
			double worstT = 0, worstFirst = -1e9, silent = 1e9;
			for (uint32_t seed = 1; seed <= 3; seed++) {
				rngState = 7919u * seed;
				Rig* r = new Rig(m);
				r->f.params[FogFrame::VISIBILITY] = vis;
				r->f.params[FogFrame::SIZE] = size;
				const int W = (int) (0.005 * RATE), N = (int) (12 * RATE);
				std::vector<double> env;
				double acc = 0;
				int firstSound = -1;
				for (int i = 0; i < N; i++) {
					const FogResult o = r->step(0.57735f * white()); // +/-1 V uniform's RMS
					if (firstSound < 0 && (o.outL != 0.f || o.outR != 0.f))
						firstSound = i;
					acc += 0.5 * ((double) o.outL * o.outL + (double) o.outR * o.outR);
					if (i % W == W - 1) {
						env.push_back(std::sqrt(acc / W));
						acc = 0;
					}
				}
				delete r;
				double sus = 0;
				for (size_t k = env.size() * 2 / 3; k < env.size(); k++)
					sus += env[k] * env[k];
				sus = std::sqrt(sus / (double) (env.size() - env.size() * 2 / 3));
				size_t k = 0;
				while (k < env.size() && env[k] < 0.1 * sus)
					k++;
				worstT = std::fmax(worstT, (double) (k + 1) * 0.005);
				double top = 0;
				for (int j = 0; j < 16; j++)
					top = std::fmax(top, env[j]);
				worstFirst = std::fmax(worstFirst, 20.0 * std::log10(top / sus + 1e-300));
				silent = std::fmin(silent, (double) firstSound / RATE);
			}
			if (m == rka::fog::SMOKE) {
				if (size == 0.5f) {
					CHECK(silent >= 0.130, "SMOKE: wet %g s after the input, want none before its lines (0.130 s)", silent);
					CHECK(worstT > 0.12, "SMOKE reaches -20 dB in %g s: it should have no early reflections", worstT);
				}
			}
			else if (size == 0.5f) {
				const double limit = m == rka::fog::FRET ? 0.04 : 0.10;
				CHECK(worstT <= limit, "%s: -20 dB of the sustain after %g s, want <= %g", NAMES[m], worstT, limit);
				if (m == rka::fog::FRET)
					CHECK(worstFirst <= -6.0, "FRET: the first 80 ms reach %+.1f dB re the sustain, want <= -6", worstFirst);
			}
			else
				CHECK(worstT <= 0.05, "%s at Size 1: -20 dB of the sustain after %g s, want <= 0.05", NAMES[m], worstT);
			std::printf("  early_onset %-5s Size %.1f: first sound at %.1f ms, -20 dB within %.3f s, first 80 ms at most %+.1f dB re the sustain\n",
			            NAMES[m], size, 1e3 * silent, worstT, worstFirst);
		}
}

// The early reflections are stereo, and safe to sum to mono. At Size 1 the
// lines' first echoes are 140 ms or more away (HAAR, FRET, ICE), so the
// first 120 ms of the wet are the early reflections alone: with the same
// noise on both inputs for those 120 ms, both sides sound (within 3 dB of
// each other), they are not the same signal (correlation under 0.6 in
// magnitude), and their sum keeps its power within 4 dB of the two sides'
// (0 dB for the same signal on both, -3 dB for unrelated ones).
void earlyStereo() {
	const int modes[3] = {rka::fog::HAAR, rka::fog::FRET, rka::fog::ICE};
	for (int m : modes) {
		Controls c;
		c.size = 1.f;
		c.drift = 0.f;
		NetRig* rig = new NetRig(m, c);
		const int N = (int) (0.120 * RATE);
		std::vector<float> L(N), R(N);
		rngState = 4242u;
		for (int i = 0; i < N; i++) {
			const float x = white();
			rig->net.process(x, x, L[i], R[i]);
		}
		delete rig;
		double ll = 0, rr = 0, lr = 0;
		for (int i = 0; i < N; i++) {
			ll += (double) L[i] * L[i];
			rr += (double) R[i] * R[i];
			lr += (double) L[i] * R[i];
		}
		CHECK(ll > 0 && rr > 0, "%s: no early reflections (L %g, R %g)", NAMES[m], ll, rr);
		const double balance = 10.0 * std::log10(rr / ll), corr = lr / std::sqrt(ll * rr);
		const double monoDb = 10.0 * std::log10((ll + rr + 2.0 * lr) / (2.0 * (ll + rr)));
		CHECK(std::fabs(balance) <= 3.0, "%s: early reflections R against L %+.1f dB, want within 3", NAMES[m], balance);
		CHECK(std::fabs(corr) <= 0.6, "%s: early reflections L/R correlation %+.2f, want within 0.6", NAMES[m], corr);
		CHECK(monoDb >= -4.0, "%s: the early reflections' mono sum is %.1f dB, want >= -4", NAMES[m], monoDb);
		std::printf("  early_stereo %-5s: R/L %+.2f dB, correlation %+.2f, mono sum %.1f dB\n", NAMES[m], balance, corr, monoDb);
	}
}

// Rise time to -3 dB of the Bloom swell under a held input, in the mode's
// voicing (as the engine sets it).
double riseTime(float bloom, float bias) {
	rka::fog::Bloom b;
	b.setup(RATE);
	b.set(bloom, bias);
	for (int i = 0; i < (int) (60 * RATE); i++)
		if (b.process(1.f) >= 0.70794578f)
			return (double) i / RATE;
	return -1.0;
}

// Each knob keeps its direction in every mode: Damp 0 -> 1 shortens the
// 4 kHz T60 by at least 30 %; Bloom 0 -> 1 lengthens the feed's rise time
// at least 3x.
void modesKnobDirections() {
	const double want = specT60(FOG_V);
	for (int m = 0; m < 4; m++) {
		double hi[2];
		for (int k = 0; k < 2; k++) {
			Controls c;
			c.damp = (float) k;
			hi[k] = measureT60(impulse(m, c, irSeconds(want)), RATE, 4000.f);
		}
		CHECK(hi[0] > 0 && hi[1] > 0 && hi[1] <= 0.7 * hi[0], "%s: 4 kHz T60 Damp 0 %g s, Damp 1 %g s, want <= 70%%", NAMES[m], hi[0],
		      hi[1]);
		const float bias = rka::fog::voicing((rka::fog::Mode) m).bloomBias;
		const double r0 = riseTime(0.f, bias), r1 = riseTime(1.f, bias);
		CHECK(r0 > 0 && r1 >= 3.0 * r0, "%s: Bloom rise 0 -> %g s, 1 -> %g s, want >= 3x", NAMES[m], r0, r1);
		std::printf("  modes_knob_directions %-5s: 4 kHz T60 Damp 0 %.2f s, Damp 1 %.2f s (%.0f%%); Bloom rise %.3f s -> %.2f s\n",
		            NAMES[m], hi[0], hi[1], 100.0 * hi[1] / hi[0], r0, r1);
	}
}

// T60 4 kHz / 250 Hz at FOG, Size noon, Drift noon, the given Damp.
double hfRatio(int m, float damp) {
	Controls c;
	c.damp = damp;
	const std::vector<float> ir = impulse(m, c, irSeconds(specT60(FOG_V)));
	return measureT60(ir, RATE, 4000.f) / measureT60(ir, RATE, 250.f);
}

// FRET is darker than HAAR at the same settings: its 4 kHz / 250 Hz T60
// ratio is below HAAR's (by at least 15 %, beyond the measurement's spread)
// at Damp 0 and at noon.
void fretDarkerThanHaar() {
	for (float d : {0.f, 0.5f}) {
		const double h = hfRatio(rka::fog::HAAR, d), f = hfRatio(rka::fog::FRET, d);
		CHECK(f > 0 && h > 0 && f <= 0.85 * h, "Damp %g: FRET ratio %g, HAAR %g, want FRET <= 0.85 HAAR", d, f, h);
		std::printf("  fret_darker_than_haar Damp %.1f: T60 4k/250 FRET %.3f, HAAR %.3f\n", d, f, h);
	}
}

// The wet envelope of an impulse at Size 1 over its first 2.5 s (Visibility
// FOG, Damp noon, Drift 0): the RMS over 20 ms windows every 10 ms, in dB.
// Peaks are counted in the first 2 s only; the extra half second lets a
// repeat just before 2 s be judged against the trough after it, not cut off
// by the end of the window.
std::vector<double> echoEnvelope(int m) {
	Controls c;
	c.size = 1.f;
	c.drift = 0.f;
	const std::vector<float> ir = impulse(m, c, 2.5);
	const size_t win = (size_t) (0.02 * RATE), hop = (size_t) (0.01 * RATE);
	std::vector<double> e;
	for (size_t a = 0; a + win <= ir.size(); a += hop)
		e.push_back(db(rms(ir, a, a + win) + 1e-12));
	return e;
}

// The envelope's peaks, in time order, whose prominence (height above the
// higher of the two troughs separating it from a higher point, or from the
// ends) is at least 6 dB: the distinct repeats.
std::vector<size_t> echoPeaks(const std::vector<double>& e) {
	std::vector<size_t> peaks;
	for (size_t i = 1; i + 1 < e.size() && i < 200; i++) { // 200 hops: 2 s
		if (!(e[i] > e[i - 1] && e[i] >= e[i + 1]))
			continue;
		double left = e[i], right = e[i];
		size_t j = i;
		while (j > 0 && e[j - 1] <= e[i]) {
			j--;
			left = e[j] < left ? e[j] : left;
		}
		j = i;
		while (j + 1 < e.size() && e[j + 1] <= e[i]) {
			j++;
			right = e[j] < right ? e[j] : right;
		}
		if (e[i] - (left > right ? left : right) >= 6.0)
			peaks.push_back(i);
	}
	return peaks;
}

// SMOKE is echo-led: at Size 1, at least 3 distinct repeats in the first 2 s
// (it shows 4, every 0.46 s), and more than HAAR shows. And they blur into the wash (the in-loop
// diffusion): between the second and third repeats the envelope never falls
// more than 40 dB under the second (with no diffusion in the loop, the
// repeats sit in silence, over 100 dB down).
void smokeHasEchoes() {
	const std::vector<double> e = echoEnvelope(rka::fog::SMOKE);
	const std::vector<size_t> s = echoPeaks(e);
	const size_t h = echoPeaks(echoEnvelope(rka::fog::HAAR)).size();
	CHECK(s.size() >= 3, "SMOKE: %zu envelope peaks of 6 dB in 2 s, want >= 3", s.size());
	CHECK(h < s.size(), "HAAR shows %zu peaks, SMOKE %zu: want HAAR fewer", h, s.size());
	if (s.size() >= 3) {
		double trough = e[s[1]];
		for (size_t i = s[1]; i <= s[2]; i++)
			trough = e[i] < trough ? e[i] : trough;
		const double depth = e[s[1]] - trough;
		CHECK(depth <= 40.0, "SMOKE: the gap after the second repeat falls %g dB, want <= 40 (blurred)", depth);
		std::printf("  smoke_has_echoes: SMOKE repeats at %.2f, %.2f, %.2f s; gap after the second %.1f dB deep\n", s[0] * 0.01,
		            s[1] * 0.01, s[2] * 0.01, depth);
	}
	std::printf("  smoke_has_echoes: SMOKE %zu peaks, HAAR %zu (also FRET %zu, ICE %zu)\n", s.size(), h,
	            echoPeaks(echoEnvelope(rka::fog::FRET)).size(), echoPeaks(echoEnvelope(rka::fog::ICE)).size());
}

// Power (Hann-windowed DFT, summed over centre +/- 10 Hz in 1 Hz steps): the
// octave itself, not sidebands beside it. (A two-tap shifter with 50 ms
// grains cancels its own carrier at 40n + 20 Hz, leaving a pair at +/-20 Hz
// beating at 40 Hz; a band of +/-40 Hz hid that.)
double bandPower(const std::vector<float>& x, double centre, double rate = RATE) {
	const size_t n = x.size();
	double total = 0;
	for (double f = centre - 10.0; f <= centre + 10.0 + 1e-9; f += 1.0) {
		const double dw = 2.0 * PI * f / rate;
		double re = 0, im = 0;
		for (size_t i = 0; i < n; i++) {
			const double w = (0.5 - 0.5 * std::cos(2.0 * PI * (double) i / (double) n)) * x[i];
			re += w * std::cos(dw * (double) i);
			im -= w * std::sin(dw * (double) i);
		}
		total += re * re + im * im;
	}
	return total;
}

// The tail (0.5-2.5 s after the input stops) of a 3 s, 2 V 220 Hz sine, at
// noon knobs, Mix 1, Bloom 0. The sine fades in and out over 100 ms (raised
// cosine), so a click at either end does not put broadband energy, 440 Hz
// included, into the tail.
std::vector<float> sineTail(int m) {
	Rig* r = new Rig(m);
	const int n = (int) (3 * RATE), ramp = (int) (0.1 * RATE);
	for (int i = 0; i < n; i++) {
		const int edge = i < n - 1 - i ? i : n - 1 - i;
		const double g = edge >= ramp ? 1.0 : 0.5 - 0.5 * std::cos(PI * edge / ramp);
		r->step((float) (2.0 * g * std::sin(2.0 * PI * 220.0 * i / RATE)));
	}
	std::vector<float> t;
	for (int i = 0; i < (int) (2.5 * RATE); i++) {
		const FogResult o = r->step(0.f);
		if (i >= (int) (0.5 * RATE))
			t.push_back(o.outL);
	}
	delete r;
	return t;
}

// ICE's tail holds an octave up: under a 220 Hz sine, its energy near 440 Hz
// is at least 20 dB above HAAR's.
void iceShimmer() {
	const std::vector<float> ice = sineTail(rka::fog::ICE), haar = sineTail(rka::fog::HAAR);
	const double pi = bandPower(ice, 440.0), ph = bandPower(haar, 440.0);
	const double ri = bandPower(ice, 220.0), rh = bandPower(haar, 220.0);
	const double d = 10.0 * std::log10((pi + 1e-30) / (ph + 1e-30));
	CHECK(d >= 20.0, "ICE 440 Hz energy %+.1f dB re HAAR's, want >= +20", d);
	std::printf("  ice_shimmer: 440 Hz ICE %+.1f dB re HAAR; re its own 220 Hz: ICE %+.1f dB, HAAR %+.1f dB\n", d,
	            10.0 * std::log10(pi / ri), 10.0 * std::log10((ph + 1e-30) / rh));
}

// The network's tail (0.5-2.5 s after the input stops) of a 3 s, 1 V sine at
// hz (raised-cosine ends, 100 ms), Visibility, Size and Drift at noon, Damp 0
// (so the octave is not darkened against its note).
void netSineTail(int m, double hz, std::vector<float>& L, std::vector<float>& R, float vis = 0.5f) {
	Controls c;
	c.vis = vis;
	c.damp = 0.f;
	NetRig* rig = new NetRig(m, c);
	const int n = (int) (3 * RATE), ramp = (int) (0.1 * RATE);
	float l, r;
	for (int i = 0; i < n; i++) {
		const int edge = i < n - 1 - i ? i : n - 1 - i;
		const double g = edge >= ramp ? 1.0 : 0.5 - 0.5 * std::cos(PI * edge / ramp);
		const float x = (float) (g * std::sin(2.0 * PI * hz * i / RATE));
		rig->net.process(x, x, l, r);
	}
	L.clear();
	R.clear();
	for (int i = 0; i < (int) (2.5 * RATE); i++) {
		rig->net.process(0.f, 0.f, l, r);
		if (i >= (int) (0.5 * RATE)) {
			L.push_back(l);
			R.push_back(r);
		}
	}
	delete rig;
}

// The octave against its note in the tail, dB: the power in +/-10 Hz bands at
// 2f and at f, both sides, summed over three notes at f x 0.985, 1 and 1.015.
// A single pure tone meets the network's modal response by luck: the note
// and its octave each land +/-3 dB from their neighbours' level (ICE at
// 220 Hz: the note 2 dB low, its octave 3 dB high, whatever the shifter),
// so one tone reads the network, not the shifter; the three-note cluster
// averages that down.
double octaveDb(int m, double hz, float vis = 0.5f, double* octAbs = nullptr) {
	double note = 0, oct = 0;
	std::vector<float> L, R;
	for (double k : {0.985, 1.0, 1.015}) {
		netSineTail(m, hz * k, L, R, vis);
		note += bandPower(L, hz * k) + bandPower(R, hz * k);
		oct += bandPower(L, 2.0 * hz * k) + bandPower(R, 2.0 * hz * k);
	}
	if (octAbs)
		*octAbs = 10.0 * std::log10(oct + 1e-30);
	return 10.0 * std::log10((oct + 1e-30) / (note + 1e-30));
}

double median(std::vector<double> v) {
	std::sort(v.begin(), v.end());
	return v[v.size() / 2];
}

// ICE's octave is an octave at every pitch: across 110-880 Hz (220, 260 and
// 300 Hz among them, where a two-tap shifter with 50 ms grains cancels its
// carrier) the octave in a +/-10 Hz band at 2f, against the note, is within
// 6 dB of its median over those pitches, and at least 30 dB above HAAR's (a
// linear network: no octave). 6 dB is twice the spread measured with the
// jittered shifter (-2.4 to +2.8 dB, which includes the 80 Hz high-pass's
// roll-off at 110 Hz); the two-tap shifter with fixed 50 ms grains missed it
// by 13-18 dB at 147, 220, 260 and 300 Hz, and 8 dB over at 880 Hz.
void iceOctaveEveryPitch() {
	const double pitches[] = {110, 147, 196, 220, 260, 300, 349, 440, 523, 659, 880};
	std::vector<double> d;
	for (double hz : pitches)
		d.push_back(octaveDb(rka::fog::ICE, hz));
	const double med = median(d), haar = octaveDb(rka::fog::HAAR, 220.0);
	for (size_t k = 0; k < d.size(); k++) {
		CHECK(std::fabs(d[k] - med) <= 6.0, "ICE %g Hz: octave %+.1f dB re note, %+.1f dB from the median %+.1f, want within 6",
		      pitches[k], d[k], d[k] - med, med);
		std::printf("  ice_octave_every_pitch %4.0f Hz: octave %+.1f dB re note (%+.1f from median)\n", pitches[k], d[k], d[k] - med);
	}
	CHECK(med >= haar + 30.0, "ICE median octave %+.1f dB, HAAR %+.1f dB: want >= 30 dB above", med, haar);
	std::printf("  ice_octave_every_pitch: median %+.1f dB re note, HAAR %+.1f dB\n", med, haar);
}

// ICE's octave stays audible however long the tail (the feed-forward octave
// on the wet): under a held 440 Hz note at Visibility 0.5, 0.75 and 1, the
// 880 Hz carrier is 13 +/- 4 dB under the note, it falls no more than 4 dB
// from noon to fully right, and it is at least 20 dB above HAAR's 880 Hz
// (whose octave is only leakage). The in-loop cascade alone gave -15, -27
// and -34 dB.
void iceOctaveAudibleLong() {
	double first = 0;
	for (float v : {0.5f, 0.75f, 1.f}) {
		double iceAbs = 0, haarAbs = 0;
		const double d = octaveDb(rka::fog::ICE, 440.0, v, &iceAbs);
		octaveDb(rka::fog::HAAR, 440.0, v, &haarAbs);
		if (v == 0.5f)
			first = d;
		CHECK(d >= -17.0 && d <= -9.0, "ICE v %g: 880 Hz carrier %+.1f dB re the 440 Hz note, want -13 +/- 4", v, d);
		CHECK(iceAbs >= haarAbs + 20.0, "ICE v %g: 880 Hz %.1f dB, HAAR %.1f dB, want >= 20 dB above", v, iceAbs, haarAbs);
		CHECK(first - d <= 4.0, "ICE v %g: octave %+.1f dB, %.1f dB under noon's, want <= 4", v, d, first - d);
		std::printf("  ice_octave_audible_long v %.2f: octave %+.1f dB re note, %+.1f dB re HAAR's 880 Hz\n", v, d, iceAbs - haarAbs);
	}
}

// The shifter's high-pass keeps the bass out of the octave: a 30 Hz note's
// octave is at least 8 dB further under it than the median note's (with the
// two 80 Hz one-poles, 20 dB further; without them, 2 dB).
void iceBassKeptOut() {
	const double pitches[] = {110, 220, 440, 880};
	std::vector<double> d;
	for (double hz : pitches)
		d.push_back(octaveDb(rka::fog::ICE, hz));
	const double med = median(d), bass = octaveDb(rka::fog::ICE, 30.0);
	CHECK(bass <= med - 8.0, "ICE 30 Hz: octave %+.1f dB re note, median %+.1f dB: want >= 8 dB lower", bass, med);
	std::printf("  ice_bass_kept_out: 30 Hz octave %+.1f dB re note, against %+.1f dB at 110-880 Hz\n", bass, med);
}

// The energy held in a network's lines: every sample in their buffers, so
// the last 0.7-1.4 s written to each (a smooth measure of the tail; the two
// output taps of a single tone beat by +/-5 dB from window to window as the
// network's modes drift in and out of phase).
double lineEnergy(const Network& n) {
	double e = 0;
	for (int k = 0; k < n.n; k++)
		for (float x : n.line[k].buf)
			e += (double) x * x;
	return e;
}

// Least-squares slope (dB per second) of y (dB) sampled every dt seconds.
double fitSlope(const std::vector<double>& y, double dt) {
	double st = 0, sy = 0, stt = 0, sty = 0;
	const double n = (double) y.size();
	for (size_t i = 0; i < y.size(); i++) {
		const double t = (double) i * dt;
		st += t;
		sy += y[i];
		stt += t * t;
		sty += t * y[i];
	}
	return (n * sty - st * sy) / (n * stt - st * st);
}

// The decay (dB/s) of a network's tail after 10 s of a held input: the line
// energy every 2 s from 2 to 18 s into the silence, fitted by a straight
// line in dB. outSlope gets the output's (both sides, the energy over 1 s
// windows from 2 to 34 s, 32 of them, fitted the same way). The output of a
// single pure tone beats slowly as the network's modes drift in and out of
// phase at the taps: with ICE's shimmer entirely off, its 880 Hz output at
// Size 1 fitted -0.56 dB/s over 2-18 s while its lines fell at -0.75; over
// 2-34 s the two agree (-0.75).
// kind: 0 sine at hz, 1 saw at hz.
double tailSlope(int m, const Controls& c, float rate, int kind, double hz, double* outSlope = nullptr, double holdS = 10.0) {
	NetRig* rig = new NetRig(m, c, rate);
	float l, r;
	double ph = 0;
	for (int i = 0; i < (int) (holdS * rate); i++) {
		ph += hz / rate;
		ph -= std::floor(ph);
		const float x = kind == 0 ? (float) std::sin(2.0 * PI * ph) : (float) (2.0 * ph - 1.0);
		rig->net.process(x, x, l, r);
	}
	std::vector<double> state, out;
	for (int w = 0; w < 34; w++) {
		double sum = 0;
		for (int i = 0; i < (int) rate; i++) {
			rig->net.process(0.f, 0.f, l, r);
			sum += (double) l * l + (double) r * r;
		}
		if (w >= 2)
			out.push_back(10.0 * std::log10(sum + 1e-300));
		if (w >= 1 && w < 18 && (w & 1)) // every 2 s, from 2 to 18 s
			state.push_back(10.0 * std::log10(lineEnergy(rig->net) + 1e-300));
	}
	delete rig;
	if (outSlope)
		*outSlope = fitSlope(out, 1.0);
	return fitSlope(state, 2.0);
}

// ICE's tail obeys the T60 law on tones, where its octave is coherent with
// the note's own harmonics: the energy in its lines falls at least as fast,
// and so does its output, the feed-forward octave included (fitted over
// 32 one-second windows): sines at 110-3520 Hz (562 and 880 Hz among them),
// Visibility fully right, Size 0 and 1, Damp 0, Drift 0, at 48 and 96 kHz. Each tail falls at
// least as fast as the law allows at its slow end (T60 x 1.15, the spec's
// tolerance, as fog-soak uses).
void iceTonalDecay() {
	const float v = 1.f; // fully right: T60 80 s, the fewest losses a pass
	const double law = -60.0 / (1.15 * specT60(v));
	double worst = -1e9, worstOut = -1e9;
	char where[128] = "";
	for (float rate : {48000.f, 96000.f})
		for (float size : {0.f, 1.f})
			for (double hz : {110.0, 220.0, 440.0, 562.0, 880.0, 1760.0, 3520.0}) {
				Controls c;
				c.vis = v;
				c.size = size;
				c.damp = 0.f;
				c.drift = 0.f;
				double o = 0;
				const double s = tailSlope(rka::fog::ICE, c, rate, 0, hz, &o);
				CHECK(s <= law, "ICE %g kHz Size %g, %g Hz sine: tail falls %.3f dB/s, the law %.3f (T60 %.0f s, want <= %.0f)", rate / 1000,
				      size, hz, s, law, -60.0 / s, 1.15 * specT60(v));
				// The output, feed-forward octave included, follows the tail.
				CHECK(o <= law, "ICE %g kHz Size %g, %g Hz sine: output falls %.3f dB/s, the law %.3f (T60 %.0f s)", rate / 1000, size, hz, o,
				      law, -60.0 / o);
				if (s > worst) {
					worst = s;
					std::snprintf(where, sizeof where, "%g kHz, Size %g, %g Hz (output's fit %.3f)", rate / 1000, size, hz, o);
				}
				worstOut = o > worstOut ? o : worstOut;
			}
	std::printf("  ice_tonal_decay: slowest tail %.3f dB/s at %s (law at its slow end %.3f; slowest output fit %.3f)\n", worst, where,
	            law, worstOut);
}

// The in-loop crossfade never gains, whatever the phase between the octave
// and the note's own 2nd harmonic: a tone at f plus its 2nd harmonic at 24
// phases, through an OctaveShifter and shimmerCrossfade() at a = 0.5: in
// every 50 ms window (a grain; a loop pass is 16-500 ms) after the first
// 0.5 s, the output RMS is no more than the input's. The old a s - b m form
// (b = 0.6 a^2, i.e. (1 - b) m + a s) reached 1.15 there, where the octave
// lined up with the harmonic (0.97 over the whole 2 s, which hid it).
void shimmerCrossfadeNeverGains() {
	double worst = 0;
	double worstHz = 0, worstPh = 0;
	for (double hz : {100.0, 150.0, 200.0, 300.0, 440.0, 1000.0})
		for (int k = 0; k < 24; k++) {
			const double ph = 2.0 * PI * k / 24.0;
			rka::fog::OctaveShifter* sh = new rka::fog::OctaveShifter;
			sh->allocate(RATE, 0u);
			sh->configure(RATE, 7u);
			double sx = 0, sy = 0;
			int wn = 0;
			const int win = (int) (0.05 * RATE);
			for (int i = 0; i < (int) (2.5 * RATE); i++) {
				const double t = (double) i / RATE;
				const float x = (float) (std::sin(2.0 * PI * hz * t) + std::sin(2.0 * PI * 2.0 * hz * t + ph));
				const float y = rka::fog::shimmerCrossfade(x, sh->process(x), 0.5f);
				if (i >= (int) (0.5 * RATE)) {
					sx += (double) x * x;
					sy += (double) y * y;
					if (++wn == win) {
						const double r = std::sqrt(sy / sx);
						if (r > worst) {
							worst = r;
							worstHz = hz;
							worstPh = k * 15.0;
						}
						sx = sy = 0;
						wn = 0;
					}
				}
			}
			delete sh;
		}
	CHECK(worst <= 1.0, "crossfade output RMS %.4f x the input's in a 50 ms window at %g Hz, harmonic at %g deg: want <= 1", worst, worstHz,
	      worstPh);
	std::printf("  shimmer_crossfade_never_gains: worst 50 ms output/input RMS %.4f (%g Hz, harmonic at %g deg)\n", worst, worstHz, worstPh);
}

// ---- switching ----

// A held 110 Hz sine (3 V), Mix 1, Visibility noon: switching mode every 1 s
// through all four and back to HAAR. No output step exceeds twice the largest
// step in the 100 ms before it plus 0.05 V. Then, for each switch, with the
// input stopped just before it (Visibility FOG), the old tail is below
// -60 dB within 100 ms.
void modeSwitchClickFree() {
	{
		Rig* r = new Rig(rka::fog::HAAR);
		const int win = (int) (0.1 * RATE), sec = (int) RATE;
		std::vector<double> step;
		double prev = 0, worstRatio = 0;
		int bad = 0, firstBad = -1;
		const int total = 6 * sec;
		for (int i = 0; i < total; i++) {
			if (i >= sec && i % sec == 0)
				r->f.mode = (i / sec) % 4; // FRET, SMOKE, ICE, HAAR, FRET
			const FogResult o = r->step(3.f * (float) std::sin(2.0 * PI * 110.0 * i / RATE));
			step.push_back(std::fabs(o.outL - prev));
			prev = o.outL;
			if (i > sec / 2) {
				double m = 0;
				for (int k = i - win; k < i; k++)
					m = step[k] > m ? step[k] : m;
				const double lim = 2.0 * m + 0.05;
				worstRatio = std::fmax(worstRatio, step[i] / lim);
				if (step[i] > lim) {
					if (firstBad < 0)
						firstBad = i;
					bad++;
				}
			}
		}
		CHECK(bad == 0, "%d steps over the limit, first at %.4f s", bad, firstBad / RATE);
		std::printf("  mode_switch_click_free: worst step / limit %.3f over 5 switches\n", worstRatio);
		delete r;
	}
	for (int m = 0; m < 4; m++) {
		Rig* r = new Rig(m);
		r->f.params[FogFrame::VISIBILITY] = FOG_V;
		for (int i = 0; i < (int) (2 * RATE); i++)
			r->step(3.f * (float) std::sin(2.0 * PI * 110.0 * i / RATE));
		std::vector<float> before, after;
		for (int i = 0; i < (int) (0.05 * RATE); i++)
			before.push_back(r->step(0.f).outL); // the tail, input stopped
		r->f.mode = (m + 1) % 4;
		for (int i = 0; i < (int) (0.15 * RATE); i++) {
			const FogResult o = r->step(0.f);
			if (i >= (int) (0.1 * RATE))
				after.push_back(o.outL);
		}
		const double a = rms(before), b = rms(after);
		const double d = db((b + 1e-30) / a);
		CHECK(a > 0.1 && d <= -60.0, "%s -> %s: tail %g V RMS before, %+.1f dB 100 ms after, want <= -60", NAMES[m], NAMES[(m + 1) % 4], a, d);
		std::printf("  mode_switch_click_free %s -> %s: tail %.3f V before, %s 100 ms after\n", NAMES[m], NAMES[(m + 1) % 4], a,
		            b > 0 ? "below -60 dB" : "silent");
		delete r;
	}
}

// An engine already in mode m, as if it had run there from the start.
void startIn(Rig& r, int m) {
	r.f.mode = m;
	r.eng.mode = (rka::fog::Mode) m;
	r.eng.reset();
}

// A saved mode is there from the first sample: an engine whose first frame
// asks for ICE (as a patch loads) runs ICE at once, with no fade, sample for
// sample as one that was always in ICE.
void modeStartsSnapped() {
	Rig* a = new Rig(rka::fog::ICE);
	Rig* b = new Rig(rka::fog::ICE);
	startIn(*b, rka::fog::ICE);
	double worst = 0;
	bool snapped = true;
	uint32_t seed = 5u;
	for (int i = 0; i < (int) RATE; i++) {
		seed = seed * 1664525u + 1013904223u;
		const float x = 3.f * ((float) (seed >> 8) / 8388608.f - 1.f);
		const FogResult oa = a->step(x), ob = b->step(x);
		if (i == 0)
			snapped = a->eng.mode == rka::fog::ICE && a->eng.fade == 1.f && a->eng.fadeState == 0;
		worst = std::fmax(worst, std::fmax(std::fabs(oa.outL - ob.outL), std::fabs(oa.outR - ob.outR)));
	}
	CHECK(snapped, "first sample: mode %d, fade %g, state %d; want ICE, 1, 0", (int) a->eng.mode, a->eng.fade, a->eng.fadeState);
	CHECK(worst < 1e-6, "a saved ICE differs from an engine always in ICE by %g V", worst);
	std::printf("  mode_starts_snapped: in ICE from sample 0 (%s), worst difference %.3g V\n", snapped ? "yes" : "no", worst);
	delete a;
	delete b;
}

// A mode change while asleep takes effect, silently, and the engine stays
// asleep (its new network is clear): HAAR asleep, switch to ICE, 0.3 s of
// silence, then a noise burst: the same output as a fresh engine started in
// ICE given the same burst (Drift 0, 48 kHz: sample for sample).
void modeSwitchWhileAsleep() {
	Rig* a = new Rig(rka::fog::HAAR);
	Rig* b = new Rig(rka::fog::ICE);
	startIn(*b, rka::fog::ICE);
	for (Rig* r : {a, b})
		r->f.params[FogFrame::DRIFT] = 0.f;
	uint32_t seed = 9u;
	auto noise = [&]() {
		seed = seed * 1664525u + 1013904223u;
		return 3.f * ((float) (seed >> 8) / 8388608.f - 1.f);
	};
	for (int i = 0; i < (int) (0.5 * RATE); i++)
		a->step(noise());
	int guard = 0;
	while (!a->eng.asleep() && guard++ < (int) (60 * RATE))
		a->step(0.f);
	CHECK(a->eng.asleep(), "HAAR never slept");
	a->f.mode = rka::fog::ICE;
	double quietPeak = 0;
	for (int i = 0; i < (int) (0.3 * RATE); i++) {
		const FogResult o = a->step(0.f);
		quietPeak = std::fmax(quietPeak, std::fabs(o.outL));
	}
	CHECK(a->eng.mode == rka::fog::ICE, "after the switch while asleep, mode %d, want ICE", (int) a->eng.mode);
	CHECK(a->eng.asleep(), "the switch woke a silent engine");
	double worst = 0, peak = 0;
	for (int i = 0; i < (int) (3 * RATE); i++) {
		const float x = i < (int) (0.2 * RATE) ? noise() : 0.f;
		const FogResult oa = a->step(x), ob = b->step(x);
		worst = std::fmax(worst, std::fmax(std::fabs(oa.outL - ob.outL), std::fabs(oa.outR - ob.outR)));
		peak = std::fmax(peak, std::fabs(ob.outL));
	}
	CHECK(quietPeak == 0.0, "output %g V during the switch while asleep, want silence", quietPeak);
	CHECK(peak > 0.1 && worst < 2e-5, "after a switch while asleep the output differs from ICE's by %g V (peak %g)", worst, peak);
	std::printf("  mode_switch_while_asleep: silent through the switch, then ICE within %.3g V (peak %.2f V)\n", worst, peak);
	delete a;
	delete b;
}

// Rapid toggling: a held 110 Hz sine, the mode changed every 10 ms for 0.3 s
// (through all four, ending on SMOKE): no click by the rule above, and the
// latest choice wins (SMOKE, once the fades are done).
void modeRapidToggle() {
	Rig* r = new Rig(rka::fog::HAAR);
	const int win = (int) (0.1 * RATE), start = (int) RATE, every = (int) (0.01 * RATE), toggles = 30;
	std::vector<double> step;
	double prev = 0, worstRatio = 0;
	int bad = 0;
	const int total = 3 * (int) RATE;
	for (int i = 0; i < total; i++) {
		if (i >= start && i < start + toggles * every && (i - start) % every == 0) {
			const int k = (i - start) / every;
			r->f.mode = k == toggles - 1 ? rka::fog::SMOKE : (k * 3 + 1) % 4;
		}
		const FogResult o = r->step(3.f * (float) std::sin(2.0 * PI * 110.0 * i / RATE));
		step.push_back(std::fabs(o.outL - prev));
		prev = o.outL;
		if (i > (int) RATE / 2) {
			double m = 0;
			for (int k = i - win; k < i; k++)
				m = step[k] > m ? step[k] : m;
			const double lim = 2.0 * m + 0.05;
			worstRatio = std::fmax(worstRatio, step[i] / lim);
			bad += step[i] > lim;
		}
	}
	CHECK(bad == 0, "rapid toggling: %d steps over the limit", bad);
	CHECK(r->eng.mode == rka::fog::SMOKE && r->eng.fadeState == 0, "rapid toggling ended in mode %d (state %d), want SMOKE",
	      (int) r->eng.mode, r->eng.fadeState);
	std::printf("  mode_rapid_toggle: 30 changes in 0.3 s, worst step / limit %.3f, ends in %s\n", worstRatio, NAMES[(int) r->eng.mode]);
	delete r;
}

// A return to a mode starts it clean too: ICE's tail, input stopped, then
// HAAR, then back to ICE at once. From the moment ICE is back, the output is
// silence (nothing of the old ICE tail, which ICE's feed-forward shifters
// last held, replays).
void modeReturnStartsClean() {
	Rig* r = new Rig(rka::fog::ICE);
	r->f.params[FogFrame::VISIBILITY] = FOG_V;
	for (int i = 0; i < (int) (2 * RATE); i++)
		r->step(3.f * (float) std::sin(2.0 * PI * 440.0 * i / RATE));
	r->f.mode = rka::fog::HAAR;
	int guard = 0;
	while ((r->eng.mode != rka::fog::HAAR || r->eng.fadeState != 0) && guard++ < (int) RATE)
		r->step(0.f);
	r->f.mode = rka::fog::ICE;
	double peak = 0;
	bool back = false;
	for (int i = 0; i < (int) (0.3 * RATE); i++) {
		const FogResult o = r->step(0.f);
		back = back || r->eng.mode == rka::fog::ICE;
		if (back)
			peak = std::fmax(peak, std::fmax(std::fabs(o.outL), std::fabs(o.outR)));
	}
	CHECK(back && peak == 0.0, "back in ICE: output peak %g V, want silence", peak);
	std::printf("  mode_return_starts_clean: ICE -> HAAR -> ICE, peak after the return %g V\n", peak);
	delete r;
}

// ---- safety, every mode ----

// test_network's always_decays: v = 1, Damp 0; 10 s of noise (Drift 1), or
// of a 110 Hz saw or an 880 Hz sine (Drift 0: tones whose harmonics an
// octave shifter could reinforce coherently), then eight 5 s windows of
// silence, each with less energy than the one before, the last at least
// 20 dB under the first, all finite.
void alwaysDecays() {
	const char* inputs[3] = {"noise", "110 Hz saw", "880 Hz sine"};
	for (int m = 0; m < 4; m++)
		for (int kind = 0; kind < 3; kind++) {
		Controls c;
		c.vis = 1.f;
		c.damp = 0.f;
		c.drift = kind == 0 ? 1.f : 0.f; // tones without Drift: coherent
		NetRig* rig = new NetRig(m, c);
		float l, r;
		double ph = 0;
		const double hz = kind == 1 ? 110.0 : 880.0;
		for (int i = 0; i < (int) (10 * RATE); i++) {
			ph += hz / RATE;
			ph -= std::floor(ph);
			const float x = kind == 0 ? 2.f * white() : kind == 1 ? (float) (2.0 * ph - 1.0) : (float) std::sin(2.0 * PI * ph);
			rig->net.process(x, x, l, r);
		}
		double e[8];
		bool finite = true;
		for (int k = 0; k < 8; k++) {
			double s = 0;
			for (int i = 0; i < (int) (5 * RATE); i++) {
				rig->net.process(0.f, 0.f, l, r);
				finite = finite && std::isfinite(l) && std::isfinite(r);
				s += (double) l * l + (double) r * r;
			}
			e[k] = s;
		}
		delete rig;
		bool mono = true;
		for (int k = 1; k < 8; k++)
			mono = mono && e[k] < e[k - 1];
		const double drop = 10.0 * std::log10(e[7] / e[0]);
		CHECK(finite, "%s %s: non-finite output", NAMES[m], inputs[kind]);
		CHECK(mono, "%s %s: energy per 5 s window does not fall monotonically", NAMES[m], inputs[kind]);
		CHECK(drop <= -20.0, "%s %s: after 40 s the tail is only %g dB down", NAMES[m], inputs[kind], drop);
		std::printf("  all_modes_safe always_decays %-5s %-11s: last window %.1f dB below the first (law: %.1f dB)\n", NAMES[m],
		            inputs[kind], drop, -60.0 * 35.0 / specT60(1.0));
		}
}

// test_engine's poly_input_bounded: 16 channels of +/-5 V squares on each
// side, v = 1, Mix 1, 20 s: never above 10.5 V.
void polyInputBounded() {
	for (int m = 0; m < 4; m++) {
		Rig* r = new Rig(m);
		r->f.params[FogFrame::VISIBILITY] = 1.f;
		float vl[16], vr[16];
		r->f.inL.v = vl;
		r->f.inL.channels = 16;
		r->f.inR.v = vr;
		r->f.inR.channels = 16;
		double peak = 0;
		bool finite = true;
		for (int i = 0; i < (int) (20 * RATE); i++) {
			for (int c = 0; c < 16; c++) {
				const int p = 40 + 17 * c, p2 = 53 + 11 * c;
				vl[c] = ((i % p) < p / 2) ? 5.f : -5.f;
				vr[c] = ((i % p2) < p2 / 2) ? 5.f : -5.f;
			}
			FogResult o;
			r->eng.process(r->f, o);
			finite = finite && std::isfinite(o.outL) && std::isfinite(o.outR);
			peak = std::fmax(peak, std::fmax(std::fabs(o.outL), std::fabs(o.outR)));
		}
		CHECK(finite && peak <= 10.5, "%s: 16 ch squares, v = 1: peak %g V, want <= 10.5", NAMES[m], peak);
		std::printf("  all_modes_safe poly_input_bounded %-5s: peak %.3f V\n", NAMES[m], peak);
		delete r;
	}
}

} // namespace

int main() {
	const auto t0 = std::chrono::steady_clock::now();
	modesT60();
	smokeLateTail();
	earlyOnset();
	earlyStereo();
	modesKnobDirections();
	fretDarkerThanHaar();
	smokeHasEchoes();
	iceShimmer();
	iceOctaveEveryPitch();
	iceOctaveAudibleLong();
	shimmerCrossfadeNeverGains();
	iceBassKeptOut();
	iceTonalDecay();
	modeSwitchClickFree();
	modeStartsSnapped();
	modeSwitchWhileAsleep();
	modeRapidToggle();
	modeReturnStartsClean();
	alwaysDecays();
	polyInputBounded();
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  test_modes ran in %.1f s\n", secs);
	return rkatest::finish("test_modes");
}

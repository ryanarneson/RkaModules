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
#include "Voicing.hpp"
#include "test_measure.hpp"
#include "testing.hpp"
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
	NetRig(int m, const Controls& c) {
		net.allocate(RATE);
		net.configure(rka::fog::voicing((rka::fog::Mode) m), RATE);
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
// coupling, leaked its lows away there: 38 s for 70).
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

// The wet envelope of an impulse at Size 1 over its first 2 s (Visibility
// FOG, Damp noon, Drift 0): the RMS over 20 ms windows every 10 ms, in dB.
std::vector<double> echoEnvelope(int m) {
	Controls c;
	c.size = 1.f;
	c.drift = 0.f;
	const std::vector<float> ir = impulse(m, c, 2.0);
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
	for (size_t i = 1; i + 1 < e.size(); i++) {
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

// SMOKE is echo-led: at Size 1, at least 3 distinct repeats in the first 2 s,
// and more than HAAR shows. And they blur into the wash (the in-loop
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

// Power (Hann-windowed DFT, summed over centre +/- 40 Hz in 1 Hz steps). The
// band is wide because a granular shifter's output is a carrier with
// sidebands at its grain rate (20 Hz for 50 ms grains); for some input
// frequencies (220 Hz with 50 ms grains: its two taps, 25 ms apart, are 5.5
// cycles apart) the carrier cancels and the energy sits in the sidebands.
// +/-40 Hz is about +/-1.5 semitones at 440 Hz.
double bandPower(const std::vector<float>& x, double centre) {
	const size_t n = x.size();
	double total = 0;
	for (double f = centre - 40.0; f <= centre + 40.0 + 1e-9; f += 1.0) {
		const double dw = 2.0 * PI * f / RATE;
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

// ---- safety, every mode ----

// test_network's always_decays: v = 1, Damp 0, Drift 1; 10 s of noise, then
// eight 5 s windows of silence, each with less energy than the one before,
// the last at least 20 dB under the first, all finite.
void alwaysDecays() {
	for (int m = 0; m < 4; m++) {
		Controls c;
		c.vis = 1.f;
		c.damp = 0.f;
		c.drift = 1.f;
		NetRig* rig = new NetRig(m, c);
		float l, r;
		for (int i = 0; i < (int) (10 * RATE); i++) {
			const float x = 2.f * white();
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
		CHECK(finite, "%s: non-finite output", NAMES[m]);
		CHECK(mono, "%s: energy per 5 s window does not fall monotonically", NAMES[m]);
		CHECK(drop <= -20.0, "%s: after 40 s the tail is only %g dB down", NAMES[m], drop);
		std::printf("  all_modes_safe always_decays %-5s: last window %.1f dB below the first (law: %.1f dB)\n", NAMES[m], drop,
		            -60.0 * 35.0 / specT60(1.0));
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
	modesKnobDirections();
	fretDarkerThanHaar();
	smokeHasEchoes();
	iceShimmer();
	modeSwitchClickFree();
	alwaysDecays();
	polyInputBounded();
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  test_modes ran in %.1f s\n", secs);
	return rkatest::finish("test_modes");
}

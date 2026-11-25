// Rigging's engine (RiggingEngine.hpp): six strings in a chord, driven by
// the audio input and struck by STRUM or a new note, mixed to stereo.
// Pitches and levels in the mix are measured by a windowed Fourier sum at
// one frequency (as test_string.cpp), from the engine's outputs alone.
#include "testing.hpp"
#include "rigging/RiggingEngine.hpp"
#include "hadal/Random.hpp"

#include <cmath>
#include <cstdlib>
#include <complex>
#include <initializer_list>
#include <vector>

using rka::RiggingEngine;
using rka::RiggingFrame;
using rka::RiggingResult;
typedef RiggingFrame F;

namespace {

const double kTwoPi = 6.283185307179586, kFs = 48000.0, kC2 = 65.40639;

std::complex<double> bin(const std::vector<float>& x, double hz, long start, long n) {
	std::complex<double> s = 0.0;
	for (long i = 0; i < n; i++) {
		const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / n);
		const double ph = kTwoPi * hz * (start + i) / kFs;
		s += (double) x[start + i] * w * std::complex<double>(std::cos(ph), -std::sin(ph));
	}
	return s / (double) n;
}

double level(const std::vector<float>& x, double hz, long start, long n) {
	return std::abs(bin(x, hz, start, n));
}

double measureHz(const std::vector<float>& x, double guess, long start, long n) {
	const std::complex<double> a = bin(x, guess, start, n), b = bin(x, guess, start + n, n);
	return guess + std::arg(b * std::conj(a)) / (kTwoPi * n / kFs);
}

double cents(double hz, double want) {
	return 1200.0 * std::log2(hz / want);
}

double measureT60(const std::vector<float>& x, double hz, long start, double window, double gap) {
	const long n = std::lround(window * kFs), g = std::lround(gap * kFs);
	const double drop = 20.0 * std::log10(level(x, hz, start, n) / level(x, hz, start + g, n));
	return drop > 1e-9 ? 60.0 * gap / drop : 1e9;
}

// The design's chords, written out again: semitones and cents, or ratios.
double wantRatio(int chord, int k) {
	static const double semis[6][6] = {{0, 0, 0, 0, 0, 0}, {0, 12, 12, 24, 24, 36}, {0, 7, 12, 19, 24, 31},
	                                   {0, 7, 12, 15, 19, 24}, {0, 5, 7, 12, 17, 19}, {0, 1, 2, 3, 4, 5}};
	static const double cts[2][6] = {{-8, -4, 0, 3, 7, 11}, {0, -5, 5, -5, 5, 0}};
	static const double bell[6] = {1, 2, 2.4, 3, 4, 6};
	if (chord == 6)
		return bell[k];
	return std::pow(2.0, semis[chord][k] / 12.0 + (chord < 2 ? cts[chord][k] : 0.0) / 1200.0);
}

struct Rig {
	RiggingEngine e;
	F f;
	RiggingResult r;
	rka::hadal::Random noise;
	explicit Rig(uint32_t seed = 1) : e(seed) {
		e.setSampleRate((float) kFs);
		noise.seed(99);
	}
	void step() { e.process(f, r); }
	void run(double seconds) {
		const long n = std::lround(seconds * kFs);
		for (long i = 0; i < n; i++)
			step();
	}
	// Mono sum, and each side.
	std::vector<float> mono, left, right;
	void record(double seconds, bool withNoise = false, float volts = 5.f) {
		mono.clear();
		left.clear();
		right.clear();
		const long n = std::lround(seconds * kFs);
		for (long i = 0; i < n; i++) {
			if (withNoise) {
				f.inL = volts * noise.uniform();
				f.inLPatched = true;
			}
			step();
			mono.push_back(r.l + r.r);
			left.push_back(r.l);
			right.push_back(r.r);
		}
	}
	// A trigger at STRUM: high for 1 ms.
	void strum() {
		f.strumPatched = true;
		f.strum = 10.f;
		run(0.001);
		f.strum = 0.f;
	}
	static float peak(const std::vector<float>& x, long from = 0, long to = -1) {
		float p = 0.f;
		for (long i = from; i < (to < 0 ? (long) x.size() : to); i++)
			p = std::fabs(x[i]) > p ? std::fabs(x[i]) : p;
		return p;
	}
	static double rms(const std::vector<float>& x, long from, long to) {
		double s = 0.0;
		for (long i = from; i < to; i++)
			s += (double) x[i] * x[i];
		return std::sqrt(s / (to - from));
	}
};

// Ring's knob for a time in seconds (the design's law, inverted here).
float ringKnob(double seconds) {
	return (float) (std::log(seconds / 0.1) / std::log(600.0));
}

void chord_pitches() {
	for (int chord = 0; chord < 7; chord++) {
		for (int variant = 0; variant < (chord == 2 ? 3 : 1); variant++) {
			Rig g;
			g.f.params[F::CHORD] = (float) chord;
			g.f.params[F::RING] = ringKnob(15.0);
			g.f.params[F::DAMP] = 0.1f;
			g.f.params[F::SPREAD] = 0.f;
			double base = kC2;
			if (variant == 1) {
				g.f.voct = 1.f;
				base *= 2.0;
			}
			if (variant == 2) {
				g.f.params[F::TUNE] = -12.f;
				base *= 0.5;
			}
			g.run(0.2);
			g.strum();
			g.record(1.3);
			for (int k = 0; k < 6; k++) {
				const double want = base * wantRatio(chord, k);
				CHECK(std::fabs(cents(g.e.stringHz(k), want)) < 0.5, "%s string %d: the engine says %g Hz, want %g", rka::rigging::chordName(chord), k + 1,
				      g.e.stringHz(k), want);
				// Unison's and Octaves' strings are a few cents apart: too
				// close to tell apart in the mix; the string test covers tuning.
				if (chord < 2)
					continue;
				const long n = 24000;
				const double got = measureHz(g.mono, want, 4800, n);
				// (Loose: in most chords a lower string's overtone sits on this
				// pitch, and a string's overtones are a few cents off true.)
				CHECK(std::fabs(cents(got, want)) < 8.0, "%s string %d: %.3f Hz in the mix, want %.3f (%.1f cents)", rka::rigging::chordName(chord), k + 1,
				      got, want, cents(got, want));
			}
			CHECK(g.e.chord() == chord, "chord() is %d", g.e.chord());
		}
	}
	// Tune snaps to semitones unless the pitch is free.
	Rig g;
	g.f.params[F::TUNE] = 0.4f;
	g.run(0.2);
	CHECK(std::fabs(cents(g.e.stringHz(0), kC2)) < 0.5, "Tune 0.4 snapped: string 1 at %g Hz", g.e.stringHz(0));
	g.f.freePitch = true;
	g.run(0.2);
	CHECK(std::fabs(cents(g.e.stringHz(0), kC2) - 40.0) < 0.5, "Tune 0.4 free: %.1f cents above C2", cents(g.e.stringHz(0), kC2));
}

void drone() {
	Rig g;
	g.record(7.0, true);
	const float peak = Rig::peak(g.left, 5 * 48000);
	CHECK(peak > 2.2f && peak < 4.5f, "white noise at +-5 V in, defaults: peaks of %g V out (about 3)", peak);
	const double a = Rig::rms(g.left, 3 * 48000, 5 * 48000), b = Rig::rms(g.left, 5 * 48000, 7 * 48000);
	CHECK(std::fabs(20.0 * std::log10(a / b)) < 2.0, "the drone is not steady: %.3g then %.3g V RMS", a, b);
	double lo = 1e9, hi = 0.0;
	for (double t60 : {0.3, 1.0, 4.0, 15.0, 60.0}) {
		Rig h;
		h.f.params[F::RING] = ringKnob(t60);
		h.record(t60 * 0.8 + 6.0, true);
		const long n = (long) h.left.size();
		const double rms = Rig::rms(h.left, n - 4 * 48000, n);
		lo = std::min(lo, rms);
		hi = std::max(hi, rms);
	}
	CHECK(20.0 * std::log10(hi / lo) < 6.0, "the drone's level across Ring: %.3g to %.3g V RMS (%.1f dB)", lo, hi, 20.0 * std::log10(hi / lo));
	// Silence in, silence out (no drone of its own).
	Rig s;
	s.record(1.0);
	CHECK(Rig::peak(s.mono) == 0.f, "nothing patched and no strike: peak %g", Rig::peak(s.mono));
}

void strike_levels() {
	float alone = 0.f;
	for (int k = 0; k < 6; k++) {
		Rig g;
		for (int j = 0; j < 6; j++)
			g.f.params[F::LEVEL_1 + j] = j == k ? 1.f : 0.f;
		g.run(0.1);
		g.strum();
		g.record(1.0);
		const float p = std::max(Rig::peak(g.left), Rig::peak(g.right));   // its louder side
		CHECK(p > 1.2f && p < 4.5f, "string %d struck alone: peaks %g V left, %g V right", k + 1, Rig::peak(g.left), Rig::peak(g.right));
		alone = std::max(alone, std::max(Rig::peak(g.left), Rig::peak(g.right)));
	}
	Rig g;
	g.f.params[F::SPREAD] = 0.f;
	g.run(0.1);
	g.strum();
	g.record(1.0);
	const float six = std::max(Rig::peak(g.left), Rig::peak(g.right));
	CHECK(six > 4.f && six < 9.f, "all six struck at once: peak %g V", six);
	// A strike is as loud at its start whatever Ring is.
	double first[2];
	int i = 0;
	for (double t60 : {1.0, 60.0}) {
		Rig h;
		h.f.params[F::RING] = ringKnob(t60);
		h.f.params[F::SPREAD] = 0.f;
		h.run(0.1);
		h.strum();
		h.record(0.1);
		first[i++] = Rig::rms(h.mono, 480, 2880);
	}
	CHECK(std::fabs(20.0 * std::log10(first[0] / first[1])) < 3.0, "a strike's first 50 ms: %.3g V RMS at Ring 1 s, %.3g at 60 s", first[0], first[1]);
}

// Cluster, two octaves up: six fundamentals a semitone apart (15 Hz and
// more), and no string's overtone on another's fundamental (in Fifths
// string 1's second harmonic is string 3's pitch).
const double kC4 = 4.0 * kC2;
void cluster(Rig& g) {
	g.f.params[F::CHORD] = 5.f;
	g.f.params[F::TUNE] = 24.f;
}

void levels() {
	for (int k = 0; k < 6; k++) {
		Rig g;
		cluster(g);
		g.f.params[F::RING] = ringKnob(15.0);
		g.f.params[F::SPREAD] = 0.f;
		g.f.params[F::LEVEL_1 + k] = 0.f;
		g.run(0.2);
		g.strum();
		g.record(1.0);
		double others = 1e9, mine = 0.0;
		for (int j = 0; j < 6; j++) {
			const double l = level(g.mono, kC4 * wantRatio(5, j), 9600, 24000);
			if (j == k)
				mine = l;
			else
				others = std::min(others, l);
		}
		CHECK(others > 0.01, "level %d at 0 took another string with it (%.3g)", k + 1, others);
		// (The window's own leakage from its neighbours is about a thousandth.)
		CHECK(mine < 5e-3 * others, "level %d at 0: its fundamental is at %.3g, the others' at %.3g or more", k + 1, mine, others);
		// Brought up mid-ring, the string is there (it was ringing all along), with no step.
		const float before = Rig::peak(g.mono, 43200) ;
		g.f.params[F::LEVEL_1 + k] = 1.f;
		std::vector<float> was = g.mono;
		g.record(0.6);
		double step = std::fabs(g.mono[0] - was.back());
		for (size_t i = 1; i < 4800; i++)
			step = std::max(step, (double) std::fabs(g.mono[i] - g.mono[i - 1]));
		double usual = 0.0;
		for (size_t i = was.size() - 4800; i < was.size(); i++)
			usual = std::max(usual, (double) std::fabs(was[i] - was[i - 1]));
		CHECK(level(g.mono, kC4 * wantRatio(5, k), 4800, 24000) > 0.01, "level %d raised mid-ring: the string is not there", k + 1);
		CHECK(step < 3.0 * usual + 0.02, "level %d raised mid-ring: a step of %.3g (usual %.3g, peak %g)", k + 1, step, usual, before);
	}
	// Every level at 0: exact silence from the strings, strike or not.
	Rig z;
	for (int j = 0; j < 6; j++)
		z.f.params[F::LEVEL_1 + j] = 0.f;
	z.run(0.5);
	z.strum();
	z.record(1.0, true);
	CHECK(Rig::peak(z.mono) == 0.f, "every level at 0: peak %g", Rig::peak(z.mono));
}

void input_routing() {
	std::vector<float> outs[2];
	for (int side = 0; side < 2; side++) {
		Rig g;
		g.f.inLPatched = side == 0;
		g.f.inRPatched = side == 1;
		rka::hadal::Random rng;
		rng.seed(5);
		for (long i = 0; i < 48000; i++) {
			const float v = 5.f * rng.uniform();
			g.f.inL = side == 0 ? v : 0.f;
			g.f.inR = side == 1 ? v : 0.f;
			g.step();
			outs[side].push_back(g.r.l);
			outs[side].push_back(g.r.r);
		}
	}
	CHECK(outs[0] == outs[1] && Rig::peak(outs[0]) > 0.5f, "one cable in IN L and the same in IN R give different outputs");
	// Both patched: strings 1, 3, 5 follow L, strings 2, 4, 6 follow R.
	for (int silent = 0; silent < 2; silent++) {
		Rig g;
		cluster(g);
		g.f.inLPatched = g.f.inRPatched = true;
		g.f.params[F::RING] = ringKnob(15.0);
		rka::hadal::Random rng;
		rng.seed(5);
		g.mono.clear();
		for (long i = 0; i < 4 * 48000; i++) {
			const float v = 5.f * rng.uniform();
			g.f.inL = silent == 0 ? 0.f : v;
			g.f.inR = silent == 1 ? 0.f : v;
			g.step();
			g.mono.push_back(g.r.l + g.r.r);
		}
		double fedLeast = 1e9, unfedMost = 0.0;
		for (int k = 0; k < 6; k++) {
			const double l = level(g.mono, kC4 * wantRatio(5, k), 96000, 48000);
			const bool fed = (k % 2 == 0) != (silent == 0);
			if (fed)
				fedLeast = std::min(fedLeast, l);
			else
				unfedMost = std::max(unfedMost, l);
		}
		CHECK(fedLeast > 0.005 && unfedMost < 0.05 * fedLeast, "IN %s silent: its strings at %.3g or less, the others at %.3g or more", silent == 0 ? "L" : "R",
		      unfedMost, fedLeast);
	}
}

void stereo() {
	int side[6];
	for (int k = 0; k < 6; k++) {
		Rig g;
		for (int j = 0; j < 6; j++)
			g.f.params[F::LEVEL_1 + j] = j == k ? 1.f : 0.f;
		cluster(g);
		g.f.params[F::RING] = ringKnob(15.0);
		g.run(0.1);
		g.strum();
		g.record(1.0);
		const double hz = kC4 * wantRatio(5, k);
		const double l = level(g.left, hz, 9600, 24000), r = level(g.right, hz, 9600, 24000), m = level(g.mono, hz, 9600, 24000);
		side[k] = l > r * 1.05 ? -1 : r > l * 1.05 ? 1 : 0;
		CHECK(std::fabs(20.0 * std::log10(m / (l + r))) < 1.0, "string %d: L + R is %.3g, its sides %.3g and %.3g", k + 1, m, l, r);
	}
	CHECK(side[0] == 0, "string 1 is not centred");
	for (int k = 1; k < 6; k++)
		CHECK(side[k] != 0 && (k == 1 || side[k] == -side[k - 1]), "string %d is %s", k + 1, side[k] == 0 ? "centred" : "on the same side as the one below");
}

void mix() {
	// Dry: the input, with the normalling, untouched.
	Rig g;
	g.f.params[F::MIX] = 0.f;
	g.f.inLPatched = true;
	bool same = true;
	g.run(0.1);
	for (int i = 0; i < 4800; i++) {
		g.f.inL = 3.f * std::sin(0.05f * i);
		g.step();
		same = same && std::fabs(g.r.l - g.f.inL) < 1e-4f && std::fabs(g.r.r - g.f.inL) < 1e-4f;
	}
	CHECK(same, "Mix fully dry does not pass the input to both outputs");
	// Halfway: both at -3 dB.
	Rig full, half;
	half.f.params[F::MIX] = 0.5f;
	full.record(4.0, true);
	half.noise.seed(99);
	half.f.params[F::LEVEL_1] = 1.f;
	half.record(4.0, true);
	// The strings' part of the half mix: the half mix less the dry at -3 dB.
	rka::hadal::Random rng;
	rng.seed(99);
	double sw = 0.0, sf = 0.0;
	for (size_t i = 0; i < full.left.size(); i++) {
		const float in = 5.f * rng.uniform();
		if (i < 96000)
			continue;
		const double wet = half.left[i] - 0.70710678 * in;
		sw += wet * wet;
		sf += (double) full.left[i] * full.left[i];
	}
	CHECK(std::fabs(10.0 * std::log10(sw / sf) + 3.0) < 0.3, "Mix at half: the strings are at %.2f dB, want -3", 10.0 * std::log10(sw / sf));
}

// String 1 alone at C4 (26 periods in the shortest Ring); the windows fit
// the ring expected.
double strikeT60(float ringKnobValue, float ringCv, double window, double gap) {
	Rig g;
	g.f.params[F::TUNE] = 24.f;
	g.f.params[F::RING] = ringKnobValue;
	g.f.ringCv = ringCv;
	g.f.params[F::DAMP] = 0.f;
	g.f.params[F::CHORD] = 2.f;
	for (int j = 1; j < 6; j++)
		g.f.params[F::LEVEL_1 + j] = 0.f;
	g.run(0.1);
	g.strum();
	g.record(gap + 2.0 * window + 0.05);
	return measureT60(g.mono, kC4, 480, window, gap);
}

double strikeCentroid(float dampKnob, float dampCv) {
	Rig g;
	g.f.params[F::DAMP] = dampKnob;
	g.f.dampCv = dampCv;
	for (int j = 1; j < 6; j++)
		g.f.params[F::LEVEL_1 + j] = 0.f;
	g.run(0.1);
	g.strum();
	g.record(0.6);
	double num = 0.0, den = 0.0;
	for (int h = 1; h < 60; h++) {
		const double m = level(g.mono, h * kC2, 9600, 14400);
		num += h * m * m;
		den += m * m;
	}
	return num / den;
}

void cv() {
	const double up = strikeT60(0.5f, 5.f, 0.3, 1.8), top = strikeT60(1.f, 0.f, 0.3, 1.8);
	const double down = strikeT60(0.5f, -5.f, 0.02, 0.03), bottom = strikeT60(0.f, 0.f, 0.02, 0.03);
	CHECK(std::fabs(up / top - 1.0) < 0.05 && top > 40.0 && top < 75.0, "Ring 0.5 with +5 V rings %.3g s, Ring 1 rings %.3g", up, top);
	CHECK(std::fabs(down / bottom - 1.0) < 0.05 && bottom > 0.07 && bottom < 0.13, "Ring 0.5 with -5 V rings %.3g s, Ring 0 rings %.3g", down, bottom);
	const double dark = strikeCentroid(0.5f, 5.f), darkest = strikeCentroid(1.f, 0.f), bright = strikeCentroid(0.5f, -5.f), brightest = strikeCentroid(0.f, 0.f);
	CHECK(std::fabs(dark / darkest - 1.0) < 0.02 && std::fabs(bright / brightest - 1.0) < 0.02 && brightest > 3.0 * darkest,
	      "Damp CV: centroids %.3g and %.3g (knob 1: %.3g, knob 0: %.3g)", dark, bright, darkest, brightest);
}

double largestStep(const std::vector<float>& x, long from, long to) {
	double m = 0.0;
	for (long i = from + 1; i < to; i++)
		m = std::max(m, (double) std::fabs(x[i] - x[i - 1]));
	return m;
}

void chord_cv_no_chatter() {
	Rig g;
	g.f.params[F::CHORD] = 2.f;
	g.f.params[F::DAMP] = 0.7f;   // a smooth drone, so a click would show
	g.f.params[F::RING] = ringKnob(15.0);
	g.record(3.0, true);
	// CV resting on the boundary to the next chord, with a little noise.
	int changes = 0, held = g.e.chord();
	rka::hadal::Random rng;
	rng.seed(8);
	for (long i = 0; i < 96000; i++) {
		g.f.chordCv = (0.5f + 0.03f * rng.uniform()) / 0.7f;
		g.f.inL = 0.f;
		g.step();
		changes += g.e.chord() != held ? 1 : 0;
		held = g.e.chord();
	}
	CHECK(changes <= 1, "Chord CV on a boundary: the chord changed %d times", changes);
	// A chord change mid-ring glides.
	Rig h;
	h.f.params[F::CHORD] = 2.f;
	h.f.params[F::DAMP] = 0.7f;
	h.f.params[F::RING] = ringKnob(15.0);
	h.record(3.0, true);
	h.f.inLPatched = false;
	h.f.inL = 0.f;
	h.record(0.5);
	const double before = largestStep(h.mono, 24000 - 2400, 24000);
	const float last = h.mono.back();
	h.f.params[F::CHORD] = 3.f;
	h.record(0.2);
	const double during = std::max(largestStep(h.mono, 0, 9600), (double) std::fabs(h.mono[0] - last));
	CHECK(h.e.chord() == 3, "the chord did not change");
	CHECK(during < 2.0 * before, "a chord change mid-ring: a step of %.4g, %.4g before it", during, before);
}

// Damp stepped while an input plays through the strings: no click. (The
// loop's low-pass and the input's share once jumped at the control tick:
// steps of up to 3.9 V on a sine whose own steps are 0.2 V.) Several
// instants in the sine's cycle, from Damp 1 and from Damp 0.33 to 0.
void damp_step_with_input() {
	for (float from : {1.f, 0.33f}) {
		Rig g;
		g.f.params[F::DAMP] = from;
		g.f.strumPatched = true;
		g.f.inLPatched = true;
		const double hz = 130.81;
		long t = 0;
		auto play = [&](double seconds, std::vector<float>* out) {
			const long n = std::lround(seconds * kFs);
			for (long i = 0; i < n; i++, t++) {
				g.f.inL = 5.f * (float) std::sin(kTwoPi * hz * t / kFs);
				g.step();
				if (out)
					out->push_back(g.r.l);
			}
		};
		play(5.0, nullptr);
		double worst = 0.0, steadyWorst = 0.0;
		for (int instant = 0; instant < 8; instant++) {
			g.f.params[F::DAMP] = from;
			play(1.0 + instant * 0.00097, nullptr);
			std::vector<float> steady, after;
			play(1.0, &steady);
			const double s = largestStep(steady, 0, (long) steady.size());
			g.f.params[F::DAMP] = 0.f;
			play(0.1, &after);
			const double a = std::max(largestStep(after, 0, (long) after.size()), (double) std::fabs(after[0] - steady.back()));
			worst = std::max(worst, a / s);
			steadyWorst = std::max(steadyWorst, s);
			CHECK(a < 3.0 * s, "Damp %g to 0 with a 130.81 Hz sine in (instant %d): a step of %.3g V, %.3g V steady", from, instant, a, s);
		}
		std::printf("Damp %g to 0 with a sine in: worst step %.2f times the steady step (%.3g V)\n", from, worst, steadyWorst);
	}
}

// Strikes counted by the engine over `samples`, with V/OCT set at the given
// samples. (The light stays on 0.1 s, so it cannot count close strikes.)
int strikesIn(Rig& g, long samples, const std::vector<std::pair<long, float>>& voctAt) {
	const long before = g.e.strikes();
	size_t next = 0;
	for (long i = 0; i < samples; i++) {
		while (next < voctAt.size() && voctAt[next].first == i)
			g.f.voct = voctAt[next++].second;
		g.step();
	}
	return (int) (g.e.strikes() - before);
}

void note_strikes() {
	const float semi = 1.f / 12.f;
	{
		Rig g;
		g.run(0.3);
		CHECK(strikesIn(g, 24000, {{100, semi}}) == 1, "a semitone step on V/OCT, STRUM unpatched: not one strike");
		g.record(0.2);
		CHECK(Rig::peak(g.mono) > 0.5f, "a note strike makes no sound");
	}
	{
		Rig g;
		g.run(0.3);
		std::vector<std::pair<long, float>> steps;
		for (int i = 0; i < 48; i++)
			steps.push_back({100 + i, 2.f * (i + 1) / 48.f});   // 2 V over 1 ms
		CHECK(strikesIn(g, 24000, steps) == 1, "a 2 V edge 1 ms long: not one strike");
	}
	{
		Rig g;
		g.run(0.3);
		std::vector<std::pair<long, float>> ramp;
		for (long i = 0; i < 96000; i++)
			ramp.push_back({i, (float) i / 48000.f});   // 1 V a second
		CHECK(strikesIn(g, 96000, ramp) == 0, "a 1 V/s ramp struck");
	}
	{
		Rig g;
		g.run(0.3);
		CHECK(strikesIn(g, 24000, {{100, 0.2f * semi}}) == 0, "a fifth of a semitone struck");
	}
	{
		Rig g;
		g.run(0.3);
		CHECK(strikesIn(g, 24000, {{100, semi}, {100 + 240, 2.f * semi}}) == 1, "two notes 5 ms apart: not one strike");
		Rig h;
		h.run(0.3);
		// The light stays on 0.1 s: count strikes by the sound's restarts instead.
		h.f.voct = 0.f;
		h.run(0.01);
		h.f.voct = semi;
		h.run(0.02);
		h.f.voct = 2.f * semi;
		h.run(0.3);
		CHECK(h.e.strikes() == 2, "two notes 20 ms apart: %ld strikes", h.e.strikes());
	}
	// A note that slews to its pitch (a sequencer with glide, a slew
	// limiter) is one note: no second strike while V/OCT is still moving.
	for (const auto& k : std::initializer_list<std::pair<float, double>>{{1.f, 0.015}, {2.f, 0.04}, {1.f, 0.005}, {3.f, 0.05}}) {
		Rig g;
		g.run(0.3);
		std::vector<std::pair<long, float>> slew;
		const long n = std::lround(k.second * kFs);
		for (long i = 0; i <= n; i++)
			slew.push_back({100 + i, k.first * (float) i / (float) n});
		const int got = strikesIn(g, 24000, slew);
		CHECK(got == 1, "%g V slewed over %g ms: %d strikes", k.first, k.second * 1000.0, got);
	}
	{
		Rig g;
		g.f.strumPatched = true;
		g.run(0.3);
		CHECK(strikesIn(g, 24000, {{100, 1.f}}) == 0 && g.e.strikes() == 0, "with STRUM patched a new note struck");
	}
	{
		Rig g;
		g.run(0.3);
		g.f.params[F::TUNE] = 5.f;
		g.run(0.2);
		g.f.params[F::CHORD] = 4.f;
		g.run(0.2);
		CHECK(g.e.strikes() == 0, "Tune or Chord struck");
	}
}

void strum_edges() {
	Rig g;
	g.f.strumPatched = true;
	g.run(0.1);
	g.f.strum = 0.9f;
	g.run(0.05);
	CHECK(g.e.strikes() == 0, "0.9 V struck");
	g.f.strum = 1.f;
	g.step();
	CHECK(g.e.strikes() == 1 && g.r.light, "1 V did not strike");
	g.run(0.5);
	CHECK(g.e.strikes() == 1 && !g.r.light, "a held gate struck again, or the light stayed on");
	g.f.strum = 0.2f;
	g.run(0.01);
	g.f.strum = 5.f;
	g.run(0.01);
	CHECK(g.e.strikes() == 1, "0.2 V re-armed the trigger");
	g.f.strum = 0.05f;
	g.run(0.01);
	g.f.strum = 5.f;
	long lit = 0;
	for (int i = 0; i < 24000; i++) {
		g.step();
		lit += g.r.light ? 1 : 0;
	}
	CHECK(g.e.strikes() == 2, "a second trigger did not strike");
	CHECK(std::labs(lit - 4800) <= 16, "the light was on for %ld samples, want 4800", lit);
	g.f.strum = NAN;
	g.run(0.05);
	g.f.strum = 0.f;
	g.run(0.05);
	CHECK(g.e.strikes() == 2, "NaN on STRUM struck");
}

void clamps() {
	Rig g;
	for (float v : {10.f, -10.f}) {
		g.f.voct = v;
		g.f.strumPatched = true;
		g.record(0.5, true);
		bool in = true, finite = true;
		for (int k = 0; k < 6; k++)
			in = in && g.e.stringHz(k) >= 8.f && g.e.stringHz(k) <= 12000.f;
		for (float x : g.mono)
			finite = finite && std::isfinite(x);
		CHECK(in && finite, "V/OCT at %g V: string 1 at %g Hz, string 6 at %g Hz", v, g.e.stringHz(0), g.e.stringHz(5));
	}
	g.f.voct = 0.f;
	g.run(0.2);
	for (int k = 0; k < 6; k++)
		CHECK(std::fabs(cents(g.e.stringHz(k), kC2 * wantRatio(2, k))) < 0.5, "0.2 s after V/OCT returned: string %d at %g Hz", k + 1, g.e.stringHz(k));
}

void nan_inputs() {
	for (float bad : {NAN, INFINITY, -INFINITY}) {
		Rig g;
		g.f.inLPatched = g.f.inRPatched = true;
		bool finite = true;
		for (long i = 0; i < 48000; i++) {
			g.f.inL = g.f.inR = g.f.voct = g.f.chordCv = g.f.ringCv = g.f.dampCv = g.f.strum = i % 3 == 0 ? bad : 1.f;
			g.step();
			finite = finite && std::isfinite(g.r.l) && std::isfinite(g.r.r) && std::fabs(g.r.l) <= 10.f && std::fabs(g.r.r) <= 10.f;
		}
		CHECK(finite, "%g on every input: the output is not finite and within 10 V", bad);
	}
	// A bad value reads as 0 V: the same output as 0 V.
	Rig a, b;
	a.f.ringCv = NAN;
	a.f.dampCv = INFINITY;
	a.f.chordCv = NAN;
	a.f.voct = NAN;
	a.record(0.5, true);
	b.record(0.5, true);
	CHECK(a.mono == b.mono, "NaN and inf on the CVs do not read as 0 V");
}

void no_dc() {
	Rig g;
	g.f.inLPatched = true;
	g.f.inL = 5.f;
	g.run(9.0);
	double mean = 0.0;
	for (long i = 0; i < 48000; i++) {
		g.step();
		mean += g.r.l;
	}
	CHECK(std::fabs(mean / 48000.0) < 1e-3, "5 V DC in for 10 s: %.3g V DC out", mean / 48000.0);
	// Unipolar noise (0 to 10 V) drones about zero.
	Rig u;
	u.f.inLPatched = true;
	rka::hadal::Random rng;
	rng.seed(4);
	mean = 0.0;
	for (long i = 0; i < 6 * 48000; i++) {
		u.f.inL = 5.f + 5.f * rng.uniform();
		u.step();
		if (i >= 4 * 48000)
			mean += u.r.l;
	}
	CHECK(std::fabs(mean / 96000.0) < 0.02, "noise on a 5 V offset: %.3g V DC out", mean / 96000.0);
}

// Initialize in the middle of a rake: nothing more is struck, and silence.
void reset_cancels_a_rake() {
	Rig g;
	g.f.params[F::SPREAD] = 1.f;
	g.run(0.1);
	g.strum();
	g.run(0.4);          // strings 1 and 2 struck; four to come
	g.e.reset();
	g.record(1.5);
	CHECK(Rig::peak(g.mono) == 0.f, "after a reset mid-rake the output peaks at %g V", Rig::peak(g.mono));
}

// Many strikes on high strings, then a level turned down in the silence
// after: no thump. (DC a string held was once stepped by its level knob:
// 4.5 V.)
void no_thump_from_a_level() {
	Rig g;
	g.f.voct = 5.f;
	g.f.strumPatched = true;
	g.f.params[F::RING] = ringKnob(1.0);
	g.f.params[F::SPREAD] = 0.f;
	g.run(0.1);
	for (int strike = 0; strike < 600; strike++) {
		g.f.strum = 10.f;
		g.run(0.001);
		g.f.strum = 0.f;
		g.run(0.049);
	}
	g.record(5.0);
	CHECK(Rig::peak(g.mono, 4 * 48000) < 0.01f, "5 s after the strikes the output still peaks at %g V", Rig::peak(g.mono, 4 * 48000));
	g.f.params[F::LEVEL_4] = 0.f;
	g.f.params[F::LEVEL_5] = 0.f;
	g.record(1.0);
	CHECK(Rig::peak(g.mono) < 0.02f, "levels 4 and 5 turned down in the silence: a thump of %g V", Rig::peak(g.mono));
	g.f.voct = 0.f;
	g.record(1.0);
	CHECK(Rig::peak(g.mono) < 0.1f, "V/OCT returned to 0 in the silence: a thump of %g V", Rig::peak(g.mono));
}

void laws() {
	// The laws' inverses, for a value typed into the tooltip.
	for (float k : {0.f, 0.1f, 0.5f, 0.9f, 1.f}) {
		CHECK(std::fabs(RiggingEngine::ringKnob(RiggingEngine::ringSeconds(k)) - k) < 1e-4f, "Ring's law does not invert at %g", k);
		CHECK(std::fabs(RiggingEngine::spreadKnob(RiggingEngine::spreadSeconds(k)) - k) < 1e-4f, "Spread's law does not invert at %g", k);
	}
	CHECK(RiggingEngine::ringKnob(1e9f) == 1.f && RiggingEngine::ringKnob(0.f) == 0.f && RiggingEngine::ringKnob(NAN) == 0.f &&
	      RiggingEngine::spreadKnob(99.f) == 1.f && RiggingEngine::spreadKnob(-1.f) == 0.f, "the inverses clamp");

	CHECK(std::fabs(RiggingEngine::ringSeconds(0.f) - 0.1f) < 1e-4f && std::fabs(RiggingEngine::ringSeconds(1.f) / 60.f - 1.f) < 1e-3f, "Ring's law: %g to %g s",
	      RiggingEngine::ringSeconds(0.f), RiggingEngine::ringSeconds(1.f));
	CHECK(std::fabs(RiggingEngine::ringSeconds(ringKnob(4.0)) / 4.f - 1.f) < 1e-3f, "Ring's law at 4 s");
	CHECK(RiggingEngine::spreadSeconds(0.f) == 0.f && std::fabs(RiggingEngine::spreadSeconds(1.f) - 1.5f) < 1e-5f &&
	      std::fabs(RiggingEngine::spreadSeconds(0.5f) - 0.1875f) < 1e-5f, "Spread's law");
	F f;
	CHECK(std::fabs(RiggingEngine::ringSeconds(f.params[F::RING]) / 4.f - 1.f) < 0.02f, "Ring's default is %g s", RiggingEngine::ringSeconds(f.params[F::RING]));
	CHECK(std::fabs(RiggingEngine::spreadSeconds(f.params[F::SPREAD]) / 0.06f - 1.f) < 0.05f, "Spread's default is %g s", RiggingEngine::spreadSeconds(f.params[F::SPREAD]));
	CHECK(f.params[F::CHORD] == 2.f && f.params[F::MIX] == 1.f && f.params[F::LEVEL_6] == 1.f && std::fabs(f.params[F::DAMP] - 0.33f) < 0.01f && f.params[F::TUNE] == 0.f,
	      "the frame's defaults");
}

} // namespace

int main() {
	laws();
	chord_pitches();
	drone();
	strike_levels();
	levels();
	input_routing();
	stereo();
	mix();
	cv();
	chord_cv_no_chatter();
	damp_step_with_input();
	note_strikes();
	strum_edges();
	reset_cancels_a_rake();
	no_thump_from_a_level();
	clamps();
	nan_inputs();
	no_dc();
	return rkatest::finish("rigging test_engine");
}

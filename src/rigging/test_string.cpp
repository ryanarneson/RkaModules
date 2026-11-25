// One Rigging string (String.hpp): in tune, ringing as long as Ring says,
// darkened by Damp, gliding to a new pitch, bounded.
// Pitch and level are measured here by a windowed Fourier sum at one
// frequency: the phase it gains between two windows gives the frequency,
// the level it loses gives the decay. Neither uses anything of the string's.
#include "testing.hpp"
#include "rigging/String.hpp"
#include "hadal/Random.hpp"

#include <cmath>
#include <cstdlib>
#include <complex>
#include <initializer_list>
#include <vector>

using rka::rigging::String;

namespace {

const double kTwoPi = 6.283185307179586;

// Hann-windowed Fourier sum of x[start .. start + n) at hz.
std::complex<double> bin(const std::vector<float>& x, double fs, double hz, long start, long n) {
	std::complex<double> s = 0.0;
	for (long i = 0; i < n; i++) {
		const double w = 0.5 - 0.5 * std::cos(kTwoPi * (i + 0.5) / n);
		const double ph = kTwoPi * hz * (start + i) / fs;
		s += (double) x[start + i] * w * std::complex<double>(std::cos(ph), -std::sin(ph));
	}
	return s / (double) n;
}

// The frequency near `guess` (within 2 %), from the phase gained between two
// windows of `periods` periods, the second starting `periods` after the first.
double measureHz(const std::vector<float>& x, double fs, double guess, long start, int periods = 20) {
	const long n = std::lround(periods * fs / guess);
	const std::complex<double> a = bin(x, fs, guess, start, n), b = bin(x, fs, guess, start + n, n);
	const double dphi = std::arg(b * std::conj(a));
	return guess + dphi / (kTwoPi * n / fs);
}

double cents(double hz, double want) {
	return 1200.0 * std::log2(hz / want);
}

// Seconds to fall 60 dB, from the level at hz in two windows `gap` seconds apart.
double measureT60(const std::vector<float>& x, double fs, double hz, long start, double window, double gap) {
	const long n = std::lround(window * fs), g = std::lround(gap * fs);
	const double a = std::abs(bin(x, fs, hz, start, n)), b = std::abs(bin(x, fs, hz, start + g, n));
	const double drop = 20.0 * std::log10(a / b);
	return drop > 1e-9 ? 60.0 * gap / drop : 1e9;
}

// Strike: one period of noise into the string.
void strike(String& s, uint32_t seed = 7, float level = 1.f) {
	rka::hadal::Random rng;
	rng.seed(seed);
	const long n = std::lround(s.periodSamples());
	for (long i = 0; i < n; i++)
		s.process(level * rng.uniform());
}

std::vector<float> run(String& s, double seconds, double fs) {
	std::vector<float> x((size_t) std::lround(seconds * fs));
	for (float& v : x)
		v = s.process(0.f);
	return x;
}

String make(float fs, float hz, float ring, float damp) {
	String s;
	s.setSampleRate(fs);
	s.setFrequency(hz);
	s.setRing(ring);
	s.setDamp(damp);
	s.snap();
	return s;
}

void tuning() {
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (float damp : {0.f, 0.5f, 1.f})
			for (float hz : {30.f, 65.406f, 220.f, 880.f, 2000.f}) {
				String s = make(fs, hz, 10.f, damp);
				strike(s);
				const std::vector<float> x = run(s, 45.0 / hz + 0.01, fs);
				const double got = measureHz(x, fs, hz, std::lround(4.0 * fs / hz));
				CHECK(std::fabs(cents(got, hz)) < 3.0, "%g Hz at %g Hz rate, Damp %g: %.3f Hz, %.2f cents off", hz, fs, damp, got, cents(got, hz));
			}
	// High strings (V/OCT can send them there).
	for (float fs : {44100.f, 48000.f})
		for (float hz : {3520.f, 5000.f, 7919.f}) {
			String s = make(fs, hz, 10.f, 0.f);
			strike(s);
			const std::vector<float> x = run(s, 0.1, fs);
			const double got = measureHz(x, fs, hz, std::lround(0.02 * fs), 60);
			CHECK(std::fabs(cents(got, hz)) < 3.0, "%g Hz at %g Hz rate: %.2f Hz, %.2f cents off", hz, fs, got, cents(got, hz));
		}
	for (float hz : {16.f, 20.f}) {
		String s = make(48000.f, hz, 10.f, 0.33f);
		strike(s);
		const std::vector<float> x = run(s, 45.0 / hz + 0.01, 48000.0);
		const double got = measureHz(x, 48000.0, hz, std::lround(4.0 * 48000.0 / hz));
		CHECK(std::fabs(cents(got, hz)) < 10.0, "%g Hz: %.3f Hz, %.2f cents off", hz, got, cents(got, hz));
	}
}

void ring() {
	const double fs = 48000.0;
	for (float hz : {30.f, 110.f, 440.f, 1000.f}) {
		const float shortest = 20.f / hz > 0.1f ? 20.f / hz : 0.1f;
		for (float t60 : {shortest, 1.f, 4.f, 15.f, 60.f}) {
			if (t60 < shortest)
				continue;
			const double window = 20.0 / hz > 0.02 ? 20.0 / hz : 0.02, gap = t60 * 0.12 > window ? t60 * 0.12 : window;
			for (float damp : {0.f, 0.33f}) {
				String s = make(fs, hz, t60, damp);
				strike(s);
				const std::vector<float> x = run(s, gap + 2.0 * window + 8.0 / hz, fs);
				const double got = measureT60(x, fs, hz, std::lround(4.0 * fs / hz), window, gap);
				if (damp == 0.f)
					CHECK(std::fabs(got / t60 - 1.0) < 0.2, "%g Hz, Ring %g s: rings %.3g s", hz, t60, got);
				else
					CHECK(got > 0.5 * t60 * 0.95 && got < t60 * 1.05, "%g Hz, Ring %g s at Damp 0.33: rings %.3g s, want at least half", hz, t60, got);
			}
		}
	}
	// The read between samples loses a little of a high string each trip
	// (a third of a 1 s Ring at 3.5 kHz). The gain makes it up while it has
	// room under 1: these Rings are still Ring's.
	const struct { float hz, t60; } high[] = {{3520.f, 1.f}, {5000.f, 0.5f}, {2000.f, 8.f}};
	for (const auto& k : high) {
		String s = make(44100.0, k.hz, k.t60, 0.f);
		strike(s);
		const std::vector<float> x = run(s, k.t60 * 0.2 + 0.12, 44100.0);
		const double got = measureT60(x, 44100.0, k.hz, 441, 0.05, k.t60 * 0.2);
		CHECK(std::fabs(got / k.t60 - 1.0) < 0.1, "%g Hz at 44.1 kHz, Ring %g s: rings %.3g s", k.hz, k.t60, got);
	}
	// Above 1 kHz a long Ring may fall short, but not far.
	String s = make(fs, 2000.f, 60.f, 0.f);
	strike(s);
	const std::vector<float> x = run(s, 3.2, fs);
	const double got = measureT60(x, fs, 2000.0, 480, 0.05, 3.0);
	CHECK(got > 10.0, "2 kHz, Ring 60 s: rings %.3g s, want at least 10", got);
}

// Spectral centroid (Hz) of x over n samples from start, up to 8 kHz.
double centroid(const std::vector<float>& x, double fs, double f0, long start, long n) {
	double num = 0.0, den = 0.0;
	for (int h = 1; h * f0 < 8000.0; h++) {
		const double m = std::abs(bin(x, fs, h * f0, start, n));
		num += h * f0 * m * m;
		den += m * m;
	}
	return den > 0.0 ? num / den : 0.0;
}

void damp() {
	const double fs = 48000.0;
	for (float hz : {65.406f, 440.f}) {
		double lastCentroid = 1e9, lastT60 = 1e9;
		for (float d : {0.f, 0.33f, 0.66f, 1.f}) {
			String s = make(fs, hz, 4.f, d);
			strike(s);
			const std::vector<float> x = run(s, 1.4, fs);
			const long n = std::lround(40.0 * fs / hz) < 14400 ? std::lround(40.0 * fs / hz) : 14400;
			const double c = centroid(x, fs, hz, 24000, n);
			const double t = measureT60(x, fs, hz, 4800, 0.1, 0.6);
			// Darker with every step, until only the fundamental is left.
			CHECK(d == 0.f || c < lastCentroid * 0.9 || (c <= lastCentroid * 1.001 && c < hz * 1.02), "%g Hz, Damp %g: centroid %.0f Hz half a second after a strike, was %.0f at less Damp", hz, d, c, lastCentroid);
			CHECK(t <= lastT60 * 1.02, "%g Hz, Damp %g rings %.3g s, longer than at less Damp (%.3g s)", hz, d, t, lastT60);
			lastCentroid = c;
			lastT60 = t;
		}
	}
}

double largestStep(const std::vector<float>& x, long from, long to) {
	double m = 0.0;
	for (long i = from + 1; i < to; i++)
		m = std::max(m, (double) std::fabs(x[i] - x[i - 1]));
	return m;
}

void glide() {
	const double fs = 48000.0;
	const struct { float from, to; double allow; } cases[] = {{220.f, 110.f, 1.3}, {220.f, 330.f, 2.2}, {110.f, 116.54f, 1.3}};
	for (const auto& k : cases) {
		String s = make(fs, k.from, 10.f, 0.6f);
		strike(s);
		std::vector<float> x = run(s, 1.0, fs);          // the overtones die: a smooth wave
		s.setFrequency(k.to);
		const std::vector<float> y = run(s, 0.4, fs);
		const double before = largestStep(x, 48000 - 2400, 48000);
		const double during = largestStep(y, 0, 4800);
		// The seam between the two runs is a step of the string's too.
		const double seam = std::fabs(y[0] - x.back());
		CHECK(during < k.allow * before && seam < k.allow * before, "%g to %g Hz: a step of %.4g (seam %.4g) during the glide, %.4g before it", k.from,
		      k.to, during, seam, before);
		const double got = measureHz(y, fs, k.to, 4800, 12);
		CHECK(std::fabs(cents(got, k.to)) < 3.0, "%g to %g Hz: %.3f Hz 100 ms on, %.2f cents off", k.from, k.to, got, cents(got, k.to));
		// And it did move: not there in the first 2 ms.
		CHECK(std::fabs(cents(s.frequency(), k.to)) < 0.1, "the string reports %g Hz at the end", s.frequency());
	}
	// A glide takes time: 1 ms after the change the pitch is between the two.
	String s = make(fs, 220.f, 10.f, 0.6f);
	s.setFrequency(440.f);
	for (int i = 0; i < 48; i++)
		s.process(0.f);
	CHECK(s.frequency() > 225.f && s.frequency() < 400.f, "1 ms into an octave's glide the string is at %g Hz", s.frequency());
}

void input_share() {
	const double fs = 48000.0;
	double lo = 1e9, hi = 0.0;
	for (float t60 : {0.3f, 1.f, 4.f, 15.f, 60.f}) {
		String s = make(fs, 65.406f, t60, 0.33f);
		rka::hadal::Random rng;
		rng.seed(3);
		const long n = std::lround((t60 * 0.8 + 6.0) * fs), tail = 5 * 48000;
		double sum = 0.0;
		for (long i = 0; i < n; i++) {
			const float y = s.process(rng.uniform() * s.inputShare());
			if (i >= n - tail)
				sum += (double) y * y;
		}
		const double rms = std::sqrt(sum / tail);
		lo = std::min(lo, rms);
		hi = std::max(hi, rms);
	}
	CHECK(20.0 * std::log10(hi / lo) < 6.0, "noise through the input share: RMS from %.4g to %.4g across Ring (%.1f dB)", lo, hi, 20.0 * std::log10(hi / lo));
	// Without the share the same span is tens of dB: the test can tell.
	CHECK(make(fs, 65.406f, 60.f, 0.33f).inputShare() < 0.5f * make(fs, 65.406f, 0.3f, 0.33f).inputShare(), "the share does not fall as Ring rises");
}

void bounded() {
	const double fs = 48000.0;
	const float hz = 65.406f;
	String s = make(fs, hz, 60.f, 0.f);
	float peak = 0.f;
	bool finite = true;
	double phase = 0.0;
	for (long i = 0; i < 30L * 48000; i++) {
		phase += hz / fs;
		phase -= std::floor(phase);
		const float y = s.process(phase < 0.5 ? 10.f : -10.f);
		peak = std::max(peak, std::fabs(y));
		finite = finite && std::isfinite(y);
	}
	CHECK(finite && peak <= rka::rigging::LOOP_CEIL * rka::rigging::LOOP_OVERSHOOT, "a +-10 V square at the string's pitch for 30 s: peak %g V", peak);
	CHECK(peak > rka::rigging::LOOP_KNEE, "the square never reached the bound (peak %g V): the test proves nothing", peak);
	// Once under the knee the bound is out of the way: the ring is Ring's.
	run(s, 20.0, fs);
	const std::vector<float> x = run(s, 8.0, fs);
	CHECK(largestStep(x, 0, 48000) < 20.f, "still finite");
	const double got = measureT60(x, fs, hz, 0, 0.5, 7.0);
	CHECK(std::fabs(got / 60.0 - 1.0) < 0.2, "after the bound let go the string rings %.3g s, want 60", got);
	// And it ends in true silence.
	s.setRing(1.f);
	run(s, 19.0, fs);
	const std::vector<float> end = run(s, 1.0, fs);
	bool silent = true;
	for (float v : end)
		silent = silent && v == 0.f;
	CHECK(silent, "20 Ring times on the string is not exactly silent");
}

void rate_change() {
	String s = make(48000.f, 220.f, 10.f, 0.2f);
	strike(s);
	run(s, 0.2, 48000.0);
	for (float fs : {96000.f, 44100.f}) {
		s.setSampleRate(fs);
		s.setFrequency(220.f);
		bool quiet = true;
		for (int i = 0; i < 100; i++)
			quiet = quiet && s.process(0.f) == 0.f;
		CHECK(quiet, "a rate change to %g Hz did not clear the string", fs);
		strike(s);
		const std::vector<float> x = run(s, 45.0 / 220.0 + 0.01, fs);
		const double got = measureHz(x, fs, 220.0, std::lround(4.0 * fs / 220.0));
		CHECK(std::fabs(cents(got, 220.0)) < 3.0, "after the change to %g Hz: %.3f Hz, %.2f cents off", fs, got, cents(got, 220.0));
	}
	// The lowest string at the highest rate fits and rings.
	String low;
	low.setSampleRate(768000.f);
	low.setFrequency(8.f);
	low.setRing(10.f);
	low.snap();
	CHECK(std::fabs(low.periodSamples() - 96000.f) < 1.f, "8 Hz at 768 kHz is %g samples", low.periodSamples());
	strike(low);
	double energy = 0.0;
	bool finite = true;
	for (long i = 0; i < 3 * 96000L; i++) {
		const float y = low.process(0.f);
		finite = finite && std::isfinite(y);
		if (i > 2 * 96000L)
			energy += (double) y * y;
	}
	CHECK(finite && energy > 1.0, "8 Hz at 768 kHz: energy %g in its third period", energy);
	// Out-of-range pitches clamp.
	String c = make(48000.f, 1.f, 4.f, 0.f);
	CHECK(c.frequency() == 8.f, "1 Hz clamps to %g", c.frequency());
	c.setFrequency(1e9f);
	c.snap();
	CHECK(c.frequency() == 12000.f, "1 GHz clamps to %g", c.frequency());
	c.setFrequency(NAN);
	c.snap();
	CHECK(c.frequency() == 8.f, "NaN clamps to %g", c.frequency());
	CHECK(std::isfinite(c.process(NAN)) && c.process(NAN) == c.process(0.f), "a NaN sample in reads as 0");
}

// A glide ends: some time after a change of pitch the string has stopped
// working its coefficients out (it did not, once: the last step of the
// glide rounded to nothing, short of "arrived").
void glide_ends() {
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (float from : {65.406f, 261.63f, 1046.5f, 4186.f})
			for (float ratio : {1.0595f, 2.f, 0.5f, 1.4983f}) {
				String s = make(fs, from, 4.f, 0.33f);
				CHECK(!s.gliding(), "a string at rest is gliding");
				s.setFrequency(from * ratio);
				CHECK(s.gliding(), "a string given a new pitch is not gliding");
				run(s, 0.5, fs);
				CHECK(!s.gliding() && s.frequency() == from * ratio, "%g to %g Hz at %g: still gliding half a second on (at %.6f Hz)", from, from * ratio,
				      fs, s.frequency());
			}
}

// DC in a string's loop dies, at worst as slowly as a 60 s Ring, so strikes
// cannot pile it up. (The loop passes DC as it does its own pitch, and a
// strike's burst is not exactly balanced about zero. Once, the gain of a
// high string was held just under 1 whatever Ring was: DC sat in it for a
// quarter of an hour, and 600 strikes left 10 V of it.)
void dc_dies_with_ring() {
	const double fs = 48000.0;
	for (float hz : {6272.f, 8372.f, 2093.f}) {
		String s = make(fs, hz, 1.f, 0.33f);
		rka::hadal::Random rng;
		rng.seed(11);
		// 600 strikes, 20 a second, each a burst of smoothed noise about a
		// period long (as Strike.hpp makes them), then 5 s of rest.
		const long n = std::max(8L, std::lround(s.periodSamples()));
		float lp = 0.f;
		for (int strike = 0; strike < 600; strike++)
			for (long i = 0; i < 2400; i++) {
				float in = 0.f;
				if (i < n) {
					lp = i == 0 ? 0.f : lp;
					lp += 0.5f * (rng.uniform() - lp);
					in = 5.2f * lp;
				}
				s.process(in);
			}
		run(s, 4.0, fs);
		const std::vector<float> x = run(s, 1.0, fs);
		double mean = 0.0;
		for (float v : x)
			mean += v;
		mean /= x.size();
		CHECK(std::fabs(mean) < 1.0, "%g Hz, Ring 1 s: %.3g V of DC in the string 5 s after 600 strikes", hz, mean);
		// And it is on its way out: a 60 s fall is 1 dB a second.
		run(s, 9.0, fs);
		const std::vector<float> y = run(s, 1.0, fs);
		double later = 0.0;
		for (float v : y)
			later += v;
		later /= y.size();
		CHECK(std::fabs(later) <= std::fabs(mean) * 0.4 + 1e-4, "%g Hz: DC %.3g V, then %.3g V 10 s later", hz, mean, later);
	}
}

// A sudden change of Ring does not step the output: the gain moves a
// little each sample.
void ring_change_is_smooth() {
	const double fs = 48000.0;
	String s = make(fs, 220.f, 60.f, 0.7f);
	strike(s);
	const std::vector<float> x = run(s, 1.0, fs);
	const double before = largestStep(x, 48000 - 2400, 48000);
	s.setRing(0.1f);
	const std::vector<float> y = run(s, 0.01, fs);
	const double during = std::max(largestStep(y, 0, 480), (double) std::fabs(y[0] - x.back()));
	// (The note is collapsing in a tenth of a second: some steepening is its own.)
	CHECK(during < 2.0 * before, "Ring 60 s to 0.1 s on a ringing string: a step of %.4g, %.4g before it", during, before);
}

} // namespace

// Every Damp gives a finite string, the smallest above 0 too: there the
// loss per trip rounds to nothing, and the low-pass's pole was once worked
// out as 0 / 0, a NaN that then went round the loop for good. Reached by
// the knob or a CV just off 0.
void tiny_damp_is_finite() {
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (float hz : {32.7f, 130.81f, 1046.5f, 4186.f})
			for (float d : {1e-7f, 1e-6f, 1e-5f, 1e-4f, 1e-3f}) {
				String s = make(fs, hz, 4.f, d);
				bool finite = true;
				for (int i = 0; i < 2000 && finite; i++)
					finite = std::isfinite(s.process(0.3f * (float) std::sin(0.01 * i)));
				CHECK(finite, "Damp %g at %g Hz, %g: not finite", d, hz, fs);
			}
}

// Even at Damp 0 the very top dies early, as air takes it from a real
// string: the partial nearest 10 kHz falls 60 dB in at most about 0.6 s,
// while the fundamental still rings as long as Ring says. Once, Damp 0
// kept every partial to 20 kHz as long and as loud as the fundamental: a
// held, metallic sizzle on every note.
void top_dies_at_damp_0() {
	const double fs = 48000.0;
	for (float hz : {65.406f, 130.81f, 523.25f, 1046.5f}) {
		String s = make((float) fs, hz, 4.f, 0.f);
		strike(s);
		const std::vector<float> x = run(s, 2.0, fs);
		const double top = std::round(10000.0 / hz) * hz;
		const double tTop = measureT60(x, fs, top, 2400, 0.1, 0.3);
		const double t1 = measureT60(x, fs, hz, 4800, 0.3, 1.2);
		CHECK(tTop < 0.6, "%g Hz at Damp 0: the partial at %.0f Hz rings %.2f s (want under 0.6)", hz, top, tTop);
		CHECK(std::fabs(t1 / 4.0 - 1.0) < 0.05, "%g Hz at Damp 0: the fundamental rings %.2f s (want Ring's 4)", hz, t1);
	}
}

int main() {
	top_dies_at_damp_0();
	tiny_damp_is_finite();
	glide_ends();
	dc_dies_with_ring();
	ring_change_is_smooth();
	tuning();
	ring();
	damp();
	glide();
	input_share();
	bounded();
	rate_change();
	return rkatest::finish("rigging test_string");
}

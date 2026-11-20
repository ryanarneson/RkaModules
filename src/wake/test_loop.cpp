// Wake's loop colour and smear (src/wake/Colour.hpp, Smear.hpp): Tone, the
// three styles' band and drive, the DC block and the bound; Diffuse's
// spread, level and exact zero.
#include "hadal/Random.hpp"
#include "wake/Colour.hpp"
#include "wake/Smear.hpp"
#include "testing.hpp"

#include <cmath>
#include <vector>

namespace {

using namespace rka::wake;
const float FS = 48000.f;
const double TWO_PI = 6.283185307179586;

double db(double x) {
	return 20.0 * std::log10(x > 1e-30 ? x : 1e-30);
}

Colour make(float tone, int style, float seconds = 0.3f) {
	Colour c;
	c.setSampleRate(FS);
	c.set(tone, STYLES[style], seconds);
	return c;
}

// Gain in dB of a sine of `hz` and `volts` peak through `c`, by projection
// onto the input's frequency (so harmonics do not count).
double gainDb(Colour c, double hz, float volts = 1.f) {
	const int settle = 48000, n = 48000;
	double re = 0, im = 0;
	for (int i = 0; i < settle + n; i++) {
		const double ph = TWO_PI * hz * i / FS;
		const float y = c.process(volts * (float) std::sin(ph));
		if (i >= settle) {
			re += y * std::sin(ph);
			im += y * std::cos(ph);
		}
	}
	return db(2.0 * std::sqrt(re * re + im * im) / n / volts);
}

// The frequency where the gain is 3 dB under its 1 kHz (or, for a
// high-pass, 5 kHz) value, by bisection between lo and hi.
double corner(const Colour& c, double lo, double hi, double refHz, bool lowPass) {
	const double ref = gainDb(c, refHz);
	for (int i = 0; i < 20; i++) {
		const double mid = std::sqrt(lo * hi);
		const bool below = gainDb(c, mid) < ref - 3.0;
		if (below == lowPass)
			hi = mid;
		else
			lo = mid;
	}
	return std::sqrt(lo * hi);
}

double harmonicDb(Colour c, double hz, int k, float volts) {
	const int settle = 48000, n = 48000;
	double re = 0, im = 0;
	for (int i = 0; i < settle + n; i++) {
		const float y = c.process(volts * (float) std::sin(TWO_PI * hz * i / FS));
		if (i >= settle) {
			re += y * std::sin(TWO_PI * hz * k * i / FS);
			im += y * std::cos(TWO_PI * hz * k * i / FS);
		}
	}
	return db(2.0 * std::sqrt(re * re + im * im) / n);
}

void digital_flat() {
	const Colour c = make(0.5f, 2);
	for (double hz : {30.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0}) {
		const double g = gainDb(c, hz);
		CHECK(std::fabs(g) <= 1.0, "Digital at noon is %+.2f dB at %.0f Hz (want within 1)", g, hz);
	}
	// Tone at noon is out of the path, not a filter at the top of the band.
	CHECK(std::fabs(gainDb(c, 15000.0)) < 0.02, "Digital at noon is %+.4f dB at 15 kHz (want none)", gainDb(c, 15000.0));
	CHECK(std::fabs(gainDb(c, 1000.0, 5.f)) < 0.01, "Digital passes a +-5 V sine at %+.4f dB", gainDb(c, 1000.0, 5.f));
	CHECK(harmonicDb(c, 1000.0, 3, 10.f) < -80.0, "Digital adds a third harmonic at %.1f dB to a +-10 V sine",
	      harmonicDb(c, 1000.0, 3, 10.f));
}

void tone() {
	// Darker to the left, thinner to the right, step by step.
	double prev = 1e9;
	for (float t : {0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.f}) {
		const double g = gainDb(make(t, 2), 4000.0);
		CHECK(g < prev + (t == 0.5f ? 1e9 : -0.2), "Tone %.1f passes 4 kHz at %+.2f dB (after %+.2f)", t, g, prev);
		prev = g;
	}
	prev = 1e9;
	for (float t : {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.f}) {
		const double g = gainDb(make(t, 2), 100.0);
		CHECK(g < prev + (t == 0.5f ? 1e9 : -0.2), "Tone %.1f passes 100 Hz at %+.2f dB (after %+.2f)", t, g, prev);
		prev = g;
	}
	const double dark = corner(make(0.f, 2), 50.0, 10000.0, 50.0, true);
	const double thin = corner(make(1.f, 2), 50.0, 10000.0, 15000.0, false);
	std::printf("  Tone's corners: %.0f Hz at full left, %.0f Hz at full right\n", dark, thin);
	CHECK(std::fabs(dark / 500.0 - 1.0) < 0.15, "Tone full left is 3 dB down at %.0f Hz (want 500)", dark);
	CHECK(std::fabs(thin / 1000.0 - 1.0) < 0.15, "Tone full right is 3 dB down at %.0f Hz (want 1000)", thin);
	// Left leaves the lows, right leaves the highs.
	const double keepLow = gainDb(make(0.f, 2), 50.0), keepHigh = gainDb(make(1.f, 2), 12000.0);
	CHECK(std::fabs(keepLow) < 0.3 && std::fabs(keepHigh) < 0.8, "Tone must leave the other end of the band alone (%+.2f dB at 50 Hz, %+.2f dB at 12 kHz)",
	      keepLow, keepHigh);
}

void tape_and_analog() {
	for (int style : {0, 1}) {
		const double g = gainDb(make(0.5f, style, 0.01f), 8000.0, 0.1f);
		CHECK(g <= -3.0, "style %d is %+.2f dB at 8 kHz (want 3 dB down or more)", style, g);
		const double lows = gainDb(make(0.5f, style), 20.0, 0.1f);
		CHECK(lows < -1.0, "style %d is %+.2f dB at 20 Hz (want a low cut)", style, lows);
	}
	const double shortC = corner(make(0.5f, 1, 0.01f), 200.0, 20000.0, 400.0, true);
	const double longC = corner(make(0.5f, 1, 1.f), 200.0, 20000.0, 400.0, true);
	const double longer = corner(make(0.5f, 1, 8.f), 200.0, 20000.0, 400.0, true);
	const double tapeShort = corner(make(0.5f, 0, 0.01f), 200.0, 20000.0, 400.0, true);
	const double tapeLong = corner(make(0.5f, 0, 1.f), 200.0, 20000.0, 400.0, true);
	std::printf("  Analog's high cut: %.0f Hz at 10 ms, %.0f Hz at 1 s, %.0f Hz at 8 s; Tape's: %.0f / %.0f Hz\n", shortC, longC,
	            longer, tapeShort, tapeLong);
	CHECK(longC < 0.5 * shortC, "Analog must darken as Time lengthens (%.0f Hz at 10 ms, %.0f Hz at 1 s)", shortC, longC);
	CHECK(std::fabs(longer / longC - 1.0) < 0.02, "beyond 1 s Analog's corner holds (%.0f vs %.0f Hz)", longer, longC);
	CHECK(std::fabs(tapeLong / tapeShort - 1.0) < 0.02, "Tape's corner does not move with Time");
	const double tape3 = harmonicDb(make(0.5f, 0), 200.0, 3, 5.f), analog3 = harmonicDb(make(0.5f, 1), 200.0, 3, 5.f);
	std::printf("  third harmonic of a +-5 V sine: Tape %.1f dB, Analog %.1f dB\n", tape3, analog3);
	CHECK(analog3 >= tape3 + 6.0, "Analog must be grittier than Tape (third harmonic %.1f vs %.1f dB)", analog3, tape3);
	CHECK(std::fabs(gainDb(make(0.5f, 0), 1000.0, 0.1f)) < 0.5, "Tape passes a quiet 1 kHz sine at %+.2f dB",
	      gainDb(make(0.5f, 0), 1000.0, 0.1f));
}

void blend() {
	const StyleConstants half = blendStyles(STYLES[0], STYLES[1], 0.5f);
	CHECK(std::fabs(half.drive - 1.5f) < 1e-6f && std::fabs(half.lowCutHz - 60.f) < 1e-4f, "a half blend is half way");
	const StyleConstants a = blendStyles(STYLES[0], STYLES[2], 0.f), b = blendStyles(STYLES[0], STYLES[2], 1.f);
	CHECK(a.highCutHzShort == 6000.f && !a.crossfade && b.highCutHzShort == 0.f && b.highCutHzLong == 0.f && b.crossfade &&
	          b.drive == 0.f,
	      "the ends of a blend are the styles themselves");
	const StyleConstants m = blendStyles(STYLES[0], STYLES[2], 0.5f);
	CHECK(m.highCutHzShort > 6000.f && m.highCutHzShort < 20000.f, "blending to no high cut opens through the band (%g)",
	      m.highCutHzShort);
}

// The loop's own safety: gain over 1 for minutes, with noise, DC and
// silence going in; then the gain comes down.
void bounded() {
	for (int style = 0; style < 3; style++) {
		Colour c = make(0.5f, style);
		rka::hadal::Random rng;
		rng.seed(11);
		// A 5 ms loop round the colour.
		const int D = 240;
		std::vector<float> ring(D, 0.f);
		int p = 0;
		float peak = 0.f;
		bool finite = true;
		double sum = 0;
		long count = 0;
		auto run = [&](float seconds, float gain, int input) {
			const long n = (long) (seconds * FS);
			for (long i = 0; i < n; i++) {
				const float in = input == 0 ? 10.f * rng.uniform() : input == 1 ? 10.f : 0.f;
				const float y = c.process(gain * ring[p] + in);
				ring[p] = y;
				p = p + 1 < D ? p + 1 : 0;
				finite = finite && y == y;
				peak = std::max(peak, std::fabs(y));
				sum += y;
				count++;
			}
		};
		run(60.f, 1.1f, 0);
		run(60.f, 1.1f, 1);
		sum = 0;
		count = 0;
		run(60.f, 1.1f, 1);
		const double meanDc = sum / count;
		run(60.f, 1.1f, 2);
		CHECK(finite && peak <= LOOP_CEIL, "style %d at 110 %% for 4 minutes: peak %.2f V (bound %.0f)", style, peak, LOOP_CEIL);
		// (The bound, driven lopsided by the DC, leaves a little: a twentieth
		// of what went in at most. Without the DC block it sits at the bound.)
		CHECK(std::fabs(meanDc) < 0.5, "style %d holds a mean of %.4f V with 10 V DC going in (want under 0.5)", style, meanDc);
		// Feedback down to 50 %: 60 dB gone within 30 passes of silence.
		float before = 0.f;
		for (int i = 0; i < D; i++)
			before = std::max(before, std::fabs(ring[i]));
		run(30 * D / FS, 0.5f, 2);
		float after = 0.f;
		for (int i = 0; i < D; i++)
			after = std::max(after, std::fabs(ring[i]));
		CHECK(before > 0.1f, "style %d should still be sounding at 110 %% (%.3f V)", style, before);
		CHECK(after < before * 1e-3f, "style %d at 50 %% falls from %.3f to %.6f V in 30 passes (want 60 dB)", style, before,
		      after);
		// And then to exact silence, with no subnormals left.
		run(20.f, 0.5f, 2);
		float last = 1.f;
		for (int i = 0; i < D; i++)
			last = std::max(i ? last : 0.f, std::fabs(ring[i]));
		CHECK(last == 0.f, "style %d must reach exact silence (%g)", style, last);
	}
	// DC alone, no loop: gone within 2 seconds.
	Colour c = make(0.5f, 2);
	float y = 0.f;
	for (int i = 0; i < 96000; i++)
		y = c.process(10.f);
	CHECK(std::fabs(y) < 0.005f, "10 V DC leaves %.5f V after 2 s (want under 5 mV)", y);
	// A NaN does not stay.
	c.process(std::nanf(""));
	y = c.process(1.f);
	CHECK(y == y, "a NaN must not stay in the filters");
}

Smear smear(float amount, uint32_t seed = 0) {
	Smear s;
	s.setSampleRate(FS, seed);
	s.setAmount(amount);
	return s;
}

void smear_zero_exact() {
	Smear s = smear(0.f);
	rka::hadal::Random rng;
	rng.seed(5);
	bool same = true;
	for (int i = 0; i < 48000; i++) {
		const float x = rng.uniform();
		same = same && s.process(x) == x;
	}
	CHECK(same && !s.engaged(), "Diffuse 0 must return its input bit for bit");
	// Back to 0 from above: exact again once the fade is over.
	s.setAmount(0.6f);
	for (int i = 0; i < 4800; i++)
		s.process(rng.uniform());
	s.setAmount(0.f);
	for (int i = 0; i < 4800; i++)
		s.process(rng.uniform());
	same = true;
	for (int i = 0; i < 4800; i++) {
		const float x = rng.uniform();
		same = same && s.process(x) == x;
	}
	CHECK(same, "Diffuse back at 0 is exact again");
}

struct Spread {
	double energy, centroidMs, t90Ms;
};

Spread impulse(float amount, uint32_t seed = 0) {
	Smear s = smear(amount, seed);
	// Let the fade-in finish on silence first.
	for (int i = 0; i < 4800; i++)
		s.process(0.f);
	const int n = 48000;
	std::vector<double> e(n);
	double total = 0, moment = 0;
	for (int i = 0; i < n; i++) {
		const float y = s.process(i == 0 ? 1.f : 0.f);
		e[i] = (double) y * y;
		total += e[i];
		moment += e[i] * i;
	}
	double run = 0;
	int t90 = 0;
	for (int i = 0; i < n && run < 0.9 * total; i++) {
		run += e[i];
		t90 = i;
	}
	return {total, 1e3 * moment / total / FS, 1e3 * t90 / FS};
}

void smear_spreads() {
	const Spread low = impulse(0.1f), half = impulse(0.5f), full = impulse(1.f);
	std::printf("  Diffuse 0.1 / 0.5 / 1: 90 %% of the energy within %.1f / %.1f / %.1f ms; centroid %.1f / %.1f / %.1f ms\n",
	            low.t90Ms, half.t90Ms, full.t90Ms, low.centroidMs, half.centroidMs, full.centroidMs);
	CHECK(half.t90Ms > low.t90Ms + 1.0 && full.t90Ms > half.t90Ms + 3.0, "more Diffuse must spread a repeat further");
	for (const Spread& s : {low, half, full}) {
		CHECK(s.centroidMs < 10.0, "a smeared repeat's centroid is %.2f ms late (want under 10)", s.centroidMs);
		CHECK(std::fabs(db(std::sqrt(s.energy))) < 2.0, "a smeared repeat's level is %+.2f dB (want within 2)",
		      db(std::sqrt(s.energy)));
	}
	// The latency the engine takes off the echo time is the centroid.
	Smear s = smear(0.5f);
	CHECK(std::fabs(1e3 * s.latencySamples() / FS - half.centroidMs) < 0.5, "latency %.2f ms against a centroid of %.2f ms",
	      1e3 * s.latencySamples() / FS, half.centroidMs);
	// The two sides differ.
	Smear a = smear(0.8f, 0), b = smear(0.8f, 1);
	for (int i = 0; i < 4800; i++) {
		a.process(0.f);
		b.process(0.f);
	}
	double diff = 0;
	for (int i = 0; i < 4800; i++)
		diff += std::fabs(a.process(i == 0 ? 1.f : 0.f) - b.process(i == 0 ? 1.f : 0.f));
	CHECK(diff > 0.5, "the two sides' smears must differ (%.3f)", diff);
	// Engaging does not click on a steady signal.
	Smear e = smear(0.f);
	float prev = 0.f, jump = 0.f;
	for (int i = 0; i < 48000; i++) {
		if (i == 20000)
			e.setAmount(0.5f);
		if (i == 34000)
			e.setAmount(0.f);
		const float y = e.process((float) std::sin(TWO_PI * 100.0 * i / FS));
		if (i > 0)
			jump = std::max(jump, std::fabs(y - prev));
		prev = y;
	}
	CHECK(jump < 3.f * (float) (TWO_PI * 100.0 / FS), "turning Diffuse on and off steps %.4f a sample", jump);
}

} // namespace

int main() {
	digital_flat();
	tone();
	tape_and_analog();
	blend();
	bounded();
	smear_zero_exact();
	smear_spreads();
	return rkatest::finish("wake test_loop");
}

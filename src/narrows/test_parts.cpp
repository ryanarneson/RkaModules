// Unit tests for src/narrows/Comb.hpp and Ping.hpp. Run with `make test`.
#include "Comb.hpp"
#include "Laws.hpp"
#include "Ping.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>

using namespace rka;
using namespace rka::narrows;

static const float FS = 48000.f;

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float next() {
		s = s * 1664525u + 1013904223u;
		return float(int32_t(s)) * (1.f / 2147483648.f);
	}
};

static Comb makeComb(float fs = FS) {
	Comb c;
	c.allocate(fs);
	c.setSampleRate(fs);
	c.reset();
	return c;
}

// Gain in dB of a comb for a sine, after it has settled.
static double combGainDb(float sineHz, float period, float feedback) {
	Comb c = makeComb();
	double in = 0.0, out = 0.0;
	for (int i = 0; i < 48000; i++) {
		const float x = 0.1f * std::sin(2.f * float(M_PI) * sineHz * i / FS);
		const float y = c.process(x, period, feedback);
		if (i >= 24000) {
			in += double(x) * x;
			out += double(y) * y;
		}
	}
	return 10.0 * std::log10(out / in);
}

static void comb_exact_at_zero() {
	Comb c = makeComb();
	Lcg rng(1);
	long different = 0;
	float last[200] = {};
	for (int i = 0; i < 20000; i++) {
		const float x = rng.next();
		if (!(c.process(x, 100.f, 0.f) == x))
			different++;
		last[i % 200] = x;
	}
	CHECK(different == 0, "%ld samples changed at feedback 0", different);
	// The line was written all along: with feedback turned up and silence
	// in, what went in 100 samples ago comes straight back.
	const float want = last[(20000 - 100) % 200];
	const float y = c.process(0.f, 100.f, 0.9f);
	CHECK(std::fabs(y - 0.9f * want) < 1e-4f && std::fabs(want) > 1e-3f, "first echo %g, want %g", y, 0.9f * want);
}

// reset() costs nothing (the engine calls it on the audio thread when a
// voice joins) and still forgets: what was in the line is never read.
static void comb_reset_forgets() {
	Comb c = makeComb();
	Lcg rng(2);
	for (int i = 0; i < 5000; i++)
		c.process(rng.next(), 100.f, 0.9f);
	c.reset();
	double peak = 0.0;
	for (int i = 0; i < 500; i++)
		peak = std::fmax(peak, std::fabs(double(c.process(0.f, 100.f, 0.9f))));
	CHECK(peak == 0.0, "after reset, silence in gives %g", peak);
	// And it works as new afterwards: an impulse comes back one period on.
	c.reset();
	c.process(1.f, 100.f, 0.9f);
	float y = 0.f;
	for (int i = 0; i < 100; i++)
		y = c.process(0.f, 100.f, 0.9f);
	const float a = 2.f * (1.f - 0.9f) / (2.f - 0.9f);
	CHECK(std::fabs(y - 0.9f * a) < 1e-4f, "the first echo after a reset: %g, want %g", y, 0.9f * a);
}

// A comb just reset reads as one that has run on silence for a long time.
static void comb_fresh_equals_run_in() {
	Comb fresh = makeComb(), runIn = makeComb();
	for (int i = 0; i < 20000; i++)
		runIn.process(0.f, 43.64f, 0.9f);
	Lcg rng(12);
	long different = 0;
	for (int i = 0; i < 5000; i++) {
		const float x = rng.next();
		if (!(fresh.process(x, 43.64f, 0.9f) == runIn.process(x, 43.64f, 0.9f)))
			different++;
	}
	CHECK(different == 0, "%ld samples differ", different);
}

static double peakNear(float centre, float period, float feedback) {
	double best = -1e9, at = 0.0;
	for (float hz = centre * 0.97f; hz <= centre * 1.03f; hz += centre * 0.0025f) {
		const double g = combGainDb(hz, period, feedback);
		if (g > best) {
			best = g;
			at = hz;
		}
	}
	return at;
}

static void comb_peaks() {
	// Period 100 samples at 48 kHz: 480 Hz.
	CHECK(combGainDb(480.f, 100.f, 0.9f) - combGainDb(240.f, 100.f, 0.9f) > 20.0, "positive: 480 Hz over 240 Hz");
	CHECK(combGainDb(960.f, 100.f, 0.9f) - combGainDb(720.f, 100.f, 0.9f) > 20.0, "positive: 960 Hz over 720 Hz");
	CHECK(combGainDb(240.f, 100.f, -0.9f) - combGainDb(480.f, 100.f, -0.9f) > 20.0, "negative: 240 Hz over 480 Hz");
	CHECK(combGainDb(720.f, 100.f, -0.9f) - combGainDb(960.f, 100.f, -0.9f) > 20.0, "negative: 720 Hz over 960 Hz");
	const double p = peakNear(480.f, 100.f, 0.9f), n = peakNear(240.f, 100.f, -0.9f);
	CHECK(std::fabs(p - 480.0) < 4.8, "positive peak at %f Hz", p);
	CHECK(std::fabs(n - 240.0) < 2.4, "negative peak at %f Hz", n);
	// A fractional period is in tune too: 1 kHz is 48 samples; 1.1 kHz is 43.64.
	const double q = peakNear(1100.f, FS / 1100.f, 0.9f);
	CHECK(std::fabs(q - 1100.0) < 11.0, "fractional period: peak at %f Hz", q);
}

static void comb_peak_level() {
	for (float fb : {0.3f, 0.6f, 0.95f}) {
		const double up = combGainDb(480.f, 100.f, fb), down = combGainDb(240.f, 100.f, -fb);
		CHECK(up <= 6.5 && up >= 0.0, "peak at feedback %g: %f dB", fb, up);
		CHECK(down <= 6.5 && down >= 0.0, "peak at feedback %g: %f dB", -fb, down);
	}
}

static void comb_bounded() {
	for (float fb : {0.95f, -0.95f}) {
		Comb c = makeComb();
		Lcg rng(4);
		double peak = 0.0;
		for (int i = 0; i < 96000; i++) {
			const float x = i < 48000 ? 2.4f * rng.next() : 2.4f;   // noise, then DC
			peak = std::fmax(peak, std::fabs(double(c.process(x, 37.3f, fb))));
		}
		CHECK(peak < 4.8, "feedback %g: peak %g", fb, peak);
		float y = 1.f;
		for (int i = 0; i < 96000; i++)
			y = c.process(0.f, 37.3f, fb);
		CHECK(y == 0.f, "feedback %g: back to exact silence, got %g", fb, y);
	}
}

static void comb_slides() {
	// A 2 kHz sine through a ringing comb; the period jumps from 100 to 61
	// samples. A 2 kHz sine at the comb's peak level steps by up to 0.26
	// between samples on its own; a period that jumped would add a break on
	// top of that (0.29 measured), a slid one does not (0.13).
	Comb c = makeComb();
	float prev = 0.f;
	double before = 0.0, across = 0.0;
	for (int i = 0; i < 48000; i++) {
		const float x = 0.5f * std::sin(2.f * float(M_PI) * 2000.f * i / FS);
		const float y = c.process(x, i < 24000 ? 100.f : 61.f, 0.8f);
		if (i > 20000 && i < 24000)
			before = std::fmax(before, std::fabs(double(y) - prev));
		if (i >= 24000 && i < 24200)
			across = std::fmax(across, std::fabs(double(y) - prev));
		prev = y;
	}
	CHECK(across < 0.2 && before > 0.01, "largest step across a jump of the period: %g (before it: %g)", across, before);
	// And it does arrive: a little later the period is the new one.
	Comb d = makeComb();
	for (int i = 0; i < 4800; i++)
		d.process(0.f, i < 2400 ? 100.f : 50.f, 0.8f);
	CHECK(std::fabs(d.period() - 50.f) < 0.01f, "period after the slide: %g", d.period());
}

static void comb_range() {
	Comb c = makeComb();
	Lcg rng(6);
	const float bad[] = {0.f, 1.f, -5.f, NAN, 1e9f, INFINITY};
	for (float p : bad)
		for (int i = 0; i < 2000; i++) {
			const float y = c.process(rng.next(), p, 0.9f);
			CHECK(y == y && std::fabs(y) < 10.f, "period %g gave %g", p, y);
			if (!(y == y))
				break;
		}
	CHECK(c.period() >= 2.f && c.period() < FS / 8.f + 4096.f, "the period stays inside the line: %g", c.period());
	// The floor is 2 samples: with period 0 asked for, the comb's first
	// peak is at fs / 2.
	Comb f = makeComb();
	for (int i = 0; i < 1000; i++)
		f.process(0.f, 0.f, 0.5f);
	CHECK(f.period() == 2.f, "floor: %g", f.period());
	// One period of 8 Hz fits at every rate.
	for (float fs : {44100.f, 48000.f, 192000.f, 768000.f}) {
		Comb g = makeComb(fs);
		for (int i = 0; i < 200000; i++)
			g.process(0.f, fs / 8.f, 0.5f);
		CHECK(std::fabs(g.period() - fs / 8.f) < 1.f, "8 Hz at %g: period %g", fs, g.period());
	}
}

static void ping_one_per_edge() {
	for (float fs : {44100.f, 48000.f, 192000.f}) {
		PingPulse p;
		p.setSampleRate(fs);
		p.reset();
		// Low, then a gate held high for 50 ms, then low.
		double peak = 0.0, area = 0.0;
		int length = 0, pulses = 0;
		float prev = 0.f;
		double step = 0.0;
		const int n = int(0.1f * fs);
		for (int i = 0; i < n; i++) {
			const float gate = (i > n / 10 && i < n * 6 / 10) ? 10.f : 0.f;
			const float y = p.process(gate);
			peak = std::fmax(peak, double(y));
			area += y;
			if (y > 0.f)
				length++;
			if (y > 0.f && prev == 0.f)
				pulses++;
			step = std::fmax(step, std::fabs(double(y) - prev));
			CHECK(y >= 0.f, "never negative: %g", y);
			prev = y;
		}
		CHECK(pulses == 1, "a held gate at %g: %d pulses", fs, pulses);
		CHECK(std::fabs(peak - 1.0) < 0.02, "peak at %g: %g", fs, peak);
		CHECK(std::fabs(length / fs - 0.001) < 0.0002, "length at %g: %g ms", fs, 1000.0 * length / fs);
		CHECK(step < 12.0 / (0.001 * fs), "no step at its ends at %g: %g", fs, step);
		CHECK(area > 0.0, "it has a body");
	}
	// Schmitt levels: 0.5 V does not fire from low; once high, 0.5 V does
	// not re-arm, 0.05 V does.
	PingPulse p;
	p.setSampleRate(FS);
	p.reset();
	int fired = 0;
	auto run = [&](float volts, int count) {
		float prev = 0.f;
		for (int i = 0; i < count; i++) {
			const float y = p.process(volts);
			if (y > 0.f && prev == 0.f)
				fired++;
			prev = y;
		}
	};
	run(0.5f, 500);
	CHECK(fired == 0, "0.5 V does not fire");
	run(1.5f, 500);
	CHECK(fired == 1, "1.5 V fires");
	run(0.5f, 500);
	run(1.5f, 500);
	CHECK(fired == 1, "0.5 V does not re-arm");
	run(0.05f, 500);
	run(1.5f, 500);
	CHECK(fired == 2, "0.05 V re-arms");
	// A 2 kHz square and garbage: bounded, finite.
	PingPulse q;
	q.setSampleRate(FS);
	q.reset();
	double peak = 0.0, jump = 0.0;
	float was = 0.f;
	for (int i = 0; i < 48000; i++) {
		const float gate = i < 24000 ? ((i / 12) % 2 ? 10.f : 0.f) : (i % 3 == 0 ? NAN : (i % 3 == 1 ? INFINITY : -1e30f));
		const float y = q.process(gate);
		peak = std::fmax(peak, std::fabs(double(y)));
		if (!(y == y))
			peak = 1e9;
		jump = std::fmax(jump, std::fabs(double(y) - was));
		was = y;
	}
	CHECK(peak <= 1.0 + 1e-6, "audio-rate and hostile gates: peak %g", peak);
	CHECK(jump < 12.0 / (0.001 * FS), "an edge inside a pulse does not restart it: step %g", jump);
}

int main() {
	comb_exact_at_zero();
	comb_reset_forgets();
	comb_fresh_equals_run_in();
	comb_peaks();
	comb_peak_level();
	comb_bounded();
	comb_slides();
	comb_range();
	ping_one_per_edge();
	return rkatest::finish("narrows/test_parts");
}

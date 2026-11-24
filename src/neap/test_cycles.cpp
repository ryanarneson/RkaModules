// Neap's eight cycles (Cycles.hpp): shapes, frequencies, drift, crossfade, reset.
#include "neap/Cycles.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace rka::neap;

const float kNoonE1 = rateExponent(0.5f, 0.f, 0.f);
const double kTwoPi = 6.283185307179586;

void triangle_shape() {
	CHECK(Cycles::triangle(0.f) == 0.f, "tri(0)");
	CHECK(std::fabs(Cycles::triangle(0.25f) - 1.f) < 1e-6f, "tri(0.25)");
	CHECK(std::fabs(Cycles::triangle(0.5f)) < 1e-6f, "tri(0.5)");
	CHECK(std::fabs(Cycles::triangle(0.75f) + 1.f) < 1e-6f, "tri(0.75)");
	CHECK(std::fabs(Cycles::triangle(0.125f) - 0.5f) < 1e-6f, "tri(0.125)");
	CHECK(std::fabs(Cycles::triangle(0.875f) + 0.5f) < 1e-6f, "tri(0.875)");
	// Through step(): a step that lands each cycle on a quarter.
	for (int i = 0; i < kCycles; i++) {
		Cycles c;
		float x[kCycles];
		const double hz = cycleHz(i, kNoonE1, 0.f);
		const float dt = float(0.25 / hz);
		c.step(kNoonE1, false, dt, x);
		CHECK(std::fabs(x[i] - 1.f) < 1e-4f, "cycle %d at a quarter reads %g", i, x[i]);
		c.step(kNoonE1, false, dt, x);
		CHECK(std::fabs(x[i]) < 1e-4f, "cycle %d at a half reads %g", i, x[i]);
		c.step(kNoonE1, false, dt, x);
		CHECK(std::fabs(x[i] + 1.f) < 1e-4f, "cycle %d at three quarters reads %g", i, x[i]);
	}
	// Starts rising from 0.
	Cycles c;
	float x[kCycles];
	c.step(kNoonE1, false, 1e-3f, x);
	for (int i = 0; i < kCycles; i++)
		CHECK(x[i] > 0.f && x[i] < 0.1f, "cycle %d starts rising (%g)", i, x[i]);
}

// Rising zero crossings of the triangle, linearly interpolated (exact: the
// triangle is a straight line through zero). Returns the frequency from the
// first to the last crossing, or 0 with fewer than 2.
void crossing_freqs(float e1, double fs, double seconds, double hz[kCycles], int counts[kCycles]) {
	Cycles c;
	const float dt = float(1.0 / fs);
	const long n = long(seconds * fs);
	float x[kCycles], prev[kCycles];
	double first[kCycles], last[kCycles];
	for (int i = 0; i < kCycles; i++) { counts[i] = 0; first[i] = last[i] = 0; prev[i] = 0.f; }
	for (long k = 0; k < n; k++) {
		c.step(e1, false, dt, x);
		for (int i = 0; i < kCycles; i++) {
			if (k > 0 && prev[i] < 0.f && x[i] >= 0.f) {
				const double t = (double(k) - 1 + double(-prev[i]) / (double(x[i]) - prev[i])) / fs;
				if (counts[i] == 0) first[i] = t;
				last[i] = t;
				counts[i]++;
			}
			prev[i] = x[i];
		}
	}
	for (int i = 0; i < kCycles; i++)
		hz[i] = counts[i] >= 2 ? (counts[i] - 1) / (last[i] - first[i]) : 0.0;
}

void frequency_by_crossings() {
	const double rates[3] = {48000.0, 44100.0, 96000.0};
	for (double fs : rates) {
		// Noon: cycles 1..3 (their periods fit the run).
		{
			double hz[kCycles];
			int cnt[kCycles];
			crossing_freqs(kNoonE1, fs, 40.0, hz, cnt);
			for (int i = 0; i < 3; i++) {
				const double want = cycleHz(i, kNoonE1, 0.f);
				CHECK(cnt[i] >= 5, "noon cycle %d: %d crossings", i, cnt[i]);
				CHECK(std::fabs(hz[i] / want - 1.0) < 1e-4, "noon fs %g cycle %d: %g Hz, want %g", fs, i, hz[i], want);
			}
		}
		// Four octaves up: all eight cycles.
		{
			const float e1 = kNoonE1 + 4.f;
			double hz[kCycles];
			int cnt[kCycles];
			crossing_freqs(e1, fs, 120.0, hz, cnt);
			for (int i = 0; i < kCycles; i++) {
				const double want = cycleHz(i, e1, 0.f);
				CHECK(cnt[i] >= 5, "cycle %d: %d crossings", i, cnt[i]);
				CHECK(std::fabs(hz[i] / want - 1.0) < 1e-4, "fs %g cycle %d: %g Hz, want %g", fs, i, hz[i], want);
			}
		}
	}
}

void slowest_long_run() {
	// e1 at the bottom clamp, 768 kHz: cycle 8 advances ~1.4e-11 of a cycle
	// per sample, far below a float's resolution at its phase.
	const float e1 = rateExponent(0.f, 0.f, -100.f);
	const double fs = 768000.0;
	const float dt = float(1.0 / fs);
	const long n = 50000000;
	Cycles c;
	float x[kCycles];
	for (long k = 0; k < n; k++) c.step(e1, false, dt, x);
	for (int i = 0; i < kCycles; i++) {
		const double f = cycleHz(i, e1, 0.f);
		const double want = f * double(dt) * double(n);
		const double got = c.phase(i);
		CHECK(want < 1.0, "test expects no wrap");
		CHECK(std::fabs(got / want - 1.0) < 1e-6, "cycle %d advanced %.9g, want %.9g", i, got, want);
	}
	// Still moving, not frozen.
	const double before = c.phase(7);
	for (int k = 0; k < 1000000; k++) c.step(e1, false, dt, x);
	CHECK(c.phase(7) > before, "slowest cycle froze");
}

// Per-sample frequency ratio from the phase advance (independent of cycleHz).
void drift_bounded_and_moving() {
	const double fs = 1000.0;
	const float dt = float(1.0 / fs);
	const long n = long(30 * 60 * fs);
	Cycles c;
	float x[kCycles];
	double prevPhase[kCycles] = {0};
	double lo[kCycles], hi[kCycles];
	for (int i = 0; i < kCycles; i++) { lo[i] = 1e9; hi[i] = -1e9; }
	for (long k = 0; k < n; k++) {
		c.step(kNoonE1, true, dt, x);
		for (int i = 0; i < kCycles; i++) {
			double d = c.phase(i) - prevPhase[i];
			if (d < 0) d += 1.0;
			prevPhase[i] = c.phase(i);
			const double r = d * fs / cycleHz(i, kNoonE1, 0.f) - 1.0;
			if (k < 10) continue;
			if (r < lo[i]) lo[i] = r;
			if (r > hi[i]) hi[i] = r;
		}
	}
	for (int i = 0; i < kCycles; i++) {
		CHECK(hi[i] <= 0.005 + 1e-6 && lo[i] >= -0.005 - 1e-6, "cycle %d drift range [%g, %g]", i, lo[i], hi[i]);
		CHECK(hi[i] - lo[i] > 0.002, "cycle %d drift barely moves: [%g, %g]", i, lo[i], hi[i]);
	}
	// Each cycle's drift matches a reference built from the plan's numbers:
	// RandomGen(1000 + i) primed once, advanced on each wrap of a phasor at
	// 1 / (60 + 9 i) Hz, d = 0.005 * value(p, 1).
	Cycles c2;
	rka::Phasor refPh[kCycles];
	rka::RandomGen refGen[kCycles] = {rka::RandomGen(1000), rka::RandomGen(1001), rka::RandomGen(1002), rka::RandomGen(1003),
	                                  rka::RandomGen(1004), rka::RandomGen(1005), rka::RandomGen(1006), rka::RandomGen(1007)};
	for (int i = 0; i < kCycles; i++) refGen[i].advance();
	double prev2[kCycles] = {0};
	double worstErr = 0;
	for (long k = 0; k < 300000; k++) {
		c2.step(kNoonE1, true, dt, x);
		for (int i = 0; i < kCycles; i++) {
			if (refPh[i].step(1.0 / (60.0 + 9.0 * i), double(dt), false, false)) refGen[i].advance();
			const double want = 0.005 * double(refGen[i].value(float(refPh[i].phase), 1.f));
			double d = c2.phase(i) - prev2[i];
			if (d < 0) d += 1.0;
			prev2[i] = c2.phase(i);
			const double got = d * fs / cycleHz(i, kNoonE1, 0.f) - 1.0;
			if (k > 0 && std::fabs(got - want) > worstErr) worstErr = std::fabs(got - want);
		}
	}
	CHECK(worstErr < 2e-6, "drift differs from the reference by %g", worstErr);
}

void drift_off_is_exact_and_reset_keeps_drift() {
	const float dt = float(1.0 / 48000.0);
	const int n = 20000;
	std::vector<float> a, b, d;
	float x[kCycles];
	Cycles fresh;
	for (int k = 0; k < n; k++) { fresh.step(kNoonE1 + 6.f, false, dt, x); a.insert(a.end(), x, x + kCycles); }
	// Same object after reset().
	fresh.reset();
	for (int k = 0; k < n; k++) { fresh.step(kNoonE1 + 6.f, false, dt, x); b.insert(b.end(), x, x + kCycles); }
	CHECK(a == b, "drift-off run differs after reset()");
	// After a drift-on run, then reset: drift off is still exact.
	Cycles used;
	for (int k = 0; k < 5000; k++) used.step(kNoonE1 + 6.f, true, dt, x);
	used.reset();
	for (int k = 0; k < n; k++) { used.step(kNoonE1 + 6.f, false, dt, x); d.insert(d.end(), x, x + kCycles); }
	CHECK(a == d, "drift-off after a drift-on run is not exact");
	CHECK(std::memcmp(a.data(), d.data(), a.size() * sizeof(float)) == 0, "not bit-identical");

	// Drift on: reset() keeps the drift state, so the rate just after a reset
	// matches the rate just before it (a fresh start would read 0).
	const double fs = 1000.0;
	const float dt2 = float(1.0 / fs);
	Cycles c;
	for (int k = 0; k < 40000; k++) c.step(kNoonE1, true, dt2, x);
	double pp = c.phase(0);
	c.step(kNoonE1, true, dt2, x);
	double d1 = c.phase(0) - pp; if (d1 < 0) d1 += 1;
	const double before = d1 * fs / cycleHz(0, kNoonE1, 0.f) - 1.0;
	c.reset();
	c.step(kNoonE1, true, dt2, x);
	const double after = c.phase(0) * fs / cycleHz(0, kNoonE1, 0.f) - 1.0;
	CHECK(std::fabs(before) > 1e-4, "drift at 40 s is %g, too small to test", before);
	CHECK(std::fabs(after - before) < 1e-5, "drift jumped over reset: %g -> %g", before, after);
}

void wave_crossfade_no_jump() {
	const float e1 = rateExponent(1.f, 0.f, 0.f); // fastest
	const double fs = 48000.0;
	const float dt = float(1.0 / fs);
	Cycles c;
	float x[kCycles], prev[kCycles];
	c.step(e1, false, dt, prev);
	Wave w = WAVE_TRIANGLE;
	long next = 100;
	double worst = 0;
	for (long k = 0; k < 200000; k++) {
		if (k == next) {
			w = (w == WAVE_TRIANGLE) ? WAVE_SINE : WAVE_TRIANGLE;
			c.setWave(w);
			next += 37 + (k * 7919) % 900; // many different phases and fade states
		}
		double p0[kCycles];
		for (int i = 0; i < kCycles; i++) p0[i] = c.phase(i);
		c.step(e1, false, dt, x);
		for (int i = 0; i < kCycles; i++) {
			double dp = c.phase(i) - p0[i];
			if (dp < 0) dp += 1;
			const float pa = float(p0[i]), pb = float(c.phase(i));
			const double diff = std::fmax(std::fabs(Cycles::sine(pa) - Cycles::triangle(pa)),
			                              std::fabs(Cycles::sine(pb) - Cycles::triangle(pb)));
			const double bound = kTwoPi * dp + diff * (1.0 / (0.010 * fs)) + 1e-5;
			const double step = std::fabs(double(x[i]) - double(prev[i]));
			if (step > bound) {
				CHECK(false, "k %ld cycle %d step %g > bound %g", k, i, step, bound);
				k = 200000;
				break;
			}
			if (step / bound > worst) worst = step / bound;
			CHECK(std::fabs(x[i]) <= 1.f + 1e-6f, "out of range %g", x[i]);
			prev[i] = x[i];
		}
	}
	// Toggling every 2 ms stays inside +-1.
	Cycles t;
	float m = 0.f;
	for (long k = 0; k < 96000; k++) {
		if (k % 96 == 0) t.setWave((k / 96) % 2 ? WAVE_SINE : WAVE_TRIANGLE);
		t.step(e1, false, dt, x);
		for (int i = 0; i < kCycles; i++) m = std::fmax(m, std::fabs(x[i]));
	}
	CHECK(m <= 1.f + 1e-6f, "toggling every 2 ms reached %g", m);
}

void crossfade_takes_ten_ms() {
	const double fs = 48000.0;
	const float dt = float(1.0 / fs);
	const float e1 = rateExponent(1.f, 0.f, 0.f);
	Cycles c;
	float x[kCycles];
	c.setWave(WAVE_SINE);
	for (int k = 1; k <= 480; k++) {
		c.step(e1, false, dt, x);
		const float p = float(c.phase(0));
		const float t = Cycles::triangle(p), s = Cycles::sine(p);
		const float m = float(k) / 480.f;
		const float want = (1.f - m) * t + m * s;
		CHECK(std::fabs(x[0] - want) < 2e-4f, "step %d: %g, want %g", k, x[0], want);
	}
	// Afterwards the pure sine, against the library sine.
	for (int k = 0; k < 1000; k++) {
		c.step(e1, false, dt, x);
		for (int i = 0; i < kCycles; i++) {
			const double want = std::sin(kTwoPi * c.phase(i));
			CHECK(std::fabs(x[i] - want) < 1e-4, "cycle %d sine %g vs %g", i, x[i], want);
		}
	}
	// And back to the triangle.
	c.setWave(WAVE_TRIANGLE);
	for (int k = 0; k < 480; k++) c.step(e1, false, dt, x);
	for (int i = 0; i < kCycles; i++)
		CHECK(x[i] == Cycles::triangle(float(c.phase(i))), "cycle %d not back to triangle", i);
}

void reset_puts_all_at_zero_rising() {
	const float dt = float(1.0 / 48000.0);
	Cycles c;
	float x[kCycles];
	for (int k = 0; k < 7777; k++) c.step(kNoonE1 + 5.f, true, dt, x);
	c.setWave(WAVE_SINE);
	for (int k = 0; k < 100; k++) c.step(kNoonE1 + 5.f, true, dt, x); // mid-fade
	c.reset();
	for (int i = 0; i < kCycles; i++) CHECK(c.phase(i) == 0.0, "cycle %d phase %g after reset", i, c.phase(i));
	// The crossfade is done: the sine target is reached at once.
	c.step(kNoonE1, false, 1e-3f, x);
	for (int i = 0; i < kCycles; i++) {
		const double want = std::sin(kTwoPi * c.phase(i));
		CHECK(x[i] > 0.f, "cycle %d not rising", i);
		CHECK(std::fabs(x[i] - want) < 1e-5, "cycle %d reads %g after reset, sine is %g", i, x[i], want);
	}
}

void cache_follows_e1_and_nan_is_safe() {
	const float dt = float(1.0 / 48000.0);
	Cycles c;
	float x[kCycles];
	c.step(kNoonE1, false, dt, x);
	double p = c.phase(0);
	c.step(kNoonE1 + 1.f, false, dt, x);
	const double d = c.phase(0) - p;
	CHECK(std::fabs(d * 48000.0 / cycleHz(0, kNoonE1 + 1.f, 0.f) - 1.0) < 1e-5, "e1 change not followed (%g Hz)", d * 48000.0);
	// NaN e1 keeps the last rate; nothing goes NaN.
	c.step(NAN, true, dt, x);
	for (int i = 0; i < kCycles; i++)
		CHECK(x[i] == x[i] && c.phase(i) == c.phase(i), "NaN leaked");
	// Absurd sample time stays finite and in range.
	c.step(kNoonE1, true, 0.f, x);
	c.step(kNoonE1, true, 1e9f, x);
	for (int i = 0; i < kCycles; i++)
		CHECK(x[i] == x[i] && std::fabs(x[i]) <= 1.f + 1e-6f, "bad value %g", x[i]);
}

} // namespace

int main() {
	triangle_shape();
	frequency_by_crossings();
	slowest_long_run();
	drift_bounded_and_moving();
	drift_off_is_exact_and_reset_keeps_drift();
	wave_crossfade_no_jump();
	crossfade_takes_ten_ms();
	reset_puts_all_at_zero_rising();
	cache_follows_e1_and_nan_is_safe();
	return rkatest::finish("neap/test_cycles");
}

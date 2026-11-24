// Neap's tide and HIGH gate (Tide.hpp): limiter, bounds, weighting, glide, hysteresis.
#include "neap/Tide.hpp"
#include "neap/Cycles.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>

namespace {

using namespace rka::neap;

const float kNoonE1 = rateExponent(0.5f, 0.f, 0.f);

void limit_shape() {
	CHECK(tideLimit(0.f) == 0.f, "limit(0)");
	CHECK(std::fabs(tideLimit(1e-3f) / 1e-3f - 1.f) < 1e-4f, "slope at 0: %g", tideLimit(1e-3f) / 1e-3f);
	float prev = tideLimit(-10.f);
	for (int i = -1000; i <= 1000; i++) {
		const float x = i * 0.01f;
		const float y = tideLimit(x);
		CHECK(std::fabs(y) <= 1.f + 1e-6f, "|limit(%g)| = %g", x, y);
		CHECK(y >= prev - 1e-7f, "monotone at %g", x);
		CHECK(std::fabs(y + tideLimit(-x)) < 1e-6f, "odd at %g", x);
		prev = y;
	}
	CHECK(std::fabs(tideLimit(2.999999f) - 1.f) < 1e-5f, "just below 3: %g", tideLimit(2.999999f));
	CHECK(tideLimit(3.f) == 1.f && tideLimit(3.000001f) == 1.f && tideLimit(100.f) == 1.f, "at and past 3");
	CHECK(tideLimit(-3.f) == -1.f && tideLimit(-100.f) == -1.f, "negative side");
	const float nan = std::nanf("");
	CHECK(tideLimit(nan) == 0.f, "NaN gives 0");
}

void bounded_24h() {
	const float ws[5] = {-1.f, -0.5f, 0.f, 0.5f, 1.f};
	const float fs = 100.f, dt = 1.f / fs;
	const long n = long(24.0 * 3600.0 * fs);
	Cycles c;
	c.reset();
	Tide t[5];
	float peak = 0.f;
	float x[kCycles];
	for (long k = 0; k < n; k++) {
		c.step(kNoonE1, false, dt, x);
		for (int j = 0; j < 5; j++) {
			const float v = t[j].step(x, ws[j], dt);
			const float a = std::fabs(v);
			if (!(a <= 5.f)) {
				CHECK(false, "w=%g: |TIDE| = %g at sample %ld", ws[j], v, k);
				return;
			}
			if (a > peak) peak = a;
		}
	}
	CHECK(peak > 1.5f, "the tide actually moves: peak %g", peak);
}

// Pearson correlation of two series.
struct Corr {
	double n = 0, sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
	void add(double a, double b) { n++; sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; }
	double r() const {
		const double cov = sab - sa * sb / n, va = saa - sa * sa / n, vb = sbb - sb * sb / n;
		return cov / std::sqrt(va * vb);
	}
};

void weight_leads() {
	const float fs = 20.f, dt = 1.f / fs;
	const float targets[3] = {-1.f, 0.f, 1.f};
	double r1[3], r8[3];
	for (int m = 0; m < 3; m++) {
		Cycles c;
		c.reset();
		Tide t;
		float x[kCycles];
		Corr a, b;
		const long n = long(6 * 270 * fs);  // six cycle-8 periods
		for (long k = 0; k < n; k++) {
			c.step(kNoonE1, false, dt, x);
			const float v = t.step(x, targets[m], dt);
			if (k > long(2 * fs)) {  // past the 50 ms glide
				a.add(v, x[0]);
				b.add(v, x[7]);
			}
		}
		r1[m] = a.r();
		r8[m] = b.r();
	}
	CHECK(r1[0] > r8[0] + 0.3, "w=-1: c1 %g vs c8 %g", r1[0], r8[0]);
	CHECK(r8[2] > r1[2] + 0.3, "w=+1: c8 %g vs c1 %g", r8[2], r1[2]);
	CHECK(r1[0] > r1[1] && r1[1] > r1[2], "c1 correlation falls with w: %g %g %g", r1[0], r1[1], r1[2]);
	CHECK(r8[0] < r8[1] && r8[1] < r8[2], "c8 correlation rises with w: %g %g %g", r8[0], r8[1], r8[2]);

	// At w = 0 the eight weights are equal: TIDE = 5 limit(G mean(x)).
	Tide t;
	const float x[kCycles] = {0.9f, -0.2f, 0.4f, 0.1f, -0.7f, 0.3f, 0.5f, -0.1f};
	float mean = 0.f;
	for (int i = 0; i < kCycles; i++) mean += x[i];
	mean /= kCycles;
	const float v = t.step(x, 0.f, 1e-3f);
	const float want = 5.f * tideLimit(kTideGain * mean);
	CHECK(std::fabs(v - want) < 1e-4f, "equal weights at 0: %g vs %g", v, want);
}

void weight_glide() {
	const float dt = 1.f / 48000.f;
	Tide t;
	const float x[kCycles] = {1.f, -1.f, 0.5f, 0.f, -0.5f, 1.f, -1.f, 0.7f};
	t.step(x, 0.f, dt);
	CHECK(t.weight() == 0.f, "starts at 0: %g", t.weight());
	float prev = t.step(x, 0.f, dt);
	float maxStep = 0.f;
	float wAt[3] = {0, 0, 0};
	for (int k = 1; k <= 2402; k++) {
		const float v = t.step(x, 1.f, dt);
		maxStep = std::fmax(maxStep, std::fabs(v - prev));
		prev = v;
		if (k >= 2399 && k <= 2401) wAt[k - 2399] = t.weight();
	}
	const float want = 1.f - float(std::exp(-1.0));  // 63.2 % at 50 ms (2400 samples)
	CHECK(wAt[0] <= want + 1e-3f && wAt[2] >= want - 1e-3f, "63%% at 50 ms: %g %g %g vs %g", wAt[0], wAt[1], wAt[2], want);
	CHECK(std::fabs(wAt[1] - want) < 2e-3f, "w at 2400 = %g, want %g", wAt[1], want);
	// With the cycles held, a glide step moves TIDE by far less than a jump to the target would.
	CHECK(maxStep < 0.01f, "TIDE step during glide: %g", maxStep);
	Tide jump;
	jump.step(x, 0.f, dt);
	const float before = jump.step(x, 0.f, dt);
	Tide fresh;
	float after = 0.f;
	for (int k = 0; k < 48000; k++) after = fresh.step(x, 1.f, dt);
	CHECK(std::fabs(after - before) > 0.1f, "weight really changes TIDE (%g vs %g)", after, before);
	CHECK(std::fabs(fresh.weight() - 1.f) < 1e-4f, "settles at the target: %g", fresh.weight());

	// NaN target keeps the previous target.
	Tide n;
	for (int k = 0; k < 48000; k++) n.step(x, 1.f, dt);
	for (int k = 0; k < 48000; k++) n.step(x, std::nanf(""), dt);
	CHECK(std::fabs(n.weight() - 1.f) < 1e-4f, "NaN target keeps 1: %g", n.weight());
	// NaN in x never reaches the output.
	float bad[kCycles] = {std::nanf(""), 0, 0, 0, 0, 0, 0, 0};
	const float v = n.step(bad, 0.f, dt);
	CHECK(v == v && std::fabs(v) <= 5.f, "NaN cycle: %g", v);
}

void high_hysteresis() {
	HighGate g;
	const float mark = 2.f;
	int ups = 0, downs = 0;
	bool last = g.high;
	float upAt = 0, downAt = 0;
	const int n = 20000;
	for (int k = 0; k <= 2 * n; k++) {
		const float tide = k <= n ? mark - 1.f + 2.f * k / n : mark + 1.f - 2.f * (k - n) / n;
		g.step(tide, mark);
		if (g.high != last) {
			if (g.high) { ups++; upAt = tide; } else { downs++; downAt = tide; }
			last = g.high;
		}
	}
	CHECK(ups == 1 && downs == 1, "one switch each way: %d %d", ups, downs);
	CHECK(std::fabs(upAt - (mark + 0.05f)) < 1e-3f, "rises at %g", upAt);
	CHECK(std::fabs(downAt - (mark - 0.05f)) < 1e-3f, "falls at %g", downAt);

	// Noise of +-0.04 around the mark never chatters, from either state.
	for (int start = 0; start < 2; start++) {
		HighGate h;
		h.step(start ? mark + 1.f : mark - 1.f, mark);
		CHECK(h.high == (start == 1), "start state %d", start);
		int flips = 0;
		bool l = h.high;
		unsigned s = 12345;
		for (int k = 0; k < 10000; k++) {
			s = s * 1664525u + 1013904223u;
			const float noise = (float((s >> 8) & 0xffff) / 65535.f * 2.f - 1.f) * 0.04f;
			h.step(mark + noise, mark);
			if (h.high != l) { flips++; l = h.high; }
		}
		CHECK(flips == 0, "chatter from state %d: %d flips", start, flips);
	}
	// NaN tide leaves it unchanged; step returns the state.
	HighGate h;
	CHECK(h.step(mark + 1.f, mark) == true, "returns high");
	CHECK(h.step(std::nanf(""), mark) == true && h.high, "NaN keeps high");
	CHECK(h.step(mark - 1.f, mark) == false, "returns low");
	CHECK(h.step(std::nanf(""), mark) == false, "NaN keeps low");
}

} // namespace

int main() {
	limit_shape();
	bounded_24h();
	weight_leads();
	weight_glide();
	high_hysteresis();
	return rkatest::finish("neap/test_tide");
}

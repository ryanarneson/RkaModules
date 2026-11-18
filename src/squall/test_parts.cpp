// Squall's parts: Memory (the history and its loop), the gate, and (Task 4)
// the Swell follower.
#include "testing.hpp"
#include "squall/Gate.hpp"
#include "squall/Memory.hpp"
#include "squall/Swell.hpp"

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <initializer_list>
#include <random>

using namespace rka::squall;

static Event ev(float i) {
	Event e;
	e.gap = 1.f + i;
	e.value = i / 100.f;
	e.length = 0.25f;
	return e;
}

static void memory_loop_period() {
	for (int L : {4, 8, 16}) {
		Memory m;
		m.setLength(L);
		for (int i = 0; i < L; i++)
			m.push(ev((float) i));
		CHECK(m.canReplay(), "L %d: full history must replay", L);
		Event n = m.next();
		CHECK(n.value == ev(0.f).value && n.gap == ev(0.f).gap, "L %d: next is e0 (got value %g)", L, n.value);
		m.push(n);
		n = m.next();
		CHECK(n.value == ev(1.f).value, "L %d: then e1 (got value %g)", L, n.value);
		// Locked: L more replays reproduce the loop exactly.
		for (int k = 0; k < 3 * L; k++) {
			const Event a = m.next();
			CHECK(a.value == ev((float) ((k + 1) % L)).value, "L %d step %d: value %g", L, k, a.value);
			m.push(a);
		}
	}
}

static void memory_before_full() {
	Memory m;
	m.setLength(4);
	CHECK(!m.canReplay(), "empty history must not replay");
	for (int i = 0; i < 3; i++) {
		m.push(ev((float) i));
		CHECK(!m.canReplay(), "%d of 4 events must not replay", i + 1);
	}
	m.push(ev(3.f));
	CHECK(m.canReplay(), "4 of 4 replays");
	m.setLength(16);
	CHECK(!m.canReplay(), "4 of 16 must not replay");
}

static void loop_length_change_while_locked() {
	Memory m;
	m.setLength(16);
	for (int i = 0; i < 16; i++)
		m.push(ev((float) i));
	m.setLength(4);
	CHECK(m.length() == 4, "length %d", m.length());
	Event first[4];
	for (int k = 0; k < 4; k++) {
		const Event a = m.next();
		CHECK(a.value >= 0.f && a.value <= 0.15f + 1e-6f, "replay %d is a stored event (value %g)", k, a.value);
		first[k] = a;
		m.push(a);
	}
	for (int k = 0; k < 12; k++) {
		const Event a = m.next();
		CHECK(a.value == first[k % 4].value, "after the change the loop has period 4 (step %d)", k);
		m.push(a);
	}
	m.setLength(5);
	CHECK(m.length() == 4 || m.length() == 8, "setLength(5) clamps to 4, 8 or 16 (got %d)", m.length());
	m.setLength(100);
	CHECK(m.length() == 16, "setLength(100) clamps to 16 (got %d)", m.length());
}

static void memory_wraps() {
	Memory m;
	for (int i = 0; i < 40; i++)
		m.push(ev((float) i));
	CHECK(m.count() == 16, "count %d", m.count());
	CHECK(m.at(0).value == ev(24.f).value, "oldest is push 24 (value %g)", m.at(0).value);
	CHECK(m.at(15).value == ev(39.f).value, "newest is push 39 (value %g)", m.at(15).value);
	m.clear();
	CHECK(m.count() == 0 && !m.canReplay(), "clear empties the history");
}

static int highRun(Gate& g, int maxN) {
	int n = 0;
	while (n < maxN && g.process())
		n++;
	return n;
}

static void gate_length() {
	Gate g;
	g.setSampleRate(48000.f);
	CHECK(!g.process(), "a new gate is low");
	g.fire(0.1f);
	const int n = highRun(g, 100000);
	CHECK(std::abs(n - 4800) <= 1, "fire(0.1) at 48 kHz: high for %d samples, want 4800", n);
	CHECK(!g.high(), "low after its length");
}

static void gate_retrigger() {
	Gate g;
	g.setSampleRate(48000.f);
	g.fire(0.1f);
	for (int i = 0; i < 1000; i++)
		g.process();
	g.fire(0.05f);
	int low = 0;
	while (low < 1000 && !g.process())
		low++;
	CHECK(std::abs(low - 48) <= 1, "retrigger: low for %d samples, want 48", low);
	const int high = 1 + highRun(g, 100000);
	CHECK(std::abs(high - 2400) <= 2, "then high for %d samples, want 2400", high);
}


// Swell: settle time (to within 5 % of a 0 -> 5 V step), in seconds.
static double settleTime(float gap, float fs) {
	Swell s;
	s.setSampleRate(fs);
	s.setGap(gap);
	s.setState(0.f, 0.f);
	s.setTarget(5.f);
	long long n = 0;
	long long lastOut = 0;
	const long long maxN = (long long) (3.0 * gap * fs) + 10;
	for (; n < maxN; n++) {
		const float v = s.process();
		if (std::fabs(v - 5.f) > 0.25f)
			lastOut = n + 1;
	}
	return lastOut / (double) fs;
}

static void swell_settles() {
	Swell s;
	s.setSampleRate(48000.f);
	s.setGap(1.f);
	s.setState(0.f, 0.f);
	s.setTarget(5.f);
	float v = 0.f;
	for (int i = 0; i < 24000; i++)
		v = s.process();
	CHECK(v < 2.5f, "at half a gap Swell is at %g V, want under 2.5", v);
	for (int i = 0; i < 72000; i++)
		v = s.process();
	CHECK(std::fabs(v - 5.f) <= 0.25f, "at two gaps Swell is at %g V, want within 0.25 of 5", v);
}

static void swell_no_overshoot() {
	// A step from rest, up and down, never passes its target.
	for (float target : {5.f, -5.f}) {
		Swell s;
		s.setSampleRate(48000.f);
		s.setGap(0.5f);
		s.setState(0.f, 0.f);
		s.setTarget(target);
		float worst = 0.f;
		for (int i = 0; i < 480000; i++) {
			const float v = s.process();
			worst = std::max(worst, target > 0.f ? v - target : target - v);
		}
		CHECK(worst <= 1e-6f, "step to %g overshoots by %g", target, worst);
	}
	// Random targets with reversals mid-glide: never outside the targets seen.
	std::mt19937 rng(9);
	std::uniform_real_distribution<float> u(-5.f, 5.f);
	Swell s;
	s.setSampleRate(48000.f);
	s.setGap(0.2f);
	s.setState(0.f, 0.f);
	float lo = 0.f, hi = 0.f;
	for (int k = 0; k < 400; k++) {
		const float t = u(rng);
		lo = std::min(lo, t);
		hi = std::max(hi, t);
		s.setTarget(t);
		const int len = 500 + (int) (rng() % 20000);
		for (int i = 0; i < len; i++) {
			const float v = s.process();
			CHECK(v >= lo - 1e-5f && v <= hi + 1e-5f, "Swell %g outside the targets [%g, %g]", v, lo, hi);
			if (v < lo - 1e-5f || v > hi + 1e-5f)
				return;
		}
	}
}

// The largest per-sample change of slope over random targets.
static double worstSlopeChange(Swell& s, int targets, uint32_t seed) {
	std::mt19937 rng(seed);
	std::uniform_real_distribution<float> u(-5.f, 5.f);
	double prev = s.out(), prevSlope = 0.0, worst = 0.0;
	bool first = true;
	for (int k = 0; k < targets; k++) {
		s.setTarget(u(rng));
		for (int i = 0; i < 20000; i++) {
			const double v = s.process();
			const double slope = v - prev;
			if (!first)
				worst = std::max(worst, std::fabs(slope - prevSlope));
			first = false;
			prevSlope = slope;
			prev = v;
		}
	}
	return worst;
}

static void swell_no_corner() {
	Swell s;
	s.setSampleRate(48000.f);
	s.setGap(1.f);
	s.setState(0.f, 0.f);
	const double worst = worstSlopeChange(s, 100, 3);
	CHECK(worst < 1e-6, "largest change of slope %g V/sample^2, bound 1e-6", worst);
}

static void swell_pace_follows_gap() {
	const double base = settleTime(1.f, 1000.f);
	for (float gap : {0.125f, 9.f, 600.f}) {
		const float fs = 48000.f;
		const double t = settleTime(gap, fs);
		CHECK(std::fabs(t / gap / base - 1.0) < 0.05, "gap %g s at %g Hz: settles in %g s (%g gaps), want %g gaps", gap, fs, t, t / gap, base);
	}
}

static void swell_brakes() {
	Swell s;
	s.setSampleRate(48000.f);
	s.setGap(1.f);
	s.setState(0.f, 0.f);
	s.setTarget(5.f);
	double prev = 0.0, slope = 0.0;
	for (int i = 0; i < 24000; i++) {
		const double v = s.process();
		slope = v - prev;
		prev = v;
	}
	const double moving = slope;
	CHECK(moving > 1e-6, "mid-glide slope %g", moving);
	s.setFrozen(true);
	double worstChange = 0.0, last = slope;
	for (int i = 0; i < 2880; i++) {   // 60 ms
		const double v = s.process();
		slope = v - prev;
		prev = v;
		worstChange = std::max(worstChange, std::fabs(slope - last));
		last = slope;
	}
	CHECK(std::fabs(slope) < 0.01 * moving, "60 ms into Freeze the slope is %g of its %g", slope, moving);
	CHECK(worstChange < 0.2 * moving, "braking changes the slope by up to %g per sample (moving %g)", worstChange, moving);
	const double held = prev;
	for (int i = 0; i < 48000; i++)
		prev = s.process();
	CHECK(std::fabs(prev - held) < 0.01, "frozen Swell drifts from %g to %g", held, prev);
	s.setFrozen(false);
	const double v0 = prev, v1 = s.process();
	CHECK(std::fabs(v1 - v0) < 0.05 * moving, "on release the first step is %g (moving was %g): it starts from rest", v1 - v0, moving);
}

static void swell_stable_any_rate() {
	{
		Swell s;
		s.setSampleRate(1.f);
		s.setGap(0.125f);
		s.setState(0.f, 0.f);
		s.setTarget(5.f);
		for (int i = 0; i < 100; i++) {
			const float v = s.process();
			CHECK(std::isfinite(v) && v >= -1e-4f && v <= 5.0001f, "1 Hz, 0.125 s gap: %g", v);
		}
	}
	{
		// 60 s at 768 kHz: a step is ~5e-8 of the value, under half a float
		// ulp, so a float state would stall short of the target.
		const double t = settleTime(60.f, 768000.f);
		CHECK(t < 130.0, "768 kHz, 60 s gap: settles in %g s, want about 120", t);
	}
}

int main() {
	memory_loop_period();
	memory_before_full();
	loop_length_change_while_locked();
	memory_wraps();
	gate_length();
	gate_retrigger();
	swell_settles();
	swell_no_overshoot();
	swell_no_corner();
	swell_pace_follows_gap();
	swell_brakes();
	swell_stable_any_rate();
	return rkatest::finish("squall test_parts");
}

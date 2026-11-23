// Wake's time laws and clock follower (src/wake/Timing.hpp), against the
// numbers in docs/plans/2026-11-20-wake.md worked out here.
#include "wake/Timing.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace rka::wake;

void time_law() {
	CHECK(std::fabs(timeSeconds(0.f) - 0.01f) < 1e-6f, "knob 0 is %g s (want 10 ms)", timeSeconds(0.f));
	CHECK(std::fabs(timeSeconds(1.f) / 10.f - 1.f) < 1e-4f, "knob 1 is %g s (want 10 s)", timeSeconds(1.f));
	CHECK(std::fabs(timeSeconds(0.4924f) / 0.3f - 1.f) < 0.01f, "the default knob is %g s (want 300 ms)", timeSeconds(0.4924f));
	for (float k = 0.f; k <= 1.f; k += 0.05f) {
		const double want = 0.01 * std::pow(1000.0, (double) k);
		CHECK(std::fabs(timeSeconds(k) / want - 1.0) < 1e-4, "knob %.2f is %g s (want %g)", k, timeSeconds(k), want);
		CHECK(std::fabs(timeKnob(timeSeconds(k)) - k) < 1e-3f, "the inverse of knob %.2f is %g", k, timeKnob(timeSeconds(k)));
	}
	CHECK(timeSeconds(-3.f) == timeSeconds(0.f) && timeSeconds(7.f) == timeSeconds(1.f), "the knob clamps");
	CHECK(timeSeconds(std::nanf("")) == timeSeconds(0.f), "a NaN knob reads 10 ms");
	CHECK(timeKnob(0.f) == 0.f && timeKnob(99.f) == 1.f, "typed times clamp");
}

void multiples() {
	const double want[10] = {1.0 / 4, 1.0 / 3, 1.0 / 2, 2.0 / 3, 3.0 / 4, 1, 3.0 / 2, 2, 3, 4};
	for (int i = 0; i < 10; i++)
		CHECK(std::fabs(clockMultiple(i) - want[i]) < 1e-6, "multiple %d is %g (want %g)", i, clockMultiple(i), want[i]);
	CHECK(clockMultiple(-1) == 0.25f && clockMultiple(99) == 4.f, "steps clamp");
	CHECK(clockStep(0.5f, 0.f) == 5 && clockMultiple(5) == 1.f, "noon is x1 (step %d)", clockStep(0.5f, 0.f));
	CHECK(clockStep(0.f, 0.f) == 0 && clockStep(1.f, 0.f) == 9, "the knob's ends are 1/4 and x4");
	// 10 V is the travel: 9 steps. From noon (4.5, rounds to 5): +1 V is
	// 5.4 -> 5, +2 V is 6.3 -> 6, -1 V is 3.6 -> 4, -5 V is 0.
	CHECK(clockStep(0.5f, 1.f) == 5 && clockStep(0.5f, 2.f) == 6 && clockStep(0.5f, -1.f) == 4 && clockStep(0.5f, -5.f) == 0,
	      "CV moves along the steps (%d %d %d %d)", clockStep(0.5f, 1.f), clockStep(0.5f, 2.f), clockStep(0.5f, -1.f),
	      clockStep(0.5f, -5.f));
	CHECK(clockStep(0.9f, 10.f) == 9 && clockStep(0.1f, -10.f) == 0 && clockStep(std::nanf(""), 0.f) == 0, "the sum clamps");
	CHECK(!std::strcmp(clockName(0), "1/4") && !std::strcmp(clockName(5), "x1") && !std::strcmp(clockName(9), "x4"),
	      "the names");
}

// Typing what the tooltip shows (Wake.cpp: "%s clock" of clockName) sets
// the knob to that step; anything else names none.
void typed_multiples() {
	for (int step = 0; step < kMultiples; step++) {
		char shown[32], upper[32];
		std::snprintf(shown, sizeof shown, "%s clock", clockName(step));
		std::snprintf(upper, sizeof upper, "  %s   CLOCK ", clockName(step));
		for (char* c = upper; *c; c++)
			*c = (*c >= 'a' && *c <= 'z') ? (char) (*c - 'a' + 'A') : *c;
		const char* typed[3] = {shown, clockName(step), upper};
		for (const char* t : typed) {
			const int got = clockStepNamed(t);
			CHECK(got == step, "\"%s\" names step %d (want %d)", t, got, step);
			CHECK(clockStep(clockKnob(got), 0.f) == step, "the knob for \"%s\" is %g, step %d (want %d)", t, clockKnob(got),
			      clockStep(clockKnob(got), 0.f), step);
		}
	}
	const char* junk[] = {"", " ", "clock", "x", "x5", "1/5", "x1.", "x1.25", "x2 clocks", "x2x", "250", "250 ms", "1.5 s",
	                      "1/2 clock x", "2", "1 / 2"};
	for (const char* t : junk)
		CHECK(clockStepNamed(t) == -1, "\"%s\" names no step (got %d)", t, clockStepNamed(t));
	CHECK(clockStepNamed(nullptr) == -1, "null names no step");
	CHECK(clockStepNamed("x1.5") == 6 && clockStepNamed("x1") == 5, "x1.5 is not x1");
	CHECK(clockKnob(-3) == 0.f && clockKnob(99) == 1.f, "knob steps clamp");
}

void ratios() {
	const double f[7][2] = {{0.5, 1}, {2.0 / 3, 1}, {0.75, 1}, {1, 1}, {1, 0.75}, {1, 2.0 / 3}, {1, 0.5}};
	for (int r = 0; r < 7; r++) {
		float l, rt;
		ratioTimes(r, 0.6f, l, rt);
		CHECK(std::fabs(l - 0.6 * f[r][0]) < 1e-6 && std::fabs(rt - 0.6 * f[r][1]) < 1e-6,
		      "ratio %d of 600 ms gives %g / %g (want %g / %g)", r, l, rt, 0.6 * f[r][0], 0.6 * f[r][1]);
	}
	float l, r;
	ratioTimes(0, 0.012f, l, r);
	CHECK(l == 0.01f && r == 0.012f, "the shorter side stops at 10 ms (%g / %g)", l, r);
	ratioTimes(-4, 1.f, l, r);
	CHECK(l == 0.5f && r == 1.f, "ratios clamp");
	CHECK(!std::strcmp(ratioName(0), "L 1:2") && !std::strcmp(ratioName(3), "1:1") && !std::strcmp(ratioName(5), "R 2:3"),
	      "the names");
}

void fit() {
	CHECK(std::fabs(fitTime(45.f, 10.f) - 5.625f) < 1e-5f, "45 s fits as %g s (want 5.625)", fitTime(45.f, 10.f));
	CHECK(std::fabs(fitTime(0.001f, 10.f) - 0.016f) < 1e-6f, "1 ms fits as %g s (want 16 ms)", fitTime(0.001f, 10.f));
	CHECK(fitTime(2.f, 10.f) == 2.f, "a time in range is kept");
	CHECK(fitTime(std::nanf(""), 10.f) == 0.01f && fitTime(-1.f, 10.f) == 0.01f && fitTime(0.f, 10.f) == 0.01f,
	      "NaN, negative and zero read 10 ms");
	CHECK(fitTime(INFINITY, 10.f) == 10.f && fitTime(1e30f, 2.7f) == 2.7f, "infinite reads the longest time");
	CHECK(fitTime(4.f, 2.7f) == 2.f && fitTime(1.f, std::nanf("")) == 0.01f, "a short buffer's limit is used");
}

void clock_follower() {
	ClockFollower c;
	c.reset();
	CHECK(!c.locked(), "no edges: not locked");
	// 0.5 s at 48 kHz, 10 ms pulses.
	for (int i = 0; i < 24000 * 3 + 10; i++) {
		c.process(i % 24000 < 480 ? 10.f : 0.f);
		if (i == 100)
			CHECK(!c.locked(), "one edge: not locked");
		if (i == 24001)
			CHECK(c.locked() && std::fabs(c.periodSamples() - 24000.f) <= 1.f, "locked on the second edge at %g samples",
			      c.periodSamples());
	}
	// Stopped: the period holds.
	for (int i = 0; i < 500000; i++)
		c.process(0.f);
	CHECK(c.locked() && c.periodSamples() == 24000.f, "a stopped clock keeps its period (%g)", c.periodSamples());
	// Started again after a long pause: the pause is not a period.
	bool held = true;
	for (int i = 0; i < 24000 * 3; i++) {
		c.process(i % 24000 < 480 ? 10.f : 0.f);
		held = held && c.periodSamples() == 24000.f;
	}
	CHECK(held && c.locked(), "a clock restarted after a pause keeps its period throughout (%g)", c.periodSamples());
	// One that slows to a third is followed.
	for (int i = 0; i < 72000 * 3; i++)
		c.process(i % 72000 < 480 ? 10.f : 0.f);
	CHECK(c.periodSamples() == 72000.f, "a clock that slows to a third is followed (%g)", c.periodSamples());
	// A ripple on a high level makes no edges; nor does one under 1 V.
	c.reset();
	for (int i = 0; i < 48000; i++)
		c.process(i % 100 < 50 ? 5.f : 0.5f);
	CHECK(!c.locked(), "a high level dipping to 0.5 V is not a clock (it must fall under 0.1 V first)");
	c.reset();
	for (int i = 0; i < 48000; i++)
		c.process(i % 100 < 50 ? 0.9f : 0.f);
	CHECK(!c.locked(), "pulses under 1 V are not a clock");
	// The fastest clock there can be, and garbage: the period is never 0.
	c.reset();
	bool ok = true;
	for (int i = 0; i < 10000; i++) {
		c.process(i % 2 ? 10.f : 0.f);
		ok = ok && (!c.locked() || c.periodSamples() >= 1.f);
	}
	CHECK(ok && c.locked() && c.periodSamples() == 2.f, "an every-other-sample clock has period %g", c.periodSamples());
	c.reset();
	const float junk[5] = {std::nanf(""), INFINITY, -INFINITY, 1e30f, -1e30f};
	ok = true;
	for (int i = 0; i < 10000; i++) {
		c.process(junk[i % 5]);
		ok = ok && (!c.locked() || (c.periodSamples() >= 1.f && c.periodSamples() == c.periodSamples()));
	}
	CHECK(ok, "NaN and infinities never give a period under a sample");
}

} // namespace

int main() {
	time_law();
	multiples();
	typed_multiples();
	ratios();
	fit();
	clock_follower();
	return rkatest::finish("wake test_timing");
}

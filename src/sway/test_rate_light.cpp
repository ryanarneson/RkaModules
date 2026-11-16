// Unit tests for src/sway/RateLight.hpp. Run with `make test`.
#include "RateLight.hpp"
#include "testing.hpp"
#include <cmath>
#include <initializer_list>
#include <limits>

using rka::RateLight;
using rka::Voice;
using rka::VoiceInput;
using rka::rateLightHz;

// Up to 10 Hz the light is on for the first half of each cycle and off for
// the second.
static void testBlinksUpToTenHz() {
	RateLight light;
	for (double hz : {0.1, 1.0, 10.0}) {
		for (double p : {0.0, 0.1, 0.25, 0.49, 0.4999})
			CHECK(light.process(p, hz) == 1.f, "%g Hz, phase %g: %g", hz, p, light.process(p, hz));
		for (double p : {0.5, 0.51, 0.75, 0.99, 0.999999999})
			CHECK(light.process(p, hz) == 0.f, "%g Hz, phase %g: %g", hz, p, light.process(p, hz));
	}
}

// Above 10 Hz a blink is no longer readable: steady at half brightness,
// whatever the phase.
static void testSteadyAboveTenHz() {
	RateLight light;
	for (double hz : {10.01, 100.0, 8000.0})
		for (double p : {0.0, 0.25, 0.5, 0.75, 0.99})
			CHECK(light.process(p, hz) == 0.5f, "%g Hz, phase %g: %g", hz, p, light.process(p, hz));
}

// 0 Hz is what the module sends while the light is stopped: it stays as the
// phase left it (lit in the first half, dark in the second), never steady
// and never forced dark.
static void testZeroHzHoldsWhereThePhaseIs() {
	RateLight light;
	CHECK(light.process(0.25, 0.0) == 1.f, "0 Hz, phase 0.25: %g", light.process(0.25, 0.0));
	CHECK(light.process(0.75, 0.0) == 0.f, "0 Hz, phase 0.75: %g", light.process(0.75, 0.0));
}

// A rate that is not a rate lights nothing.
static void testBadRateIsDark() {
	RateLight light;
	const double nan = std::numeric_limits<double>::quiet_NaN();
	for (double p : {0.0, 0.25, 0.75}) {
		CHECK(light.process(p, nan) == 0.f, "NaN Hz, phase %g: %g", p, light.process(p, nan));
		CHECK(light.process(p, -1.0) == 0.f, "-1 Hz, phase %g: %g", p, light.process(p, -1.0));
		CHECK(light.process(p, -100.0) == 0.f, "-100 Hz, phase %g: %g", p, light.process(p, -100.0));
	}
}

// The light as the module drives it: channel 1's voice, its phase and
// rateLightHz().
static const double SR = 48000.0;

static float shown(const Voice& v, bool hold) {
	return RateLight().process(v.phasor.phase, rateLightHz(v, hold));
}

static void run(Voice& v, const VoiceInput& in, double seconds, bool clockConnected, bool oneShot) {
	for (long n = 0; n < long(seconds * SR); n++)
		v.tick(in, clockConnected, oneShot, 1.0 / SR, float(SR));
}

// HOLD freezes the light where the phase stopped, even at a rate where it
// would otherwise glow steadily.
static void testHoldFreezesTheLight() {
	for (double stopAt : {0.2, 0.7}) {
		Voice v;
		VoiceInput in;
		in.freeHz = 20.0;
		run(v, in, stopAt / 20.0, false, false);
		const double phase = v.phasor.phase;
		const float before = shown(v, false);
		CHECK(before == 0.5f, "20 Hz runs steady before HOLD: %g", before);
		in.hold = true;
		for (int i = 0; i < 10; i++) {
			run(v, in, 0.1, false, false);
			CHECK(v.phasor.phase == phase, "HOLD keeps the phase: %f vs %f", v.phasor.phase, phase);
			CHECK(rateLightHz(v, true) == 0.0, "held: %g Hz", rateLightHz(v, true));
			const float want = phase < 0.5 ? 1.f : 0.f;
			CHECK(shown(v, true) == want, "held at phase %f: %g, want %g", phase, shown(v, true), want);
		}
	}
}

// A one-shot that has run to the end of its cycle is dark, at any rate.
static void testFinishedOneShotIsDark() {
	for (double hz : {2.0, 20.0}) {
		Voice v;
		VoiceInput in;
		in.freeHz = hz;
		run(v, in, 0.2 / hz, false, true);
		CHECK(shown(v, false) == (hz > RateLight::BLINK_MAX_HZ ? 0.5f : 1.f), "%g Hz one-shot running: %g", hz,
			shown(v, false));
		run(v, in, 2.0 / hz, false, true);
		CHECK(v.phasor.parked, "%g Hz one-shot has finished", hz);
		CHECK(rateLightHz(v, false) == 0.0, "%g Hz finished one-shot: %g Hz", hz, rateLightHz(v, false));
		CHECK(shown(v, false) == 0.f, "%g Hz finished one-shot: %g", hz, shown(v, false));
	}
}

// Locked to a 1 Hz clock at x2 the light runs at 2 Hz, not at the free rate.
static void testClockedRateIsTheClockedRate() {
	Voice v;
	VoiceInput in;
	in.freeHz = 0.3;
	in.ratio = 1;   // x2
	int rises = 0;
	float last = shown(v, false);
	for (long n = 1; n <= long(4.0 * SR); n++) {
		in.edge = n % long(SR) == 0;
		v.tick(in, true, false, 1.0 / SR, float(SR));
		const float now = shown(v, false);
		if (n > long(2.0 * SR) && now > last)
			rises++;
		last = now;
	}
	CHECK(v.clock.locked(), "the clock has locked");
	CHECK(std::fabs(rateLightHz(v, false) - 2.0) < 1e-6, "clocked x2 at 1 Hz: %g Hz", rateLightHz(v, false));
	CHECK(rises == 4, "two seconds at 2 Hz light up 4 times: %d", rises);
}

// RESET after a finished one-shot starts a new cycle: lit again.
static void testResetLightsIt() {
	Voice v;
	VoiceInput in;
	in.freeHz = 20.0;
	run(v, in, 1.0, false, true);
	CHECK(shown(v, false) == 0.f, "finished one-shot: %g", shown(v, false));
	in.freeHz = 2.0;
	in.reset = true;
	v.tick(in, false, true, 1.0 / SR, float(SR));
	CHECK(!v.phasor.parked, "RESET releases the one-shot");
	CHECK(rateLightHz(v, false) == 2.0, "after RESET: %g Hz", rateLightHz(v, false));
	CHECK(shown(v, false) == 1.f, "after RESET: %g", shown(v, false));
}

int main() {
	testBlinksUpToTenHz();
	testSteadyAboveTenHz();
	testZeroHzHoldsWhereThePhaseIs();
	testBadRateIsDark();
	testHoldFreezesTheLight();
	testFinishedOneShotIsDark();
	testClockedRateIsTheClockedRate();
	testResetLightsIt();
	return rkatest::finish("test_rate_light");
}

// Voltages, triggers and timing across the shoal::Core boundary: CV and gate
// inputs, the input range, the 60 us tick clock and the held outputs.
#include <cmath>
#include <cstdlib>

#include "testing.hpp"
#include "test_util.hpp"
#include "CoreTest.hpp"
#include "shim/Volts.hpp"

using shoal::CoreTest;

namespace {

// CV1 steps 0 V -> 2 V. The firmware's smoothed value changes only on every
// third tick (the ADC scan rate) and settles with one-quarter smoothing.
void testAdcCadence() {
	shoal::Core c;
	run(c, 0.1);  // settle at 0 V (smoothing starts from code 0)
	const uint32_t before = CoreTest::smoothedCode(c, 0);
	// Smoothing from below truncates: it settles at most 1 code under.
	CHECK(before == 2730 || before == 2729, "settled at the 0 V code, got %u", before);

	c.setCv(0, 2.f);
	uint32_t prev = before;
	int phase = -1;
	for (int i = 0; i < 60; i++) {
		c.tick();
		const uint32_t now = CoreTest::smoothedCode(c, 0);
		if (now != prev) {
			if (phase < 0)
				phase = i % 3;
			CHECK(i % 3 == phase, "tick %d: value changed off the every-third-tick cadence", i);
		}
		prev = now;
	}
	CHECK(phase >= 0, "value changed");
	// Each scan closes a quarter of the gap: 20 scans in 60 ticks leave
	// 819 * 0.75^20 = 2.6 codes; 100 ticks (33 scans) leave under 1.
	run(c, 40 * 60e-6);
	const int target = 2730 - 819;  // 2 V
	CHECK(std::abs((int) CoreTest::smoothedCode(c, 0) - target) <= 1,
		"within 1 code of target after 100 ticks, got %u", CoreTest::smoothedCode(c, 0));
}

void testPitchRoundTrip() {
	shoal::Core c;
	for (int v = -3; v <= 6; v++) {
		c.setCv(0, (float) v);
		run(c, 0.02);
		const int32_t p = CoreTest::pitchIn(c, 0);
		CHECK(std::abs(p - v * 12 * 128) <= 8, "%d V: pitch %d, want %d", v, (int) p, v * 12 * 128);
	}
}

int32_t settledPitch(shoal::Core& c, float volts) {
	c.setCv(0, volts);
	run(c, 0.02);
	return CoreTest::pitchIn(c, 0);
}

void testInputRange() {
	shoal::Core c;
	// Hardware: the window is about -3.5 V .. +6.5 V.
	CHECK(settledPitch(c, -5.f) == settledPitch(c, -3.5f), "hardware: -5 V reads as -3.5 V");
	// The window ends at code 4095: -1365 codes = -3.33 V.
	CHECK(std::abs(settledPitch(c, -3.5f) - (-1365 * 15360 / 4096)) <= 8, "hardware: window ends at -3.33 V");
	CHECK(settledPitch(c, 6.5f) != settledPitch(c, 5.f), "hardware: 6.5 V differs from 5 V");

	c.setInputRange(shoal::InputRange::Bipolar5);
	CHECK(std::abs(settledPitch(c, 0.f)) <= 8, "bipolar: 0 V is pitch 0");
	CHECK(std::abs(settledPitch(c, 1.f) - 1536) <= 8, "bipolar: 1 V is 1536");
	CHECK(settledPitch(c, -5.f) != settledPitch(c, -4.f), "bipolar: -5 V and -4 V differ");
	CHECK(std::abs(settledPitch(c, -5.f) + 5 * 1536) <= 8, "bipolar: -5 V reached");
	CHECK(settledPitch(c, 5.f) == settledPitch(c, 6.5f), "bipolar: +5 V and +6.5 V read the same");
	CHECK(std::abs(settledPitch(c, 5.f) - 5 * 1536) <= 8, "bipolar: +5 V reached");

	// Switching back restores the hardware window.
	c.setInputRange(shoal::InputRange::Hardware);
	CHECK(std::abs(settledPitch(c, 0.f)) <= 8, "back to hardware: 0 V");
	CHECK(settledPitch(c, -5.f) == settledPitch(c, -3.5f), "back to hardware: clamps at -3.5 V");

	// Switching mid-run does not fault and settles within 60 ticks.
	c.setCv(0, 2.f);
	run(c, 0.02);
	c.setInputRange(shoal::InputRange::Bipolar5);
	run(c, 60 * 60e-6);
	CHECK(std::abs(CoreTest::pitchIn(c, 0) - 2 * 1536) <= 8 * 3, "settles after a mid-run switch, got %d",
		(int) CoreTest::pitchIn(c, 0));
	CHECK(allFinite(c), "finite after switching");
}

void testHostileVolts() {
	const float hostile[] = { NAN, INFINITY, -INFINITY, 100.f, -100.f };
	for (float v : hostile) {
		for (int app = 0; app < shoal::kNumApps; app++) {
			shoal::Core c;
			c.selectApp(app);
			for (int ch = 0; ch < 4; ch++) {
				c.setCv(ch, v);
				c.setGate(ch, v);
			}
			run(c, 0.1);
			CHECK(allFinite(c), "app %d, volts %g: outputs finite", app, (double) v);
		}
	}
	// NaN reads as 0 V.
	shoal::Core c;
	c.setCv(0, NAN);
	run(c, 0.02);
	CHECK(std::abs(CoreTest::pitchIn(c, 0)) <= 8, "NaN CV reads as 0 V");
	// NaN on a high gate reads as 0 V: the gate falls and can rise again.
	c.setGate(0, 10.f);
	c.setGate(0, NAN);
	run(c, 0.002);
	c.setGate(0, 10.f);  // rising edge again, must be latched without fault
	run(c, 0.002);
	CHECK(allFinite(c), "gate NaN then high");
}

void testShortPulseLatched() {
	shoal::Core c;  // app 0, CopierMaschine: clock on TR1, samples CV1
	c.setCv(0, 1.f);
	run(c, 0.05);
	c.setGate(0, 10.f);
	c.setGate(0, 0.f);  // both before the next tick
	run(c, 0.005);
	CHECK(std::fabs(c.out(0) - 1.f) < 0.01f, "short pulse clocked once: A = %g", (double) c.out(0));

	c.setCv(0, 2.f);
	run(c, 0.05);
	CHECK(std::fabs(c.out(0) - 1.f) < 0.01f, "no clock, no change: A = %g", (double) c.out(0));
	c.setGate(0, 10.f);
	c.setGate(0, 0.f);
	c.setGate(0, 10.f);
	c.setGate(0, 0.f);  // two edges, one tick
	run(c, 0.005);
	CHECK(std::fabs(c.out(0) - 2.f) < 0.01f, "A = %g", (double) c.out(0));
	CHECK(std::fabs(c.out(1) - 1.f) < 0.01f, "exactly one shift: B = %g", (double) c.out(1));
	CHECK(std::fabs(c.out(2) - 0.f) < 0.01f, "exactly one shift: C = %g", (double) c.out(2));
}

void testSchmittOnGate() {
	shoal::Core c;
	c.setCv(0, 1.f);
	run(c, 0.05);
	auto clocks = [&](float volts) {
		const float before = c.out(1);
		c.setGate(0, volts);
		run(c, 0.005);
		return c.out(1) != before;  // B changes when a clock shifts A down
	};
	// Prime B with a different value than A so a shift is visible.
	c.setGate(0, 10.f); run(c, 0.005); c.setGate(0, 0.f); run(c, 0.005);
	c.setCv(0, 2.f); run(c, 0.05);
	CHECK(!clocks(0.9f), "0.9 V does not clock");
	CHECK(clocks(1.1f), "1.1 V clocks");
	c.setCv(0, 3.f); run(c, 0.05);
	CHECK(!clocks(0.5f), "falling to 0.5 V (above 0.1 V): still high, no clock");
	CHECK(!clocks(1.1f), "wobble back to 1.1 V: no new clock");
	CHECK(!clocks(0.5f), "wobble 0.5 V: no clock");
	CHECK(!clocks(1.1f), "wobble 1.1 V: no clock");
	CHECK(!clocks(0.f), "low");
	CHECK(clocks(1.1f), "after a full fall, rising again clocks");
}

void testTickCountExact() {
	const double rates[] = { 8000, 44100, 48000, 96000, 768000 };
	for (double rate : rates) {
		shoal::Core c;
		shoal::TickClock k;
		k.setSampleRate(rate);
		const long samples = std::lround(rate * 10.0);
		for (long i = 0; i < samples; i++)
			for (int n = k.advance(); n > 0; n--)
				c.tick();
		const uint64_t t = CoreTest::ticks(c);
		CHECK(t == 166666 || t == 166667, "rate %g: %llu ticks in 10 s", rate, (unsigned long long) t);
	}
}

void testOutputsHold() {
	shoal::Core c;
	Lcg rng;
	c.setCv(0, 1.f);
	for (int i = 0; i < 400; i++) {
		if (i % 7 == 0)
			c.setCv(0, rng.volts(-3.f, 6.f));
		c.setGate(0, (rng.next() >> 28) < 6 ? 10.f : 0.f);
		c.tick();
		uint32_t codes[4];
		for (int ch = 0; ch < 4; ch++) {
			codes[ch] = c.outCode(ch);
			CHECK(c.out(ch) == shoal::dacToVolts(codes[ch]), "out == dacToVolts(outCode)");
		}
		// Inputs and gates between ticks do not move the outputs.
		c.setCv(0, rng.volts(-3.f, 6.f));
		c.setGate(0, 10.f);
		c.setGate(0, 0.f);
		for (int ch = 0; ch < 4; ch++)
			CHECK(c.outCode(ch) == codes[ch], "ch %d held between ticks", ch);
	}
}

} // namespace

int main() {
	testAdcCadence();
	testPitchRoundTrip();
	testInputRange();
	testHostileVolts();
	testShortPulseLatched();
	testSchmittOnGate();
	testTickCountExact();
	testOutputsHold();
	return rkatest::finish("test_io");
}

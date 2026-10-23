// Unit tests for src/sway/Voice.hpp. Run with `make test`.
#include "Voice.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

static const double SR = 48000.0;

static double gap(double a, double b) {
	const double d = std::fmod(b - a + 2.0, 1.0);
	return std::fmin(d, 1.0 - d);
}

// Channels that become active later must already be in step: run with one
// active channel, switch to four, and check they share channel 0's phase.
static void testLateChannelsFreeRunning() {
	VoiceBank bank;
	VoiceInput in[VoiceBank::SIZE];
	for (auto& v : in)
		v.freeHz = 0.37;
	// Only channel 0 is read until 1.3 s, then four; the bank's voices
	// can't tell, which is the point.
	for (long n = 0; n < long(5.3 * SR); n++)
		bank.tick(in, false, false, 1.0 / SR, float(SR));
	for (int c = 1; c < 4; c++)
		CHECK(std::fabs(bank.voices[c].phasor.phase - bank.voices[0].phasor.phase) < 1e-9,
			"channel %d phase %f vs channel 0 %f", c, bank.voices[c].phasor.phase, bank.voices[0].phasor.phase);
}

static void testLateChannelsClocked() {
	VoiceBank bank;
	VoiceInput in[VoiceBank::SIZE];
	for (long n = 1; n <= long(6.0 * SR); n++) {
		const bool edge = n % 24000 == 0;  // 2 Hz clock
		for (auto& v : in) {
			v.edge = edge;
			v.ratio = -3;  // /4
			v.freeHz = 2.0;
		}
		bank.tick(in, true, false, 1.0 / SR, float(SR));
	}
	for (int c = 1; c < 4; c++)
		CHECK(gap(bank.voices[0].phasor.phase, bank.voices[c].phasor.phase) < 1e-6,
			"clocked channel %d phase %f vs channel 0 %f", c, bank.voices[c].phasor.phase, bank.voices[0].phasor.phase);
}

// EOC: `wrapped` is true on exactly the samples where a cycle ends.
static int countWraps(VoiceBank& bank, VoiceInput* in, double seconds, bool clock, bool oneShot) {
	int wraps = 0;
	for (long n = 1; n <= long(seconds * SR); n++) {
		bank.tick(in, clock, oneShot, 1.0 / SR, float(SR));
		wraps += bank.voices[0].wrapped;
	}
	return wraps;
}

static void testEocFreeRunning() {
	VoiceBank bank;
	VoiceInput in[VoiceBank::SIZE];
	for (auto& v : in)
		v.freeHz = 1.0;
	CHECK(countWraps(bank, in, 3.5, false, false) == 3, "1 Hz for 3.5 s ends 3 cycles");
}

static void testEocOneShotOnce() {
	VoiceBank bank;
	VoiceInput in[VoiceBank::SIZE];
	for (auto& v : in)
		v.freeHz = 2.0;
	CHECK(countWraps(bank, in, 3.0, false, true) == 1, "one-shot ends exactly one cycle");
}

static void testEocOnClockResync() {
	// x1 at a 2 Hz clock, free rate far off: every locked edge ends a cycle.
	VoiceBank bank;
	VoiceInput in[VoiceBank::SIZE];
	int wraps = 0;
	for (long n = 1; n <= long(5.0 * SR); n++) {
		for (auto& v : in) {
			v.edge = n % 24000 == 0;
			v.ratio = 0;
			v.freeHz = 0.01;
		}
		bank.tick(in, true, false, 1.0 / SR, float(SR));
		wraps += bank.voices[0].wrapped;
	}
	// Edges every 0.5 s: 10 edges. The 2nd locks and restarts a cycle that
	// had barely begun (phase ~0.01, not an end); edges 3..10 each end one.
	CHECK(wraps == 8, "clocked x1 ended %d cycles, expected 8", wraps);
}

static void testResetIsNotEoc() {
	VoiceBank bank;
	VoiceInput in[VoiceBank::SIZE];
	for (auto& v : in)
		v.freeHz = 1.0;
	countWraps(bank, in, 0.6, false, false);
	for (auto& v : in)
		v.reset = true;
	bank.tick(in, false, false, 1.0 / SR, float(SR));
	CHECK(!bank.voices[0].wrapped, "RESET mid-cycle is not an end of cycle");
}

int main() {
	testLateChannelsFreeRunning();
	testLateChannelsClocked();
	testEocFreeRunning();
	testEocOneShotOnce();
	testEocOnClockResync();
	testResetIsNotEoc();
	return rkatest::finish("test_voice");
}

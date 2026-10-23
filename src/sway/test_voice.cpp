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

int main() {
	testLateChannelsFreeRunning();
	testLateChannelsClocked();
	return rkatest::finish("test_voice");
}

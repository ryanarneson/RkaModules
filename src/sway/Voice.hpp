#pragma once
#include "ClockSync.hpp"
#include "Phasor.hpp"
#include "RandomGen.hpp"
#include "Rate.hpp"

// Rack-independent per-channel timing for Sway: phase, clock follower and
// random values, advanced once per sample.
namespace rka {

// One sample's timing inputs for one channel.
struct VoiceInput {
	bool reset = false;
	bool edge = false;
	bool hold = false;
	// Frequency when not following a clock.
	double freeHz = 1.0;
	// Clock ratio index, -7..7.
	int ratio = 0;
};

struct Voice {
	Phasor phasor;
	ClockSync clock;
	RandomGen random;
	// Frequency used this sample, Hz.
	double freq = 0.0;

	void tick(const VoiceInput& in, bool clockConnected, bool oneShot, double dt, float sampleRate) {
		if (in.reset) {
			phasor.reset();
			clock.resetCount();
		}
		const bool sync = clock.process(in.edge, dt, in.ratio);
		freq = clockConnected && clock.locked() ? clampHz(clock.frequency(in.ratio), sampleRate) : in.freeHz;
		bool wrapped = phasor.step(freq, dt, in.hold, oneShot);
		// Restart on the clock. A resync just before the wrap still starts a
		// new cycle for the random outputs. Clock edges never restart a
		// one-shot; only RESET does.
		if (sync && !in.hold && !oneShot) {
			wrapped = wrapped || phasor.phase > 0.5;
			phasor.reset();
		}
		if (wrapped)
			random.advance();
	}
};

// Every voice advances every sample, active or not, so a channel that
// becomes active later (CHANNELS raised, a poly cable patched) is already in
// step with the others.
struct VoiceBank {
	static const int SIZE = 16;
	Voice voices[SIZE];

	VoiceBank() {
		for (int c = 0; c < SIZE; c++)
			voices[c].random = RandomGen(c + 1);
	}

	// in: one entry per voice (SIZE).
	void tick(const VoiceInput* in, bool clockConnected, bool oneShot, double dt, float sampleRate) {
		for (int c = 0; c < SIZE; c++)
			voices[c].tick(in[c], clockConnected, oneShot, dt, sampleRate);
	}
};

} // namespace rka

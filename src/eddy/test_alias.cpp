// Eddy's aliasing (docs/DSP-ALIASING.md; TESTING.md rule 16), measured with
// rkatest::AliasMeter (src/testing_alias.hpp) at 44.1 and 48 kHz: what comes
// out of the read faster than speed 1. `test_alias --table` prints every
// measurement instead of checking it.
#include "eddy/test_rig.hpp"
#include "testing.hpp"
#include "testing_alias.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rig;
using rkatest::AliasMeter;

namespace {

bool table = false;
const double RATES[2] = {44100.0, 48000.0};
const size_t N = 32768;

// The knob that gives |speed| s in (0, 2] (the law's straight stretches).
float knobFor(double s) {
	const double a = s < 0 ? -s : s;
	const double k = a <= 1.0 ? 0.04 + a * 0.92 : 1.04 + (a - 1.0) * 0.96;
	return (float) (s < 0 ? -k : k);
}
// Speed s by the knob alone (|s| <= 2) or by knob and V/OCT (an octave up).
struct Speed {
	float knob, voct;
};
Speed speedFor(double s) {
	const double a = s < 0 ? -s : s;
	if (a <= 2.0)
		return {knobFor(s), 0.f};
	return {knobFor(s / 2.0), 1.f};
}
double speedOf(Speed sp) {
	return rka::eddy::speedFinal(rka::eddy::speedKnob(sp.knob), sp.voct);
}

// A tape holding a 5 V sine at `ratio` of the rate, played at speed s from
// its start (its end going backwards): N samples of the output after the
// voice is in and the speed settled.
std::vector<float> playTone(double fs, double ratio, double s) {
	Rig r((float) fs);
	// The speed is set before the first control tick, which takes it without
	// a glide.
	const Speed sp = speedFor(s);
	r.f.params[F::SPEED] = sp.knob;
	r.f.voct = sp.voct;
	r.use(r.sine(6.0, ratio * fs, 5.f));
	r.press(&F::playPress);
	r.run(2048);
	std::vector<float> out(N);
	for (size_t i = 0; i < N; i++) {
		r.step();
		out[i] = r.r.l;
	}
	return out;
}

struct ReadResult {
	double landsHz;   // where the tone should be heard, unfolded
	double otherDb;   // strongest other component, dB re 5 V
	double levelDb;   // the tone's level, dB re 5 V (if below Nyquist)
};
ReadResult readTone(double fs, double ratio, double s) {
	const std::vector<float> x = playTone(fs, ratio, s);
	const AliasMeter m(x.data(), x.size(), fs);
	const double sp = speedOf(speedFor(s));
	ReadResult r;
	r.landsHz = ratio * fs * (sp < 0 ? -sp : sp);
	std::vector<double> want;
	if (r.landsHz < fs / 2.0)
		want.push_back(r.landsHz);
	r.otherDb = m.otherDb(want, 5.0);
	r.levelDb = want.empty() ? -999.0 : AliasMeter::db(m.amplitude(r.landsHz) / 5.0);
	return r;
}

const double TONES[4] = {0.1, 0.2, 0.3, 0.4};

// ---- The read.

// Above speed 1 the read is band-limited (BandLimitedRead.hpp). The target
// (task 10c, B3): anything the speed puts above Nyquist comes out at least
// 50 dB below a full tone, at speeds 2 and 4, at 44.1 and 48 kHz, in both
// directions; content landing up to 0.8 of Nyquist keeps its level within
// 0.1 dB. Measured 2026-11-22 (5 V tones at 0.1 .. 0.4 of the rate): the
// strongest alias -68.0 dB (a tone landing at 1.2 x Nyquist), levels within
// 0.01 dB; test_band_read's finer sweep finds -57.1 dB at worst (a tone
// landing on Nyquist itself). Before the change the aliases were the whole
// tone (0 dB at speeds 2 to 4, -2.8 dB at 1.5).
const double FAST_ALIAS_DB = -50.0;
const double PASS_DB = 0.1;

void fastRead() {
	for (double fs : RATES)
		for (double s : {1.5, 2.0, 2.37, 3.0, 4.0, -1.5, -2.0, -3.0, -4.0})
			for (double ratio : TONES) {
				const ReadResult r = readTone(fs, ratio, s);
				const double nyq = fs / 2.0;
				if (table) {
					std::printf("fast  fs %5.0f speed %+5.2f tone %.1f fs: lands %.2f x Nyquist  other %7.1f dB  level %7.2f dB\n",
					            fs, s, ratio, r.landsHz / nyq, r.otherDb, r.levelDb);
					continue;
				}
				CHECK(r.otherDb <= FAST_ALIAS_DB, "fs %.0f, speed %+.2f, a tone at %.1f of the rate (lands at %.2f x Nyquist): the strongest other component is %.1f dB re 5 V",
				      fs, s, ratio, r.landsHz / nyq, r.otherDb);
				if (r.landsHz <= 0.8 * nyq)
					CHECK(std::fabs(r.levelDb) <= PASS_DB, "fs %.0f, speed %+.2f, a tone at %.1f of the rate keeps its level: %.3f dB", fs, s,
					      ratio, r.levelDb);
			}
}

// Through speed 1 and the crossfade above it (1 .. 1.25) the level of a
// tone in the passband does not step. Measured 2026-11-22: within 0.02 dB.
void acrossOne() {
	for (double fs : RATES)
		for (double s : {1.0, 1.01, 1.1, 1.24, 1.26, 1.5, -1.01, -1.1}) {
			const ReadResult r = readTone(fs, 0.1, s);
			if (table) {
				std::printf("cross fs %5.0f speed %+5.2f tone 0.1 fs: level %7.3f dB  other %7.1f dB\n", fs, s, r.levelDb, r.otherDb);
				continue;
			}
			CHECK(std::fabs(r.levelDb) <= PASS_DB, "fs %.0f, speed %+.2f: a tone at 0.1 of the rate keeps its level (%.3f dB)", fs, s,
			      r.levelDb);
		}
}

// ---- Helpers.

// The tape's frames [a, a + n) of side 0.
std::vector<float> tapeAt(const Tape& t, size_t a, size_t n) {
	return std::vector<float>(t.ch[0] + a, t.ch[0] + a + n);
}

// ---- What is written back above speed 1.

// The voices write the tape in place (Wear from each frame's own value, Dub
// from the input), never what they read: the band limit on the read does not
// reach the tape. A tone at 0.4 of the rate (above the output's Nyquist at
// speed 2) worn one pass at speed 2 is on the tape as it is after one pass
// at speed 1; at Wear 0 nothing is written at all.
void writtenBack() {
	for (double fs : RATES) {
		double level[2];
		for (int k = 0; k < 2; k++) {
			const double s = k == 0 ? 1.0 : 2.0;
			Rig r((float) fs);
			r.f.params[F::SPEED] = knobFor(s);
			r.f.params[F::WEAR] = 1.f;
			TapeSet* set = r.sine(2.0, 0.4 * fs, 2.f);
			r.use(set);
			r.press(&F::playPress);
			r.run((long) (0.75 * fs / s));   // past frame 0.2 s + N / 2 of the tape
			const std::vector<float> x = tapeAt(set->tape, (size_t) (0.2 * fs), 16384);
			const AliasMeter m(x.data(), x.size(), fs);
			level[k] = AliasMeter::db(m.amplitude(0.4 * fs) / 2.0);
		}
		if (table)
			std::printf("write fs %5.0f a 0.4 fs tone worn one pass: at speed 1 %.3f dB, at speed 2 %.3f dB\n", fs, level[0], level[1]);
		else
			CHECK(std::fabs(level[1] - level[0]) < 0.5, "fs %.0f: a tone above the output's Nyquist worn at speed 2 stays on the tape (%.2f dB) as at speed 1 (%.2f dB)",
			      fs, level[1], level[0]);

		// Wear 0, Dub off, speed 2 for many turns: the tape is untouched.
		Rig r((float) fs);
		r.f.params[F::SPEED] = knobFor(2.0);
		TapeSet* set = r.sine(0.5, 0.4 * fs, 5.f);
		r.use(set);
		const Snapshot before(set->tape);
		r.press(&F::playPress);
		r.run((long) (2 * fs));
		CHECK(table || before.equals(set->tape), "fs %.0f: at Wear 0 eight turns at speed 2 leave the tape as it was, to the bit", fs);
	}
}

} // namespace

int main(int argc, char** argv) {
	table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
	fastRead();
	acrossOne();
	writtenBack();
	return table ? 0 : rkatest::finish("eddy/test_alias");
}

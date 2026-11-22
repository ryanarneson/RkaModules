// Eddy's aliasing (docs/DSP-ALIASING.md; TESTING.md rule 16), measured with
// rkatest::AliasMeter (src/testing_alias.hpp) at 44.1 and 48 kHz: what comes
// out of the read at every speed, what wear and the limits add, and what dub
// lays on the tape away from speed 1. `test_alias --table` prints every
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

// Slower than 1 the Hermite read leaves images (it is the interpolator's
// quality, kept as it was, to the bit). Bounds: measured 2026-11-22 (the
// same at 44.1 and 48 kHz, either direction, speed 0.5 or 0.25) plus 3 dB:
// tone 0.1 of the rate -50.9 dB, 0.2 -30.6, 0.3 -18.9, 0.4 -11.2. At speed 1
// the read is exact (below -150 dB).
const double SLOW_BOUND_DB[4] = {-47.9, -27.6, -15.9, -8.2};

void slowRead() {
	for (double fs : RATES)
		for (double s : {0.5, 0.25, -0.5, -0.25, 1.0, -1.0})
			for (int t = 0; t < 4; t++) {
				const double ratio = TONES[t];
				const ReadResult r = readTone(fs, ratio, s);
				if (table) {
					std::printf("slow  fs %5.0f speed %+5.2f tone %.1f fs: other %7.1f dB  level %7.2f dB\n", fs, s, ratio,
					            r.otherDb, r.levelDb);
					continue;
				}
				const double bound = (s == 1.0 || s == -1.0) ? -150.0 : SLOW_BOUND_DB[t];
				CHECK(r.otherDb <= bound, "fs %.0f, speed %+.2f, a tone at %.1f of the rate: images at %.1f dB re 5 V (bound %.1f)", fs, s,
				      ratio, r.otherDb, bound);
			}
}

// ---- Wear's saturation, in the loop.

// The tape's frames [a, a + n) of side 0.
std::vector<float> tapeAt(const Tape& t, size_t a, size_t n) {
	return std::vector<float>(t.ch[0] + a, t.ch[0] + a + n);
}

// Wear's saturation (the soft bound at drive 2, Wear 1) is in the loop: each
// pass saturates the tape again, and the harmonics above Nyquist fold back
// onto it. Not fixed in this task (task 10c): pinned at today's level. The
// worst over passes 1, 2, 4, 8 and 16, at 44.1 or 48 kHz, measured
// 2026-11-22, plus 3 dB. The tone's harmonics and a 400 Hz skirt round each
// (the dropouts' sidebands) are expected; hiss and dropouts are in the rest.
//   5 kHz at 5 V: -52.5 dB    5 kHz at 9 V: -31.5 dB
//   9 kHz at 5 V: -43.4 dB    9 kHz at 9 V: -10.5 dB (pass 16 at 44.1 kHz,
//   the tone itself by then 30 dB down from wear's high loss)
double wearBound(double hz, float volts) {
	if (hz < 6000.0)
		return volts < 6.f ? -49.5 : -28.5;
	return volts < 6.f ? -40.4 : -7.5;
}

void wearLoop() {
	for (double fs : RATES)
		for (double hz : {5000.0, 9000.0})
			for (float volts : {5.f, 9.f}) {
				// A second of tape, a whole number of cycles, so the join is seamless.
				Rig r((float) fs);
				TapeSet* set = r.sine(1.0, hz, volts);
				r.use(set);
				r.f.params[F::WEAR] = 1.f;
				r.press(&F::playPress);
				double worst = -999.0;
				for (int pass = 1; pass <= 16; pass++) {
					r.run((long) fs);
					if (pass == 1 || pass == 2 || pass == 4 || pass == 8 || pass == 16) {
						const std::vector<float> x = tapeAt(set->tape, (size_t) (0.2 * fs), 16384);
						const AliasMeter m(x.data(), x.size(), fs);
						const double d = m.aliasDb(AliasMeter::harmonics(hz, fs), 400.0);
						worst = d > worst ? d : worst;
						if (table)
							std::printf("wear  fs %5.0f %5.0f Hz at %.0f V, pass %2d: other %7.1f dB re the tone (at %.0f Hz)\n", fs, hz,
							            volts, pass, d, m.strongestOther(AliasMeter::harmonics(hz, fs), 400.0).hz);
					}
				}
				if (table)
					continue;
				const double bound = wearBound(hz, volts);
				CHECK(worst <= bound, "fs %.0f, %.0f Hz at %.0f V worn 16 passes at Wear 1: other components on the tape at %.1f dB re the tone (bound %.1f)",
				      fs, hz, volts, worst, bound);
			}
}

// ---- The write guard and the output limit.

// The output limit and the write guard share the soft bound (identity to
// 8 V, then into 10 V): above the knee its harmonics fold back. Not fixed:
// pinned. Measured 2026-11-22, the same within 0.2 dB for both and at both
// rates, plus 3 dB: 5 kHz at 9 V -55.4 dB, at 12 V -31.8 dB; 9 kHz at 9 V
// -53.3 dB, at 12 V -23.2 dB.
double limitBound(double hz, float volts) {
	if (hz < 6000.0)
		return volts < 10.f ? -52.4 : -28.8;
	return volts < 10.f ? -50.3 : -20.2;
}

void limits() {
	for (double fs : RATES)
		for (double hz : {5000.0, 9000.0})
			for (float volts : {9.f, 12.f}) {
				// The output limit: no tape, the input straight through.
				{
					Rig r((float) fs, 1, false);
					std::vector<float> out(N);
					for (size_t i = 0; i < N + 512; i++) {
						r.step((float) (volts * std::sin(2 * PI * hz * (double) i / fs)), 0.f);
						if (i >= 512)
							out[i - 512] = r.r.l;
					}
					const AliasMeter m(out.data(), out.size(), fs);
					const double d = m.aliasDb(AliasMeter::harmonics(hz, fs));
					if (table)
						std::printf("limit fs %5.0f %5.0f Hz at %4.1f V: other %7.1f dB re the tone\n", fs, hz, volts, d);
					else
						CHECK(d <= limitBound(hz, volts), "fs %.0f, %.0f Hz at %.0f V through the output limit: other at %.1f dB re the tone (bound %.1f)",
						      fs, hz, volts, d, limitBound(hz, volts));
				}
				// The write guard: dubbed at speed 1 onto a silent tape.
				{
					Rig r((float) fs);
					TapeSet* set = r.loaded((size_t) (2 * fs), 0, [](size_t, float& l, float& rr) { l = rr = 0.f; });
					r.use(set);
					r.f.params[F::DUB_LEVEL] = 1.f;
					r.press(&F::playPress);
					r.step();
					r.press(&F::dubPress);
					for (long i = 0; i < (long) fs; i++) {
						const float x = (float) (volts * std::sin(2 * PI * hz * (double) i / fs));
						r.step(x, x);
					}
					const std::vector<float> x = tapeAt(set->tape, (size_t) (0.2 * fs), N);
					const AliasMeter m(x.data(), x.size(), fs);
					const double d = m.aliasDb(AliasMeter::harmonics(hz, fs));
					if (table)
						std::printf("guard fs %5.0f %5.0f Hz at %4.1f V: other %7.1f dB re the tone\n", fs, hz, volts, d);
					else
						CHECK(d <= limitBound(hz, volts), "fs %.0f, %.0f Hz at %.0f V dubbed through the write guard: other at %.1f dB re the tone (bound %.1f)",
						      fs, hz, volts, d, limitBound(hz, volts));
				}
			}
}

// ---- Dub away from speed 1.

// Dub away from speed 1 (TapeWriter, Tape.hpp). Not fixed: pinned.
// Slower (0.5): each frame is the input averaged over the head's travel (a
// box filter): an input that lands above the tape's Nyquist (15 kHz -> 30
// kHz) folds back at -12.7 / -10.2 dB re 5 V (44.1 / 48 kHz); one that fits
// (5 kHz -> 10 kHz) is clean (under -150 dB) at -1 dB of level. Faster (2):
// the input is interpolated linearly between samples, leaving images:
// 5 kHz -> 2.5 kHz with an image at -30.1 / -31.5 dB, 15 kHz -> 7.5 kHz at
// -11.7 / -13.1 dB. Measured 2026-11-22; bounds 3 dB over the worse rate.
double dubBound(double s, double hz) {
	if (s < 1.0)
		return hz < 10000.0 ? -100.0 : -7.2;
	return hz < 10000.0 ? -27.1 : -8.7;
}

void dubSpeeds() {
	for (double fs : RATES)
		for (double s : {0.5, 2.0})
			for (double hz : {5000.0, 15000.0}) {
				Rig r((float) fs);
				TapeSet* set = r.loaded((size_t) (4 * fs), 0, [](size_t, float& l, float& rr) { l = rr = 0.f; });
				r.f.params[F::SPEED] = knobFor(s);
				r.use(set);
				r.f.params[F::DUB_LEVEL] = 1.f;
				r.press(&F::playPress);
				r.step();
				r.press(&F::dubPress);
				for (long i = 0; i < (long) (1.2 * fs); i++) {
					const float x = (float) (5.0 * std::sin(2 * PI * hz * (double) i / fs));
					r.step(x, x);
				}
				const std::vector<float> x = tapeAt(set->tape, (size_t) (0.1 * fs), 16384);
				const AliasMeter m(x.data(), x.size(), fs);
				const double lands = hz / s;   // on the tape, played back at speed 1
				std::vector<double> want;
				if (lands < fs / 2.0)
					want.push_back(lands);
				const double d = m.otherDb(want, 5.0);
				const double lvl = want.empty() ? -999.0 : AliasMeter::db(m.amplitude(lands) / 5.0);
				if (table) {
					std::printf("dub   fs %5.0f speed %.1f, %5.0f Hz in: on the tape at %6.0f Hz  other %7.1f dB re 5 V  level %6.2f dB (at %.0f Hz)\n",
					            fs, s, hz, lands, d, lvl, m.strongestOther(want).hz);
					continue;
				}
				const double bound = dubBound(s, hz);
				CHECK(d <= bound, "fs %.0f, %.0f Hz dubbed at speed %.1f: other on the tape at %.1f dB re 5 V (bound %.1f)", fs, hz, s, d,
				      bound);
			}
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

// ---- Speed moved at audio rate.

// Speed moved at audio rate: the 60 ms glide takes a 200 Hz CV of +-1 speed
// down to about +-0.013, and what comes out is FM: the tone and sidebands
// at multiples of 200 Hz, expected. Anything else (the control rate's steps,
// interpolation) is pinned. Measured 2026-11-22 before the band-limited
// read: -95.9 dB (44.1 kHz), -114.6 dB (48 kHz).
const double FM_BOUND_DB = -92.9;   // the same after: -95.9 and -114.6, plus 3 dB

void speedModulation() {
	for (double fs : RATES) {
		Rig r((float) fs);
		r.use(r.sine(6.0, 1000.0, 5.f));
		r.f.params[F::SPEED_ATT] = 1.f;
		r.press(&F::playPress);
		std::vector<float> out(N);
		for (long i = 0; i < (long) N + 4800; i++) {
			r.f.speedCv = (float) (2.5 * std::sin(2 * PI * 200.0 * (double) i / fs));
			r.step();
			if (i >= 4800)
				out[(size_t) (i - 4800)] = r.r.l;
		}
		const AliasMeter m(out.data(), out.size(), fs);
		const double d = m.aliasDb(AliasMeter::harmonics(200.0, fs));
		if (table)
			std::printf("fm    fs %5.0f 1 kHz tone, Speed CV 200 Hz +-2.5 V at full: other %7.1f dB re the tone (at %.0f Hz)\n", fs, d,
			            m.strongestOther(AliasMeter::harmonics(200.0, fs)).hz);
		else
			CHECK(d <= FM_BOUND_DB, "fs %.0f, Speed CV at 200 Hz: other than the tone and its sidebands at %.1f dB (bound %.1f)", fs, d,
			      FM_BOUND_DB);
	}
}

} // namespace

int main(int argc, char** argv) {
	table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
	fastRead();
	acrossOne();
	slowRead();
	wearLoop();
	limits();
	dubSpeeds();
	writtenBack();
	speedModulation();
	return table ? 0 : rkatest::finish("eddy/test_alias");
}

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
// White noise against the frames it passed (measured 2026-11-22): at 1
// +0.000 dB (the frames themselves), at 0.5, 0.25 and 1/32 -0.094 to
// -0.100 dB (the speed-1 kernel's roll-off from 0.8 of Nyquist; the Hermite
// read lost 0.86 to 0.91 dB), at 2 -3.57 dB (half the band would land above
// Nyquist and is taken out; the roll-off from 0.8 of the new Nyquist takes
// the rest).
const double NOISE_AT_2_DB = -3.57;
const double NOISE_TOL_DB = 0.2;

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

// Just above 1 the band limit holds too (fix round 1, I2): from x1.02 up,
// tape content landing just above Nyquist (at 1.04 of it) and well above
// it (a tone at 0.49 of the rate) comes out under FAST_ALIAS_DB. Measured
// 2026-11-22: see NEAR_ONE in the report; the reader's own sweep finds
// -57.4 dB at worst over x1.02 .. x1.26.
void nearOne() {
	for (double fs : RATES)
		for (double s : {1.05, 1.1, 1.15, 1.2, 1.26, 1.5, 3.0, -1.1})
			for (int far = 0; far < 2; far++) {
				const double a = s < 0 ? -s : s;
				const double ratio = far ? 0.49 : std::fmin(0.499, 0.52 / a);
				if (2.0 * ratio * a <= 1.0)
					continue;   // nothing lands above Nyquist
				const ReadResult r = readTone(fs, ratio, s);
				if (table) {
					std::printf("near  fs %5.0f speed %+5.2f tone %.3f fs: lands %.2f x Nyquist  other %7.1f dB\n", fs, s, ratio,
					            r.landsHz / (fs / 2.0), r.otherDb);
					continue;
				}
				CHECK(r.otherDb <= FAST_ALIAS_DB, "fs %.0f, speed %+.2f, a tone at %.3f of the rate (lands at %.2f x Nyquist): other %.1f dB re 5 V",
				      fs, s, ratio, r.landsHz / (fs / 2.0), r.otherDb);
			}
}

// Through speed 1 the level of a tone in the passband does not step.
// Measured 2026-11-22: within 0.003 dB.
void acrossOne() {
	for (double fs : RATES)
		for (double s : {1.0, 1.01, 1.1, 1.24, 1.26, 1.5, -1.01, -1.1, 0.99, 0.9, -0.99}) {
			const ReadResult r = readTone(fs, 0.1, s);
			if (table) {
				std::printf("cross fs %5.0f speed %+5.2f tone 0.1 fs: level %7.3f dB  other %7.1f dB\n", fs, s, r.levelDb, r.otherDb);
				continue;
			}
			CHECK(std::fabs(r.levelDb) <= PASS_DB, "fs %.0f, speed %+.2f: a tone at 0.1 of the rate keeps its level (%.3f dB)", fs, s,
			      r.levelDb);
		}
}

// A tape tone at 0.42 of the rate (where the speed-1 and the faster kernels
// differ most) with the speed moved through 1 by the Speed CV: a slow glide
// (0.9 to 1.1 and back over 4 s, the RMS in 10 ms blocks, the largest
// change from one block to the next) and an audio-rate wobble (a 200 Hz CV
// of +-0.375 V, about +-0.002 of speed after the 60 ms glide: everything
// but the tone and its first two FM sidebands either side). Bounds: the
// measurement (2026-11-22) plus 3 dB, or plus 0.05 dB for the step.
// Measured 2026-11-22: the largest 10 ms step 0.221 / 0.226 dB (44.1 / 48
// kHz), which is the faster kernel's roll-off sliding past the tone as it
// rises (0.84 to 0.92 of Nyquist), not a step at 1; the wobble -76.5 /
// -76.4 dB re the tone.
const double GLIDE_STEP_DB = 0.3;
const double WOBBLE_BOUND_DB = -73.4;

void glideThroughOne() {
	for (double fs : RATES) {
		{
			Rig r((float) fs);
			r.f.params[F::SPEED_ATT] = 1.f;
			r.f.speedCv = -0.25f;
			r.use(r.sine(10.0, 0.42 * fs, 5.f));
			r.press(&F::playPress);
			r.run((long) (0.5 * fs));
			const long blk = (long) (0.01 * fs), total = (long) (4.0 * fs);
			double prev = -1.0, worst = 0.0;
			for (long b = 0; b < total / blk; b++) {
				double e = 0.0;
				for (long i = 0; i < blk; i++) {
					const double t = (double) (b * blk + i) / (double) total;   // 0 .. 1
					r.f.speedCv = (float) (t < 0.5 ? -0.25 + t : 0.75 - t);       // -0.25 .. +0.25 .. -0.25 V
					r.step();
					e += (double) r.r.l * r.r.l;
				}
				const double lv = 10.0 * std::log10(e / (double) blk);
				if (prev > -1.0 + 1e-9 || b > 0)
					worst = std::fmax(worst, std::fabs(lv - prev));
				prev = lv;
			}
			if (table)
				std::printf("glide fs %5.0f a 0.42 fs tone, speed 0.9 -> 1.1 -> 0.9: largest 10 ms level step %.3f dB\n", fs, worst);
			else
				CHECK(worst <= GLIDE_STEP_DB, "fs %.0f: gliding through speed 1 the level of a 0.42 fs tone steps by %.3f dB in 10 ms (bound %.2f)",
				      fs, worst, GLIDE_STEP_DB);
		}
		{
			Rig r((float) fs);
			r.f.params[F::SPEED_ATT] = 1.f;
			r.use(r.sine(10.0, 0.42 * fs, 5.f));
			r.press(&F::playPress);
			std::vector<float> out(N);
			for (long i = 0; i < (long) N + 9600; i++) {
				r.f.speedCv = (float) (0.375 * std::sin(2 * PI * 200.0 * (double) i / fs));
				r.step();
				if (i >= 9600)
					out[(size_t) (i - 9600)] = r.r.l;
			}
			const AliasMeter m(out.data(), out.size(), fs);
			const double f0 = 0.42 * fs;
			const std::vector<double> want = {f0, f0 - 200.0, f0 + 200.0, f0 - 400.0, f0 + 400.0};
			const double d = m.aliasDb(want);
			if (table)
				std::printf("wobble fs %5.0f a 0.42 fs tone, speed 1 +- 0.002 at 200 Hz: other %7.1f dB re the tone (at %.0f Hz)\n", fs, d,
				            m.strongestOther(want).hz);
			else
				CHECK(d <= WOBBLE_BOUND_DB, "fs %.0f: speed wobbling round 1 at 200 Hz: other than the tone and its sidebands %.1f dB (bound %.1f)",
				      fs, d, WOBBLE_BOUND_DB);
		}
	}
}

// At 1 and slower the read interpolates with the speed-1 kernel. Bounds:
// measured 2026-11-22 (the same at 44.1 and 48 kHz, either direction,
// speed 0.5 or 0.25) plus 3 dB: tone 0.1 of the rate -81.0 dB, 0.2 -70.8,
// 0.3 -66.7, 0.4 -63.1. (With the Hermite read before: -50.9, -30.6,
// -18.9, -11.2.) At speed 1 from a whole frame the read is the tape's
// own samples (below -150 dB).
const double SLOW_BOUND_DB[4] = {-78.0, -67.8, -63.7, -60.1};

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
				CHECK(std::fabs(r.levelDb) <= PASS_DB, "fs %.0f, speed %+.2f, a tone at %.1f of the rate keeps its level (%.3f dB)", fs, s,
				      ratio, r.levelDb);
			}
}

// At speed 1 from a whole frame Eddy plays the tape's own samples, to the
// bit: checked against the tape itself, forwards (the knob at 1),
// backwards (-1), and at 1 made as 2 x 2^-1 (the knob at 2, V/OCT -1 V),
// with the tape alone at the output (Mix 1). (The knob at 0.5 with V/OCT
// +1 V is not exactly 1: the knob's law gives 0.5 only to within a float
// rounding, so the head drifts between frames and is read through the
// kernel, not to the bit.)
void speedOneToTheBit() {
	struct Way {
		float knob, voct;
		const char* name;
	};
	const Way ways[3] = {{0.96f, 0.f, "knob 1"}, {-0.96f, 0.f, "knob -1"}, {2.f, -1.f, "knob 2, V/OCT -1 V"}};
	for (double fs : RATES)
		for (const Way& w : ways) {
			Rig r((float) fs);
			r.f.params[F::SPEED] = w.knob;
			r.f.voct = w.voct;
			r.f.params[F::MIX] = 1.f;
			TapeSet* set = r.noise(6.0, 5.f, 11);
			r.use(set);
			r.press(&F::playPress);
			r.run(2000);   // the voice's fade and Play's are done
			// Where the head is: the frame the output equals (noise: one match).
			r.step();
			const Tape& t = set->tape;
			long at = -1;
			for (size_t k = 0; k < t.length; k++)
				if (std::memcmp(&t.ch[0][k], &r.r.l, sizeof(float)) == 0 && std::memcmp(&t.ch[1][k], &r.r.r, sizeof(float)) == 0)
					at = (long) k;
			const int dir = w.knob < 0.f ? -1 : 1;
			// And it is the frame the head was on: the head the engine shows
			// has moved on by one since the read.
			const long shown = std::lround((double) r.r.head * (double) t.length) - dir;
			CHECK(table || at == shown, "fs %.0f, %s: the output is the frame under the head (frame %ld, the head at %ld)", fs, w.name, at,
			      shown);
			bool same = at >= 0;
			for (long i = 1; same && i < (long) (2.0 * fs); i++) {
				r.step();
				const long k = at + dir * i;
				same = k >= 0 && k < (long) t.length && std::memcmp(&t.ch[0][k], &r.r.l, sizeof(float)) == 0 &&
				       std::memcmp(&t.ch[1][k], &r.r.r, sizeof(float)) == 0;
			}
			CHECK(table || same, "fs %.0f, %s: 2 s of output are the tape's own frames, to the bit (found at frame %ld)", fs, w.name, at);
		}
}

// Level against speed (the user heard the tape get quieter as the knob was
// turned down): white noise and a 1 kHz sine on the tape, played by the
// knob, the tape alone at the output. The noise's RMS is compared with the
// RMS of the frames the head passed; the sine's amplitude with 5 V.
// Measured 2026-11-22: see LEVEL in the report. Faster than 1 the noise
// loses what would land above Nyquist (about half its power at 2: that is
// the aliasing taken out), so speed 2 is held to its own expectation.
void levelAgainstSpeed() {
	for (double fs : RATES) {
		for (double s : {2.0, 1.0, 0.5, 0.25, 1.0 / 32}) {
			Rig r((float) fs);
			r.f.params[F::SPEED] = knobFor(s);
			r.f.params[F::MIX] = 1.f;
			TapeSet* set = r.noise(10.0, 5.f, 5);
			r.use(set);
			r.press(&F::playPress);
			r.run(2000);
			const long n = (long) (1.5 * fs);
			double e = 0.0;
			for (long i = 0; i < n; i++) {
				r.step();
				e += (double) r.r.l * r.r.l;
			}
			// The frames passed: from about 2000 s in to (2000 + n) s.
			const size_t a = (size_t) (2000.0 * s), b = (size_t) ((2000.0 + (double) n) * s);
			const double ref = rms(set->tape.ch[0] + a, b - a);
			const double d = 10.0 * std::log10(e / (double) n) - AliasMeter::db(ref);
			const double want = s > 1.0 ? NOISE_AT_2_DB : 0.0;
			if (table)
				std::printf("level fs %5.0f white noise at speed %.4f: %+.3f dB re the frames passed\n", fs, s, d);
			else
				CHECK(std::fabs(d - want) <= NOISE_TOL_DB, "fs %.0f, white noise at speed %.4f: %+.3f dB re the frames it passed (want %+.2f +- %.2f)",
				      fs, s, d, want, NOISE_TOL_DB);
		}
		for (double s : {2.0, 1.0, 0.5, 0.25, 1.0 / 32, 1.0 / 64, 1.0 / 128}) {
			Rig r((float) fs);
			r.f.params[F::SPEED] = knobFor(s);
			r.f.params[F::MIX] = 1.f;
			r.use(r.sine(10.0, 1000.0, 5.f));
			r.press(&F::playPress);
			r.run(2000);
			std::vector<float> out(65536);
			for (float& x : out) {
				r.step();
				x = r.r.l;
			}
			const AliasMeter m(out.data(), out.size(), fs);
			const double d = AliasMeter::db(m.amplitude(1000.0 * s) / 5.0);
			// Stopped: the output is full down to 1/64 and fades to 0 below it.
			const double want = s < 1.0 / 64 ? AliasMeter::db(s * 64.0) : 0.0;
			if (table)
				std::printf("level fs %5.0f 1 kHz sine at speed %.4f: %+.4f dB\n", fs, s, d);
			else
				CHECK(std::fabs(d - want) <= 0.05, "fs %.0f, a 1 kHz sine at speed %.4f: %+.4f dB (want %+.2f +- 0.05)", fs, s, d, want);
		}
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

// ---- What is written back away from speed 1.

// The voices write the tape in place (Wear from each frame's own value, Dub
// from the input: box-filtered over the head's travel slower than 1,
// interpolated between input samples faster), never what they read: the
// kernels on the read do not reach the tape. A tone at 0.4 of the rate
// (above the output's Nyquist at speed 2, in the speed-1 kernel's roll-off
// at 0.5) worn one pass at 0.5 or 2 is on the tape as after one pass at 1;
// at Wear 0 with Dub off nothing is written at all, at 0.5 or 2; and Dub
// writes nothing under 1/16 (the Stopped law), as before.
void writtenBack() {
	for (double fs : RATES) {
		double level[3];
		const double speeds[3] = {1.0, 0.5, 2.0};
		for (int k = 0; k < 3; k++) {
			const double s = speeds[k];
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
			std::printf("write fs %5.0f a 0.4 fs tone worn one pass: at speed 1 %.3f dB, at 0.5 %.3f dB, at 2 %.3f dB\n", fs, level[0],
			            level[1], level[2]);
		else
			for (int k = 1; k < 3; k++)
				CHECK(std::fabs(level[k] - level[0]) < 0.5, "fs %.0f: a 0.4 fs tone worn at speed %.1f stays on the tape (%.2f dB) as at speed 1 (%.2f dB)",
				      fs, speeds[k], level[k], level[0]);

		// Wear 0, Dub off, for many turns: the tape is untouched.
		for (double s : {0.5, 2.0}) {
			Rig r((float) fs);
			r.f.params[F::SPEED] = knobFor(s);
			TapeSet* set = r.sine(0.5, 0.4 * fs, 5.f);
			r.use(set);
			const Snapshot before(set->tape);
			r.press(&F::playPress);
			r.run((long) (2 * fs));
			CHECK(table || before.equals(set->tape), "fs %.0f: at Wear 0 and speed %.1f, 2 s of turns leave the tape as it was, to the bit", fs, s);
		}

		// Dub on at speed 1/32: the head is under 1/16, nothing is written.
		Rig r((float) fs);
		r.f.params[F::SPEED] = knobFor(1.0 / 32);
		r.f.params[F::DUB_LEVEL] = 1.f;
		TapeSet* set = r.sine(1.0, 1000.0, 2.f);
		r.use(set);
		const Snapshot before(set->tape);
		r.press(&F::playPress);
		r.step();
		r.press(&F::dubPress);
		for (long i = 0; i < (long) fs; i++)
			r.step(3.f, 3.f);
		CHECK(table || before.equals(set->tape), "fs %.0f: Dub at speed 1/32 writes nothing, to the bit", fs);
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
	nearOne();
	acrossOne();
	glideThroughOne();
	slowRead();
	speedOneToTheBit();
	levelAgainstSpeed();
	wearLoop();
	limits();
	dubSpeeds();
	writtenBack();
	speedModulation();
	return table ? 0 : rkatest::finish("eddy/test_alias");
}

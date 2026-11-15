// Fault sweeps: every app, every setting across its range, settings at their
// extremes together, and random controls, all over random and extreme input,
// under the address and undefined-behaviour sanitizers. Each sanitizer stop
// is a firmware line that behaves differently on a desktop CPU; the guards are
// listed under "Desktop-CPU guards" in firmware/CHANGES.md, and each has a
// deterministic regression test at the end of this file.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "CoreTest.hpp"
#include "testing.hpp"
#include "test_util.hpp"

using shoal::Core;
using shoal::CoreTest;

namespace {

// Base of every generator seed. Fixed, so a run is reproducible; build with
// -DSHOAL_FAULT_SEED=n to sweep with other random input.
#ifndef SHOAL_FAULT_SEED
#define SHOAL_FAULT_SEED 0
#endif
constexpr uint32_t kSeed = SHOAL_FAULT_SEED * 100000u;

const shoal::InputRange kRanges[] = { shoal::InputRange::Hardware, shoal::InputRange::Bipolar5 };
const float kExtremes[] = { -3.33f, 0.f, 6.67f };

// Lcg-driven inputs: each CV takes a new value in [-6, +8] V every 1..200
// ticks; each gate toggles every 1..400 ticks.
struct RandomInputs {
	Lcg rng;
	int cvLeft[shoal::kNumChannels] = {};
	int gateLeft[shoal::kNumChannels] = {};
	bool gate[shoal::kNumChannels] = {};

	explicit RandomInputs(uint32_t seed) { rng.state = seed; }

	void step(Core& c) {
		for (int ch = 0; ch < shoal::kNumChannels; ch++) {
			if (--cvLeft[ch] <= 0) {
				c.setCv(ch, rng.volts(-6.f, 8.f));
				cvLeft[ch] = 1 + (int) (rng.next() % 200);
			}
			if (--gateLeft[ch] <= 0) {
				gate[ch] = !gate[ch];
				c.setGate(ch, gate[ch] ? 10.f : 0.f);
				gateLeft[ch] = 1 + (int) (rng.next() % 400);
			}
		}
	}
	void run(Core& c, long ticks) {
		for (long i = 0; i < ticks; i++) {
			step(c);
			c.tick();
		}
	}
};

long ticksFor(double seconds) { return std::lround(seconds * shoal::kTickHz); }

void silence(Core& c) {
	for (int ch = 0; ch < shoal::kNumChannels; ch++) {
		c.setCv(ch, 0.f);
		c.setGate(ch, 0.f);
	}
}

// Run `ticks` ticks, checking the outputs every 1000; false on the first
// non-finite output.
template <typename Step>
bool runChecked(Core& c, long ticks, Step step) {
	for (long i = 0; i < ticks; i++) {
		step(i);
		c.tick();
		if (i % 1000 == 999 && !allFinite(c))
			return false;
	}
	return allFinite(c);
}

void testEveryAppRuns() {
	for (int app = 0; app < shoal::kNumApps; app++)
		for (shoal::InputRange range : kRanges) {
			Core c;
			c.selectApp(app);
			c.setInputRange(range);
			const char* an = Core::appName(app);
			const int r = (int) range;
			silence(c);
			CHECK(runChecked(c, ticksFor(3), [](long) {}), "%s, range %d: silent run finite", an, r);
			RandomInputs in(kSeed + 1000u + (uint32_t) app);
			CHECK(runChecked(c, ticksFor(3), [&](long) { in.step(c); }), "%s, range %d: random run finite", an, r);
			// The fixed extremes, a third of a second each on every CV, gates
			// still toggling.
			const long third = ticksFor(1) / 3;
			CHECK(runChecked(c, 3 * third, [&](long i) {
				if (i % third == 0)
					for (int ch = 0; ch < shoal::kNumChannels; ch++)
						c.setCv(ch, kExtremes[i / third]);
				for (int ch = 0; ch < shoal::kNumChannels; ch++)
					if (--in.gateLeft[ch] <= 0) {
						in.gate[ch] = !in.gate[ch];
						c.setGate(ch, in.gate[ch] ? 10.f : 0.f);
						in.gateLeft[ch] = 1 + (int) (in.rng.next() % 400);
					}
			}), "%s, range %d: extremes finite", an, r);
		}
}

// The values a sweep visits: every value of a range up to 64 wide, otherwise
// min, max, the default and `spaced` evenly spaced values.
std::vector<int> sweepValues(const shoal::SettingInfo& s, int spaced) {
	std::vector<int> v;
	if (s.max - s.min <= 64) {
		for (int x = s.min; x <= s.max; x++)
			v.push_back(x);
		return v;
	}
	v.push_back(s.min);
	v.push_back(s.max);
	v.push_back(s.def);
	for (int k = 0; k < spaced; k++)
		v.push_back(s.min + (int) ((int64_t) (s.max - s.min) * k / (spaced - 1)));
	return v;
}

void testEverySettingSwept() {
	uint8_t frame[shoal::kFrameBytes];
	long visits = 0;
	for (shoal::InputRange range : kRanges) {
		const int spaced = 32;
		for (int app = 0; app < shoal::kNumApps; app++) {
			Core c;
			c.selectApp(app);
			c.setInputRange(range);
			RandomInputs in(kSeed + 2000u + (uint32_t) app);
			const char* an = Core::appName(app);
			const int last = c.channels() - 1;
			std::vector<int> channels = { 0 };
			if (last > 0)
				channels.push_back(last);
			for (int ch : channels) {
				for (int i = 0; i < c.settingCount(); i++) {
					const shoal::SettingInfo s = c.settingInfo(i);
					for (int value : sweepValues(s, spaced)) {
						CHECK(c.setSettingRaw(ch, i, value) && c.setting(ch, i) == value, "%s, ch %d, %s = %d: set",
							an, ch, s.name, value);
						in.run(c, 200);
						c.drawMenu(frame);
						c.drawScope(frame);
						visits++;
						if (!allFinite(c)) {
							CHECK(false, "%s, range %d, ch %d, %s = %d: outputs finite", an, (int) range, ch, s.name,
								value);
							break;
						}
					}
					c.setSettingRaw(ch, i, s.def);
				}
			}
		}
	}
	CHECK(visits > 0, "sweep ran");
}

void testPairsAtExtremes() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core c;
		c.selectApp(app);
		RandomInputs in(kSeed + 3000u + (uint32_t) app);
		const char* an = Core::appName(app);
		// 0: all min, 1: all max, 2: min/max by index parity, 3: max/min by parity.
		for (int pattern = 0; pattern < 4; pattern++) {
			for (int ch = 0; ch < c.channels(); ch++)
				for (int i = 0; i < c.settingCount(); i++) {
					const shoal::SettingInfo s = c.settingInfo(i);
					const bool high = pattern == 1 || (pattern == 2 && i % 2) || (pattern == 3 && i % 2 == 0);
					c.setSettingRaw(ch, i, high ? s.max : s.min);
				}
			CHECK(runChecked(c, ticksFor(0.2), [&](long) { in.step(c); }), "%s, pattern %d: finite", an, pattern);
		}
	}
}

// Random presses (short, held, and R long presses that open the app menu)
// and encoder turns over random input. While the app menu is open, presses
// may switch app; the test returns to its app once the menu has closed.
void testControlsMash() {
	uint8_t frame[shoal::kFrameBytes];
	const double seconds = 20;
	for (int app = 0; app < shoal::kNumApps; app++) {
		Core c;
		c.selectApp(app);
		RandomInputs in(kSeed + 4000u + (uint32_t) app);
		Lcg rng;
		rng.state = kSeed + 5000u + (uint32_t) app;
		const char* an = Core::appName(app);
		const long total = ticksFor(seconds);
		long t = 0;
		bool ok = true;
		while (t < total && ok) {
			const uint32_t r = rng.next() >> 8;
			long hold = 0;
			int button = -1;
			switch (r % 8) {
				case 0: case 1: case 2:
					c.turnEncoder((shoal::Encoder) (r / 8 % 2), (int) (r / 16 % 21) - 10);
					break;
				case 3: case 4:
					button = (int) (r / 8 % 4);
					hold = ticksFor(0.01 + (r / 32 % 100) * 0.001);   // 10..109 ms
					break;
				case 5:
					button = (int) (r / 8 % 4);
					hold = ticksFor(0.3 + (r / 32 % 100) * 0.01);     // 0.3..1.29 s
					break;
				case 6:
					button = shoal::BUTTON_R;                           // opens the app menu
					hold = ticksFor(1.1);
					break;
				default:
					break;
			}
			if (button >= 0)
				c.setButton((shoal::Button) button, true);
			const long n = hold + ticksFor(0.005 + (r / 4096 % 50) * 0.002);
			ok = runChecked(c, n, [&](long i) {
				in.step(c);
				if (i == hold && button >= 0)
					c.setButton((shoal::Button) button, false);
			});
			t += n;
			c.drawMenu(frame);
			c.drawScope(frame);
			if (!c.appMenuOpen() && c.app() != app)
				c.selectApp(app);
		}
		CHECK(ok, "%s: outputs finite while mashing controls", an);
	}
}

// Not in the plan, added because the sweeps above change one setting at a
// time and most guards needed two or more together: every setting of an app
// at a random value (a third each at min, max and anywhere in range), on a
// random input range, then 1 s of random input with one random setting
// changed every 500 ticks, drawing both screens every 400 ticks.
void testRandomSettingCombinations() {
	uint8_t frame[shoal::kFrameBytes];
	Lcg rng;
	rng.state = kSeed + 6000u;
	auto pick = [&](const shoal::SettingInfo& s, bool extremes) {
		const uint32_t m = extremes ? rng.next() % 3 : 2;
		return m == 0 ? s.min : m == 1 ? s.max : s.min + (int) (rng.next() % (uint32_t) (s.max - s.min + 1));
	};
	for (int app = 0; app < shoal::kNumApps; app++)
		for (int trial = 0; trial < 40; trial++) {
			Core c;
			c.selectApp(app);
			c.setInputRange(rng.next() & 1 ? shoal::InputRange::Bipolar5 : shoal::InputRange::Hardware);
			for (int ch = 0; ch < c.channels(); ch++)
				for (int i = 0; i < c.settingCount(); i++)
					c.setSettingRaw(ch, i, pick(c.settingInfo(i), true));
			RandomInputs in(rng.next());
			const bool ok = runChecked(c, ticksFor(1), [&](long t) {
				in.step(c);
				if (t % 500 == 250) {
					const int ch = (int) (rng.next() % (uint32_t) c.channels());
					const int i = (int) (rng.next() % (uint32_t) c.settingCount());
					c.setSettingRaw(ch, i, pick(c.settingInfo(i), false));
				}
				if (t % 400 == 0) {
					c.drawMenu(frame);
					c.drawScope(frame);
				}
			});
			CHECK(ok, "%s, trial %d: outputs finite", Core::appName(app), trial);
		}
}

// ---- Regression tests for the desktop-CPU guards (CHANGES.md) ----

int settingIndex(Core& c, const char* name) {
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, name) == 0)
			return i;
	CHECK(false, "no setting named \"%s\"", name);
	return 0;
}

// Piqued (app 7) with "Eucl length" 1 (2 steps) and CV1 mapped to the length:
// a negative CV takes the live length to 0 (APP_ENVGEN.ino constrains it to
// 0..31), and EuclideanFilter is called with num_steps 0, since the caller
// only tests the setting. The Teensy read bjorklund_patterns[-33 + fill],
// before the table; Shoal reads row 0 (see bjorklund.cpp), so a one-step
// cycle fires on every trigger with a fill of 1 or more, and never with 0.
// At 0 V the 2-step cycle with fill 1 fires on every other trigger.
int piquedFires(int fill, float cv, int fires[6]) {
	Core c;
	c.selectApp(7);
	c.setSettingRaw(0, settingIndex(c, "Eucl length"), 1);
	c.setSettingRaw(0, settingIndex(c, "Fill"), fill);
	c.setSettingRaw(0, settingIndex(c, "CV1 -> "), 6);   // "Eleng"
	c.setSettingRaw(0, settingIndex(c, "S1"), 40);       // attack and decay of
	c.setSettingRaw(0, settingIndex(c, "S2"), 40);       // about 10 ms each
	c.setCv(0, cv);
	run(c, 0.2);
	uint8_t frame[shoal::kFrameBytes];
	int n = 0;
	for (int k = 0; k < 6; k++) {
		pulse(c, 0, 0.002);
		float peak = 0.f;
		for (int t = 0; t < 1000; t++) {
			c.tick();
			peak = std::fmax(peak, c.out(0));
		}
		fires[k] = peak > 0.5f;
		n += fires[k];
		run(c, 0.1);
		c.drawMenu(frame);   // the menu draws the live pattern (EuclideanPattern, length 0)
	}
	return n;
}

void testPiquedEuclideanLengthZero() {
	int f[6];
	CHECK(piquedFires(0, -2.f, f) == 0, "length 0, fill 0: never fires");
	CHECK(piquedFires(1, -2.f, f) == 6, "length 0, fill 1: fires on every trigger");
	CHECK(piquedFires(2, -2.f, f) == 6, "length 0, fill 2: fires on every trigger");
	CHECK(piquedFires(1, 0.f, f) == 3 && f[0] != f[1], "length 1 (2 steps), fill 1: every other trigger");
}

// CopierMaschine (app 0) on the "off" scale (Scale 4, braids::scales[0], no
// notes) with CV3 rotating the mask: RotateMask took `amount % 0`. The
// Teensy's RotateMask returned 0xffff there and the quantizer, with no notes,
// passes the pitch through, so the rotation changes nothing: A follows CV1
// with CV3 at 0 V or at 1 V.
void testOffScaleMaskRotate() {
	float a[2];
	for (int k = 0; k < 2; k++) {
		Core c;
		c.setSettingRaw(0, settingIndex(c, "Scale"), 4);
		c.setCv(0, 1.3f);
		c.setCv(2, k ? 1.f : 0.f);
		run(c, 0.05);
		pulse(c, 0);
		run(c, 0.01);
		a[k] = c.out(0);
	}
	CHECK(std::fabs(a[0] - 1.3f) < 0.01f, "off scale: A follows CV1, got %g", (double) a[0]);
	CHECK(a[1] == a[0], "off scale: CV3 rotation changes nothing (%g vs %g)", (double) a[1], (double) a[0]);
}

// Low-rents (app 6) draws its vectorscope with 6-bit values (0..63), which
// stay in the shared scope history after a switch to Piqued (app 7), whose
// scope draws channels C and D at row 32 + value: rows 64..95 for outputs
// below about 1.25 V, past the 64-row frame (here past the core's state, on
// the Teensy into the next frame buffer or past it). Those pixels are
// dropped. At its fastest settings Low-rents swings over most of the DAC
// range, so a second of history holds such values.
void testScopeAfterVectorscope() {
	Core c;
	c.selectApp(6);
	for (const char* name : { "Freq 1", "Freq 2" })
		c.setSettingRaw(0, settingIndex(c, name), 255);
	for (const char* name : { "LFreq 1 Rng", "LFreq 2 Rng" })
		c.setSettingRaw(0, settingIndex(c, name), 4);
	uint8_t frame[shoal::kFrameBytes];
	uint32_t low = 0;
	for (int k = 0; k < 500; k++) {
		run(c, 0.002);
		low += c.outCode(2) < 32768 || c.outCode(3) < 32768;
		c.drawScope(frame);
	}
	CHECK(low > 0, "C or D went below 1.25 V");
	c.selectApp(7);
	c.drawScope(frame);
	CHECK(allFinite(c), "drew the scope after the vectorscope");
}

// braids::Quantizer::Process: when no codeword is within 16384 (distances
// are truncated to int16), q stays -1 and the hysteresis boundaries read
// codebook_[-2] and codebook_[-1], the last two enabled_notes_ (same layout on
// the Teensy). The random sweep reached it from Sequins' aux COPY output
// (APP_A_SEQ.ino, step_pitch_aux_), whose pitch adds octave offsets from
// settings and CV to the step pitch (for this scale, bands of pitches from
// about 26000 up; 100000 is one). q is then clamped to 1: the result is
// codebook_[1], for this 16-note scale note 1 four spans down (96 - 4 * 1536), plus the
// two-octave offset Process removes and adds back (3072).
void testQuantizerFarPitch() {
	const int32_t in[] = { 100000, 500, 100000 };
	int32_t out[3];
	CoreTest::quantizerRun(in, 3, out);
	CHECK(out[0] == 96 - 4 * 1536 + 3072, "100000: %d", (int) out[0]);
	CHECK(out[1] == 480, "500 after it: %d, want note 5 (480)", (int) out[1]);
	CHECK(out[2] == out[0], "100000 again: %d", (int) out[2]);
}

// Per Nørgård's infinity series (IntSq sequence 4, Quantermain and
// CopierMaschine): the bit loop starts at b = 32, and `0x01 << 32` is a
// shift by the type's width. The Teensy's shift gives 0, so the first test
// ((sk & 0) == 0) always passes and sets the pending bit; x86 shifts by 0
// and tests bit 0 instead, which differs for every even index.
uint16_t norgardTeensy(uint32_t sk) {
	int bitSum = 0, pending = 0;
	for (int b = 32; b > -1; --b) {
		const uint32_t mask = b < 32 ? 1u << b : 0u;
		if ((sk & mask) == mask) {
			bitSum += pending;
			pending = 1;
		} else {
			pending = -pending;
		}
	}
	return (uint16_t) (12 + bitSum + pending);
}

void testNorgardSeries() {
	Core c;
	uint16_t out[40];
	CoreTest::intSeqRun(c, 4, 0, 40, 40, out);
	for (int i = 0; i < 40; i++) {
		const uint32_t k = (uint32_t) (i + 1) % 41;   // Clock advances first; the loop wraps at start + length
		CHECK(out[i] >> 8 == norgardTeensy(k), "k %u: %u, want %u", (unsigned) k, out[i] >> 8, norgardTeensy(k));
	}
}

// Sequins (app 8): the aux envelope (output C for channel 1) switched from
// ADSR to AD after it finished (segment 3 of 3) left segment_ past AD's last
// segment (Sequins, unlike Piqued, does not reset the envelope on a type
// change). It never equals the segment count again, and with "dec/rel reset"
// off a gate does not restart it: the next step runs it through the stale
// segments 3..7 (fast: their times are 0), reading level_[8] (time_[0]) at
// segment 7, and then past its arrays. From segment 8 it is now taken as
// finished, so the gates after that run a normal AD (5 V).
void testEnvelopeTypeShrinks() {
	Core c;
	c.selectApp(8);
	c.setSettingRaw(0, settingIndex(c, "dec/rel reset"), 0);   // none
	for (const char* name : { "--> att dur", "--> dec dur", "--> rel dur" })
		c.setSettingRaw(0, settingIndex(c, name), 40);
	c.setSettingRaw(0, settingIndex(c, "aux. mode"), 4);       // ADSR
	for (int k = 0; k < 4; k++) {
		pulse(c, 0);
		run(c, 0.3);
	}
	c.setSettingRaw(0, settingIndex(c, "aux. mode"), 2);       // AD
	pulse(c, 0);                                               // the walk through segments 3..7
	run(c, 0.3);
	for (int k = 0; k < 4; k++) {
		pulse(c, 0);
		float peak = 0.f;
		for (int t = 0; t < 3000; t++) {
			c.tick();
			peak = std::fmax(peak, c.out(2));
		}
		CHECK(std::fabs(peak - 5.f) < 0.05f, "clock %d after the switch: AD peak %g", k, (double) peak);
		run(c, 0.3);
	}
}

// References (app 12): "Octave" -3 with "Mod range oct" -3 and a rate of 0
// (the modulation toggles every tick) puts the pitch 6 octaves down, below
// the lowest drawn octave; the screensaver's semitone was negative and
// indexed before the note names.
void testReferencesLowPitchName() {
	Core c;
	c.selectApp(12);
	c.setSettingRaw(0, settingIndex(c, "Octave"), -3);
	c.setSettingRaw(0, settingIndex(c, "Semitone"), 1);
	c.setSettingRaw(0, settingIndex(c, "Mod range oct"), -3);
	c.setSettingRaw(0, settingIndex(c, "Mod rate (s)"), 0);
	uint8_t frame[shoal::kFrameBytes];
	for (int k = 0; k < 4; k++) {
		c.tick();
		c.drawScope(frame);
	}
	CHECK(allFinite(c), "drew the low pitch");
}

// Sequins (app 8), play mode SEQ+1, length 2, with the direction switched
// between pendulum 1 and forward on every clock: the pendulum's reverse
// branch steps from 0 to -1 and, with the other sequence still to come
// (EoS -1), leaves the step at -1 for one clock. The step mask was shifted by
// -1; the Teensy's shift (by 255, the count's low byte) gives 0, so that
// step is off.
void testSequinsStepMinusOne() {
	Core c;
	c.selectApp(8);
	c.setSettingRaw(0, settingIndex(c, "playmode"), 1);   // SEQ+1
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, "sequence length") == 0)
			c.setSettingRaw(0, i, 2);
	const int direction = settingIndex(c, "direction");    // the first: the sequence's
	for (int k = 0; k < 300; k++) {
		c.setSettingRaw(0, direction, k % 2 ? 0 : 2);      // fwd, pnd1
		pulse(c, 0);
		run(c, 0.005);
	}
	CHECK(allFinite(c), "pendulum and forward alternating");
}

// Acid Curds (app 11): "chord:" 31 on progression 4 opens the chord editor on
// slot 31 + 3 * 8 = 55 of a 32-entry table, and the editor's R encoder wrote
// there (past the table on the Teensy). Those writes are dropped: no user
// chord changes. The same steps on slot 0 of progression 1 change chord 0,
// so the editor did open.
bool chordEditChanges(int progression, int slot, int cursorSteps) {
	Core c;
	c.selectApp(11);
	c.setEncoderAcceleration(false);   // one detent, one step
	c.setSettingRaw(0, settingIndex(c, "progression"), progression);
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, "num.chords") == 0)
			c.setSettingRaw(0, i, 31);
	c.setSettingRaw(0, settingIndex(c, "chord:"), slot);
	int8_t before[32][5];
	for (int k = 0; k < 32; k++)
		for (int f = 0; f < 5; f++)
			before[k][f] = CoreTest::userChordField(c, k, f);
	run(c, 0.1);
	c.turnEncoder(shoal::ENCODER_R, cursorSteps);
	run(c, 0.1);
	c.setButton(shoal::BUTTON_R, true);
	run(c, 0.05);
	c.setButton(shoal::BUTTON_R, false);
	run(c, 0.1);
	c.turnEncoder(shoal::ENCODER_R, 3);
	run(c, 0.1);
	bool changed = false;
	for (int k = 0; k < 32; k++)
		for (int f = 0; f < 5; f++)
			changed = changed || before[k][f] != CoreTest::userChordField(c, k, f);
	return changed;
}

void testChordEditorPastTable() {
	int steps = -1;
	for (int k = 0; k < 24 && steps < 0; k++)
		if (chordEditChanges(0, 0, k))
			steps = k;
	CHECK(steps >= 0, "found the chord editor in the menu");
	if (steps >= 0)
		CHECK(!chordEditChanges(3, 31, steps), "slot 55: no chord changed");
}

// Harrington 1200 (app 1), minor root, on the ±5 V range: root CV1 at -5 V
// (-60 semitones) with Transpose -11, Octave -3 and Inversion -3 renders
// chords with notes below -120 (-122 after some transformations), where
// (note + 120) % 12 was negative and indexed before the note names, in the
// menu (note_name) and in the screensaver.
void testH1200LowNotes() {
	Core c;
	c.selectApp(1);
	c.setInputRange(shoal::InputRange::Bipolar5);
	c.setSettingRaw(0, settingIndex(c, "Transpose"), -11);
	c.setSettingRaw(0, settingIndex(c, "Transpose CV"), 1);
	c.setSettingRaw(0, settingIndex(c, "Octave"), -3);
	c.setSettingRaw(0, settingIndex(c, "Inversion"), -3);
	c.setSettingRaw(0, settingIndex(c, "Root mode"), 1);
	c.setCv(0, -5.f);
	run(c, 0.1);
	uint8_t frame[shoal::kFrameBytes];
	for (int k = 0; k < 40; k++) {
		pulse(c, (k * 7) % 4);   // TR1..TR4 in turn: clock and transformations
		run(c, 0.01);
		c.drawMenu(frame);
		c.drawScope(frame);
	}
	CHECK(allFinite(c), "drew the low notes");
}

} // namespace

int main() {
	testPiquedEuclideanLengthZero();
	testOffScaleMaskRotate();
	testScopeAfterVectorscope();
	testQuantizerFarPitch();
	testNorgardSeries();
	testEnvelopeTypeShrinks();
	testReferencesLowPitchName();
	testSequinsStepMinusOne();
	testChordEditorPastTable();
	testH1200LowNotes();
	testEveryAppRuns();
	testEverySettingSwept();
	testPairsAtExtremes();
	testControlsMash();
	testRandomSettingCombinations();
	return rkatest::finish("test_faults");
}

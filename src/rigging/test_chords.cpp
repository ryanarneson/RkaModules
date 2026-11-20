// Rigging's chord table, the Chord CV's stepping and the pitch rules
// (docs/specs/2026-11-19-rigging-design.md).
#include "testing.hpp"
#include "rigging/Chords.hpp"

#include <cmath>
#include <cstring>

using namespace rka::rigging;

namespace {

// The design's table, written out again here: semitones and cents above
// string 1, or plain ratios for Bell.
void table() {
	const struct { const char* name; double semis[6]; double cents[6]; } want[6] = {
		{"Unison", {0, 0, 0, 0, 0, 0}, {-8, -4, 0, 3, 7, 11}},
		{"Octaves", {0, 12, 12, 24, 24, 36}, {0, -5, 5, -5, 5, 0}},
		{"Fifths", {0, 7, 12, 19, 24, 31}, {0, 0, 0, 0, 0, 0}},
		{"Minor", {0, 7, 12, 15, 19, 24}, {0, 0, 0, 0, 0, 0}},
		{"Sus", {0, 5, 7, 12, 17, 19}, {0, 0, 0, 0, 0, 0}},
		{"Cluster", {0, 1, 2, 3, 4, 5}, {0, 0, 0, 0, 0, 0}},
	};
	CHECK(kStrings == 6 && kChords == 7, "six strings, seven chords");
	for (int c = 0; c < 6; c++) {
		CHECK(std::strcmp(chordName(c), want[c].name) == 0, "chord %d is named \"%s\"", c, chordName(c));
		for (int k = 0; k < 6; k++) {
			const double r = std::pow(2.0, want[c].semis[k] / 12.0 + want[c].cents[k] / 1200.0);
			CHECK(std::fabs(chordRatio(c, k) / r - 1.0) < 1e-6, "%s string %d: ratio %.7f, want %.7f", want[c].name, k + 1, chordRatio(c, k), r);
		}
	}
	const double bell[6] = {1, 2, 2.4, 3, 4, 6};
	CHECK(std::strcmp(chordName(6), "Bell") == 0, "chord 6 is named \"%s\"", chordName(6));
	for (int k = 0; k < 6; k++)
		CHECK(std::fabs(chordRatio(6, k) / bell[k] - 1.0) < 1e-6, "Bell string %d: ratio %g", k + 1, chordRatio(6, k));
	// Out of range reads as the nearest valid chord and string.
	CHECK(chordRatio(-3, 0) == chordRatio(0, 0) && chordRatio(99, 5) == chordRatio(6, 5) && chordRatio(2, 99) == chordRatio(2, 5),
	      "out-of-range chord or string");
}

void cv_steps() {
	CHECK(chordSetting(3.f, 0.f, -1) == 3, "knob 3, no CV");
	CHECK(chordSetting(3.f, -5.f, -1) == 0 && chordSetting(3.f, 5.f, -1) == 6, "+-5 V from the middle reaches both ends: %d, %d",
	      chordSetting(3.f, -5.f, -1), chordSetting(3.f, 5.f, -1));
	CHECK(chordSetting(2.f, 10.f / 7.f, -1) == 3, "one step is 1.43 V");
	CHECK(chordSetting(0.f, -10.f, -1) == 0 && chordSetting(6.f, 10.f, -1) == 6, "clamped at the ends");
	CHECK(chordSetting(2.f, NAN, 4) == 2, "NaN CV reads as 0 V");
}

void hysteresis() {
	// A CV dithering about the boundary between 2 and 3.
	int held = chordSetting(2.f, 0.f, -1), changes = 0;
	for (int i = 0; i < 10000; i++) {
		const float cv = (0.5f + ((i * 7919) % 100 - 50) * 0.001f * 0.7f) / 0.7f;   // 0.5 step +- 0.05 V
		const int now = chordSetting(2.f, cv, held);
		changes += now != held ? 1 : 0;
		held = now;
	}
	CHECK(changes <= 1, "the setting changed %d times on a dithering boundary", changes);
	// A slow ramp up visits every setting once, in order; and back down.
	held = -1;
	int seq[32], n = 0;
	for (int i = 0; i <= 2000; i++) {
		const int now = chordSetting(0.f, 10.f * i / 2000.f, held);
		if (now != held && n < 32)
			seq[n++] = now;
		held = now;
	}
	bool ordered = n == 7;
	for (int i = 0; i < n && ordered; i++)
		ordered = seq[i] == i;
	CHECK(ordered, "a ramp up visited %d settings, want 0..6 in order", n);
	n = 0;
	for (int i = 2000; i >= 0; i--) {
		const int now = chordSetting(0.f, 10.f * i / 2000.f, held);
		if (now != held && n < 32)
			seq[n++] = now;
		held = now;
	}
	CHECK(n == 6 && seq[0] == 5 && seq[5] == 0, "a ramp down visited %d settings", n);
	// Hysteresis is small: a clear move of a whole step always follows.
	CHECK(chordSetting(3.f, 0.f, 2) == 3 && chordSetting(4.3f, 0.f, 5) == 4 && chordSetting(4.5f, 0.f, 5) == 5,
	      "0.6 of a step holds; further leaves");
}

void pitch() {
	CHECK(std::fabs(string1Hz(0.f, 0.f) - 65.4064f) < 0.001f, "Tune 0 is C2: %g Hz", string1Hz(0.f, 0.f));
	CHECK(std::fabs(string1Hz(12.f, 0.f) / 130.8128f - 1.f) < 1e-5f && std::fabs(string1Hz(0.f, 1.f) / 130.8128f - 1.f) < 1e-5f,
	      "+12 semitones or +1 V is an octave");
	CHECK(std::fabs(string1Hz(-24.f, -1.f) / 8.1758f - 1.f) < 1e-4f, "-24 semitones and -1 V: %g Hz", string1Hz(-24.f, -1.f));
	CHECK(clampHz(1.f, 48000.f) == 8.f && clampHz(20000.f, 48000.f) == 12000.f && clampHz(20000.f, 20000.f) == 8000.f,
	      "clamped to 8 Hz .. min(12 kHz, 0.4 fs)");
	CHECK(clampHz(440.f, 48000.f) == 440.f, "in range is untouched");
	CHECK(clampHz(NAN, 48000.f) == 8.f && clampHz(INFINITY, 48000.f) == 12000.f, "NaN and inf");
	CHECK(std::isfinite(string1Hz(0.f, 1e9f)) || string1Hz(0.f, 1e9f) > 0.f, "a huge V/OCT does not give NaN");
}

} // namespace

int main() {
	table();
	cv_steps();
	hysteresis();
	pitch();
	return rkatest::finish("rigging test_chords");
}

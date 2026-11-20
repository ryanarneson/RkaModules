#pragma once
// Rigging's tunings (docs/specs/2026-11-19-rigging-design.md): the seven
// chords as frequency ratios of the six strings to string 1, the Chord
// knob and CV as a setting, and string 1's pitch from Tune and V/OCT.
// Rack-free, C++11. Control rate: these use libm.
#include <cmath>

#include "rigging/RiggingTuning.hpp"

namespace rka {
namespace rigging {

constexpr int kStrings = 6, kChords = 7;

inline const char* chordName(int chord) {
	static const char* const names[kChords] = {"Unison", "Octaves", "Fifths", "Minor", "Sus", "Cluster", "Bell"};
	return names[chord < 0 ? 0 : chord >= kChords ? kChords - 1 : chord];
}

// String `string`'s (0..5) frequency over string 1's nominal pitch, detune
// included. (In Unison string 1 itself sits 8 cents under it.)
inline float chordRatio(int chord, int string) {
	static const float semis[kChords - 1][kStrings] = {
		{0, 0, 0, 0, 0, 0},        // Unison
		{0, 12, 12, 24, 24, 36},   // Octaves
		{0, 7, 12, 19, 24, 31},    // Fifths
		{0, 7, 12, 15, 19, 24},    // Minor
		{0, 5, 7, 12, 17, 19},     // Sus
		{0, 1, 2, 3, 4, 5},        // Cluster
	};
	// A bell's hum, prime, tierce, quint, nominal and the partial above.
	static const float bell[kStrings] = {1.f, 2.f, 2.4f, 3.f, 4.f, 6.f};
	chord = chord < 0 ? 0 : chord >= kChords ? kChords - 1 : chord;
	string = string < 0 ? 0 : string >= kStrings ? kStrings - 1 : string;
	if (chord == kChords - 1)
		return bell[string];
	float cents = 0.f;
	if (chord == 0)
		cents = UNISON_CENTS[string];
	else if (chord == 1 && (string == 1 || string == 3))
		cents = -OCTAVE_PAIR_CENTS;
	else if (chord == 1 && (string == 2 || string == 4))
		cents = OCTAVE_PAIR_CENTS;
	return (float) std::pow(2.0, semis[chord][string] / 12.0 + cents / 1200.0);
}

// The chord for the knob (0..6, a detent each) plus the CV, 10 V across the
// seven. `held` is the setting returned last (-1: none yet): the sum must
// pass 0.6 of a step from it to leave, so a CV resting on a boundary does
// not chatter. NaN or infinite CV reads as 0 V.
inline int chordSetting(float knob, float cvVolts, int held) {
	if (!(cvVolts - cvVolts == 0.f))
		cvVolts = 0.f;
	if (!(knob - knob == 0.f))
		knob = 0.f;
	float x = knob + cvVolts * 0.7f;
	x = x < 0.f ? 0.f : x > kChords - 1.f ? kChords - 1.f : x;
	if (held >= 0 && held < kChords && std::fabs(x - (float) held) <= 0.6f)
		return held;
	return (int) (x + 0.5f);
}

// String 1's nominal pitch: C2 with Tune in semitones and V/OCT in volts.
// Not clamped (clampHz); bounded so that no input gives inf or NaN.
inline float string1Hz(float tuneSemitones, float voct) {
	float oct = tuneSemitones / 12.f + voct;
	if (!(oct - oct == 0.f))
		oct = 0.f;
	oct = oct < -20.f ? -20.f : oct > 20.f ? 20.f : oct;
	return 65.40639f * (float) std::pow(2.0, (double) oct);
}

// A string's frequency kept to 8 Hz .. the lower of 12 kHz and 0.4 fs.
inline float clampHz(float hz, float fs) {
	const float top = 0.4f * fs < 12000.f ? 0.4f * fs : 12000.f;
	if (!(hz >= 8.f))
		return 8.f;   // below, or NaN
	return hz > top ? top : hz;
}

} // namespace rigging
} // namespace rka

#pragma once
// Rigging's numbers that are set by ear (the listening stop of
// docs/plans/2026-11-19-rigging.md). Notes of the chords, and the laws the
// panel promises, are not here: they are the design's.

namespace rka {
namespace rigging {

// Detune of the six strings in Unison, and of the doubled strings in
// Octaves (2 and 3, 4 and 5), in cents.
constexpr float UNISON_CENTS[6] = {-8.f, -4.f, 0.f, 3.f, 7.f, 11.f};
constexpr float OCTAVE_PAIR_CENTS = 5.f;

// A change of pitch glides: the string's pitch closes on its target with
// this time constant (within 3 cents of an octave's jump in 6 of them).
constexpr float GLIDE_TAU_S = 0.008f;

// Damp. Besides Ring's decay, the fundamental decays at
// DAMP_MAX_RATE * damp^DAMP_POWER nepers a second and harmonic n at n * n
// times that, whatever the string's pitch. At the default Damp (a third)
// that is 0.11 a second: a 60 s Ring keeps half its time.
constexpr float DAMP_MAX_RATE = 3.f;
constexpr int DAMP_POWER = 3;

// A string's loop is untouched to LOOP_KNEE volts and never passes
// LOOP_CEIL (Ballast's curve).
constexpr float LOOP_KNEE = 12.f, LOOP_CEIL = 16.f;
// What a string puts out can pass LOOP_CEIL by this factor at most (the
// read between two bounded samples overshoots a little).
constexpr float LOOP_OVERSHOOT = 1.25f;

// The strike: a burst of noise into each string, about one period long,
// peaking at STRIKE_LEVEL volts, low-passed by a one-pole with this
// coefficient a sample (1: none) so it is not all edge.
constexpr float STRIKE_LEVEL = 4.f;
constexpr float STRIKE_SMOOTH = 0.5f;

// The audio input is high-passed here before the strings: a string's loop
// passes DC as it does its own pitch, so DC in would ring as DC.
constexpr float INPUT_HIGHPASS_HZ = 3.f;

} // namespace rigging
} // namespace rka

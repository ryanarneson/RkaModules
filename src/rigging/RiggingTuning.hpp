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
constexpr float STRIKE_LEVEL = 5.2f;
constexpr float STRIKE_SMOOTH = 0.5f;

// The audio input is high-passed here before the strings: a string's loop
// passes DC as it does its own pitch, so DC in would ring as DC.
constexpr float INPUT_HIGHPASS_HZ = 3.f;

// Levels. The audio input into the strings (on top of each string's own
// share, which keeps the drone's loudness steady across Ring), and the
// strings' sum on its way to the soft limit.
constexpr float INPUT_GAIN = 1.f;
// The strings hear the input only up to this many volts either way.
constexpr float INPUT_MAX = 24.f;
constexpr float OUTPUT_GAIN = 1.f;

// Where the six strings sit, -1 (left) to +1 (right): the lowest in the
// middle, the higher ones further out, side about.
constexpr float PAN[6] = {0.f, -0.25f, 0.25f, -0.5f, 0.5f, -0.75f};

// The strings' sum is untouched to OUT_KNEE volts and never passes OUT_CEIL
// (Ballast's curve). Each string's DC is blocked below OUT_DC_HZ before its
// level knob, so DC a string holds (a strike's burst leaves a little) is
// never heard as a thump when a level moves.
constexpr float OUT_KNEE = 8.f, OUT_CEIL = 10.f, OUT_DC_HZ = 5.f;

// A new note at V/OCT (STRUM unpatched) strikes: a move of more than
// NOTE_STEP_V within NOTE_WINDOW_S; then none until V/OCT has kept still
// (moving slower than that) for NOTE_HOLD_S.
constexpr float NOTE_STEP_V = 1.f / 30.f, NOTE_WINDOW_S = 0.001f, NOTE_HOLD_S = 0.01f;

// The strike light, and the smoothing of levels and Mix.
constexpr float LIGHT_S = 0.1f, SMOOTH_S = 0.005f;

} // namespace rigging
} // namespace rka

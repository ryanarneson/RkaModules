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

} // namespace rigging
} // namespace rka

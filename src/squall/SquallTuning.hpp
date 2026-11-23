#pragma once
// Squall's tunables, in one place (spec: docs/specs/2026-11-18-squall-design.md).
// N_MAX, K and SWELL_OMEGA are starting figures, tuned by ear at the
// listening stop; the tests take them from here.
namespace rka {
namespace squall {

// Gust: branching ratio n = gust x N_MAX; a bump decays with
// tau = K x the average gap.
static const float N_MAX = 0.9f;
static const float K = 0.35f;

// Swell: two one-poles at omega = SWELL_OMEGA / average gap (5 % in two
// gaps); Freeze brakes it with a BRAKE_S one-pole.
static const float SWELL_OMEGA = 2.4f;
static const float BRAKE_S = 0.02f;

// Gate: a quarter of the average gap, held to 5 ms ... 4 s; 1 ms low on a
// retrigger; 10 ms while a clock's period is unknown.
static const float GATE_FRACTION = 0.25f;
static const float GATE_MIN_S = 0.005f;
static const float GATE_MAX_S = 4.f;
static const float RETRIGGER_S = 0.001f;
static const float FIRST_CLOCK_GATE_S = 0.01f;

// Saved state: a gap over SAVED_GAP_MAX average gaps (the process makes one
// over 50 about e^-50 of the time) or a gate length over SAVED_LENGTH_MAX
// (four times GATE_FRACTION) loads as the default (issue #49).
static const float SAVED_GAP_MAX = 50.f;
static const float SAVED_LENGTH_MAX = 4.f * GATE_FRACTION;

// The event light.
static const float LIGHT_S = 0.1f;

} // namespace squall
} // namespace rka

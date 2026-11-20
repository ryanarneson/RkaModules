#pragma once
// Narrows: every tuned number in one place. Rack-free.

namespace rka {
namespace narrows {

// Volts per unit inside the filter. Fathom's ladder self-oscillates at only
// about 0.17 of a unit at the feedback used here, so the scale is set to
// bring that sine out near 5 V; a 5 V signal is then 0.18 of a unit, well
// inside the straight part of the ladder's tanh stages, and Drive is what
// pushes into them.
static const float VOLTS = 28.f;
// What goes into the filter is held to this, in volts.
static const float INPUT_LIMIT_V = 24.f;
// The output's soft limit, in volts: straight below the knee, never past
// the ceiling (the curve of Wake's outputs).
static const float OUT_KNEE_V = 8.f;
static const float OUT_CEIL_V = 10.f;

// Cutoff: the knob's lowest note and its span.
static const float CUTOFF_BASE_HZ = 16.f;
static const float CUTOFF_OCTAVES = 10.f;
static const float CUTOFF_DEFAULT = 0.6f;   // 1024 Hz
// The summed cutoff is held to these (and to CUTOFF_MAX_FS of the rate).
static const float CUTOFF_MIN_HZ = 8.f;
static const float CUTOFF_MAX_HZ = 20000.f;
static const float CUTOFF_MAX_FS = 0.45f;
// Spread at full: octaves between the two sides.
static const float SPREAD_OCTAVES = 1.f;

// Res. Up to RES_ONSET the ladder's feedback follows Fathom's square law
// and reaches 4, where self-oscillation begins; over the rest of the travel
// it rises in a straight line to RES_K_MAX, which sets how loud the
// self-oscillating sine gets (the ladder's level grows with the excess
// over 4, and its pitch falls with it: PITCH_FIX in the engine).
static const float RES_ONSET = 0.95f;
static const float RES_K_ONSET = 4.f;
static const float RES_K_MAX = 4.6f;

// The state core. Its damping at Res 0 (Butterworth: -3 dB at the cutoff),
// its damping at Res 1 (negative: the oscillation grows until the
// resonance clip holds it), and the scale of that clip, which sets the
// level the oscillation settles at (tuned to the ladder's).
static const float STATE_K0 = 1.41421356f;
static const float STATE_K_TOP = -0.05f;
static const float STATE_CLIP = 2.6f;

// The ladder's pitch correction above the onset. Its self-oscillation falls
// flat as the feedback k rises past 4: by (107.2 e - 42 e^2) (1 - 2.3 g)
// cents, e = k - 4 and g the per-pole tan (measured, 30 Hz to 4 kHz at
// 44.1 and 96 kHz). The engine raises the ladder's g by the same amount;
// A and B are those cents as a natural-log ratio (x ln 2 / 1200).
static const float PITCH_FIX_A = 0.061921f;
static const float PITCH_FIX_B = -0.024260f;
static const float PITCH_FIX_G = 2.3f;

// Drive at full: octaves of gain (24 dB). The output is turned down by
// DRIVE_MAKEUP of that, in octaves: less than all of it, since a driven
// core is already holding the level back.
static const float DRIVE_OCTAVES = 4.f;
static const float DRIVE_MAKEUP = 0.62f;

// Comb: the feedback at the ends of the knob.
static const float COMB_MAX = 0.95f;
// The shortest delay, in samples.
static const float COMB_MIN_SAMPLES = 2.f;
// How fast the comb's delay follows a jump of the cutoff, seconds.
static const float COMB_SLIDE_S = 0.003f;

// Ping: the pulse's length, and its peak as an input voltage.
static const float PING_S = 0.001f;
static const float PING_V = 5.f;

// Smoothing of the knobs and of Mode, seconds.
static const float SMOOTH_S = 0.005f;
// The core switch's crossfade, seconds.
static const float CORE_FADE_S = 0.03f;

} // namespace narrows
} // namespace rka

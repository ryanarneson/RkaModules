#pragma once
// Wake's numbers in one place (docs/specs/2026-11-20-wake-design.md). The
// ones marked "by ear" are settled at the listening stop.

namespace rka {
namespace wake {

// Time: 10 ms to 10 s, log.
constexpr float TIME_MIN_S = 0.01f, TIME_MAX_S = 10.f;
// The buffer holds TIME_MAX_S up to this many samples a side; above that
// rate the longest time is shorter.
constexpr int BUFFER_MAX_SAMPLES = 1 << 21;

// Feedback's top: repeats grow into the loop's bound.
constexpr float FEEDBACK_MAX = 1.1f;

// Tone: the corners at the ends of the travel; flat at noon.
constexpr float TONE_OPEN_HZ = 20000.f, TONE_DARK_HZ = 500.f;
constexpr float TONE_FULL_HZ = 20.f, TONE_THIN_HZ = 1000.f;

// What one pass through the loop does, per style (by ear).
struct StyleConstants {
	float lowCutHz;         // high-pass corner; 0 is none
	float highCutHzShort;   // low-pass corner at TIME_MIN_S; 0 is none
	float highCutHzLong;    // ... at HIGH_CUT_LONG_S and beyond
	float drive;            // 0 is clean; 1 saturates softly at +-10 V; 2 at +-5 V
	float glideSeconds;     // the delay's glide when Time moves
	float glideOctaves;     // the furthest pitch bends
	bool crossfade;         // true: no glide, a crossfade
};
constexpr float HIGH_CUT_LONG_S = 1.f;
constexpr float CROSSFADE_S = 0.04f;
constexpr StyleConstants STYLES[3] = {
    {40.f, 6000.f, 6000.f, 1.f, 0.15f, 2.f, false},   // Tape
    {80.f, 5000.f, 1500.f, 2.f, 0.08f, 2.f, false},   // Analog
    {0.f, 0.f, 0.f, 0.f, 0.15f, 2.f, true},           // Digital
};
constexpr float STYLE_RAMP_S = 0.03f;

// The loop's bound (Ballast's curve): linear to the knee, never past the
// ceiling. A +-10 V signal passes Digital's loop untouched.
constexpr float LOOP_KNEE = 12.f, LOOP_CEIL = 16.f;
constexpr float LOOP_DC_HZ = 5.f;

// Drift: the wander's depth at full, as pitch (by ear).
constexpr float DRIFT_CENTS = 30.f;

// Freeze and the switches.
constexpr float JOIN_S = 0.01f;
constexpr float SWITCH_S = 0.01f;

// The input into the delay, and the outputs.
constexpr float INPUT_MAX = 24.f;
constexpr float OUT_KNEE = 8.f, OUT_CEIL = 10.f;

// Controls are read every this many samples; knobs smooth over this long.
constexpr int CONTROL_SAMPLES = 16;
constexpr float SMOOTH_S = 0.005f;

} // namespace wake
} // namespace rka

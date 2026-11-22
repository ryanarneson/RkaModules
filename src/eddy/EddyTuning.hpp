#pragma once
// Eddy's numbers in one place (docs: the Global Constraints of the plan).
// The Wear ones were settled by ear at the listening stop (build 2.0.27,
// 2026-11-22).

namespace rka {
namespace eddy {

// Speed: flat stretches either side of -1, 0 and +1; the CV's depth; the
// glide over the knob plus CV; the V/OCT's own short smoothing; the hold.
constexpr float SPEED_FLAT_WIDTH = 0.04f;
constexpr float SPEED_KNOB_MAX = 2.f;
constexpr float SPEED_CV_PER_VOLT = 0.4f;
constexpr float SPEED_GLIDE_S = 0.06f;
constexpr float VOCT_SMOOTH_S = 0.002f;
constexpr float SPEED_HOLD = 4.f;

// Stopped: the output is fully back at 1/64; writing is off under 1/16 and
// fully on from 1/8.
constexpr float OUT_FULL_SPEED = 1.f / 64.f;
constexpr float WRITE_OFF_SPEED = 1.f / 16.f, WRITE_FULL_SPEED = 1.f / 8.f;

// Fades and the window: F, the shortest Length, the CV's reach on Start.
constexpr double FADE_S = 0.01;
constexpr double LENGTH_MIN_S = 0.01;
constexpr float START_CV_VOLTS = 5.f;   // volts for the whole tape

// Wear, per pass over the tape (w is the knob, 0..1).
constexpr float WEAR_LEVEL_DB = -0.5f;          // times w^2
constexpr float WEAR_HIGHS_AMOUNT = 0.3f;       // times w^2: the way to the low-pass
constexpr float WEAR_HIGHS_HZ = 4000.f;
constexpr float WEAR_DRIVE_BASE = 1.f;          // drive = base + w, into the soft bound
constexpr float WEAR_HISS_DB = -70.f;           // re WEAR_HISS_REF_VOLTS, times w
constexpr float WEAR_HISS_REF_VOLTS = 5.f;
constexpr float WEAR_DROPOUT_RATE = 0.5f;       // times w^2, per second of tape passed
constexpr float WEAR_DROPOUT_MIN_S = 0.005f, WEAR_DROPOUT_MAX_S = 0.04f;
constexpr float WEAR_DROPOUT_GAIN_MIN = 0.3f, WEAR_DROPOUT_GAIN_MAX = 0.8f;
constexpr float WEAR_DROPOUT_EDGE_S = 0.002f;

// The write path's DC blockers: the dub's input (what is added), and the
// trim of the whole tape's offset as it is written (far below the audio
// band, so it costs the tape going round almost nothing a pass).
constexpr float WRITE_DC_HZ = 5.f;
constexpr float TAPE_DC_HZ = 0.01f;

// The engine (EddyEngine.hpp). The tape: 5 minutes, at most 2^24 frames,
// and a tail of 10 ms (the join's longest fade, F) after it.
constexpr double TAPE_MAX_S = 300.0;
constexpr double TAPE_MAX_FRAMES = 16777216.0;
constexpr double TAIL_S = 0.01;
// A recording shorter than its minimum is thrown away, the tape before it
// left as it was: REC_GATE_MIN_S for one the REC gate started and ended (a
// patch can take a short slice), REC_BUTTON_MIN_S for one the button
// started or ended (a quick double tap on the button is a slip, not a
// recording; changed at the listening stop, 2026-11-22: it was 50 ms for
// both). The first REC_BUTTON_MIN_S of every recording is kept in the
// engine's own buffer, sized for this long at REC_BUFFER_FS (above that
// rate the minimums are shorter), and copied onto the tape PRE_COPY_FRAMES
// a sample once the recording stands.
constexpr double REC_GATE_MIN_S = 0.05;
constexpr double REC_BUTTON_MIN_S = 0.3;
constexpr float REC_BUFFER_FS = 768000.f;
constexpr int PRE_COPY_FRAMES = 64;
// While recording the window shows a span this long at first, doubled each
// time the recording reaches its end and held to the tape's capacity, so the
// waveform fills half to all of it (changed at the listening stop,
// 2026-11-21: it was the whole capacity).
constexpr double REC_SPAN_S = 4.0;
// EOC's width; the fade that cuts the oldest voice when all are in use.
constexpr double EOC_S = 0.001;
constexpr double CUT_S = 0.001;
// Controls are read every CONTROL_SAMPLES; the overview rescans at most
// OVERVIEW_BUDGET frames a sample.
constexpr int CONTROL_SAMPLES = 16;
constexpr int OVERVIEW_BUDGET = 32;

} // namespace eddy
} // namespace rka

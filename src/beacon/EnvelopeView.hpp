#pragma once
// Beacon's window geometry: where the four stages sit across the window and
// the curve drawn through them. Rack-free, allocation-free; the module calls
// layoutEnvelope on the UI thread each frame (std::log10 is fine here).
#include "Curve.hpp"
#include "Timing.hpp"
#include "Voice.hpp"

#include <cmath>

namespace rka {
namespace beacon {

struct ViewParams {
	float attack, decay, sustain01, release, shape;   // seconds, 0..1
};

namespace detail {
inline float clamp01(float v) {
	return v >= 0.f ? (v <= 1.f ? v : 1.f) : 0.f;   // NaN -> 0
}
// Width weight of a timed stage: 1 at 1 ms up to 5.78 at 60 s.
inline float timeWeight(float seconds) {
	const float t = seconds >= kMinSeconds ? (seconds <= kMaxSeconds ? seconds : float(kMaxSeconds)) : float(kMinSeconds);   // NaN -> 1 ms
	return 1.f + std::log10(t / float(kMinSeconds));
}
} // namespace detail

// The running stage drawn from its true start: the height at fraction p of
// the stage, from start01 to goal01 along the voice's curve (rising when the
// goal is above the start). start01 at p = 0 and goal01 at p = 1 exactly.
// Each value is clamped to the window, NaN to 0.
inline float liveStageHeight(float p, float start01, float goal01, float shape) {
	const float s = detail::clamp01(start01), g = detail::clamp01(goal01), sh = detail::clamp01(shape);
	const float c = g > s ? curveRise(p, sh) : curveFall(p, sh);
	if (c >= 1.f)
		return g;
	return s + (g - s) * c;
}

struct EnvelopeLayout {
	float x[5];   // stage edges in 0..1 of the inner width: A starts at x[0] = 0, R ends at x[4] = 1
	float shape, sustain01;
	bool ar;      // AR mode: no D or S span (x[1] = x[2] = x[3]); Release falls from the top

	// Point on the drawn envelope at fraction u of the inner width, as height 0..1.
	float heightAt(float u) const {
		u = detail::clamp01(u);
		if (u < x[1])
			return curveRise((u - x[0]) / (x[1] - x[0]), shape);
		if (u < x[2])
			return 1.f + (sustain01 - 1.f) * curveFall((u - x[1]) / (x[2] - x[1]), shape);
		if (u < x[3])
			return sustain01;
		const float top = ar ? 1.f : sustain01;
		return top * (1.f - curveFall((u - x[3]) / (x[4] - x[3]), shape));
	}

	// The span the running Attack, Decay or Release is drawn over, [u0, u1]:
	// its own, always. (A Release that cut Attack short runs over the R
	// span from the level it started at; an Attack resumed part-way is on
	// its own curve.) Other stages: (0, 0).
	void liveSpan(Stage stage, float& u0, float& u1) const {
		if (stage < ATTACK || stage > RELEASE) {
			u0 = u1 = 0.f;
			return;
		}
		u0 = x[stage - 1];
		u1 = x[stage];
	}

	// Point f (0..1) of the running stage's bright line, drawn from the
	// stage's breakpoint (Voice::bpPhase): bpPhase (0..1, NaN 0) of the way
	// across liveSpan to its end, its height liveStageHeight(f, start01,
	// goal01), start01 being the level at the breakpoint. With bpPhase 0
	// (no Shape change during the stage) that is the whole span from the
	// stage's start.
	void livePoint(Stage stage, float f, float start01, float goal01, float& u, float& h, float bpPhase = 0.f) const {
		float u0, u1;
		liveSpan(stage, u0, u1);
		const float b = detail::clamp01(bpPhase);
		u0 = u0 + b * (u1 - u0);
		u = u0 + f * (u1 - u0);
		h = liveStageHeight(f, start01, goal01, shape);
	}

	// Where the dot goes for a stage and phase. Attack, Decay and Release:
	// on the running stage's bright line (livePoint, from the breakpoint
	// bpPhase with start01 its level), so it is on it by construction; a
	// phase before the breakpoint (a sample apart) sits at its start. Sustain has no phase worth drawing: mid-span at the real
	// level, or, with the sustain level at 0, at the end of the line (1, 0).
	// IDLE: x 0, height 0.
	void dot(Stage stage, float phase, float level01, float start01, float goal01, float& u, float& h, float bpPhase = 0.f) const {
		const float p = detail::clamp01(phase), b = detail::clamp01(bpPhase);
		const float f = b < 1.f ? detail::clamp01((p - b) / (1.f - b)) : 1.f;
		switch (stage) {
			case ATTACK:
			case DECAY:
			case RELEASE: livePoint(stage, f, start01, goal01, u, h, b); return;
			case SUSTAIN:
				if (sustain01 <= 0.f) {
					u = 1.f;
					h = 0.f;
				} else {
					u = 0.5f * (x[2] + x[3]);
					h = detail::clamp01(level01);
				}
				return;
			default: u = 0.f; h = 0.f; return;
		}
	}
};

// The stages' spans, widths growing with the log of their times (Sustain a
// fixed share). AR mode (`ar`): only Attack and Release, as the voice runs
// there; Decay and Sustain get no width.
inline EnvelopeLayout layoutEnvelope(const ViewParams& p, bool ar = false) {
	EnvelopeLayout e;
	e.shape = detail::clamp01(p.shape);
	e.sustain01 = detail::clamp01(p.sustain01);
	e.ar = ar;
	const float w[4] = {detail::timeWeight(p.attack), ar ? 0.f : detail::timeWeight(p.decay), ar ? 0.f : 2.5f,
	                    detail::timeWeight(p.release)};
	const float total = w[0] + w[1] + w[2] + w[3];
	float acc = 0.f;
	for (int i = 0; i < 4; i++) {
		e.x[i] = acc / total;
		acc += w[i];
	}
	e.x[4] = 1.f;
	return e;
}

// The Hold line: channel 1's output, a point every 1 / POINTS_PER_SECOND of
// the UI's frame clock (not one per frame), so it scrolls at the same speed
// at 60 and 144 Hz. sample() is called once a frame with the frame's
// timestamp; the newest point follows the level until the next is due.
// A start (or a restart after stop()) fills the ring flat at the level. A
// stall is held to one ring's worth; a NaN or backward clock adds nothing.
struct HoldRing {
	static constexpr int RING = 160;
	static constexpr double POINTS_PER_SECOND = 60.0;

	float ring[RING] = {};
	int pos = 0;        // the next point to write; the newest is pos - 1
	long pushed = 0;    // points written since the start, for tests
	bool live = false;
	double last = 0.0, acc = 0.0;

	void sample(float level, double now) {
		const float v = level == level ? (level < -10.f ? -10.f : level > 10.f ? 10.f : level) : 0.f;
		if (!live) {
			for (int i = 0; i < RING; i++)
				ring[i] = v;
			pos = 0;
			pushed = 0;
			acc = 0.0;
			last = now;
			live = now == now;
			return;
		}
		const double dt = now - last;
		if (!(dt > 0.0))
			return;      // NaN, or the clock went back: nothing (last kept)
		last = now;
		acc += (dt < RING / POINTS_PER_SECOND ? dt : RING / POINTS_PER_SECOND) * POINTS_PER_SECOND;
		int n = int(acc + 1e-6);   // a frame clock at exactly 60 Hz sums to 0.99999..
		acc = n <= acc ? acc - n : 0.0;
		for (; n > 0; n--) {
			ring[pos] = v;
			pos = (pos + 1) % RING;
			pushed++;
		}
		ring[(pos + RING - 1) % RING] = v;
	}
	void stop() { live = false; }

	// Point i, 0 the oldest, RING - 1 the newest.
	float at(int i) const { return ring[(pos + i) % RING]; }
};

} // namespace beacon
} // namespace rka

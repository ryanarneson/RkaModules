#pragma once
// Beacon's window geometry: where the four stages sit across the window and
// the curve drawn through them. Rack-free, allocation-free; the module calls
// layoutEnvelope on the UI thread each frame (std::log10 is fine here).
#include "beacon/Curve.hpp"
#include "beacon/Timing.hpp"
#include "beacon/Voice.hpp"

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

	// Point on the drawn envelope at fraction u of the inner width, as height 0..1.
	float heightAt(float u) const {
		u = detail::clamp01(u);
		if (u < x[1])
			return curveRise((u - x[0]) / (x[1] - x[0]), shape);
		if (u < x[2])
			return 1.f + (sustain01 - 1.f) * curveFall((u - x[1]) / (x[2] - x[1]), shape);
		if (u < x[3])
			return sustain01;
		return sustain01 * (1.f - curveFall((u - x[3]) / (x[4] - x[3]), shape));
	}

	// Where the dot goes for a stage and phase. Attack, Decay and Release:
	// on the running stage's own line (liveStageHeight, from its start to
	// its goal), so it is on the bright line by construction. Sustain has no
	// phase worth drawing: mid-span at the real level, or, with the sustain
	// level at 0, at the end of the line (1, 0). IDLE: x 0, height 0.
	void dot(Stage stage, float phase, float level01, float start01, float goal01, float& u, float& h) const {
		const float p = detail::clamp01(phase);
		switch (stage) {
			case ATTACK: u = x[0] + p * (x[1] - x[0]); break;
			case DECAY: u = x[1] + p * (x[2] - x[1]); break;
			case RELEASE: u = x[3] + p * (x[4] - x[3]); break;
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
		h = liveStageHeight(p, start01, goal01, shape);
	}
};

inline EnvelopeLayout layoutEnvelope(const ViewParams& p) {
	EnvelopeLayout e;
	e.shape = detail::clamp01(p.shape);
	e.sustain01 = detail::clamp01(p.sustain01);
	const float w[4] = {detail::timeWeight(p.attack), detail::timeWeight(p.decay), 2.5f, detail::timeWeight(p.release)};
	const float total = w[0] + w[1] + w[2] + w[3];
	float acc = 0.f;
	for (int i = 0; i < 4; i++) {
		e.x[i] = acc / total;
		acc += w[i];
	}
	e.x[4] = 1.f;
	return e;
}

} // namespace beacon
} // namespace rka

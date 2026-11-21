#pragma once
// Narrows' second core: a 12 dB per octave state-variable filter, in the
// trapezoidal (zero-delay) form of Zavalishin, "The Art of VA Filter
// Design", ch. 4, as src/fathom/Svf.hpp. A template on the sample type, as
// LadderCore: float, or rka::Float4 for four filters in one pass. Run at
// twice the engine's rate, in the ladder's units.
//
// Two things make it a partner to the ladder:
// - Res maps onto the damping so that it reaches zero at RES_ONSET, where
//   the ladder begins to self-oscillate, and goes a little negative above,
//   so the oscillation grows.
// - The damping is not fixed: the resonance path carries a tanh, so as the
//   band-pass signal grows the damping returns toward its Res 0 value.
//   That holds a self-oscillation at a steady level and tames loud
//   resonant peaks. As in the ladder, the tanh's slope is taken from the
//   last sample, which keeps the loop solvable in one step.
// The input has the ladder's soft clip too, so Drive has something to push
// into, and the same tiny noise so self-oscillation starts from silence.
#include "../fathom/LadderCore.hpp"
#include "NarrowsTuning.hpp"

namespace rka {
namespace narrows {

template <typename T>
struct StateOut {
	T lp, bp, hp;
};

template <typename T>
struct StateCore {
	T s1, s2;
	// Last band-pass output, for the resonance clip's slope.
	T bpPrev;

	StateCore() {
		reset();
	}

	void reset() {
		s1 = s2 = bpPrev = splat<T>(0.f);
	}

	// One oversampled-rate sample. g: tan(pi fc / fs) at that rate (what
	// the ladder takes); res: 0 .. 1; white: this sample's noise in
	// [-1, 1].
	__attribute__((always_inline)) StateOut<T> step(T x, T g, T res, T white) {
		const T zero = splat<T>(0.f);
		// The damping Res asks for: STATE_K0 at 0, by the ladder's square
		// law to 0 at RES_ONSET, then a straight line to STATE_K_TOP.
		const T r = res * (1.f / RES_ONSET);
		const T below = STATE_K0 * (1.f - r * r);
		const T above = (STATE_K_TOP / (1.f - RES_ONSET)) * (res - RES_ONSET);
		const T k = vmax(below, zero) + vmin(above, zero);
		// The resonance clip: slope 1 for a small band-pass signal, falling
		// as it grows, which moves the damping from k back toward STATE_K0.
		const T b = vmax(splat<T>(-4.f), vmin(STATE_CLIP * bpPrev, splat<T>(4.f)));
		const T t = vmax(tanhXdX(b), splat<T>(0.01f));
		const T damp = STATE_K0 + (k - STATE_K0) * t;

		const T u = vmax(splat<T>(-4.f), vmin(x + LADDER_NOISE * white, splat<T>(4.f)));
		const T in = tanhXdX(u) * u;

		const T hp = (in - (damp + g) * s1 - s2) / (1.f + g * (damp + g));
		const T v1 = g * hp;
		const T bp = v1 + s1;
		s1 = v1 + bp;
		const T v2 = g * bp;
		const T lp = v2 + s2;
		s2 = v2 + lp;
		bpPrev = bp;

		StateOut<T> o;
		o.lp = lp;
		o.bp = bp;
		o.hp = hp;
		return o;
	}
};

} // namespace narrows
} // namespace rka

#pragma once
// The arithmetic of the four-pole transistor ladder in Ladder.hpp, as a
// template on the sample type: float (Fathom's Ladder) or rka::Float4
// (Narrows: four filters in one pass). One step hands back the loop input
// and the four poles; the caller mixes them into a response and applies
// ladderLimit(). Sources and method: see Ladder.hpp.
//
// Fathom's output must not change: the expressions here are Ladder.hpp's
// as they were, in the same order (test_ladder_shared.cpp compares them
// with the frozen copy, bit for bit).
#include "../Float4.hpp"

namespace rka {

// tan(x) by a degree (3,4) rational function; |x| < pi/2, 2.8e-5 relative.
template <typename T>
inline T tan34(T x) {
	const T x2 = x * x;
	const T num = 1.f + x2 * -0.09776575533683811f;
	const T den = 1.f + x2 * (-0.43119539396382f + x2 * 0.0105011966117302f);
	return x * num / den;
}

// tanh(x)/x by a degree (4,6) rational function; |x| <= 4, 4.2e-6 relative.
template <typename T>
inline T tanhXdX(T x) {
	const T x2 = x * x;
	const T num = 1.f + x2 * (0.121953514066257f + x2 * 0.00204623480007919f);
	const T den = 1.f + x2 * (0.455305674254515f + x2 * (0.0204552909446164f + x2 * 9.48027717633287e-05f));
	return num / den;
}

// Soft limit to +/-2: 2 tanh(v/2).
template <typename T>
inline T ladderLimit(T v) {
	return v * tanhXdX(0.5f * v);
}

// White noise into the loop so self-oscillation starts from silence.
static const float LADDER_NOISE = 1e-6f;

// The loop input after its clamp to +/-4, and the four poles' outputs.
template <typename T>
struct LadderNodes {
	T u, y0, y1, y2, y3;
};

template <typename T>
struct LadderCore {
	// Trapezoidal integrator states and each pole's last output; yPrev[3]
	// is the resonance feedback.
	T s[4];
	T yPrev[4];
	// Last loop input, for the input tanh's slope.
	T uPrev;

	LadderCore() {
		reset();
	}

	void reset() {
		for (int i = 0; i < 4; i++)
			s[i] = yPrev[i] = splat<T>(0.f);
		uPrev = splat<T>(0.f);
	}

	// One oversampled-rate sample. g: the per-pole tan(pi fc / fs) at that
	// rate; k: the feedback; comp: the input gain; white: this sample's
	// noise in [-1, 1], which goes into the loop at LADDER_NOISE.
	//
	// Always inlined: with the plugin's -funsafe-math-optimizations the
	// compiler may reorder this arithmetic, and it only arrives at the
	// order Ladder.hpp had (and so at Fathom's exact output) when it sees
	// step() inside its caller. Out of line, results move in the last bit.
	__attribute__((always_inline)) LadderNodes<T> step(T x, T g, T k, T comp, T white) {
		// Per-stage tanh slope from the previous output, floored so every
		// stage keeps damping itself; the input tanh's slope from the
		// previous loop input.
		const T tMin = splat<T>(0.01f);
		const T t0 = vmax(tanhXdX(yPrev[0]), tMin);
		const T t1 = vmax(tanhXdX(yPrev[1]), tMin);
		const T t2 = vmax(tanhXdX(yPrev[2]), tMin);
		const T t3 = vmax(tanhXdX(yPrev[3]), tMin);
		const T tu = vmax(tanhXdX(uPrev), tMin);

		// With the slopes fixed, each stage is y_i = a_i * in_i + b_i, so the
		// last stage is y3 = P * u_sat + Q and the feedback loop can be
		// solved for this sample's u instead of using the last one's
		// (Zavalishin's zero-delay feedback): self-oscillation then sits at
		// the pole frequency at any sample rate.
		const T a0 = g / (1.f + g * t0), b0 = s[0] / (1.f + g * t0);
		const T a1 = g / (1.f + g * t1), b1 = s[1] / (1.f + g * t1);
		const T a2 = g / (1.f + g * t2), b2 = s[2] / (1.f + g * t2);
		const T a3 = g / (1.f + g * t3), b3 = s[3] / (1.f + g * t3);
		const T P = a3 * t2 * a2 * t1 * a1 * t0 * a0;
		const T Q = a3 * t2 * (a2 * t1 * (a1 * t0 * b0 + b1) + b2) + b3;
		const T u0 = (comp * x - k * Q + LADDER_NOISE * white) / (1.f + k * P * tu);
		const T u0Clamped = vmax(splat<T>(-4.f), vmin(u0, splat<T>(4.f)));
		const T u0Sat = tanhXdX(u0Clamped) * u0Clamped;
		uPrev = u0Clamped;

		const T y0 = a0 * u0Sat + b0;
		const T y1 = a1 * t0 * y0 + b1;
		const T y2 = a2 * t1 * y1 + b2;
		const T y3 = a3 * t2 * y2 + b3;

		s[0] = 2.f * y0 - s[0];
		s[1] = 2.f * y1 - s[1];
		s[2] = 2.f * y2 - s[2];
		s[3] = 2.f * y3 - s[3];
		yPrev[0] = y0;
		yPrev[1] = y1;
		yPrev[2] = y2;
		yPrev[3] = y3;

		LadderNodes<T> n;
		n.u = u0Clamped;
		n.y0 = y0;
		n.y1 = y1;
		n.y2 = y2;
		n.y3 = y3;
		return n;
	}
};

} // namespace rka

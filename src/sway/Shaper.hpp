#pragma once
#include <algorithm>
#include <cmath>
#include "Waves.hpp"

// Rack-independent shaping for Sway's MORPH output: SKEW warps the phase,
// MORPH crossfades sin -> tri -> saw -> square, SMOOTH rounds (< 0) or
// wavefolds (> 0).
namespace rka {

inline float wrap01(float x) {
	return x - std::floor(x);
}

// Warp the phase so the rise (trough -> peak) takes `skew` of the cycle and
// the fall the rest. Measured from the trough (p = 0.75 for sin/tri) so the
// shape stays in phase with SIN and TRI; skew 0.5 leaves p unchanged.
inline float skewPhase(float p, float skew) {
	skew = std::max(0.001f, std::min(skew, 0.999f));
	const float s = wrap01(p + 0.25f);
	const float w = s < skew ? 0.5f * s / skew : 0.5f + 0.5f * (s - skew) / (1.f - skew);
	return wrap01(w - 0.25f);
}

// Triangle fold back into [-1, 1].
inline float fold(float x) {
	const float m = std::fmod(x + 1.f, 4.f);
	return 1.f - std::fabs((m < 0.f ? m + 4.f : m) - 2.f);
}

// dt is the phase increment per sample (0 disables anti-aliasing); pw is
// the square's pulse width.
inline float shape(float p, float skew, float morph, float smooth, float pw, float dt) {
	const float q = skewPhase(p, skew);
	// The warp speeds the phase up by its local slope; scale the BLEP width
	// to match, capped so it stays a short correction.
	const float sk = std::max(0.001f, std::min(skew, 0.999f));
	const float slope = wrap01(p + 0.25f) < sk ? 0.5f / sk : 0.5f / (1.f - sk);
	const float dq = std::min(dt * slope, 0.5f);

	const float m = std::max(0.f, std::min(morph, 1.f)) * 3.f;
	const int k = std::min(int(m), 2);
	const float x = m - k;
	auto wave = [&](int i) {
		switch (i) {
			case 0: return waveSin(q);
			case 1: return waveTri(q);
			case 2: return sawBlep(q, dq);
			default: return sqrBlep(q, pw, dq);
		}
	};
	float y = wave(k) + (wave(k + 1) - wave(k)) * x;

	if (smooth < 0.f)
		y += (std::sin(float(M_PI) / 2.f * y) - y) * -smooth;
	else if (smooth > 0.f)
		y = fold(y * (1.f + 3.f * smooth));
	return y;
}

} // namespace rka

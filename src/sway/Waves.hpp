#pragma once
#include <cmath>

// Rack-independent waveforms of a normalized phase p in [0, 1), output in
// [-1, 1]. SIN and TRI start at 0 rising, so they stay in phase.
namespace rka {

inline float waveSin(float p) {
	return std::sin(2.f * float(M_PI) * p);
}

inline float waveTri(float p) {
	if (p < 0.25f)
		return 4.f * p;
	if (p < 0.75f)
		return 2.f - 4.f * p;
	return 4.f * p - 4.f;
}

inline float waveSaw(float p) {
	return 2.f * p - 1.f;
}

inline float waveSqr(float p, float pw) {
	return p < pw ? 1.f : -1.f;
}

// Two-sample polyBLEP residual for a unit step at t = 0 (t is the phase
// distance from the discontinuity, dt the phase increment per sample).
inline float polyBlep(float t, float dt) {
	if (dt <= 0.f)
		return 0.f;
	if (t < dt) {
		t /= dt;
		return t + t - t * t - 1.f;
	}
	if (t > 1.f - dt) {
		t = (t - 1.f) / dt;
		return t * t + t + t + 1.f;
	}
	return 0.f;
}

// Band-limited saw: the naive saw minus its step at the wrap.
inline float sawBlep(float p, float dt) {
	return waveSaw(p) - polyBlep(p, dt);
}

// Band-limited square: steps up at p = 0 and down at p = pw.
inline float sqrBlep(float p, float pw, float dt) {
	float t = p - pw;
	if (t < 0.f)
		t += 1.f;
	return waveSqr(p, pw) + polyBlep(p, dt) - polyBlep(t, dt);
}

} // namespace rka

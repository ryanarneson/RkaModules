#pragma once
// Beacon's stage curves: phase p in 0..1 to level fraction 0..1, blended by
// Shape from linear (noon) to the classic (right: fast start, long tail) or
// the reverse (left: slow start, late finish). Rack-free; no libm, no table.
namespace rka {
namespace beacon {

namespace detail {
inline float unit(float p) {
	return p >= 0.f ? (p <= 1.f ? p : 1.f) : 0.f;   // NaN -> 0
}
inline float classic(float p) {
	const float q = 1.f - p, q2 = q * q;
	return 1.f - q2 * q2;
}
inline float reverse(float p) {
	const float p2 = p * p;
	return p2 * p2;
}
// p + a (c - p) rather than (1 - a) p + a c: exact at both ends.
inline float blend(float p, float shape) {
	p = unit(p);
	const float a = shape >= 0.5f ? 2.f * shape - 1.f : 1.f - 2.f * shape;
	const float c = shape >= 0.5f ? classic(p) : reverse(p);
	return p + (a <= 1.f ? a : 1.f) * (c - p);
}
} // namespace detail

// Rising stages (goal above start).
inline float curveRise(float p, float shape) {
	return detail::blend(p, shape);
}

// Falling stages (goal below start).
inline float curveFall(float p, float shape) {
	return detail::blend(p, shape);
}

} // namespace beacon
} // namespace rka

#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// Rack-free replacements for the libm calls on Sway's and Fathom's
// per-sample paths. The C library's exp2, pow, sin, cos and tanh are fast
// on macOS and Linux but slow in MinGW's libm, which the Windows build
// uses; these are a few multiplies each and give the same result
// everywhere. Their error bounds are tested in src/fathom/test_fast_math.cpp.
// std::floor compiles to one instruction with the plugin's architecture
// flags (SSE4.1 roundss / ARMv8 frintm), so it stays.
namespace rka {

// 2^n for an integer n in [-126, 127], from the bits.
inline float pow2i(int n) {
	const uint32_t bits = uint32_t(n + 127) << 23;
	float f;
	std::memcpy(&f, &bits, sizeof(f));
	return f;
}

// 2^n for an integer n in [-1022, 1023].
inline double pow2iD(int n) {
	const uint64_t bits = uint64_t(n + 1023) << 52;
	double d;
	std::memcpy(&d, &bits, sizeof(d));
	return d;
}

// 2^x for float x, within 1.2e-7 relative (one float ulp) over the whole
// clamped range [-126, 126]; exactly 1 at 0. x is split into a nearest
// integer and a fraction in [-0.5, 0.5], and 2^fraction is the degree-7
// Taylor series of e^(f ln 2) (truncation under 6e-9).
inline float fastExp2(float x) {
	x = std::max(-126.f, std::min(x, 126.f));
	const float n = std::floor(x + 0.5f);
	const float f = (x - n) * 0.693147180559945f;
	const float p = 1.f + f * (1.f + f * (0.5f + f * (1.f / 6 + f * (1.f / 24 + f * (1.f / 120 + f * (1.f / 720 + f * (1.f / 5040)))))));
	return p * pow2i(int(n));
}

// 2^x for double x (clamped to [-1000, 1000]), within 1e-15 relative
// (tested over Sway's -24 ... 14): the same split, with the degree-13
// series (truncation under 2e-16).
inline double fastExp2D(double x) {
	x = std::max(-1000.0, std::min(x, 1000.0));
	const double n = std::floor(x + 0.5);
	const double f = (x - n) * 0.69314718055994530942;
	double p = 1.0 / 6227020800.0;  // 1/13!
	static const double inv[13] = {1.0 / 479001600.0, 1.0 / 39916800.0, 1.0 / 3628800.0, 1.0 / 362880.0, 1.0 / 40320.0,
	                               1.0 / 5040.0, 1.0 / 720.0, 1.0 / 120.0, 1.0 / 24.0, 1.0 / 6.0, 0.5, 1.0, 1.0};
	for (int i = 0; i < 13; i++)
		p = inv[i] + f * p;
	return p * pow2iD(int(n));
}

// 10^(db / 20): decibels to gain.
inline float fastDbToGain(float db) {
	return fastExp2(db * 0.166096404744368f);  // log2(10) / 20
}

// sin(2 pi p) for any float p, within 2.5e-7 absolute (measured 1.7e-7 on
// arm64, 2.0e-7 on x86-64 without FMA; an argument within a float rounding
// of the libm path's 2 pi p alone gives up to 5e-7), and exactly
// 0 at whole cycles and +/-1 at quarter cycles. The phase is folded to
// q in [-1/2, 1/2]: within 1/8 of a zero crossing, the degree-9 odd Taylor
// series of sin(2 pi q) (truncation under 2e-9); otherwise the degree-12
// series of cos about the nearer peak, |2 pi d| <= pi / 2 (under 7e-9).
inline float fastSin2pi(float p) {
	const float q = p - std::floor(p + 0.5f);
	const float a = std::fabs(q);
	if (a <= 0.125f) {
		const float x = q * 6.28318530717959f;
		const float x2 = x * x;
		return x * (1.f + x2 * (-1.f / 6 + x2 * (1.f / 120 + x2 * (-1.f / 5040 + x2 * (1.f / 362880)))));
	}
	const float x = (0.25f - a) * 6.28318530717959f;
	const float x2 = x * x;
	const float c = 1.f + x2 * (-0.5f + x2 * (1.f / 24 + x2 * (-1.f / 720 + x2 * (1.f / 40320 + x2 * (-1.f / 3628800 + x2 * (1.f / 479001600))))));
	return q < 0.f ? -c : c;
}

// tanh(x) for any float x, within 1.5e-7 absolute: (e - 1) / (e + 1) with
// e = e^(2x) from fastExp2, and x clamped to +/-9, where tanh is 1 within
// 3e-8.
inline float fastTanh(float x) {
	x = std::max(-9.f, std::min(x, 9.f));
	const float e = fastExp2(x * 2.88539008177793f);  // 2 / ln 2
	return (e - 1.f) / (e + 1.f);
}

} // namespace rka

#pragma once
// Four floats side by side, on the compiler's vector type (GCC and Clang:
// the toolchains of all three platforms). Arithmetic and comparisons work
// lane by lane, a float beside a Float4 is spread over the lanes, and v[i]
// reads or writes one lane. No Rack headers and no platform intrinsics.
//
// The helpers below have the same names for one float and for four, so a
// template on the sample type reads the same for both.
#include <algorithm>

namespace rka {

typedef float Float4 __attribute__((vector_size(16)));

inline Float4 splat4(float x) {
	return Float4{x, x, x, x};
}

// As std::min and std::max, lane by lane, with their argument order: the
// first argument is returned when the two compare equal or unordered.
inline float vmin(float a, float b) {
	return std::min(a, b);
}
inline float vmax(float a, float b) {
	return std::max(a, b);
}
inline Float4 vmin(Float4 a, Float4 b) {
	return b < a ? b : a;
}
inline Float4 vmax(Float4 a, Float4 b) {
	return a < b ? b : a;
}

// One value in every lane of T (float or Float4).
template <typename T>
inline T splat(float x);
template <>
inline float splat<float>(float x) {
	return x;
}
template <>
inline Float4 splat<Float4>(float x) {
	return splat4(x);
}

} // namespace rka

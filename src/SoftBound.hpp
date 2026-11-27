#pragma once
// The output's soft bound, shared by Eddy (eddy/Wear.hpp: its write path and
// its outputs) and Doldrums: the exact identity up to 8 V (bit for bit), then
// tanh-shaped into a ceiling of 10 V. The input must not be NaN. No libm.
#include "FastMath.hpp"

namespace rka {

inline float softBound(float x) {
	const float a = x >= 0.f ? x : -x;
	if (a <= 8.f)
		return x;
	const float y = 8.f + 2.f * fastTanh((a - 8.f) * 0.5f);
	return x >= 0.f ? y : -y;
}

} // namespace rka

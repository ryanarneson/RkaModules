#pragma once
// Hadal's random source: PCG32 (O'Neill), one per user, seeded by the
// engine. No global state, so two Hadals with different seeds never share a
// stream, and one seed repeats exactly (the tests rely on both).
#include <cstdint>

namespace rka {
namespace hadal {

struct Random {
	uint64_t state = 0x853c49e6748fea9bULL;

	void seed(uint32_t s) {
		state = 0x9E3779B97F4A7C15ULL * ((uint64_t) s + 1u);
		next();
		next();
	}

	uint32_t next() {
		const uint64_t old = state;
		state = old * 6364136223846793005ULL + 1442695040888963407ULL;
		const uint32_t xs = (uint32_t) (((old >> 18u) ^ old) >> 27u);
		const uint32_t rot = (uint32_t) (old >> 59u);
		return (xs >> rot) | (xs << ((32u - rot) & 31u));
	}

	// [0, 1)
	float unit() {
		return (float) (next() >> 8) * (1.f / 16777216.f);
	}

	// [-1, 1)
	float uniform() {
		return unit() * 2.f - 1.f;
	}
};

} // namespace hadal
} // namespace rka

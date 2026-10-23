#pragma once
#include <cmath>
#include <cstdint>

// Rack-independent random voltages for the RAND output. splitmix64
// rather than <random> so every platform produces the same sequence.
namespace rka {

struct RandomGen {
	// The value the last cycle ended on, and the one this cycle heads to.
	float prev = 0.f;
	float next = 0.f;

	explicit RandomGen(uint64_t seed = 1) : state(seed) {}

	// Call on each cycle wrap.
	void advance() {
		prev = next;
		next = uniform();
	}

	// The output at cycle phase p in [0, 1]: a cosine glide from prev to
	// next over the first `glide` of the cycle, then holds next. glide 0 is
	// stepped; glide 1 glides over the whole cycle.
	float value(float p, float glide) const {
		if (p >= glide)
			return next;
		const float t = p / glide;
		return prev + (next - prev) * (1.f - std::cos(float(M_PI) * t)) * 0.5f;
	}

private:
	uint64_t state;

	// Uniform in [-1, 1].
	float uniform() {
		uint64_t z = (state += 0x9e3779b97f4a7c15ull);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		z ^= z >> 31;
		// Top 24 bits -> [0, 1] exactly representable in a float.
		return float(z >> 40) / float((1 << 24) - 1) * 2.f - 1.f;
	}
};

} // namespace rka

#pragma once
#include <cmath>
#include <cstdint>

// Rack-independent random voltages for the STEP and RAND outputs. splitmix64
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

	// Stepped: holds the new value for the whole cycle.
	float step() const {
		return next;
	}

	// Smooth: cosine glide from prev to next over the cycle (p in [0, 1]).
	float smooth(float p) const {
		return prev + (next - prev) * (1.f - std::cos(float(M_PI) * p)) * 0.5f;
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

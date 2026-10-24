#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Rack-independent slow random walks that keep a held drone breathing:
// each saw gets its own pitch offset (cents) and level offset (dB), drawn
// afresh about twice a second and slewed over about a second so nothing
// steps. splitmix64 as in Sway's RandomGen, so every platform drifts the
// same way from the same seed.
namespace rka {

struct DriftWalk {
	static constexpr float MAX_CENTS = 6.f;
	static constexpr float MAX_DB = 3.f;
	static constexpr float INTERVAL_SEC = 0.5f;
	static constexpr float SLEW_SEC = 1.f;

	// Current, slewed offsets.
	float cents = 0.f;
	float db = 0.f;

	explicit DriftWalk(uint64_t seed = 1) : state(seed) {}

	// amount 0..1 (clamped) scales the targets; 0 glides back to rest.
	void process(float amount, float dt) {
		const float a = std::max(0.f, std::min(amount, 1.f));
		timer -= dt;
		if (timer <= 0.f) {
			targetCents = MAX_CENTS * a * uniform();
			targetDb = -MAX_DB * a * (0.5f + 0.5f * uniform());
			timer = INTERVAL_SEC * (1.f + 0.25f * uniform());
		}
		if (dt != lastDt) {
			coef = 1.f - std::exp(-dt / SLEW_SEC);
			lastDt = dt;
		}
		cents += (targetCents - cents) * coef;
		db += (targetDb - db) * coef;
	}

private:
	uint64_t state;
	float targetCents = 0.f;
	float targetDb = 0.f;
	// Seconds until the next draw; 0 so the first call draws.
	float timer = 0.f;
	float coef = 0.f;
	float lastDt = -1.f;

	// Uniform in [-1, 1].
	float uniform() {
		uint64_t z = (state += 0x9e3779b97f4a7c15ull);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		z ^= z >> 31;
		return float(z >> 40) / float((1 << 24) - 1) * 2.f - 1.f;
	}
};

// Walks for the seven saws and the sub, with their outputs as multipliers.
struct Drift {
	static const int N = 7;
	DriftWalk saws[N];
	DriftWalk sub;
	float pitchRatio[N];
	float levelGain[N];
	// The sub drifts in level only.
	float subGain = 1.f;

	Drift() : sub(100) {
		for (int i = 0; i < N; i++) {
			saws[i] = DriftWalk(i + 1);
			pitchRatio[i] = 1.f;
			levelGain[i] = 1.f;
		}
	}

	void process(float amount, float dt) {
		for (int i = 0; i < N; i++) {
			saws[i].process(amount, dt);
			pitchRatio[i] = std::exp2(saws[i].cents / 1200.f);
			levelGain[i] = std::pow(10.f, saws[i].db / 20.f);
		}
		sub.process(amount, dt);
		subGain = std::pow(10.f, sub.db / 20.f);
	}
};

} // namespace rka

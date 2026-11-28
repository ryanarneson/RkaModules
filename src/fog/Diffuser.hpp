#pragma once

// Multichannel allpass-style diffuser: up to 4 stages, each a bank of N
// delays (lengths drawn by seed within the stage's span), a channel shuffle
// with polarity flips, and an orthonormal Hadamard mix. Delays plus an
// orthonormal mix preserve energy. N must be a power of two.
//
// tap[s] is stage s's output as of the last process(): the sound after s + 1
// stages, each channel a different +/- sum of every echo so far (N after one
// stage, N^2 after two), for a caller that wants the early part of the
// build-up as well as the end of it.
#include "DelayLine.hpp"
#include <cstdint>

namespace rka {

template <int N>
struct Diffuser {
	static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
	static const int MAX_STAGES = 4;
	// Longest stage span the buffers are sized for, times maxScale.
	static constexpr float MAX_STAGE_MS = 100.f;

	DelayLine lines[MAX_STAGES][N];
	int delay[MAX_STAGES][N];
	int perm[MAX_STAGES][N];
	float sign[MAX_STAGES][N];
	int stages = 0;
	float norm = 1.f;
	float tap[MAX_STAGES][N] = {};

	static uint32_t lcg(uint32_t& s) {
		s = s * 1664525u + 1013904223u;
		return s >> 8; // 24 bits
	}

	// staggerBase: line (s, i) gets stagger index staggerBase + s * N + i, so
	// no two lines share a cache set (see DelayLine::allocate).
	void allocate(float sampleRate, float maxScale, uint32_t staggerBase = 0) {
		const int maxD = (int) (MAX_STAGE_MS * 0.001f * sampleRate * maxScale) + 4;
		for (int s = 0; s < MAX_STAGES; s++)
			for (int i = 0; i < N; i++)
				lines[s][i].allocate(maxD, staggerBase + (uint32_t) (s * N + i));
		stages = 0;
	}

	void configure(float sampleRate, const float* stageMs, int nStages, float scale, uint32_t seed) {
		stages = nStages < 0 ? 0 : nStages > MAX_STAGES ? MAX_STAGES : nStages;
		// 1 / sqrt(N) by Newton iteration (no libm).
		float r = 1.f;
		for (int k = 0; k < 20; k++)
			r = 0.5f * (r + (float) N / r);
		norm = 1.f / r;
		for (int s = 0; s < stages; s++) {
			uint32_t st = seed * 2654435761u + (uint32_t) (s + 1) * 40503u;
			lcg(st);
			const float span = stageMs[s] * 0.001f * sampleRate * scale;
			for (int i = 0; i < N; i++) {
				const float u = (float) lcg(st) * (1.f / 16777216.f);
				int d = (int) (span * ((float) i + 0.25f + 0.5f * u) / (float) N);
				const int hi = lines[s][i].maxDelay() - 2;
				d = d < 1 ? 1 : d > hi ? hi : d;
				delay[s][i] = d;
				perm[s][i] = i;
			}
			for (int i = N - 1; i > 0; i--) {
				const int j = (int) (lcg(st) % (uint32_t) (i + 1));
				const int t = perm[s][i];
				perm[s][i] = perm[s][j];
				perm[s][j] = t;
			}
			for (int i = 0; i < N; i++)
				sign[s][i] = (lcg(st) & 0x100u) ? -1.f : 1.f;
		}
	}

	void clear() {
		for (int s = 0; s < MAX_STAGES; s++)
			for (int i = 0; i < N; i++) {
				lines[s][i].clear();
				tap[s][i] = 0.f;
			}
	}

	void process(float* x) {
		for (int s = 0; s < stages; s++) {
			float t[N];
			for (int i = 0; i < N; i++) {
				lines[s][i].write(x[i]);
				t[perm[s][i]] = sign[s][i] * lines[s][i].readInt(delay[s][i]);
			}
			for (int h = 1; h < N; h <<= 1) {
				for (int i = 0; i < N; i += 2 * h) {
					for (int j = i; j < i + h; j++) {
						const float a = t[j], b = t[j + h];
						t[j] = a + b;
						t[j + h] = a - b;
					}
				}
			}
			for (int i = 0; i < N; i++)
				tap[s][i] = x[i] = t[i] * norm;
		}
	}
};

} // namespace rka

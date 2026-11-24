#pragma once
// Spring's Rack-free core: Neap's eight shaped cycle values (+-5 V) in,
// sixteen outputs (volts) out; plus the link detection helper.
//
// No libm, no allocation, NaN-safe. SpringCore has no threading: the module
// calls step() from fromNeap, and reset() plus zeroed outputs when the link
// is lost.
#include <atomic>
#include <stdint.h>

namespace rka {

struct SpringCore {
	enum { F1, F3, F5, F7, MIN23, MAX23, MIN67, MAX67, T2, T4, T6, T8, S14, S58, SODD, SEVEN, N_OUT };

	SpringCore() {
		for (int i = 0; i < 4; i++)
			patched_[i] = false;
		sampleTime_ = 1.f / 48000.f;
		reset();
	}

	// Clears trigger state and outputs. The next step() only records values,
	// so a re-link never fires a burst.
	void reset() {
		for (int i = 0; i < 4; i++) {
			have_[i] = false;
			prev_[i] = 0.f;
			timer_[i] = 0.f;
		}
		for (int i = 0; i < N_OUT; i++)
			out[i] = 0.f;
	}

	void setPatched(int tIndex, bool patched) {
		if (tIndex >= 0 && tIndex < 4)
			patched_[tIndex] = patched;
	}

	void step(const float x[8], float sampleTime) {
		if (sampleTime > 0.f && sampleTime < 1e9f) // NaN fails both
			sampleTime_ = sampleTime;
		const float dt = sampleTime_;

		// Folds.
		out[F1] = fold(x[0]);
		out[F3] = fold(x[2]);
		out[F5] = fold(x[4]);
		out[F7] = fold(x[6]);

		// Min / max.
		out[MIN23] = pick(x[1], x[2], false);
		out[MAX23] = pick(x[1], x[2], true);
		out[MIN67] = pick(x[5], x[6], false);
		out[MAX67] = pick(x[5], x[6], true);

		// Triggers: sources are cycles 2, 4, 6, 8.
		bool src[4];
		for (int k = 0; k < 4; k++) {
			const float v = x[2 * k + 1];
			if (timer_[k] > 0.f)
				timer_[k] -= dt;
			if (v != v) {
				have_[k] = false; // no previous
			}
			else {
				if (have_[k] && prev_[k] < 0.f && v >= 0.f)
					timer_[k] = kPulse; // new or restarted
				prev_[k] = v;
				have_[k] = true;
			}
			src[k] = timer_[k] > 0.f;
		}
		for (int k = 0; k < 4; k++) {
			bool hi = false;
			if (patched_[k]) {
				hi = src[k];
				for (int j = (k + 3) & 3; j != k && !patched_[j]; j = (j + 3) & 3)
					hi = hi || src[j];
			}
			out[T2 + k] = hi ? 10.f : 0.f;
		}

		// Steps: first named cycle is the MSB.
		out[S14] = code(x[0], x[1], x[2], x[3]);
		out[S58] = code(x[4], x[5], x[6], x[7]);
		out[SODD] = code(x[0], x[2], x[4], x[6]);
		out[SEVEN] = code(x[1], x[3], x[5], x[7]);
	}

	float out[N_OUT];

private:
	static constexpr float kPulse = 0.010f;

	static float fold(float v) {
		if (v != v)
			return 0.f;
		const float a = 2.f * (v < 0.f ? -v : v);
		return a > 10.f ? 10.f : a;
	}
	static float pick(float a, float b, bool wantMax) {
		if (a != a)
			return b != b ? 0.f : b;
		if (b != b)
			return a;
		return wantMax ? (a > b ? a : b) : (a < b ? a : b);
	}
	static float code(float a, float b, float c, float d) {
		const int n = 8 * (a > 0.f) + 4 * (b > 0.f) + 2 * (c > 0.f) + (d > 0.f);
		return n == 15 ? 10.f : n * 10.f / 15.f;
	}

	bool patched_[4];
	bool have_[4];
	float prev_[4];
	float timer_[4];
	float sampleTime_;
};

// Unlink detection. Neap's thread calls neapCalled(); Spring's process()
// calls linkedAfterProcess() once per call. Linked until 64 consecutive
// process calls see no change in the counter; starts unlinked.
struct SpringLink {
	void neapCalled() {
		count_.fetch_add(1, std::memory_order_relaxed);
	}
	bool linkedAfterProcess() {
		const uint32_t c = count_.load(std::memory_order_relaxed);
		if (c != seen_) {
			seen_ = c;
			idle_ = 0;
			linked_ = true;
		}
		else if (idle_ < kTimeout) {
			idle_++;
		}
		if (idle_ >= kTimeout)
			linked_ = false;
		return linked_;
	}

private:
	enum { kTimeout = 64 };
	std::atomic<uint32_t> count_{0};
	uint32_t seen_ = 0;
	uint32_t idle_ = kTimeout;
	bool linked_ = false;
};

} // namespace rka

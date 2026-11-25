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
	// Neap's values are within +-5 V; whatever else arrives (an infinity, 1e30)
	// stays inside a jack's +-10 V.
	static float pick(float a, float b, bool wantMax) {
		float v;
		if (a != a)
			v = b != b ? 0.f : b;
		else if (b != b)
			v = a;
		else
			v = wantMax ? (a > b ? a : b) : (a < b ? a : b);
		return v > 10.f ? 10.f : v < -10.f ? -10.f : v;
	}
	static float code(float a, float b, float c, float d) {
		const int n = 8 * (a > 0.f) + 4 * (b > 0.f) + 2 * (c > 0.f) + (d > 0.f);
		return n == 15 ? 10.f : n * (2.f / 3.f);
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

// The module's whole Neap-thread contract, Rack-free: the core, the link
// counter and a reset latch. request() may come from any thread (a reset, a
// bypass or un-bypass from the UI, an unlink seen by Spring's process());
// the core itself is only touched in fromNeap(), where the latch is taken
// and the core reset before the next step, so a bypassed stretch never
// leaves old trigger history to resume.
struct SpringRun {
	SpringCore core;
	SpringLink link;

	void requestReset() {
		resetPending_.store(true, std::memory_order_relaxed);
	}
	void fromNeap(const float x[8], float sampleTime, const bool patched[4]) {
		link.neapCalled();
		for (int k = 0; k < 4; k++)
			core.setPatched(k, patched[k]);
		if (resetPending_.exchange(false, std::memory_order_relaxed))
			core.reset();
		core.step(x, sampleTime);
	}

private:
	std::atomic<bool> resetPending_{true};
};

} // namespace rka

#pragma once
// Real FFT of a power-of-two length, on pffft (src/dep/pffft/, vendored,
// compiled once as pffft.o with its symbols renamed rka_pffft_*). Shared by
// the plugin and the Rack-free tests, so both run the same transform.
// Needs pffft.o linked (tests.mk builds it for every Doldrums test).
#include "dep/pffft/rka_prefix.h"
#include "dep/pffft/pffft.h"

namespace rka {

// Real transform of n points (a power of two, 32 <= n <= 131072). Buffers
// come from the constructor; nothing is allocated after it.
struct RealFft {
	explicit RealFft(int n)
		: n_(n),
		  setup_(pffft_new_setup(n, PFFFT_REAL)),
		  work_(alloc(n)) {
		if (!setup_ || !work_)
			n_ = 0;
	}
	~RealFft() {
		if (setup_)
			pffft_destroy_setup(setup_);
		free(work_);
	}
	RealFft(const RealFft&) = delete;
	RealFft& operator=(const RealFft&) = delete;

	// 0 if n was not one pffft takes (then forward and inverse do nothing).
	int size() const { return n_; }

	// in: n reals. out: n floats, re/im interleaved for bins 0..n/2-1
	// (bin 0's imaginary slot holds the Nyquist bin's real part, as pffft
	// orders it). in and out are 16-byte aligned (alloc) and may not overlap.
	void forward(const float* in, float* out) {
		if (n_)
			pffft_transform_ordered(setup_, in, out, work_, PFFFT_FORWARD);
	}
	// The reverse. inverse(forward(x)) == n * x.
	void inverse(const float* in, float* out) {
		if (n_)
			pffft_transform_ordered(setup_, in, out, work_, PFFFT_BACKWARD);
	}

	static float* alloc(int n) {
		return static_cast<float*>(pffft_aligned_malloc(sizeof(float) * (size_t) (n > 0 ? n : 1)));
	}
	static void free(float* p) { pffft_aligned_free(p); }

private:
	int n_;
	PFFFT_Setup* setup_;
	float* work_;
};

}

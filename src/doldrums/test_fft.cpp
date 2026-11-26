// rka::RealFft (src/Fft.hpp, on the vendored pffft): the impulse's flat
// spectrum, a sine's bin, the round trip, a naive DFT as an independent path,
// and no allocation after construction. Built with the sanitizers and
// pffft.o; a counting operator new and the sanitizer's malloc hook see
// every allocation, pffft's included.
#include "testing.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static long long gAllocs = 0;

void* operator new(std::size_t n) {
	gAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
	return operator new(n);
}
void operator delete[](void* p) noexcept {
	std::free(p);
}
void operator delete(void* p) noexcept {
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}

extern "C" int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void*, size_t),
                                                         void (*free_hook)(const volatile void*));
static long long gMallocs = 0;
static void mallocHook(const volatile void*, size_t) {
	gMallocs++;
}
static void freeHook(const volatile void*) {}

#include "Fft.hpp"

namespace {

uint32_t gRng = 12345;
float noise() {
	gRng ^= gRng << 13;
	gRng ^= gRng >> 17;
	gRng ^= gRng << 5;
	return (float) (gRng >> 8) / (float) (1 << 23) - 1.f;
}

const double PI = 3.14159265358979323846;

void impulse_is_flat() {
	const int n = 1024;
	rka::RealFft fft(n);
	float* in = rka::RealFft::alloc(n);
	float* out = rka::RealFft::alloc(n);
	for (int i = 0; i < n; i++)
		in[i] = i == 0 ? 1.f : 0.f;
	fft.forward(in, out);
	float worst = 0.f;
	for (int k = 0; k < n / 2; k++) {
		worst = std::fmax(worst, std::fabs(out[2 * k] - 1.f));
		worst = std::fmax(worst, k == 0 ? 0.f : std::fabs(out[2 * k + 1]));
	}
	// Bin 0's second slot is the Nyquist bin's real part, also 1.
	worst = std::fmax(worst, std::fabs(out[1] - 1.f));
	CHECK(worst < 1e-6f, "impulse spectrum off flat by %g", worst);
	rka::RealFft::free(in);
	rka::RealFft::free(out);
}

void sine_lands_in_its_bin() {
	const int n = 16384, bin = 1000;
	rka::RealFft fft(n);
	float* in = rka::RealFft::alloc(n);
	float* out = rka::RealFft::alloc(n);
	for (int i = 0; i < n; i++)
		in[i] = (float) std::sin(2.0 * PI * (double) ((int64_t) bin * i % n) / n);
	fft.forward(in, out);
	double mag = std::hypot(out[2 * bin], out[2 * bin + 1]);
	CHECK(std::fabs(mag - n / 2.0) < 1e-3 * (n / 2.0), "bin %d holds %g, want %g", bin, mag, n / 2.0);
	double worst = 0;
	for (int k = 1; k < n / 2; k++) {
		if (k == bin)
			continue;
		worst = std::fmax(worst, std::hypot(out[2 * k], out[2 * k + 1]));
	}
	CHECK(worst < 1e-4 * mag, "other bins reach %g (limit %g)", worst, 1e-4 * mag);
	rka::RealFft::free(in);
	rka::RealFft::free(out);
}

void round_trip() {
	const int sizes[] = {8192, 32768, 131072};
	for (int n : sizes) {
		rka::RealFft fft(n);
		CHECK(fft.size() == n, "size %d", fft.size());
		float* in = rka::RealFft::alloc(n);
		float* mid = rka::RealFft::alloc(n);
		float* out = rka::RealFft::alloc(n);
		for (int i = 0; i < n; i++)
			in[i] = noise();
		fft.forward(in, mid);
		fft.inverse(mid, out);
		float worst = 0.f;
		for (int i = 0; i < n; i++)
			worst = std::fmax(worst, std::fabs(out[i] / (float) n - in[i]));
		CHECK(worst < 1e-5f, "n=%d round trip off by %g", n, worst);
		rka::RealFft::free(in);
		rka::RealFft::free(mid);
		rka::RealFft::free(out);
	}
}

void matches_a_naive_dft() {
	const int n = 64;
	rka::RealFft fft(n);
	float* in = rka::RealFft::alloc(n);
	float* out = rka::RealFft::alloc(n);
	for (int i = 0; i < n; i++)
		in[i] = noise();
	fft.forward(in, out);
	double worst = 0;
	for (int k = 0; k <= n / 2; k++) {
		double re = 0, im = 0;
		for (int i = 0; i < n; i++) {
			double a = 2.0 * PI * (double) ((int64_t) k * i % n) / n;
			re += in[i] * std::cos(a);
			im -= in[i] * std::sin(a);
		}
		if (k == 0) {
			worst = std::fmax(worst, std::fabs(out[0] - re));
		}
		else if (k == n / 2) {
			// The Nyquist bin's real part sits in bin 0's imaginary slot.
			worst = std::fmax(worst, std::fabs(out[1] - re));
		}
		else {
			worst = std::fmax(worst, std::fabs(out[2 * k] - re));
			worst = std::fmax(worst, std::fabs(out[2 * k + 1] - im));
		}
	}
	CHECK(worst < 1e-4, "differs from the DFT by %g", worst);
	rka::RealFft::free(in);
	rka::RealFft::free(out);
}

void no_allocation_after_construction() {
	const int n = 4096;
	__sanitizer_install_malloc_and_free_hooks(mallocHook, freeHook);
	long long m0 = gMallocs;
	rka::RealFft fft(n);
	float* in = rka::RealFft::alloc(n);
	float* out = rka::RealFft::alloc(n);
	CHECK(gMallocs > m0, "the malloc hook saw nothing: it cannot be trusted");
	for (int i = 0; i < n; i++)
		in[i] = noise();
	long long m1 = gMallocs, a1 = gAllocs;
	for (int r = 0; r < 20; r++) {
		fft.forward(in, out);
		fft.inverse(out, in);
		for (int i = 0; i < n; i++)
			in[i] /= (float) n;
	}
	CHECK(gMallocs == m1, "%lld mallocs in forward/inverse", gMallocs - m1);
	CHECK(gAllocs == a1, "%lld operator new calls in forward/inverse", gAllocs - a1);
	__sanitizer_install_malloc_and_free_hooks(nullptr, nullptr);
	rka::RealFft::free(in);
	rka::RealFft::free(out);
}

}

int main() {
	impulse_is_flat();
	sine_lands_in_its_bin();
	round_trip();
	matches_a_naive_dft();
	no_allocation_after_construction();
	return rkatest::finish("test_fft");
}

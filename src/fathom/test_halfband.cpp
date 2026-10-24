// Unit tests for src/fathom/Halfband.hpp. Run with `make test`.
#include "Halfband.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

// A 1 kHz sine up and straight back down comes out at unity (within
// 0.1 dB), after settling.
static void testRoundTripIsUnity() {
	Upsampler2x up;
	Downsampler2x down;
	const float fs = 48000.f;
	double sumIn = 0.0, sumOut = 0.0;
	for (int i = 0; i < 8192; i++) {
		const float x = std::sin(2.f * float(M_PI) * 1000.f * i / fs);
		float os[2];
		up.process(x, os);
		const float y = down.process(os);
		if (i >= 4096) {
			sumIn += double(x) * x;
			sumOut += double(y) * y;
		}
	}
	const double dB = 10.0 * std::log10(sumOut / sumIn);
	CHECK(std::fabs(dB) < 0.1, "round trip %f dB", dB);
}

// Content at the oversampled Nyquist (alternating +1/-1) is removed on the
// way down: below -60 dB.
static void testDownsamplerRejectsNyquist() {
	Downsampler2x down;
	double sum = 0.0;
	int n = 0;
	for (int i = 0; i < 8192; i++) {
		const float os[2] = {1.f, -1.f};
		const float y = down.process(os);
		if (i >= 4096) {
			sum += double(y) * y;
			n++;
		}
	}
	const double dB = 10.0 * std::log10(sum / n + 1e-30);
	CHECK(dB < -60.0, "alternating input leaks at %f dB", dB);
}

int main() {
	testRoundTripIsUnity();
	testDownsamplerRejectsNyquist();
	return rkatest::finish("test_halfband");
}

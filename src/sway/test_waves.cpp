// Unit tests for src/sway/Waves.hpp. Run with `make test`.
#include "Waves.hpp"
#include "testing.hpp"
#include <cmath>
#include <initializer_list>

using namespace rka;

static const int N = 48000;

static void testRanges() {
	float lo[4] = {1, 1, 1, 1}, hi[4] = {-1, -1, -1, -1};
	for (int i = 0; i < N; i++) {
		const float p = float(i) / N;
		const float v[4] = {waveSin(p), waveTri(p), waveSaw(p), waveSqr(p, 0.5f)};
		for (int k = 0; k < 4; k++) {
			lo[k] = std::fmin(lo[k], v[k]);
			hi[k] = std::fmax(hi[k], v[k]);
		}
	}
	const char* names[4] = {"sin", "tri", "saw", "sqr"};
	for (int k = 0; k < 4; k++) {
		CHECK(lo[k] >= -1.f && lo[k] < -0.99f, "%s min %f", names[k], lo[k]);
		CHECK(hi[k] <= 1.f && hi[k] > 0.99f, "%s max %f", names[k], hi[k]);
	}
}

static void testTriangleContinuity() {
	float maxStep = 0.f, prev = waveTri(0.f);
	for (int i = 1; i <= 2 * N; i++) {
		const float v = waveTri(float(i % N) / N);
		maxStep = std::fmax(maxStep, std::fabs(v - prev));
		prev = v;
	}
	CHECK(maxStep < 5.f / N, "triangle jumped by %f", maxStep);
}

static void testPulseWidth() {
	for (float pw : {0.1f, 0.5f, 0.9f}) {
		int high = 0;
		for (int i = 0; i < N; i++)
			high += waveSqr(float(i) / N, pw) > 0.f;
		const float duty = float(high) / N;
		CHECK(std::fabs(duty - pw) < 0.001f, "pw %f gave duty %f", pw, duty);
	}
}

static void testSinTriInPhase() {
	CHECK(waveSin(0.f) == 0.f && waveTri(0.f) == 0.f, "both start at 0");
	CHECK(std::fabs(waveSin(0.25f) - 1.f) < 1e-6f && std::fabs(waveTri(0.25f) - 1.f) < 1e-6f, "both peak at 1/4");
	CHECK(std::fabs(waveSin(0.75f) + 1.f) < 1e-6f && std::fabs(waveTri(0.75f) + 1.f) < 1e-6f, "both trough at 3/4");
	CHECK(waveSaw(0.f) == -1.f && waveSaw(0.5f) == 0.f, "saw rises from -1");
}

static void testBlepIdentityAtZeroDt() {
	for (int i = 0; i < 1000; i++) {
		const float p = i / 1000.f;
		CHECK(sawBlep(p, 0.f) == waveSaw(p), "saw at p=%f", p);
		CHECK(sqrBlep(p, 0.3f, 0.f) == waveSqr(p, 0.3f), "sqr at p=%f", p);
	}
}

// Sum of squared first differences: a cheap proxy for energy near Nyquist.
template <typename F>
static double roughness(F wave) {
	double sum = 0.0, prev = wave(0.f);
	for (int i = 1; i < 4096; i++) {
		const double v = wave(float(std::fmod(i * 5000.0 / 48000.0, 1.0)));
		sum += (v - prev) * (v - prev);
		prev = v;
	}
	return sum;
}

static void testBlepReducesAliasing() {
	const float dt = 5000.f / 48000.f;
	const double naiveSaw = roughness([](float p) { return waveSaw(p); });
	const double blepSaw = roughness([dt](float p) { return sawBlep(p, dt); });
	CHECK(blepSaw < naiveSaw, "saw: blep %f vs naive %f", blepSaw, naiveSaw);
	const double naiveSqr = roughness([](float p) { return waveSqr(p, 0.5f); });
	const double blepSqr = roughness([dt](float p) { return sqrBlep(p, 0.5f, dt); });
	CHECK(blepSqr < naiveSqr, "sqr: blep %f vs naive %f", blepSqr, naiveSqr);
}

int main() {
	testRanges();
	testTriangleContinuity();
	testPulseWidth();
	testSinTriInPhase();
	testBlepIdentityAtZeroDt();
	testBlepReducesAliasing();
	return rkatest::finish("test_waves");
}

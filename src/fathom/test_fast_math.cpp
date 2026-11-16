// Error bounds of src/FastMath.hpp against libm in double precision, over
// dense sweeps of each function's range. Built with the plugin's
// floating-point flags (tests.mk). Run with `make test`.
#include "FastMath.hpp"
#include "testing.hpp"
#include <cmath>

using namespace rka;

// Every float in [lo, hi] at a step, plus the ends.
template <typename F>
static void sweep(double lo, double hi, long n, F f) {
	for (long i = 0; i <= n; i++)
		f(float(lo + (hi - lo) * double(i) / double(n)));
}

static void testExp2() {
	double worst = 0.0, worstX = 0.0;
	sweep(-126.0, 126.0, 4000000, [&](float x) {
		const double want = std::exp2(double(x));
		const double err = std::fabs(double(fastExp2(x)) / want - 1.0);
		if (err > worst) {
			worst = err;
			worstX = x;
		}
	});
	// The drift range, finely: cents / 1200 and dB * log2(10) / 20.
	sweep(-0.6, 0.6, 2000000, [&](float x) {
		const double err = std::fabs(double(fastExp2(x)) / std::exp2(double(x)) - 1.0);
		if (err > worst) {
			worst = err;
			worstX = x;
		}
	});
	CHECK(worst < 1.2e-7, "fastExp2 relative error %g at %g", worst, worstX);
	CHECK(fastExp2(0.f) == 1.f, "fastExp2(0) is %.9g", fastExp2(0.f));
	CHECK(fastExp2(-1000.f) > 0.f && std::isfinite(fastExp2(1000.f)), "fastExp2 clamps");
	std::printf("fastExp2: max relative error %.3g\n", worst);
}

static void testExp2D() {
	double worst = 0.0, worstX = 0.0;
	for (long i = 0; i <= 4000000; i++) {
		// Sway's range, -24 ... 14, in float steps as it is fed.
		const double x = double(float(-24.0 + 38.0 * double(i) / 4000000.0));
		const double err = std::fabs(fastExp2D(x) / std::exp2(x) - 1.0);
		if (err > worst) {
			worst = err;
			worstX = x;
		}
	}
	CHECK(worst < 1e-15, "fastExp2D relative error %g at %g", worst, worstX);
	CHECK(fastExp2D(0.0) == 1.0 && fastExp2D(3.0) == 8.0, "fastExp2D at integers");
	std::printf("fastExp2D: max relative error %.3g\n", worst);
}

static void testDbToGain() {
	double worst = 0.0;
	sweep(-3.0, 3.0, 600000, [&](float db) {
		const double err = std::fabs(double(fastDbToGain(db)) / std::pow(10.0, double(db) / 20.0) - 1.0);
		worst = std::fmax(worst, err);
	});
	CHECK(worst < 1.2e-7, "fastDbToGain relative error %g", worst);
}

static void testSin() {
	double worst = 0.0, worstP = 0.0;
	auto check = [&](float p) {
		const double err = std::fabs(double(fastSin2pi(p)) - std::sin(2.0 * M_PI * double(p)));
		if (err > worst) {
			worst = err;
			worstP = p;
		}
	};
	sweep(-2.0, 2.0, 8000000, check);
	// Far from 0 (any p is accepted), and either side of each eighth cycle,
	// where the two series meet.
	sweep(997.0, 1003.0, 600000, check);
	for (int k = -8; k <= 8; k++)
		for (float d : {-1e-7f, 0.f, 1e-7f}) {
			check(0.125f * k + d);
		}
	CHECK(worst < 2e-7, "fastSin2pi absolute error %g at %g", worst, worstP);
	CHECK(fastSin2pi(0.f) == 0.f && fastSin2pi(0.25f) == 1.f && fastSin2pi(-0.25f) == -1.f && fastSin2pi(0.75f) == -1.f,
	      "fastSin2pi exact at 0 and the peaks: %.9g %.9g %.9g", fastSin2pi(0.f), fastSin2pi(0.25f), fastSin2pi(0.75f));
	std::printf("fastSin2pi: max absolute error %.3g\n", worst);
}

static void testTanh() {
	double worst = 0.0, worstX = 0.0;
	sweep(-20.0, 20.0, 8000000, [&](float x) {
		const double err = std::fabs(double(fastTanh(x)) - std::tanh(double(x)));
		if (err > worst) {
			worst = err;
			worstX = x;
		}
	});
	CHECK(worst < 1.5e-7, "fastTanh absolute error %g at %g", worst, worstX);
	CHECK(fastTanh(0.f) == 0.f, "fastTanh(0) is %g", fastTanh(0.f));
	CHECK(std::fabs(fastTanh(1e30f)) <= 1.f && std::fabs(fastTanh(-1e30f)) <= 1.f, "fastTanh stays within +/-1");
	std::printf("fastTanh: max absolute error %.3g\n", worst);
}

int main() {
	testExp2();
	testExp2D();
	testDbToGain();
	testSin();
	testTanh();
	return rkatest::finish("test_fast_math");
}

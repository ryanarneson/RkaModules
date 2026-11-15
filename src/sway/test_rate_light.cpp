// Unit tests for src/sway/RateLight.hpp. Run with `make test`.
#include "RateLight.hpp"
#include "testing.hpp"
#include <cmath>
#include <initializer_list>
#include <limits>

using rka::RateLight;

// Up to 10 Hz the light is on for the first half of each cycle and off for
// the second.
static void testBlinksUpToTenHz() {
	RateLight light;
	for (double hz : {0.1, 1.0, 10.0}) {
		for (double p : {0.0, 0.1, 0.25, 0.49, 0.4999})
			CHECK(light.process(p, hz) == 1.f, "%g Hz, phase %g: %g", hz, p, light.process(p, hz));
		for (double p : {0.5, 0.51, 0.75, 0.99, 0.999999999})
			CHECK(light.process(p, hz) == 0.f, "%g Hz, phase %g: %g", hz, p, light.process(p, hz));
	}
}

// Above 10 Hz a blink is no longer readable: steady at half brightness,
// whatever the phase.
static void testSteadyAboveTenHz() {
	RateLight light;
	for (double hz : {10.01, 100.0, 8000.0})
		for (double p : {0.0, 0.25, 0.5, 0.75, 0.99})
			CHECK(light.process(p, hz) == 0.5f, "%g Hz, phase %g: %g", hz, p, light.process(p, hz));
}

// A rate that is not a rate lights nothing.
static void testBadRateIsDark() {
	RateLight light;
	const double nan = std::numeric_limits<double>::quiet_NaN();
	for (double p : {0.0, 0.25, 0.75}) {
		CHECK(light.process(p, nan) == 0.f, "NaN Hz, phase %g: %g", p, light.process(p, nan));
		CHECK(light.process(p, -1.0) == 0.f, "-1 Hz, phase %g: %g", p, light.process(p, -1.0));
		CHECK(light.process(p, -100.0) == 0.f, "-100 Hz, phase %g: %g", p, light.process(p, -100.0));
	}
}

int main() {
	testBlinksUpToTenHz();
	testSteadyAboveTenHz();
	testBadRateIsDark();
	return rkatest::finish("test_rate_light");
}

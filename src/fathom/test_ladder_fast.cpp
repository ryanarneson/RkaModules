// Fathom's Ladder (the LadderCore template) against the frozen pre-template
// copy in testdata/ref/, built with the plugin's floating-point flags. A
// compiler is free there to order the two sources' sums differently, so
// they are held to rounding, not to the bit (test_ladder_shared.cpp holds
// them to the bit under strict floating point). A ladder near
// self-oscillation grows a last-bit difference without limit, so both
// start again from rest every 4096 samples. Run with `make test`.
#include "Ladder.hpp"
#include "testdata/ref/Ladder.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>

static const float FS = 48000.f;

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float next() {
		s = s * 1664525u + 1013904223u;
		return float(int32_t(s)) * (1.f / 2147483648.f);
	}
};

static void ladder_within_rounding() {
	const rka::LadderType types[4] = {rka::LadderType::LP24, rka::LadderType::LP12, rka::LadderType::BP,
	                                  rka::LadderType::HP};
	const rkaref::LadderType refTypes[4] = {rkaref::LadderType::LP24, rkaref::LadderType::LP12, rkaref::LadderType::BP,
	                                        rkaref::LadderType::HP};
	const float ks[3] = {0.f, 2.f, 4.1f};
	const float fcs[3] = {30.f, 1000.f, 12000.f};
	const float amps[3] = {0.f, 0.5f, 4.f};
	long different = 0, total = 0;
	double worst = 0.0, loudest = 0.0;
	for (int t = 0; t < 4; t++)
		for (float k : ks)
			for (float fc : fcs)
				for (float amp : amps) {
					rka::Ladder a;
					rkaref::Ladder b;
					a.setCoefficients(fc, k, types[t], FS);
					b.setCoefficients(fc, k, refTypes[t], FS);
					Lcg rng(17u + t);
					for (int i = 0; i < 2 * 48000; i++) {
						if (i % 4096 == 0) {
							a.reset();
							b.reset();
						}
						const float x = amp * rng.next();
						const float ya = a.process(x), yb = b.process(x);
						if (!(ya == yb))
							different++;
						const double d = std::fabs(double(ya) - yb) / (1.0 + std::fabs(double(yb)));
						if (!(d <= worst))   // NaN too
							worst = d;
						loudest = std::fmax(loudest, std::fabs(double(yb)));
						total++;
					}
				}
	std::printf("  template vs frozen ladder, plugin flags: %ld of %ld samples differ, worst relative %.3g\n", different, total,
	            worst);
	CHECK(loudest > 0.5, "the ladders were driven: loudest %g", loudest);
	CHECK(worst < 1e-5, "worst %g", worst);
}

int main() {
	ladder_within_rounding();
	return rkatest::finish("fathom/test_ladder_fast");
}

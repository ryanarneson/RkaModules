// The ladder and halfband as templates (LadderCore.hpp, Halfband.hpp,
// Float4.hpp): Fathom's Ladder must be bit-identical to the frozen copy in
// testdata/ref/ (which is the pre-template code, namespace rkaref), and the
// four-wide core must match four one-wide cores. Run with `make test`.
#include "Float4.hpp"
#include "Ladder.hpp"
#include "LadderCore.hpp"
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

static void ladder_bit_identical() {
	const rka::LadderType types[4] = {rka::LadderType::LP24, rka::LadderType::LP12, rka::LadderType::BP,
	                                  rka::LadderType::HP};
	const rkaref::LadderType refTypes[4] = {rkaref::LadderType::LP24, rkaref::LadderType::LP12, rkaref::LadderType::BP,
	                                        rkaref::LadderType::HP};
	const float ks[3] = {0.f, 2.f, 4.1f};
	const float fcs[3] = {30.f, 1000.f, 12000.f};
	const float amps[3] = {0.f, 0.5f, 4.f};   // 0: self-oscillation's start, from the noise term
	long different = 0, total = 0;
	for (int t = 0; t < 4; t++)
		for (float k : ks)
			for (float fc : fcs)
				for (float amp : amps) {
					rka::Ladder a;
					rkaref::Ladder b;
					a.reset();
					b.reset();
					a.setCoefficients(fc, k, types[t], FS);
					b.setCoefficients(fc, k, refTypes[t], FS);
					Lcg rng(17u + t);
					for (int i = 0; i < 2 * 48000; i++) {
						const float x = amp * rng.next();
						const float ya = a.process(x), yb = b.process(x);
						if (!(ya == yb))
							different++;
						total++;
					}
				}
	CHECK(total == 4L * 3 * 3 * 3 * 2 * 48000, "ran %ld samples", total);
	CHECK(different == 0, "%ld of %ld samples differ from the frozen ladder", different, total);
}

static void halfband_bit_identical() {
	rka::Upsampler2x ua;
	rkaref::Upsampler2x ub;
	rka::Downsampler2x da;
	rkaref::Downsampler2x db;
	Lcg rng(5);
	long different = 0;
	for (int i = 0; i < 48000; i++) {
		const float x = rng.next();
		float oa[2], ob[2];
		ua.process(x, oa);
		ub.process(x, ob);
		if (!(oa[0] == ob[0] && oa[1] == ob[1]))
			different++;
		if (!(da.process(oa) == db.process(ob)))
			different++;
	}
	CHECK(different == 0, "%ld samples differ from the frozen halfband", different);
}

// Each lane of a four-wide core against a one-wide core given that lane's
// input and settings. The two round differently in the last bit, and a
// ladder near self-oscillation (lanes 2 and 3) grows such a difference
// without limit, so both start again from rest every 4096 samples.
static void four_wide_equals_one_wide() {
	rka::LadderCore<rka::Float4> wide;
	rka::LadderCore<float> narrow[4];
	wide.reset();
	for (auto& n : narrow)
		n.reset();
	const float g[4] = {0.004f, 0.03f, 0.2f, 0.6f};
	const float k[4] = {0.f, 2.f, 4.f, 4.1f};
	const float comp[4] = {1.f, 3.f, 1.f, 5.1f};
	Lcg rng(99);
	double worst = 0.0;
	for (int i = 0; i < 96000; i++) {
		if (i % 4096 == 0) {
			wide.reset();
			for (auto& n : narrow)
				n.reset();
		}
		float x[4], nz[4];
		for (int l = 0; l < 4; l++) {
			x[l] = (l + 1) * 0.4f * rng.next();
			nz[l] = rng.next();
		}
		const rka::LadderNodes<rka::Float4> w = wide.step(rka::Float4{x[0], x[1], x[2], x[3]}, rka::Float4{g[0], g[1], g[2], g[3]},
		                                                  rka::Float4{k[0], k[1], k[2], k[3]},
		                                                  rka::Float4{comp[0], comp[1], comp[2], comp[3]},
		                                                  rka::Float4{nz[0], nz[1], nz[2], nz[3]});
		const rka::Float4 wl = rka::ladderLimit(w.y3);
		for (int l = 0; l < 4; l++) {
			const rka::LadderNodes<float> n = narrow[l].step(x[l], g[l], k[l], comp[l], nz[l]);
			const float pairs[6][2] = {{w.u[l], n.u},   {w.y0[l], n.y0}, {w.y1[l], n.y1},
			                           {w.y2[l], n.y2}, {w.y3[l], n.y3}, {wl[l], rka::ladderLimit(n.y3)}};
			for (auto& p : pairs) {
				const double d = std::fabs(double(p[0]) - p[1]) / (1.0 + std::fabs(double(p[1])));
				if (d > worst)
					worst = d;
			}
		}
	}
	std::printf("  four-wide vs one-wide: worst relative difference %.3g\n", worst);
	CHECK(worst < 1e-5, "worst %g", worst);
}

int main() {
	ladder_bit_identical();
	halfband_bit_identical();
	four_wide_equals_one_wide();
	return rkatest::finish("fathom/test_ladder_shared");
}

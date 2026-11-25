// Neap's tables, rate law, no-locking rule and tooltip text (NeapRate.hpp).
#include "neap/NeapRate.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

using namespace rka::neap;

bool near(double a, double want, double rel) {
	return std::fabs(a / want - 1.0) <= rel;
}

const float kNoon = 0.5f;

void noon_periods() {
	const float e1 = rateExponent(kNoon, 0.f, 0.f);
	CHECK(near(cycleHz(0, e1, 0.f), 0.625, 1e-4), "cycle 1 at noon is %g Hz", cycleHz(0, e1, 0.f));
	CHECK(near(cycleHz(7, e1, 0.f), 0.625 / 169.07242944, 1e-4), "cycle 8 at noon is %g Hz", cycleHz(7, e1, 0.f));
	CHECK(near(cycleHz(0, e1, 0.01f), 0.625 * 1.01, 1e-4), "drift scales the frequency");
}

void top_and_bottom() {
	CHECK(near(cycleHz(0, rateExponent(1.f, 0.f, 0.f), 0.f), 40.0, 1e-4), "rate 1 is not 40 Hz");
	CHECK(near(cycleHz(0, rateExponent(0.f, 0.f, 0.f), 0.f), 40.0 / 4096.0, 1e-4), "rate 0 is not 40/4096 Hz");
}

void cv_one_volt_per_octave() {
	const double base = cycleHz(0, rateExponent(kNoon, 1.f, 0.f), 0.f);
	CHECK(near(cycleHz(0, rateExponent(kNoon, 1.f, 1.f), 0.f), 2 * base, 1e-6), "+1 V does not double");
	CHECK(near(cycleHz(0, rateExponent(kNoon, -1.f, 1.f), 0.f), base / 2, 1e-6), "att -1, +1 V does not halve");
	CHECK(near(cycleHz(0, rateExponent(kNoon, 1.f, 1.f), 0.f), 1.25, 1e-6), "double of 0.625 Hz");
	const double top = cycleHz(0, rateExponent(1.f, 1.f, 100.f), 0.f);
	CHECK(near(top, 160.0, 1e-6), "top clamp is %g Hz (want 160)", top);
	CHECK(near(cycleHz(0, rateExponent(1.f, 1.f, 3.f), 0.f), 160.0, 1e-6), "2 octaves past the top");
	const double bot = cycleHz(0, rateExponent(0.f, 1.f, -100.f), 0.f);
	CHECK(near(bot, 40.0 / 16384.0, 1e-6), "bottom clamp is %g Hz", bot);
	const float nanf = std::nanf(""), inf = INFINITY;
	CHECK(rateExponent(kNoon, 1.f, nanf) == rateExponent(kNoon, 1.f, 0.f), "NaN CV is the knob alone");
	CHECK(rateExponent(kNoon, 1.f, inf) == rateExponent(1.f, 1.f, 100.f), "+inf CV is the top clamp");
	CHECK(rateExponent(kNoon, 1.f, -inf) == rateExponent(0.f, 1.f, -100.f), "-inf CV is the bottom clamp");
	CHECK(std::isfinite(rateExponent(kNoon, nanf, 5.f)) && std::isfinite(rateExponent(nanf, 1.f, 5.f)), "NaN knob or att finite");
}

void spread_zero_at_noon_and_for_cycle_1() {
	const float want[kCycles - 1] = {2.16f, 1.93f, 2.25f, 1.93f, 1.93f, 2.23f, 2.17f};
	double sum = 0;
	for (int i = 0; i < kCycles - 1; i++) {
		CHECK(kSteps[i] == want[i], "step %d is %g", i + 1, kSteps[i]);
		sum += std::log2(double(kSteps[i]));
		CHECK(std::fabs(cycleOffset(i + 1) - sum) < 1e-5, "L_%d is %g, steps give %g", i + 2, cycleOffset(i + 1), sum);
	}
	CHECK(cycleOffset(0) == 0.f, "L_1 is not 0");
	CHECK(kSlopes[0] == 0.f, "s_1 is not 0");
	const float en = rateExponent(kNoon, 0.f, 0.f);
	for (int i = 0; i < kCycles; i++)
		CHECK(std::fabs(cycleExponent(i, en) - (en - cycleOffset(i))) < 1e-6f, "spread at noon, cycle %d", i + 1);
	for (float e1 = en - 8.f; e1 <= en + 8.f; e1 += 1.f)
		CHECK(cycleExponent(0, e1) == e1, "cycle 1 moved at e1 = %g", e1);
	CHECK(cycleExponent(7, en + 6.f) > en + 6.f - cycleOffset(7), "positive slope shifts cycle 8 up above noon");
	CHECK(std::fabs(cycleOffset(7) - std::log2(169.07242944)) < 1e-5f, "L_8 is the step product");
}

void order_kept() {
	const float en = rateExponent(kNoon, 0.f, 0.f);
	for (int k = 0; k <= 80; k++) {
		const float e1 = en - 8.f + 16.f * k / 80.f;
		for (int i = 0; i + 1 < kCycles; i++)
			CHECK(cycleHz(i, e1, 0.f) > cycleHz(i + 1, e1, 0.f), "order broken at e1 = %g, cycle %d", e1, i + 1);
	}
}

void no_locking() {
	CHECK(lockMargin(2.5) < 1e-12 && lockMargin(2.0) < 1e-12 && lockMargin(5.5) < 1e-12, "locks are not found");
	CHECK(std::fabs(lockMargin(2.52) - 0.02 / 2.52) < 1e-12, "half-integer margin of 2.52 is %g", lockMargin(2.52));
	CHECK(std::fabs(lockMargin(7.5) - 0.5 / 7.5) < 1e-12, "above 6 only integers count: %g", lockMargin(7.5));
	const float en = rateExponent(kNoon, 0.f, 0.f);
	float worstAll = 1.f;
	for (int k = 0; k <= 80; k++) {
		const float e1 = en - 8.f + 16.f * k / 80.f;
		float worst = 0.f;
		const bool ok = ratiosClearOfLocks(e1, &worst);
		CHECK(ok && worst >= 0.03f, "locking at e1 = %g, worst margin %g", e1, worst);
		if (worst < worstAll) worstAll = worst;
	}
	std::printf("no_locking: worst margin over the range %.4f\n", worstAll);
}

void tooltip_text() {
	char b[64];
	rateTooltip(0.5f, 0.f, 0.f, b, sizeof b);
	CHECK(!std::strcmp(b, "1: 1.6 s \xC2\xB7 8: 4.5 min"), "noon tooltip is '%s'", b);
	rateTooltip(1.f, 0.f, 0.f, b, sizeof b);
	CHECK(!std::strcmp(b, "1: 25.0 ms \xC2\xB7 8: 4.2 s"), "top tooltip is '%s'", b);
	rateTooltip(0.f, 0.f, 0.f, b, sizeof b);
	CHECK(!std::strcmp(b, "1: 1.7 min \xC2\xB7 8: 4.8 h"), "bottom tooltip is '%s'", b);
	rateTooltip(0.5f, 0.f, std::nanf(""), b, sizeof b);
	CHECK(!std::strcmp(b, "1: 1.6 s \xC2\xB7 8: 4.5 min"), "NaN CV tooltip is '%s'", b);
	rateTooltip(0.5f, 0.f, 0.f, b, 8);
	CHECK(std::strlen(b) < 8, "tooltip overran its buffer");

	formatPeriod(0.85, b, sizeof b);   CHECK(!std::strcmp(b, "850 ms"), "'%s'", b);
	formatPeriod(0.0425, b, sizeof b); CHECK(!std::strcmp(b, "42.5 ms"), "'%s'", b);
	formatPeriod(0.99996, b, sizeof b); CHECK(!std::strcmp(b, "1.0 s"), "rolls over to seconds: '%s'", b);
	formatPeriod(59.99, b, sizeof b);  CHECK(!std::strcmp(b, "1.0 min"), "rolls over to minutes: '%s'", b);
	formatPeriod(25.0, b, sizeof b);   CHECK(!std::strcmp(b, "25.0 s"), "'%s'", b);
	formatPeriod(3599.0, b, sizeof b); CHECK(!std::strcmp(b, "1.0 h"), "rolls over to hours: '%s'", b);
	formatPeriod(17280.0, b, sizeof b); CHECK(!std::strcmp(b, "4.8 h"), "'%s'", b);
	formatPeriod(70000.0, b, sizeof b); CHECK(!std::strcmp(b, "19.4 h"), "'%s'", b);
	formatPeriod(std::nan(""), b, sizeof b); CHECK(b[0] != 0, "NaN period gives text");
}

} // namespace

int main() {
	noon_periods();
	top_and_bottom();
	cv_one_volt_per_octave();
	spread_zero_at_noon_and_for_cycle_1();
	order_kept();
	no_locking();
	tooltip_text();
	return rkatest::finish("neap/test_rate");
}

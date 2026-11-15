// CPU cost of shoal::Core per app: 10 s of firmware time (166,667 ticks) over
// random input, timed on the host clock. Prints microseconds per tick and the
// share of one core needed to run in real time (a tick is 60 us); exits
// non-zero if any app needs more than 2 %. Build with -O2, no sanitizers
// (`make shoal-bench`).
#include <chrono>
#include <cstdio>

#include "shoal/Core.hpp"

namespace {

struct Lcg {
	uint32_t state = 12345;
	uint32_t next() { return state = state * 1664525u + 1013904223u; }
	float volts(float lo, float hi) { return lo + (hi - lo) * (float) (next() >> 8) / (float) 0xffffff; }
};

constexpr long kTicks = 166667;
constexpr double kTickUs = 60.0;
constexpr double kLimitPercent = 2.0;

} // namespace

int main() {
	bool ok = true;
	for (int app = 0; app < shoal::kNumApps; app++) {
		shoal::Core c;
		c.selectApp(app);
		Lcg rng;
		rng.state = 777u + (uint32_t) app;
		// Same input shape as the fault sweeps: a new CV every 1..200 ticks, a
		// gate toggle every 1..400 ticks.
		int cvLeft[shoal::kNumChannels] = {}, gateLeft[shoal::kNumChannels] = {};
		bool gate[shoal::kNumChannels] = {};
		const auto t0 = std::chrono::steady_clock::now();
		for (long i = 0; i < kTicks; i++) {
			for (int ch = 0; ch < shoal::kNumChannels; ch++) {
				if (--cvLeft[ch] <= 0) {
					c.setCv(ch, rng.volts(-6.f, 8.f));
					cvLeft[ch] = 1 + (int) (rng.next() % 200);
				}
				if (--gateLeft[ch] <= 0) {
					gate[ch] = !gate[ch];
					c.setGate(ch, gate[ch] ? 10.f : 0.f);
					gateLeft[ch] = 1 + (int) (rng.next() % 400);
				}
			}
			c.tick();
		}
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		const double perTick = us / kTicks;
		const double percent = 100.0 * perTick / kTickUs;
		const bool over = percent > kLimitPercent;
		ok = ok && !over;
		std::printf("%-20s %7.3f us/tick  %5.2f %% of a core%s\n", shoal::Core::appName(app), perTick, percent,
			over ? "  OVER 2 %" : "");
	}
	return ok ? 0 : 1;
}

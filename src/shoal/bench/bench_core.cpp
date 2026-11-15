// CPU cost of shoal::Core per app: 10 s of firmware time (166,667 ticks) over
// random input, with both screens drawn (drawMenu + drawScope) at 60 Hz and
// the state saved at 2 Hz, as the Rack module will. Timed on the host clock.
// Prints microseconds per tick and, as shares of one core needed to run in
// real time (a tick is 60 us), the ticks, the drawing, the saves and their
// total; exits non-zero if any app's total is over 2 %. Build with -O2, no
// sanitizers (`make shoal-bench`).
#include <chrono>
#include <cstdint>
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
constexpr double kDrawHz = 60.0, kSaveHz = 2.0;

using Clock = std::chrono::steady_clock;

double usSince(Clock::time_point t0) {
	return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

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
		uint8_t frame[shoal::kFrameBytes];
		uint8_t image[shoal::kStateBytes];
		double tickUs = 0.0, drawUs = 0.0, saveUs = 0.0;
		double drawPhase = 0.0, savePhase = 0.0;
		long draws = 0, saves = 0;
		const Clock::time_point start = Clock::now();
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
			drawPhase += kDrawHz / shoal::kTickHz;
			if (drawPhase >= 1.0) {
				drawPhase -= 1.0;
				const Clock::time_point t0 = Clock::now();
				c.drawMenu(frame);
				c.drawScope(frame);
				drawUs += usSince(t0);
				draws++;
			}
			savePhase += kSaveHz / shoal::kTickHz;
			if (savePhase >= 1.0) {
				savePhase -= 1.0;
				const Clock::time_point t0 = Clock::now();
				c.save(image);
				saveUs += usSince(t0);
				saves++;
			}
		}
		// The ticks (with the input changes) are the rest of the loop's time.
		tickUs = usSince(start) - drawUs - saveUs;
		// Shares of one core: time spent per unit of firmware time.
		const double runUs = kTicks * kTickUs;
		const double tickPct = 100.0 * tickUs / runUs, drawPct = 100.0 * drawUs / runUs;
		const double savePct = 100.0 * saveUs / runUs, total = tickPct + drawPct + savePct;
		const bool over = total > kLimitPercent;
		ok = ok && !over;
		std::printf("%-20s %6.3f us/tick %6.2f us/draw %6.2f us/save   ticks %5.3f %% + draw %5.3f %% + save %5.3f %%"
			" = %5.3f %% of a core%s\n", shoal::Core::appName(app), tickUs / kTicks, drawUs / draws, saveUs / saves,
			tickPct, drawPct, savePct, total, over ? "  OVER 2 %" : "");
	}
	return ok ? 0 : 1;
}

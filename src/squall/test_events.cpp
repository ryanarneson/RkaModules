// Squall's event process: Weather's rate law, Gust's self-exciting bursts,
// the clocked chance per tick, and long gaps (spec: Weather, Gust).
#include "testing.hpp"
#include "FastMath.hpp"
#include "squall/Events.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace rka;
using namespace rka::squall;

// Free-running: run until `events` events (or `maxSeconds`), returning the
// gaps in seconds. Every event excites Gust, as the engine does.
static std::vector<double> runFree(float w, float g, float fs, int events, uint32_t seed, double maxSeconds = 1e9) {
	Events e;
	e.seed(seed);
	e.setSampleRate(fs);
	e.setWeather(w);
	e.setGust(g);
	std::vector<double> gaps;
	double last = 0.0;
	const double dt = 1.0 / fs;
	long long n = 0;
	while ((int) gaps.size() < events && n * dt < maxSeconds) {
		n++;
		if (e.stepFree()) {
			e.excite();
			const double t = n * dt;
			gaps.push_back(t - last);
			last = t;
		}
	}
	return gaps;
}

static double mean(const std::vector<double>& v) {
	double s = 0.0;
	for (double x : v)
		s += x;
	return s / v.size();
}

static double cov(const std::vector<double>& v) {
	const double m = mean(v);
	double s = 0.0;
	for (double x : v)
		s += (x - m) * (x - m);
	return std::sqrt(s / v.size()) / m;
}

static void fast_log2_accuracy() {
	double worst = 0.0;
	for (int i = 0; i <= 10000; i++) {
		const float x = (float) std::pow(10.0, -9.0 + 9.0 * i / 10000.0);
		const double err = std::fabs((double) fastLog2(x) - std::log2((double) x));
		worst = std::max(worst, err);
	}
	CHECK(worst < 1e-3, "worst abs error %g", worst);
	CHECK(fastLog2(1.f) == 0.f || std::fabs(fastLog2(1.f)) < 1e-6f, "log2(1) = %g", fastLog2(1.f));
}

static void weather_rate_law() {
	for (float w : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		const double want = (1.0 / 600.0) * std::pow(4800.0, (double) w);
		const double got = weatherRate(w);
		CHECK(std::fabs(got / want - 1.0) < 1e-4, "rate(%g) = %g, want %g", w, got, want);
	}
	CHECK(std::fabs(weatherRate(1.f) - 8.0) < 1e-3, "rate(1) = %g", weatherRate(1.f));
	for (float w : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		const double want = std::pow(256.0, (double) w - 1.0);
		CHECK(std::fabs(tickChance(w) / want - 1.0) < 1e-4, "p(%g) = %g, want %g", w, tickChance(w), want);
	}
}

static void free_rate_at_each_word() {
	for (float w : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		const float fs = w >= 1.f ? 2000.f : 200.f;
		const std::vector<double> gaps = runFree(w, 0.f, fs, 2500, 11u);
		const double rate = 1.0 / mean(gaps);
		const double want = weatherRate(w);
		CHECK(std::fabs(rate / want - 1.0) < 0.10, "w %g: rate %g, want %g", w, rate, want);
	}
}

static void gust_keeps_the_amount() {
	const double want = weatherRate(0.5f);
	for (float g : {0.f, 0.5f, 1.f}) {
		const std::vector<double> gaps = runFree(0.5f, g, 200.f, 40000, 23u);
		const double rate = 1.0 / mean(gaps);
		CHECK(std::fabs(rate / want - 1.0) < 0.12, "gust %g: rate %g, want %g", g, rate, want);
	}
}

static void gust_clusters() {
	const double c0 = cov(runFree(0.5f, 0.f, 200.f, 5000, 31u));
	const double c1 = cov(runFree(0.5f, 1.f, 200.f, 5000, 31u));
	CHECK(std::fabs(c0 - 1.0) < 0.1, "gust 0: CV of gaps %g, want 1", c0);
	CHECK(c1 > 1.5, "gust 1: CV of gaps %g, want > 1.5", c1);
}

static void no_pulse_at_gust_0() {
	std::vector<double> gaps = runFree(0.5f, 0.f, 200.f, 5000, 41u);
	const double rate = weatherRate(0.5f);
	// Lag autocorrelations of the gap sequence.
	const double m = mean(gaps);
	double var = 0.0;
	for (double x : gaps)
		var += (x - m) * (x - m);
	for (int lag = 1; lag <= 10; lag++) {
		double c = 0.0;
		for (size_t i = lag; i < gaps.size(); i++)
			c += (gaps[i] - m) * (gaps[i - lag] - m);
		CHECK(std::fabs(c / var) < 0.05, "lag %d autocorrelation %g", lag, c / var);
	}
	// Kolmogorov-Smirnov against the exponential.
	std::sort(gaps.begin(), gaps.end());
	const double n = (double) gaps.size();
	double d = 0.0;
	for (size_t i = 0; i < gaps.size(); i++) {
		const double f = 1.0 - std::exp(-rate * gaps[i]);
		d = std::max(d, std::max(std::fabs(f - i / n), std::fabs(f - (i + 1) / n)));
	}
	CHECK(d < 1.63 / std::sqrt(n), "KS D %g, bound %g", d, 1.63 / std::sqrt(n));
}

static void clocked_chance_per_tick() {
	for (float w : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		Events e;
		e.seed(5u);
		e.setSampleRate(48000.f);
		e.setWeather(w);
		e.setGust(0.f);
		const int ticks = 400000;
		int fired = 0;
		for (int i = 0; i < ticks; i++)
			if (e.tick()) {
				e.excite();
				fired++;
			}
		const double share = (double) fired / ticks;
		CHECK(std::fabs(share / tickChance(w) - 1.0) < 0.10, "w %g: share %g, want %g", w, share, tickChance(w));
		if (w == 1.f)
			CHECK(fired == ticks, "Stormy fired %d of %d ticks", fired, ticks);
	}
}

static void long_gaps_at_calm() {
	// At 768 kHz a slow rate adds about 1e-8 per sample to the wait: a float
	// accumulator stalls (the step is under half an ulp) and never fires.
	// Fair (one per 72 s) for 10 simulated minutes expects 8.3 events.
	Events e;
	e.seed(3u);
	e.setSampleRate(768000.f);
	e.setWeather(0.25f);
	e.setGust(0.f);
	int count = 0;
	const long long n = 600LL * 768000LL;
	for (long long i = 0; i < n; i++)
		if (e.stepFree()) {
			e.excite();
			count++;
		}
	CHECK(count >= 3 && count <= 16, "10 min at Fair, 768 kHz: %d events, want about 8", count);
}

static void seeds_repeat_and_differ() {
	const std::vector<double> a = runFree(0.75f, 0.5f, 1000.f, 200, 7u);
	const std::vector<double> b = runFree(0.75f, 0.5f, 1000.f, 200, 7u);
	const std::vector<double> c = runFree(0.75f, 0.5f, 1000.f, 200, 8u);
	CHECK(a == b, "one seed must repeat exactly");
	CHECK(a != c, "different seeds must differ");
}

static void bad_rate_ignored() {
	Events e;
	e.seed(1u);
	e.setSampleRate(1000.f);
	for (float bad : {0.f, -1.f, NAN, INFINITY, 1e9f})
		e.setSampleRate(bad);
	e.setWeather(1.f);
	e.setGust(0.f);
	int fired = 0;
	for (int i = 0; i < 100000; i++)
		if (e.stepFree())
			fired++;
	// 100 s at 1 kHz at 8/s: about 800.
	CHECK(fired > 600 && fired < 1000, "after bad rates: %d events in 100 s at Stormy", fired);
}

int main() {
	fast_log2_accuracy();
	weather_rate_law();
	free_rate_at_each_word();
	gust_keeps_the_amount();
	gust_clusters();
	no_pulse_at_gust_0();
	clocked_chance_per_tick();
	long_gaps_at_calm();
	seeds_repeat_and_differ();
	bad_rate_ignored();
	return rkatest::finish("squall test_events");
}

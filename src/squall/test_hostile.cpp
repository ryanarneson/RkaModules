// Squall under hostile conditions (plan Task 6), built with the address and
// undefined-behaviour sanitizers and a counting operator new: extreme and
// changing sample rates, NaN and inf on every input, no allocation in
// process(), seeded fuzz of everything, and denormal-free decaying state.
#include "testing.hpp"
#include "squall/SquallEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>

static thread_local long tAllocs = 0;

void* operator new(std::size_t n) {
	tAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}

typedef rka::SquallFrame F;

namespace {

struct Rig {
	rka::SquallEngine e;
	F f;
	float cvv[3][16] = {};
	rka::SquallResult r;
	explicit Rig(uint32_t seed = 1) : e(seed) {
		for (int i = 0; i < 3; i++)
			f.cv[i].v = cvv[i];
	}
};

const float LO[4] = {-5.f, 0.f, -2.f, -1.f}, HI[4] = {5.f, 10.f, 2.f, 1.f};

bool ok(const rka::SquallResult& r, int range) {
	const int k = (range >= 0 && range < 4) ? range : 0;
	return r.step >= LO[k] - 1e-4f && r.step <= HI[k] + 1e-4f && r.swell >= -10.f && r.swell <= 10.f &&
	       (r.gate == 0.f || r.gate == 10.f);
}

// Events per second at the current settings, over `seconds`.
double measuredRate(Rig& g, float fs, double seconds) {
	long long before = g.e.debug().events;
	const long long n = (long long) (seconds * fs);
	for (long long i = 0; i < n; i++)
		g.e.process(g.f, g.r);
	return (g.e.debug().events - before) / seconds;
}

void extreme_rates() {
	for (float fs : {1.f, 10.f, 1000.f, 44100.f, 192000.f, 768000.f}) {
		Rig g(3);
		g.e.setSampleRate(fs);
		g.f.params[F::WEATHER] = 1.f;
		g.f.params[F::GUST] = 1.f;
		g.f.params[F::MEMORY] = 0.5f;
		bool good = true;
		const long n = fs < 1000.f ? 2000 : 200000;
		for (long i = 0; i < n; i++) {
			g.e.process(g.f, g.r);
			good = good && ok(g.r, 0) && std::isfinite(g.r.swell);
		}
		CHECK(good, "at %g Hz the outputs must stay finite and in range", fs);
	}
}

void rate_change_recovers() {
	Rig g(4);
	g.f.params[F::WEATHER] = 1.f;
	g.f.params[F::GUST] = 0.f;
	for (float fs : {44100.f, 768000.f, 48000.f}) {
		g.e.setSampleRate(fs);
		for (int i = 0; i < 20000; i++)
			g.e.process(g.f, g.r);
	}
	const double rate = measuredRate(g, 48000.f, 300.0);
	CHECK(std::fabs(rate / 8.0 - 1.0) < 0.1, "after 44.1k -> 768k -> 48k: %g events/s at Stormy, want 8", rate);
}

void hostile_inputs() {
	for (float bad : {NAN, INFINITY, -INFINITY}) {
		Rig g(5);
		g.e.setSampleRate(48000.f);
		for (int c = 0; c < 3; c++) {
			g.f.params[F::WEATHER_ATT + c] = 1.f;
			g.cvv[c][0] = bad;
			g.f.cv[c].channels = 1;
		}
		g.f.clock = bad;
		g.f.freeze = bad;
		g.f.clockPatched = true;
		bool good = true;
		for (int i = 0; i < 100000; i++) {
			g.e.process(g.f, g.r);
			good = good && ok(g.r, 0) && std::isfinite(g.r.swell);
		}
		g.f.clockPatched = false;
		for (int i = 0; i < 100000; i++) {
			g.e.process(g.f, g.r);
			good = good && ok(g.r, 0) && std::isfinite(g.r.swell);
		}
		CHECK(good, "%g on every CV, Clock and Freeze must keep the outputs finite and in range", bad);
	}
}

void no_allocation_in_process() {
	Rig g(6);
	g.e.setSampleRate(48000.f);
	for (int c = 0; c < 3; c++) {
		g.f.params[F::WEATHER_ATT + c] = 1.f;
		g.f.cv[c].channels = 1;
	}
	g.f.params[F::MEMORY] = 0.7f;
	const long before = tAllocs;
	for (int n = 0; n < 400000; n++) {
		for (int c = 0; c < 3; c++)
			g.cvv[c][0] = (n / 480) % 2 ? 4.f : -4.f;
		g.f.clockPatched = (n / 100000) % 2;
		g.f.clock = (n / 240) % 2 ? 10.f : 0.f;
		g.f.freeze = (n / 70000) % 3 == 2 ? 10.f : 0.f;
		g.f.range = (n / 50000) % 4;
		g.f.loopLength = 4 << ((n / 60000) % 3);
		g.e.process(g.f, g.r);
	}
	rka::SquallState s;
	g.e.getState(s);
	g.e.setState(s);
	g.e.process(g.f, g.r);
	CHECK(tAllocs == before, "%ld allocations in process() or the state calls", tAllocs - before);
}

void seeded_fuzz() {
	rka::hadal::Random rng;
	rng.seed(2026);
	Rig g(9);
	bool good = true;
	for (int n = 0; n < 1000000; n++) {
		if (n % 4096 == 0) {
			for (int p = 0; p < F::PARAMS_LEN; p++)
				g.f.params[p] = p >= F::WEATHER_ATT ? rng.uniform() : rng.unit();
			for (int c = 0; c < 3; c++)
				g.f.cv[c].channels = (int) (rng.unit() * 3.f);
			g.f.clockPatched = rng.unit() < 0.5f;
			g.f.range = (int) (rng.unit() * 4.f);
			g.f.loopLength = (int) (rng.unit() * 20.f);
			if (n % 65536 == 0) {
				const float rates[5] = {44100.f, 48000.f, 96000.f, 192000.f, 22050.f};
				g.e.setSampleRate(rates[rng.next() % 5]);
			}
		}
		for (int c = 0; c < 3; c++)
			for (int k = 0; k < 2; k++)
				g.cvv[c][k] = rng.uniform() * 12.f;
		g.f.clock = rng.unit() < 0.3f ? 10.f : 0.f;
		g.f.freeze = rng.unit() < 0.01f ? 10.f : (rng.unit() < 0.5f ? g.f.freeze : 0.f);
		g.e.process(g.f, g.r);
		good = good && ok(g.r, g.f.range) && std::isfinite(g.r.swell);
	}
	CHECK(good, "fuzzed knobs, CVs, clock, Freeze, menus and rates must keep every output finite and in range");
}

// Gust's excitation, decaying after a burst, is exactly 0 or normal at every
// step, never subnormal (Stormy decays fastest: tau = K / 8 s).
void denormals() {
	rka::squall::Events ev;
	ev.seed(4);
	ev.setSampleRate(768000.f);
	ev.setWeather(1.f);
	ev.setGust(1.f);
	for (int i = 0; i < 50; i++)
		ev.excite();
	bool normal = true;
	double worst = 0.0;
	for (long i = 0; i < 60L * 768000L; i++) {
		ev.stepFree(false);
		const double x = ev.excitation();
		if (!(x == 0.0 || std::fabs(x) > 2.2250738585072014e-308)) {
			normal = false;
			worst = x;
		}
	}
	CHECK(normal, "Gust's excitation passes through subnormals (%g)", worst);
	bool normalT = true;
	for (int i = 0; i < 5000; i++) {
		ev.tick(false);
		const float xt = ev.tickExcitation();
		normalT = normalT && (xt == 0.f || std::fabs(xt) > 1.17549435e-38f);
	}
	CHECK(normalT, "Gust's tick excitation passes through subnormals");
}

} // namespace

int main() {
	extreme_rates();
	rate_change_recovers();
	hostile_inputs();
	no_allocation_in_process();
	seeded_fuzz();
	denormals();
	return rkatest::finish("squall test_hostile");
}

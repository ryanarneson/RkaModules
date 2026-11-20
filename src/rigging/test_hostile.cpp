// Rigging under hostile conditions, built with the address and
// undefined-behaviour sanitizers and a counting operator new: extreme and
// changing sample rates, NaN and inf on every input, a loud input at a
// string's own pitch with Ring at its longest, triggers at audio rate, no
// allocation in process(), seeded fuzz of everything, and strings that
// decay to exact silence.
#include "testing.hpp"
#include "rigging/RiggingEngine.hpp"
#include "hadal/Random.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>
#include <vector>

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

typedef rka::RiggingFrame F;

namespace {

struct Rig {
	rka::RiggingEngine e;
	F f;
	rka::RiggingResult r;
	explicit Rig(uint32_t seed = 1) : e(seed) {}
	// Finite, and within 10 V for the strings plus whatever dry input Mix
	// lets through (the dry path is the input, untouched).
	bool step() {
		e.process(f, r);
		const float inL = f.inLPatched ? f.inL : f.inRPatched ? f.inR : 0.f, inR = f.inRPatched ? f.inR : inL;
		const float dryL = std::isfinite(inL) ? std::fabs(inL) : 0.f, dryR = std::isfinite(inR) ? std::fabs(inR) : 0.f;
		const float mix = f.params[F::MIX];
		const bool allWet = mix >= 1.f;   // Mix fully right: the strings only, once the knob's smoothing is done
		wet = allWet ? wet + 1 : 0;
		const float allowL = wet > 4800 ? 10.f : 10.f + dryL * 1.0001f, allowR = wet > 4800 ? 10.f : 10.f + dryR * 1.0001f;
		return std::isfinite(r.l) && std::isfinite(r.r) && std::fabs(r.l) <= allowL && std::fabs(r.r) <= allowR;
	}
	long wet = 0;
};

void extreme_rates() {
	for (float fs : {1.f, 10.f, 1000.f, 44100.f, 192000.f, 768000.f}) {
		Rig g(3);
		g.e.setSampleRate(fs);
		g.f.inLPatched = true;
		g.f.strumPatched = true;
		rka::hadal::Random rng;
		rng.seed(1);
		bool good = true;
		const long n = fs < 1000.f ? 2000 : 200000;
		for (long i = 0; i < n; i++) {
			g.f.inL = 5.f * rng.uniform();
			g.f.strum = (i / 500) % 2 ? 10.f : 0.f;
			good = good && g.step();
		}
		CHECK(good, "at %g Hz the outputs must stay finite and within 10 V", fs);
	}
	// Rates the engine refuses leave it as it was.
	Rig g;
	g.e.setSampleRate(0.f);
	g.e.setSampleRate(-5.f);
	g.e.setSampleRate(NAN);
	g.e.setSampleRate(1e9f);
	g.f.strumPatched = true;
	g.f.strum = 10.f;
	bool good = true;
	for (int i = 0; i < 48000; i++)
		good = good && g.step();
	CHECK(good && std::fabs(g.e.stringHz(0) - 65.40639f) < 0.01f, "a bad sample rate upset the engine (string 1 at %g Hz)", g.e.stringHz(0));
}

// The pitch of string 1 alone, from the lag at which half a second of the
// output best matches itself (an independent path: no Fourier sum, nothing
// of the engine's). Lags from 40 to 100 Hz, refined between samples.
double pitchByAutocorrelation(Rig& g, float fs) {
	std::vector<float> x((size_t) (fs * 0.5f));
	for (float& v : x) {
		g.step();
		v = g.r.l;
	}
	const int lo = (int) (fs / 100.f), hi = (int) (fs / 40.f);
	std::vector<double> c((size_t) hi + 2, 0.0);
	const size_t n = x.size() - (size_t) hi - 2;
	for (int lag = lo - 1; lag <= hi + 1; lag++)
		for (size_t i = 0; i < n; i++)
			c[(size_t) lag] += (double) x[i] * x[i + (size_t) lag];
	int best = lo;
	for (int lag = lo; lag <= hi; lag++)
		if (c[(size_t) lag] > c[(size_t) best])
			best = lag;
	const double a = c[(size_t) best - 1], b = c[(size_t) best], d = c[(size_t) best + 1];
	const double shift = (a - 2.0 * b + d) != 0.0 ? 0.5 * (a - d) / (a - 2.0 * b + d) : 0.0;
	return fs / (best + shift);
}

void rate_change_recovers() {
	Rig g(4);
	g.f.inLPatched = true;
	for (int j = 1; j < 6; j++)
		g.f.params[F::LEVEL_1 + j] = 0.f;
	g.f.params[F::DAMP] = 0.6f;
	g.f.params[F::RING] = 0.8f;
	rka::hadal::Random rng;
	rng.seed(2);
	bool good = true;
	for (float fs : {44100.f, 768000.f, 48000.f}) {
		g.e.setSampleRate(fs);
		for (int i = 0; i < 30000; i++) {
			g.f.inL = 5.f * rng.uniform();
			good = good && g.step();
		}
	}
	// Input off; strike, and measure from 50 ms on (the burst is over).
	g.f.inL = 0.f;
	g.f.strumPatched = true;
	g.f.strum = 10.f;
	for (int i = 0; i < 2400; i++)
		good = good && g.step();
	const double hz = pitchByAutocorrelation(g, 48000.f);
	CHECK(good, "outputs left 10 V or went non-finite across 44.1k -> 768k -> 48k");
	CHECK(std::fabs(1200.0 * std::log2(hz / 65.40639)) < 10.0, "after 44.1k -> 768k -> 48k string 1 is at %g Hz, want 65.41", hz);
}

void hostile_inputs() {
	for (float bad : {NAN, INFINITY, -INFINITY, 1e30f, -1e30f}) {
		Rig g(5);
		g.e.setSampleRate(48000.f);
		g.f.inLPatched = g.f.inRPatched = true;
		bool good = true;
		for (int i = 0; i < 100000; i++) {
			g.f.inL = g.f.inR = g.f.voct = g.f.chordCv = g.f.ringCv = g.f.dampCv = bad;
			g.f.strum = i % 7 == 0 ? bad : 0.f;
			g.f.strumPatched = (i / 20000) % 2 == 0;
			for (int p = 0; p < F::PARAMS_LEN; p++)
				g.f.params[p] = i > 50000 ? bad : g.f.params[p];
			good = good && g.step();
		}
		// And it comes back.
		g.f = F();
		g.f.strumPatched = true;
		g.f.strum = 10.f;
		for (int i = 0; i < 48000; i++)
			good = good && g.step();
		CHECK(good, "%g on every input and knob must keep the outputs finite and within 10 V", bad);
		CHECK(std::fabs(g.e.stringHz(0) - 65.40639f) < 0.01f, "after %g the engine does not come back (string 1 at %g Hz)", bad, g.e.stringHz(0));
	}
}

// A +-10 V square at string 1's pitch, Ring 60 s, for a minute: under 10 V
// all the way, and gone after the input stops as Ring says.
void tuned_square_at_60s() {
	Rig g(6);
	g.f.inLPatched = true;
	g.f.params[F::RING] = 1.f;
	g.f.params[F::DAMP] = 0.f;
	bool good = true;
	float peak = 0.f;
	double phase = 0.0;
	for (long i = 0; i < 60L * 48000; i++) {
		phase += 65.40639 / 48000.0;
		phase -= std::floor(phase);
		g.f.inL = phase < 0.5 ? 10.f : -10.f;
		good = good && g.step();
		peak = std::max(peak, std::max(std::fabs(g.r.l), std::fabs(g.r.r)));
	}
	CHECK(good, "a tuned +-10 V square for a minute: the output left 10 V or went non-finite");
	CHECK(peak > 8.f, "the square never reached the soft limit (peak %g V): the test proves nothing", peak);
	// Input off, Ring still 60 s: the output is back under the soft limit's
	// knee within 10 s (the input's share and the strings' bound keep a
	// tuned input from piling up far above it), and still ringing.
	g.f.inL = 0.f;
	for (long i = 0; i < 10L * 48000; i++)
		good = good && g.step();
	float after = 0.f;
	for (int i = 0; i < 48000; i++) {
		g.step();
		after = std::max(after, std::max(std::fabs(g.r.l), std::fabs(g.r.r)));
	}
	CHECK(after < 8.f && after > 0.5f, "10 s after the square stopped (Ring 60 s) the output peaks at %g V", after);
	// Ring down to 1 s: silence within 20 s.
	g.f.params[F::RING] = 0.39f;
	for (long i = 0; i < 20L * 48000; i++)
		good = good && g.step();
	float tail = 0.f;
	for (int i = 0; i < 48000; i++) {
		g.step();
		tail = std::max(tail, std::max(std::fabs(g.r.l), std::fabs(g.r.r)));
	}
	CHECK(good && tail == 0.f, "20 s after the square stopped (Ring 1 s) the output peaks at %g V", tail);
}

void strum_at_audio_rate() {
	for (int period : {48, 5, 2}) {
		Rig g(7);
		g.f.strumPatched = true;
		g.f.params[F::SPREAD] = 0.5f;
		bool good = true;
		for (long i = 0; i < 10L * 48000; i++) {
			g.f.strum = (i % period) < (period + 1) / 2 ? 10.f : 0.f;
			good = good && g.step();
		}
		CHECK(good, "STRUM at %g Hz: the output left 10 V or went non-finite", 48000.0 / period);
		CHECK(g.e.strikes() >= 480000 / period - 1, "STRUM at %g Hz: %ld strikes", 48000.0 / period, g.e.strikes());
	}
}

void no_allocation_in_process() {
	Rig g(8);
	g.e.setSampleRate(48000.f);
	g.f.inLPatched = true;
	rka::hadal::Random rng;
	rng.seed(3);
	g.step();
	const long before = tAllocs;
	for (int n = 0; n < 400000; n++) {
		g.f.inL = 5.f * rng.uniform();
		g.f.voct = (n / 9000) % 5 - 2.f;
		g.f.chordCv = (n / 7000) % 9 - 4.f;
		g.f.ringCv = 5.f * rng.uniform();
		g.f.dampCv = (n / 300) % 2 ? 4.f : -4.f;
		g.f.strumPatched = (n / 100000) % 2;
		g.f.strum = (n / 2400) % 2 ? 10.f : 0.f;
		g.f.params[F::TUNE] = (float) ((n / 30000) % 49 - 24);
		g.f.freePitch = (n / 50000) % 2;
		g.step();
	}
	g.e.reset();
	g.step();
	CHECK(tAllocs == before, "%ld allocations in process() or reset()", tAllocs - before);
}

void seeded_fuzz() {
	rka::hadal::Random rng;
	rng.seed(2026);
	Rig g(9);
	bool good = true;
	for (int n = 0; n < 1000000; n++) {
		if (n % 4096 == 0) {
			for (int p = 0; p < F::PARAMS_LEN; p++)
				g.f.params[p] = rng.unit();
			g.f.params[F::TUNE] = 30.f * rng.uniform();
			g.f.params[F::CHORD] = 8.f * rng.unit() - 1.f;
			g.f.inLPatched = rng.unit() < 0.7f;
			g.f.inRPatched = rng.unit() < 0.5f;
			g.f.strumPatched = rng.unit() < 0.5f;
			g.f.freePitch = rng.unit() < 0.5f;
			g.f.voct = 12.f * rng.uniform();
			if (n % 131072 == 0) {
				const float rates[5] = {44100.f, 48000.f, 96000.f, 192000.f, 22050.f};
				g.e.setSampleRate(rates[rng.next() % 5]);
			}
		}
		g.f.inL = 12.f * rng.uniform();
		g.f.inR = 12.f * rng.uniform();
		g.f.chordCv = 12.f * rng.uniform();
		g.f.ringCv = rng.unit() < 0.001f ? 12.f * rng.uniform() : g.f.ringCv;
		g.f.dampCv = rng.unit() < 0.001f ? 12.f * rng.uniform() : g.f.dampCv;
		g.f.strum = rng.unit() < 0.002f ? 10.f : (rng.unit() < 0.1f ? 0.f : g.f.strum);
		if (rng.unit() < 0.0005f)
			g.f.voct = 12.f * rng.uniform();
		good = good && g.step();
	}
	CHECK(good, "fuzzed knobs, CVs, triggers, cables and rates must keep the outputs finite and within 10 V");
}

// Struck once and left: every sample of the tail is exactly 0 or a normal
// number, never subnormal, and it ends in exact silence. A subnormal tail
// would also be slow: the last second must not take far longer than the first.
void denormals() {
	Rig g(10);
	g.f.strumPatched = true;
	g.f.params[F::RING] = 0.39f;   // about 1 s
	g.f.params[F::DAMP] = 0.f;
	g.f.params[F::MIX] = 1.f;
	g.f.strum = 10.f;
	bool normal = true;
	float worst = 0.f;
	double firstSecond = 0.0, lastSecond = 0.0;
	for (int s = 0; s < 30; s++) {
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < 48000; i++) {
			g.step();
			for (float v : {g.r.l, g.r.r}) {
				const float a = std::fabs(v);
				if (a != 0.f && a < 1.17549435e-38f) {
					normal = false;
					worst = v;
				}
			}
		}
		const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		if (s == 0)
			firstSecond = dt;
		if (s == 29)
			lastSecond = dt;
	}
	CHECK(normal, "the tail of a strike passes through subnormals (%g)", worst);
	CHECK(g.r.l == 0.f && g.r.r == 0.f, "30 s after a 1 s strike the output is %g, %g", g.r.l, g.r.r);
	CHECK(lastSecond < 4.0 * firstSecond + 0.05, "the silent tail runs slow: %.3g s for its last second, %.3g s for the first", lastSecond, firstSecond);
}

} // namespace

int main() {
	extreme_rates();
	rate_change_recovers();
	hostile_inputs();
	tuned_square_at_60s();
	strum_at_audio_rate();
	no_allocation_in_process();
	seeded_fuzz();
	denormals();
	return rkatest::finish("rigging test_hostile");
}

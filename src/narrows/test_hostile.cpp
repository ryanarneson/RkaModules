// Narrows under hostile conditions, built with the address and
// undefined-behaviour sanitizers and a counting operator new: NaN and inf on
// every port and knob, extreme and changing sample rates, channel counts
// that disagree and change every sample, the cutoff at its ends under
// audio-rate FM, minutes with everything at its top, the core switch and
// Mode flipped at audio rate, pings at audio rate, a seeded fuzz of
// everything, and no allocation in process().
#include "narrows/NarrowsEngine.hpp"
#include "testing.hpp"

#include <chrono>
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

typedef rka::NarrowsFrame F;
using namespace rka::narrows;

namespace {

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float next() {   // -1 .. 1
		s = s * 1664525u + 1013904223u;
		return float(int32_t(s)) * (1.f / 2147483648.f);
	}
	int upTo(int n) {   // 0 .. n - 1
		s = s * 1664525u + 1013904223u;
		return int((s >> 8) % uint32_t(n));
	}
};

struct Rig {
	rka::NarrowsEngine e;
	F f;
	rka::NarrowsResult r;
	explicit Rig(float fs = 48000.f) {
		e.setSampleRate(fs);
		f.params[F::CUTOFF] = CUTOFF_DEFAULT;
		f.params[F::CORE] = 1.f;
		f.outRConnected = true;
	}
	rka::NarrowsPort* ports[8] = {&f.inL, &f.inR, &f.voct, &f.fm, &f.res, &f.comb, &f.mode, &f.ping};
	// Every output channel finite and inside the limit.
	bool step() {
		e.process(f, r);
		if (r.channels < 1 || r.channels > F::MAX_VOICES)
			return false;
		for (int v = 0; v < r.channels; v++)
			if (!(std::fabs(r.l[v]) <= 10.f && std::fabs(r.r[v]) <= 10.f))
				return false;
		return true;
	}
	void everythingUp() {
		f.params[F::RES] = 1.f;
		f.params[F::DRIVE] = 1.f;
		f.params[F::COMB] = 1.f;
		f.params[F::SPREAD] = 1.f;
	}
	void channels(int n) {
		for (auto* p : ports)
			p->channels = n;
	}
};

void hostile_values() {
	const float bad[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f, 1e-40f};
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.everythingUp();
		g.f.params[F::CORE] = float(core);
		g.channels(16);
		Lcg rng(11 + core);
		long badSteps = 0;
		for (int i = 0; i < 60000; i++) {
			for (auto* p : g.ports)
				for (int c = 0; c < 16; c++)
					p->v[c] = rng.upTo(4) == 0 ? bad[rng.upTo(6)] : 10.f * rng.next();
			if (i % 500 == 0)
				for (int k = 0; k < F::PARAMS_LEN; k++)
					g.f.params[k] = rng.upTo(3) == 0 ? bad[rng.upTo(6)] : rng.next();
			if (!g.step())
				badSteps++;
		}
		CHECK(badSteps == 0, "core %d: %ld steps not finite or past 10 V", core, badSteps);
		// And it recovers: clean settings, silence in, quiet out.
		for (auto* p : g.ports)
			for (int c = 0; c < 16; c++)
				p->v[c] = 0.f;
		for (int k = 0; k < F::PARAMS_LEN; k++)
			g.f.params[k] = 0.f;
		g.f.params[F::CUTOFF] = CUTOFF_DEFAULT;
		g.f.params[F::CORE] = float(core);
		for (int i = 0; i < 96000; i++)
			g.step();
		float peak = 0.f;
		for (int i = 0; i < 4800; i++) {
			g.step();
			for (int v = 0; v < 16; v++)
				peak = std::fmax(peak, std::fabs(g.r.l[v]) + std::fabs(g.r.r[v]));
		}
		CHECK(peak < 0.01f, "core %d: quiet again afterwards (%g V)", core, peak);
	}
}

// Review Focus 2: any rate, a change of rate in any state, and the cutoff
// at its ends under audio-rate FM.
void extreme_rates() {
	for (float fs : {1.f, 10.f, 1000.f, 8000.f, 44100.f, 192000.f, 768000.f})
		for (int core = 0; core < 2; core++) {
			Rig g(fs);
			g.everythingUp();
			g.f.params[F::CORE] = float(core);
			g.f.params[F::FM] = 1.f;
			g.channels(3);
			Lcg rng(5);
			long badSteps = 0;
			for (int i = 0; i < 30000; i++) {
				g.f.params[F::CUTOFF] = (i / 5000) % 2 ? 1.f : 0.f;
				for (int c = 0; c < 3; c++) {
					g.f.inL.v[c] = 10.f * rng.next();
					g.f.inR.v[c] = 10.f * rng.next();
					g.f.fm.v[c] = 10.f * rng.next();   // +/-10 octaves, every sample
					g.f.voct.v[c] = 10.f * rng.next();
					g.f.ping.v[c] = (i / 7) % 2 ? 10.f : 0.f;
				}
				if (!g.step())
					badSteps++;
				if (i == 15000)
					g.e.setSampleRate(fs * 2.f);
			}
			CHECK(badSteps == 0, "at %g Hz, core %d: %ld bad steps", fs, core, badSteps);
		}
	// Rates that are not rates leave the engine as it was.
	Rig g;
	g.e.setSampleRate(0.f);
	g.e.setSampleRate(-48000.f);
	g.e.setSampleRate(NAN);
	g.f.inL.channels = 1;
	g.f.inL.v[0] = 1.f;
	CHECK(g.step(), "after rates that are not rates");
}

// Review Focus 1: channel counts that disagree and change every sample.
void channel_churn() {
	Rig g;
	g.everythingUp();
	g.f.params[F::RES] = 0.8f;
	Lcg rng(21);
	long badSteps = 0, wrongCount = 0;
	for (int i = 0; i < 200000; i++) {
		int most = 1;
		for (auto* p : g.ports) {
			p->channels = rng.upTo(18);   // 0 .. 17: one past the most a cable carries
			for (int c = 0; c < 16; c++)
				p->v[c] = 5.f * rng.next();
		}
		for (auto* p : {&g.f.inL, &g.f.inR, &g.f.voct, &g.f.ping})
			most = p->channels > most ? p->channels : most;
		if (most > 16)
			most = 16;
		if (!g.step())
			badSteps++;
		if (g.r.channels != most)
			wrongCount++;
	}
	CHECK(badSteps == 0 && wrongCount == 0, "%ld bad steps, %ld wrong voice counts", badSteps, wrongCount);
}

// Review Focus 3: minutes with everything at its top; bounded, no DC, no
// slowdown in silence, and silent once Res comes down.
void minutes_at_the_top() {
	for (int core = 0; core < 2; core++) {
		Rig g;
		g.everythingUp();
		g.f.params[F::CORE] = float(core);
		g.f.params[F::CUTOFF] = cutoffKnob(200.f);
		g.f.inL.channels = 1;
		Lcg rng(31);
		long badSteps = 0;
		const int n = 60 * 48000;
		double noiseSeconds = 0.0, silentSeconds = 0.0, dc = 0.0;
		for (int phase = 0; phase < 3; phase++) {
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < n; i++) {
				g.f.inL.v[0] = phase == 0 ? 10.f * rng.next() : phase == 1 ? 10.f : 0.f;
				if (!g.step())
					badSteps++;
				if (phase == 2 && i >= n - 48000)
					dc += g.r.l[0];
			}
			const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			(phase == 0 ? noiseSeconds : silentSeconds) = dt;
		}
		CHECK(badSteps == 0, "core %d: %ld bad steps in three minutes", core, badSteps);
		CHECK(std::fabs(dc / 48000.0) < 0.05, "core %d: DC %g V", core, dc / 48000.0);
		CHECK(silentSeconds < 3.0 * noiseSeconds, "core %d: silence took %g s against %g s of noise (subnormals?)", core,
		      silentSeconds, noiseSeconds);
		// Everything down, and the comb's delay line and both cores drain.
		g.f.params[F::RES] = g.f.params[F::COMB] = g.f.params[F::DRIVE] = 0.f;
		for (int i = 0; i < 10 * 48000; i++)
			g.step();
		float peak = 0.f;
		for (int i = 0; i < 4800; i++) {
			g.step();
			peak = std::fmax(peak, std::fabs(g.r.l[0]));
		}
		CHECK(peak < 0.01f, "core %d: silent after it all (%g V)", core, peak);
	}
}

// Review Focus 4 and 5: the core switch, Mode's CV and PING at audio rate.
void switches_at_audio_rate() {
	Rig g;
	g.everythingUp();
	g.channels(4);
	Lcg rng(41);
	long badSteps = 0;
	int flips = 0;
	for (int i = 0; i < 300000; i++) {
		const int every = 1 + (i / 30000);   // every sample, then every 2, ... 10
		if (i % every == 0) {
			g.f.params[F::CORE] = 1.f - g.f.params[F::CORE];
			flips++;
		}
		for (int c = 0; c < 4; c++) {
			g.f.inL.v[c] = 5.f * rng.next();
			g.f.mode.v[c] = (i / (c + 1)) % 2 ? 10.f : 0.f;
			g.f.ping.v[c] = (i / (c + 2)) % 2 ? 10.f : 0.f;
		}
		if (!g.step())
			badSteps++;
	}
	CHECK(badSteps == 0 && flips > 80000, "%ld bad steps", badSteps);
	// A PING cable with more channels than the audio: every voice is struck
	// from its own channel, the audio's one channel feeds them all.
	Rig p;
	p.f.params[F::RES] = 0.9f;
	p.f.inL.channels = 1;
	p.f.ping.channels = 16;
	for (int i = 0; i < 2000; i++) {
		for (int c = 0; c < 16; c++)
			p.f.ping.v[c] = (i > 100 && i < 200) ? 10.f : 0.f;
		CHECK(p.step(), "poly ping over mono audio");
	}
	CHECK(p.r.channels == 16, "sixteen voices from PING alone, got %d", p.r.channels);
}

void fuzz() {
	for (uint32_t seed = 1; seed <= 12; seed++) {
		Lcg rng(seed * 7919u);
		const float rates[] = {8000.f, 44100.f, 48000.f, 96000.f, 384000.f};
		Rig g(rates[rng.upTo(5)]);
		long badSteps = 0;
		for (int i = 0; i < 40000; i++) {
			if (rng.upTo(2000) == 0)
				for (int k = 0; k < F::PARAMS_LEN; k++)
					g.f.params[k] = k == F::COMB || k == F::FM ? rng.next() : 0.5f + 0.6f * rng.next();
			if (rng.upTo(3000) == 0)
				for (auto* p : g.ports)
					p->channels = rng.upTo(17);
			if (rng.upTo(20000) == 0)
				g.e.reset();
			if (rng.upTo(20000) == 0)
				g.e.setSampleRate(rates[rng.upTo(5)]);
			g.f.outRConnected = (i / 5000) % 2 == 0;
			for (auto* p : g.ports)
				for (int c = 0; c < 16; c++)
					p->v[c] = 12.f * rng.next();
			if (!g.step())
				badSteps++;
		}
		CHECK(badSteps == 0, "seed %u: %ld bad steps", seed, badSteps);
	}
}

void no_allocation_in_process() {
	Rig g;
	const long before = tAllocs;
	g.e.setSampleRate(96000.f);   // longer lines than the 48 kHz ones it has
	CHECK(tAllocs > before, "setSampleRate allocates the combs (the counter works)");
	g.everythingUp();
	Lcg rng(51);
	const long start = tAllocs;
	for (int i = 0; i < 100000; i++) {
		if (i % 1000 == 0) {
			g.channels(1 + rng.upTo(16));
			g.f.params[F::CORE] = float(rng.upTo(2));
			g.f.params[F::COMB] = rng.next();
		}
		if (i == 50000)
			g.e.reset();
		for (auto* p : g.ports)
			for (int c = 0; c < 16; c++)
				p->v[c] = 5.f * rng.next();
		g.step();
	}
	CHECK(tAllocs == start, "%ld allocations in process() and reset()", tAllocs - start);
}

} // namespace

int main() {
	hostile_values();
	extreme_rates();
	channel_churn();
	minutes_at_the_top();
	switches_at_audio_rate();
	fuzz();
	no_allocation_in_process();
	return rkatest::finish("narrows/test_hostile");
}

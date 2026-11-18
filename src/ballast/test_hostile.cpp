// Ballast under hostile conditions (plan Task 5), built with the address and
// undefined-behaviour sanitizers and a counting operator new: NaN and inf on
// every input, unusable and extreme sample rates, rate changes, no
// allocation in process(), seeded fuzz, and denormal-free state.
#include "testing.hpp"
#include "BallastEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

// Heap allocations made by operator new on this thread.
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

typedef rka::BallastFrame F;

namespace {

struct Nop : rka::BallastExpander {};

struct Rig {
	rka::BallastEngine e;
	F f;
	float inL[4][16] = {}, inR[4][16] = {}, cv[4][16] = {}, mcv[16] = {}, chL[16] = {}, chR[16] = {};
	rka::BallastResult out;
	Rig() {
		for (int p = 0; p < F::PARAMS_LEN; p++)
			f.params[p] = 0.75f;
		for (int c = 0; c < 4; c++) {
			f.inL[c].v = inL[c];
			f.inR[c].v = inR[c];
			f.cv[c].v = cv[c];
			f.inL[c].channels = f.inR[c].channels = f.cv[c].channels = 1;
		}
		f.masterCv.v = mcv;
		f.chainL.v = chL;
		f.chainR.v = chR;
		f.masterCv.channels = f.chainL.channels = f.chainR.channels = 1;
		f.outL = f.outR = true;
	}
	void setAll(float v) {
		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 16; k++)
				inL[c][k] = inR[c][k] = cv[c][k] = v;
		for (int k = 0; k < 16; k++)
			mcv[k] = chL[k] = chR[k] = v;
	}
};

bool ok(const rka::BallastResult& r) {
	return r.l == r.l && r.r == r.r && r.l <= 10.f && r.l >= -10.f && r.r <= 10.f && r.r >= -10.f;
}

void nan_inputs_safe() {
	for (float bad : {NAN, INFINITY, -INFINITY}) {
		Rig g;
		g.setAll(bad);
		bool good = true;
		for (int i = 0; i < 4800; i++) {
			g.e.process(g.f, g.out);
			good = good && ok(g.out);
		}
		g.setAll(1.f);
		for (int i = 0; i < 4800; i++) {
			g.e.process(g.f, g.out);
			good = good && ok(g.out);
		}
		CHECK(good, "%g on every input, CV and CHAIN must keep the outputs finite and within 10 V", bad);
	}
}

// The 3 Hz response of the DC blocker after the given rate history: the RMS
// of a 3 Hz sine on against off, in dB.
double threeHz(rka::BallastEngine& on) {
	rka::BallastEngine off;
	F f[2];
	float x[16] = {};
	for (F& fr : f) {
		for (int p = 0; p < F::PARAMS_LEN; p++)
			fr.params[p] = 0.75f;
		fr.inL[0].v = x;
		fr.inL[0].channels = 1;
		fr.outL = fr.outR = true;
	}
	f[1].dcBlock = false;
	rka::BallastResult a, b;
	double sa = 0, sb = 0;
	const int settle = 4 * 48000, n = 2 * 48000;
	for (int i = 0; i < settle + n; i++) {
		x[0] = std::sin(2.0 * 3.14159265358979 * 3.0 * i / 48000.0);
		on.process(f[0], a);
		off.process(f[1], b);
		if (i >= settle) {
			sa += (double) a.l * a.l;
			sb += (double) b.l * b.l;
		}
	}
	return 10.0 * std::log10(sa / sb);
}

void bad_rate_ignored() {
	rka::BallastEngine e;
	for (float bad : {0.f, -1.f, NAN, INFINITY, 1e9f})
		e.setSampleRate(bad);
	const double r = threeHz(e);
	CHECK(std::fabs(r + 3.0) <= 0.5, "after unusable rates the blocker is %+.2f dB at 3 Hz (want -3: still 48 kHz)", r);
}

void rate_change_recovers() {
	rka::BallastEngine e;
	float x[16] = {1.f};
	F f;
	f.inL[0].v = x;
	f.inL[0].channels = 1;
	f.outL = f.outR = true;
	rka::BallastResult o;
	for (float fs : {44100.f, 768000.f, 48000.f}) {
		e.setSampleRate(fs);
		for (int i = 0; i < 20000; i++)
			e.process(f, o);
	}
	const double r = threeHz(e);
	CHECK(std::fabs(r + 3.0) <= 0.5, "after 44.1k, 768k, 48k the blocker is %+.2f dB at 3 Hz (want -3 +- 0.5)", r);
}

void extreme_rates() {
	for (float fs : {1.f, 10.f, 1000.f, 8000.f, 44100.f, 192000.f, 768000.f}) {
		Rig g;
		g.e.setSampleRate(fs);
		bool good = true;
		for (int i = 0; i < 20000; i++) {
			g.setAll((i / 7) % 2 ? 10.f : -10.f);
			g.e.process(g.f, g.out);
			good = good && ok(g.out);
		}
		CHECK(good, "at %.0f Hz the outputs must stay finite and within 10 V", fs);
	}
}

void no_allocation_in_process() {
	Rig g;
	Nop a, b;
	rka::BallastExpander* chain[2] = {&a, &b};
	const long before = tAllocs;
	for (int n = 0; n < 200000; n++) {
		g.setAll((n / 480) % 2 ? 4.f : -4.f);
		g.f.outR = (n / 7000) % 2;
		g.e.process(g.f, g.out, chain, 2);
	}
	CHECK(tAllocs == before, "%ld allocations in process()", tAllocs - before);
}

void seeded_fuzz() {
	Rig g;
	uint32_t s = 2026;
	auto next = [&]() {
		s = s * 1664525u + 1013904223u;
		return (float) (s >> 8) / 16777216.f;   // [0, 1)
	};
	bool good = true;
	for (int n = 0; n < 1000000; n++) {
		if (n % 4096 == 0) {
			for (int p = 0; p < F::PARAMS_LEN; p++)
				g.f.params[p] = next() * 1.2f - 0.1f;
			for (int c = 0; c < 4; c++) {
				g.f.inL[c].channels = (int) (next() * 17.f);
				g.f.inR[c].channels = (int) (next() * 17.f);
				g.f.cv[c].channels = (int) (next() * 3.f);
			}
			g.f.masterCv.channels = (int) (next() * 2.f);
			g.f.chainL.channels = (int) (next() * 3.f);
			g.f.chainR.channels = (int) (next() * 3.f);
			g.f.outL = next() < 0.8f;
			g.f.outR = next() < 0.6f;
			g.f.dcBlock = next() < 0.7f;
			if (n % 65536 == 0) {
				const float rates[5] = {44100.f, 48000.f, 96000.f, 192000.f, 22050.f};
				g.e.setSampleRate(rates[(int) (next() * 5.f)]);
			}
		}
		for (int c = 0; c < 4; c++)
			for (int k = 0; k < 2; k++) {
				g.inL[c][k] = next() * 24.f - 12.f;
				g.inR[c][k] = next() * 24.f - 12.f;
				g.cv[c][k] = next() * 24.f - 12.f;
			}
		g.mcv[0] = next() * 24.f - 12.f;
		g.chL[0] = next() * 24.f - 12.f;
		g.e.process(g.f, g.out);
		good = good && ok(g.out);
	}
	CHECK(good, "fuzzed knobs, CVs, poly counts, patch states and rates must keep the outputs finite and within 10 V");
}

// After a loud run then silence, the DC blocker's state is 0 or normal.
void denormals() {
	Rig g;
	g.setAll(5.f);
	for (int i = 0; i < 48000; i++)
		g.e.process(g.f, g.out);
	g.setAll(0.f);
	for (int i = 0; i < 2000000; i++)
		g.e.process(g.f, g.out);
	bool normal = true;
	for (int s = 0; s < 2; s++) {
		const float v = g.e.dcState(s);
		normal = normal && (v == 0.f || std::fabs(v) > 1e-30f);
	}
	CHECK(normal, "the DC blocker decays into subnormals (%g, %g)", g.e.dcState(0), g.e.dcState(1));
}

} // namespace

int main() {
	nan_inputs_safe();
	bad_rate_ignored();
	rate_change_recovers();
	extreme_rates();
	no_allocation_in_process();
	seeded_fuzz();
	denormals();
	return rkatest::finish("ballast test_hostile");
}

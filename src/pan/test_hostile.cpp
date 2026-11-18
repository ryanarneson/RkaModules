// Pan under hostile conditions (plan Task 5), built with the address and
// undefined-behaviour sanitizers and a counting operator new: NaN and inf on
// every control, unusable rates, no allocation, seeded fuzz; always as a
// Ballast and Pan pair, as in Rack.
#include "testing.hpp"
#include "PanCore.hpp"
#include "ballast/BallastEngine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
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

typedef rka::BallastFrame BF;

namespace {

struct TestPan : rka::PanCore {
	rka::PanFrame in;
	float cvv[4][16] = {};
	int idleValue = 0;
	TestPan() {
		for (int c = 0; c < 4; c++) {
			in.cv[c].v = cvv[c];
			in.cv[c].channels = 1;
		}
	}
	void readFrame(rka::PanFrame& f) override {
		f = in;
	}
	int idle() const override {
		return idleValue;
	}
};

struct Pair {
	rka::BallastEngine e;
	TestPan pan;
	BF f;
	float inL[4][16] = {}, inR[4][16] = {}, cv[4][16] = {}, mcv[16] = {}, chL[16] = {}, chR[16] = {};
	rka::BallastResult out;
	Pair() {
		for (int p = 0; p < BF::PARAMS_LEN; p++)
			f.params[p] = 0.75f;
		for (int c = 0; c < 4; c++) {
			f.inL[c].v = inL[c];
			f.inR[c].v = inR[c];
			f.cv[c].v = cv[c];
			f.inL[c].channels = 1;
			f.inR[c].channels = c % 2;
		}
		f.masterCv.v = mcv;
		f.chainL.v = chL;
		f.chainR.v = chR;
		f.outL = f.outR = true;
	}
	void step() {
		rka::BallastExpander* chain[1] = {&pan};
		pan.linked();
		e.process(f, out, chain, 1);
	}
};

bool ok(const rka::BallastResult& r) {
	return r.l == r.l && r.r == r.r && r.l <= 10.f && r.l >= -10.f && r.r <= 10.f && r.r >= -10.f;
}

void nan_cv_safe() {
	for (float bad : {NAN, INFINITY, -INFINITY}) {
		Pair g;
		for (int c = 0; c < 4; c++) {
			g.inL[c][0] = g.inR[c][0] = 1.f;
			g.pan.in.knob[c] = bad;
			g.pan.in.att[c] = bad;
			for (int k = 0; k < 16; k++)
				g.pan.cvv[c][k] = bad;
		}
		bool good = true;
		for (int i = 0; i < 4800; i++) {
			g.step();
			good = good && ok(g.out);
		}
		for (int c = 0; c < 4; c++) {
			g.pan.in.knob[c] = 0.3f;
			g.pan.in.att[c] = 1.f;
			g.pan.cvv[c][0] = 1.f;
		}
		for (int i = 0; i < 4800; i++) {
			g.step();
			good = good && ok(g.out);
		}
		CHECK(good, "%g on every Pan knob, attenuverter and CV must keep the outputs finite and within 10 V", bad);
	}
}

void bad_rate_ignored() {
	TestPan a, b;
	a.setSampleRate(48000.f);
	b.setSampleRate(48000.f);
	for (float bad : {0.f, -1.f, NAN, INFINITY, 1e9f})
		b.setSampleRate(bad);
	a.in.knob[0] = b.in.knob[0] = 1.f;
	bool same = true;
	for (int i = 0; i < 2400; i++) {
		a.linked();
		b.linked();
		same = same && a.position(0) == b.position(0);
	}
	CHECK(same, "unusable sample rates must be ignored (the smoothing changed)");
}

void no_allocation() {
	Pair g;
	const long before = tAllocs;
	for (int n = 0; n < 200000; n++) {
		for (int c = 0; c < 4; c++) {
			g.inL[c][0] = (n / 480) % 2 ? 4.f : -4.f;
			g.pan.cvv[c][0] = (n / 300) % 2 ? 5.f : -5.f;
			g.pan.in.att[c] = 1.f;
		}
		g.pan.in.stereoMode = (n / 50000) % 2;
		g.step();
	}
	CHECK(tAllocs == before, "%ld allocations in a Ballast and Pan pair", tAllocs - before);
}

void seeded_fuzz() {
	Pair g;
	uint32_t s = 2026;
	auto next = [&]() {
		s = s * 1664525u + 1013904223u;
		return (float) (s >> 8) / 16777216.f;
	};
	bool good = true;
	for (int n = 0; n < 1000000; n++) {
		if (n % 4096 == 0) {
			for (int c = 0; c < 4; c++) {
				g.pan.in.knob[c] = next() * 1.4f - 0.2f;
				g.pan.in.att[c] = next() * 2.4f - 1.2f;
				g.pan.in.cv[c].channels = (int) (next() * 3.f);
				g.f.inL[c].channels = (int) (next() * 3.f);
				g.f.inR[c].channels = (int) (next() * 3.f);
			}
			g.pan.in.stereoMode = (int) (next() * 3.f) - 0;   // 2 is invalid: treated as true pan
			g.pan.idleValue = next() < 0.1f ? 5 : 0;
			g.f.outR = next() < 0.7f;
			g.f.dcBlock = next() < 0.7f;
			if (n % 65536 == 0) {
				const float rates[4] = {44100.f, 48000.f, 96000.f, 192000.f};
				const float fs = rates[(int) (next() * 4.f)];
				g.e.setSampleRate(fs);
				g.pan.setSampleRate(fs);
			}
		}
		for (int c = 0; c < 4; c++) {
			for (int k = 0; k < 2; k++) {
				g.inL[c][k] = next() * 24.f - 12.f;
				g.inR[c][k] = next() * 24.f - 12.f;
				g.pan.cvv[c][k] = next() * 24.f - 12.f;
			}
		}
		g.step();
		good = good && ok(g.out);
	}
	CHECK(good, "fuzzed Pan and Ballast controls must keep the outputs finite and within 10 V");
}

} // namespace

int main() {
	nan_cv_safe();
	bad_rate_ignored();
	no_allocation();
	seeded_fuzz();
	return rkatest::finish("pan test_hostile");
}

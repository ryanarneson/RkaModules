// Hadal under hostile conditions (plan Task 6), built with the address and
// undefined-behaviour sanitizers and a counting operator new: sample-rate
// changes, extreme rates, no allocation in process(), seeded fuzz of every
// knob, CV, patch state and rate, and denormal-free decaying state.
#include "testing.hpp"
#include "test_measure.hpp"
#include "HadalEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

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

using namespace hadaltest;
typedef rka::HadalFrame F;

namespace {

struct Rig {
	rka::HadalEngine e;
	F f;
	float cvv[5][16] = {};
	explicit Rig(uint32_t seed = 1) : e(seed) {
		for (int i = 0; i < F::PARAMS_LEN; i++)
			f.params[i] = 0.f;
		f.params[F::DEPTH] = 0.5f;
		f.params[F::RUMBLE] = 0.5f;
		f.params[F::HISS] = 0.5f;
		f.params[F::CRACKLE] = 0.5f;
		for (int i = 0; i < 5; i++) {
			f.cv[i].v = cvv[i];
			f.patched[i] = true;
		}
	}
};

bool ok(const rka::HadalResult& r) {
	const float v[5] = {r.rumble, r.hiss, r.crackle, r.l, r.r};
	for (float x : v)
		if (!(x == x) || x > 10.f || x < -10.f)
			return false;
	return true;
}

std::vector<float> hissAt(Rig& g, float fs, float seconds) {
	rka::HadalResult res;
	std::vector<float> out((size_t) (fs * seconds));
	for (int i = 0; i < (int) (fs * 0.3f); i++)
		g.e.process(g.f, res);
	for (float& v : out) {
		g.e.process(g.f, res);
		v = res.hiss;
	}
	return out;
}

void rate_change_recovers() {
	Rig fresh, g;
	fresh.f.params[F::RUMBLE] = fresh.f.params[F::CRACKLE] = 0.f;
	g.f.params[F::RUMBLE] = g.f.params[F::CRACKLE] = 0.f;
	fresh.e.setSampleRate(48000.f);
	const std::vector<float> ref = hissAt(fresh, 48000.f, 4.f);
	bool finite = true;
	rka::HadalResult res;
	for (float fs : {44100.f, 768000.f, 48000.f}) {
		g.e.setSampleRate(fs);
		for (int i = 0; i < (int) (fs * 0.5f); i++) {
			g.e.process(g.f, res);
			finite = finite && ok(res);
		}
	}
	CHECK(finite, "rate changes must keep every output finite and within 10 V");
	const std::vector<float> after = hissAt(g, 48000.f, 4.f);
	const double c0 = centroid(psd(ref, 4096), 4096, 48000.0), c1 = centroid(psd(after, 4096), 4096, 48000.0);
	CHECK(std::fabs(c1 / c0 - 1.0) < 0.05, "after rate changes the hiss centroid is %.0f Hz (fresh %.0f)", c1, c0);
	CHECK(std::fabs(db(rms(after) / rms(ref))) < 1.0, "after rate changes the hiss level is off by %.2f dB",
	      db(rms(after) / rms(ref)));
}

void extreme_rates() {
	for (float fs : {1.f, 10.f, 1000.f, 8000.f, 44100.f, 192000.f, 768000.f}) {
		Rig g;
		g.e.setSampleRate(fs);
		g.f.params[F::DRIFT] = 1.f;
		rka::HadalResult res;
		bool good = true;
		const int n = fs < 48000.f ? 20000 : (int) (fs * 0.5f);
		for (int i = 0; i < n; i++) {
			g.e.process(g.f, res);
			good = good && ok(res);
		}
		CHECK(good, "at %.0f Hz every output must stay finite and within 10 V", fs);
	}
}

void no_allocation_in_process() {
	Rig g;
	g.f.params[F::DRIFT] = 1.f;
	for (int i = 0; i < 5; i++) {
		g.f.params[F::DEPTH_ATT + i] = 1.f;
		g.f.cv[i].channels = 1;
	}
	rka::HadalResult res;
	const long before = tAllocs;
	for (int n = 0; n < 200000; n++) {
		for (int i = 0; i < 5; i++)
			g.cvv[i][0] = (n / 480) % 2 ? 4.f : -4.f;
		g.f.patched[n / 20000 % 5] = (n / 7000) % 2;
		g.e.process(g.f, res);
	}
	CHECK(tAllocs == before, "%ld allocations in process()", tAllocs - before);
}

void seeded_fuzz() {
	rka::hadal::Random rng;
	rng.seed(2026);
	Rig g(9);
	rka::HadalResult res;
	bool good = true;
	for (int n = 0; n < 1000000; n++) {
		if (n % 4096 == 0) {
			for (int p = 0; p < F::PARAMS_LEN; p++)
				g.f.params[p] = p >= F::DEPTH_ATT ? rng.uniform() : rng.unit();
			for (int i = 0; i < 5; i++) {
				g.f.patched[i] = rng.unit() < 0.6f;
				g.f.cv[i].channels = (int) (rng.unit() * 3.f);
			}
			if (n % 65536 == 0) {
				const float rates[5] = {44100.f, 48000.f, 96000.f, 192000.f, 22050.f};
				g.e.setSampleRate(rates[rng.next() % 5]);
			}
		}
		for (int i = 0; i < 5; i++)
			for (int c = 0; c < 2; c++)
				g.cvv[i][c] = rng.uniform() * 12.f;
		g.e.process(g.f, res);
		good = good && ok(res);
	}
	CHECK(good, "fuzzed knobs, CVs, patch states and rates must keep every output finite and within 10 V");
}

// A crackle that stops ticking decays to exactly 0, never into subnormals.
void denormals() {
	rka::hadal::Crackle c;
	c.setRate(48000.f);
	c.seed(4);
	c.setDepth(1.f);
	c.setDensity(50.f);
	float l, r;
	for (int i = 0; i < 48000; i++)
		c.process(l, r);
	c.setDensity(0.f);
	for (int i = 0; i < 2000000; i++)
		c.process(l, r);
	bool normal = true;
	for (int s = 0; s < 2; s++)
		for (double v : {c.y1[s], c.y2[s]})
			normal = normal && (v == 0.0 || std::fabs(v) > 1e-300);
	CHECK(normal, "crackle's state decays into subnormals (y1 %g, y2 %g)", c.y1[0], c.y2[0]);
}

} // namespace

int main() {
	rate_change_recovers();
	extreme_rates();
	no_allocation_in_process();
	seeded_fuzz();
	denormals();
	return rkatest::finish("hadal test_hostile");
}

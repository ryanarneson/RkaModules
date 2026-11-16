// The Sluice engine under hostile input, and its allocation behaviour:
// NaN and infinity on inputs, CVs and Levels, a sample-rate change in the
// middle of a Level step, no heap allocation in process(), and values of
// 1e30. Built with the sanitizers (tests.mk), so undefined behaviour and
// memory errors fail the run. Run with `make test`.
#include "SluiceEngine.hpp"
#include "testing.hpp"

#include <cmath>
#include <algorithm>
#include <cstdint>
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

using F = rka::SluiceFrame;

namespace {

const float FS = 48000.f;
const int ROWS = 4;

struct Patch {
	float in[ROWS][16] = {};
	int inCh[ROWS] = {};
	float cv[ROWS][16] = {};
	int cvCh[ROWS] = {};
	bool outConn[ROWS] = {};
	float level[ROWS] = {};
	bool exp = false;

	F frame() const {
		F f;
		for (int r = 0; r < ROWS; r++) {
			f.params[F::LEVEL_A + r] = level[r];
			f.in[r].v = in[r];
			f.in[r].channels = inCh[r];
			f.cv[r].v = cv[r];
			f.cv[r].channels = cvCh[r];
			f.outConnected[r] = outConn[r];
		}
		f.params[F::CURVE] = exp ? 1.f : 0.f;
		return f;
	}
};

bool allFinite(const rka::SluiceResult& r) {
	for (int o = 0; o < ROWS; o++) {
		if (!std::isfinite(r.light[o]))
			return false;
		for (int c = 0; c < r.channels[o]; c++)
			if (!std::isfinite(r.out[o][c]))
				return false;
	}
	return true;
}

void testNanDoesNotTravel() {
	const float bad[3] = {NAN, INFINITY, -INFINITY};
	for (int k = 0; k < 3; k++) {
		for (int where = 0; where < 3; where++) {
			Patch p;
			for (float& l : p.level)
				l = 1.f;
			p.outConn[3] = true;
			const float ins[ROWS] = {0.f, 0.25f, 0.5f, 1.f};
			for (int r = 0; r < ROWS; r++) {
				p.in[r][0] = ins[r];
				p.inCh[r] = 1;
			}
			// Row A is the hostile one.
			if (where == 0)
				p.in[0][0] = bad[k];
			else if (where == 1) {
				p.cv[0][0] = bad[k];
				p.cvCh[0] = 1;
				p.in[0][0] = 1.f;
			} else {
				// A's 1 V shows what the Level became: it must read as 0.
				p.in[0][0] = 1.f;
				p.level[0] = bad[k];
			}
			rka::SluiceEngine e;
			rka::SluiceResult r;
			const F f = p.frame();
			for (int i = 0; i < 2000; i++)
				e.process(f, FS, r);
			CHECK(allFinite(r), "bad %d at %d: outputs not finite", k, where);
			// Row A's input is 0 (where 0) or 1 with a bad CV (0 V, so Level 1:
			// gain 1; where 2: Level read as 0, gain 0, so row A adds 0).
			const float a = where == 1 ? 1.f : 0.f;
			const float want = a + 0.25f + 0.5f + 1.f;
			CHECK(std::fabs(r.out[3][0] - want) < 1e-5f, "bad %d at %d: D = %g, want %g", k, where, r.out[3][0], want);
		}
	}
}

// Step Level 0 -> 1, switch sample rate after 5 ms: within 1 % of the target
// at 50 ms of wall time (+/- 1 ms). 48 -> 96 kHz, and 96 -> 48 kHz (where a
// stale coefficient would be twice too slow).
void testSmoothingFollowsSampleRate() {
	const float rates[2][2] = {{48000.f, 96000.f}, {96000.f, 48000.f}};
	for (int d = 0; d < 2; d++)
		for (int e = 0; e < 2; e++) {
			const float r0 = rates[d][0], r1 = rates[d][1];
			Patch p;
			p.in[0][0] = 1.f;
			p.inCh[0] = 1;
			p.outConn[0] = true;
			rka::SluiceEngine eng;
			rka::SluiceResult r;
			F f = p.frame();
			for (int i = 0; i < 100; i++)
				eng.process(f, r0, r);
			CHECK(r.out[0][0] == 0.f, "settled at Level 0: %g", r.out[0][0]);
			f.params[F::LEVEL_A] = 1.f;
			for (int i = 0; i < int(0.005f * r0); i++)
				eng.process(f, r0, r);
			// The rest of 50 ms (+/- 1 ms) at the new rate.
			const int n = int((0.045f + (e ? 0.001f : -0.001f)) * r1);
			for (int i = 0; i < n; i++)
				eng.process(f, r1, r);
			CHECK(std::fabs(r.out[0][0] - 1.f) <= 0.01f, "%g -> %g Hz, 50 ms %+d ms: %g, not within 1%% of 1", r0, r1, e ? 1 : -1, r.out[0][0]);
		}
}

// An independent scalar model of the spec, in double through libm: the rope,
// the p clamp, the curve, the mix downward with mono spread, the carry stop.
// Every row is held as 16 dense voltages (a mono value copied into all 16),
// which is a different shape from the engine's channel-count bookkeeping.
struct Ref {
	double out[ROWS][16];
	int channels[ROWS];
	double light[ROWS];
};

double refCurve(double p, bool exp) {
	if (!exp)
		return p;
	return std::pow(10.0, 3.0 * (p - 1.0)) * std::fmin(1.0, p / 0.05);
}

// Voltage of channel c of a cable as Rack reads it.
double refPoly(const float* v, int ch, int c) {
	return ch == 1 ? v[0] : c < ch ? v[c] : 0.0;
}

Ref reference(const Patch& p) {
	Ref ref;
	double carry[16] = {};  // dense
	int carryCh = 0;
	const float* ropeV = nullptr;
	int ropeCh = 0;
	for (int r = 0; r < ROWS; r++) {
		if (p.inCh[r] > 0) {
			ropeV = p.in[r];
			ropeCh = p.inCh[r];
		}
		const int n = ropeCh > 0 ? ropeCh : 1;
		double own[16] = {};
		double lightP = 0.0;
		for (int c = 0; c < n; c++) {
			double pos = double(p.level[r]) + refPoly(p.cv[r], p.cvCh[r], c) / 10.0;
			pos = pos < 0.0 ? 0.0 : pos > 1.0 ? 1.0 : pos;
			lightP = std::fmax(lightP, pos);
			own[c] = ropeCh > 0 ? refPoly(ropeV, ropeCh, c) * refCurve(pos, p.exp) : 0.0;
		}
		if (n == 1)  // a mono row is copied into every voice of a wider one
			for (int c = 1; c < 16; c++)
				own[c] = own[0];
		ref.channels[r] = std::max(n, carryCh);
		for (int c = 0; c < 16; c++)
			ref.out[r][c] = own[c] + carry[c];
		// Only a mono signal fills every voice; a wider one is 0 beyond its width.
		if (ref.channels[r] > 1)
			for (int c = ref.channels[r]; c < 16; c++)
				ref.out[r][c] = 0.0;
		ref.light[r] = lightP;
		if (p.outConn[r]) {
			carryCh = 0;
			for (double& x : carry)
				x = 0.0;
		} else {
			carryCh = ref.channels[r];
			for (int c = 0; c < 16; c++)
				carry[c] = ref.out[r][c];
		}
	}
	return ref;
}

// A fixed-seed LCG, as in src/fathom/test_engine.cpp.
struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float uniform() {  // [0, 1)
		s = s * 1664525u + 1013904223u;
		return float(s >> 8) / 16777216.f;
	}
	int below(int n) {
		return int(uniform() * float(n));
	}
};

// Random patches against the model: Levels (some outside [0, 1]), the curve,
// CVs, input and CV channel counts 0..16 with their jacks unplugged or not,
// output jacks. The Level is held, so a fresh engine's smoother has settled.
void testFuzzAgainstReference() {
	Lcg rng(20261115u);
	int bad = 0;
	for (int trial = 0; trial < 600; trial++) {
		Patch p;
		p.exp = rng.below(2);
		for (int r = 0; r < ROWS; r++) {
			const float l = rng.uniform();
			p.level[r] = l < 0.1f ? -0.3f : l > 0.9f ? 1.4f : rng.uniform();
			p.inCh[r] = rng.below(3) == 0 ? 0 : rng.below(17);
			p.cvCh[r] = rng.below(3) == 0 ? 0 : rng.below(17);
			p.outConn[r] = rng.below(2);
			for (int c = 0; c < 16; c++) {
				p.in[r][c] = (rng.uniform() * 2.f - 1.f) * 10.f;
				p.cv[r][c] = (rng.uniform() * 2.f - 1.f) * 8.f;
			}
		}
		rka::SluiceEngine e;
		rka::SluiceResult r;
		const F f = p.frame();
		for (int i = 0; i < 4; i++)
			e.process(f, FS, r);
		const Ref ref = reference(p);
		for (int row = 0; row < ROWS; row++) {
			bool ok = r.channels[row] == ref.channels[row] && std::fabs(r.light[row] - ref.light[row]) < 1e-6;
			for (int c = 0; ok && c < r.channels[row]; c++) {
				const double want = ref.out[row][c];
				ok = std::fabs(r.out[row][c] - want) <= 1e-5 + 1e-5 * std::fabs(want);
			}
			if (!ok) {
				for (int c = 0; c < r.channels[row]; c++)
					if (std::fabs(r.out[row][c] - ref.out[row][c]) > 1e-5 + 1e-5 * std::fabs(ref.out[row][c]))
						std::printf("   voice %d: %.9g want %.9g; light %.9g want %.9g\n", c, r.out[row][c], ref.out[row][c], r.light[row], ref.light[row]);
				if (bad++ < 5)
					std::printf("  trial %d row %d: channels %d (want %d), out[0] %.9g (want %.9g)\n", trial, row, r.channels[row],
					            ref.channels[row], r.out[row][0], ref.out[row][0]);
			}
		}
	}
	CHECK(bad == 0, "fuzz: %d rows differ from the reference", bad);
}

// Sample rates no host uses: the outputs stay finite, and the engine is back
// at the exact gain on returning to 48 kHz.
void testEdgeSampleRates() {
	const float rates[5] = {1.f, 768000.f, 0.f, -48000.f, -1.f};
	for (int k = 0; k < 5; k++) {
		for (int exp = 0; exp < 2; exp++) {
			Patch p;
			p.exp = exp;
			p.in[0][0] = 1.f;
			p.inCh[0] = 1;
			p.cv[0][0] = 1.f;
			p.cvCh[0] = 1;
			p.outConn[0] = true;
			p.level[0] = 0.25f;
			rka::SluiceEngine e;
			rka::SluiceResult r;
			F f = p.frame();
			e.process(f, FS, r);
			// Step the Level at the odd rate, so the smoother has work to do.
			f.params[F::LEVEL_A] = 0.9f;
			bool finite = true;
			for (int i = 0; i < 5000; i++) {
				e.process(f, rates[k], r);
				finite = finite && allFinite(r);
			}
			CHECK(finite, "%g Hz, exp=%d: an output was not finite", rates[k], exp);
			// Back at 48 kHz: 1 s settles any sane state to the target exactly.
			for (int i = 0; i < 48000; i++)
				e.process(f, FS, r);
			const double want = (exp ? std::pow(10.0, 3.0 * (1.0 - 1.0)) : 1.0) * 1.0;  // p = 0.9 + 0.1 = 1
			CHECK(r.out[0][0] == float(want), "after %g Hz, exp=%d: %.9g, want exactly %g", rates[k], exp, r.out[0][0], want);
			// And a step at 48 kHz still behaves.
			f.params[F::LEVEL_A] = 0.4f;
			for (int i = 0; i < 48000; i++)
				e.process(f, FS, r);
			const double want2 = refCurve(0.4 + 0.1, exp);
			CHECK(std::fabs(r.out[0][0] - want2) <= 1e-5 * want2, "after %g Hz, exp=%d, Level 0.4: %.9g, want %.9g", rates[k], exp,
			      r.out[0][0], want2);
		}
	}
}

void testNoAllocationInProcess() {
	static float in[ROWS][16], cv[ROWS][16];
	rka::SluiceEngine eng;
	rka::SluiceResult r;
	F f;
	const int chans[ROWS] = {16, 0, 7, 1};
	for (int r2 = 0; r2 < ROWS; r2++) {
		f.in[r2].v = in[r2];
		f.in[r2].channels = chans[r2];
		f.cv[r2].v = cv[r2];
		f.cv[r2].channels = r2 == 2 ? 1 : 16;
	}
	// Warm up once so one-time work (if any) is not counted.
	eng.process(f, FS, r);
	const long before = tAllocs;
	for (int i = 0; i < 48000; i++) {
		for (int k = 0; k < ROWS; k++)
			for (int c = 0; c < 16; c++) {
				in[k][c] = float(((i * 7 + c * 13 + k) % 200) - 100) * 0.1f;
				cv[k][c] = float(((i * 3 + c * 11 + k * 5) % 200) - 100) * 0.1f;
			}
		for (int k = 0; k < ROWS; k++)
			f.params[F::LEVEL_A + k] = float((i / 100 + k) % 11) * 0.1f;
		f.params[F::CURVE] = (i / 5000) % 2 ? 1.f : 0.f;
		for (int k = 0; k < ROWS; k++)
			f.outConnected[k] = ((i / 3000) >> k) & 1;
		eng.process(f, FS, r);
	}
	const long made = tAllocs - before;
	CHECK(made == 0, "process() made %ld heap allocations in 1 s", made);
}

void testExtremeValues() {
	const float big[2] = {1e30f, -1e30f};
	for (int s = 0; s < 2; s++)
		for (int exp = 0; exp < 2; exp++) {
			Patch p;
			p.exp = exp;
			for (int r = 0; r < ROWS; r++) {
				p.level[r] = 0.5f;
				for (int c = 0; c < 16; c++) {
					p.in[r][c] = big[(s + c) & 1];
					p.cv[r][c] = big[(s + r + c) & 1];
				}
				p.inCh[r] = 16;
				p.cvCh[r] = 16;
			}
			rka::SluiceEngine e;
			rka::SluiceResult r;
			const F f = p.frame();
			for (int i = 0; i < 500; i++)
				e.process(f, FS, r);
			CHECK(allFinite(r), "1e30: outputs not finite");
			for (int o = 0; o < ROWS; o++) {
				CHECK(r.light[o] >= 0.f && r.light[o] <= 1.f, "light %g outside [0, 1]", r.light[o]);
				for (int c = 0; c < r.channels[o]; c++)
					CHECK(std::fabs(r.out[o][c]) <= 4e30f, "out[%d][%d] = %g exceeds the sum of the inputs", o, c, r.out[o][c]);
			}
		}
	// Level and CV huge: gain stays in [0, 1], so a unit input stays in [0, 1].
	Patch p;
	p.in[0][0] = 1.f;
	p.inCh[0] = 1;
	p.outConn[0] = true;
	const float levels[4] = {1e30f, -1e30f, 1e30f, -1e30f};
	const float cvs[4] = {1e30f, 1e30f, -1e30f, -1e30f};
	for (int k = 0; k < 4; k++)
		for (int exp = 0; exp < 2; exp++) {
			p.exp = exp;
			p.level[0] = levels[k];
			p.cv[0][0] = cvs[k];
			p.cvCh[0] = 1;
			rka::SluiceEngine e;
			rka::SluiceResult r;
			const F f = p.frame();
			for (int i = 0; i < 500; i++)
				e.process(f, FS, r);
			CHECK(r.out[0][0] >= 0.f && r.out[0][0] <= 1.f, "level %g cv %g: out %g outside [0, 1]", levels[k], cvs[k], r.out[0][0]);
		}
}

} // namespace

int main() {
	testNanDoesNotTravel();
	testSmoothingFollowsSampleRate();
	testFuzzAgainstReference();
	testEdgeSampleRates();
	testNoAllocationInProcess();
	testExtremeValues();
	return rkatest::finish("sluice test_hostile");
}

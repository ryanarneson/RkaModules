// Fog's engine under hostile input, and its allocation behaviour: NaN and
// infinity on every input, CV and parameter, odd sample rates and a rate
// change over a ringing tail, a Size CV square wave, no heap allocation in
// process(), no denormals in a long silence, and a seeded fuzz of everything.
// Built with the sanitizers (tests.mk), so undefined behaviour and memory
// errors fail the run. Run with `make test`.
//
// Bound on the output: the wet limiter ends at 10 V, so with Mix at 1 (or dry
// kill) the output stays within 10.5 V. Below Mix 1 the dry adds on top, so
// the bound is 10.5 V plus the dry level fed in.
#include "FogEngine.hpp"
#include "test_measure.hpp"
#include "testing.hpp"

#include <climits>
#include <cmath>
#include <cstdint>
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

namespace {

using rka::FogEngine;
using rka::FogFrame;
using rka::FogResult;

const float FS = 48000.f;

uint32_t rng = 1u;
float uniform() { // [-1, 1)
	rng = rng * 1664525u + 1013904223u;
	return (float) (rng >> 8) / 8388608.f - 1.f;
}
float unit01() {
	return 0.5f * uniform() + 0.5f;
}

// An engine and a frame whose ports point at buffers the test fills.
struct Rig {
	FogEngine eng;
	FogFrame f;
	float vL[16] = {};
	float vR[16] = {};
	float cv[5][16] = {};

	explicit Rig(float rate = FS) {
		eng.setSampleRate(rate);
		f.inL.v = vL;
		f.inR.v = vR;
		for (int i = 0; i < 5; i++)
			f.cv[i].v = cv[i];
		f.params[FogFrame::VISIBILITY] = 0.5f;
		for (int p = FogFrame::SIZE; p <= FogFrame::DRIFT; p++)
			f.params[p] = 0.5f;
		f.params[FogFrame::BLOOM] = 0.f;
		f.params[FogFrame::MIX] = 1.f;
		f.inL.channels = 1;
	}
	FogResult step(float l) {
		vL[0] = l;
		FogResult r;
		eng.process(f, r);
		return r;
	}
};

bool finite2(const FogResult& r) {
	return std::isfinite(r.outL) && std::isfinite(r.outR);
}

// The spec-test setting for T60: Mix 1, no low cut, Bloom 0, Damp 0, Drift 0,
// Size at noon, Visibility v. A 10 V impulse into a reset engine, then T60 at
// 1 kHz (the decay does not depend on the band at Damp 0).
double impulseT60(Rig& r, float v) {
	r.f.params[FogFrame::VISIBILITY] = v;
	r.f.params[FogFrame::SIZE] = 0.5f;
	r.f.params[FogFrame::DAMP] = 0.f;
	r.f.params[FogFrame::DRIFT] = 0.f;
	r.f.params[FogFrame::BLOOM] = 0.f;
	r.f.params[FogFrame::MIX] = 1.f;
	r.f.lowCut = 0;
	r.f.mode = 0;
	r.eng.reset();
	const double t60 = 0.4 * std::pow(200.0, (double) v);
	const double secs = std::fmax(3.0, 0.68 * t60 + 2.0);
	const int n = (int) (secs * r.eng.hostRate);
	std::vector<float> ir((size_t) n);
	for (int i = 0; i < n; i++)
		ir[(size_t) i] = r.step(i == 0 ? 10.f : 0.f).outL;
	return fogtest::measureT60(ir, r.eng.hostRate, 1000.f);
}

double wantT60(float v) {
	return 0.4 * std::pow(200.0, (double) v);
}

// ---- NaN and infinity ----

void nanInputs() {
	const float bad[3] = {NAN, INFINITY, -INFINITY};
	const int MODES = 4;
	for (int k = 0; k < 3; k++) {
		// Where the bad value goes: 0 IN L, 1 IN R, 2..6 a CV, 7 every param,
		// 8 one param at a time (cycling), 9 everything at once.
		for (int where = 0; where < 10; where++) {
			Rig r;
			r.f.inR.channels = 1;
			for (int i = 0; i < 5; i++) {
				r.f.cv[i].channels = 1;
				r.cv[i][0] = 0.f;
			}
			for (int p = FogFrame::SIZE_ATT; p <= FogFrame::MIX_ATT; p++)
				r.f.params[p] = 1.f;
			r.f.mode = where % MODES;
			const int n = (int) FS;
			bool ok = true;
			for (int i = 0; i < n; i++) {
				const float in = 3.f * uniform();
				r.vR[0] = 3.f * uniform();
				const bool hit = where == 0 || where == 9;
				r.vL[0] = hit ? bad[(k + i) % 3] : in;
				if (where == 1 || where == 9)
					r.vR[0] = bad[(k + i) % 3];
				for (int c = 0; c < 5; c++)
					r.cv[c][0] = (where == 2 + c || where == 9) ? bad[(k + i + c) % 3] : 0.f;
				if (where == 7 || where == 9)
					for (int p = 0; p < FogFrame::PARAMS_LEN; p++)
						r.f.params[p] = bad[(k + p) % 3];
				if (where == 8) {
					for (int p = 0; p < FogFrame::PARAMS_LEN; p++)
						r.f.params[p] = 0.5f;
					r.f.params[(i / 100) % FogFrame::PARAMS_LEN] = bad[k];
				}
				if (where == 9) {
					r.f.mode = (i % 5) - 1; // -1 .. 3
					r.f.driftRate = i & 3;
					r.f.lowCut = (i % 7) - 2;
				}
				const FogResult o = r.step(r.vL[0]);
				ok = ok && finite2(o);
			}
			CHECK(ok, "bad value %d at %d: outputs not finite", k, where);
			// Back to normal: after one second of clean input, a fresh impulse
			// gives a non-zero, finite wet signal.
			for (int p = 0; p < FogFrame::PARAMS_LEN; p++)
				r.f.params[p] = 0.5f;
			r.f.params[FogFrame::MIX] = 1.f;
			r.f.params[FogFrame::BLOOM] = 0.f;
			for (int p = FogFrame::SIZE_ATT; p <= FogFrame::MIX_ATT; p++)
				r.f.params[p] = 0.f;
			for (int c = 0; c < 5; c++)
				r.cv[c][0] = 0.f;
			r.vR[0] = 0.f;
			r.f.inR.channels = 0;
			r.f.mode = 0;
			r.f.driftRate = 0;
			r.f.lowCut = 1;
			for (int i = 0; i < (int) FS; i++)
				finite2(r.step(0.f));
			double e = 0;
			bool fin = true;
			for (int i = 0; i < (int) FS; i++) {
				const FogResult o = r.step(i == 0 ? 10.f : 0.f);
				fin = fin && finite2(o);
				e += (double) o.outL * o.outL + (double) o.outR * o.outR;
			}
			CHECK(fin && e > 1e-6, "bad %d at %d: after recovery, wet energy %g (finite %d)", k, where, e, (int) fin);
		}
	}
}

// ---- sample rates ----

void edgeSampleRates() {
	Rig r;
	const float rates[] = {1.f, 8000.f, 22050.f, 44100.f, 192000.f, 384000.f, 768000.f};
	for (float rate : rates) {
		r.eng.setSampleRate(rate);
		CHECK(r.eng.hostRate == rate, "rate %g not taken: %g", rate, r.eng.hostRate);
		r.f.params[FogFrame::DRIFT] = 1.f;
		r.f.params[FogFrame::BLOOM] = 0.5f;
		bool ok = true;
		const int n = rate < 100.f ? 4000 : (int) (rate * 0.25f);
		for (int i = 0; i < n; i++)
			ok = ok && finite2(r.step(5.f * uniform()));
		CHECK(ok, "rate %g: outputs not finite", rate);
	}
	// 768 kHz itself is valid and divides by 8.
	r.eng.setSampleRate(768000.f);
	CHECK(r.eng.hostRate == 768000.f && r.eng.divider.factor() == 8, "768000: rate %g factor %d", r.eng.hostRate, r.eng.divider.factor());
	// The guard keeps the last good rate on bad ones.
	const float badRates[] = {0.f, -1.f, NAN, INFINITY, -INFINITY, 768001.f, 1e9f};
	r.eng.setSampleRate(44100.f);
	for (float rate : badRates) {
		r.eng.setSampleRate(rate);
		CHECK(r.eng.hostRate == 44100.f, "bad rate %g changed the rate to %g", rate, r.eng.hostRate);
		bool ok = true;
		for (int i = 0; i < 4410; i++)
			ok = ok && finite2(r.step(5.f * uniform()));
		CHECK(ok, "bad rate %g: outputs not finite", rate);
	}
	// And back at 48 kHz the decay is right again.
	r.eng.setSampleRate(48000.f);
	r.f.params[FogFrame::DRIFT] = 0.f;
	const float v = 0.3f;
	const double got = impulseT60(r, v), want = wantT60(v);
	CHECK(got > 0 && std::fabs(got / want - 1.0) < 0.15, "48 kHz after the sweep: T60 %g s, want %g s +/- 15 %%", got, want);
	std::printf("  edge_sample_rates: T60 at 48 kHz after the sweep %.3f s (want %.3f s)\n", got, want);
}

void rateChangeMidTail() {
	Rig r(44100.f);
	r.f.params[FogFrame::DRIFT] = 0.5f;
	r.f.params[FogFrame::VISIBILITY] = 0.6f;
	bool ok = true;
	auto run = [&](int n, bool noise) {
		for (int i = 0; i < n; i++)
			ok = ok && finite2(r.step(noise ? 5.f * uniform() : 0.f));
	};
	run(44100, true);
	run(4410, false); // a tail is ringing
	r.eng.setSampleRate(96000.f);
	run(9600, false);
	run(48000, true);
	run(9600, false);
	r.eng.setSampleRate(48000.f);
	run(4800, false);
	CHECK(ok, "rate change over a tail: outputs not finite");
	const float v = 0.3f;
	const double got = impulseT60(r, v), want = wantT60(v);
	CHECK(got > 0 && std::fabs(got / want - 1.0) < 0.15, "T60 at 48 kHz after 44.1/96/48 kHz: %g s, want %g s +/- 15 %%", got, want);
	std::printf("  rate_change_mid_tail: T60 at the final 48 kHz %.3f s (want %.3f s)\n", got, want);
}

// ---- Size CV ----

void sizeCvSweepIsSafe() {
	for (int driftRate = 0; driftRate < 2; driftRate++) {
		Rig r;
		r.f.cv[0].channels = 1;
		r.f.params[FogFrame::SIZE_ATT] = 1.f;
		r.f.params[FogFrame::DRIFT] = 1.f; // the Drift swing is at its widest
		r.f.params[FogFrame::VISIBILITY] = 0.8f;
		r.f.driftRate = driftRate;
		double peak = 0;
		bool ok = true;
		const int n = (int) (10.f * FS);
		for (int i = 0; i < n; i++) {
			// 10 Hz square, +/-5 V.
			r.cv[0][0] = ((i * 10 / (int) FS) & 1) ? -5.f : 5.f;
			if (((int) ((int64_t) i * 20 / (int) FS)) % 2 == 0)
				r.cv[0][0] = 5.f;
			else
				r.cv[0][0] = -5.f;
			const FogResult o = r.step(5.f * uniform());
			ok = ok && finite2(o);
			peak = std::fmax(peak, std::fmax(std::fabs(o.outL), std::fabs(o.outR)));
		}
		CHECK(ok, "Size CV square: outputs not finite");
		CHECK(peak <= 10.5, "Size CV square (drift rate %d): peak %g V", driftRate, peak);
		std::printf("  size_cv_sweep_is_safe: drift rate %d, peak %.3f V\n", driftRate, peak);
	}
}

// ---- allocation ----

void noAllocationInProcess() {
	for (int mode = 0; mode < 4; mode++) {
		Rig r;
		r.f.mode = mode;
		r.f.inR.channels = 1;
		for (int i = 0; i < 5; i++)
			r.f.cv[i].channels = 1;
		for (int p = FogFrame::SIZE_ATT; p <= FogFrame::MIX_ATT; p++)
			r.f.params[p] = 1.f;
		r.f.params[FogFrame::BLOOM] = 0.3f;
		r.f.params[FogFrame::DRIFT] = 0.7f;
		r.f.params[FogFrame::MIX] = 0.5f;
		const long before = tAllocs;
		bool ok = true;
		for (int i = 0; i < (int) (2 * FS); i++) {
			r.vR[0] = 5.f * uniform();
			for (int c = 0; c < 5; c++)
				r.cv[c][0] = 5.f * uniform();
			// Moving knobs and menu settings, and a mode change every half
			// second, are process() work too.
			r.f.params[FogFrame::VISIBILITY] = 0.5f + 0.5f * uniform();
			r.f.params[FogFrame::DAMP] = unit01();
			if (i % 24000 == 0)
				r.f.mode = (mode + i / 24000) % 4;
			r.f.driftRate = (i / 12000) & 1;
			r.f.lowCut = (i / 8000) % 3;
			ok = ok && finite2(r.step(5.f * uniform()));
		}
		const long allocs = tAllocs - before;
		CHECK(allocs == 0, "mode %d: %ld allocations in process()", mode, allocs);
		CHECK(ok, "mode %d: outputs not finite", mode);
	}
	// A sample-rate change does allocate (the buffers are sized for it): the
	// counter must be able to see that.
	Rig r;
	const long before = tAllocs;
	r.eng.setSampleRate(96000.f);
	CHECK(tAllocs > before, "the allocation counter saw nothing in setSampleRate");
}

// ---- denormals ----

// A float is fine when it is 0 or at least 1e-30 in magnitude.
template <typename T>
bool okValue(T x) {
	const T a = x < 0 ? -x : x;
	return a == 0 || a >= (T) 1e-30;
}

// Every stored value of the engine and its network, Bloom, low cut and mix.
long badState(const Rig& r) {
	long bad = 0;
	const rka::fog::Network& n = r.eng.net;
	auto scan = [&](const rka::DelayLine& d) {
		for (float x : d.buf)
			bad += !okValue(x);
	};
	for (int i = 0; i < rka::fog::Network::MAX_LINES; i++) {
		scan(n.line[i]);
		bad += !okValue(n.shelfZ[i]);
	}
	for (int s = 0; s < 4; s++)
		for (int i = 0; i < 8; i++)
			scan(n.diffuser.lines[s][i]);
	for (int i = 0; i < rka::fog::Network::LOOP_AP; i++)
		scan(n.loopAp[i]);
	for (const rka::fog::OctaveShifter* sh : {&n.shifter, &n.octL, &n.octR}) {
		scan(sh->buf);
		bad += !okValue(sh->lp1) + !okValue(sh->lp2) + !okValue(sh->hp1) + !okValue(sh->hp2);
	}
	for (int c = 0; c < 2; c++)
		for (int i = 0; i < 2; i++)
			bad += !okValue(r.eng.toneZ[c][i]);
	bad += !okValue(n.scaleOffset);
	for (int c = 0; c < 2; c++)
		for (int i = 0; i < 2; i++)
			bad += !okValue(r.eng.cutZ[c][i]);
	for (const rka::fog::Bloom* b : {&r.eng.bloomL, &r.eng.bloomR})
		bad += !okValue(b->level) + !okValue(b->gain);
	bad += !okValue(r.eng.mixSm) + !okValue(r.eng.fade);
	return bad;
}

void noDenormals() {
	// v = 1 (an 80 s tail, which does not reach 1e-30 in 120 s, so this
	// checks the state while it decays) and v = 0 (0.4 s, which does).
	const float vs[2] = {1.f, 0.f};
	for (int mode = 0; mode < 4; mode++) {
		for (float v : vs) {
			Rig r;
			r.f.mode = mode;
			r.f.params[FogFrame::VISIBILITY] = v;
			r.f.params[FogFrame::DRIFT] = 0.5f;
			r.f.params[FogFrame::DAMP] = 0.5f;
			r.f.params[FogFrame::MIX] = 0.5f; // the dry is 0 in silence
			long badOut = 0, badSt = 0;
			double lastRms = 0;
			const int noise = (int) (2 * FS), silence = (int) (120 * FS);
			for (int i = 0; i < noise + silence; i++) {
				const FogResult o = r.step(i < noise ? 5.f * uniform() : 0.f);
				badOut += !okValue(o.outL) + !okValue(o.outR);
				if (i % 9600 == 0 || i == noise + silence - 1)
					badSt += badState(r);
				if (i == noise + silence - 1)
					lastRms = std::fabs(o.outL);
			}
			CHECK(badOut == 0, "mode %d v %g: %ld output samples are denormal", mode, v, badOut);
			CHECK(badSt == 0, "mode %d v %g: %ld state values are denormal", mode, v, badSt);
			if (mode == 0)
				std::printf("  no_denormals: mode 0 v %g: %ld bad output samples, %ld bad state values, last |out| %g\n", v, badOut, badSt, lastRms);
		}
	}
}

// ---- fuzz ----

void fuzz() {
	const int TRIALS = 200;
	const float rates[] = {44100.f, 48000.f, 96000.f, 192000.f};
	Rig r;
	long samples = 0;
	double worstOver = -1e9;
	for (int t = 0; t < TRIALS; t++) {
		const float rate = rates[(unsigned) (unit01() * 3.99f)];
		r.eng.setSampleRate(rate);
		FogFrame& f = r.f;
		auto knob = [&]() {
			const float u = unit01();
			return u < 0.1f ? 0.f : u > 0.9f ? 1.f : unit01();
		};
		for (int p = 0; p < FogFrame::PARAMS_LEN; p++)
			f.params[p] = p >= FogFrame::SIZE_ATT ? 2.f * unit01() - 1.f : knob();
		f.mode = (int) (unit01() * 3.99f);
		f.driftRate = unit01() > 0.5f;
		f.lowCut = (int) (unit01() * 2.99f);
		f.dryKill = unit01() > 0.8f;
		f.inL.channels = (int) (unit01() * 16.99f);
		f.inR.channels = (int) (unit01() * 16.99f);
		for (int c = 0; c < 5; c++)
			f.cv[c].channels = (int) (unit01() * 16.99f);
		const float amp = unit01() < 0.2f ? 10.f : 5.f;
		const int n = (int) (0.5f * rate);
		r.eng.reset(); // the Mix ramp starts at its target
		bool wetOnly = false;
		for (int i = 0; i < n; i++) {
			for (int c = 0; c < 16; c++) {
				r.vL[c] = amp * uniform();
				r.vR[c] = amp * uniform();
				for (int k = 0; k < 5; k++)
					r.cv[k][c] = 10.f * uniform();
			}
			// Mix is 1 (or dry kill) when the knob plus its CV reads 1 now; the
			// 5 ms ramp otherwise lags a CV that jumps every sample, so the
			// wet-only bound holds only while Mix has sat at 1.
			const bool at1 = f.dryKill || FogEngine::knob(f, FogFrame::MIX, 4, FogFrame::MIX_ATT) >= 1.f;
			wetOnly = at1 && (i == 0 ? true : wetOnly);
			FogResult o;
			r.eng.process(f, o);
			samples++;
			CHECK(finite2(o), "trial %d sample %d: output %g, %g", t, i, o.outL, o.outR);
			// Mix 1 or dry kill: the wet alone. Else the dry adds on top; the
			// dry gain is at most 1.
			const float dryL = f.inL.sum();
			const float dryR = f.inR.channels > 0 ? f.inR.sum() : dryL;
			const float boundL = wetOnly ? 10.5f : 10.5f + std::fabs(dryL);
			const float boundR = wetOnly ? 10.5f : 10.5f + std::fabs(dryR);
			const double overL = std::fabs(o.outL) - boundL, overR = std::fabs(o.outR) - boundR;
			worstOver = std::fmax(worstOver, std::fmax(overL, overR));
			if (overL > 0 || overR > 0) {
				CHECK(false, "trial %d sample %d: out %g, %g over the bound %g, %g (wet only %d)", t, i, o.outL, o.outR, boundL, boundR, (int) wetOnly);
				break;
			}
			if (!finite2(o))
				break;
		}
	}
	std::printf("  fuzz: %d trials, %ld samples, worst margin under the bound %.3f V\n", TRIALS, samples, -worstOver);
}

} // namespace

int main() {
	nanInputs();
	edgeSampleRates();
	rateChangeMidTail();
	sizeCvSweepIsSafe();
	noAllocationInProcess();
	noDenormals();
	fuzz();
	return rkatest::finish("fog test_hostile");
}

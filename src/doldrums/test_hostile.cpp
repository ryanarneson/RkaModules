// Doldrums' engine under hostile conditions (TESTING.md rules 4, 6 and 8; the
// plan's Review Focus 1 and 4), built with the address and undefined-
// behaviour sanitizers and counting allocators (operator new and delete, and
// every malloc and free through the sanitizer's hooks) and counting locks
// (pthread_mutex_lock and trylock, and std::mutex's lock and try_lock):
//
// - fuzz: seeded fuzz of every param, CV, gate, input and menu setting
//   together (both settings of "At the end of a pass", the three grain
//   sizes, Wide on and off), NaN, +-inf, +-1e30 and denormal samples and
//   CVs, 16 V/OCT channels at extremes (roots above Nyquist and far under
//   16 Hz), channel counts and grain sizes out of range, every param outside
//   its range, resets, new seeds, rate changes between 44.1 and 768 kHz
//   (allocate(rate) then setSampleRate(rate), sometimes with samples
//   between) and rates the engine cannot use. Nothing here assumes when a
//   pass starts or what sounds: what must hold every sample is a finite
//   output within the soft bound's ceiling (10 V), RUN at 0 or 10 V, the
//   anchor and the last slice inside the tape, and no heap operation or
//   lock inside process(). The tape is fenced (its guard frames poisoned for
//   ASan), so a read of a slice outside it is a report.
// - instances_independent: an engine run beside another fed the fuzz makes
//   the same output to the bit as one run alone; another seed, another
//   output.
// - nan_reads_as_zero_volts_on_the_tape: NaN, +-inf and -0 on every door
//   give the output, to the bit, of 0 V there.
// - position_cv_at_audio_rate (Review Focus 1): Position and Stretch by CV
//   at audio rate, stepped, huge and NaN, with the attenuverters at either
//   end, frozen and recording, in both settings, at each grain size.
// - hot_sine_on_a_tooth (Review Focus 4): a +-40 V sine on a tooth of the
//   narrowest comb at full Harmonics: no NaN, the output held under the soft
//   bound's ceiling.
// - empty_comb_is_silent (Review Focus 4): 16 roots all above Nyquist, or all
//   under 16 Hz: exact silence, not NaN.
// - dc_in: DC in, no DC out (the 16 Hz cut).
#include "doldrums/DoldrumsEngine.hpp"
#include "hadal/Random.hpp"
#include "testing.hpp"

#include <sanitizer/asan_interface.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <pthread.h>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif

// Heap operations by anyone, counted; Host::step takes the difference across
// process(). One thread, so plain counters.
static long gHeapOps = 0;
static long gLocks = 0;

void* operator new(std::size_t n) {
	gHeapOps++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	if (p)
		gHeapOps++;
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	if (p)
		gHeapOps++;
	std::free(p);
}

// malloc, calloc and free, seen by the sanitizer's allocator (declared here:
// not every compiler installs <sanitizer/allocator_interface.h>).
extern "C" int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void*, size_t),
                                                         void (*free_hook)(const volatile void*));
static void mallocHook(const volatile void*, size_t) {
	gHeapOps++;
}
static void freeHook(const volatile void* p) {
	if (p)
		gHeapOps++;
}

// Locks. On macOS this file defines pthread_mutex_lock and trylock itself,
// as libm_calls does the maths functions: ld binds the test's own calls
// (the engine is header-only, so all of its code is the test's) to these
// before libSystem's, and they count and forward. libc++'s std::mutex::lock
// lives in the library, whose calls do not come here, so it is defined here
// too. On Linux tests.mk links with --wrap for the two pthread calls, which
// libstdc++'s inline std::mutex reaches.
#if defined(__APPLE__)
extern "C" int pthread_mutex_lock(pthread_mutex_t* m) {
	static int (*real)(pthread_mutex_t*) = reinterpret_cast<int (*)(pthread_mutex_t*)>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
	gLocks++;
	return real(m);
}
extern "C" int pthread_mutex_trylock(pthread_mutex_t* m) {
	static int (*real)(pthread_mutex_t*) = reinterpret_cast<int (*)(pthread_mutex_t*)>(dlsym(RTLD_NEXT, "pthread_mutex_trylock"));
	gLocks++;
	return real(m);
}
#if defined(_LIBCPP_VERSION)
namespace std {
inline namespace __1 {
void mutex::lock() {
	static void (*real)(mutex*) = reinterpret_cast<void (*)(mutex*)>(dlsym(RTLD_NEXT, "_ZNSt3__15mutex4lockEv"));
	gLocks++;
	real(this);
}
bool mutex::try_lock() noexcept {
	static bool (*real)(mutex*) = reinterpret_cast<bool (*)(mutex*)>(dlsym(RTLD_NEXT, "_ZNSt3__15mutex8try_lockEv"));
	gLocks++;
	return real(this);
}
} // namespace __1
} // namespace std
#endif
#else
extern "C" int __real_pthread_mutex_lock(pthread_mutex_t* m);
extern "C" int __real_pthread_mutex_trylock(pthread_mutex_t* m);
extern "C" int __wrap_pthread_mutex_lock(pthread_mutex_t* m) {
	gLocks++;
	return __real_pthread_mutex_lock(m);
}
extern "C" int __wrap_pthread_mutex_trylock(pthread_mutex_t* m) {
	gLocks++;
	return __real_pthread_mutex_trylock(m);
}
#endif

// GCC's ASan on Linux keeps a fake stack for every frame by default; the
// engine returns no stack addresses.
extern "C" const char* __asan_default_options() {
	return "detect_stack_use_after_return=0";
}

namespace {

using namespace rka::doldrums;
using rka::hadal::Random;
typedef DoldrumsFrame F;

const float NaN = std::nanf("");
const float INF = INFINITY;
const double kPi = 3.14159265358979323846;
// The soft bound's ceiling (SoftBound.hpp: 8 V, then tanh-shaped into 10 V).
const float CEILING = 10.f;

// Values no input should ever be given.
const float JUNK[13] = {NaN, INF, -INF, 1e30f, -1e30f, 3.4e38f, -3.4e38f, 1e-40f, -1e-40f, -0.f, 1e6f, -1e6f, 100.f};

// ---- One engine, fenced, its frame, and what was seen.
struct Host {
	std::unique_ptr<DoldrumsEngine> e;
	F f;
	DoldrumsResult r;
	float rate = 0.f;
	float* voct;   // its own 16-float heap block, so a read past it is a report
	long long t = 0;
	long heap = 0, locks = 0, insane = 0;
	std::string why;
	// Coverage.
	long stopSamples = 0, roundSamples = 0, wideSamples = 0, narrowSamples = 0, followSamples = 0, followingSamples = 0;
	long sized[3] = {0, 0, 0};   // samples heard at each grain size (the menu's, where the sizes differ)
	long frozenSamples = 0, heldSamples = 0, stoppedSamples = 0, runHigh = 0, runLow = 0;
	double peak = 0.0;

	explicit Host(float rate_ = 48000.f, uint32_t seed = 1) : e(new DoldrumsEngine), voct(new float[16]) {
		for (int c = 0; c < 16; c++) voct[c] = 0.f;
		f.params[F::MIX] = 1.f;        // all wet, as these tests were written (the default moved to 0.5 at 2.0.49)
		f.params[F::POSITION] = 1.f;   // Position at now, as these tests were written (the default moved to 0.9 at 2.0.46; its own effect is test_engine's default_position_freezes_a_stretch)
		e->seed(seed);
		use(rate_);
	}
	~Host() {
		fence(false);
		delete[] voct;
	}
	Host(const Host&) = delete;
	Host& operator=(const Host&) = delete;

	// The tape's guard frames (Ring::GUARD either side of each channel).
	void fence(bool on) {
		Ring& ring = e->ring();
		for (int c = 0; c < 2; c++) {
			const float* ch = ring.channel(c);
			if (!ch) continue;
			const float* lo = ch - Ring::GUARD;
			const float* hi = ch + ring.capacity();
			const size_t bytes = size_t(Ring::GUARD) * sizeof(float);
			if (on) {
				ASAN_POISON_MEMORY_REGION(lo, bytes);
				ASAN_POISON_MEMORY_REGION(hi, bytes);
			} else {
				ASAN_UNPOISON_MEMORY_REGION(lo, bytes);
				ASAN_UNPOISON_MEMORY_REGION(hi, bytes);
			}
		}
	}
	// Off the audio thread, as the module's onSampleRateChange: allocate,
	// then adopt. The fence is down while they run (a new tape is cleared
	// whole, guard frames too) and up while process() does. Each also
	// collects and frees the rings the engine has handed back, as the
	// module's worker does (a frozen tape's ring is kept as the source of a
	// conversion, which nobody makes here, until a thaw or reset).
	void collect() {
		while (Ring* old = e->retired()) delete old;
	}
	bool allocate(float fs) {
		fence(false);
		collect();
		const bool ok = e->allocate(fs);
		fence(true);
		return ok;
	}
	void setSampleRate(float fs) {
		fence(false);
		e->setSampleRate(fs);
		collect();
		fence(true);
	}
	void reset() {
		fence(false);
		e->reset();
		collect();
		fence(true);
	}
	bool use(float fs) {
		if (!allocate(fs)) return false;
		setSampleRate(fs);
		rate = fs;
		return true;
	}

	void fail(const char* what) {
		if (insane++ == 0) {
			char b[400];
			std::snprintf(b, sizeof b, "sample %lld at %.0f Hz: %s (out %g %g, run %g; now %lld cap %lld anchor %lld end %lld grain %d)", t,
			              rate, what, r.outL, r.outR, r.run, e->state().now, e->state().capacity, e->state().anchor,
			              e->state().end, e->state().grain);
			why = b;
		}
	}

	void step() {
		const long h0 = gHeapOps, l0 = gLocks;
		e->process(f, r);
		heap += gHeapOps - h0;
		locks += gLocks - l0;
		t++;
		check();
	}
	void run(long long n) {
		for (long long i = 0; i < n; i++) step();
	}

	// What must hold after every sample.
	void check() {
		if (!(std::fabs(r.outL) <= CEILING && std::fabs(r.outR) <= CEILING))
			return fail("an output is not finite or over the soft bound's ceiling");
		if (!(r.run == 0.f || r.run == 10.f))
			return fail("RUN is neither 0 nor 10 V");
		peak = std::max(peak, double(std::max(std::fabs(r.outL), std::fabs(r.outR))));
		const DoldrumsState& s = e->state();
		if (s.grain > 0 && (e->staged() & DoldrumsEngine::STAGED_BOUNDARY) && s.capacity >= s.grain) {
			// The pass was stepped on this sample: the anchor at least the
			// shortest grain inside the tape, the last slice no newer than now
			// (and every slice copied inside the fenced ring, or ASan says so).
			const long long shortest = grainFrames(GRAIN_SHORT, rate);
			if (!(s.anchor >= s.now - s.capacity + shortest && s.anchor <= s.now)) return fail("the anchor is off the tape");
			if (!(s.end <= s.now)) return fail("the slice ends after now");
			// While recording: Follow, recording and sounding: on the flag, never held.
			if (s.following && (s.held || s.stopped || s.end != s.anchor || !f.follow)) return fail("following, but held, stopped or off the flag");
		}
		(f.stopAtEnd ? stopSamples : roundSamples)++;
		(f.wide ? wideSamples : narrowSamples)++;
		followSamples += f.follow;
		followingSamples += s.following;
		if (s.grain > 0)
			for (int g = 0; g < 3; g++)
				if (s.grain == grainFrames(GrainSize(g), rate) && (g == std::max(0, std::min(f.grainSize, 2)))) sized[g]++;
		if (r.frozen) frozenSamples++;
		if (s.held) heldSamples++;
		if (s.stopped) stoppedSamples++;
		(r.run > 5.f ? runHigh : runLow)++;
	}

	bool sane(const char* what) {
		CHECK(insane == 0, "%s: %ld bad samples; the first: %s", what, insane, why.c_str());
		CHECK(heap == 0, "%s: %ld heap operations inside process()", what, heap);
		CHECK(locks == 0, "%s: %ld locks inside process()", what, locks);
		return insane == 0 && heap == 0 && locks == 0;
	}
};

// ---- The fuzz's choices.

float junk(Random& g) {
	return JUNK[g.next() % 13];
}
// lo..hi mostly, its ends sometimes, outside it or junk otherwise.
float param(Random& g, float lo, float hi) {
	const uint32_t k = g.next() % 20;
	if (k < 12) return lo + (hi - lo) * g.unit();
	if (k < 14) return k == 12 ? lo : hi;
	if (k < 16) return k == 14 ? lo - (hi - lo) * 10.f * g.unit() : hi + (hi - lo) * 10.f * g.unit();
	return junk(g);
}

// How a voltage moves within a segment.
struct Signal {
	enum Kind { CONST, RANDOM, SQUARE, SINE, SPIKES, NOISE, DENORMAL, KINDS };
	int kind = CONST;
	float a = 0.f, b = 0.f;   // levels
	long period = 1;
	double hz = 0.0;
	float value(long long t, Random& g, float rate) const {
		switch (kind) {
		case CONST: return a;
		case RANDOM: return a + (b - a) * g.unit();
		case SQUARE: return (t / period) % 2 ? b : a;
		case SINE: return float(a * std::sin(2.0 * kPi * hz * double(t) / rate));
		case SPIKES: return g.next() % 64 == 0 ? junk(g) : a * g.uniform();
		case NOISE: return a * g.uniform();
		default: return (t & 1) ? 1e-40f : -1e-40f;
		}
	}
	static Signal pick(Random& g, float lo, float hi, float rate) {
		Signal s;
		s.kind = int(g.next() % KINDS);
		s.a = g.next() % 5 == 0 ? junk(g) : lo + (hi - lo) * g.unit();
		s.b = g.next() % 5 == 0 ? junk(g) : lo + (hi - lo) * g.unit();
		const long periods[6] = {1, 2, 7, 64, 1000, 20000};
		s.period = periods[g.next() % 6];
		s.hz = (0.5 + 0.49 * g.unit()) * (g.next() % 2 ? rate : 100.f);   // past Nyquist or slow
		if (s.kind == SINE && !(s.a > -1e3f && s.a < 1e3f)) s.a = 10.f;
		return s;
	}
};

struct Segment {
	Signal inL, inR, stretch, position, harmonics, gate, trig;
	Signal v[16];
};

const float RATES[8] = {44100.f, 48000.f, 88200.f, 96000.f, 176400.f, 192000.f, 384000.f, 768000.f};

// A new set of choices for the frame: each param with chance 1/3, the menu,
// the patching, and how every voltage moves.
void choose(Host& h, Segment& s, Random& g) {
	F& f = h.f;
	static const float LO[F::kParams] = {0, -1, 0, -1, 0, -1, 0, -36, -1, -24, 1, 0, 0, 0};
	static const float HI[F::kParams] = {1, 1, 1, 1, 1, 1, 1, 36, 1, 24, 32, 1, 1, 1};
	for (int p = 0; p < F::kParams; p++)
		if (g.next() % 3 == 0) f.params[p] = p == F::FREEZE && g.next() % 2 ? float(g.next() % 2) : param(g, LO[p], HI[p]);
	if (g.next() % 3 == 0) {
		const int odd[5] = {-1, 3, 99, INT_MIN, INT_MAX};
		f.grainSize = g.next() % 8 ? int(g.next() % 3) : odd[g.next() % 5];
	}
	if (g.next() % 3 == 0) f.wide = g.next() % 2;
	if (g.next() % 3 == 0) f.stopAtEnd = g.next() % 2;
	if (g.next() % 3 == 0) f.follow = g.next() % 2;   // While recording: Drift or Follow
	if (g.next() % 4 == 0) f.inRPatched = g.next() % 2;
	if (g.next() % 3 == 0) {
		const int odd[6] = {-1, -1000, 17, 1000000, INT_MAX, INT_MIN};
		f.voctChannels = g.next() % 6 ? int(g.next() % 17) : odd[g.next() % 6];
		f.voct = g.next() % 10 ? h.voct : nullptr;
	}
	s.inL = Signal::pick(g, -12.f, 12.f, h.rate);
	s.inR = Signal::pick(g, -12.f, 12.f, h.rate);
	s.stretch = Signal::pick(g, -12.f, 12.f, h.rate);
	s.position = Signal::pick(g, -12.f, 12.f, h.rate);
	s.harmonics = Signal::pick(g, -12.f, 12.f, h.rate);
	s.gate = Signal::pick(g, -1.f, 11.f, h.rate);
	s.trig = Signal::pick(g, -1.f, 11.f, h.rate);
	for (int c = 0; c < 16; c++) s.v[c] = Signal::pick(g, -12.f, 12.f, h.rate);   // +-12 V: roots past Nyquist, and far under 16 Hz
}

void apply(Host& h, const Segment& s, Random& g) {
	F& f = h.f;
	const long long t = h.t;
	f.inL = s.inL.value(t, g, h.rate);
	f.inR = s.inR.value(t, g, h.rate);
	f.stretchCv = s.stretch.value(t, g, h.rate);
	f.positionCv = s.position.value(t, g, h.rate);
	f.harmonicsCv = s.harmonics.value(t, g, h.rate);
	f.freezeGate = s.gate.value(t, g, h.rate);
	f.trig = s.trig.value(t, g, h.rate);
	for (int c = 0; c < 16; c++) h.voct[c] = s.v[c].value(t, g, h.rate);
}

// One seed's fuzz: segments of 1 to 6000 samples (one in eight up to 60000,
// for whole passes at the Long grain), each with new choices, and between
// them, now and then, a rate change, a bad rate, a reset or a new seed.
// Returns the rate changes made.
long fuzzSeed(uint32_t seed, long long samples, Host& h) {
	long changes = 0;
	Random g;
	g.seed(seed);
	Segment s;
	choose(h, s, g);
	while (h.t < samples) {
		const uint32_t k = g.next() % 64;
		if (k < 3) {
			const float fs = RATES[g.next() % 8];
			if (g.next() % 3 == 0) {
				// allocate, some samples (no sound of its own), then adopt.
				CHECK(h.allocate(fs), "allocate(%g)", fs);
				const long n = long(g.next() % 2000);
				for (long i = 0; i < n; i++) {
					apply(h, s, g);
					h.step();
				}
				h.setSampleRate(fs);
				h.rate = fs;
			} else
				CHECK(h.use(fs), "rate %g", fs);
			changes++;
		} else if (k == 3) {
			const float bad[6] = {0.f, -48000.f, NaN, INF, 7999.f, 1e9f};
			const float fs = bad[g.next() % 6];
			CHECK(!h.allocate(fs), "allocate(%g) refused", fs);
			h.setSampleRate(fs);        // not allocated: nothing
			h.setSampleRate(12345.f);   // a usable rate, but not the one allocated: nothing
		} else if (k == 4)
			h.reset();
		else if (k == 5)
			h.e->seed(g.next());
		choose(h, s, g);
		const long long n = 1 + (g.next() % 8 ? g.next() % 6000 : g.next() % 60000);
		for (long long i = 0; i < n && h.t < samples; i++) {
			apply(h, s, g);
			h.step();
		}
	}
	return changes;
}

void fuzz() {
	const uint32_t seeds[3] = {1, 2, 3};
	struct {
		long stopSamples = 0, roundSamples = 0, wideSamples = 0, narrowSamples = 0, followSamples = 0, followingSamples = 0, sized[3] = {0, 0, 0};
		long frozenSamples = 0, heldSamples = 0, stoppedSamples = 0, runHigh = 0, runLow = 0, rateChanges = 0;
		double peak = 0.0;
	} total;
	for (uint32_t seed : seeds) {
		Host h(48000.f, seed);
		total.rateChanges += fuzzSeed(seed, 2000000, h);
		char what[64];
		std::snprintf(what, sizeof what, "fuzz seed %u", seed);
		h.sane(what);
		total.stopSamples += h.stopSamples;
		total.roundSamples += h.roundSamples;
		total.wideSamples += h.wideSamples;
		total.narrowSamples += h.narrowSamples;
		total.followSamples += h.followSamples;
		total.followingSamples += h.followingSamples;
		for (int g = 0; g < 3; g++) total.sized[g] += h.sized[g];
		total.frozenSamples += h.frozenSamples;
		total.heldSamples += h.heldSamples;
		total.stoppedSamples += h.stoppedSamples;
		total.runHigh += h.runHigh;
		total.runLow += h.runLow;
		total.peak = std::max(total.peak, h.peak);
	}
	// The fuzz reached what it is meant to.
	CHECK(total.stopSamples > 0 && total.roundSamples > 0, "fuzz: both end-of-pass settings (stop %ld, go round %ld samples)",
	      total.stopSamples, total.roundSamples);
	CHECK(total.wideSamples > 0 && total.narrowSamples > 0, "fuzz: Wide on and off (%ld, %ld)", total.wideSamples, total.narrowSamples);
	CHECK(total.followSamples > 100000 && total.followSamples < 5800000 && total.followingSamples > 10000,
	      "fuzz: While recording, Follow for %ld samples (following for %ld), Drift for the rest", total.followSamples, total.followingSamples);
	CHECK(total.sized[0] > 0 && total.sized[1] > 0 && total.sized[2] > 0, "fuzz: each grain size heard (%ld, %ld, %ld samples)",
	      total.sized[0], total.sized[1], total.sized[2]);
	CHECK(total.frozenSamples > 0 && total.heldSamples > 0 && total.stoppedSamples > 0, "fuzz: frozen %ld, held %ld, stopped %ld samples",
	      total.frozenSamples, total.heldSamples, total.stoppedSamples);
	CHECK(total.runHigh > 0 && total.runLow > 0, "fuzz: RUN high %ld, low %ld samples", total.runHigh, total.runLow);
	CHECK(total.rateChanges > 20, "fuzz: %ld rate changes", total.rateChanges);
	CHECK(total.peak > 8.f, "fuzz: the soft bound was reached (peak %.2f V)", total.peak);
	std::printf("fuzz: 3 seeds x 2e6 samples, %ld rate changes, peak |out| %.3f V (ceiling %.0f V)\n", total.rateChanges,
	            total.peak, CEILING);
}

// ---- Instances are independent (rule 8).

// A patch a person might make: noise and a tone, Freeze tapped, Stretch and
// Position moving, the comb on, TRIG now and then.
void ordinary(Host& h, long long t) {
	F& f = h.f;
	const double s = double(t) / 48000.0;
	f.inL = float(3.0 * std::sin(2.0 * kPi * 220.0 * s) + 0.5 * std::sin(2.0 * kPi * 1771.0 * s));
	f.inR = float(2.0 * std::sin(2.0 * kPi * 330.0 * s));
	f.inRPatched = true;
	f.params[F::FREEZE] = (t / 40000) % 2 ? 1.f : 0.f;
	f.params[F::HARMONICS] = 0.6f;
	f.params[F::STRETCH_ATT] = 0.5f;
	f.stretchCv = float(5.0 * std::sin(2.0 * kPi * 0.3 * s));
	f.params[F::POSITION_ATT] = 0.04f;   // Position 0.98 to 1: at most 0.6 s back
	f.positionCv = float(-2.5 - 2.5 * std::sin(2.0 * kPi * 0.11 * s));
	f.trig = (t % 70000) < 20 ? 10.f : 0.f;
	f.voct = h.voct;
	f.voctChannels = 3;
	h.voct[0] = 0.f;
	h.voct[1] = 0.25f;
	h.voct[2] = float(7.0 / 12.0);
}

void instances_independent() {
	const long long n = 4 * 48000;
	Host alone(48000.f, 11);
	std::vector<float> a;
	for (long long t = 0; t < n; t++) {
		ordinary(alone, t);
		alone.step();
		a.push_back(alone.r.outL);
		a.push_back(alone.r.outR);
	}
	// The same beside one fed the fuzz's choices, sample by sample.
	Host same(48000.f, 11), other(48000.f, 12);
	Random g;
	g.seed(99);
	Segment s;
	choose(other, s, g);
	bool equal = true;
	for (long long t = 0; t < n; t++) {
		if (t % 3000 == 0) choose(other, s, g);
		apply(other, s, g);
		other.step();
		ordinary(same, t);
		same.step();
		equal = equal && same.r.outL == a[size_t(2 * t)] && same.r.outR == a[size_t(2 * t + 1)];
	}
	double energy = 0.0;
	for (float x : a) energy += double(x) * x;
	CHECK(energy > 0.0, "instances_independent: the patch sounds");
	CHECK(equal, "an engine beside another makes the same output, to the bit, as one alone");
	same.sane("instances_independent");
	other.sane("instances_independent (the other)");
	// Another seed: other phases, another output.
	Host seeded(48000.f, 13);
	long differ = 0;
	for (long long t = 0; t < n; t++) {
		ordinary(seeded, t);
		seeded.step();
		differ += seeded.r.outL != a[size_t(2 * t)];
	}
	CHECK(differ > n / 2, "another seed gives another output (%ld of %lld samples differ)", differ, n);
}

// ---- NaN, infinity and -0 read as 0 V (the plan's Global Constraints).

// Two engines, the same seed and the same patch, sample for sample; at
// scattered samples one is given NaN, +inf, -inf or -0 on a door and the
// other 0 V there. Every door: IN L, IN R (patched), the three CVs (the
// attenuverters open), the Freeze gate, TRIG and a V/OCT channel (Harmonics
// up, so the comb reads it). At Mix 1 the output is the wet sound alone, so
// what the tape recorded and what the controls read are all that differ:
// the outputs must be equal to the bit. Recording, and frozen with a pass
// travelling (Position 0.5).
void nan_reads_as_zero_volts_on_the_tape() {
	const float bad[4] = {NaN, INF, -INF, -0.f};
	for (int frozen = 0; frozen < 2; frozen++) {
		Host a(48000.f, 51), z(48000.f, 51);
		for (Host* h : {&a, &z}) {
			F& f = h->f;
			f.inRPatched = true;
			f.params[F::FREEZE] = float(frozen);
			f.params[F::POSITION] = frozen ? 0.5f : 1.f;
			f.params[F::STRETCH_ATT] = 0.3f;
			f.params[F::POSITION_ATT] = 0.05f;
			f.params[F::HARMONICS] = 0.5f;
			f.params[F::HARMONICS_ATT] = 0.5f;
			f.voct = h->voct;
			f.voctChannels = 2;
		}
		if (frozen)   // a tape of noise and a tone to travel through
			for (Host* h : {&a, &z}) {
				Random n;
				n.seed(8);
				Ring& ring = h->e->ring();
				for (long long k = 0; k < ring.capacity(); k++) {
					const float x = float(2.0 * std::sin(2.0 * kPi * 440.0 * double(k) / 48000.0));
					ring.write(x + n.uniform(), x - n.uniform(), false);
				}
			}
		Random g;
		g.seed(77);
		const long long n = 6 * 48000;
		long hostile = 0, differ = 0;
		double energy = 0.0;
		for (long long t = 0; t < n; t++) {
			const double s = double(t) / 48000.0;
			float v[9];
			v[0] = float(3.0 * std::sin(2.0 * kPi * 220.0 * s)) + 0.5f * g.uniform();         // IN L
			v[1] = float(2.0 * std::sin(2.0 * kPi * 331.0 * s)) + 0.5f * g.uniform();         // IN R
			v[2] = float(4.0 * std::sin(2.0 * kPi * 0.4 * s));                                // Stretch CV
			v[3] = float(-4.0 + 3.0 * std::sin(2.0 * kPi * 0.3 * s));                          // Position CV
			v[4] = float(3.0 * std::sin(2.0 * kPi * 0.2 * s));                                // Harmonics CV
			v[5] = frozen ? 0.f : ((t % 60000) < 2000 ? 10.f : 0.f);                         // Freeze gate
			v[6] = (t % 20000) < 200 ? 10.f : 0.f;                                            // TRIG
			v[7] = float(0.5 * std::sin(2.0 * kPi * 0.25 * s));                               // V/OCT 1
			v[8] = 7.f / 12.f;                                                                 // V/OCT 2
			float w[9];
			std::memcpy(w, v, sizeof v);
			float zv[9];
			std::memcpy(zv, v, sizeof v);
			for (int d = 0; d < 8; d++)
				if (g.next() % 16 == 0) {
					w[d] = bad[g.next() % 4];
					zv[d] = 0.f;
					hostile++;
				}
			float* sets[2] = {w, zv};
			Host* hs[2] = {&a, &z};
			for (int k = 0; k < 2; k++) {
				F& f = hs[k]->f;
				const float* x = sets[k];
				f.inL = x[0];
				f.inR = x[1];
				f.stretchCv = x[2];
				f.positionCv = x[3];
				f.harmonicsCv = x[4];
				f.freezeGate = x[5];
				f.trig = x[6];
				hs[k]->voct[0] = x[7];
				hs[k]->voct[1] = x[8];
				hs[k]->step();
			}
			std::uint32_t la, lz, ra, rz;
			std::memcpy(&la, &a.r.outL, 4);
			std::memcpy(&lz, &z.r.outL, 4);
			std::memcpy(&ra, &a.r.outR, 4);
			std::memcpy(&rz, &z.r.outR, 4);
			differ += la != lz || ra != rz || a.r.run != z.r.run;
			energy += double(z.r.outL) * z.r.outL;
		}
		const char* what = frozen ? "nan_reads_as_zero_volts_on_the_tape (frozen, travelling)" : "nan_reads_as_zero_volts_on_the_tape (recording)";
		a.sane(what);
		z.sane(what);
		CHECK(energy > 0.0 && hostile > 10000, "%s: the patch sounds and the doors were hit (%ld hostile samples)", what, hostile);
		CHECK(differ == 0, "%s: NaN, +-inf and -0 read as 0 V: %ld of %lld samples differ from the engine given 0 V", what, differ, n);
	}
}

// ---- Review Focus 1: Position and Stretch by CV at audio rate.

void position_cv_at_audio_rate() {
	Random g;
	g.seed(5);
	int cases = 0;
	for (int size = 0; size < 3; size++)
		for (int frozen = 0; frozen < 2; frozen++)
			for (int stop = 0; stop < 2; stop++)
				for (float att : {-1.f, 1.f}) {
					Host h(48000.f, 3);
					h.f.grainSize = size;
					h.f.stopAtEnd = stop;
					h.f.inRPatched = true;
					h.f.params[F::POSITION_ATT] = att;
					h.f.params[F::STRETCH_ATT] = -att;
					h.f.params[F::POSITION] = 0.5f;
					// A tape of noise first.
					for (int i = 0; i < 48000; i++) {
						h.f.inL = 4.f * g.uniform();
						h.f.inR = 4.f * g.uniform();
						h.step();
					}
					h.f.params[F::FREEZE] = float(frozen);
					double energy = 0.0;
					const long long n = 3 * 48000;
					for (long long i = 0; i < n; i++) {
						const long long t = h.t;
						h.f.inL = 4.f * g.uniform();
						h.f.inR = 4.f * g.uniform();
						// Four ways, a second and a half each: a 3 kHz square of
						// +-10 V; a random step every sample; +-1e30 and NaN
						// alternating; a 5 kHz sine. Stretch's CV likewise, shifted.
						const int way = int((i / 36000) % 4);
						auto cv = [&](long long k) -> float {
							switch (way) {
							case 0: return (k / 8) % 2 ? 10.f : -10.f;
							case 1: return 12.f * g.uniform();
							case 2: return k % 2 ? 1e30f : (k % 3 ? -1e30f : NaN);
							default: return float(10.0 * std::sin(2.0 * kPi * 5000.0 * double(k) / 48000.0));
							}
						};
						h.f.positionCv = cv(t);
						h.f.stretchCv = cv(t + 3);
						h.f.trig = stop && (t % 30000) < 10 ? 10.f : 0.f;   // stopped passes woken
						h.step();
						energy += double(h.r.outL) * h.r.outL;
					}
					char what[96];
					std::snprintf(what, sizeof what, "position_cv_at_audio_rate (%s, %s, %s, att %+.0f)", size == 0 ? "Short" : size == 1 ? "Medium" : "Long",
					              frozen ? "frozen" : "recording", stop ? "stop" : "go round", att);
					h.sane(what);
					CHECK(energy > 0.0, "%s: grains sound", what);
					cases++;
				}
	CHECK(cases == 24, "position_cv_at_audio_rate: %d cases", cases);
}

// ---- Review Focus 4: the hottest comb.

// A +-40 V sine exactly on the comb's one tooth (Partials 1, the root; ROOT
// 0 = C4, 261.63 Hz), Harmonics 1, Width at its least (5 cents). The makeup
// is measured from the sound (Tables.hpp), so a tone on a tooth comes out at
// about its own level: the input is made hot instead (under the old rule a
// +-10 V tone was raised by the bound's +18 dB, to about 80 V).
void hot_sine_on_a_tooth() {
	const double hz = 261.6256;
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (int size = 0; size < 3; size++)
			for (int frozen = 0; frozen < 2; frozen++)
				for (int wide = 0; wide < 2; wide++) {
					Host h(fs, 21);
					h.f.grainSize = size;
					h.f.wide = wide;
					h.f.params[F::ROOT] = 0.f;
					h.f.params[F::PARTIALS] = 1.f;
					h.f.params[F::WIDTH] = 0.f;
					h.f.params[F::HARMONICS] = 1.f;
					const long long n = (long long) (3.0 * fs);
					for (long long i = 0; i < n; i++) {
						h.f.inL = float(40.0 * std::sin(2.0 * kPi * hz * double(h.t) / fs));
						if (frozen && i == n / 3) h.f.params[F::FREEZE] = 1.f;
						h.step();
					}
					char what[96];
					std::snprintf(what, sizeof what, "hot_sine_on_a_tooth (%.0f Hz, %s, %s, %s)", fs, size == 0 ? "Short" : size == 1 ? "Medium" : "Long",
					              frozen ? "frozen" : "recording", wide ? "Wide" : "default");
					h.sane(what);
					// Not vacuous: the comb passed it hot, and the bound held it.
					CHECK(h.peak > 8.f, "%s: the output reaches the soft bound (peak %.2f V)", what, h.peak);
				}
}

// 16 roots, all far above Nyquist (+12 V: C2 x 4096) or all far under 16 Hz
// (-12 V), Harmonics 1: no tooth in the band, the mask all 0, and the wet
// sound exact silence (once the grains from before have played out).
void empty_comb_is_silent() {
	for (float volts : {12.f, -12.f})
		for (int size = 0; size < 3; size++) {
			Host h(48000.f, 31);
			h.f.grainSize = size;
			h.f.params[F::HARMONICS] = 1.f;
			h.f.params[F::PARTIALS] = 32.f;
			h.f.voct = h.voct;
			h.f.voctChannels = 16;
			for (int c = 0; c < 16; c++) h.voct[c] = volts + 0.01f * float(c);
			Random g;
			g.seed(4);
			const int grain = grainFrames(GrainSize(size), 48000.f);
			const long long settle = 4LL * grain + h.e->latencyFrames(), n = settle + 48000;
			bool silent = true;
			for (long long i = 0; i < n; i++) {
				h.f.inL = 4.f * g.uniform();
				h.step();
				if (i >= settle) silent = silent && h.r.outL == 0.f && h.r.outR == 0.f;
			}
			char what[80];
			std::snprintf(what, sizeof what, "empty_comb_is_silent (%+.0f V, %s)", volts, size == 0 ? "Short" : size == 1 ? "Medium" : "Long");
			h.sane(what);
			CHECK(silent, "%s: exact silence", what);
		}
}

// ---- DC in, no DC out: the 16 Hz cut (and DC is bin 0, always 0).

// The largest |mean| of the wet sound over 0.25 s blocks, from a few grains
// in: a component under 16 Hz that got through (the cut's bins pass a few
// hertz) would swing these means by volts (with the cut taken out: 0.6 to
// 4.4 V). Measured 2026-11-26: at most 9.4e-5 V, for 5 and 10 V of DC in.
const double DC_MEAN_BOUND = 1e-3;

void dc_in() {
	double worst = 0.0;
	for (float fs : {44100.f, 48000.f, 96000.f})
		for (int size = 0; size < 3; size++)
			for (int frozen = 0; frozen < 2; frozen++)
				for (float shift : {0.f, 24.f})
					for (float dc : {5.f, -10.f}) {
						Host h(fs, 41);
						h.f.grainSize = size;
						h.f.params[F::SHIFT] = shift;
						h.f.params[F::BLUR] = shift > 0.f ? 1.f : 0.f;
						h.f.inL = dc;
						// The whole tape DC already, so no slice holds the step from
						// silence to it (a step is a sound, with its own low end).
						Ring& ring = h.e->ring();
						for (long long k = 0; k < ring.capacity(); k++) ring.write(dc, dc, false);
						const int grain = grainFrames(GrainSize(size), fs);
						const long long settle = (long long) fs + 3LL * grain + h.e->latencyFrames();
						const long long block = (long long) (0.25 * fs);
						const long long n = settle + 8 * block;
						double sum = 0.0, most = 0.0;
						for (long long i = 0; i < n; i++) {
							if (frozen && i == (long long) fs) h.f.params[F::FREEZE] = 1.f;
							h.step();
							if (i < settle) continue;
							sum += h.r.outL;
							if ((i - settle + 1) % block == 0) {
								most = std::max(most, std::fabs(sum / double(block)));
								sum = 0.0;
							}
						}
						char what[96];
						std::snprintf(what, sizeof what, "dc_in (%.0f Hz, %s, %s, Shift %+.0f, %+.0f V)", fs, size == 0 ? "Short" : size == 1 ? "Medium" : "Long",
						              frozen ? "frozen" : "recording", shift, dc);
						h.sane(what);
						CHECK(most <= DC_MEAN_BOUND, "%s: the wet sound's mean over 0.25 s reaches %.4f V (bound %.3f)", what, most, DC_MEAN_BOUND);
						worst = std::max(worst, most);
					}
	std::printf("dc_in: worst |mean| over 0.25 s %.2e V\n", worst);
}

} // namespace

int main() {
	__sanitizer_install_malloc_and_free_hooks(mallocHook, freeHook);
	// The counters see what they should (rule 1, for the harness itself).
	{
		const long h0 = gHeapOps, l0 = gLocks;
		std::unique_ptr<int> p(new int(1));
		void* volatile q = std::malloc(8);
		std::free(q);
		std::mutex m;
		m.lock();
		m.unlock();
		CHECK(gHeapOps - h0 >= 3, "the heap counter sees new, malloc and free (%ld)", gHeapOps - h0);
		CHECK(gLocks - l0 >= 1, "the lock counter sees std::mutex::lock (%ld)", gLocks - l0);
	}
	fuzz();
	instances_independent();
	nan_reads_as_zero_volts_on_the_tape();
	position_cv_at_audio_rate();
	hot_sine_on_a_tooth();
	empty_comb_is_silent();
	dc_in();
	return rkatest::finish("doldrums/test_hostile");
}

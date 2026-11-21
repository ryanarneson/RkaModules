// Eddy's engine under hostile conditions, built with the address and
// undefined-behaviour sanitizers and counting allocators (operator new and
// delete, and every malloc and free through the sanitizer's hooks): NaN,
// infinity and 1e30 on every input and param; rates of 1 Hz, 8 kHz and
// 768 kHz, changes between them mid-run and rates the engine cannot use; the
// window's extremes (Review Focus 3) for 60 s; seeded fuzz of every control,
// gate and button for 10 minutes of audio; a wearing loop of silence; and no
// allocation or free in process(), across Rec, Restore and a set being taken.
//
// Every tape is fenced: the frames of its allocation outside the tape and its
// tail (the guard frames before frame 0, and everything past length + tail)
// are poisoned for ASan, so a read or write of any of them is a report rather
// than a silent zero. The worker stand-in moves the fence when a copy or a
// clear changes the tape's length. (Originals are not fenced: recording
// writes each frame just before it counts it.)
#include "testing.hpp"
#include "eddy/EddyEngine.hpp"
#include "hadal/Random.hpp"

#include <sanitizer/asan_interface.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <vector>

// Heap operations by anyone, counted; Host::step takes the difference across
// process(). One thread, so plain counters (a thread_local touched in a
// malloc hook may itself allocate on first use).
static long gHeapOps = 0;

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

// malloc, calloc and free (Tape's own buffers among them), seen by the
// sanitizer's allocator. Declared here: not every compiler installs
// <sanitizer/allocator_interface.h>.
extern "C" int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void*, size_t),
                                                         void (*free_hook)(const volatile void*));
static void mallocHook(const volatile void*, size_t) {
	gHeapOps++;
}
static void freeHook(const volatile void* p) {
	if (p)
		gHeapOps++;
}

namespace {

using rka::EddyEngine;
using rka::EddyFrame;
using rka::EddyResult;
using rka::Tape;
using rka::TapeSet;
typedef EddyFrame F;

const float NaN = std::nanf("");

// ---- Fences.

size_t allFrames(const Tape& t) {
	return t.capacity() + t.tailCapacity();
}
void unfence(Tape& t) {
	if (!t.ch[0])
		return;
	for (int c = 0; c < 2; c++)
		ASAN_UNPOISON_MEMORY_REGION(t.ch[c] - Tape::GUARD, (allFrames(t) + 2 * Tape::GUARD) * sizeof(float));
}
// Poisons all but frames [0, length + tail); returns length + tail.
size_t fence(Tape& t) {
	if (!t.ch[0])
		return 0;
	unfence(t);
	const size_t used = t.extent(t.length + t.tail), all = allFrames(t);
	for (int c = 0; c < 2; c++) {
		ASAN_POISON_MEMORY_REGION(t.ch[c] - Tape::GUARD, Tape::GUARD * sizeof(float));
		ASAN_POISON_MEMORY_REGION(t.ch[c] + used, (all - used + Tape::GUARD) * sizeof(float));
	}
	return used;
}

bool finiteTape(const Tape& t) {
	const size_t n = t.extent(t.length + t.tail);
	for (int c = 0; c < 2; c++)
		for (size_t k = 0; k < n; k++)
			if (!std::isfinite(t.ch[c][k]))
				return false;
	return true;
}

// The capacity a set at rate fs must have: min(300 s, 2^24 frames), and a
// tail of 10 ms.
size_t wantCapacity(double fs) {
	return (size_t) std::min(std::floor(300.0 * fs), 16777216.0);
}
size_t wantTail(double fs) {
	return (size_t) std::ceil(0.01 * fs);
}

// ---- The engine and a stand-in for the module's worker thread, which runs
// between process() calls: it makes and frees sets, does the copies the
// engine asks for, and keeps every tape fenced.
struct Host {
	EddyEngine e;
	F f;
	EddyResult r;
	float fs;
	std::map<TapeSet*, size_t> sets;   // made and not yet freed, with the extent fenced
	long heap = 0;                     // heap operations inside process()
	long t = 0;
	long copies = 0;
	int copyDelay = 0;                 // samples between a copy wanted and done
	int copyLeft = -1;
	EddyEngine::Copy copying = EddyEngine::NONE;
	TapeSet* copySet = nullptr;
	long insane = 0;
	std::string why;
	rka::hadal::Random rnd;

	explicit Host(float rate, uint32_t seed = 1) : e(seed), fs(rate) {
		e.setSampleRate(rate);
		rnd.seed(seed * 101u);
	}
	~Host() {
		for (auto& s : sets) {
			unfence(s.first->tape);
			delete s.first;
		}
	}
	Host(const Host&) = delete;
	Host& operator=(const Host&) = delete;

	// An empty set at `rate`, fenced (null if it cannot be made).
	TapeSet* make(float rate) {
		TapeSet* s = new TapeSet;
		if (!s->allocate(rate)) {
			delete s;
			return nullptr;
		}
		sets[s] = fence(s->tape);
		return s;
	}
	// A loaded set: n frames of noise at `volts` (n and tail held to the
	// set's room), tape and original equal, the overview built.
	TapeSet* loaded(float rate, size_t n, size_t tail, float volts) {
		TapeSet* s = new TapeSet;
		if (!s->allocate(rate)) {
			delete s;
			return nullptr;
		}
		n = std::min(n, s->tape.capacity());
		tail = std::min(tail, s->tape.tailCapacity());
		for (size_t k = 0; k < n + tail; k++)
			for (int c = 0; c < 2; c++)
				s->tape.ch[c][k] = s->original.ch[c][k] = volts * rnd.uniform();
		s->tape.length = s->original.length = n;
		s->tape.tail = s->original.tail = tail;
		s->overview.build(s->tape);
		sets[s] = fence(s->tape);
		return s;
	}
	void release(TapeSet* s) {
		unfence(s->tape);
		sets.erase(s);
		delete s;
	}

	// Offers s and runs until the engine has taken it (false if it did not
	// within `most` samples).
	bool use(TapeSet* s, long most = 200000) {
		long n = 0;
		while (!e.offer(s) && n++ < most)
			step();
		for (; e.live() != s && n < most; n++)
			step();
		return e.live() == s;
	}

	void worker() {
		if (copying == EddyEngine::NONE) {
			copying = e.copyWanted();
			if (copying != EddyEngine::NONE) {
				copySet = e.live();
				copyLeft = copyDelay;
			}
		}
		if (copying != EddyEngine::NONE && copyLeft-- <= 0) {
			Tape& to = copying == EddyEngine::ORIGINAL_TO_TAPE ? copySet->tape : copySet->original;
			const Tape& from = copying == EddyEngine::ORIGINAL_TO_TAPE ? copySet->original : copySet->tape;
			unfence(to);
			for (int c = 0; c < 2; c++)
				std::memcpy(to.ch[c], from.ch[c], (from.length + from.tail) * sizeof(float));
			to.length = from.length;
			to.tail = from.tail;
			sets[copySet] = fence(copySet->tape);
			copying = EddyEngine::NONE;
			copies++;
			e.copyDone();
		}
		while (TapeSet* s = e.collect())
			release(s);
		// A clear shortens the live tape: the fence follows.
		if (TapeSet* s = e.live()) {
			auto it = sets.find(s);
			if (it != sets.end() && it->second != s->tape.extent(s->tape.length + s->tape.tail))
				it->second = fence(s->tape);
		}
	}

	void step(float l = 0.f, float rr = 0.f) {
		f.inL = l;
		f.inR = rr;
		const long h0 = gHeapOps;
		e.process(f, r);
		heap += gHeapOps - h0;
		f.recPress = f.dubPress = f.playPress = f.restorePress = false;
		t++;
		check();
		worker();
	}
	void run(long n) {
		for (long i = 0; i < n; i++)
			step();
	}

	void fail(const char* what) {
		if (insane++ == 0) {
			char b[400];
			std::snprintf(b, sizeof b,
			              "sample %ld: %s (out %g %g, head %g, start %g, length %g, overlap %g, seconds %g, voices %d)", t,
			              what, r.l, r.r, r.head, r.winStart, r.winLength, r.overlap, r.seconds, e.activeVoices());
			why = b;
		}
	}
	// What must hold after every sample: the outputs finite and within the
	// limit (10 V: the bound for a storm too, Review Focus 2), at most four
	// voices, the view finite and inside the tape, and the turn the engine
	// uses never shorter than m (Review Focus 3). The turn is
	// L - (o - t) >= L - o with t >= 0, so L - o >= m, from the engine's own
	// L and o, proves it.
	void check() {
		if (!(std::fabs(r.l) <= 10.f && std::fabs(r.r) <= 10.f))
			return fail("an output is not finite or over 10 V");
		if (e.activeVoices() > 4)
			return fail("more than 4 voices");
		const float v[5] = {r.head, r.winStart, r.winLength, r.overlap, r.seconds};
		for (float x : v)
			if (!std::isfinite(x))
				return fail("the view is not finite");
		if (r.empty)
			return;
		if (!(r.head >= 0.f && r.head <= 1.f && r.winStart >= 0.f && r.winStart < 1.f && r.winLength > 0.f &&
		      r.winLength <= 1.f && r.overlap >= 0.f && r.seconds > 0.f))
			return fail("the view is outside the tape");
		if (r.recording)
			return;
		const double rate = fs > 0.f && fs < 1e8f ? (double) fs : 48000.0;
		const double len = (double) r.seconds * rate;
		const double L = (double) r.winLength * len, o = (double) r.overlap * len;
		const double m = std::min(0.01 * rate, L / 4.0);
		// The view's fractions are floats: a frame or so of slack on a full tape.
		if (!(L - o >= m - (len * 3e-7 + 1e-6)))
			return fail("a turn shorter than m");
	}
	bool sane(const char* what) {
		CHECK(insane == 0, "%s: %ld bad samples; the first at %s", what, insane, why.c_str());
		CHECK(heap == 0, "%s: %ld heap operations inside process()", what, heap);
		return insane == 0 && heap == 0;
	}
	float peak(long n) {
		float p = 0.f;
		for (long i = 0; i < n; i++) {
			step();
			p = std::max(p, std::max(std::fabs(r.l), std::fabs(r.r)));
		}
		return p;
	}
	// The input as noise.
	void stepNoise(float volts) {
		step(volts * rnd.uniform(), volts * rnd.uniform());
	}
};

// ---- NaN, infinity and 1e30 on every input and param, in turn, with a
// tape playing, dubbed and worn, and Rec, Play and Restore at work.
void hostile_inputs() {
	const float junk[7] = {NaN, INFINITY, -INFINITY, 1e30f, -1e30f, 3.4e38f, -0.f};
	Host g(48000.f, 5);
	g.copyDelay = 200;
	g.f.inRPatched = true;
	TapeSet* s = g.loaded(48000.f, 96000, 0, 4.f);
	CHECK(g.use(s), "the set is taken");
	g.f.playPress = true;
	g.f.params[F::WEAR] = 0.5f;
	g.f.params[F::SMEAR] = 0.5f;
	g.run(100);
	g.f.dubPress = true;
	g.run(100);
	const F rest = g.f;
	const int fields = 9 + F::PARAMS_LEN;
	bool finite = true;
	for (long i = 0; i < (long) fields * 7 * 1500; i++) {
		const int which = (int) (i / (7 * 1500));
		const float j = junk[(i / 1500) % 7];
		F& f = g.f;
		const bool recPress = f.recPress;
		f = rest;
		f.recPress = recPress;
		f.speedCv = which == 0 ? j : 0.f;
		f.voct = which == 1 ? j : 0.f;
		f.startCv = which == 2 ? j : 0.f;
		// While IN L or IN R is the junk, the REC gate records it (9 500
		// samples, then low): NaN and infinity must not reach the tape.
		const bool recJunk = (which == 7 || which == 8) && i % (7 * 1500) < 9500;
		f.recGate = which == 3 ? j : (recJunk ? 10.f : 0.f);
		f.dubGate = which == 4 ? j : 0.f;
		f.playTrig = which == 5 ? j : (i % 9000 < 50 ? 10.f : 0.f);
		f.restoreTrig = which == 6 ? j : (i % 20000 < 50 ? 10.f : 0.f);
		for (int p = 0; p < F::PARAMS_LEN; p++)
			if (which == 9 + p)
				f.params[p] = j;
		const float in = 3.f * g.rnd.uniform();
		g.step(which == 7 ? j : in, which == 8 ? j : -in);
		// Each field's junk, checked before a dub or a pass of wear can
		// paper over it.
		if (i % 1500 == 1499) {
			const TapeSet* live = g.e.live();
			finite = finite && live && finiteTape(live->tape) && finiteTape(live->original);
		}
	}
	g.f = rest;
	g.run(48000);
	g.sane("junk on every input and param");
	CHECK(finite, "the tape and the original hold no NaN or infinity");
	// And afterwards it still plays.
	g.f.playTrig = 10.f;
	g.run(10);
	g.f.playTrig = 0.f;
	CHECK(g.peak(48000) > 1.f && g.r.playing, "after the junk the tape still plays");
}

// ---- Rates.

// At 1 Hz, 8 kHz and 768 kHz: the set's room follows the rate; a recording
// (to the capacity at 1 Hz), its copy, then play with Dub, Wear and Smear
// full and the window and speed moving, with every tape fenced.
void rates_extreme() {
	const float rates[3] = {1.f, 8000.f, 768000.f};
	// Samples recorded, then played: 1 Hz runs for long stretches of "audio".
	const long recN[3] = {400, 16000, 768000}, playN[3] = {3000, 80000, 1536000};
	for (int k = 0; k < 3; k++) {
		const float fs = rates[k];
		Host g(fs, 20 + k);
		g.copyDelay = 3;
		g.f.inRPatched = true;
		TapeSet* s = g.make(fs);
		CHECK(s && s->tape.capacity() == wantCapacity(fs) && s->original.capacity() == wantCapacity(fs) &&
		          s->tape.tailCapacity() == wantTail(fs),
		      "%g Hz: room for %zu frames and a tail of %zu (want %zu and %zu)", fs, s ? s->tape.capacity() : 0,
		      s ? s->tape.tailCapacity() : 0, wantCapacity(fs), wantTail(fs));
		if (!s)
			continue;
		CHECK(g.use(s), "%g Hz: the set is taken", fs);
		g.f.recPress = true;
		for (long i = 0; i < recN[k]; i++)
			g.stepNoise(4.f);
		if (g.r.recording)
			g.f.recPress = true;
		// The tail (m), then the copy.
		for (long i = 0; i < (long) (0.05 * fs) + 200 && (g.r.recording || g.e.copyWanted() != EddyEngine::NONE || g.copies == 0); i++)
			g.stepNoise(4.f);
		const size_t want = std::min((size_t) recN[k], wantCapacity(fs));
		CHECK(g.e.live() == s && s->tape.length == want && g.copies == 1, "%g Hz: the recording is the tape (%zu frames, want %zu; %ld copies)",
		      fs, s->tape.length, want, g.copies);
		g.f.params[F::SMEAR] = 1.f;
		g.f.params[F::WEAR] = 1.f;
		g.f.params[F::SPEED_ATT] = 1.f;
		g.f.dubPress = true;
		for (long i = 0; i < playN[k]; i++) {
			g.f.params[F::LENGTH] = 0.5f + 0.5f * g.rnd.uniform();
			g.f.startCv = (i / 97) % 2 ? 2.f * g.rnd.uniform() : 0.f;
			g.f.speedCv = 5.f * g.rnd.uniform();
			g.f.voct = (i / 1000) % 3 == 2 ? g.rnd.uniform() : 0.f;
			g.stepNoise(4.f);
		}
		CHECK(g.r.playing && g.r.dubbing, "%g Hz: playing and dubbing", fs);
		CHECK(finiteTape(s->tape), "%g Hz: the tape holds no NaN or infinity", fs);
		char what[64];
		std::snprintf(what, sizeof what, "at %g Hz", fs);
		g.sane(what);
	}
}

// Rate changes mid-run, in each state: playing and dubbing, recording, a
// copy under way, a clear waiting, and with the copy that follows a recording
// not yet done. A set at the old rate offered after the change is handed
// back, not used; one at the new rate is taken and has the new rate's room.
void rates_change() {
	Host g(48000.f, 30);
	g.copyDelay = 2000;
	g.f.inRPatched = true;
	g.f.params[F::WEAR] = 0.3f;
	g.f.params[F::SMEAR] = 0.7f;
	CHECK(g.use(g.loaded(48000.f, 3 * 48000, 480, 4.f)), "the first set is taken");
	g.f.playPress = true;
	g.run(4800);
	const float rates[10] = {8000.f, 768000.f, 1.f, 44100.f, 48000.f, 96000.f, 1.f, 8000.f, 768000.f, 48000.f};
	float now = 48000.f;
	for (int k = 0; k < 10; k++) {
		const int state = k % 5;
		if (state == 0) {   // dubbing
			g.f.dubGate = 10.f;
			g.run(2000);
		}
		else if (state == 1) {   // recording, past its shortest
			g.f.recPress = true;
			for (long i = 0; i < (long) (0.08 * now) + 2; i++)
				g.stepNoise(4.f);
			CHECK(g.r.recording, "state %d at %g Hz: recording", state, now);
		}
		else if (state == 2) {   // a copy under way
			g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
			// The writes fade out (F) before the copy is asked for.
			for (long i = 0; i < (long) (0.02 * now) + 2000 && g.copying == EddyEngine::NONE; i++)
				g.step();
			CHECK(g.copying != EddyEngine::NONE, "state %d at %g Hz: a copy under way", state, now);
		}
		else if (state == 3) {   // a clear waiting for the voices
			g.e.clear();
			g.step();
		}
		else {   // a recording just ended, its copy not done
			g.f.recPress = true;
			for (long i = 0; i < (long) (0.08 * now) + 2; i++)
				g.stepNoise(4.f);
			g.f.recPress = true;
			g.step();
		}
		const float old = now;
		now = rates[k];
		g.fs = now;
		g.e.setSampleRate(now);
		g.f.dubGate = 0.f;
		// A set at the old rate: handed back (once a copy under way is done),
		// never used.
		TapeSet* stale = g.loaded(old, 4800, 0, 4.f);
		long n = 0;
		while (!g.e.offer(stale) && n++ < 100000)
			g.stepNoise(4.f);
		bool used = false;
		for (n = 0; n < 100000 && g.sets.count(stale); n++) {
			used = used || g.e.live() == stale;
			g.stepNoise(4.f);
		}
		CHECK(!used && g.sets.count(stale) == 0, "a set at %g Hz offered at %g Hz is handed back, not used", old, now);
		// One at the new rate: taken, with the new rate's room.
		TapeSet* s = g.loaded(now, (size_t) (2.0 * now) + 1, (size_t) std::ceil(0.01 * now), 4.f);
		CHECK(g.use(s), "the set at %g Hz is taken", now);
		CHECK(s->tape.capacity() == wantCapacity(now) && s->tape.tailCapacity() == wantTail(now), "%g Hz: room for %zu frames",
		      now, s->tape.capacity());
		g.f.playPress = !g.r.playing;
		g.f.params[F::LENGTH] = 0.3f + 0.07f * k;
		for (long i = 0; i < std::max(3000L, (long) (0.2 * now)); i++)
			g.stepNoise(3.f);
		CHECK(g.r.playing && !g.r.empty, "after the change to %g Hz the new tape plays", now);
	}
	g.sane("rate changes mid-run");
}

// Rates the engine cannot use: silence, no crash, and back to work once a
// good rate and a set at it come.
void rates_unusable() {
	Host g(48000.f, 40);
	CHECK(g.use(g.loaded(48000.f, 48000, 0, 4.f)), "the set is taken");
	g.f.playPress = true;
	g.f.dubPress = true;
	g.run(4800);
	const float bad[5] = {0.f, -48000.f, NaN, INFINITY, 1e9f};
	for (float fs : bad) {
		g.fs = fs;
		g.e.setSampleRate(fs);
		TapeSet* s = g.loaded(fs, 4800, 0, 4.f);
		CHECK(!s, "no set can be made at %g Hz", fs);
		const float p = g.peak(4800);
		CHECK(p == 0.f && g.r.empty, "at %g Hz: silence (%g V) and no tape", fs, p);
	}
	g.fs = 48000.f;
	g.e.setSampleRate(48000.f);
	CHECK(g.use(g.loaded(48000.f, 48000, 0, 4.f)), "a set at 48 kHz is taken again");
	g.f.playPress = !g.r.playing;
	CHECK(g.peak(9600) > 1.f, "and plays");
	g.sane("unusable rates");
}

// ---- Review Focus 3: Length at 10 ms with Smear at 1, Start CV as +-10 V
// noise, Speed at +-4 with V/OCT stepping every sample, Dub and Wear on so
// the voices write too; on a full tape (with a tail), and on tapes only 50 ms
// long, with and without a tail. 60 s of audio each. Every read and write
// stays in the tape and its tail (the fence), every turn is at least m, the
// tape holds no NaN.
void window_extremes() {
	struct Case {
		const char* name;
		size_t frames, tail;
	};
	const Case cases[3] = {{"a full tape", wantCapacity(48000.0), 480}, {"a 50 ms tape", 2400, 0}, {"a 50 ms tape with a tail", 2400, 120}};
	for (const Case& c : cases) {
		Host g(48000.f, 50);
		g.copyDelay = 50;
		g.f.inRPatched = true;
		TapeSet* s = g.loaded(48000.f, c.frames, c.tail, 4.f);
		CHECK(s && s->tape.length == c.frames && s->tape.tail == c.tail, "%s: made", c.name);
		CHECK(g.use(s), "%s: taken", c.name);
		g.f.params[F::LENGTH] = 0.f;
		g.f.params[F::SMEAR] = 1.f;
		g.f.params[F::WEAR] = 1.f;
		g.f.playPress = true;
		g.step();
		g.f.dubPress = true;
		long joins = 0, flips = 0;
		for (long i = 0; i < 60 * 48000L; i++) {
			// Speed: the knob at -2 or +2 (flipping every quarter second)
			// times 2^(0.5..1.5 V), held to 4.
			if (i % 12000 == 0) {
				g.f.params[F::SPEED] = g.f.params[F::SPEED] > 0.f ? -2.f : 2.f;
				flips++;
			}
			g.f.voct = 1.f + 0.5f * g.rnd.uniform();
			g.f.startCv = 10.f * g.rnd.uniform();
			g.stepNoise(4.f);
			joins += g.r.eoc ? 1 : 0;
		}
		CHECK(g.r.playing && g.r.dubbing && !g.r.empty, "%s: still playing and dubbing", c.name);
		CHECK(joins > 1000 && flips > 200, "%s: the head turned (%ld samples of EOC) and reversed (%ld)", c.name, joins, flips);
		CHECK(finiteTape(s->tape), "%s: the tape holds no NaN or infinity", c.name);
		g.sane(c.name);
	}
}

// ---- Seeded fuzz: every control, gate and button each block of 32 samples,
// and the worker's requests (sets offered, loaded or empty, clears, copies,
// the transport, the rate) now and then. 10 minutes of audio over 5 seeds.
void seeded_fuzz() {
	const float fuzzRates[4] = {48000.f, 44100.f, 96000.f, 8000.f};
	long offered = 0, clears = 0, rateChanges = 0, recordings = 0;
	for (uint32_t seed = 1; seed <= 5; seed++) {
		Host g(48000.f, seed);
		rka::hadal::Random rng;
		rng.seed(seed * 7919u);
		g.copyDelay = 64;
		CHECK(g.use(g.make(48000.f)), "seed %u: the first set is taken", seed);
		bool wasRec = false;
		const long n = 120L * 48000;
		auto knob = [&](int p) {
			const float u = rng.unit();
			if (rng.unit() < 0.05f)   // out of range now and then
				return p == F::SPEED ? 6.f * u - 3.f : 1.6f * u - 0.3f;
			return p == F::SPEED ? 4.f * u - 2.f : p == F::SPEED_ATT ? 2.f * u - 1.f : u;
		};
		for (long i = 0; i < n; i++) {
			if (i % 32 == 0) {
				F& f = g.f;
				for (int p = 0; p < F::PARAMS_LEN; p++)
					if (rng.unit() < 0.25f)
						f.params[p] = knob(p);
				if (rng.unit() < 0.25f)
					f.speedCv = 12.f * rng.uniform();
				if (rng.unit() < 0.25f)
					f.voct = rng.unit() < 0.8f ? 2.f * rng.uniform() : 12.f * rng.uniform();
				if (rng.unit() < 0.25f)
					f.startCv = 12.f * rng.uniform();
				float* gates[4] = {&f.recGate, &f.dubGate, &f.playTrig, &f.restoreTrig};
				for (float* gate : gates)
					if (rng.unit() < 0.08f)
						*gate = *gate > 0.5f ? 0.f : (rng.unit() < 0.9f ? 10.f : 0.5f + rng.unit());
				f.recPress = rng.unit() < 0.01f;
				f.dubPress = rng.unit() < 0.02f;
				f.playPress = rng.unit() < 0.02f;
				f.restorePress = rng.unit() < 0.01f;
				if (rng.unit() < 0.005f)
					f.loop = !f.loop;
				if (rng.unit() < 0.005f)
					f.inRPatched = !f.inRPatched;
				// The worker's side.
				const float w = rng.unit();
				if (w < 0.002f) {
					const size_t lens[5] = {1, 3, (size_t) (0.05f * g.fs), (size_t) g.fs, (size_t) (5.f * g.fs)};
					TapeSet* s = rng.unit() < 0.2f ? g.make(g.fs)
					                                : g.loaded(g.fs, lens[(int) (rng.unit() * 5.f) % 5],
					                                           rng.unit() < 0.5f ? 0 : (size_t) (0.01f * g.fs), 5.f);
					if (s && g.e.offer(s)) {
						offered++;
						if (rng.unit() < 0.5f)
							g.e.setTransport(rng.unit() < 0.7f, rng.unit() < 0.3f);
					}
					else if (s)
						g.release(s);
				}
				else if (w < 0.003f) {
					g.e.clear();
					clears++;
				}
				else if (w < 0.006f)
					g.e.requestCopy(rng.unit() < 0.5f ? EddyEngine::ORIGINAL_TO_TAPE : EddyEngine::TAPE_TO_ORIGINAL);
				else if (w < 0.0063f) {
					g.fs = fuzzRates[(int) (rng.unit() * 4.f) % 4];
					g.e.setSampleRate(g.fs);
					rateChanges++;
					// The module converts and offers; here a fresh set.
					TapeSet* s = g.loaded(g.fs, (size_t) g.fs, 0, 4.f);
					long k = 0;
					while (!g.e.offer(s) && k++ < 100000)
						g.stepNoise(5.f);
				}
			}
			g.stepNoise(rng.unit() < 0.001f ? 50.f : 5.f);
			if (g.r.recording && !wasRec)
				recordings++;
			wasRec = g.r.recording;
		}
		char what[32];
		std::snprintf(what, sizeof what, "fuzz seed %u", seed);
		g.sane(what);
		if (TapeSet* s = g.e.live())
			CHECK(finiteTape(s->tape) && finiteTape(s->original), "seed %u: the tapes hold no NaN or infinity", seed);
	}
	CHECK(offered > 50 && clears > 50 && rateChanges > 5 && recordings > 50,
	      "the fuzz reached the worker's requests and Rec (%ld sets offered, %ld clears, %ld rate changes, %ld recordings)",
	      offered, clears, rateChanges, recordings);
}

// ---- A wearing loop of silence, and a dub fading into silence: no
// subnormal reaches the tape or the outputs, and the cost does not rise.
bool subnormal(float x) {
	return x != 0.f && std::fabs(x) < 1.17549435e-38f;
}
void denormals() {
	struct Case {
		const char* name;
		float volts, wear, length;
		bool dub;
	};
	// Length 0 is 10 ms, a hundred passes a second; 1 is the whole 0.25 s.
	// 1e-6 V is about the quietest step of a 24-bit file; 1e-30 V stands for
	// what any dub over silence comes to after long enough, without the wait.
	const Case cases[6] = {{"silence worn", 0.f, 1.f, 0.f, false},
	                       {"noise dubbed over with silence", 4.f, 0.f, 0.f, true},
	                       {"noise dubbed over with silence, the whole tape", 4.f, 0.f, 1.f, true},
	                       {"quiet noise dubbed over with silence", 1e-6f, 0.f, 1.f, true},
	                       {"quiet noise dubbed over with silence and barely worn", 1e-6f, 1e-4f, 1.f, true},
	                       {"faint noise dubbed over with silence", 1e-30f, 0.f, 0.f, true}};
	for (const Case& c : cases) {
		Host g(48000.f, 60);
		TapeSet* s = g.loaded(48000.f, 12000, 0, c.volts);
		CHECK(g.use(s), "%s: taken", c.name);
		g.f.params[F::LENGTH] = c.length;
		g.f.params[F::WEAR] = c.wear;
		g.f.playPress = true;
		g.step();
		g.f.dubPress = c.dub;
		bool normal = true;
		double first = 0.0, last = 0.0;
		for (int sec = 0; sec < 30; sec++) {
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < 48000; i++) {
				g.step();
				normal = normal && !subnormal(g.r.l) && !subnormal(g.r.r);
			}
			const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			if (sec == 1)
				first = dt;
			last = dt;
		}
		long tiny = 0;
		for (int ch = 0; ch < 2; ch++)
			for (size_t k = 0; k < s->tape.length; k++)
				tiny += subnormal(s->tape.ch[ch][k]) ? 1 : 0;
		CHECK(normal && tiny == 0, "%s: subnormals at the outputs (%d) or on the tape (%ld frames)", c.name, (int) !normal, tiny);
		CHECK(last < 2.0 * first + 0.05, "%s: the loop runs slow: %.3f s for its last second, %.3f s for its second", c.name, last,
		      first);
		CHECK(g.r.playing, "%s: playing", c.name);
		g.sane(c.name);
	}
}

// ---- No allocation and no free in process(), across the transport: a set
// taken, Rec and its copy, Play, Dub, Wear, Smear, Restore and its copy,
// "Make this the original", a new set taken while playing, a clear, a rate
// change and its set. (Host::step counts every process() call in every test;
// this one makes sure each of these happened.)
void no_allocation() {
	Host g(48000.f, 70);
	g.copyDelay = 300;
	g.f.inRPatched = true;
	CHECK(g.use(g.make(48000.f)), "an empty set is taken");
	g.f.recPress = true;
	for (int i = 0; i < 48000; i++)
		g.stepNoise(4.f);
	CHECK(g.r.recording, "recording");
	g.f.recPress = true;
	for (int i = 0; i < 4800; i++)
		g.stepNoise(4.f);
	CHECK(!g.r.recording && g.r.playing && g.copies == 1, "the recording ended, its copy made, Play on");
	g.f.params[F::WEAR] = 0.8f;
	g.f.params[F::SMEAR] = 1.f;
	g.f.params[F::LENGTH] = 0.6f;
	g.f.dubPress = true;
	for (int i = 0; i < 48000; i++)
		g.stepNoise(2.f);
	CHECK(g.r.dubbing, "dubbing");
	g.f.restorePress = true;
	g.run(4800);
	CHECK(g.copies == 2 && !g.r.dubbing, "Restore: copied, the dub over");
	g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	g.run(4800);
	CHECK(g.copies == 3, "made the original");
	TapeSet* next = g.loaded(48000.f, 48000, 480, 3.f);
	CHECK(g.use(next) && g.r.playing, "a new set taken while playing");
	g.run(4800);
	g.e.clear();
	g.run(4800);
	CHECK(g.r.empty && next->tape.length == 0, "cleared");
	g.fs = 96000.f;
	g.e.setSampleRate(96000.f);
	g.run(100);
	CHECK(g.use(g.loaded(96000.f, 96000, 0, 3.f)), "a set at 96 kHz taken");
	g.f.playPress = true;
	CHECK(g.peak(9600) > 1.f, "and plays");
	g.sane("the transport");
}

} // namespace

int main() {
	__sanitizer_install_malloc_and_free_hooks(mallocHook, freeHook);
	hostile_inputs();
	rates_extreme();
	rates_change();
	rates_unusable();
	window_extremes();
	no_allocation();
	denormals();
	seeded_fuzz();
	return rkatest::finish("eddy test_hostile");
}

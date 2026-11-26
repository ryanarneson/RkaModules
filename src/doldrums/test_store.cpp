// Doldrums' frozen tape kept with the patch (TapeStore.hpp) and the swap of a
// tape into a running engine (DoldrumsEngine.hpp, "Tapes handed in"), against
// the design's "With the patch" and its Testing bullet, and the plan's Review
// Focus 5. Built with the sanitizers, counting heap operations (operator new
// and delete, and malloc and free through the sanitizer's hooks) and locks
// (as test_hostile does), with WavFile.hpp compiled in. Files are written only
// in a temporary folder made here and removed at the end.
//
// - save_plan: the table (button on and not yet written: write; written
//   already: nothing; button off or Keep off: remove; a freeze by the gate
//   alone: remove).
// - round_trip: a tape written and read back is the tape, to the bit, oldest
//   frame first, with the ring wrapped; the file is 32-bit float stereo at
//   the rate, capacity frames long.
// - converts_rate: a 440 Hz tone saved at 48 kHz, read at 44.1 and 96 kHz
//   (and converted ring to ring): 440 Hz within 1 cent, 30 s long, the tone
//   from end to end; equal rates are a copy to the bit.
// - swap_under_a_running_engine: a stored tape handed in under an engine
//   playing a frozen tape: no heap operation or lock in any process() call,
//   no click, the new tape heard, its pass from the anchor, no Freeze edge,
//   RUN dipping once for the new pass, the old ring handed back, and the
//   window's spare view (rebuilt before the offer) the same as one fed the
//   new tape frame by frame.
// - Review Focus 5: missing_file, truncated_file, empty_file, not_a_wav,
//   foreign_rate, absurdly_large, mono_file, too_long, too_short and
//   hostile_samples: false with `why` set and `out` empty, or a sane tape;
//   never a crash (ASan).
// - save_during_swap: the live tape saved while a swap and a thaw come in the
//   middle of the save (the hold, driven a step at a time): the file is one
//   whole tape or there is none; nothing is written while a tape is on its
//   way or the freeze is still fading.
#define RKA_WAV_IMPLEMENTATION
#include "doldrums/TapeStore.hpp"
#include "doldrums/TapeView.hpp"
#include "testing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <pthread.h>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif

// ---- Heap operations and locks, counted (one thread: plain counters).
static long gHeapOps = 0;
static long gLocks = 0;

void* operator new(std::size_t n) {
	gHeapOps++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
	gHeapOps++;
	return std::malloc(n ? n : 1);
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

extern "C" int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void*, size_t),
                                                         void (*free_hook)(const volatile void*));
static void mallocHook(const volatile void*, size_t) {
	gHeapOps++;
}
static void freeHook(const volatile void* p) {
	if (p)
		gHeapOps++;
}

// Locks, as test_hostile counts them (its comment says why each).
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

extern "C" const char* __asan_default_options() {
	return "detect_stack_use_after_return=0";
}

namespace fs = std::filesystem;

namespace {

using namespace rka::doldrums;
using rka::WavInfo;
typedef DoldrumsFrame F;

const double kPi = 3.14159265358979323846;

struct TempDir {
	fs::path dir;
	TempDir() {
		dir = fs::temp_directory_path() /
		      ("rka_doldrums_store_" + std::to_string((long long) std::rand()) + "_" + std::to_string((long long) std::time(nullptr)));
		fs::create_directories(dir);
	}
	~TempDir() {
		std::error_code ec;
		fs::remove_all(dir, ec);
	}
	std::string sub(const std::string& name) const {
		fs::create_directories(dir / name);
		return (dir / name).string();
	}
};
TempDir* gTemp = nullptr;

std::string tapeFile(const std::string& dir) { return (fs::path(dir) / STORED_TAPE).string(); }

uint32_t bitsOf(float v) {
	uint32_t b;
	std::memcpy(&b, &v, sizeof b);
	return b;
}

// The tape oldest first through Ring::copy (not TapeStore's own reader).
void oldestFirst(const Ring& r, std::vector<float>& l, std::vector<float>& rr) {
	l.assign(size_t(r.capacity()), 0.f);
	rr.assign(l.size(), 0.f);
	CHECK(r.copy(r.now(), int(r.capacity()), l.data(), rr.data()), "the whole tape is readable");
}

// A ring of `cap` frames, filled with l and r (oldest first).
std::unique_ptr<Ring> ringOf(const std::vector<float>& l, const std::vector<float>& r) {
	std::unique_ptr<Ring> g(new Ring);
	g->allocate((long long) l.size());
	g->fill(l.data(), r.data());
	return g;
}

void sine(std::vector<float>& x, long long n, double hz, double rate, double volts, double phase = 0.0) {
	x.resize(size_t(n));
	for (long long i = 0; i < n; i++) x[size_t(i)] = float(volts * std::sin(2.0 * kPi * hz * double(i) / rate + phase));
}

// Frequency from the upward zero crossings (interpolated) in [a, b).
double frequency(const std::vector<float>& x, double rate, size_t a, size_t b) {
	double first = -1, last = -1;
	long long count = 0;
	for (size_t i = a + 1; i < b; i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f) {
			const double t = double(i - 1) + double(-x[i - 1]) / double(x[i] - x[i - 1]);
			if (first < 0) first = t;
			last = t;
			count++;
		}
	return count > 1 ? double(count - 1) * rate / (last - first) : 0.0;
}

double rms(const std::vector<float>& x, size_t a, size_t n) {
	double s = 0;
	for (size_t i = a; i < a + n; i++) s += double(x[i]) * x[i];
	return std::sqrt(s / double(n));
}

void put16(std::string& s, uint32_t v) {
	s += char(v & 255);
	s += char((v >> 8) & 255);
}
void put32(std::string& s, uint32_t v) {
	put16(s, v & 0xFFFF);
	put16(s, v >> 16);
}
// A WAV image by hand (not through WavFile's writer).
std::string wavBytes(unsigned tag, unsigned channels, uint32_t rate, unsigned bits, const std::string& data, int64_t claimed = -1) {
	std::string s = "RIFF";
	put32(s, 36 + (uint32_t) data.size());
	s += "WAVEfmt ";
	put32(s, 16);
	put16(s, tag);
	put16(s, channels);
	put32(s, rate);
	const unsigned bytes = bits / 8;
	put32(s, rate * channels * bytes);
	put16(s, channels * bytes);
	put16(s, bits);
	s += "data";
	put32(s, claimed < 0 ? (uint32_t) data.size() : (uint32_t) claimed);
	s += data;
	return s;
}
std::string floatData(const std::vector<std::vector<float>>& ch) {
	std::string d;
	for (size_t i = 0; i < ch[0].size(); i++)
		for (const auto& c : ch) d.append((const char*) &c[i], 4);
	return d;
}
void writeBytes(const std::string& path, const std::string& bytes) {
	std::ofstream o(path, std::ios::binary);
	o.write(bytes.data(), (std::streamsize) bytes.size());
}

// readTape refused it: false, `out` emptied (it held a tape before), why set.
void refused(const std::string& dir, const char* name) {
	Ring out;
	out.allocate(1000);
	std::string why;
	const bool ok = readTape(dir, 48000.f, out, why);
	CHECK(!ok && out.capacity() == 0 && out.channel(0) == nullptr && !why.empty(), "%s: refused (ok %d, capacity %lld, why \"%s\")", name, ok,
	      out.capacity(), why.c_str());
	std::printf("  %s: %s\n", name, why.c_str());
}

// ---- The tests.

void save_plan() {
	struct Row {
		SaveFacts f;
		SaveAction want;
		const char* what;
	} rows[] = {
		{{true, true, 5, 0}, SAVE_WRITE, "button on, never written: write"},
		{{true, true, 5, 4}, SAVE_WRITE, "button on, written for an earlier freeze: write"},
		{{true, true, 5, 5}, SAVE_NOTHING, "button on, written since it froze: nothing"},
		{{true, false, 5, 0}, SAVE_REMOVE, "button off (not frozen, or a gate-only freeze): remove"},
		{{true, false, 5, 5}, SAVE_REMOVE, "button off, a file from before: remove"},
		{{false, true, 5, 0}, SAVE_REMOVE, "Keep off: remove"},
		{{false, true, 5, 5}, SAVE_REMOVE, "Keep off, written already: remove"},
		{{false, false, 5, 4}, SAVE_REMOVE, "both off: remove"},
	};
	for (const Row& r : rows) CHECK(savePlan(r.f) == r.want, "%s (got %d)", r.what, int(savePlan(r.f)));
	// And with the engine's numbers: a gate-only freeze is frozen but stores nothing.
	DoldrumsEngine e;
	CHECK(e.allocate(8000.f), "allocate");
	e.setSampleRate(8000.f);
	F f;
	DoldrumsResult out;
	for (int i = 0; i < 100; i++) e.process(f, out);
	const uint32_t before = e.frozenSince();
	f.freezeGate = 10.f;
	for (int i = 0; i < 400; i++) e.process(f, out);
	CHECK(out.frozen && e.frozenSince() != before, "the gate froze it: a new number");
	CHECK(savePlan({true, f.params[F::FREEZE] >= 0.5f, e.frozenSince(), 0}) == SAVE_REMOVE, "gate-only freeze: remove");
	f.params[F::FREEZE] = 1.f;
	CHECK(savePlan({true, f.params[F::FREEZE] >= 0.5f, e.frozenSince(), 0}) == SAVE_WRITE, "the button on too: write");
}

void round_trip() {
	const float rate = 48000.f;
	const std::string dir = gTemp->sub("round_trip");
	Ring src;
	src.allocate(tapeFrames(rate));
	src.setFadeFrames(240);
	uint32_t x = 12345;
	auto noise = [&]() {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		return float(int32_t(x)) * (7.f / 2147483648.f);
	};
	// More than a tape, so the oldest frame is part way round the store; then frozen.
	const long long n = src.capacity() + 777777;
	for (long long i = 0; i < n; i++) {
		const float v = noise();
		src.write(v, -0.5f * v + 0.001f, false);
	}
	for (int i = 0; i < 300; i++) src.write(noise(), noise(), true);
	CHECK(src.physical(src.now()) != 0, "the write point is not at the start of the store");
	std::string why;
	CHECK(writeTape(dir, src, rate, why), "written: %s", why.c_str());
	WavInfo info;
	std::string w;
	CHECK(rka::probeWav(tapeFile(dir), info, w) && info.sampleRate == 48000 && info.channels == 2 && info.frames == uint64_t(src.capacity()),
	      "the file: %u Hz, %u channels, %llu frames", info.sampleRate, info.channels, (unsigned long long) info.frames);
	{
		std::ifstream in(tapeFile(dir), std::ios::binary);
		std::string head(64, '\0');
		in.read(&head[0], 64);
		const size_t fmt = head.find("fmt ");
		uint16_t tag = 0, bits = 0;
		if (fmt != std::string::npos) {
			std::memcpy(&tag, head.data() + fmt + 8, 2);
			std::memcpy(&bits, head.data() + fmt + 22, 2);
		}
		CHECK(tag == 3 && bits == 32, "32-bit float (format tag %u, %u bits)", tag, bits);
	}
	Ring back;
	CHECK(readTape(dir, rate, back, why) && why.empty(), "read back: %s", why.c_str());
	CHECK(back.capacity() == src.capacity() && back.now() == back.capacity() && back.fadedOut(), "a whole tape, frozen and faded out");
	std::vector<float> a, b, c, d;
	oldestFirst(src, a, b);
	oldestFirst(back, c, d);
	long long bad = 0;
	for (size_t i = 0; i < a.size(); i++) bad += bitsOf(a[i]) != bitsOf(c[i]) || bitsOf(b[i]) != bitsOf(d[i]);
	CHECK(bad == 0, "round trip to the bit, oldest frame first (%lld frames differ)", bad);
	// And the file itself holds the oldest frame first.
	std::vector<float> fl(a.size()), fr(a.size());
	CHECK(rka::readWav(tapeFile(dir), fl.data(), fr.data(), fl.size(), info, w) && bitsOf(fl[0]) == bitsOf(a[0]) &&
	          bitsOf(fl.back()) == bitsOf(a.back()) && bitsOf(fr[12345]) == bitsOf(b[12345]),
	      "the file's first frame is the oldest");
	Ring none;
	CHECK(!writeTape(dir, none, rate, why) && !why.empty(), "no tape: not written (%s)", why.c_str());
	CHECK(!writeTape(gTemp->sub("round_trip") + "/missing/deeper", src, rate, why) && !why.empty(), "no folder: not written (%s)", why.c_str());
}

void converts_rate() {
	const std::string dir = gTemp->sub("converts_rate");
	const float from = 48000.f;
	std::vector<float> l, r;
	sine(l, tapeFrames(from), 440.0, from, 5.0);
	sine(r, tapeFrames(from), 440.0, from, 3.0, 1.0);
	std::unique_ptr<Ring> src = ringOf(l, r);
	std::string why;
	CHECK(writeTape(dir, *src, from, why), "written at 48 kHz: %s", why.c_str());
	const float rates[2] = {44100.f, 96000.f};
	for (float to : rates) {
		for (int path = 0; path < 2; path++) {
			Ring out;
			const bool ok = path == 0 ? readTape(dir, to, out, why) : convertTape(*src, from, to, out, why);
			const char* how = path == 0 ? "read" : "converted";
			CHECK(ok, "%s at %g: %s", how, to, why.c_str());
			CHECK(out.capacity() == tapeFrames(to) && out.now() == out.capacity() && out.fadedOut(), "%s at %g: 30 s (%lld frames), frozen",
			      how, to, out.capacity());
			std::vector<float> a, b;
			oldestFirst(out, a, b);
			const size_t margin = size_t(to * 0.05);
			for (int side = 0; side < 2; side++) {
				const std::vector<float>& x = side ? b : a;
				const double f = frequency(x, to, margin, x.size() - margin);
				const double cents = 1200.0 * std::log2(f / 440.0);
				CHECK(std::fabs(cents) < 1.0, "%s at %g, side %d: %.5f Hz (%+.4f cents)", how, to, side, f, cents);
				const double want = (side ? 3.0 : 5.0) / std::sqrt(2.0);
				const double head = rms(x, margin, size_t(to)), tail = rms(x, x.size() - margin - size_t(to), size_t(to));
				CHECK(std::fabs(head / want - 1) < 0.01 && std::fabs(tail / want - 1) < 0.01, "%s at %g, side %d: the tone end to end (%.3f, %.3f V rms)",
				      how, to, side, head, tail);
			}
		}
	}
	// Equal rates: a copy, to the bit.
	Ring same;
	CHECK(convertTape(*src, from, from, same, why), "converted to its own rate");
	std::vector<float> a, b;
	oldestFirst(same, a, b);
	long long bad = 0;
	for (size_t i = 0; i < a.size(); i++) bad += bitsOf(a[i]) != bitsOf(l[i]) || bitsOf(b[i]) != bitsOf(r[i]);
	CHECK(bad == 0, "equal rates: to the bit (%lld frames differ)", bad);
	Ring out;
	out.allocate(10);
	CHECK(!convertTape(*src, from, 1000.f, out, why) && out.capacity() == 0 && !why.empty(), "an unusable rate refused, out empty (%s)",
	      why.c_str());
	Ring empty;
	CHECK(!convertTape(empty, from, 44100.f, out, why) && out.capacity() == 0, "no tape: refused");
}

// Mean power at hz over x[a, a + n): Goertzel on Hann-windowed blocks of 4096
// (12 Hz wide at 48 kHz, so a tone whose phase jumps grain to grain is caught
// whole), as a sine's amplitude squared.
double tonePower(const std::vector<float>& x, size_t a, size_t n, double hz, double rate) {
	const size_t B = 4096;
	const double w = 2.0 * kPi * hz / rate, c = 2.0 * std::cos(w);
	double sum = 0;
	int blocks = 0;
	for (size_t s = a; s + B <= a + n; s += B, blocks++) {
		double s1 = 0, s2 = 0;
		for (size_t i = 0; i < B; i++) {
			const double win = 0.5 - 0.5 * std::cos(2.0 * kPi * double(i) / double(B));
			const double s0 = win * x[s + i] + c * s1 - s2;
			s2 = s1;
			s1 = s0;
		}
		sum += (s1 * s1 + s2 * s2 - c * s1 * s2) * 16.0 / double(B * B);
	}
	return blocks ? sum / blocks : 0.0;
}
double worstStep(const std::vector<float>& x, size_t a, size_t b) {
	double w = 0;
	for (size_t i = a + 1; i < b; i++) w = std::max(w, double(std::fabs(x[i] - x[i - 1])));
	return w;
}

// The module's window, as TapeStore.hpp's sequence has it, on one thread.
struct Window {
	TapeView views[2];
	int fed = 0;
	uint32_t seen = 0;
};

void swap_under_a_running_engine() {
	{
		const float rate = 48000.f;
		const std::string dir = gTemp->sub("swap");
		DoldrumsEngine e;
		CHECK(e.allocate(rate), "allocate");
		e.setSampleRate(rate);
		e.seed(7);
		F f;
		f.params[F::POSITION] = 0.5f;
		DoldrumsResult out;
		Window win;
		win.views[0].configure(e.ring().capacity(), e.ring().now());
		std::vector<float> L, run;
		long heap = 0, locks = 0;
		long long boundaryAfter = -1, take = -1;
		long long anchorSeen = 0, endSeen = 0;
		const int grain = grainFrames(GRAIN_MEDIUM, rate);
		auto step = [&]() {
			const long h0 = gHeapOps, k0 = gLocks;
			e.process(f, out);
			heap += gHeapOps - h0;
			locks += gLocks - k0;
			L.push_back(out.outL);
			run.push_back(out.run);
			// The module, after process (TapeStore.hpp, 2d).
			if (e.tapesTaken() != win.seen) {
				win.seen = e.tapesTaken();
				win.fed = 1 - win.fed;
				take = (long long) L.size() - 1;
			}
			if (take >= 0 && boundaryAfter < 0 && (e.staged() & DoldrumsEngine::STAGED_BOUNDARY) && (long long) L.size() - 1 > take) {
				boundaryAfter = (long long) L.size() - 1;
				anchorSeen = e.state().anchor;
				endSeen = e.state().end;
			}
		};
		// Tape A, a 440 Hz tone, written straight onto the live ring; then frozen and playing.
		{
			Ring& g = e.ring();
			for (long long i = 0; i < g.capacity(); i++) g.write(float(5.0 * std::sin(2.0 * kPi * 440.0 * double(i) / rate)), 0.f, false);
		}
		f.params[F::FREEZE] = 1.f;
		for (int i = 0; i < int(rate * 2); i++) step();
		const double pa = tonePower(L, L.size() - 48000, 48000, 440.0, rate);
		CHECK(pa > 1.0, "tape A sounds (%.3g)", pa);
		const uint32_t since = e.frozenSince();
		const Ring* a = e.live();
		// Tape B, a 300 Hz tone, kept with a patch: written, then read as the module would.
		std::vector<float> bl, br;
		sine(bl, tapeFrames(rate), 300.0, rate, 5.0);
		sine(br, tapeFrames(rate), 300.0, rate, 5.0);
		std::string why;
		CHECK(writeTape(dir, *ringOf(bl, br), rate, why), "B written");
		e.expectStored();
		DoldrumsEngine::TapeJob job;
		CHECK(e.tapeWanted(job) && job.from == nullptr && job.toRate == rate, "the stored tape is wanted at %g", job.toRate);
		Ring* b = makeWanted(job, dir, why);
		CHECK(b != nullptr, "B read: %s", why.c_str());
		if (!b) return;
		// The spare view, rebuilt from B before the offer (B is the worker's alone).
		win.views[1 - win.fed].rebuild(*b);
		CHECK(e.offer(b, job.ticket), "offered");
		Ring* second = new Ring;
		CHECK(!e.offer(second, job.ticket), "a second offer waits for the first to be looked at");
		delete second;
		const size_t T = L.size();
		for (int i = 0; i < int(rate * 3); i++) step();
		CHECK(take == (long long) T, "taken on the next sample (%lld, %zu)", take, T);
		CHECK(heap == 0 && locks == 0, "no heap operation (%ld) or lock (%ld) in process(), the take included", heap, locks);
		CHECK(e.live() == b && e.tapesTaken() == 1u, "B is live");
		Ring* back = e.retired();
		CHECK(back == a && e.retired() == nullptr, "A came back, and only A");
		delete back;
		CHECK(!e.tapeWanted(job), "nothing more is wanted");
		CHECK(e.frozenSince() == since && out.frozen, "no Freeze edge: the same frozen tape number");
		// The pass, from the anchor of the new tape.
		const long long cap = tapeFrames(rate);
		const long long anchor = cap - lagFrames(0.5f, cap, grain);
		CHECK(boundaryAfter > take && anchorSeen == anchor && endSeen == anchor, "B's pass starts from its anchor (anchor %lld end %lld, want %lld)",
		      anchorSeen, endSeen, anchor);
		// B heard, A gone.
		const double p300 = tonePower(L, L.size() - 48000, 48000, 300.0, rate), p440 = tonePower(L, L.size() - 48000, 48000, 440.0, rate);
		CHECK(p300 > 1.0 && p440 < 1e-3 * p300, "B heard (300 Hz %.3g, 440 Hz %.3g)", p300, p440);
		// No click: no step larger than the tone's own around the swap.
		const double before = worstStep(L, T - 48000, T - 4800), around = worstStep(L, T - 4800, T + 96000);
		CHECK(around <= 1.25 * before, "no click at the swap (worst step %.3f V, %.3f V before)", around, before);
		std::printf("swap: A %.3g before; after, 300 Hz %.3g, 440 Hz %.3g; worst step %.3f V (%.3f V before)\n", pa, p300, p440, around, before);
		// RUN: one dip, for the new pass, and high otherwise.
		long long low = 0, dips = 0;
		for (size_t i = T; i < run.size(); i++) {
			low += run[i] < 5.f;
			dips += run[i] < 5.f && run[i - 1] > 5.f;
		}
		CHECK(dips == 1 && low == runDipFrames(rate), "RUN dips once for the new pass (%lld dips, %lld samples low)", dips, low);
		// The window: the spare, now fed, is B's tape, as a view fed B frame by frame has it.
		CHECK(win.fed == 1, "the module switched to the spare view");
		TapeView streamed;
		streamed.configure(cap, 0);
		for (long long i = 0; i < cap; i++) streamed.wrote(bl[size_t(i)], br[size_t(i)]);
		win.views[win.fed].publish(e.state());
		streamed.publish(e.state());
		TapeSnapshot s1, s2;
		CHECK(win.views[win.fed].read(s1) && streamed.read(s2), "both views read");
		long long cols = 0;
		for (int i = 0; i < TapeSnapshot::COLS; i++) cols += s1.low[i] != s2.low[i] || s1.high[i] != s2.high[i];
		CHECK(cols == 0 && s1.high[100] > 4.9f, "the view shows B (%d columns differ; peak %.3f)", int(cols), s1.high[100]);
	}
}

// ---- Review Focus 5.

void missing_file() {
	refused(gTemp->sub("missing"), "missing_file");
	refused((gTemp->dir / "no_such_folder").string(), "missing folder");
}

void truncated_file() {
	const std::string dir = gTemp->sub("truncated");
	std::vector<float> a(48000, 0.25f), b(48000, -0.25f);
	// A header claiming more than is there.
	writeBytes(tapeFile(dir), wavBytes(3, 2, 48000, 32, floatData({a, b}), 48000 * 8 * 4));
	refused(dir, "truncated_file (data shorter than its header)");
	// A real tape cut part way.
	std::string why;
	CHECK(writeTape(dir, *ringOf(std::vector<float>(size_t(tapeFrames(8000.f)), 1.f), std::vector<float>(size_t(tapeFrames(8000.f)), 2.f)), 8000.f, why),
	      "a tape written");
	fs::resize_file(tapeFile(dir), fs::file_size(tapeFile(dir)) / 2);
	refused(dir, "truncated_file (cut in half)");
	writeBytes(tapeFile(dir), std::string("RIFF\x10\0\0\0WAVEfmt ", 16));
	refused(dir, "truncated_file (header only)");
}

void empty_file() {
	const std::string dir = gTemp->sub("empty");
	writeBytes(tapeFile(dir), "");
	refused(dir, "empty_file (0 bytes)");
	writeBytes(tapeFile(dir), wavBytes(3, 2, 48000, 32, ""));
	refused(dir, "empty_file (no frames)");
}

void not_a_wav() {
	const std::string dir = gTemp->sub("not_a_wav");
	writeBytes(tapeFile(dir), "This is not a WAV file, it is a note somebody saved as tape.wav.\n");
	refused(dir, "not_a_wav");
	fs::create_directories(fs::path(tapeFile(dir) + ".d"));
	fs::remove(tapeFile(dir));
	fs::create_directories(tapeFile(dir));
	refused(dir, "not_a_wav (a folder)");
}

void foreign_rate() {
	const std::string dir = gTemp->sub("foreign_rate");
	std::vector<float> a(4000, 0.5f);
	writeBytes(tapeFile(dir), wavBytes(3, 1, 4000, 32, floatData({a})));
	refused(dir, "a rate no tape is made at (4 kHz)");
	Ring out;
	std::string why;
	CHECK(!readTape(gTemp->sub("foreign_rate"), 0.f, out, why) && !why.empty(), "asked for at rate 0: refused");
}

void absurdly_large() {
	const std::string dir = gTemp->sub("absurd");
	// Two tapes and one frame at 8 kHz, mono 16-bit: small on disk, too long for a tape.
	const long long n = 2 * tapeFrames(8000.f) + 1;
	std::string d(size_t(n) * 2, '\0');
	writeBytes(tapeFile(dir), wavBytes(1, 1, 8000, 16, d));
	refused(dir, "absurdly_large (more than two tapes long)");
	// Exactly two tapes is read (the newest tape kept).
	d.resize(size_t(n - 1) * 2);
	writeBytes(tapeFile(dir), wavBytes(1, 1, 8000, 16, d));
	Ring out;
	std::string why;
	CHECK(readTape(dir, 8000.f, out, why) && out.capacity() == tapeFrames(8000.f), "two tapes long: read (%s)", why.c_str());
	// A header claiming billions of frames: refused before anything is allocated.
	writeBytes(tapeFile(dir), wavBytes(3, 2, 48000, 32, std::string(64, '\0'), 0x7ffffff0));
	refused(dir, "absurdly_large (a claimed 2 GB)");
}

void mono_file() {
	const std::string dir = gTemp->sub("mono");
	const float rate = 8000.f;
	const long long cap = tapeFrames(rate);
	std::vector<float> m((size_t) cap);
	for (long long i = 0; i < cap; i++) m[size_t(i)] = float(i % 1000) * 0.001f - 0.5f;
	writeBytes(tapeFile(dir), wavBytes(3, 1, 8000, 32, floatData({m})));
	Ring out;
	std::string why;
	CHECK(readTape(dir, rate, out, why), "mono read: %s", why.c_str());
	std::vector<float> a, b;
	oldestFirst(out, a, b);
	long long bad = 0;
	for (size_t i = 0; i < a.size(); i++) bad += a[i] != m[i] || b[i] != m[i];
	CHECK(bad == 0, "mono: both sides are the file (%lld frames differ)", bad);
}

// A file whose frame i holds i (so where each frame lands can be seen).
std::string rampFile(long long n, uint32_t rate) {
	std::vector<float> l((size_t) n), r((size_t) n);
	for (long long i = 0; i < n; i++) {
		l[size_t(i)] = float(i) * 1e-4f;
		r[size_t(i)] = -float(i) * 1e-4f;
	}
	return wavBytes(3, 2, rate, 32, floatData({l, r}));
}

void too_long() {
	const std::string dir = gTemp->sub("too_long");
	const float rate = 8000.f;
	const long long cap = tapeFrames(rate), n = cap + 8000 * 10;
	writeBytes(tapeFile(dir), rampFile(n, 8000));
	Ring out;
	std::string why;
	CHECK(readTape(dir, rate, out, why) && !why.empty(), "40 s read (%s)", why.c_str());
	std::vector<float> a, b;
	oldestFirst(out, a, b);
	CHECK(a.size() == size_t(cap) && a[0] == float(n - cap) * 1e-4f && a.back() == float(n - 1) * 1e-4f && b.back() == -float(n - 1) * 1e-4f,
	      "the newest 30 s kept (oldest %g, newest %g)", a[0] * 1e4, a.back() * 1e4);
}

void too_short() {
	const std::string dir = gTemp->sub("too_short");
	const float rate = 8000.f;
	const long long cap = tapeFrames(rate), n = 8000 * 10;
	writeBytes(tapeFile(dir), rampFile(n, 8000));
	Ring out;
	std::string why;
	CHECK(readTape(dir, rate, out, why) && !why.empty(), "10 s read (%s)", why.c_str());
	std::vector<float> a, b;
	oldestFirst(out, a, b);
	long long loud = 0;
	for (long long i = 0; i < cap - n; i++) loud += a[size_t(i)] != 0.f || b[size_t(i)] != 0.f;
	CHECK(loud == 0, "the old end is silence (%lld frames not)", loud);
	CHECK(a[size_t(cap - n)] == 0.f && a[size_t(cap - n + 1)] == 1e-4f && a.back() == float(n - 1) * 1e-4f, "then the file, ending with its last frame");
	// A file one frame long, at another rate: a tape, silent but for its end.
	writeBytes(tapeFile(dir), wavBytes(3, 2, 44100, 32, floatData({{0.5f}, {0.5f}})));
	CHECK(readTape(dir, rate, out, why) && out.capacity() == cap, "one frame at 44.1 kHz read: %s", why.c_str());
}

void hostile_samples() {
	const std::string dir = gTemp->sub("hostile");
	std::vector<float> l = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f, 3.4e38f, 99.f, -100.f, 1e-40f, -0.f};
	std::vector<float> r(l.rbegin(), l.rend());
	writeBytes(tapeFile(dir), wavBytes(3, 2, 8000, 32, floatData({l, r})));
	Ring out;
	std::string why;
	CHECK(readTape(dir, 8000.f, out, why), "read");
	std::vector<float> a, b;
	oldestFirst(out, a, b);
	const size_t o = a.size() - l.size();
	const float want[10] = {0.f, 0.f, 0.f, 100.f, -100.f, 100.f, 99.f, -100.f, 1e-40f, -0.f};
	bool ok = true;
	for (size_t i = 0; i < l.size(); i++) ok = ok && bitsOf(a[o + i]) == bitsOf(want[i]) && bitsOf(b[o + l.size() - 1 - i]) == bitsOf(want[i]);
	CHECK(ok, "NaN and inf read as 0 V, the rest held to +-100 V");
}

// ---- The save, a step at a time.

// The file's frames, or nothing.
bool fileFrames(const std::string& dir, std::vector<float>& l, std::vector<float>& r) {
	WavInfo info;
	std::string w;
	if (!rka::probeWav(tapeFile(dir), info, w)) return false;
	l.assign(size_t(info.frames), 0.f);
	r.assign(size_t(info.frames), 0.f);
	return rka::readWav(tapeFile(dir), l.data(), r.data(), info.frames, info, w);
}

void save_during_swap() {
	const float rate = 8000.f;
	const std::string dir = gTemp->sub("save_during_swap");
	DoldrumsEngine e;
	CHECK(e.allocate(rate), "allocate");
	e.setSampleRate(rate);
	F f;
	DoldrumsResult out;
	auto run = [&](int n) {
		for (int i = 0; i < n; i++) {
			f.inL = float(1 + (i % 7));
			e.process(f, out);
		}
	};
	const long long cap = tapeFrames(rate);
	run(int(cap) + 1000);   // tape A: the input, recorded
	uint32_t since = 0;
	std::string why;
	CHECK(!saveFrozen(dir, e, rate, since, why, 0) && !fs::exists(tapeFile(dir)), "recording: not written (%s)", why.c_str());
	f.params[F::FREEZE] = 1.f;
	run(10);
	CHECK(!saveFrozen(dir, e, rate, since, why, 0) && !fs::exists(tapeFile(dir)), "frozen, still fading: not written (%s)", why.c_str());
	run(100);
	std::vector<float> al, ar;
	oldestFirst(e.ring(), al, ar);
	CHECK(saveFrozen(dir, e, rate, since, why) && since == e.frozenSince(), "frozen and still: written (%s)", why.c_str());
	std::vector<float> fl, fr;
	CHECK(fileFrames(dir, fl, fr) && fl == al && fr == ar, "the file is tape A, whole");

	// Now the hold driven by hand, with a swap and a thaw in the middle of it.
	const Ring* held = e.holdStill(&since);
	CHECK(held == e.live() && held == &e.ring(), "held: the live ring");
	// A stored tape B arrives while A is held.
	e.expectStored();
	DoldrumsEngine::TapeJob job;
	CHECK(e.tapeWanted(job), "wanted");
	std::vector<float> bl(size_t(cap), 3.f), br(size_t(cap), -3.f);
	Ring* b = new Ring;
	b->allocate(cap);
	b->fill(bl.data(), br.data());
	CHECK(e.offer(b, job.ticket), "B offered");
	run(1);
	CHECK(e.live() == b, "B taken while A is held");
	// The thaw, held off: nothing is written to B while the hold stands.
	f.params[F::FREEZE] = 0.f;
	run(500);
	CHECK(b->now() == cap, "thawed while held: nothing written (now %lld)", b->now());
	// The copy, of the ring that was held: A, unchanged, whole.
	std::vector<float> hl((size_t) cap), hr((size_t) cap);
	rka::doldrums::store::image(*held, hl.data(), hr.data());
	e.letGo();
	CHECK(hl == al && hr == ar, "the held ring is A, whole, after the swap and the thaw");
	run(1);
	CHECK(b->now() == cap + 1, "let go: the thaw goes on (now %lld)", b->now());
	Ring* a = e.retired();
	CHECK(a == held, "A handed back after the swap");
	delete a;
	// Recording: no save. Frozen again, with a tape on its way: no save either.
	CHECK(!saveFrozen(dir, e, rate, since, why, 0), "recording B: not written");
	f.params[F::FREEZE] = 1.f;
	run(100);
	e.expectStored();
	CHECK(!saveFrozen(dir, e, rate, since, why, 0), "frozen with a tape on its way: not written (%s)", why.c_str());
	run(1);
	CHECK(e.tapeWanted(job), "frozen, the wait stands");
	e.reset();
	f.params[F::FREEZE] = 0.f;
	run(10);
	f.params[F::FREEZE] = 1.f;
	run(100);
	CHECK(!e.tapeWanted(job) && saveFrozen(dir, e, rate, since, why), "after reset and a new freeze: written (%s)", why.c_str());
	std::vector<float> cl, cr;
	oldestFirst(e.ring(), cl, cr);
	CHECK(fileFrames(dir, fl, fr) && fl == cl && fr == cr, "the file is the new tape, whole");
	while (Ring* x = e.retired()) delete x;
}

} // namespace

int main() {
	__sanitizer_install_malloc_and_free_hooks(mallocHook, freeHook);
	std::srand((unsigned) std::time(nullptr));
	TempDir temp;
	gTemp = &temp;
	save_plan();
	round_trip();
	converts_rate();
	swap_under_a_running_engine();
	missing_file();
	truncated_file();
	empty_file();
	not_a_wav();
	foreign_rate();
	absurdly_large();
	mono_file();
	too_long();
	too_short();
	hostile_samples();
	save_during_swap();
	gTemp = nullptr;
	return rkatest::finish("doldrums/test_store");
}

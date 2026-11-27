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
//   alone: remove; a tape on its way: nothing, whatever else).
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
// - save_while_a_load_is_pending_keeps_the_file and
//   save_while_a_conversion_is_pending_keeps_the_file: while the stored tape
//   is being read, or a frozen one converted after a rate change, every save
//   (saveStep, as the module makes it) leaves the file to the byte, whatever
//   the button and Keep say; settled, the ordinary rules; a failed load,
//   given up, leaves a bad file while the button is on and removes it once
//   thawed.
// - save_during_swap: the live tape saved while a swap and a thaw come in the
//   middle of the save (the hold, driven a step at a time): the file is one
//   whole tape or there is none; nothing is written while a tape is on its
//   way or the freeze is still fading.
// - freeze_step: the freeze's own write (freezeStep, TapeStore.hpp 7), the
//   save's path with the save's rules: nothing when the plan is not to
//   write; later while the tape is not still, with no heap operation (the
//   copy is not made ready); a take during the copy (the
//   same seam) writes nothing and asks for later, not for a removal; a
//   write that fails is final for the freeze and leaves the old file to the
//   caller (the keeper removes it: test_keeper); else
//   written, the number taken.
// - a_tape_is_stamped_only_with_its_own_rate: the hold refuses a rate that
//   is not the held tape's (the file would play at the wrong speed).
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
#include <iterator>
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
		{{true, true, 5, 0, false}, SAVE_WRITE, "button on, never written: write"},
		{{true, true, 5, 4, false}, SAVE_WRITE, "button on, written for an earlier freeze: write"},
		{{true, true, 5, 5, false}, SAVE_NOTHING, "button on, written since it froze: nothing"},
		{{true, false, 5, 0, false}, SAVE_REMOVE, "button off (not frozen, or a gate-only freeze): remove"},
		{{true, false, 5, 5, false}, SAVE_REMOVE, "button off, a file from before: remove"},
		{{false, true, 5, 0, false}, SAVE_REMOVE, "Keep off: remove"},
		{{false, true, 5, 5, false}, SAVE_REMOVE, "Keep off, written already: remove"},
		{{false, false, 5, 4, false}, SAVE_REMOVE, "both off: remove"},
		// A tape on its way (the stored one being read, a frozen one converted): the file as it is, whatever else.
		{{true, true, 5, 0, true}, SAVE_NOTHING, "pending, button on, never written: nothing"},
		{{true, true, 5, 4, true}, SAVE_NOTHING, "pending, written for an earlier freeze: nothing"},
		{{true, true, 5, 5, true}, SAVE_NOTHING, "pending, written since: nothing"},
		{{true, false, 5, 5, true}, SAVE_NOTHING, "pending, button off: nothing (not removed)"},
		{{false, true, 5, 5, true}, SAVE_NOTHING, "pending, Keep off: nothing (not removed)"},
		{{false, false, 5, 0, true}, SAVE_NOTHING, "pending, both off: nothing"},
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
	CHECK(savePlan({true, f.params[F::FREEZE] >= 0.5f, e.frozenSince(), 0, e.tapePending()}) == SAVE_REMOVE, "gate-only freeze: remove");
	f.params[F::FREEZE] = 1.f;
	CHECK(savePlan({true, f.params[F::FREEZE] >= 0.5f, e.frozenSince(), 0, e.tapePending()}) == SAVE_WRITE, "the button on too: write");
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

std::string fileBytes(const std::string& dir) {
	std::ifstream in(tapeFile(dir), std::ios::binary);
	if (!in) return "<none>";
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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

	// A save under way while a stored tape B is asked for, handed in and taken,
	// and the input thawed, all while A is held for the copy (the seam runs
	// after the copy, before letGo). The copy is A's frames, but the number the
	// save would give it is the one B carries now (a tape handed in keeps the
	// number), so nothing may be written: the file stays as it was. So that a
	// rewrite would show (A's frames written again would be A's bytes), the
	// file is first made a different tape, C.
	{
		Ring c;
		c.allocate(cap);
		std::vector<float> cl((size_t) cap, 0.25f), cr((size_t) cap, -0.25f);
		c.fill(cl.data(), cr.data());
		CHECK(writeTape(dir, c, rate, why), "tape C written: %s", why.c_str());
	}
	const std::string before = fileBytes(dir);
	CHECK(fileFrames(dir, fl, fr) && fl[0] == 0.25f && fl != al, "the file is tape C, not A");
	uint32_t writtenFor = 0;   // as if A's file had not been written: the plan says write
	const Ring* a = e.live();
	Ring* b = nullptr;
	std::vector<float> bl((size_t) cap, 3.f), br((size_t) cap, -3.f);
	DoldrumsEngine::TapeJob job;
	auto swapAndThaw = [&]() {
		e.expectStored();
		CHECK(e.tapeWanted(job), "wanted");
		b = new Ring;
		b->allocate(cap);
		b->fill(bl.data(), br.data());
		CHECK(e.offer(b, job.ticket), "B offered");
		run(1);
		CHECK(e.live() == b, "B taken while A is held");
		f.params[F::FREEZE] = 0.f;
		run(500);
		CHECK(b->now() == cap, "thawed while held: nothing written (now %lld)", b->now());
	};
	const SaveFacts facts = {true, true, e.frozenSince(), writtenFor, e.tapePending()};
	CHECK(savePlan(facts) == SAVE_WRITE, "the plan: write");
	CHECK(saveStep(facts, dir, e, rate, writtenFor, why, 0, swapAndThaw) == SAVE_NOTHING && writtenFor == 0,
	      "a tape taken during the copy: nothing written (%s)", why.c_str());
	CHECK(fileBytes(dir) == before, "the file is as it was (tape C), not A's frames under B's number");
	CHECK(e.frozenSince() == facts.frozenSince, "B carries the number the save read");
	run(1);
	CHECK(b && b->now() == cap + 1, "let go: the thaw goes on (now %lld)", b ? b->now() : -1);
	Ring* old = e.retired();
	CHECK(old == a, "A handed back after the swap");
	delete old;
	// A later save writes the tape that is live.
	f.params[F::FREEZE] = 1.f;
	run(100);
	CHECK(saveStep({true, true, e.frozenSince(), writtenFor, e.tapePending()}, dir, e, rate, writtenFor, why) == SAVE_WRITE &&
	          writtenFor == e.frozenSince(),
	      "frozen again: written (%s)", why.c_str());
	std::vector<float> ll, lr;
	oldestFirst(e.ring(), ll, lr);
	CHECK(fileFrames(dir, fl, fr) && fl == ll && fr == lr && ll[0] == 3.f, "the file is the live tape (B, recorded on)");
	f.params[F::FREEZE] = 0.f;
	run(10);
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


// ---- A save while a tape is on its way leaves the file as it is.


// The module's save (TapeStore.hpp, 3): the facts from the engine, saveStep,
// and the removal it asks for.
SaveAction moduleSave(DoldrumsEngine& e, const std::string& dir, float rate, bool keep, bool button, uint32_t& writtenFor) {
	std::string why;
	const SaveFacts f = {keep, button, e.frozenSince(), writtenFor, e.tapePending()};
	const SaveAction a = saveStep(f, dir, e, rate, writtenFor, why, 0);
	if (a == SAVE_REMOVE) {
		std::error_code ec;
		fs::remove(tapeFile(dir), ec);
	}
	return a;
}

void save_while_a_load_is_pending_keeps_the_file() {
	const float rate = 8000.f;
	const long long cap = tapeFrames(rate);
	const std::string dir = gTemp->sub("pending_load");
	std::vector<float> tl((size_t) cap), tr((size_t) cap);
	for (long long i = 0; i < cap; i++) {
		tl[size_t(i)] = float(i % 991) * 0.01f - 5.f;
		tr[size_t(i)] = float(i % 313) * -0.02f + 3.f;
	}
	std::string why;
	CHECK(writeTape(dir, *ringOf(tl, tr), rate, why), "the stored tape written");
	const std::string stored = fileBytes(dir);
	{
		// The patch opens frozen by the button, with its stored tape.
		DoldrumsEngine e;
		CHECK(e.allocate(rate), "allocate");
		e.setSampleRate(rate);
		F f;
		f.params[F::FREEZE] = 1.f;
		DoldrumsResult out;
		for (int i = 0; i < 100; i++) e.process(f, out);
		CHECK(!e.tapePending(), "nothing on its way yet");
		e.expectStored();
		uint32_t writtenFor = e.frozenSince();
		CHECK(e.tapePending(), "the stored tape on its way");
		// Every save while it is on its way: the file exactly as it is (left
		// to the ordinary rules these would write the silent stand-in over it,
		// or remove it).
		uint32_t never = 0, same = writtenFor;
		CHECK(moduleSave(e, dir, rate, true, true, never) == SAVE_NOTHING && never == 0, "pending, never written: nothing");
		CHECK(moduleSave(e, dir, rate, true, false, same) == SAVE_NOTHING, "pending, button off: nothing");
		CHECK(moduleSave(e, dir, rate, false, true, same) == SAVE_NOTHING, "pending, Keep off: nothing");
		CHECK(moduleSave(e, dir, rate, true, true, writtenFor) == SAVE_NOTHING, "pending, as the module has it: nothing");
		CHECK(fileBytes(dir) == stored, "the file is untouched while the load is pending");
		// Read and offered, not yet taken: still on its way.
		DoldrumsEngine::TapeJob job;
		CHECK(e.tapeWanted(job), "wanted");
		Ring* r = makeWanted(job, dir, why);
		CHECK(r && e.offer(r, job.ticket), "read and offered: %s", why.c_str());
		CHECK(e.tapePending() && moduleSave(e, dir, rate, true, true, never) == SAVE_NOTHING && fileBytes(dir) == stored,
		      "offered, not yet taken: the file untouched");
		// A write that finds a tape on its way between the plan and the copy: the file stays.
		{
			uint32_t w = 0;
			const SaveFacts stale = {true, true, e.frozenSince(), 0, false};
			CHECK(saveStep(stale, dir, e, rate, w, why, 0) == SAVE_NOTHING && fileBytes(dir) == stored,
			      "a tape went on its way after the plan: the file stays");
		}
		e.process(f, out);
		CHECK(!e.tapePending() && e.tapesTaken() == 1u, "taken: settled");
		CHECK(moduleSave(e, dir, rate, true, true, writtenFor) == SAVE_NOTHING && fileBytes(dir) == stored,
		      "settled, the file holds this tape: nothing");
		uint32_t fresh = 0;
		std::vector<float> fl, fr;
		CHECK(moduleSave(e, dir, rate, true, true, fresh) == SAVE_WRITE && fresh == e.frozenSince() && fileFrames(dir, fl, fr) && fl == tl && fr == tr,
		      "settled, never written: written, and it is the stored tape");
		while (Ring* x = e.retired()) delete x;
	}
	{
		// A stored tape that cannot be read.
		writeBytes(tapeFile(dir), "not a tape at all");
		const std::string bad = fileBytes(dir);
		DoldrumsEngine e;
		CHECK(e.allocate(rate), "allocate");
		e.setSampleRate(rate);
		F f;
		f.params[F::FREEZE] = 1.f;
		DoldrumsResult out;
		for (int i = 0; i < 100; i++) e.process(f, out);
		e.expectStored();
		uint32_t writtenFor = e.frozenSince();
		CHECK(moduleSave(e, dir, rate, true, true, writtenFor) == SAVE_NOTHING && fileBytes(dir) == bad, "pending: the bad file untouched");
		DoldrumsEngine::TapeJob job;
		CHECK(e.tapeWanted(job) && makeWanted(job, dir, why) == nullptr, "the load fails: %s", why.c_str());
		// A newer request is not ended by giving up an older one.
		const uint32_t newer = e.expectStored();
		e.giveUp(job.ticket);
		CHECK(e.tapePending(), "giving up an older ticket leaves a newer request");
		e.giveUp(newer);
		CHECK(!e.tapePending(), "given up: settled");
		for (int i = 0; i < 100; i++) e.process(f, out);
		CHECK(moduleSave(e, dir, rate, true, true, writtenFor) == SAVE_NOTHING && fileBytes(dir) == bad,
		      "settled after a failed load, button on: the bad file stays (not replaced by silence)");
		f.params[F::FREEZE] = 0.f;
		for (int i = 0; i < 100; i++) e.process(f, out);
		CHECK(moduleSave(e, dir, rate, true, false, writtenFor) == SAVE_REMOVE && !fs::exists(tapeFile(dir)), "thawed: removed");
	}
}

void save_while_a_conversion_is_pending_keeps_the_file() {
	const float rate = 8000.f, next = 16000.f;
	const std::string dir = gTemp->sub("pending_conversion");
	DoldrumsEngine e;
	CHECK(e.allocate(rate), "allocate");
	e.setSampleRate(rate);
	F f;
	DoldrumsResult out;
	long long k = 0;
	auto run = [&](long long n) {
		for (long long i = 0; i < n; i++, k++) {
			f.inL = float(k % 977) * 0.01f - 4.f;
			e.process(f, out);
		}
	};
	run(tapeFrames(rate) + 100);
	f.params[F::FREEZE] = 1.f;
	run(100);
	uint32_t writtenFor = 0;
	CHECK(moduleSave(e, dir, rate, true, true, writtenFor) == SAVE_WRITE, "the first freeze written");
	const std::string first = fileBytes(dir);
	// Frozen anew (so the ordinary rules would write), then the rate changes.
	f.params[F::FREEZE] = 0.f;
	run(500);
	f.params[F::FREEZE] = 1.f;
	run(100);
	CHECK(e.frozenSince() != writtenFor, "a new freeze, not yet written");
	CHECK(e.allocate(next), "allocate 16 kHz");
	e.setSampleRate(next);
	run(100);
	CHECK(e.tapePending(), "the conversion on its way");
	uint32_t w = writtenFor;
	CHECK(moduleSave(e, dir, next, true, true, w) == SAVE_NOTHING && w == writtenFor && fileBytes(dir) == first,
	      "pending conversion, button on: the file untouched (not written over with the silent stand-in, not removed)");
	CHECK(moduleSave(e, dir, next, true, false, w) == SAVE_NOTHING && fileBytes(dir) == first, "pending conversion, button off: untouched");
	CHECK(moduleSave(e, dir, next, false, true, w) == SAVE_NOTHING && fileBytes(dir) == first, "pending conversion, Keep off: untouched");
	// Converted and taken: settled, and the new freeze written at the new rate.
	DoldrumsEngine::TapeJob job;
	std::string why;
	CHECK(e.tapeWanted(job) && job.from && job.toRate == next, "the conversion wanted");
	Ring* r = makeWanted(job, dir, why);
	CHECK(r && e.offer(r, job.ticket), "converted and offered: %s", why.c_str());
	run(1);
	CHECK(!e.tapePending() && e.live() == r, "taken: settled");
	CHECK(moduleSave(e, dir, next, true, true, writtenFor) == SAVE_WRITE && writtenFor == e.frozenSince(), "settled: written");
	std::vector<float> fl, fr, al, ar;
	oldestFirst(e.ring(), al, ar);
	WavInfo info;
	std::string wv;
	CHECK(fileFrames(dir, fl, fr) && fl == al && fr == ar && rka::probeWav(tapeFile(dir), info, wv) && info.sampleRate == 16000,
	      "the file is the converted tape at 16 kHz");
	while (Ring* x = e.retired()) delete x;
}

void freeze_step() {
	const float rate = 8000.f;
	const long long cap = tapeFrames(rate);
	const std::string dir = gTemp->sub("freeze_step");
	DoldrumsEngine e;
	CHECK(e.allocate(rate), "allocate");
	e.setSampleRate(rate);
	F f;
	DoldrumsResult out;
	long long k = 0;
	auto run = [&](long long n) {
		for (long long i = 0; i < n; i++, k++) {
			f.inL = float(k % 613) * 0.01f - 3.f;
			e.process(f, out);
		}
	};
	auto facts = [&](bool keep, bool button, uint32_t w) { return SaveFacts{keep, button, e.frozenSince(), w, e.tapePending()}; };
	run(cap + 100);
	std::string why;
	uint32_t w = 0;
	CHECK(freezeStep(facts(true, false, 0), dir, e, rate, w, why) == FREEZE_NONE && !fs::exists(tapeFile(dir)), "button off: none");
	f.params[F::FREEZE] = 1.f;
	run(10);
	const long h0 = gHeapOps;
	const FreezeWrite fading = freezeStep(facts(true, true, 0), dir, e, rate, w, why);
	const long fadingHeap = gHeapOps - h0;
	CHECK(fading == FREEZE_LATER && !fs::exists(tapeFile(dir)) && w == 0, "still fading: later, nothing written");
	CHECK(fadingHeap == 0, "and no copy made ready for it (%ld heap operations)", fadingHeap);
	run(100);
	CHECK(freezeStep(facts(false, true, 0), dir, e, rate, w, why) == FREEZE_NONE && !fs::exists(tapeFile(dir)), "Keep off: none");

	// A take during the copy: nothing written, and later (not a failure).
	{
		Ring c;
		c.allocate(cap);
		std::vector<float> cl((size_t) cap, 0.5f), cr((size_t) cap, -0.5f);
		c.fill(cl.data(), cr.data());
		CHECK(writeTape(dir, c, rate, why), "an older tape on file: %s", why.c_str());
	}
	const std::string older = fileBytes(dir);
	Ring* b = nullptr;
	DoldrumsEngine::TapeJob job;
	const Ring* a = e.live();
	auto take = [&]() {
		e.expectStored();
		CHECK(e.tapeWanted(job), "wanted");
		b = new Ring;
		b->allocate(cap);
		std::vector<float> bl((size_t) cap, 2.f), br((size_t) cap, -2.f);
		b->fill(bl.data(), br.data());
		CHECK(e.offer(b, job.ticket), "offered");
		run(1);
		CHECK(e.live() == b, "taken while held");
	};
	const SaveFacts during = facts(true, true, 0);
	CHECK(freezeStep(during, dir, e, rate, w, why, take) == FREEZE_LATER && w == 0, "a take during the copy: later (%s)", why.c_str());
	CHECK(fileBytes(dir) == older, "the file as it was: not the copy under the new tape's number, not removed");
	Ring* old = e.retired();
	CHECK(old == a, "the old ring back");
	delete old;
	// Then written: the tape now live, the number taken.
	CHECK(freezeStep(facts(true, true, 0), dir, e, rate, w, why) == FREEZE_WRITTEN && w == e.frozenSince(), "written (%s)", why.c_str());
	std::vector<float> fl, fr, ll, lr;
	oldestFirst(e.ring(), ll, lr);
	CHECK(fileFrames(dir, fl, fr) && fl == ll && fr == lr && fl[0] == 2.f, "the file is the live tape");
	CHECK(freezeStep(facts(true, true, w), dir, e, rate, w, why) == FREEZE_NONE, "written already: none");

	// A write that fails: final for this freeze; the number not taken.
	const std::string blocked = (fs::path(gTemp->sub("freeze_step_blocked")) / "folder").string();
	writeBytes(blocked, "a file where the folder should be");
	uint32_t w2 = 0;
	CHECK(freezeStep(facts(true, true, 0), blocked, e, rate, w2, why) == FREEZE_FAILED && w2 == 0, "cannot write: failed (%s)", why.c_str());
	while (Ring* x = e.retired()) delete x;
}

void a_tape_is_stamped_only_with_its_own_rate() {
	const std::string dir = gTemp->sub("own_rate");
	DoldrumsEngine e;
	CHECK(e.allocate(8000.f), "allocate");
	e.setSampleRate(8000.f);
	F f;
	f.params[F::FREEZE] = 1.f;
	DoldrumsResult out;
	for (int i = 0; i < 200; i++) e.process(f, out);
	uint32_t since = 0;
	std::string why;
	CHECK(!saveFrozen(dir, e, 11025.f, since, why, 0) && !fs::exists(tapeFile(dir)), "an 8 kHz tape stamped 11025 Hz: refused (%s)", why.c_str());
	CHECK(saveFrozen(dir, e, 8000.f, since, why, 0) && fs::exists(tapeFile(dir)), "at its own rate: written (%s)", why.c_str());
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
	save_while_a_load_is_pending_keeps_the_file();
	save_while_a_conversion_is_pending_keeps_the_file();
	freeze_step();
	a_tape_is_stamped_only_with_its_own_rate();
	gTemp = nullptr;
	return rkatest::finish("doldrums/test_store");
}

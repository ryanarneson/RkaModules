// Doldrums' window model with real threads, built with ThreadSanitizer
// (tests.mk; it cannot be combined with ASan). Any data race on the view
// fails the run.
//
// handoff(): the audio thread writes a window of one value, publishes, and
// goes on to the next value as fast as it can, with every state flag following
// the value, while the UI thread reads for 2 s. Every snapshot read must be
// whole: all 512 columns of one value, the marks and flags and the word of
// that same value. A snapshot made of two publishes fails the uniformity;
// a reader that never got one, or never saw the value change, fails the counts.
//
// rebuilt_hands_over(): a worker rebuilds a tape's columns while the UI
// thread reads, then hands the view to the audio thread through an atomic, as
// Task 11's arrival of a tape does; the audio thread carries on from it and
// the columns it publishes are the tape's and then the new frames'. A rebuild
// that touched what the reader or the audio thread uses would be a race.
//
// swap_race() (Task 11): the engine runs on the audio thread, frozen, with a
// thaw now and then, and the module's window code after each process()
// (TapeStore.hpp's sequence: two views, the fed one switched when a tape is
// taken); a worker asks for the stored tape, makes rings, rebuilds the spare
// view from each before offering it, and collects and frees what comes back;
// the UI thread reads the shown view throughout. Whichever way the threads
// meet: no race (ThreadSanitizer), every ring made is freed once or is live
// at the end, every take is seen once, and the fed view is the live tape's.
//
// save_race() (Task 11): the engine freezes and thaws on the audio thread,
// which, each time the tape comes to stand still, notes a hash of it under
// the frozen tape's number; a worker saves (saveFrozen) as fast as it can and
// hashes each file written. Every file must be the whole tape of the number
// the save gave it (none torn by a thaw), whichever saves succeed.
#define RKA_WAV_IMPLEMENTATION
#include "TapeStore.hpp"
#include "TapeView.hpp"
#include "testing.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <thread>
#include <utility>
#include <vector>

extern "C" const char* __tsan_default_options() {
	return "halt_on_error=1";
}

namespace {

using namespace rka::doldrums;

const long long kCap = 5120;   // 10 frames a column

DoldrumsState stateFor(long long k, long long now) {
	DoldrumsState s;
	s.now = now;
	s.capacity = kCap;
	s.grain = 100 + int(k % 50);
	s.end = now - 500 - k % 1000;
	s.anchor = now - 1000 - k % 777;
	s.frozen = (k & 1) != 0;
	s.held = (k & 2) != 0;
	s.stopped = (k & 4) != 0;
	return s;
}

void handoff() {
	TapeView view;
	view.configure(kCap, 0);
	std::atomic<bool> stop{false};
	std::atomic<long long> published{0};

	std::thread audio([&]() {
		long long k = 0, now = 0;
		while (!stop.load(std::memory_order_relaxed)) {
			k++;
			const float v = float(k);   // an integer: exact in a float up to 2^24
			for (long long i = 0; i < kCap + 3; i++) {
				view.wrote(v, -v);
				now++;
			}
			view.publish(stateFor(k, now));
			published.store(k, std::memory_order_relaxed);
		}
	});

	bool untouched = true;
	long long reads = 0, failed = 0, bad = 0, lastK = 0, changes = 0;
	const auto t0 = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
		TapeSnapshot s;
		std::memset(&s, 0x5a, sizeof s);
		if (!view.read(s)) {
			// Giving up leaves `out` as it was (test_view pins it deterministically;
			// whether a read collides here depends on the machine, so it is not required).
			unsigned char* b = (unsigned char*) &s;
			bool same = true;
			for (size_t i = 0; i < sizeof s; i++) same = same && b[i] == 0x5a;
			if (!same)
				untouched = false;
			failed++;
			continue;
		}
		reads++;
		const long long k = (long long) s.high[0];
		bool ok = k >= 1;
		// (The open column is empty, 0 and 0, when the frames just ended a column.)
		for (int i = 0; ok && i < 512; i++)
			ok = (s.high[i] == float(k) && s.low[i] == -float(k)) || (i == 511 && s.high[i] == 0.f && s.low[i] == 0.f);
		if (ok) {
			// The marks, the flags and the word of the same publish.
			const DoldrumsState st = stateFor(k, 0);
			ok = s.frozen == st.frozen && s.held == st.held && s.stopped == st.stopped;
			const char* want = st.stopped ? (st.frozen ? "FROZEN STOPPED" : "STOPPED")
			                              : st.held ? (st.frozen ? "FROZEN HELD" : "HELD") : st.frozen ? "FROZEN" : "";
			ok = ok && std::strcmp(s.state, want) == 0;
			const double base = -double(kCap);
			ok = ok && s.anchor == float(double(-1000 - k % 777 - base) / kCap);
			ok = ok && s.readEnd == float(double(-500 - k % 1000 - base) / kCap);
		}
		if (!ok) {
			if (!bad)
				std::printf("first bad snapshot: k %lld, high[0] %g, high[511] %g, state \"%s\"\n", k, s.high[0], s.high[511], s.state);
			bad++;
		}
		if (k != lastK)
			changes++;
		lastK = k;
	}
	stop = true;
	audio.join();
	CHECK(bad == 0, "%lld of %lld snapshots were not whole", bad, reads);
	CHECK(untouched, "a read that gave up changed its output");
	CHECK(reads > 1000, "the reader got %lld snapshots", reads);
	CHECK(changes > 100, "the reader saw the value change %lld times", changes);
	CHECK(published.load() > 100, "the writer published %lld times", (long long) published.load());
	TapeSnapshot s;
	CHECK(view.read(s) && (long long) s.high[0] == published.load(), "the last snapshot is the last publish");
	std::printf("handoff: %lld publishes, %lld whole snapshots read, %lld retries gave up\n", (long long) published.load(), reads, failed);
}

void rebuilt_hands_over() {
	for (int round = 0; round < 50; round++) {
		Ring ring;
		ring.allocate(kCap);
		for (long long i = 0; i < kCap + 500; i++)
			ring.write(float(i % 13) - 6.f, float(i % 7) - 3.f, false);
		TapeView view;
		std::atomic<bool> ready{false}, done{false};
		std::atomic<long> early{0}, wrong{0}, reads{0};

		std::thread audio([&]() {
			while (!ready.load(std::memory_order_acquire))
				std::this_thread::yield();
			for (int i = 0; i < 103; i++)
				view.wrote(100.f, -100.f);
			DoldrumsState s;
			s.capacity = kCap;
			s.now = ring.now() + 103;
			view.publish(s);
			done.store(true, std::memory_order_release);
		});
		// The UI thread reads throughout. Nothing is published until the audio
		// thread does, so before the hand-over every read must be false, and
		// after it every snapshot is the one publish: the tape's columns, then the
		// new frames in the open column.
		std::thread ui([&]() {
			while (!done.load(std::memory_order_acquire)) {
				const bool before = !ready.load(std::memory_order_acquire);
				TapeSnapshot t;
				if (!view.read(t))
					continue;
				reads++;
				if (before)
					early++;
				if (!(t.high[511] == 100.f && t.low[511] == -100.f && t.high[300] > 0.f && t.high[300] != 100.f))
					wrong++;
			}
		});
		// The worker: rebuild, publish nothing (the audio thread's), hand over.
		view.rebuild(ring);
		ready.store(true, std::memory_order_release);
		audio.join();
		ui.join();
		CHECK(early == 0, "round %d: %ld snapshots were read before anything was published", round, early.load());
		CHECK(wrong == 0, "round %d: %ld snapshots were not the published one", round, wrong.load());
		TapeSnapshot last;
		CHECK(view.read(last), "read after the hand-over");
		CHECK(last.high[511] == 100.f && last.low[511] == -100.f, "the new frames are in the open column: %g", last.high[511]);
		CHECK(last.high[300] != 100.f && last.high[300] > 0.f, "the tape's columns are still there");
	}
}


// ---- Task 11: tapes handed in, and the save.

typedef DoldrumsFrame F;

// The module's window code, as TapeStore.hpp's sequence has it: after each
// process(), switch to the spare view when a tape has been taken, feed the
// fed view each frame written, publish now and then.
struct WindowModel {
	TapeView views[2];
	std::atomic<int> shown{0};
	std::atomic<uint32_t> switchedAt{0};
	int fed = 0;
	uint32_t seen = 0;
	long long fedTo = 0;   // the frames fed so far (an absolute index)
	long long k = 0;

	void after(DoldrumsEngine& e) {
		if (e.tapesTaken() != seen) {
			seen = e.tapesTaken();
			fed = 1 - fed;
			fedTo = e.ring().now();   // the spare was rebuilt from the ring as it was offered
			shown.store(fed, std::memory_order_release);
			switchedAt.store(seen, std::memory_order_release);
		}
		const Ring& g = e.ring();
		for (; fedTo < g.now(); fedTo++) {
			const long long p = g.physical(fedTo);
			views[fed].wrote(g.channel(0)[p], g.channel(1)[p]);
		}
		if (++k % 64 == 0) views[fed].publish(e.state());
	}
};

void swap_race() {
	const float rate = 8000.f;
	DoldrumsEngine e;
	CHECK(e.allocate(rate), "allocate");
	e.setSampleRate(rate);
	const long long cap = tapeFrames(rate);
	WindowModel win;
	win.views[0].configure(cap, 0);
	std::atomic<bool> stop{false};
	long long made = 0, freed = 0, takes = 0, offers = 0;
	Ring* outstanding = nullptr;
	uint32_t takenAt = 0;

	long long samples = 0;
	std::thread audio([&]() {
		F f;
		DoldrumsResult out;
		const auto start = std::chrono::steady_clock::now();
		while (!stop.load(std::memory_order_relaxed)) {
			samples++;
			// A thaw for 5 ms in every 150 (by the clock, so that the worker,
			// whatever its speed, has offers land frozen and thawed).
			const long long ms = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			f.params[F::FREEZE] = ms % 150 < 145 ? 1.f : 0.f;
			f.inL = float(samples % 50 - 25) * 0.1f;
			e.process(f, out);
			win.after(e);
		}
	});
	std::thread worker([&]() {
		float v = 1.f;
		while (!stop.load(std::memory_order_relaxed)) {
			while (Ring* r = e.retired()) {
				if (r == outstanding && e.tapesTaken() == takenAt) outstanding = nullptr;   // refused
				delete r;
				freed++;
			}
			if (outstanding && e.tapesTaken() != takenAt) {
				// Taken: the spare is the audio thread's once it has switched to it.
				if (win.switchedAt.load(std::memory_order_acquire) != e.tapesTaken()) {
					std::this_thread::yield();
					continue;
				}
				outstanding = nullptr;
				takes++;
			}
			if (outstanding) {
				std::this_thread::yield();
				continue;
			}
			DoldrumsEngine::TapeJob job;
			if (!e.tapeWanted(job)) {
				e.expectStored();   // the UI thread's call; nothing else changes the rings meanwhile
				continue;
			}
			Ring* r = new Ring;
			r->allocate(cap);
			std::vector<float> l(size_t(cap), v), rr(size_t(cap), -v);
			v += 1.f;
			r->fill(l.data(), rr.data());
			made++;
			win.views[1 - win.shown.load(std::memory_order_acquire)].rebuild(*r);
			takenAt = e.tapesTaken();
			while (!e.offer(r, job.ticket)) std::this_thread::yield();
			offers++;
			outstanding = r;
		}
	});
	long long reads = 0;
	const auto t0 = std::chrono::steady_clock::now();
	while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3)) {
		TapeSnapshot s;
		if (win.views[win.shown.load(std::memory_order_acquire)].read(s)) reads++;
	}
	stop = true;
	audio.join();
	worker.join();
	// Settle on this thread (the audio and worker threads are done): the
	// engine looks at an offer still waiting, and the module switches if it
	// was taken.
	F f;
	f.params[F::FREEZE] = 1.f;
	DoldrumsResult out;
	for (int i = 0; i < 10; i++) {
		e.process(f, out);
		win.after(e);
	}
	if (outstanding && e.tapesTaken() != takenAt) takes++;
	while (Ring* r = e.retired()) {
		delete r;
		freed++;
	}
	// The rings made here and the engine's first are freed once each, but for
	// the one live (the first, or one made here): made + 1 = freed + 1.
	CHECK(made == freed, "every ring is freed once or live: %lld made, %lld freed", made, freed);
	CHECK(e.live() != nullptr && e.retired() == nullptr, "one ring live, none left over");
	CHECK(takes == (long long) e.tapesTaken(), "every take seen once: %lld seen, %u taken", takes, e.tapesTaken());
	// The fed view is the live tape's.
	TapeView ref;
	ref.rebuild(e.ring());
	win.views[win.fed].publish(e.state());
	ref.publish(e.state());
	TapeSnapshot a, b;
	CHECK(win.views[win.fed].read(a) && ref.read(b), "both views read");
	int cols = 0;
	for (int i = 0; i < TapeSnapshot::COLS; i++) cols += a.low[i] != b.low[i] || a.high[i] != b.high[i];
	CHECK(cols == 0, "the fed view is the live tape's (%d columns differ)", cols);
	std::printf("swap_race: %lld samples, %lld rings made, %lld offered, %u taken, %lld snapshots read\n", samples, made, offers, e.tapesTaken(), reads);
}

uint64_t hashTape(const float* l, const float* r, size_t n) {
	uint64_t h = 1469598103934665603ull;
	const float* ch[2] = {l, r};
	for (const float* c : ch)
		for (size_t i = 0; i < n; i++) {
			uint32_t b;
			std::memcpy(&b, &c[i], 4);
			h = (h ^ b) * 1099511628211ull;
		}
	return h;
}

void save_race() {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() /
	                     ("rka_doldrums_save_race_" + std::to_string((long long) std::rand()) + "_" + std::to_string((long long) std::time(nullptr)));
	fs::create_directories(dir);
	const float rate = 8000.f;
	DoldrumsEngine e;
	CHECK(e.allocate(rate), "allocate");
	e.setSampleRate(rate);
	const long long cap = tapeFrames(rate);
	std::map<uint32_t, uint64_t> still;   // the audio thread's: the frozen tape's number -> its hash
	F f;
	DoldrumsResult out;
	long long k = 0;
	uint32_t lastNoted = 0;
	auto step = [&]() {
		k++;
		f.inL = float(k % 977) * 0.01f - 4.f;
		f.inR = float(k % 331) * -0.02f + 3.f;
		f.inRPatched = true;
		e.process(f, out);
		if (out.frozen && e.ring().fadedOut() && e.frozenSince() != lastNoted) {
			lastNoted = e.frozenSince();
			std::vector<float> l((size_t) cap), r((size_t) cap);
			store::image(e.ring(), l.data(), r.data());
			still[lastNoted] = hashTape(l.data(), r.data(), l.size());
		}
	};
	auto fileHash = [&](uint64_t& h) {
		std::vector<float> l((size_t) cap), r((size_t) cap);
		rka::WavInfo info;
		std::string w;
		if (!rka::readWav((dir / STORED_TAPE).string(), l.data(), r.data(), l.size(), info, w) || info.frames != uint64_t(cap)) return false;
		h = hashTape(l.data(), r.data(), l.size());
		return true;
	};
	// First, on one thread: a save of a still tape is that tape.
	for (long long i = 0; i < cap; i++) step();
	f.params[F::FREEZE] = 1.f;
	for (int i = 0; i < 100; i++) step();
	uint32_t since = 0;
	std::string why;
	uint64_t h = 0;
	CHECK(saveFrozen(dir.string(), e, rate, since, why) && fileHash(h) && still.count(since) && still[since] == h, "a still tape saved whole");

	std::atomic<bool> stop{false};
	std::vector<std::pair<uint32_t, uint64_t>> saved;
	long long refused = 0, unreadable = 0;
	std::thread audio([&]() {
		long long n = 0;
		while (!stop.load(std::memory_order_relaxed)) {
			n++;
			f.params[F::FREEZE] = n % 2500 < 2000 ? 1.f : 0.f;   // frozen 250 ms, thawed 62 ms
			step();
		}
	});
	std::thread worker([&]() {
		while (!stop.load(std::memory_order_relaxed)) {
			uint32_t s = 0;
			std::string w;
			if (saveFrozen(dir.string(), e, rate, s, w, 0)) {
				uint64_t fh = 0;
				if (fileHash(fh)) saved.push_back({s, fh});
				else unreadable++;
			} else {
				std::error_code ec;
				fs::remove(dir / STORED_TAPE, ec);   // the module's part: none, then
				refused++;
			}
		}
	});
	std::this_thread::sleep_for(std::chrono::seconds(3));
	stop = true;
	audio.join();
	worker.join();
	long long bad = 0;
	for (const auto& x : saved) bad += !(still.count(x.first) && still[x.first] == x.second);
	CHECK(bad == 0 && unreadable == 0, "every file written is the whole tape of its number (%lld of %zu not; %lld unreadable)", bad, saved.size(),
	      unreadable);
	std::printf("save_race: %zu saves written, %lld refused, %zu still tapes noted\n", saved.size(), refused, still.size());
	std::error_code ec;
	fs::remove_all(dir, ec);
}

} // namespace

int main() {
	handoff();
	rebuilt_hands_over();
	std::srand((unsigned) std::time(nullptr));
	swap_race();
	save_race();
	return rkatest::finish("doldrums/test_threads");
}

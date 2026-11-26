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
#include "TapeView.hpp"
#include "testing.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

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
			// Giving up leaves `out` as it was.
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
	CHECK(failed > 0, "no read ever gave up, so `out` untouched was not exercised");
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

} // namespace

int main() {
	handoff();
	rebuilt_hands_over();
	return rkatest::finish("doldrums/test_threads");
}

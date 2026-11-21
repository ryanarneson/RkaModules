// Eddy's engine with real threads, built with ThreadSanitizer (tests.mk; it
// cannot be combined with ASan). The test is the report: any data race on a
// set, a tape, an overview or the engine's own state fails the run.
//
// The audio thread (main) runs process() with the trigger storm of
// test_transport's `storm` (PLAY, RESTORE, REC and DUB as audio-rate squares,
// the four buttons pressed on their edges) for 20 s of audio. A real worker
// thread does what the module's worker does: makes and loads sets and offers
// them (then setTransport, as a patch load would), collects what the engine
// hands back (copying a stale set's original to its tape, as the contract
// says), does the copies the engine asks for, and asks for Restore, "Make
// this the original", Clear and a rate change now and then. A third thread
// reads the live set's overview (lo and hi) as the window does.
//
// Sets are never freed while the threads run: the worker reuses the sets the
// engine handed back, so the window's thread can read an overview through
// live() without racing a free. (In the module, the live() readers and the
// thread that frees sets must be the same thread or ordered.) After the run
// the worker and the window are joined, then the engine is destroyed, then
// the sets are freed.
//
// Not here: the one accepted race of the design, the worker reading a live
// tape to save it while the engine may write it (Wear, Dub): a torn read is
// one sample of wear either side. That is the module's save, not the engine
// (TESTING.md, Eddy).
#include "testing.hpp"
#include "eddy/EddyEngine.hpp"
#include "hadal/Random.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <thread>
#include <vector>

// The first report ends the run with a failure.
extern "C" const char* __tsan_default_options() {
	return "halt_on_error=1";
}

namespace {

using rka::EddyEngine;
using rka::EddyFrame;
using rka::EddyResult;
using rka::Tape;
using rka::TapeSet;
typedef EddyFrame F;

void copyTape(Tape& to, const Tape& from) {
	for (int c = 0; c < 2; c++)
		std::memcpy(to.ch[c], from.ch[c], (from.length + from.tail) * sizeof(float));
	to.length = from.length;
	to.tail = from.tail;
}

// Every set either thread made, in one list the main thread frees at the end.
struct Sets {
	std::vector<TapeSet*> made;
	~Sets() {
		for (TapeSet* s : made)
			delete s;
	}
};

struct Worker {
	EddyEngine& e;
	std::vector<TapeSet*>& made;
	std::vector<TapeSet*> spare;   // sets the engine does not hold
	float fs = 48000.f;
	long offered = 0, collected = 0, copies = 0, clears = 0, rateChanges = 0, stale = 0;
	rka::hadal::Random rnd;

	Worker(EddyEngine& engine, std::vector<TapeSet*>& m) : e(engine), made(m) {
		rnd.seed(99);
	}

	// A spare set at the current rate, or a new one.
	TapeSet* spareSet() {
		for (size_t i = 0; i < spare.size(); i++)
			if (spare[i]->fs == fs) {
				TapeSet* s = spare[i];
				spare.erase(spare.begin() + (long) i);
				return s;
			}
		TapeSet* s = new TapeSet;
		made.push_back(s);
		s->allocate(fs);
		return s;
	}
	// Loaded: noise, 50 ms to 1 s, with or without a tail, tape and
	// original equal, the overview built.
	void load(TapeSet* s) {
		const double secs[3] = {0.05, 0.3, 1.0};
		const size_t n = (size_t) (secs[rnd.next() % 3] * s->fs);
		const size_t tail = rnd.unit() < 0.5f ? 0 : s->tape.tailCapacity();
		for (size_t k = 0; k < n + tail; k++)
			for (int c = 0; c < 2; c++)
				s->tape.ch[c][k] = s->original.ch[c][k] = 4.f * rnd.uniform();
		s->tape.length = s->original.length = n;
		s->tape.tail = s->original.tail = tail;
		s->tapeStale = false;
		s->overview.build(s->tape);
	}

	// The copy the engine asks for, and the sets it hands back.
	void service() {
		const EddyEngine::Copy c = e.copyWanted();
		if (c != EddyEngine::NONE) {
			TapeSet* s = e.live();
			if (c == EddyEngine::ORIGINAL_TO_TAPE)
				copyTape(s->tape, s->original);
			else
				copyTape(s->original, s->tape);
			copies++;
			e.copyDone();
		}
		while (TapeSet* s = e.collect()) {
			if (s->tapeStale) {
				copyTape(s->tape, s->original);
				s->tapeStale = false;
				stale++;
			}
			spare.push_back(s);
			collected++;
		}
	}

	// rateNow: the audio thread asks for a rate change (while it records).
	void run(const std::atomic<bool>& stop, std::atomic<bool>& rateNow) {
		for (long it = 0; !stop.load(std::memory_order_acquire); it++) {
			service();
			if (it % 150 == 0) {
				TapeSet* s = spareSet();
				load(s);
				if (e.offer(s)) {
					offered++;
					if (rnd.unit() < 0.5f)
						e.setTransport(true, rnd.unit() < 0.3f);
				}
				else
					spare.push_back(s);
			}
			if (it % 37 == 0)
				e.requestCopy(rnd.unit() < 0.5f ? EddyEngine::ORIGINAL_TO_TAPE : EddyEngine::TAPE_TO_ORIGINAL);
			if (it % 501 == 0) {
				e.clear();
				clears++;
			}
			if (it % 300 == 299 || rateNow.exchange(false, std::memory_order_relaxed)) {
				fs = fs == 48000.f ? 44100.f : 48000.f;
				e.setSampleRate(fs);
				rateChanges++;
			}
			std::this_thread::sleep_for(std::chrono::microseconds(100));
		}
	}
};

void threads() {
	Sets sets;
	std::unique_ptr<EddyEngine> engine(new EddyEngine(1));
	EddyEngine& e = *engine;
	std::atomic<bool> stop{false}, rateNow{false};
	Worker w(e, sets.made);
	std::thread worker([&] { w.run(stop, rateNow); });

	// The window: the live set's overview, as it draws it.
	long reads = 0, notFinite = 0;
	std::thread window([&] {
		while (!stop.load(std::memory_order_acquire)) {
			if (const TapeSet* s = e.live()) {
				for (int c = 0; c < rka::Overview::COLS; c++) {
					const float lo = s->overview.lo[c].load(std::memory_order_relaxed);
					const float hi = s->overview.hi[c].load(std::memory_order_relaxed);
					notFinite += std::isfinite(lo) && std::isfinite(hi) ? 0 : 1;
				}
				reads++;
			}
			(void) e.tapeChanges();
			(void) e.originalChanges();
			std::this_thread::sleep_for(std::chrono::microseconds(50));
		}
	});

	// The audio thread: the storm, for 20 s of audio.
	F f;
	EddyResult r;
	f.inRPatched = true;
	f.playPress = true;
	int most = 0;
	long bad = 0, playing = 0, recording = 0;
	const long n = 20 * 48000;
	auto square = [](long i, int phase) { return ((i * 10 + phase) / 48) % 2 == 1; };
	bool last[6] = {};
	for (long i = 0; i < n; i++) {
		bool sq[6];
		for (int k = 0; k < 6; k++)
			sq[k] = square(i, 7 * k);
		f.recGate = sq[0] ? 10.f : 0.f;
		f.dubGate = sq[1] ? 10.f : 0.f;
		f.playTrig = sq[2] ? 10.f : 0.f;
		f.restoreTrig = sq[3] ? 10.f : 0.f;
		f.recPress = sq[4] && !last[4];
		f.dubPress = sq[5] && !last[5];
		f.playPress = f.playPress || (sq[2] && !last[2]);
		f.restorePress = sq[3] && !last[3];
		for (int k = 0; k < 6; k++)
			last[k] = sq[k];
		// Every 20 000 samples the storm gives way: to the REC gate held high
		// (a recording that stands, which a rate change asked for halfway
		// ends, leaving the set stale), then to nothing (the recording ends,
		// its tail, its copy).
		const long phase = (i / 20000) % 3;
		if (phase == 1 && i % 20000 == 10000)
			rateNow.store(true, std::memory_order_relaxed);
		if (phase != 0) {
			f.recGate = phase == 1 ? 10.f : 0.f;
			f.dubGate = f.playTrig = f.restoreTrig = 0.f;
			f.recPress = f.dubPress = f.restorePress = false;
		}
		f.inL = 2.f * (float) std::sin((double) i * 0.01);
		f.inR = -f.inL;
		e.process(f, r);
		f.recPress = f.dubPress = f.playPress = f.restorePress = false;
		most = std::max(most, e.activeVoices());
		bad += std::fabs(r.l) <= 10.f && std::fabs(r.r) <= 10.f ? 0 : 1;
		playing += r.playing ? 1 : 0;
		recording += r.recording ? 1 : 0;
	}
	stop.store(true, std::memory_order_release);
	worker.join();
	window.join();

	CHECK(bad == 0 && most <= 4, "during the storm the outputs stayed finite and under 10 V (%ld bad) with at most 4 voices (%d)", bad,
	      most);
	CHECK(w.offered > 20 && w.collected > 20 && w.copies > 10 && w.clears > 5 && w.rateChanges > 10 && w.stale > 0,
	      "the worker offered %ld sets, collected %ld (%ld stale), made %ld copies, %ld clears and %ld rate changes", w.offered,
	      w.collected, w.stale, w.copies, w.clears, w.rateChanges);
	CHECK(reads > 100 && notFinite == 0, "the window read %ld overviews, %ld columns not finite", reads, notFinite);
	CHECK(playing > n / 10 && recording > n / 20, "the storm played (%ld samples) and recorded (%ld)", playing, recording);

	// One thread now, the main one as both: back at 48 kHz, a last set
	// taken, then the last edges: everything low, a Restore, a PLAY trigger.
	w.fs = 48000.f;
	e.setSampleRate(48000.f);
	TapeSet* lastSet = w.spareSet();
	w.load(lastSet);
	auto step = [&] {
		e.process(f, r);
		f.recPress = f.dubPress = f.playPress = f.restorePress = false;
		w.service();
	};
	f.recGate = f.dubGate = f.playTrig = f.restoreTrig = 0.f;
	for (int i = 0; i < 100000 && !e.offer(lastSet); i++)
		step();
	for (int i = 0; i < 100000 && e.live() != lastSet; i++)
		step();
	CHECK(e.live() == lastSet, "the last set is taken");
	for (int i = 0; i < 100; i++)
		step();
	f.restoreTrig = 10.f;
	for (int i = 0; i < 10; i++)
		step();
	f.restoreTrig = 0.f;
	for (int i = 0; i < 100; i++)
		step();
	f.playTrig = 10.f;
	for (int i = 0; i < 10; i++)
		step();
	f.playTrig = 0.f;
	for (int i = 0; i < 48000; i++)
		step();
	CHECK(!r.recording && !r.dubbing && r.playing, "a second later: not recording, not dubbing, playing");
	CHECK(e.copyWanted() == EddyEngine::NONE, "no copy wanted");
	CHECK(lastSet->tape.length == lastSet->original.length &&
	          std::memcmp(lastSet->tape.ch[0], lastSet->original.ch[0], lastSet->tape.length * sizeof(float)) == 0,
	      "restored: the tape is the original");

	// No set lost or handed back twice: each is spare once, or live.
	std::set<TapeSet*> spare(w.spare.begin(), w.spare.end());
	CHECK(spare.size() == w.spare.size(), "no set was handed back twice (%zu spare, %zu distinct)", w.spare.size(), spare.size());
	long lost = 0;
	for (TapeSet* s : sets.made)
		lost += (spare.count(s) == 1) != (s != e.live()) ? 1 : 0;
	CHECK(lost == 0, "every one of the %zu sets is spare or live (%ld are neither or both)", sets.made.size(), lost);

	// The worker and the window are joined; now the engine goes, then the sets.
	engine.reset();
}

} // namespace

int main() {
	threads();
	return rkatest::finish("eddy test_threads");
}

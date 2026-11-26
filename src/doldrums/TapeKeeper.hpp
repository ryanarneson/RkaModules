#pragma once
// The module's side of Doldrums' tape hand-over, Rack-free so that it runs
// under the thread tests (TESTING.md rule 3; test_keeper.cpp): the worker
// thread that reads the patch's stored tape, converts a frozen tape at a rate
// change and makes the save; the two views of the window and the switch
// between them; and the save's bookkeeping. It makes exactly the calls
// TapeStore.hpp's header lists for the module, in that order and on those
// threads; Doldrums.cpp only maps Rack's events onto it.
//
// Threads (each call says which):
// - The audio thread: afterProcess(), after every engine.process(). It
//   switches to the spare view when tapesTaken() moves (TapeStore.hpp 2d:
//   the index for the UI and the count it switched at, both release), feeds
//   the view wrote() for each frame that went onto the tape and publish()
//   about every `publishEvery` samples.
// - With the engine's process() held off (the module's constructor,
//   onSampleRateChange, onReset, onAdd; Rack holds its lock exclusively):
//   configured() after allocate / setSampleRate, cleared() after reset, and
//   expectStored().
// - The worker (its own thread, between start() and stop()): every time it
//   wakes, and every 5 ms at the latest: a save if one is asked for, then
//   2a (collect the rings handed back: only it frees rings, and only between
//   jobs, so a conversion's source is never freed while it reads it, and
//   the engine's eight slots never fill), settle the last offer, 2b (make
//   the tape wanted, once per ticket; giveUp(ticket) and a line in the log
//   if it cannot), 2c (rebuild the spare view from the new ring, which is
//   the worker's alone until offered, then offer it; again a few ms later if
//   one is still waiting). The worker touches the spare view only while no
//   offer is unsettled: taken (switchedAt, acquired, has moved: the audio
//   thread's last use of that view happened before) or refused (the ring
//   came back with no take since).
// - The UI thread (any number, for read; one at a time for save): read()
//   on the view `front` names; save(), which hands the save to the worker
//   and waits for it, so that a caller who then packs the folder (Rack, as
//   onSave returns) finds tape.wav written or declined. The save's hold is
//   taken on the worker only, only for the copy (saveStep), and one save at
//   a time. Rack calls onSave with its engine lock held shared, so no
//   allocate, setSampleRate, reset or expectStored (which need it exclusive)
//   can meet a save.
//
// What it says (TESTING.md rule 14). Every tape that could not be read or
// written goes to `log` (warn true); a patch saved without its tape file is
// one line with warn false (the design: it opens silent, with no error).
// Where the user can act, trouble() says so for the window: NOT_READ (the
// stored file exists and could not be read; it is kept while the button is
// on, TapeStore.hpp) until a save writes or removes it or the module is
// initialised; NOT_SAVED (a save meant to write the frozen tape and did not,
// and the old file was removed) until a save writes or removes by plan.
//
// Rings: the worker's ready ring (made, not yet offered) is freed by the
// destructor, after stop(); everything the engine holds it frees itself.
#include "DoldrumsEngine.hpp"
#include "TapeStore.hpp"
#include "TapeView.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace rka { namespace doldrums {

struct TapeKeeper {
	enum Trouble { FINE = 0, NOT_READ = 1, NOT_SAVED = 2 };
	struct SaveAsk {
		std::string dir;                    // the patch's storage folder
		bool keepTape = true, button = false;
		float rate = 48000.f;               // the engine's rate (the file's)
	};

	// Set before start(); called on the worker. log: a line for the log
	// (warn: a warning, else information). makeDir: the storage folder made
	// before a write. exists: a file is there (to tell a patch saved without
	// its tape from a bad file). Defaults: nothing, nothing, fopen.
	std::function<void(bool warn, const std::string&)> log;
	std::function<void(const std::string& dir)> makeDir;
	std::function<bool(const std::string& path)> exists;

	explicit TapeKeeper(DoldrumsEngine& engine) : engine_(engine) {}
	TapeKeeper(const TapeKeeper&) = delete;
	TapeKeeper& operator=(const TapeKeeper&) = delete;
	~TapeKeeper() {
		stop();
		delete ready_;
	}

	// ---- process() held off.

	// After engine.allocate / setSampleRate: the fed view for the tape now.
	void configured() {
		const DoldrumsState& s = engine_.state();
		views_[fed_].configure(s.capacity, s.now);
		lastNow_ = s.now;
		countdown_ = 0;
	}
	// After engine.reset() (Initialize): the picture blank, no trouble.
	void cleared() {
		views_[fed_].clear();
		lastNow_ = engine_.ring().now();
		countdown_ = 0;
		trouble_.store(FINE);
	}
	// A patch opening frozen with its tape kept in `dir` (TapeStore.hpp 1).
	void expectStored(const std::string& dir) {
		{
			std::lock_guard<std::mutex> lk(lock_);
			storeDir_ = dir;
		}
		engine_.expectStored();
		writtenFor_.store(engine_.frozenSince());   // the file holds the tape that is on its way
		wake();
	}

	// ---- The audio thread, after each engine.process(). True when it
	// published (the caller may publish its own figures then).
	bool afterProcess(int publishEvery) {
		const uint32_t taken = engine_.tapesTaken();
		Ring& g = engine_.ring();
		const long long now = g.now();
		if (taken != takenSeen_) {
			// The ring taken is the one the spare was rebuilt for (one offer at a time).
			takenSeen_ = taken;
			fed_ ^= 1;
			front_.store(fed_, std::memory_order_release);
			switchedAt_.store(taken, std::memory_order_release);
			countdown_ = 0;   // its picture at once, not one this view had before
		}
		else if (now == lastNow_ + 1) {
			const long long p = g.physical(now - 1);
			views_[fed_].wrote(g.channel(0)[p], g.channel(1)[p]);
		}
		lastNow_ = now;
		if (--countdown_ > 0)
			return false;
		countdown_ = publishEvery > 0 ? publishEvery : 1;
		views_[fed_].publish(engine_.state());
		return true;
	}

	// ---- The UI thread.

	bool read(TapeSnapshot& out) const { return views_[front_.load(std::memory_order_acquire)].read(out); }
	int trouble() const { return trouble_.load(std::memory_order_relaxed); }

	// The save, made on the worker; waits for it. SAVE_REMOVE: the caller
	// removes tape.wav (TapeStore.hpp 3). With no worker running: nothing,
	// and the file is left as it is (false).
	bool save(const SaveAsk& ask, SaveAction& action) {
		std::lock_guard<std::mutex> one(saveLock_);
		std::unique_lock<std::mutex> lk(lock_);
		if (!worker_.joinable() || stop_)
			return false;
		saveAsk_ = ask;
		saveAsked_ = true;
		saveDone_ = false;
		wake_.notify_all();
		done_.wait(lk, [&] { return saveDone_ || stop_; });
		if (!saveDone_)
			return false;
		action = saveResult_;
		return true;
	}

	// ---- Its own thread.

	void start() {
		std::lock_guard<std::mutex> lk(lock_);
		if (worker_.joinable())
			return;
		stop_ = false;
		worker_ = std::thread(&TapeKeeper::run, this);
	}
	// Joins the worker (a job under way finishes first); a save waiting is let go.
	void stop() {
		std::thread t;
		{
			std::lock_guard<std::mutex> lk(lock_);
			stop_ = true;
			t.swap(worker_);
		}
		wake_.notify_all();
		done_.notify_all();
		if (t.joinable())
			t.join();
	}
	void wake() { wake_.notify_all(); }

	// For the tests: the number tape.wav holds (0: none written); and one
	// round of the worker (2a to 2c) on the caller's thread, only while the
	// worker is not running, so a test can put a step between two rounds.
	uint32_t writtenFor() const { return writtenFor_.load(); }
	void serviceOnce() {
		std::string dir;
		{
			std::lock_guard<std::mutex> lk(lock_);
			if (worker_.joinable())
				return;
			dir = storeDir_;
		}
		service(dir);
	}

private:
	DoldrumsEngine& engine_;

	// The window.
	TapeView views_[2];
	std::atomic<int> front_{0};
	std::atomic<uint32_t> switchedAt_{0};
	int fed_ = 0;                  // the audio thread's (and its, with process() held off)
	uint32_t takenSeen_ = 0;
	long long lastNow_ = 0;
	int countdown_ = 0;

	// The worker's requests.
	std::thread worker_;
	std::mutex lock_, saveLock_;
	std::condition_variable wake_, done_;
	bool stop_ = false;
	std::string storeDir_;
	SaveAsk saveAsk_;
	bool saveAsked_ = false, saveDone_ = false;
	SaveAction saveResult_ = SAVE_NOTHING;
	std::atomic<uint32_t> writtenFor_{0};
	std::atomic<int> trouble_{FINE};

	// The worker's alone.
	uint32_t lastTicket_ = 0;
	Ring* ready_ = nullptr;        // made; not yet offered
	uint32_t readyTicket_ = 0;
	bool readyViewBuilt_ = false;
	const Ring* offered_ = nullptr;   // offered, not yet settled (compared only)
	uint32_t takenBefore_ = 0;

	void say(bool warn, const std::string& s) {
		if (log)
			log(warn, s);
	}
	static double ms(std::chrono::steady_clock::time_point t0) {
		return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	}
	static std::string hz(float v) {
		char b[32];
		std::snprintf(b, sizeof b, "%g Hz", double(v));
		return b;
	}
	bool fileThere(const std::string& path) const {
		if (exists)
			return exists(path);
		if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
			std::fclose(f);
			return true;
		}
		return false;
	}

	void run() {
		std::unique_lock<std::mutex> lk(lock_);
		for (;;) {
			wake_.wait_for(lk, std::chrono::milliseconds(5), [&] { return stop_ || saveAsked_; });
			if (stop_)
				return;
			if (saveAsked_) {
				const SaveAsk ask = saveAsk_;
				saveAsked_ = false;
				lk.unlock();
				collect();
				const SaveAction action = saveNow(ask);
				lk.lock();
				saveResult_ = action;
				saveDone_ = true;
				done_.notify_all();
			}
			const std::string dir = storeDir_;
			lk.unlock();
			service(dir);
			lk.lock();
		}
	}

	void service(const std::string& dir) {
		collect();
		settle();
		make(dir);
		offerReady();
	}

	// 2a. A ring the worker offered that comes back with no take since was refused.
	void collect() {
		while (Ring* r = engine_.retired()) {
			if (r == offered_ && engine_.tapesTaken() == takenBefore_)
				offered_ = nullptr;
			delete r;
		}
	}
	// An offer taken is settled once the audio thread has switched to its view.
	void settle() {
		if (offered_ && switchedAt_.load(std::memory_order_acquire) != takenBefore_)
			offered_ = nullptr;
	}

	// 2b.
	void make(const std::string& dir) {
		if (ready_ || offered_)
			return;
		DoldrumsEngine::TapeJob job;
		if (!engine_.tapeWanted(job) || job.ticket == lastTicket_)
			return;
		lastTicket_ = job.ticket;
		std::string why;
		const std::string path = store::join(dir, STORED_TAPE);
		const bool missing = !job.from && (dir.empty() || !fileThere(path));
		const auto t0 = std::chrono::steady_clock::now();
		Ring* r = makeWanted(job, dir, why);
		if (!r) {
			engine_.giveUp(job.ticket);
			if (job.from)
				say(true, "cannot convert the frozen tape from " + hz(job.fromRate) + " to " + hz(job.toRate) + ": " + why +
				              "; the tape is silent");
			else if (missing)
				say(false, "the patch has no stored tape (" + path + "); the tape starts silent");
			else {
				say(true, "the stored tape " + path + " was not read: " + why +
				              "; the tape starts silent (the file is kept while Freeze stays on)");
				trouble_.store(NOT_READ);
			}
			return;
		}
		char took[48];
		std::snprintf(took, sizeof took, "%.0f ms", ms(t0));
		if (job.from)
			say(false, "converted the frozen tape from " + hz(job.fromRate) + " to " + hz(job.toRate) + " in " + took);
		else
			say(false, std::string("read the stored tape in ") + took + (why.empty() ? "" : ": " + why));
		ready_ = r;
		readyTicket_ = job.ticket;
		readyViewBuilt_ = false;
	}

	// 2c.
	void offerReady() {
		if (!ready_ || offered_)
			return;
		if (!readyViewBuilt_) {
			views_[1 - front_.load(std::memory_order_acquire)].rebuild(*ready_);
			readyViewBuilt_ = true;
		}
		takenBefore_ = engine_.tapesTaken();
		if (engine_.offer(ready_, readyTicket_)) {
			offered_ = ready_;
			ready_ = nullptr;
		}
	}

	// 3.
	SaveAction saveNow(const SaveAsk& ask) {
		const SaveFacts f = {ask.keepTape, ask.button, engine_.frozenSince(), writtenFor_.load(), engine_.tapePending()};
		const SaveAction plan = savePlan(f);
		if (plan == SAVE_WRITE && makeDir)
			makeDir(ask.dir);
		uint32_t written = writtenFor_.load();
		std::string why;
		const auto t0 = std::chrono::steady_clock::now();
		const SaveAction action = saveStep(f, ask.dir, engine_, ask.rate, written, why);
		writtenFor_.store(written);
		char took[48];
		std::snprintf(took, sizeof took, "%.0f ms", ms(t0));
		if (action == SAVE_WRITE) {
			say(false, std::string("stored the frozen tape in ") + took);
			trouble_.store(FINE);
		}
		else if (plan == SAVE_WRITE && action == SAVE_REMOVE) {
			say(true, "the frozen tape was not stored in " + ask.dir + ": " + why);
			trouble_.store(NOT_SAVED);
		}
		else if (plan == SAVE_WRITE)
			say(false, "the frozen tape was not stored (" + why + "); the stored file is left as it is");
		else if (action == SAVE_REMOVE)
			trouble_.store(FINE);   // nothing is kept, by the button or the menu
		return action;
	}
};

}} // namespace rka::doldrums

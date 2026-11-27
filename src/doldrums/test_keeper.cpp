// The module's side of the tape hand-over (TapeKeeper.hpp), built with
// ThreadSanitizer (tests.mk; it cannot be combined with ASan): any data race
// fails the run, and a deadlock trips the watchdog.
//
// The freeze's own write (TapeStore.hpp, 7), with the worker's rounds run a
// step at a time on this thread (serviceOnce), this thread also the audio
// thread:
// - frozen_tape_is_written_without_a_save: frozen by the button with sound
//   on the tape, and no save: within two rounds tape.wav is the frozen tape
//   to the bit; it is not written twice for one freeze (one attempt, the
//   same bytes); a thaw leaves the file; frozen again, written again with
//   the new tape.
// - gate_only_freeze_writes_nothing: frozen by the gate alone, nothing is
//   tried; the button pressed while the gate holds it, the tape is the
//   button's and is written (as a save would write it).
// - keep_off_writes_nothing, keep_turned_on_while_frozen_writes.
// - write_waits_for_the_tape_to_stand_still: in the freeze's fade nothing is
//   tried (not even the copy made ready); after it, the file is the tape as
//   the fade left it.
// - save_after_freeze_write_does_not_rewrite: the save (the backstop) sees
//   the freeze written and leaves the file as it is.
// - a_failed_freeze_write_is_tried_once: a folder that cannot be written: a
//   warning and TAPE NOT SAVED, once for the freeze, the number not taken.
//
// race(), with real threads: Rack is modelled by a shared mutex, as Rack's
// engine lock is: the audio thread holds it shared for each block of
// process() + afterProcess() (feeding the keeper the button, as process()
// does); two save threads hold it shared round keeper.save() (Rack's
// prepareSave), as onSave does; an events thread holds it exclusively, as
// Rack does round onAdd (expectStored), onSampleRateChange (allocate,
// setSampleRate, configured, tapeRate) and onReset (reset, cleared); a
// window thread reads the snapshots; the menu's Keep is off now and then.
// The Freeze button is on most of the time, off for a few ms now and then,
// so saves and the freeze's own writes meet thaws, freezes, stored tapes
// arriving and conversions at rate changes. Whichever way they meet: no
// race, no deadlock; saves are made; the freeze's own writes are made (and
// the saves write the few freezes they find unwritten); stored tapes are
// taken; afterwards every tape.wav on disk is a whole tape stamped with the
// rate of its length, and the window's fed view is the live tape's.
#define RKA_WAV_IMPLEMENTATION
#include "doldrums/TapeKeeper.hpp"
#include "testing.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" const char* __tsan_default_options() {
	return "halt_on_error=1";
}

namespace {

using namespace rka::doldrums;
typedef DoldrumsFrame F;
namespace fs = std::filesystem;

fs::path tempRoot() {
	static const fs::path root = fs::temp_directory_path() / ("rka_doldrums_keeper_" + std::to_string((long long) std::rand()) + "_" +
	                                                         std::to_string((long long) std::time(nullptr)));
	return root;
}
fs::path sub(const std::string& name) {
	const fs::path d = tempRoot() / name;
	fs::create_directories(d);
	return d;
}
std::string readAll(const fs::path& p) {
	std::ifstream in(p, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// A module as Doldrums.cpp makes it, with this thread as its audio thread
// and the worker's rounds run here (serviceOnce): the engine at 8 kHz, the
// keeper told the storage folder, the rate and Keep, the button fed to it
// each sample.
struct Rig {
	fs::path dir;
	DoldrumsEngine e;
	TapeKeeper k{e};
	F f;
	DoldrumsResult out;
	long long t = 0;
	std::mutex logLock;
	std::vector<std::string> infos, warns;
	explicit Rig(const fs::path& d, float rate = 8000.f) : dir(d) {
		k.log = [this](bool warn, const std::string& s) {
			std::lock_guard<std::mutex> lk(logLock);
			(warn ? warns : infos).push_back(s);
		};
		k.makeDir = [](const std::string& x) {
			std::error_code ec;
			fs::create_directories(x, ec);
		};
		e.seed(5);
		e.allocate(rate);
		e.setSampleRate(rate);
		k.configured();
		k.tapeRate(rate);
		k.storeIn(dir.string());
		f.params[F::POSITION] = 1.f;
		f.inRPatched = true;
	}
	~Rig() { k.stop(); }
	void run(long long n) {
		for (long long i = 0; i < n; i++, t++) {
			f.inL = float((t * 7) % 1999) * 0.003f - 3.f;
			f.inR = float((t * 5) % 1499) * -0.004f + 3.f;
			k.buttonIs(f.params[F::FREEZE] >= 0.5f);
			e.process(f, out);
			k.afterProcess(133);
		}
	}
	void rounds(int n) {
		for (int i = 0; i < n; i++) k.serviceOnce();
	}
	fs::path file() const { return dir / STORED_TAPE; }
	int said(const std::string& part, bool warn) {
		std::lock_guard<std::mutex> lk(logLock);
		int n = 0;
		for (const std::string& s : warn ? warns : infos) n += s.find(part) != std::string::npos;
		return n;
	}
	// tape.wav read back (WavFile's reader, not TapeStore's) against the live
	// tape oldest first (Ring::copy, not TapeStore's image), bit for bit.
	bool fileIsTheTape() {
		const Ring& g = e.ring();
		std::vector<float> l(size_t(g.capacity())), r(l.size()), fl(l.size()), fr(l.size());
		if (!g.copy(g.now(), int(g.capacity()), l.data(), r.data())) return false;
		rka::WavInfo info;
		std::string why;
		if (!rka::readWav(file().string(), fl.data(), fr.data(), fl.size(), info, why) || info.frames != fl.size()) return false;
		return std::memcmp(l.data(), fl.data(), l.size() * 4) == 0 && std::memcmp(r.data(), fr.data(), r.size() * 4) == 0;
	}
};

const char* kFreezeWrote = "(at the freeze)";

void frozen_tape_is_written_without_a_save() {
	Rig m(sub("written"));
	m.run(tapeFrames(8000.f) + 500);
	m.f.params[F::FREEZE] = 1.f;
	m.run(200);
	int rounds = 0;
	while (!fs::exists(m.file()) && rounds < 10) {
		m.k.serviceOnce();
		rounds++;
	}
	CHECK(fs::exists(m.file()) && rounds <= 2, "frozen by the button, no save: tape.wav written within two rounds (%d)", rounds);
	CHECK(m.fileIsTheTape(), "it reads back as the frozen tape, to the bit");
	CHECK(m.k.writtenFor() == m.e.frozenSince() && m.k.freezeAttempts() == 1 && m.said(kFreezeWrote, false) == 1,
	      "stamped with the freeze's number, one attempt, one line (%u %u, %u)", m.k.writtenFor(), m.e.frozenSince(), m.k.freezeAttempts());
	const std::string first = readAll(m.file());
	m.run(1000);
	m.rounds(10);
	CHECK(m.k.freezeAttempts() == 1 && m.said(kFreezeWrote, false) == 1 && readAll(m.file()) == first, "not written twice for one freeze");
	m.f.params[F::FREEZE] = 0.f;
	m.run(4000);
	m.rounds(3);
	CHECK(readAll(m.file()) == first && m.k.freezeAttempts() == 1, "thawed: the file stays as it is (its removal is the save's)");
	m.f.params[F::FREEZE] = 1.f;
	m.run(200);
	m.rounds(2);
	CHECK(m.k.freezeAttempts() == 2 && m.fileIsTheTape() && readAll(m.file()) != first && m.k.writtenFor() == m.e.frozenSince(),
	      "frozen again: written again, the new tape (%u attempts)", m.k.freezeAttempts());
	CHECK(m.warns.empty() && m.k.trouble() == TapeKeeper::FINE, "no warning, no trouble");
}

void gate_only_freeze_writes_nothing() {
	Rig m(sub("gate"));
	m.run(20000);
	m.f.freezeGate = 10.f;
	m.run(400);
	m.rounds(5);
	CHECK(m.e.state().frozen && !fs::exists(m.file()) && m.k.freezeAttempts() == 0, "frozen by the gate alone: nothing tried, no file");
	m.f.params[F::FREEZE] = 1.f;   // the button pressed while the gate holds it
	m.run(10);
	m.rounds(2);
	CHECK(fs::exists(m.file()) && m.fileIsTheTape() && m.k.freezeAttempts() == 1, "the button on: the tape is the button's, written");
}

void keep_off_writes_nothing() {
	Rig m(sub("keep_off"));
	m.k.keepTape(false);
	m.run(20000);
	m.f.params[F::FREEZE] = 1.f;
	m.run(400);
	m.rounds(5);
	CHECK(!fs::exists(m.file()) && m.k.freezeAttempts() == 0, "Keep off: nothing tried, no file");
}

void keep_turned_on_while_frozen_writes() {
	Rig m(sub("keep_on"));
	m.k.keepTape(false);
	m.run(20000);
	m.f.params[F::FREEZE] = 1.f;
	m.run(400);
	m.rounds(3);
	CHECK(!fs::exists(m.file()), "Keep off: no file");
	m.k.keepTape(true);
	m.rounds(2);
	CHECK(fs::exists(m.file()) && m.fileIsTheTape() && m.k.writtenFor() == m.e.frozenSince(), "Keep turned on while frozen: written");
}

void write_waits_for_the_tape_to_stand_still() {
	Rig m(sub("fade"));
	m.run(tapeFrames(8000.f) + 300);
	m.f.params[F::FREEZE] = 1.f;
	m.run(10);   // the fade is 5 ms, 40 frames at 8 kHz
	m.rounds(5);
	CHECK(!fs::exists(m.file()) && m.k.freezeAttempts() == 0, "in the freeze's fade: nothing tried, no file (%u)", m.k.freezeAttempts());
	m.run(100);
	m.rounds(2);
	CHECK(fs::exists(m.file()) && m.fileIsTheTape(), "after the fade: the file is the tape as the fade left it");
}

void save_after_freeze_write_does_not_rewrite() {
	Rig m(sub("backstop"));
	m.run(20000);
	m.f.params[F::FREEZE] = 1.f;
	m.run(200);
	m.rounds(2);
	CHECK(fs::exists(m.file()), "the freeze wrote it");
	const std::string bytes = readAll(m.file());
	m.k.start();
	TapeKeeper::SaveAsk ask;
	ask.dir = m.dir.string();
	ask.button = true;
	ask.rate = 8000.f;
	SaveAction act = SAVE_WRITE;
	CHECK(m.k.save(ask, act) && act == SAVE_NOTHING, "the save: nothing to do (%d)", int(act));
	CHECK(readAll(m.file()) == bytes && m.said("stored the frozen tape", false) == 1, "the file as the freeze wrote it, not rewritten");
	m.k.stop();
}

void a_failed_freeze_write_is_tried_once() {
	const fs::path base = sub("blocked");
	const fs::path dir = base / "folder";
	{
		std::ofstream o(dir);
		o << "a file where the folder should be";
	}
	Rig m(dir);
	m.run(20000);
	m.f.params[F::FREEZE] = 1.f;
	m.run(200);
	m.rounds(10);
	CHECK(m.k.freezeAttempts() == 1 && m.said("was not stored", true) == 1 && m.k.trouble() == TapeKeeper::NOT_SAVED,
	      "a warning and TAPE NOT SAVED, once for the freeze (%u attempts)", m.k.freezeAttempts());
	CHECK(m.k.writtenFor() != m.e.frozenSince(), "the freeze's number not taken: the save tries again");
}

void race() {
	const fs::path dir = sub("race");
	DoldrumsEngine e;
	TapeKeeper k(e);
	std::atomic<int> warnings{0}, infos{0};
	std::atomic<long long> freezeWrites{0}, removed{0};
	k.log = [&](bool warn, const std::string& s) {
		(warn ? warnings : infos)++;
		if (!warn && s.find(kFreezeWrote) != std::string::npos) freezeWrites++;
	};
	k.makeDir = [](const std::string& d) {
		std::error_code ec;
		fs::create_directories(d, ec);
	};
	k.remove = [&](const std::string& path) {
		std::error_code ec;
		fs::remove(path, ec);
		removed++;
	};
	std::shared_mutex rack;
	std::atomic<float> rate{8000.f};
	e.seed(3);
	e.allocate(rate.load());
	e.setSampleRate(rate.load());
	k.configured();
	k.tapeRate(rate.load());
	k.storeIn(dir.string());
	k.start();

	std::atomic<bool> stop{false}, done{false};
	std::atomic<float> button{1.f};
	std::atomic<long long> samples{0}, saves{0}, written{0}, events{0}, reads{0};

	std::thread watchdog([&]() {
		const auto t0 = std::chrono::steady_clock::now();
		while (!done.load()) {
			if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(90)) {
				std::fprintf(stderr, "test_keeper: deadlock (90 s)\n");
				std::abort();
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	});

	std::thread audio([&]() {
		F f;
		DoldrumsResult out;
		long long t = 0;
		while (!stop.load()) {
			std::shared_lock<std::shared_mutex> lk(rack);
			for (int i = 0; i < 64; i++, t++) {
				f.params[F::FREEZE] = button.load(std::memory_order_relaxed);
				k.buttonIs(f.params[F::FREEZE] >= 0.5f);
				f.inL = float(t % 97) * 0.05f - 2.f;
				f.inR = float(t % 41) * -0.1f + 2.f;
				e.process(f, out);
				k.afterProcess(133);
			}
			samples += 64;
		}
	});
	std::thread buttonThread([&]() {
		const auto t0 = std::chrono::steady_clock::now();
		while (!stop.load()) {
			const long long ms = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
			button.store(ms % 170 < 160 ? 1.f : 0.f);
			k.keepTape(ms % 1300 < 1200);   // the menu's Keep, off now and then
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	});
	auto saver = [&]() {
		while (!stop.load()) {
			{
				std::shared_lock<std::shared_mutex> lk(rack);
				TapeKeeper::SaveAsk ask;
				ask.dir = dir.string();
				ask.button = button.load() >= 0.5f;
				ask.rate = rate.load();
				SaveAction act = SAVE_NOTHING;
				if (k.save(ask, act)) {   // the keeper removes the file itself (on the worker)
					saves++;
					if (act == SAVE_WRITE) written++;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(15));
		}
	};
	std::thread saverA(saver), saverB(saver);
	std::thread eventsThread([&]() {
		int n = 0;
		while (!stop.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(120));
			std::unique_lock<std::shared_mutex> lk(rack);
			switch (n++ % 4) {
			case 0:
			case 2:
				k.expectStored(dir.string());   // onAdd: a patch opened frozen
				break;
			case 1: {   // onSampleRateChange
				const float r = rate.load() == 8000.f ? 11025.f : 8000.f;
				rate.store(r);
				e.allocate(r);
				e.setSampleRate(r);
				k.configured();
				k.tapeRate(r);
				k.wake();
				break;
			}
			case 3:   // onReset (the knobs stay: this test's button is its own)
				e.reset();
				k.cleared();
				break;
			}
			events++;
		}
	});
	std::thread window([&]() {
		while (!stop.load()) {
			TapeSnapshot s;
			if (k.read(s)) reads++;
		}
	});

	std::this_thread::sleep_for(std::chrono::seconds(4));
	stop = true;
	audio.join();
	buttonThread.join();
	saverA.join();
	saverB.join();
	eventsThread.join();
	window.join();

	// Settle on this thread, frozen: the worker finishes what it began, the
	// audio side takes or refuses it and switches.
	F f;
	f.params[F::FREEZE] = 1.f;
	DoldrumsResult out;
	for (int round = 0; round < 400 && (e.tapePending() || round < 20); round++) {
		for (int i = 0; i < 64; i++) {
			e.process(f, out);
			k.afterProcess(133);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	k.stop();
	done = true;
	watchdog.join();

	// The freeze's own write now writes most freezes within a round, so a save
	// that writes is the rarer case (a few a run); together they must write.
	CHECK(saves.load() > 20 && written.load() + freezeWrites.load() > 0, "saves made (%lld), some written (%lld by saves)", saves.load(),
	      written.load());
	CHECK(freezeWrites.load() > 0, "the freeze's own writes made (%lld)", freezeWrites.load());
	CHECK(e.tapesTaken() > 0, "stored tapes taken (%u)", e.tapesTaken());
	if (fs::exists(dir / STORED_TAPE)) {
		Ring r;
		std::string why;
		CHECK(readTape(dir.string(), rate.load(), r, why), "the file on disk is a whole tape (%s)", why.c_str());
		rka::WavInfo info;
		CHECK(rka::probeWav((dir / STORED_TAPE).string(), info, why) && (long long) info.frames == tapeFrames(float(info.sampleRate)),
		      "stamped with the rate of its length (%llu frames at %u Hz)", (unsigned long long) info.frames, info.sampleRate);
	}
	TapeView ref;
	ref.rebuild(e.ring());
	ref.publish(e.state());
	// The fed view, published now (this thread is the audio thread here).
	for (int i = 0; i < 200; i++) {
		e.process(f, out);
		k.afterProcess(1);
	}
	ref.publish(e.state());
	TapeSnapshot a, b;
	CHECK(k.read(a) && ref.read(b), "both views read");
	int cols = 0;
	for (int i = 0; i < TapeSnapshot::COLS; i++) cols += a.low[i] != b.low[i] || a.high[i] != b.high[i];
	CHECK(cols == 0, "the window's view is the live tape's (%d columns differ)", cols);
	std::printf("race: %lld samples, %lld saves (%lld written, %lld removed), %lld written at the freeze, %lld events, %u tapes taken, "
	            "%lld reads, %d warnings\n",
	            samples.load(), saves.load(), written.load(), removed.load(), freezeWrites.load(), events.load(), e.tapesTaken(), reads.load(),
	            warnings.load());
}

} // namespace

int main() {
	std::srand((unsigned) std::time(nullptr));
	frozen_tape_is_written_without_a_save();
	gate_only_freeze_writes_nothing();
	keep_off_writes_nothing();
	keep_turned_on_while_frozen_writes();
	write_waits_for_the_tape_to_stand_still();
	save_after_freeze_write_does_not_rewrite();
	a_failed_freeze_write_is_tried_once();
	race();
	std::error_code ec;
	fs::remove_all(tempRoot(), ec);
	return rkatest::finish("doldrums/test_keeper");
}

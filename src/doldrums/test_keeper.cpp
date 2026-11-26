// The module's side of the tape hand-over (TapeKeeper.hpp) with real threads,
// built with ThreadSanitizer (tests.mk; it cannot be combined with ASan): any
// data race fails the run, and a deadlock trips the watchdog.
//
// Rack is modelled by a shared mutex, as Rack's engine lock is: the audio
// thread holds it shared for each block of process() + afterProcess(); two
// save threads hold it shared round keeper.save() (Rack's prepareSave), as
// onSave does, and remove tape.wav when told to; an events thread holds it
// exclusively, as Rack does round onAdd (expectStored), onSampleRateChange
// (allocate, setSampleRate, configured) and onReset (reset, cleared); a
// window thread reads the snapshots. The Freeze button is on most of the
// time, off for a few ms now and then, so saves meet thaws, freezes,
// stored tapes arriving and conversions at rate changes.
//
// race(): whichever way they meet, no race, no deadlock; saves are made and
// some write; stored tapes are taken; afterwards every tape.wav on disk is a
// whole tape (it reads), and the window's fed view is the live tape's.
#define RKA_WAV_IMPLEMENTATION
#include "doldrums/TapeKeeper.hpp"
#include "testing.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <shared_mutex>
#include <thread>

extern "C" const char* __tsan_default_options() {
	return "halt_on_error=1";
}

namespace {

using namespace rka::doldrums;
typedef DoldrumsFrame F;
namespace fs = std::filesystem;

void race() {
	const fs::path dir = fs::temp_directory_path() /
	                     ("rka_doldrums_keeper_" + std::to_string((long long) std::rand()) + "_" + std::to_string((long long) std::time(nullptr)));
	fs::create_directories(dir);
	DoldrumsEngine e;
	TapeKeeper k(e);
	std::atomic<int> warnings{0}, infos{0};
	k.log = [&](bool warn, const std::string&) { (warn ? warnings : infos)++; };
	k.makeDir = [](const std::string& d) {
		std::error_code ec;
		fs::create_directories(d, ec);
	};
	std::shared_mutex rack;
	std::atomic<float> rate{8000.f};
	e.seed(3);
	e.allocate(rate.load());
	e.setSampleRate(rate.load());
	k.configured();
	k.start();

	std::atomic<bool> stop{false}, done{false};
	std::atomic<float> button{1.f};
	std::atomic<long long> samples{0}, saves{0}, written{0}, removed{0}, events{0}, reads{0};

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
				if (k.save(ask, act)) {
					saves++;
					if (act == SAVE_WRITE) written++;
					if (act == SAVE_REMOVE) {
						std::error_code ec;
						fs::remove(dir / STORED_TAPE, ec);
						removed++;
					}
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

	CHECK(saves.load() > 20 && written.load() > 0, "saves made (%lld), some written (%lld)", saves.load(), written.load());
	CHECK(e.tapesTaken() > 0, "stored tapes taken (%u)", e.tapesTaken());
	if (fs::exists(dir / STORED_TAPE)) {
		Ring r;
		std::string why;
		CHECK(readTape(dir.string(), rate.load(), r, why), "the file on disk is a whole tape (%s)", why.c_str());
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
	std::printf("race: %lld samples, %lld saves (%lld written, %lld removed), %lld events, %u tapes taken, %lld reads, %d warnings\n",
	            samples.load(), saves.load(), written.load(), removed.load(), events.load(), e.tapesTaken(), reads.load(), warnings.load());
	std::error_code ec;
	fs::remove_all(dir, ec);
}

} // namespace

int main() {
	std::srand((unsigned) std::time(nullptr));
	race();
	return rkatest::finish("doldrums/test_keeper");
}

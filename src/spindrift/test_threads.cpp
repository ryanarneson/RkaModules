// Spindrift's window with real threads, built with ThreadSanitizer (tests.mk;
// it cannot be combined with ASan). The test is the report: any data race on
// the view or the overview fails the run.
//
// threads(): the engine's thread runs process() as fast as it can, through grains,
// delay mode, a freeze, a quality chosen while frozen, mono and the synth,
// while a second thread, the window, copies the view and reads the
// overview's columns, as GrainWindow does each frame, for 2 s. Every copy
// must be one the window can draw (counts within the arrays, every fraction
// in 0..1, the enums known), and the window must have seen what the engine
// went through: grains sounding, delay mode, the synth, a pending quality, the
// head moving, the waveform drawn. A view that stopped publishing, or
// published one thing over another, fails one of those.
//
// menu(): the menu's settings (SpindriftEngine::Settings) written by the UI
// thread while the engine reads them each block and the Quality long press
// writes settings.gain from the engine's thread, and the save's ringSeq()
// read meanwhile. Under ThreadSanitizer a plain field would be a reported
// race; the menu's last choice must be what the engine then does.
#include "testing.hpp"
#include "SpindriftEngine.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <set>
#include <thread>

// The first report ends the run with a failure.
extern "C" const char* __tsan_default_options() {
	return "halt_on_error=1";
}

namespace {

using spindrift::GrainView;
using spindrift::SpindriftEngine;

// The phases' ends, in samples at 48 kHz: grains, delay mode (2 s: Beads
// takes 1.5 s to cross into it), frozen with a quality chosen, mono and
// unfrozen (the quality applies), the synth, and out of it. Each but the
// delay is 0.4 s, past a reconfiguration's 250 ms mute.
const long kEnds[6] = {19200, 115200, 134400, 153600, 172800, 192000};
const long kCycle = kEnds[5];

int phaseOf(long n) {
	const long t = n % kCycle;
	int p = 0;
	while (t >= kEnds[p])
		p++;
	return p;
}

bool phaseStarts(long n) {
	const long t = n % kCycle;
	if (t == 0)
		return true;
	for (int p = 0; p < 5; p++)
		if (t == kEnds[p])
			return true;
	return false;
}

bool unit(float v) {
	return v >= 0.f && v <= 1.f;
}

struct Seen {
	long copies = 0, bad = 0, grainsSounding = 0, delay = 0, synth = 0, pending = 0, frozen = 0, waveform = 0;
	std::set<float> heads;
};

void check(const GrainView::Copy& c, Seen& s) {
	bool ok = c.n >= 0 && c.n <= GrainView::MAX && c.nt >= 0 && c.nt <= GrainView::MAX;
	ok &= c.mode == spindrift::MODE_GRAIN || c.mode == spindrift::MODE_DELAY || c.mode == spindrift::MODE_SYNTH;
	ok &= c.quality >= spindrift::DIRTY_TAPE && c.quality <= spindrift::DIGITAL;
	ok &= c.pending >= spindrift::DIRTY_TAPE && c.pending <= spindrift::DIGITAL;
	ok &= unit(c.head) && unit(c.wave) && c.seconds >= 0.f && c.seconds < 100.f && c.bank >= 0 && c.bank < 12;
	for (int i = 0; ok && i < c.n; i++)
		ok &= unit(c.grains[i].position) && unit(c.grains[i].span) && unit(c.grains[i].pan) && unit(c.grains[i].env);
	for (int i = 0; ok && i < c.nt; i++)
		ok &= unit(c.taps[i].position) && c.taps[i].gain >= 0.f && std::isfinite(c.taps[i].gain);
	if (!ok) {
		if (!s.bad)
			std::printf("first bad copy: n %d nt %d mode %d head %g\n", c.n, c.nt, int(c.mode), c.head);
		s.bad++;
		return;
	}
	s.copies++;
	s.grainsSounding += c.mode == spindrift::MODE_GRAIN && c.n > 0;
	s.delay += c.mode == spindrift::MODE_DELAY;
	s.synth += c.mode == spindrift::MODE_SYNTH;
	s.pending += c.pendingQuality && c.pending != c.quality;
	s.frozen += c.frozen;
	if (c.mode != spindrift::MODE_SYNTH && s.heads.size() < 1000)
		s.heads.insert(c.head);
}

void threads() {
	SpindriftEngine e(0x5eed);
	CHECK(e.allocate(), "allocated");
	e.setSampleRate(48000.f);

	std::atomic<bool> stop{false};
	std::atomic<long> samples{0};

	// The engine's thread: the phases (kEnds), round and round.
	std::thread engine([&]() {
		spindrift::Knobs k = {};
		k.time = k.size = k.shape = k.pitch = 0.5f;
		k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
		k.density = 0.9f;
		k.dryWet = 1.f;
		spindrift::Jacks j = {};
		j.inLOn = j.inROn = true;
		long n = 0;
		while (!stop.load(std::memory_order_relaxed)) {
			const int p = phaseOf(n);
			k.size = p == 1 ? 1.f : 0.5f;                       // 1: delay mode
			j.inROn = p != 3;                                   // 3: mono
			e.settings.synth = p == 4 ? spindrift::SYNTH_ALWAYS : spindrift::SYNTH_AUTO;  // 4: the synth
			if (phaseStarts(n) && p == 2) {                     // 2: frozen, a quality chosen
				e.buttons().freezePress();
				e.buttons().qualityDown();
				e.buttons().qualityUp(10);
			}
			if (phaseStarts(n) && p == 3)
				e.buttons().freezePress();                      // released: it applies
			const float t = float(n) / 48000.f;
			j.inL = 5.f * std::sin(2.f * 3.14159265f * 220.f * t);
			j.inR = 5.f * std::sin(2.f * 3.14159265f * 331.f * t);
			spindrift::Out o;
			e.process(k, j, o);
			n++;
			if ((n & 1023) == 0)
				samples.store(n, std::memory_order_relaxed);
		}
		samples.store(n, std::memory_order_relaxed);
	});

	// The window's thread: copy and read, as each frame does, for 2 s, and
	// on until the engine has been through every phase once (it may run
	// slower than real time under the sanitizer), but not past a minute.
	Seen seen;
	std::thread window([&]() {
		const auto start = std::chrono::steady_clock::now();
		const auto until = start + std::chrono::seconds(2), cap = start + std::chrono::seconds(60);
		GrainView::Copy c;
		for (;;) {
			const auto now = std::chrono::steady_clock::now();
			if (now >= cap || (now >= until && samples.load(std::memory_order_relaxed) > kCycle))
				break;
			e.view().copy(c);
			check(c, seen);
			const rka::Overview& o = e.overview();
			bool drawn = false;
			for (int col = 0; col < rka::Overview::COLS; col++) {
				const float lo = o.lo[col].load(std::memory_order_relaxed), hi = o.hi[col].load(std::memory_order_relaxed);
				drawn |= hi - lo > 0.1f;
			}
			seen.waveform += drawn;
		}
	});
	window.join();
	stop.store(true);
	engine.join();

	const long ran = samples.load();
	std::printf("engine ran %ld samples (%.1f s of audio); window made %ld copies\n", ran, ran / 48000.0, seen.copies);
	CHECK(seen.bad == 0, "every copy drawable (%ld bad)", seen.bad);
	CHECK(seen.copies > 1000, "the window copied (%ld)", seen.copies);
	CHECK(ran > kCycle, "the engine ran every phase (%ld samples)", ran);
	CHECK(seen.grainsSounding > 0, "saw grains sounding");
	CHECK(seen.delay > 0, "saw delay mode");
	CHECK(seen.synth > 0, "saw the synth");
	CHECK(seen.frozen > 0, "saw it frozen");
	CHECK(seen.pending > 0, "saw a pending quality");
	CHECK(seen.heads.size() > 20, "saw the head move (%zu places)", seen.heads.size());
	CHECK(seen.waveform > 0, "saw the waveform");
}

}  // namespace

void menu() {
	SpindriftEngine e(0x5eed);
	CHECK(e.allocate(), "allocated");
	e.setSampleRate(48000.f);
	std::atomic<bool> stop{false}, menuDone{false};
	std::atomic<long> samples{0}, presses{0};
	std::atomic<int> gateHigh{0}, gateOther{0};

	std::thread engine([&]() {
		spindrift::Knobs k = {};
		k.time = k.size = k.shape = k.pitch = 0.5f;
		k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
		k.density = 0.9f;
		k.dryWet = 1.f;
		spindrift::Jacks j = {};
		j.inLOn = j.inROn = true;
		long n = 0, after = 0;
		while (!stop.load(std::memory_order_relaxed)) {
			// The Quality long press, now and then: it writes settings.gain.
			if (n % 9600 == 0 && !menuDone.load(std::memory_order_relaxed)) {
				e.buttons().qualityDown();
				e.buttons().qualityUp(spindrift::Buttons::kQualityLongTicks);
				presses.fetch_add(1, std::memory_order_relaxed);
			}
			const float t = float(n) / 48000.f;
			j.inL = 5.f * std::sin(2.f * 3.14159265f * 220.f * t);
			j.inR = 5.f * std::sin(2.f * 3.14159265f * 331.f * t);
			spindrift::Out o;
			e.process(k, j, o);
			n++;
			// Once the menu has finished (gate output on): the right output is
			// a 0/10 V gate, after a few blocks for the change to land.
			if (menuDone.load(std::memory_order_acquire) && ++after > 4800) {
				if (o.r == 10.f)
					gateHigh.fetch_add(1, std::memory_order_relaxed);
				else if (o.r != 0.f)
					gateOther.fetch_add(1, std::memory_order_relaxed);
				if (after > 48000)
					break;
			}
		}
		samples.store(n, std::memory_order_relaxed);
	});

	// The menu: every setting, round and round, for 1 s; then gate output on.
	std::thread ui([&]() {
		const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
		long i = 0;
		uint32_t seqs = 0;
		while (std::chrono::steady_clock::now() < until) {
			e.settings.gain = spindrift::GainMode(i % 3);
			e.settings.manualGain = 1.f + float(i % 40);
			e.settings.gateOutput = (i & 1) != 0;
			e.settings.keepFrozen = (i & 2) != 0;
			e.settings.synth = (i & 4) ? spindrift::SYNTH_NEVER : spindrift::SYNTH_AUTO;
			seqs += e.ringSeq() & 1u;
			(void) e.settings.gain.load();
			i++;
		}
		e.settings.synth = spindrift::SYNTH_NEVER;
		e.settings.gain = spindrift::GAIN_FIXED;
		e.settings.gateOutput = true;
		menuDone.store(true, std::memory_order_release);
		std::printf("menu: %ld writes (%u odd ring sequences read)\n", i, seqs);
	});
	ui.join();
	engine.join();
	stop.store(true);
	std::printf("menu: engine ran %ld samples, %ld long presses\n", samples.load(), presses.load());
	CHECK(presses.load() > 0, "the long press wrote settings.gain meanwhile");
	CHECK(gateHigh.load() > 0 && gateOther.load() == 0, "the menu's last choice holds: R is a 0/10 V gate (%d high, %d other)",
	      gateHigh.load(), gateOther.load());
}

int main() {
	threads();
	menu();
	return rkatest::finish("spindrift test_threads");
}

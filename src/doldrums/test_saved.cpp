// Doldrums' saved state (TESTING.md rule 5): the golden patch data and the
// golden tape.wav (testdata/GoldenTapeV1.hpp), loaded as the module loads
// them (SavedState.hpp; TapeKeeper.hpp's worker and window, with this thread
// as the audio thread), and the cases a patch can be wrong in. Built with
// the sanitizers; files are written only in a temporary folder made here.
//
// - param_ids: the param ids the module saves (its ParamId, held to
//   DoldrumsFrame::Param by a static_assert in Doldrums.cpp) and their
//   defaults, pinned to the plan's list.
// - golden_patch_data: the golden data loads with every setting away from
//   its default; the keys written are exactly the four; each field missing,
//   of the wrong kind or out of range takes its default alone.
// - golden_tape_v1: the golden file, made from its header and formulas,
//   reads to the bit; a patch opened frozen with it in its folder gets it
//   live through the keeper (openFrozen, as onAdd), its window shows it,
//   and neither the freeze's own write nor the next save writes it again
//   (it already holds this freeze).
// - writer_is_golden: what the save writes today is the golden format, byte
//   for byte.
// - opened_frozen_without_a_file_opens_thawed: a patch saved frozen without
//   tape.wav (quit with no save, before this build) asks for no read: the
//   Freeze button is let go, the tape records the live input and the output
//   is not silence; a line of information, no warning, nothing in the
//   window's corner.
// - opened_frozen_with_keep_off_opens_thawed: likewise with Keep off.
// - opened_frozen_with_a_bad_file_opens_thawed: a cut file, refused by its
//   header at open: a warning and TAPE NOT READ, the button let go, the
//   tape records; the next save removes the file and the trouble goes.
// - a_read_that_fails_late_lets_go: a file that looked like a tape at open
//   but cannot be read on the worker: the engine waits frozen only until the
//   read fails, then the UI is told, once, to let go; let go, the tape
//   records and the output is not silence.
// - a_press_during_the_read_keeps_the_button: the user pressed Freeze off and
//   on again while the read was under way (the gate holding the tape frozen
//   meanwhile): the failed read lets nothing go.
// - save_round_trip: a tape recorded, frozen by the button, saved through
//   the keeper and opened in a new module: the same tape to the bit.
// - save_not_written_is_shown: a save that cannot write is a warning and
//   TAPE NOT SAVED.
// - offer_settles_after_the_switch: the worker makes nothing new between a
//   take and the audio side's switch to its view.
#define RKA_WAV_IMPLEMENTATION
#include "doldrums/SavedState.hpp"
#include "doldrums/TapeKeeper.hpp"
#include "doldrums/testdata/GoldenTapeV1.hpp"
#include "testing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" const char* __asan_default_options() {
	return "detect_stack_use_after_return=0";
}

namespace fs = std::filesystem;

namespace {

using namespace rka::doldrums;
typedef DoldrumsFrame F;

struct TempDir {
	fs::path dir;
	TempDir() {
		dir = fs::temp_directory_path() /
		      ("rka_doldrums_saved_" + std::to_string((long long) std::rand()) + "_" + std::to_string((long long) std::time(nullptr)));
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

std::string readAll(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The golden file's bytes: its header, then the formulas (little-endian floats).
std::string goldenBytes() {
	std::string b(reinterpret_cast<const char*>(golden::kTapeHeaderV1), sizeof golden::kTapeHeaderV1);
	b.reserve(b.size() + size_t(golden::kTapeFrames) * 8);
	for (uint32_t i = 0; i < golden::kTapeFrames; i++) {
		const float v[2] = {golden::goldenLeft(i), golden::goldenRight(i)};
		for (float x : v) {
			uint32_t u;
			std::memcpy(&u, &x, 4);
			for (int k = 0; k < 4; k++) b += char((u >> (8 * k)) & 0xff);
		}
	}
	return b;
}

void writeBytes(const std::string& path, const std::string& bytes) {
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), std::streamsize(bytes.size()));
}

// A ring's frames oldest first equal the golden formulas, to the bit.
long long goldenMismatches(const Ring& r) {
	if (r.capacity() != (long long) golden::kTapeFrames) return -1;
	long long bad = 0;
	for (long long i = 0; i < r.capacity(); i++) {
		const long long p = r.physical(r.now() - r.capacity() + i);
		const float l = r.channel(0)[p], rr = r.channel(1)[p];
		const float gl = golden::goldenLeft(uint32_t(i)), gr = golden::goldenRight(uint32_t(i));
		bad += std::memcmp(&l, &gl, 4) != 0 || std::memcmp(&rr, &gr, 4) != 0;
	}
	return bad;
}

// A module as Doldrums.cpp makes it: the engine at a rate, the keeper with
// its log captured, and this thread as the audio thread.
struct Module {
	std::unique_ptr<DoldrumsEngine> e{new DoldrumsEngine};
	std::unique_ptr<TapeKeeper> k;
	F f;
	DoldrumsResult out;
	float inL = 0.f, inR = 0.f;   // the input patched (run's default)
	std::vector<std::string> infos, warns;
	std::mutex logLock;
	explicit Module(float rate) {
		f.params[F::POSITION] = 1.f;   // Position at now, as these tests were written (the default moved to 0.9 at 2.0.46; its own effect is test_engine's default_position_freezes_a_stretch)
		k.reset(new TapeKeeper(*e));
		k->log = [this](bool warn, const std::string& s) {
			std::lock_guard<std::mutex> lk(logLock);
			(warn ? warns : infos).push_back(s);
		};
		k->makeDir = [](const std::string& d) {
			std::error_code ec;
			fs::create_directories(d, ec);
		};
		e->seed(7);
		e->allocate(rate);
		e->setSampleRate(rate);
		k->configured();
		k->tapeRate(rate);
		f.inRPatched = true;
	}
	~Module() { k->stop(); }
	// n samples of the audio thread, with the input `inL`, `inR`.
	void run(long long n) {
		for (long long i = 0; i < n; i++) {
			f.inL = inL;
			f.inR = inR;
			k->buttonIs(f.params[F::FREEZE] >= 0.5f);
			e->process(f, out);
			k->afterProcess(800);
		}
	}
	// Runs in blocks until pred holds or `seconds` of wall time pass.
	template <class P>
	bool until(P pred, double seconds = 10.0) {
		const auto t0 = std::chrono::steady_clock::now();
		while (!pred()) {
			if (std::chrono::steady_clock::now() - t0 > std::chrono::duration<double>(seconds)) return false;
			run(64);
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
		return true;
	}
	int warnCount() {
		std::lock_guard<std::mutex> lk(logLock);
		return int(warns.size());
	}
	bool said(const std::string& part, bool warn) {
		std::lock_guard<std::mutex> lk(logLock);
		for (const std::string& s : warn ? warns : infos)
			if (s.find(part) != std::string::npos) return true;
		return false;
	}
};

void param_ids() {
	CHECK(F::STRETCH == 0 && F::STRETCH_ATT == 1 && F::POSITION == 2 && F::POSITION_ATT == 3 && F::HARMONICS == 4 &&
	          F::HARMONICS_ATT == 5 && F::BLUR == 6 && F::ROOT == 7 && F::TILT == 8 && F::SHIFT == 9 && F::PARTIALS == 10 &&
	          F::WIDTH == 11 && F::FREEZE == 12 && F::MIX == 13 && F::kParams == 14,
	      "the param ids are the plan's, in its order");
	const F d;
	// The plan's, but Position: 0.9 (about 3 s back), the user's decision at
	// 2.0.46 (it was 1, now). A default is not saved state: a patch saves
	// every param's value. And Mix: 0.5, the user's decision at 2.0.49 (it
	// was 1, all wet).
	const float want[14] = {0.5f, 0.f, 0.9f, 0.f, 0.f, 0.f, 0.f, -24.f, 0.f, 0.f, 8.f, 0.393f, 0.f, 0.5f};
	int bad = 0;
	for (int i = 0; i < 14; i++) bad += d.params[i] != want[i];
	CHECK(bad == 0, "the defaults are the plan's, but Position 0.9 and Mix 0.5 (%d differ)", bad);
}

SavedMap goldenMap(bool follow = false) {
	SavedMap m;
	if (follow)
		for (const golden::GoldenField& g : golden::kGoldenDataFollow)
			m[g.key] = {SavedValue::Kind(g.kind == 0 ? SavedValue::BOOL : SavedValue::INT), g.value};
	else
		for (const golden::GoldenField& g : golden::kGoldenData)
			m[g.key] = {SavedValue::Kind(g.kind == 0 ? SavedValue::BOOL : SavedValue::INT), g.value};
	return m;
}

void golden_patch_data() {
	const SavedState s = savedFromMap(goldenMap());
	CHECK(s.grain == 2 && s.wide && s.stopAtEnd && !s.keepTape && !s.follow,
	      "the golden data of 2.0.47 loads, as Drift (grain %d, wide %d, stop %d, keep %d, follow %d)", s.grain, int(s.wide), int(s.stopAtEnd),
	      int(s.keepTape), int(s.follow));
	const SavedState sf = savedFromMap(goldenMap(true));
	CHECK(sf.grain == 2 && sf.wide && sf.stopAtEnd && !sf.keepTape && sf.follow,
	      "the golden data with whileRecording loads (grain %d, wide %d, stop %d, keep %d, follow %d)", sf.grain, int(sf.wide), int(sf.stopAtEnd),
	      int(sf.keepTape), int(sf.follow));
	const SavedState d = savedFromMap(SavedMap());
	CHECK(d.grain == 1 && !d.wide && !d.stopAtEnd && d.keepTape && !d.follow, "nothing saved: the defaults (Drift)");
	const SavedMap w = savedToMap(SavedState());
	CHECK(w.size() == 5 && w.count("grain") && w.count("wide") && w.count("atEnd") && w.count("keepTape") && w.count("whileRecording"),
	      "the keys written are the five (%zu)", w.size());
	CHECK(w.at("grain").kind == SavedValue::INT && w.at("atEnd").kind == SavedValue::INT && w.at("wide").kind == SavedValue::BOOL &&
	          w.at("keepTape").kind == SavedValue::BOOL && w.at("whileRecording").kind == SavedValue::INT && w.at("whileRecording").value == 0.0,
	      "each key of its kind, whileRecording 0 by default");
	const SavedMap wf = savedToMap(sf);
	CHECK(wf.at("whileRecording").kind == SavedValue::INT && wf.at("whileRecording").value == 1.0, "Follow is written 1");
	for (const SavedState& x : {s, sf}) {
		const SavedState back = savedFromMap(savedToMap(x));
		CHECK(back.grain == x.grain && back.wide == x.wide && back.stopAtEnd == x.stopAtEnd && back.keepTape == x.keepTape && back.follow == x.follow,
		      "written and read back (follow %d)", int(x.follow));
	}
	// whileRecording bad (wrong kind, out of range, not whole): Drift, the
	// others load.
	const SavedValue badFollow[] = {{SavedValue::INT, 2.0},  {SavedValue::INT, -1.0}, {SavedValue::BOOL, 1.0},
	                                {SavedValue::REAL, 1.0}, {SavedValue::INT, 0.5},  {SavedValue::INT, 1e12}};
	for (const SavedValue& v : badFollow) {
		SavedMap m = goldenMap(true);
		m["whileRecording"] = v;
		const SavedState t = savedFromMap(m);
		CHECK(!t.follow && t.grain == 2 && t.wide && t.stopAtEnd && !t.keepTape, "whileRecording bad (kind %d, %g): Drift alone", int(v.kind), v.value);
	}
	// Each field bad alone: it takes its default, the others load.
	struct Bad {
		const char* key;
		SavedValue v;
	};
	const Bad bads[] = {
	    {"grain", {SavedValue::INT, 3.0}},   {"grain", {SavedValue::INT, -1.0}},   {"grain", {SavedValue::REAL, 2.0}},
	    {"grain", {SavedValue::BOOL, 1.0}},  {"wide", {SavedValue::INT, 1.0}},     {"atEnd", {SavedValue::INT, 2.0}},
	    {"atEnd", {SavedValue::BOOL, 1.0}},  {"keepTape", {SavedValue::INT, 0.0}}, {"atEnd", {SavedValue::INT, 0.5}},
	};
	for (const Bad& b : bads) {
		SavedMap m = goldenMap();
		m[b.key] = b.v;
		const SavedState t = savedFromMap(m);
		const std::string k = b.key;
		const bool ok = k == "grain"   ? t.grain == 1 && t.wide && t.stopAtEnd && !t.keepTape
		                : k == "wide"  ? t.grain == 2 && !t.wide && t.stopAtEnd && !t.keepTape
		                : k == "atEnd" ? t.grain == 2 && t.wide && !t.stopAtEnd && !t.keepTape
		                               : t.grain == 2 && t.wide && t.stopAtEnd && t.keepTape;
		CHECK(ok, "%s bad (kind %d, %g): its default alone", b.key, int(b.v.kind), b.v.value);
	}
	SavedMap m = goldenMap();
	m["version"] = {SavedValue::INT, 9.0};
	m["seed"] = {SavedValue::INT, 1234.0};
	const SavedState u = savedFromMap(m);
	CHECK(u.grain == 2 && u.wide && u.stopAtEnd && !u.keepTape, "keys it does not know are ignored");
}

void golden_tape_v1() {
	const std::string dir = gTemp->sub("golden");
	writeBytes(tapeFile(dir), goldenBytes());
	// Straight through TapeStore's reader.
	Ring r;
	std::string why;
	CHECK(readTape(dir, 48000.f, r, why) && why.empty(), "the golden file reads (%s)", why.c_str());
	CHECK(goldenMismatches(r) == 0, "the golden file to the bit (%lld frames differ)", goldenMismatches(r));

	// As the module opens it: frozen by the button, the tape kept.
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.k->start();
	m.k->storeIn(dir);
	CHECK(m.k->openFrozen(dir, true), "a file there: the read asked for");
	const bool taken = m.until([&] { return m.e->tapesTaken() == 1; });
	// The tape is taken wherever the worker's timing puts it in a hop; the new
	// tape's pass (held: Position fully right) begins at the next boundary, a
	// hop (8192 at Medium, 48 kHz) at most later, and the window shows it at
	// the publish after that (every 800 samples here).
	m.run(8192 + 2 * 800);
	CHECK(taken && m.e->live() && goldenMismatches(*m.e->live()) == 0, "opened: the golden tape live, to the bit");
	CHECK(!m.e->tapePending() && m.k->trouble() == TapeKeeper::FINE && m.warnCount() == 0, "nothing pending, no trouble, no warning");
	// The window shows it: the snapshot is that of a view rebuilt from the tape.
	TapeView ref;
	ref.rebuild(*m.e->live());
	ref.publish(m.e->state());
	TapeSnapshot a, b;
	CHECK(m.k->read(a) && ref.read(b), "both views read");
	int cols = 0;
	for (int i = 0; i < TapeSnapshot::COLS; i++) cols += a.low[i] != b.low[i] || a.high[i] != b.high[i];
	CHECK(cols == 0 && std::strcmp(a.state, "FROZEN AT END") == 0, "the window: the golden tape (%d columns differ), \"%s\"", cols, a.state);
	// The next save leaves the file as it is: it already holds this freeze.
	TapeKeeper::SaveAsk ask;
	ask.dir = dir;
	ask.button = true;
	ask.rate = 48000.f;
	SaveAction act = SAVE_WRITE;
	CHECK(m.k->save(ask, act) && act == SAVE_NOTHING && !m.said("stored the frozen tape", false), "the save writes nothing (%d)", int(act));
	CHECK(m.k->freezeAttempts() == 0, "nor did the freeze's own write try (%u)", m.k->freezeAttempts());
	CHECK(readAll(tapeFile(dir)) == goldenBytes(), "the golden file is as it was");
}

void writer_is_golden() {
	const std::string dir = gTemp->sub("writer");
	std::vector<float> l(golden::kTapeFrames), r(golden::kTapeFrames);
	for (uint32_t i = 0; i < golden::kTapeFrames; i++) {
		l[i] = golden::goldenLeft(i);
		r[i] = golden::goldenRight(i);
	}
	Ring g;
	g.allocate(golden::kTapeFrames);
	g.fill(l.data(), r.data());
	std::string why;
	CHECK(writeTape(dir, g, 48000.f, why), "written (%s)", why.c_str());
	const std::string got = readAll(tapeFile(dir));
	CHECK(got.size() == 44 + size_t(golden::kTapeFrames) * 8 && std::memcmp(got.data(), golden::kTapeHeaderV1, 44) == 0,
	      "the header is the golden one (%zu bytes)", got.size());
	CHECK(got == goldenBytes(), "the whole file is the golden one");
}

// Runs n samples of a sine at the input; the output's peak over the last
// half of them.
double sineFor(Module& m, long long n) {
	double peak = 0.0;
	for (long long i = 0; i < n; i++) {
		m.inL = float(3.0 * std::sin(0.031 * double(i)));
		m.inR = float(-2.0 * std::sin(0.023 * double(i)));
		m.run(1);
		if (i > n / 2) peak = std::max(peak, std::max(std::fabs(double(m.out.outL)), std::fabs(double(m.out.outR))));
	}
	return peak;
}
// The tape records: its newest frame is the input just written.
bool records(Module& m) {
	const Ring& g = m.e->ring();
	const long long p = g.physical(g.now() - 1);
	return !m.e->state().frozen && g.channel(0)[p] == m.inL && g.channel(1)[p] == m.inR;
}

void opened_frozen_without_a_file_opens_thawed() {
	const std::string dir = gTemp->sub("no_tape");
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;   // the patch's Freeze, on
	m.k->start();
	m.k->storeIn(dir);
	const bool reading = m.k->openFrozen(dir, true);
	if (!reading) m.f.params[F::FREEZE] = 0.f;   // onAdd lets go of the button
	CHECK(!reading && !m.e->tapePending(), "no file: no read asked for, the button to be let go");
	const double peak = sineFor(m, 48000 * 2);
	CHECK(records(m), "the tape records the live input");
	CHECK(peak > 0.3, "the output is not silence (peak %g V)", peak);
	CHECK(m.said("has no stored tape", false) && m.said("Freeze let go", false) && m.warnCount() == 0,
	      "a line of information, no warning (%d warnings)", m.warnCount());
	CHECK(m.k->trouble() == TapeKeeper::FINE, "nothing in the window's other corner");
	CHECK(!m.k->letGoWanted(), "nothing more to let go");
}

void opened_frozen_with_keep_off_opens_thawed() {
	const std::string dir = gTemp->sub("keep_off");
	writeBytes(tapeFile(dir), goldenBytes());
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.k->start();
	m.k->keepTape(false);
	const bool reading = m.k->openFrozen(dir, false);
	if (!reading) m.f.params[F::FREEZE] = 0.f;
	CHECK(!reading && !m.e->tapePending(), "Keep off: no read, the button to be let go");
	sineFor(m, 4800);
	CHECK(records(m) && m.said("Freeze let go", false) && m.warnCount() == 0, "the tape records; information, no warning");
}

void opened_frozen_with_a_bad_file_opens_thawed() {
	const std::string dir = gTemp->sub("bad");
	const std::string bytes = goldenBytes();
	writeBytes(tapeFile(dir), bytes.substr(0, bytes.size() / 3));
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.k->start();
	m.k->storeIn(dir);
	const bool reading = m.k->openFrozen(dir, true);
	if (!reading) m.f.params[F::FREEZE] = 0.f;
	CHECK(!reading && !m.e->tapePending(), "a cut file: refused at open, the button to be let go");
	CHECK(m.said("was not read", true) && m.k->trouble() == TapeKeeper::NOT_READ && m.said("Freeze let go", true),
	      "a warning saying so, and TAPE NOT READ");
	const double peak = sineFor(m, 48000 * 2);
	CHECK(records(m) && peak > 0.3, "the tape records, the output is not silence (peak %g V)", peak);
	TapeKeeper::SaveAsk ask;
	ask.dir = dir;
	ask.button = false;
	SaveAction act = SAVE_NOTHING;
	CHECK(m.k->save(ask, act) && act == SAVE_REMOVE && !fs::exists(tapeFile(dir)) && m.k->trouble() == TapeKeeper::FINE,
	      "the next save removes it, the trouble gone (%d)", int(act));
}

void a_read_that_fails_late_lets_go() {
	const std::string dir = gTemp->sub("late");
	writeBytes(tapeFile(dir), goldenBytes());
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.k->storeIn(dir);
	CHECK(m.k->openFrozen(dir, true), "a file that looks like a tape: the read asked for");
	writeBytes(tapeFile(dir), "not a tape any more");   // changed before the worker reads it
	m.run(100);
	CHECK(m.e->tapePending() && m.e->state().frozen && !m.k->letGoWanted(), "frozen, waiting for the read; nothing to let go yet");
	m.k->serviceOnce();   // the worker's round: the read fails
	CHECK(!m.e->tapePending() && m.said("was not read", true) && m.k->trouble() == TapeKeeper::NOT_READ, "the read failed: given up, shown");
	CHECK(m.k->letGoWanted(), "the UI is told to let go");
	CHECK(!m.k->letGoWanted(), "once");
	m.f.params[F::FREEZE] = 0.f;   // the UI lets go
	const double peak = sineFor(m, 48000 * 2);
	CHECK(records(m) && peak > 0.3, "let go: the tape records, the output is not silence (peak %g V)", peak);
}

void a_press_during_the_read_keeps_the_button() {
	const std::string dir = gTemp->sub("pressed");
	writeBytes(tapeFile(dir), goldenBytes());
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.f.freezeGate = 10.f;   // a gate holds the tape frozen throughout, so the wait stands
	m.k->storeIn(dir);
	CHECK(m.k->openFrozen(dir, true), "the read asked for");
	m.run(100);
	m.f.params[F::FREEZE] = 0.f;   // the user: off, and on again
	m.run(10);
	m.f.params[F::FREEZE] = 1.f;
	m.run(10);
	writeBytes(tapeFile(dir), "not a tape any more");
	CHECK(m.e->tapePending(), "frozen by the gate throughout: still waiting");
	m.k->serviceOnce();
	CHECK(!m.e->tapePending() && m.said("was not read", true), "the read failed");
	CHECK(!m.k->letGoWanted(), "pressed again during the read: the button is the user's, nothing let go");
}

void save_round_trip() {
	const std::string dir = gTemp->sub("round");
	Module a(44100.f);
	a.k->start();
	long long t = 0;
	for (; t < 44100 * 31; t++) {
		a.inL = float(3.0 * std::sin(0.01 * double(t)));
		a.inR = float(2.0 * std::sin(0.013 * double(t)));
		a.run(1);
	}
	a.f.params[F::FREEZE] = 1.f;
	a.run(44100);
	TapeKeeper::SaveAsk ask;
	ask.dir = dir;
	ask.button = true;
	ask.rate = 44100.f;
	SaveAction act = SAVE_NOTHING;
	CHECK(a.k->save(ask, act) && act == SAVE_WRITE && a.said("stored the frozen tape", false), "written (%d)", int(act));
	CHECK(a.k->save(ask, act) && act == SAVE_NOTHING, "not written twice for one freeze (%d)", int(act));

	Module b(44100.f);
	b.f.params[F::FREEZE] = 1.f;
	b.k->start();
	b.k->expectStored(dir);
	CHECK(b.until([&] { return b.e->tapesTaken() == 1; }), "the saved tape is handed in");
	b.run(100);
	const Ring& ra = a.e->ring();
	const Ring* rb = b.e->live();
	long long bad = rb && rb->capacity() == ra.capacity() ? 0 : -1;
	for (long long i = 0; bad >= 0 && i < ra.capacity(); i++) {
		const long long pa = ra.physical(ra.now() - ra.capacity() + i), pb = rb->physical(rb->now() - rb->capacity() + i);
		bad += std::memcmp(&ra.channel(0)[pa], &rb->channel(0)[pb], 4) != 0 || std::memcmp(&ra.channel(1)[pa], &rb->channel(1)[pb], 4) != 0;
	}
	CHECK(bad == 0, "the same tape to the bit (%lld frames differ)", bad);
}

void save_not_written_is_shown() {
	// The folder's place is taken by a file: nothing can be written there.
	const std::string base = gTemp->sub("unwritable");
	const std::string dir = (fs::path(base) / "blocked").string();
	writeBytes(dir, "a file, not a folder");
	Module m(48000.f);
	m.k->start();
	m.run(48000);
	m.f.params[F::FREEZE] = 1.f;
	m.run(4800);
	TapeKeeper::SaveAsk ask;
	ask.dir = dir;
	ask.button = true;
	SaveAction act = SAVE_NOTHING;
	CHECK(m.k->save(ask, act) && act == SAVE_REMOVE, "not written: the old file is to go (%d)", int(act));
	CHECK(m.said("was not stored", true) && m.k->trouble() == TapeKeeper::NOT_SAVED, "a warning, and TAPE NOT SAVED");
	m.k->cleared();
	CHECK(m.k->trouble() == TapeKeeper::FINE, "Initialize clears it");
}

// An offer taken is settled only once the audio side has switched to its
// view (TapeStore.hpp 2c/2d): until then the worker makes nothing new, so it
// cannot rebuild the view the audio side is about to switch to. The worker's
// rounds are run here, a step at a time (serviceOnce), with a process() whose
// afterProcess() has not yet run between them.
void offer_settles_after_the_switch() {
	const std::string dir = gTemp->sub("settle");
	writeBytes(tapeFile(dir), goldenBytes());
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.run(4800);
	m.k->expectStored(dir);
	m.k->serviceOnce();   // read, the spare view rebuilt, offered
	m.e->process(m.f, m.out);   // taken; afterProcess not yet run
	CHECK(m.e->tapesTaken() == 1, "taken");
	m.k->expectStored(dir);     // another tape wanted at once
	m.k->serviceOnce();
	int reads = 0;
	for (const std::string& s : m.infos) reads += s.find("read the stored tape") != std::string::npos;
	CHECK(reads == 1, "nothing made before the switch (%d reads)", reads);
	m.k->afterProcess(800);     // the switch
	m.k->serviceOnce();
	reads = 0;
	for (const std::string& s : m.infos) reads += s.find("read the stored tape") != std::string::npos;
	CHECK(reads == 2, "made once the switch is seen (%d reads)", reads);
}

} // namespace

int main() {
	std::srand((unsigned) std::time(nullptr));
	TempDir temp;
	gTemp = &temp;
	param_ids();
	golden_patch_data();
	golden_tape_v1();
	writer_is_golden();
	opened_frozen_without_a_file_opens_thawed();
	opened_frozen_with_keep_off_opens_thawed();
	opened_frozen_with_a_bad_file_opens_thawed();
	a_read_that_fails_late_lets_go();
	a_press_during_the_read_keeps_the_button();
	save_round_trip();
	save_not_written_is_shown();
	offer_settles_after_the_switch();
	gTemp = nullptr;
	return rkatest::finish("doldrums/test_saved");
}

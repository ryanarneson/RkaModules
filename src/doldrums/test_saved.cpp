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
//   live through the keeper, its window shows it, and the next save leaves
//   the file alone (not written again for the same freeze).
// - writer_is_golden: what the save writes today is the golden format, byte
//   for byte.
// - saved_without_tape_opens_silent: a patch saved frozen without tape.wav
//   opens with a silent tape (with a live input patched), no tape pending,
//   a line of information and no warning, nothing in the window's corner.
// - bad_file_is_shown: a cut file is a warning and TAPE NOT READ; the file
//   stays while the button is on; thawed, the save removes it and the
//   trouble goes.
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

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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
		f.inRPatched = true;
	}
	~Module() { k->stop(); }
	// n samples of the audio thread, with the input `inL`, `inR`.
	void run(long long n) {
		for (long long i = 0; i < n; i++) {
			f.inL = inL;
			f.inR = inR;
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
	const float want[14] = {0.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, -24.f, 0.f, 0.f, 8.f, 0.393f, 0.f, 1.f};
	int bad = 0;
	for (int i = 0; i < 14; i++) bad += d.params[i] != want[i];
	CHECK(bad == 0, "the defaults are the plan's (%d differ)", bad);
}

SavedMap goldenMap() {
	SavedMap m;
	for (const golden::GoldenField& g : golden::kGoldenData)
		m[g.key] = {SavedValue::Kind(g.kind == 0 ? SavedValue::BOOL : SavedValue::INT), g.value};
	return m;
}

void golden_patch_data() {
	const SavedState s = savedFromMap(goldenMap());
	CHECK(s.grain == 2 && s.wide && s.stopAtEnd && !s.keepTape, "the golden data loads (grain %d, wide %d, stop %d, keep %d)", s.grain,
	      int(s.wide), int(s.stopAtEnd), int(s.keepTape));
	const SavedState d = savedFromMap(SavedMap());
	CHECK(d.grain == 1 && !d.wide && !d.stopAtEnd && d.keepTape, "nothing saved: the defaults");
	const SavedMap w = savedToMap(SavedState());
	CHECK(w.size() == 4 && w.count("grain") && w.count("wide") && w.count("atEnd") && w.count("keepTape"),
	      "the keys written are the four (%zu)", w.size());
	CHECK(w.at("grain").kind == SavedValue::INT && w.at("atEnd").kind == SavedValue::INT && w.at("wide").kind == SavedValue::BOOL &&
	          w.at("keepTape").kind == SavedValue::BOOL,
	      "each key of its kind");
	const SavedState back = savedFromMap(savedToMap(s));
	CHECK(back.grain == s.grain && back.wide == s.wide && back.stopAtEnd == s.stopAtEnd && back.keepTape == s.keepTape, "written and read back");
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
	m.k->expectStored(dir);
	const bool taken = m.until([&] { return m.e->tapesTaken() == 1; });
	m.run(2000);
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
	CHECK(cols == 0 && std::strcmp(a.state, "FROZEN HELD") == 0, "the window: the golden tape (%d columns differ), \"%s\"", cols, a.state);
	// The next save leaves the file as it is: it already holds this freeze.
	TapeKeeper::SaveAsk ask;
	ask.dir = dir;
	ask.button = true;
	ask.rate = 48000.f;
	SaveAction act = SAVE_WRITE;
	CHECK(m.k->save(ask, act) && act == SAVE_NOTHING && !m.said("stored the frozen tape", false), "the save writes nothing (%d)", int(act));
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

void saved_without_tape_opens_silent() {
	const std::string dir = gTemp->sub("no_tape");
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;   // Position fully right: held at the end
	m.inL = 3.f;                   // a live input patched, from the first sample
	m.inR = -2.f;
	m.k->start();
	m.k->expectStored(dir);
	double peak = 0.0;
	const auto loud = [&](long long n) {
		for (long long i = 0; i < n; i++) {
			m.run(1);
			peak = std::max(peak, std::max(std::fabs(double(m.out.outL)), std::fabs(double(m.out.outR))));
		}
	};
	loud(1);
	const bool settled = m.until([&] { return !m.e->tapePending(); });
	loud(48000 * 2);
	CHECK(settled && !m.e->tapePending(), "no tape pending once the worker has looked");
	CHECK(peak == 0.0, "silent, with an input patched (peak %g V)", peak);
	CHECK(m.said("has no stored tape", false) && m.warnCount() == 0, "a line of information, no warning (%d warnings)", m.warnCount());
	TapeSnapshot s;
	CHECK(m.k->trouble() == TapeKeeper::FINE && m.k->read(s), "nothing in the window's other corner");
	float hi = 0.f;
	for (int i = 0; i < TapeSnapshot::COLS; i++) hi = std::max(hi, std::max(s.high[i], -s.low[i]));
	CHECK(hi == 0.f, "the window: a silent tape (%g)", hi);
}

void bad_file_is_shown() {
	const std::string dir = gTemp->sub("bad");
	const std::string bytes = goldenBytes();
	writeBytes(tapeFile(dir), bytes.substr(0, bytes.size() / 3));
	Module m(48000.f);
	m.f.params[F::FREEZE] = 1.f;
	m.k->start();
	m.k->expectStored(dir);
	const bool settled = m.until([&] { return !m.e->tapePending(); });
	m.run(4800);
	CHECK(settled && m.said("was not read", true) && m.k->trouble() == TapeKeeper::NOT_READ, "a warning, and TAPE NOT READ");
	TapeKeeper::SaveAsk ask;
	ask.dir = dir;
	ask.button = true;
	SaveAction act = SAVE_WRITE;
	CHECK(m.k->save(ask, act) && act == SAVE_NOTHING && fs::exists(tapeFile(dir)), "the button on: the file stays (%d)", int(act));
	CHECK(m.k->trouble() == TapeKeeper::NOT_READ, "still shown");
	ask.button = false;
	CHECK(m.k->save(ask, act) && act == SAVE_REMOVE && m.k->trouble() == TapeKeeper::FINE, "thawed: removed, the trouble gone (%d)", int(act));
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
	saved_without_tape_opens_silent();
	bad_file_is_shown();
	save_round_trip();
	save_not_written_is_shown();
	offer_settles_after_the_switch();
	gTemp = nullptr;
	return rkatest::finish("doldrums/test_saved");
}

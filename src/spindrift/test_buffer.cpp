// Spindrift's saved state (TESTING.md rule 5; the plan's Review Focus 3):
// the frozen buffer kept with the patch (BufferFile.hpp, the engine's
// bufferImage() and loadBufferImage()) and the module's JSON fields
// (SavedState.hpp, which Spindrift.cpp's dataToJson and dataFromJson use).
//
// - golden_buffer_v1: the committed version 1 file (testdata/GoldenBufferV1.hpp)
//   loads to the bit: every payload byte in Beads' ring, the write pointer,
//   the quality and mono as stated; the window's waveform is rescanned.
// - round_trip: a frozen engine's image, through a real file, into a new
//   engine: the ring is the same bytes (its interpolation tail too), and it
//   plays exactly what an engine given the same bytes through Beads' own
//   restore (background_buffer_backup.cc: Configure, raw bytes, write
//   pointer) plays, for 2 s; an engine with nothing restored plays silence.
// - rejects_*, missing_file: a file that does not fit loads nothing, and
//   the engine is an empty, unfrozen one: it plays what a fresh engine plays.
// - saved_state_*: the JSON fields for the defaults (the golden map), a
//   round trip of every field away from its default, the golden version 1
//   map, and every way a field can be wrong.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "testing.hpp"
#include "BufferFile.hpp"
#include "SavedState.hpp"
#include "SpindriftEngine.hpp"
#include "testdata/GoldenBufferV1.hpp"

using spindrift::BufferHeader;
using spindrift::Jacks;
using spindrift::Knobs;
using spindrift::Out;
using spindrift::SpindriftEngine;

namespace {

const double kPi = 3.14159265358979323846;
const int kRate = 48000;

Knobs wetKnobs() {
	Knobs k;
	k.time = k.size = k.shape = k.pitch = 0.5f;
	k.density = 0.8f;  // random grains, often
	k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
	k.feedback = 0.f;
	k.dryWet = 1.f;  // the grains alone
	k.reverb = 0.f;
	return k;
}

Jacks stereo() {
	Jacks j = {};
	j.inLOn = j.inROn = true;
	return j;
}

void signal(long n, Jacks& j) {
	const double t = double(n) / kRate;
	j.inL = float(4.0 * std::sin(2 * kPi * 220.0 * t));
	j.inR = float(4.0 * std::sin(2 * kPi * 331.0 * t));
}

std::unique_ptr<SpindriftEngine> makeEngine(uint32_t seed) {
	std::unique_ptr<SpindriftEngine> e(new SpindriftEngine(seed));
	e->allocate();
	e->setSampleRate(float(kRate));
	return e;
}

// A scratch file under build/ (the tests run from the repo's root).
std::string scratch(const char* name) {
	return std::string("build/test/spindrift/") + name;
}

std::vector<uint8_t> fileBytes(const BufferHeader& h, const std::vector<uint8_t>& payload) {
	std::vector<uint8_t> f(spindrift::kBufferHeaderBytes);
	spindrift::encodeHeader(h, f.data());
	f.insert(f.end(), payload.begin(), payload.end());
	return f;
}

void writeRaw(const std::string& path, const std::vector<uint8_t>& bytes) {
	FILE* f = std::fopen(path.c_str(), "wb");
	CHECK(f, "open %s", path.c_str());
	if (!f)
		return;
	if (!bytes.empty())
		std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

// The engine's memory as Beads addresses it (Processor::delay_buffer_raw_bytes):
// the saved payload's bytes (the ring and its interpolation tail).
std::vector<uint8_t> ring(SpindriftEngine& e, size_t bytes = spindrift::kBufferPayloadBytes) {
	std::vector<uint8_t> r(bytes);
	for (size_t i = 0; i < bytes; i++)
		r[i] = *e.processor().delay_buffer_raw_bytes(i);
	return r;
}

// Runs an engine on the test's signal (or, silent, on patched inputs at
// 0 V) from sample `from` for `n` samples, into out (l, r interleaved).
void run(SpindriftEngine& e, const Knobs& k, Jacks j, long from, long n, std::vector<float>* out = nullptr,
         bool silent = false) {
	for (long i = from; i < from + n; i++) {
		if (!silent)
			signal(i, j);
		Out o;
		e.process(k, j, o);
		if (out) {
			out->push_back(o.l);
			out->push_back(o.r);
		}
	}
}

double rms(const std::vector<float>& v) {
	double s = 0;
	for (float x : v)
		s += double(x) * x;
	return v.empty() ? 0.0 : std::sqrt(s / double(v.size()));
}

// A frozen image from a Digital stereo engine that heard the signal for 5 s:
// past the 4 s ring, so the write pointer has passed the ring's start and its
// interpolation tail holds the signal too.
struct Frozen {
	BufferHeader h;
	std::vector<uint8_t> payload, tail;
	bool ok = false;
};
Frozen frozenImage(uint32_t seed) {
	Frozen f;
	std::unique_ptr<SpindriftEngine> a = makeEngine(seed);
	run(*a, wetKnobs(), stereo(), 0, 5 * kRate);
	CHECK(a->ringSeq() % 2 == 1, "not frozen: the ring is being written (seq %u)", a->ringSeq());
	BufferHeader none;
	std::vector<uint8_t> scratchBytes(spindrift::kBufferPayloadBytes);
	CHECK(!a->bufferImage(none, scratchBytes.data()), "no image while the buffer is not frozen");
	a->buttons().freezePress();
	run(*a, wetKnobs(), stereo(), 5 * kRate, kRate / 10);
	CHECK(a->frozen(), "frozen");
	f.payload.resize(spindrift::kBufferPayloadBytes);
	f.ok = a->bufferImage(f.h, f.payload.data());
	CHECK(f.ok, "a frozen engine gives its image");
	CHECK(f.h.quality == spindrift::DIGITAL && !f.h.mono, "Digital, stereo (%u, %u)", f.h.quality, f.h.mono);
	CHECK(f.payload == ring(*a), "the image is the ring");
	f.tail.assign(f.payload.begin() + spindrift::kSavedBytes, f.payload.end());
	long tailSet = 0;
	for (uint8_t b : f.tail)
		tailSet += b != 0;
	CHECK(tailSet > 8, "the tail holds the signal (%ld bytes not 0)", tailSet);
	CHECK(int(f.h.writePos) == *a->processor().mutable_delay_write_ptr(), "the write pointer");
	long nonzero = 0;
	for (uint8_t b : f.payload)
		nonzero += b != 0;
	CHECK(nonzero > 100000, "the image holds the signal (%ld bytes not 0)", nonzero);
	// Unfrozen, the image is gone again.
	const uint32_t seq = a->ringSeq();
	a->buttons().freezePress();
	run(*a, wetKnobs(), stereo(), 0, 100);
	CHECK(a->ringSeq() != seq && !a->bufferImage(none, scratchBytes.data()), "no image once unfrozen");
	return f;
}

// After a failed load the engine must be an empty, unfrozen one: it plays
// what a fresh engine with the same seed and quality plays, on the signal,
// and is not frozen.
void expectFresh(SpindriftEngine& e, uint32_t seed, spindrift::Quality q, const char* what) {
	std::unique_ptr<SpindriftEngine> fresh = makeEngine(seed);
	fresh->buttons().quality = fresh->buttons().pending = q;
	std::vector<float> a, b;
	run(e, wetKnobs(), stereo(), 0, kRate / 2, &a);
	run(*fresh, wetKnobs(), stereo(), 0, kRate / 2, &b);
	CHECK(!e.buttons().frozenButton && !e.frozen(), "%s: not frozen", what);
	CHECK(a == b, "%s: plays as a fresh engine", what);
	CHECK(rms(a) > 0.01, "%s: (and the fresh engine plays: rms %g)", what, rms(a));
}

void golden_buffer_v1() {
	using namespace spindrift::golden;
	std::vector<uint8_t> file(kBufferHeaderV1, kBufferHeaderV1 + sizeof kBufferHeaderV1);
	for (uint32_t i = 0; i < kPayloadBytes; i++)
		file.push_back(goldenPayloadByte(i));
	const std::string path = scratch("golden-v1.bin");
	writeRaw(path, file);

	BufferHeader h;
	std::vector<uint8_t> payload;
	std::string why;
	CHECK(spindrift::readBufferFile(path, h, payload, why), "the golden file reads (%s)", why.c_str());
	CHECK(h.magic == spindrift::kBufferMagic && h.version == 1, "magic, version 1");
	CHECK(h.quality == kQuality && bool(h.mono) == kMono, "quality %u, mono %u", h.quality, h.mono);
	CHECK(h.writePos == kWritePos && h.bytes == kPayloadBytes, "write pointer %u, %u bytes", h.writePos, h.bytes);
	CHECK(payload.size() == kPayloadBytes, "the payload (%zu bytes)", payload.size());
	// The header encodes back to the committed bytes.
	uint8_t again[spindrift::kBufferHeaderBytes];
	spindrift::encodeHeader(h, again);
	CHECK(std::memcmp(again, kBufferHeaderV1, sizeof again) == 0, "the header encodes back to its bytes");

	// The patch says Vintage Digital (its JSON, before onAdd).
	std::unique_ptr<SpindriftEngine> e = makeEngine(7);
	e->buttons().quality = e->buttons().pending = spindrift::VINTAGE;
	CHECK(e->loadBufferImage(h, payload.data(), payload.size()), "the golden file loads");
	CHECK(e->buttons().frozenButton, "Freeze latched (beads.cc:344-347)");
	CHECK(e->processor().media() == beads::Processor::MEDIA_VINTAGE_DIGITAL && e->processor().mono(),
	      "Vintage Digital, mono");
	CHECK(*e->processor().mutable_delay_write_ptr() == int(kWritePos), "the write pointer");
	CHECK(e->processor().bufferFrames() == int(spindrift::ringFrames(kQuality, kMono)), "ringFrames() is Beads' ring (%d)",
	      e->processor().bufferFrames());
	long wrong = 0;
	for (uint32_t i = 0; i < kPayloadBytes; i++)
		wrong += *e->processor().delay_buffer_raw_bytes(i) != goldenPayloadByte(i);
	CHECK(wrong == 0, "every payload byte in Beads' ring (%ld wrong)", wrong);

	// It plays, frozen, and the ring and the image stay as loaded.
	Jacks j = stereo();
	j.inROn = false;  // mono, as stored
	std::vector<float> out;
	run(*e, wetKnobs(), j, 0, 4 * kRate, &out, true);
	CHECK(e->frozen() && e->quality() == spindrift::VINTAGE && e->mono(), "frozen, Vintage, mono");
	CHECK(rms(out) > 0.01, "it plays the loaded buffer (rms %g)", rms(out));
	BufferHeader back;
	std::vector<uint8_t> image(spindrift::kBufferPayloadBytes);
	CHECK(e->bufferImage(back, image.data()), "an image of the loaded buffer");
	CHECK(back.quality == kQuality && bool(back.mono) == kMono && back.writePos == kWritePos, "its header");
	CHECK(image == payload, "its bytes");

	// The window's waveform was rescanned over the whole ring (Task 6's carry).
	int drawn = 0;
	for (int c = 0; c < rka::Overview::COLS; c++)
		drawn += e->overview().hi[c].load() - e->overview().lo[c].load() > 0.05f;
	CHECK(drawn > rka::Overview::COLS * 9 / 10, "the waveform shows the loaded ring (%d of %d columns)", drawn,
	      rka::Overview::COLS);
}

void round_trip() {
	const Frozen f = frozenImage(0x5eed);
	if (!f.ok)
		return;
	const std::string path = scratch("round-trip.bin");
	std::string why;
	CHECK(spindrift::writeBufferFile(path, f.h, f.payload.data(), why), "written (%s)", why.c_str());
	BufferHeader h;
	std::vector<uint8_t> payload;
	CHECK(spindrift::readBufferFile(path, h, payload, why), "read back (%s)", why.c_str());
	CHECK(payload == f.payload && h.writePos == f.h.writePos && h.quality == f.h.quality && h.mono == f.h.mono,
	      "the file holds the image");

	// B: through the file, before its first sample (the module's onAdd).
	const uint32_t seed = 0xbead;
	std::unique_ptr<SpindriftEngine> b = makeEngine(seed);
	CHECK(b->loadBufferImage(h, payload.data(), payload.size()), "loads");
	const std::vector<uint8_t> whole = ring(*b);
	CHECK(std::vector<uint8_t>(whole.begin(), whole.begin() + spindrift::kSavedBytes) ==
	          std::vector<uint8_t>(f.payload.begin(), f.payload.begin() + spindrift::kSavedBytes),
	      "the same ring");
	CHECK(std::vector<uint8_t>(whole.begin() + spindrift::kSavedBytes, whole.end()) == f.tail,
	      "the same interpolation tail");

	// C: the same bytes through Beads' own restore (its raw byte access, 16
	// bytes further than Beads copies: BufferFile.hpp), after its first sample
	// (before its first block), Freeze pressed.
	std::unique_ptr<SpindriftEngine> c = makeEngine(seed);
	// D: nothing restored, Freeze pressed.
	std::unique_ptr<SpindriftEngine> d = makeEngine(seed);
	std::vector<float> ob, oc, od;
	run(*b, wetKnobs(), stereo(), 0, 1, &ob, true);
	run(*c, wetKnobs(), stereo(), 0, 1, &oc, true);
	run(*d, wetKnobs(), stereo(), 0, 1, &od, true);
	{
		spindrift::BeadsScope scope(c->context());
		c->processor().Configure(beads::Processor::Media(f.h.quality), f.h.mono != 0);
		for (size_t i = 0; i < spindrift::kBufferPayloadBytes; i++)
			*c->processor().delay_buffer_raw_bytes(i) = f.payload[i];
		*c->processor().mutable_delay_write_ptr() = int(f.h.writePos);
	}
	c->buttons().freezePress();
	d->buttons().freezePress();
	// On silent inputs: frozen, Beads lets a little of a live input through
	// (about -30 dB here), so only the buffer can sound.
	run(*b, wetKnobs(), stereo(), 1, 2 * kRate, &ob, true);
	run(*c, wetKnobs(), stereo(), 1, 2 * kRate, &oc, true);
	run(*d, wetKnobs(), stereo(), 1, 2 * kRate, &od, true);
	CHECK(b->frozen() && c->frozen(), "both frozen");
	CHECK(ob == oc, "B plays what Beads' own restore plays, for 2 s");
	CHECK(rms(ob) > 0.05, "B plays the buffer (rms %g)", rms(ob));
	CHECK(rms(od) == 0.0, "with nothing restored, silence (rms %g)", rms(od));
}

// Loads `h` (with the frozen image's payload) into a new engine whose patch
// says `q`: refused, and the engine is an empty, unfrozen one.
void expectRejected(const BufferHeader& h, const std::vector<uint8_t>& payload, spindrift::Quality q, const char* what) {
	std::unique_ptr<SpindriftEngine> e = makeEngine(31);
	e->buttons().quality = e->buttons().pending = q;
	CHECK(!e->loadBufferImage(h, payload.data(), payload.size()), "%s: refused", what);
	expectFresh(*e, 31, q, what);
}

void rejects() {
	const Frozen f = frozenImage(0x5eed);
	if (!f.ok)
		return;
	CHECK(spindrift::validBuffer(f.h, spindrift::kBufferHeaderBytes + f.payload.size()), "the image itself is valid");

	// rejects_other_quality: a Digital buffer where the patch says Vintage,
	// and a quality Beads does not have.
	expectRejected(f.h, f.payload, spindrift::VINTAGE, "other quality");
	BufferHeader h = f.h;
	h.quality = 4;
	CHECK(!spindrift::validBuffer(h, spindrift::kBufferHeaderBytes + f.payload.size()), "quality 4 is invalid");
	expectRejected(h, f.payload, spindrift::DIGITAL, "quality 4");

	// rejects_mono_mismatch: stereo with a write pointer only the mono ring
	// has (Digital stereo holds 192,000 frames; mono 384,000), and a mono
	// flag that is neither.
	h = f.h;
	h.writePos = 300000;
	CHECK(!spindrift::validBuffer(h, spindrift::kBufferHeaderBytes + f.payload.size()), "stereo, mono's write pointer");
	expectRejected(h, f.payload, spindrift::DIGITAL, "mono mismatch");
	h.mono = 1;
	CHECK(spindrift::validBuffer(h, spindrift::kBufferHeaderBytes + f.payload.size()), "(valid as mono)");
	h.mono = 2;
	expectRejected(h, f.payload, spindrift::DIGITAL, "mono flag 2");
	h = f.h;
	h.writePos = 192000;
	CHECK(!spindrift::validBuffer(h, spindrift::kBufferHeaderBytes + f.payload.size()), "the write pointer at the end");

	// rejects_newer_version, and a wrong magic.
	h = f.h;
	h.version = 2;
	expectRejected(h, f.payload, spindrift::DIGITAL, "newer version");
	h.version = 0;
	expectRejected(h, f.payload, spindrift::DIGITAL, "version 0");
	h = f.h;
	h.magic ^= 1;
	expectRejected(h, f.payload, spindrift::DIGITAL, "magic");

	// rejects_truncated: files short by one byte, the header alone, part of
	// the header, empty; one byte too long; and the engine given a short
	// payload.
	const std::vector<uint8_t> whole = fileBytes(f.h, f.payload);
	const size_t lengths[] = {whole.size() - 1, spindrift::kBufferHeaderBytes, 10, 0, whole.size() + 1};
	for (size_t len : lengths) {
		std::vector<uint8_t> bytes(whole.begin(), whole.begin() + std::min(len, whole.size()));
		if (len > whole.size())
			bytes.push_back(0);
		const std::string path = scratch("truncated.bin");
		writeRaw(path, bytes);
		BufferHeader rh;
		std::vector<uint8_t> payload;
		std::string why;
		CHECK(!spindrift::readBufferFile(path, rh, payload, why), "a file of %zu bytes is refused", len);
		CHECK(!why.empty(), "with a reason");
	}
	std::vector<uint8_t> shortPayload(f.payload.begin(), f.payload.end() - 1);
	expectRejected(f.h, shortPayload, spindrift::DIGITAL, "short payload");
	CHECK(!spindrift::validBuffer(f.h, spindrift::kBufferHeaderBytes + f.payload.size() - 1), "validBuffer: short");
	h = f.h;
	h.bytes = 4;
	CHECK(!spindrift::validBuffer(h, spindrift::kBufferHeaderBytes + 4), "a payload of another size");

	// missing_file.
	BufferHeader mh;
	std::vector<uint8_t> payload;
	std::string why;
	CHECK(!spindrift::readBufferFile(scratch("no-such-buffer.bin"), mh, payload, why), "a missing file is refused");
	CHECK(!why.empty(), "with a reason");
	std::unique_ptr<SpindriftEngine> e = makeEngine(31);
	expectFresh(*e, 31, spindrift::DIGITAL, "missing file");

	// A load after the engine has run: refused, and nothing frozen.
	std::unique_ptr<SpindriftEngine> r = makeEngine(31);
	r->buttons().freezePress();
	h = f.h;
	h.version = 9;
	CHECK(!r->loadBufferImage(h, f.payload.data(), f.payload.size()), "refused");
	CHECK(!r->buttons().frozenButton, "and Freeze released");
}

// ---- The JSON fields.

void saved_state_defaults() {
	// The golden: the JSON's fields for the defaults, as the module writes
	// them (keys, kinds, values). Never regenerate.
	struct Field {
		const char* key;
		spindrift::SavedValue::Kind kind;
		double value;
	};
	const Field golden[] = {
	    {"gain", spindrift::SavedValue::INT, 0},          {"gateOutput", spindrift::SavedValue::BOOL, 0},
	    {"keepFrozen", spindrift::SavedValue::BOOL, 1},   {"manualGain", spindrift::SavedValue::REAL, 1.0},
	    {"quality", spindrift::SavedValue::INT, 3},       {"seedLatched", spindrift::SavedValue::BOOL, 1},
	    {"frozen", spindrift::SavedValue::BOOL, 0},       {"synth", spindrift::SavedValue::INT, 0},
	    {"version", spindrift::SavedValue::INT, 1},
	};
	const spindrift::SavedMap m = spindrift::savedToMap(spindrift::SavedState());
	CHECK(m.size() == sizeof golden / sizeof golden[0], "%zu fields", m.size());
	for (const Field& g : golden) {
		auto it = m.find(g.key);
		CHECK(it != m.end(), "%s present", g.key);
		if (it == m.end())
			continue;
		CHECK(it->second.kind == g.kind && it->second.value == g.value, "%s: kind %d value %g", g.key,
		      int(it->second.kind), it->second.value);
	}
}

bool same(const spindrift::SavedState& a, const spindrift::SavedState& b) {
	return a.version == b.version && a.quality == b.quality && a.seedLatched == b.seedLatched && a.frozen == b.frozen &&
	       a.synth == b.synth && a.gain == b.gain && a.manualGain == b.manualGain && a.gateOutput == b.gateOutput &&
	       a.keepFrozen == b.keepFrozen;
}

spindrift::SavedState away() {
	spindrift::SavedState s;
	s.quality = spindrift::CLEAN_TAPE;
	s.seedLatched = false;
	s.frozen = true;
	s.synth = spindrift::SYNTH_NEVER;
	s.gain = spindrift::GAIN_MANUAL;
	s.manualGain = 12.5f;
	s.gateOutput = true;
	s.keepFrozen = false;
	return s;
}

void saved_state_round_trip() {
	const spindrift::SavedState s = away();
	CHECK(!same(s, spindrift::SavedState()), "every field away from its default");
	CHECK(same(spindrift::savedFromMap(spindrift::savedToMap(s)), s), "round trip");
	// An empty map: the defaults.
	CHECK(same(spindrift::savedFromMap(spindrift::SavedMap()), spindrift::SavedState()), "empty: defaults");
}

void saved_state_golden_v1() {
	// A version 1 patch's fields, written out (never regenerate).
	spindrift::SavedMap m;
	m["version"] = {spindrift::SavedValue::INT, 1};
	m["quality"] = {spindrift::SavedValue::INT, 1};
	m["seedLatched"] = {spindrift::SavedValue::BOOL, 0};
	m["frozen"] = {spindrift::SavedValue::BOOL, 1};
	m["synth"] = {spindrift::SavedValue::INT, 2};
	m["gain"] = {spindrift::SavedValue::INT, 2};
	m["manualGain"] = {spindrift::SavedValue::REAL, 12.5};
	m["gateOutput"] = {spindrift::SavedValue::BOOL, 1};
	m["keepFrozen"] = {spindrift::SavedValue::BOOL, 0};
	CHECK(same(spindrift::savedFromMap(m), away()), "the version 1 fields load as written");
}

void saved_state_hostile() {
	const spindrift::SavedState d;
	const spindrift::SavedMap good = spindrift::savedToMap(away());
	// Each field missing, of the wrong kind, or out of range: that field
	// alone takes its default.
	struct Bad {
		const char* key;
		spindrift::SavedValue v;
	};
	const double nan = std::nan("");
	const Bad bad[] = {
	    {"quality", {spindrift::SavedValue::INT, 4}},       {"quality", {spindrift::SavedValue::INT, -1}},
	    {"quality", {spindrift::SavedValue::REAL, 1.0}},    {"quality", {spindrift::SavedValue::BOOL, 1}},
	    {"seedLatched", {spindrift::SavedValue::INT, 0}},   {"frozen", {spindrift::SavedValue::REAL, 1}},
	    {"synth", {spindrift::SavedValue::INT, 3}},         {"synth", {spindrift::SavedValue::INT, 1e12}},
	    {"gain", {spindrift::SavedValue::INT, -2}},         {"manualGain", {spindrift::SavedValue::REAL, nan}},
	    {"manualGain", {spindrift::SavedValue::REAL, 0.5}}, {"manualGain", {spindrift::SavedValue::REAL, 41}},
	    {"manualGain", {spindrift::SavedValue::INT, 12}},   {"gateOutput", {spindrift::SavedValue::INT, 1}},
	    {"keepFrozen", {spindrift::SavedValue::REAL, 0}},   {"quality", {spindrift::SavedValue::INT, 1.5}},
	};
	for (const Bad& b : bad) {
		spindrift::SavedMap m = good;
		m[b.key] = b.v;
		const spindrift::SavedState s = spindrift::savedFromMap(m);
		spindrift::SavedState want = away();
		const std::string k = b.key;
		if (k == "quality")
			want.quality = d.quality;
		if (k == "seedLatched")
			want.seedLatched = d.seedLatched;
		if (k == "frozen")
			want.frozen = d.frozen;
		if (k == "synth")
			want.synth = d.synth;
		if (k == "gain")
			want.gain = d.gain;
		if (k == "manualGain")
			want.manualGain = d.manualGain;
		if (k == "gateOutput")
			want.gateOutput = d.gateOutput;
		if (k == "keepFrozen")
			want.keepFrozen = d.keepFrozen;
		CHECK(same(s, want), "%s wrong (kind %d, %g): that field's default, the rest kept", b.key, int(b.v.kind), b.v.value);
		m.erase(b.key);
		CHECK(same(spindrift::savedFromMap(m), want), "%s missing: its default", b.key);
	}
	// A newer version, or a version that is not one: all defaults.
	for (double v : {2.0, 0.0, -1.0}) {
		spindrift::SavedMap m = good;
		m["version"] = {spindrift::SavedValue::INT, v};
		CHECK(same(spindrift::savedFromMap(m), d), "version %g: the defaults", v);
	}
	spindrift::SavedMap m = good;
	m["version"] = {spindrift::SavedValue::REAL, 1.0};
	CHECK(same(spindrift::savedFromMap(m), d), "a version of the wrong kind: the defaults");
	m.erase("version");
	CHECK(same(spindrift::savedFromMap(m), d), "no version: the defaults");
}

}  // namespace

int main() {
	golden_buffer_v1();
	round_trip();
	rejects();
	saved_state_defaults();
	saved_state_round_trip();
	saved_state_golden_v1();
	saved_state_hostile();
	return rkatest::finish("spindrift test_buffer");
}

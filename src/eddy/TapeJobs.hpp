#pragma once
// Eddy's file and set jobs: what the module's worker thread does with whole
// tapes. Plain functions, Rack-free, C++11. Each makes a new TapeSet (or
// writes files) and touches no set the engine holds except to read it; none
// is for the audio thread (they allocate, read and write files, and convert).
//
// Each returns false with a reason in `why` and leaves its outputs as they
// were: `out` is set only on success, and a failed load or conversion has
// freed everything it took (a refused file allocates nothing beyond its
// header: it is probed before a set is made). Paths are UTF-8 and go through
// WavFile (wide-character paths on Windows).
//
// Levels. A file's full scale, +-1.0, is +-5 V on the tape (loadFile, x 5)
// and back again on export (/ 5, as 32-bit float, so a tape hotter than 5 V
// is not clipped). Note that x / 5 x 5 in float is not always x: an
// exported tape loaded again is within one unit in the last place.
//
// The patch's storage (storeTape, loadStored): in the folder Rack gives the
// module, tape.wav and original.wav, 32-bit float stereo in volts (not
// scaled, so they load back to the bit) at the set's rate, each length +
// tail frames; and tails.wav, one frame whose left side is the tape's tail
// in frames and the right the original's (so the join reads on into the
// same recorded tail after a reload). Going through WavFile, every file is
// written to a temporary name and renamed over the old.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

#include "Resample.hpp"
#include "WavFile.hpp"
#include "eddy/EddyEngine.hpp"

namespace rka {
namespace eddy {

constexpr float FILE_VOLTS = 5.f;   // a file's full scale on the tape
static const char* const STORED_TAPE = "tape.wav";
static const char* const STORED_ORIGINAL = "original.wav";
static const char* const STORED_TAILS = "tails.wav";

namespace jobs {

inline std::string join(const std::string& dir, const char* name) {
	if (dir.empty())
		return name;
	const char last = dir[dir.size() - 1];
	return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

// A new set at fs, empty, or null with the reason.
inline TapeSet* newSet(float fs, std::string& why) {
	TapeSet* s = new (std::nothrow) TapeSet;
	if (!s) {
		why = "not enough memory";
		return nullptr;
	}
	if (!s->allocate(fs)) {
		delete s;
		why = (fs >= 1.f && fs <= 4e6f) ? "not enough memory for a tape" : "unusable sample rate for a tape";
		return nullptr;
	}
	return s;
}

// The samples, length and tail of `from` (the copy contract).
inline void copyTape(Tape& to, const Tape& from) {
	const size_t n = from.length + from.tail;
	for (int c = 0; c < 2; c++)
		std::memcpy(to.ch[c], from.ch[c], n * sizeof(float));
	to.length = from.length;
	to.tail = from.tail;
}

// `from` converted by ratio (to's rate / from's) into `to`, held to to's
// capacity and tail capacity: the tape at its converted length, then as much
// of what followed it (its tail, or tape beyond the capacity) as the tail
// holds. Equal rates are a copy, to the bit. `cut`, if given, is set when
// the tape is held short by the capacity (a long tape at a high rate).
inline void convertTape(Tape& to, const Tape& from, double ratio, bool* cut = nullptr) {
	if (ratio == 1.0 && from.length <= to.capacity() && from.tail <= to.tailCapacity()) {
		copyTape(to, from);
		return;
	}
	const size_t in = from.length + from.tail;
	size_t len = (size_t) resampledFrames(from.length, ratio);
	if (len > to.capacity()) {
		len = to.capacity();
		if (cut)
			*cut = true;
	}
	const size_t total = (size_t) resampledFrames(in, ratio);
	size_t tail = total > len ? total - len : 0;
	if (tail > to.tailCapacity())
		tail = to.tailCapacity();
	for (int c = 0; c < 2; c++)
		resample(from.ch[c], in, to.ch[c], len + tail, ratio);
	to.length = len;
	to.tail = tail;
}

// Not-a-number and infinite samples read as 0 V.
inline void finiteOnly(Tape& t) {
	const size_t n = t.length + t.tail;
	for (int c = 0; c < 2; c++)
		for (size_t i = 0; i < n; i++)
			if (!(t.ch[c][i] - t.ch[c][i] == 0.f))
				t.ch[c][i] = 0.f;
}

inline bool sameRate(uint32_t fileRate, float fs) {
	return (double) fileRate == (double) fs;
}

// Reads a WAV of `info` (probed) into `into` at rate fs: the file's first
// `frames - tail` frames as the tape and the next `tail` as its tail, held
// to the capacity (frames beyond it become the tail), converted if the
// file's rate differs. Reading at another rate takes a temporary tape of the
// frames the conversion needs, never the whole of a long file. False, with
// `into` part written, only on a read error part way (the caller's set is
// new, and is freed). `cut`, if given, is set when the file's tape is longer
// than the capacity holds at fs.
inline bool readInto(Tape& into, const std::string& path, const WavInfo& info, uint64_t tail, float fs, std::string& why,
                     bool* cut = nullptr) {
	if (tail >= info.frames)
		tail = 0;
	uint64_t length = info.frames - tail;
	if (cut && (double) length * ((double) fs / (double) info.sampleRate) > (double) into.capacity() + 0.5)
		*cut = true;
	if (sameRate(info.sampleRate, fs)) {
		if (tail > into.tailCapacity())
			tail = into.tailCapacity();
		if (length > into.capacity()) {
			length = into.capacity();
			const uint64_t after = info.frames - length;
			tail = after < into.tailCapacity() ? after : into.tailCapacity();
		}
		WavInfo got;
		if (!readWav(path, into.ch[0], into.ch[1], length + tail, got, why))
			return false;
		into.length = (size_t) length;
		into.tail = (size_t) (got.frames - length);
		return true;
	}
	const double ratio = (double) fs / (double) info.sampleRate;
	// The input that fills the capacity and the tail, and the kernel's
	// reach beyond it.
	const double reach = (double) resampledetail::kZeroCrossings / (ratio < 1.0 ? ratio : 1.0) + 2.0;
	const double need = std::ceil((double) (into.capacity() + into.tailCapacity()) / ratio + reach);
	uint64_t n = info.frames;
	if ((double) n > need)
		n = (uint64_t) need;
	if (length > n) {
		length = n;
		tail = 0;
	}
	else if (length + tail > n)
		tail = n - length;
	Tape temp;
	if (!temp.allocate((size_t) n, 0)) {
		why = "not enough memory to convert the file's rate";
		return false;
	}
	WavInfo got;
	if (!readWav(path, temp.ch[0], temp.ch[1], n, got, why))
		return false;
	temp.length = (size_t) length;
	temp.tail = (size_t) tail;
	convertTape(into, temp, ratio);
	return true;
}

// The tape's and the original's tails from tails.wav; 0 and 0, with a note,
// if it is missing or unreadable.
inline void readTails(const std::string& dir, uint64_t& tape, uint64_t& original, std::string& note) {
	tape = original = 0;
	float l = 0.f, r = 0.f;
	WavInfo info;
	std::string why;
	if (!readWav(join(dir, STORED_TAILS), &l, &r, 1, info, why)) {
		note += std::string(note.empty() ? "" : "; ") + STORED_TAILS + " unreadable (" + why + "): no tails";
		return;
	}
	if (l >= 0.f && l < 16777216.f && l == std::floor(l))
		tape = (uint64_t) l;
	if (r >= 0.f && r < 16777216.f && r == std::floor(r))
		original = (uint64_t) r;
}

} // namespace jobs

// A new set at rate fs from a WAV file: read (the first capacity's worth),
// converted if the file's rate differs, x 5 (not-a-number and infinite
// samples as 0 V, every sample held to +-100 V, as the write path holds
// them), the original equal to the tape, the overview built. No tail.
// `cut`: as readInto.
inline bool loadFile(const std::string& path, float fs, TapeSet*& out, std::string& why, bool* cut = nullptr) {
	WavInfo info;
	std::string w;
	if (!probeWav(path, info, w)) {
		why = "refused: " + w;
		return false;
	}
	TapeSet* s = jobs::newSet(fs, why);
	if (!s)
		return false;
	if (!jobs::readInto(s->tape, path, info, 0, fs, w, cut)) {
		delete s;
		why = "refused: " + w;
		return false;
	}
	if (s->tape.length == 0) {
		delete s;
		why = "refused: too short to give one frame at this rate";
		return false;
	}
	s->tape.tail = 0;
	// Held to +-100 V before the x 5, so a float file's 1e38 cannot overflow
	// to infinity on the tape (the final review's M7).
	const float most = 100.f / FILE_VOLTS;
	for (int c = 0; c < 2; c++)
		for (size_t i = 0; i < s->tape.length; i++) {
			const float x = s->tape.ch[c][i];
			s->tape.ch[c][i] = !(x - x == 0.f) ? 0.f : x > most ? 100.f : x < -most ? -100.f : x * FILE_VOLTS;
		}
	jobs::copyTape(s->original, s->tape);
	s->overview.build(s->tape);
	out = s;
	return true;
}

// A new set at rate fs from the patch's storage. A lone tape.wav or
// original.wav (the other missing, or unreadable: cut short by a crash)
// serves as both. If the two differ in length (frames less tail) or rate, one is stale:
// the tape wins, as both. Files at another rate are converted. On success
// `why` is empty, or says what was made of a partial store (for the log).
//
// `whole` (the five-argument form): true when the set is exactly what the
// files hold, both read as they are at this rate, with no note; only then
// may the module take the files as holding its tape (StoreRecord). `cut`:
// the tape or the original is held short by the capacity at fs (the files
// hold more than the set: StoreRecord's `keep`).
inline bool loadStored(const std::string& dir, float fs, TapeSet*& out, std::string& why, bool& whole, bool* cut = nullptr) {
	const std::string tapePath = jobs::join(dir, STORED_TAPE), origPath = jobs::join(dir, STORED_ORIGINAL);
	WavInfo ti, oi;
	std::string tw, ow;
	const bool haveTape = probeWav(tapePath, ti, tw), haveOrig = probeWav(origPath, oi, ow);
	if (!haveTape && !haveOrig) {
		why = std::string("no stored tape (") + STORED_TAPE + ": " + tw + "; " + STORED_ORIGINAL + ": " + ow + ")";
		return false;
	}
	std::string note;
	uint64_t tapeTail = 0, origTail = 0;
	jobs::readTails(dir, tapeTail, origTail, note);
	TapeSet* s = jobs::newSet(fs, why);
	if (!s)
		return false;

	const bool pair = haveTape && haveOrig;
	bool isWhole = false;
	const bool match = pair && ti.sampleRate == oi.sampleRate && tapeTail < ti.frames && origTail < oi.frames
		&& ti.frames - tapeTail == oi.frames - origTail;
	bool tapeRead = false, origRead = false;
	std::string w;
	bool held = false;
	if (haveTape)
		tapeRead = jobs::readInto(s->tape, tapePath, ti, tapeTail, fs, w, &held);
	if (haveTape && !tapeRead)
		note += std::string(note.empty() ? "" : "; ") + STORED_TAPE + ": " + w;
	if (match && tapeRead) {
		origRead = jobs::readInto(s->original, origPath, oi, origTail, fs, w, &held);
		if (origRead && note.empty() && jobs::sameRate(ti.sampleRate, fs))
			isWhole = true;
		if (!origRead) {
			note += std::string(note.empty() ? "" : "; ") + STORED_ORIGINAL + ": " + w + ": the tape serves as both";
			jobs::copyTape(s->original, s->tape);
		}
	}
	else if (tapeRead) {
		if (pair)
			note += std::string(note.empty() ? "" : "; ") + "the tape and the original differ in length or rate: the tape serves as both";
		else
			note += std::string(note.empty() ? "" : "; ") + STORED_ORIGINAL + ": " + ow + ": the tape serves as both";
		jobs::copyTape(s->original, s->tape);
	}
	else if (haveOrig) {
		origRead = jobs::readInto(s->original, origPath, oi, origTail, fs, w, &held);
		if (!origRead) {
			delete s;
			why = std::string("the stored tape cannot be read (") + STORED_ORIGINAL + ": " + w + ")";
			return false;
		}
		note += std::string(note.empty() ? "" : "; ") + STORED_TAPE + ": " + (haveTape ? "unreadable" : tw) + ": the original serves as both";
		jobs::copyTape(s->tape, s->original);
	}
	else {
		delete s;
		why = std::string("the stored tape cannot be read (") + STORED_TAPE + ": " + w + "; " + STORED_ORIGINAL + ": " + ow + ")";
		return false;
	}
	jobs::finiteOnly(s->tape);
	jobs::finiteOnly(s->original);
	s->overview.build(s->tape);
	why = note;
	whole = isWhole && !held;
	if (cut)
		*cut = held;
	out = s;
	return true;
}
inline bool loadStored(const std::string& dir, float fs, TapeSet*& out, std::string& why) {
	bool whole = false;
	return loadStored(dir, fs, out, why, whole);
}

// Writes the set's tape (if `tape`) and original (if `original`) into the
// patch's storage folder `dir`, labelled at rate fs, and tails.wav if either
// is written. Nothing is written with both false. A flagged tape that is
// empty is refused (the module removes the folder for an empty tape).
//
// The set may be the engine's live set: its tape is read while the engine
// may write it (see the module's save).
namespace jobs {
// storeTape with the tape's file made from `tapeSource` (the set's tape, or
// its original when the tape is to become the original).
inline bool storeFrom(const std::string& dir, const Tape& tapeSource, const TapeSet& set, float fs, bool tape, bool original, std::string& why) {
	if (!tape && !original)
		return true;
	if ((tape && tapeSource.length == 0) || (original && set.original.length == 0)) {
		why = "the tape is empty";
		return false;
	}
	const double rate = std::floor((double) fs + 0.5);
	if (!(rate >= 1.0 && rate <= 4e6)) {
		why = "unusable sample rate";
		return false;
	}
	const uint32_t sr = (uint32_t) rate;
	bool ok = true;
	std::string w;
	if (tape && !writeWav(join(dir, STORED_TAPE), tapeSource.ch[0], tapeSource.ch[1], tapeSource.length + tapeSource.tail, sr, 1.f, w)) {
		why = std::string(STORED_TAPE) + ": " + w;
		ok = false;
	}
	if (original && ok
	    && !writeWav(join(dir, STORED_ORIGINAL), set.original.ch[0], set.original.ch[1], set.original.length + set.original.tail, sr, 1.f, w)) {
		why = std::string(STORED_ORIGINAL) + ": " + w;
		ok = false;
	}
	const float tails[2] = {(float) tapeSource.tail, (float) set.original.tail};
	if (ok && !writeWav(join(dir, STORED_TAILS), &tails[0], &tails[1], 1, sr, 1.f, w)) {
		why = std::string(STORED_TAILS) + ": " + w;
		ok = false;
	}
	return ok;
}
} // namespace jobs

inline bool storeTape(const std::string& dir, const TapeSet& set, float fs, bool tape, bool original, std::string& why) {
	return jobs::storeFrom(dir, set.tape, set, fs, tape, original, why);
}

// ---- What the patch's storage holds, and what a save writes.
//
// The module keeps a StoreRecord: a fact, not an inference. A file is
// "current" only when it is known to hold the live set's tape (or original)
// as of the change count recorded, at the rate recorded: because the module
// wrote it so, or because it loaded the set whole from it (loadStored's
// `whole`). Anything else (a converted or partial load, a removal, a failed
// write) leaves the files not current, and the next save writes both.
//
// Two more facts, each cleared by the next write or removal:
// - held: the folder holds a stored tape that failed to load (a passing
//   failure, such as memory, should not cost it): while the tape is empty
//   the save leaves the folder alone (the final review's M6).
// - keep: the stored tape loaded held short by the tape's capacity (a long
//   tape opened at a high rate): the folder holds more than the live set, so
//   while the tape is unchanged since (the counters recorded, at the rate
//   recorded) the save leaves the folder alone (M3).
struct StoreRecord {
	bool tapeCurrent = false, originalCurrent = false;
	uint32_t tapeChanges = 0, originalChanges = 0;
	float fs = 0.f;
	bool held = false, keep = false;
};

// What the save knows at the moment it is made.
struct SaveFacts {
	bool keepTape = true;
	bool loading = false;        // the patch's stored tape is still being loaded (not yet live)
	bool haveSet = false;        // a live set (none: between rates)
	bool known = false;          // the counters and readsOriginal below are the live set's
	bool readsOriginal = false;  // the tape is to become the original (a recording's copy, a Restore)
	size_t tapeLength = 0, originalLength = 0;
	uint32_t tapeChanges = 0, originalChanges = 0;
	float fs = 0.f;              // the live set's rate
};

struct SavePlan {
	bool remove = false;            // empty the folder
	bool tape = false, original = false;
	bool tapeFromOriginal = false;  // tape.wav made from the original
};

// The save's decision. Nothing while the stored tape loads or with no live
// set (what is stored stays); the folder removed with "Keep the tape" off or
// an empty tape (left alone if the record holds a tape that failed to load);
// nothing while the record keeps a longer stored tape and the tape is
// unchanged; otherwise each file written unless the record says it already
// holds this, at this rate. While the tape is to become the original,
// tape.wav is written from the original (so a new recording is never
// stored beside the old tape).
inline SavePlan planSave(const SaveFacts& f, const StoreRecord& r) {
	SavePlan p;
	if (!f.keepTape) {
		p.remove = true;
		return p;
	}
	if (f.loading || !f.haveSet)
		return p;
	const size_t tapeLength = f.readsOriginal ? f.originalLength : f.tapeLength;
	if (tapeLength == 0 || f.originalLength == 0) {
		p.remove = !r.held;
		return p;
	}
	const bool sameRate = r.fs == f.fs;
	if (r.keep && f.known && sameRate && !f.readsOriginal && r.tapeChanges == f.tapeChanges && r.originalChanges == f.originalChanges)
		return p;
	p.tapeFromOriginal = f.readsOriginal;
	p.tape = f.readsOriginal || !f.known || !(r.tapeCurrent && sameRate && r.tapeChanges == f.tapeChanges);
	p.original = !f.known || !(r.originalCurrent && sameRate && r.originalChanges == f.originalChanges);
	return p;
}

// The record after a save made by `p`: `ok` whether the files were written.
inline void savedAs(StoreRecord& r, const SaveFacts& f, const SavePlan& p, bool ok) {
	if (p.remove || !ok) {
		r = StoreRecord();
		return;
	}
	if (!p.tape && !p.original)
		return;
	if (r.fs != f.fs || !f.known)
		r = StoreRecord();
	r.held = r.keep = false;
	r.fs = f.fs;
	if (p.tape) {
		// From the original: the tape is not yet the original, so its file
		// is written again once the copy has moved the tape's counter.
		r.tapeCurrent = f.known && !p.tapeFromOriginal;
		r.tapeChanges = f.tapeChanges;
	}
	if (p.original) {
		r.originalCurrent = f.known;
		r.originalChanges = f.originalChanges;
	}
}

// The record after the set loaded whole from the files went live, with the
// counters at that moment, at rate fs.
inline void loadedWhole(StoreRecord& r, uint32_t tapeChanges, uint32_t originalChanges, float fs) {
	r = StoreRecord();
	r.tapeCurrent = r.originalCurrent = true;
	r.tapeChanges = tapeChanges;
	r.originalChanges = originalChanges;
	r.fs = fs;
}

// The record after the set loaded from the files held short by the capacity
// went live, with the counters then: the files are not current (they hold
// more), and are kept while the tape is unchanged.
inline void loadedCut(StoreRecord& r, uint32_t tapeChanges, uint32_t originalChanges, float fs) {
	r = StoreRecord();
	r.keep = true;
	r.tapeChanges = tapeChanges;
	r.originalChanges = originalChanges;
	r.fs = fs;
}

// The record after the stored tape failed to load from the folder as it is.
inline void loadFailed(StoreRecord& r) {
	r = StoreRecord();
	r.held = true;
}

// Carries out a plan's writes (not its removal: the module removes the
// folder through Rack). True if there was nothing to write.
inline bool storePlanned(const std::string& dir, const TapeSet& set, float fs, const SavePlan& p, std::string& why) {
	return jobs::storeFrom(dir, p.tapeFromOriginal ? set.original : set.tape, set, fs, p.tape, p.original, why);
}

// The whole tape (not its tail) as a 32-bit float stereo WAV at rate fs, / 5.
inline bool exportTape(const std::string& path, const TapeSet& set, float fs, std::string& why) {
	if (set.tape.length == 0) {
		why = "the tape is empty";
		return false;
	}
	const double rate = std::floor((double) fs + 0.5);
	if (!(rate >= 1.0 && rate <= 4e6)) {
		why = "unusable sample rate";
		return false;
	}
	return writeWav(path, set.tape.ch[0], set.tape.ch[1], set.tape.length, (uint32_t) rate, 1.f / FILE_VOLTS, why);
}

// A new set at toFs from `from` (at fromFs): the tape and the original each
// converted from their own (so they stay different), lengths and tails with
// them, the overview built. A stale set's tape is made from its original.
// `cut`: either is held short by the capacity at toFs.
inline bool convert(const TapeSet& from, float fromFs, float toFs, TapeSet*& out, std::string& why, bool* cut = nullptr) {
	if (!(fromFs >= 1.f && fromFs <= 4e6f)) {
		why = "unusable sample rate";
		return false;
	}
	TapeSet* s = jobs::newSet(toFs, why);
	if (!s)
		return false;
	const double ratio = (double) toFs / (double) fromFs;
	jobs::convertTape(s->tape, from.tapeStale ? from.original : from.tape, ratio, cut);
	jobs::convertTape(s->original, from.original, ratio, cut);
	s->overview.build(s->tape);
	out = s;
	return true;
}

} // namespace eddy
} // namespace rka

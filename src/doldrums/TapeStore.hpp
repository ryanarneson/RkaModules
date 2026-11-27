#pragma once
// Doldrums' frozen tape kept with the patch (the design's "With the patch";
// the plan's "Menu and saved state"): tape.wav in the patch's storage folder,
// 32-bit float stereo in volts (not scaled, so it reads back to the bit) at
// Rack's rate, the tape's frames oldest first, `capacity` of them; and the
// jobs the module's worker thread does with whole tapes: write it, read it,
// convert a frozen tape to a new rate. Rack-free, C++11, after Spindrift's
// BufferFile.hpp / BufferSave.hpp and Eddy's TapeJobs.hpp. None of it is for
// the audio thread: it allocates, reads and writes files, and converts
// (Resample.hpp). The engine's side of the hand-over is DoldrumsEngine.hpp's
// "Tapes handed in".
//
// When the file is written (savePlan). First: while a tape is on its way
// (SaveFacts::tapePending, the engine's tapePending(): the stored tape being
// read, or a frozen tape being converted after a rate change, asked for and
// not yet taken, refused or given up), a save leaves the file exactly as it
// is: no write, no removal, whatever the button and Keep say. The live tape
// is then only a silent stand-in, and the file may be the one copy of the
// tape (Rack autosaves by itself, and on its Auto rate the rate settles just
// after a patch opens, so such a save is ordinary). Once settled, the
// ordinary rules: written only when the Freeze BUTTON is on (a tape frozen
// by the gate alone is not kept: the button is what the patch saves), "Keep
// the frozen tape" is on, and it has not been written since it froze
// (SaveFacts::frozenSince is the engine's frozenSince(), writtenFor the
// number it had when the file was last written, 0: never); removed if the
// button is off or Keep is off. A tape handed in (the stored one read, or
// one converted at a rate change) keeps its number, so a reopened patch
// does not write its file again until the tape is frozen anew (the file
// read at another rate is then still at its first rate, which readTape
// converts).
//
// A load that fails (a bad file): the worker gives up (engine.giveUp), which
// settles the engine on its silent stand-in, frozen if the button is on.
// Until then the file is left alone, as above. After it the ordinary rules
// apply to a tape that did not come from the file: with the button still on
// the number is the one the file was given at open, so nothing is written
// and the bad file stays (it is not replaced by silence, and can still be
// mended by hand or read by a later build); thawed, or with Keep off, it is
// removed; frozen anew, the new tape is written over it.
//
// A whole tape or none. writeTape writes whatever ring it is given; the live
// ring is saved through saveFrozen, which holds it still (the engine's
// holdStill: granted only while it is frozen and faded out with no tape on
// its way, and nothing writes it while held: a thaw meanwhile waits for
// letGo) just for the copy of its frames, then writes the copy. WavFile
// writes a temporary file and renames it over the old, so the folder never
// holds half a tape. If the hold is not granted (not frozen, the freeze's
// 5 ms fade-out still running after a short wait, or a tape on its way: the
// live ring is then a silent stand-in) nothing is written and saveFrozen
// returns false: saveStep then has the module remove the old file (it is of
// an earlier freeze; writtenFor 0, so the next save tries again), unless a
// tape is on its way by then, when the file stays as it is.
// The hold makes the copy race-free (no torn frames to detect after the
// fact, as Spindrift's seqlock must), so test_threads checks it under
// ThreadSanitizer.
//
// Reading (readTape): any WAV that WavFile reads, at a rate a tape is made at
// (8 kHz to 768 kHz), at most two tapes long (60 s at its own rate: more is
// not a Doldrums tape, and would be read whole). Converted to the rate asked
// for if its own differs; then the newest `capacity` frames kept if it is
// longer, and silence put at the old end if it is shorter. Mono reads as
// both sides. NaN and inf read as 0 V and samples are held to +-100 V (as
// Eddy's loads are), so a hand-made file cannot put a sample on the tape
// that the input could not. A missing, empty, truncated, foreign or
// absurdly large file is refused: false, `out` empty, `why` set; the module
// then opens with a silent tape and no error, as the design says.
//
// Converting (convertTape): the tape oldest to newest at its rate, resampled
// (Resample.hpp: windowed sinc, its cutoff lowered going down), the newest
// frames kept or the old end padded as above. Equal rates are a copy, to the
// bit. The tape is always read by absolute index (Ring::physical), oldest
// first, never across the write point (Ring.hpp: that junction is not
// smoothed). Every ring made here arrives frozen and faded out (Ring::fill).
//
// What the module (Task 12) does: every call, in order, and its thread. The
// hold (holdStill) is taken only on the worker and only for the copy of a
// tape's frames (saveFrozen does that); reset, allocate and setSampleRate
// wait while it is held, so they must never be called on the thread that
// holds it. Rings come back through retired() into eight slots; the worker
// collects them every time it wakes (2a), so that allocate always has the
// two free slots it needs to take a new rate.
//
// 1. A patch opens with a stored tape (dataFromJson / onAdd says the Freeze
//    button is on and Keep is on). UI thread: engine.expectStored();
//    writtenFor = engine.frozenSince() (the file holds the tape that is on
//    its way); wake the worker. Worker: 2a, 2b (its job's `from` is null:
//    readTape of the folder), 2c; then the audio thread 2d. From
//    expectStored until the tape is taken, refused or given up,
//    tapePending() is true and every save leaves the file alone (3).
// 2. The worker, whenever it wakes (and every few ms while a job is
//    possible):
//    a. while (Ring* r = engine.retired()) delete r;   only the worker frees
//       rings; never a ring it is still reading (a conversion's `from`),
//       which is safe here because it collects only between jobs.
//    b. if engine.tapeWanted(job) and job.ticket is not the last one made:
//       Ring* r = makeWanted(job, dir, why) (the file read, or job.from
//       converted); remember the ticket; if !r: engine.giveUp(job.ticket)
//       and log why (a failed load: see above; the patch opens with a silent
//       tape and no error); if r: build the spare view from it (c), then
//       engine.offer(r, job.ticket), retrying a few ms later while it
//       returns false. A ring the engine refuses (its ticket no longer
//       wanted, or a rate that has changed) comes back through retired(); a
//       new ticket says what to make instead.
//    c. The window. The module keeps two TapeViews: the audio thread feeds
//       one (wrote, publish) and the UI reads the one an atomic index names.
//       Before offering r, the worker rebuilds the other one from r
//       (TapeView::rebuild): r is the worker's alone then, so no hold is
//       needed and the audio thread never pauses its picture; the offer
//       (release) hands the rebuilt view over with the ring. Then the
//       worker leaves that view alone until the offer is settled: taken
//       (engine.tapesTaken() has passed the count it read before offering)
//       or refused (r came back through retired() with the count
//       unchanged).
//    d. The audio thread, after each engine.process(): if tapesTaken() has
//       changed, the ring taken is the one the spare was built for (the
//       worker offers one at a time): feed the spare from now on, store its
//       index for the UI (release), and store the count it has switched at
//       (release); the worker acquires that before it touches the other
//       view again. A new ring from allocate (a rate change) is a new ring
//       as TapeView says: view.configure on the one fed.
//    Why rebuild before offering rather than after the swap with the audio
//    thread pausing its view: the fresh ring is reachable by no other
//    thread until offered, so building from it needs no hold of the live
//    ring and cannot meet a thaw, a reset or a rate change; and the picture
//    changes on the very sample the tape does.
// 3. A save (onSave asks; the worker runs it, the only thread that takes
//    the hold, while onSave waits for it, and never at the same time as
//    reset or onSampleRateChange): f = {keepTape, button,
//    engine.frozenSince(), writtenFor, engine.tapePending()}; then
//    saveStep(f, dir, engine, rate, writtenFor, why), which follows
//    savePlan(f) (SAVE_WRITE: saveFrozen, writtenFor = the number written)
//    and says what is left for the module: SAVE_REMOVE: remove tape.wav
//    (Rack's system::remove), writtenFor is 0; otherwise nothing. A write
//    refused because a tape went on its way, or was handed in, meanwhile
//    leaves the file too.
// 4. A rate change with a frozen tape. UI thread (onSampleRateChange, Rack
//    holding the engine's lock): engine.allocate(rate),
//    engine.setSampleRate(rate), wake the worker. The frozen tape's ring is
//    now the source of a job (2b, `from` set) at the new rate; tapePending()
//    is true until its conversion is taken (or a sample not frozen or a
//    reset ends the wait), so saves leave the file alone meanwhile. Another rate change
//    before it lands asks for it again (a new ticket), from the same source.
// 5. A failed load: the worker's 2b, makeWanted returning null, then
//    engine.giveUp(job.ticket) on the worker. The engine settles on its
//    silent stand-in; saves from then on follow the ordinary rules (above).
// 6. Initialize. UI thread: engine.reset() (nothing on its way is wanted
//    then).
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include "DoldrumsEngine.hpp"
#include "Laws.hpp"
#include "Resample.hpp"
#include "Ring.hpp"
#include "WavFile.hpp"

namespace rka { namespace doldrums {

constexpr const char* STORED_TAPE = "tape.wav";

// What the save knows. frozenSince: DoldrumsEngine::frozenSince(); writtenFor:
// its value when the file was last written, 0 if it was not (counters, not
// times); tapePending: DoldrumsEngine::tapePending(), a tape on its way.
struct SaveFacts { bool keepTape, freezeButton; uint32_t frozenSince, writtenFor; bool tapePending; };
enum SaveAction { SAVE_NOTHING, SAVE_WRITE, SAVE_REMOVE };

inline SaveAction savePlan(const SaveFacts& f) {
	if (f.tapePending) return SAVE_NOTHING;   // the file may be the tape's one copy: as it is
	if (!f.keepTape || !f.freezeButton) return SAVE_REMOVE;
	return f.writtenFor == f.frozenSince ? SAVE_NOTHING : SAVE_WRITE;
}

namespace store {

constexpr float kMostVolts = 100.f;    // a sample read from a file is held to this
constexpr double kMostTapes = 2.0;     // a file longer than this many tapes is refused

inline std::string join(const std::string& dir, const char* name) {
	if (dir.empty()) return name;
	const char last = dir[dir.size() - 1];
	return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

inline bool usableRate(double rate) { return rate >= 8000.0 && rate <= 768000.0; }

// The ring's frames oldest first, `capacity` of them, by absolute index:
// [now - capacity, now) in two pieces either side of the end of the store.
inline void image(const Ring& ring, float* l, float* r) {
	const long long cap = ring.capacity();
	const long long first = ring.physical(ring.now() - cap);
	const long long head = cap - first;
	const float* ch[2] = {ring.channel(0), ring.channel(1)};
	float* out[2] = {l, r};
	for (int c = 0; c < 2; c++) {
		std::memcpy(out[c], ch[c] + first, size_t(head) * sizeof(float));
		if (head < cap) std::memcpy(out[c] + head, ch[c], size_t(cap - head) * sizeof(float));
	}
}

// n frames into `cap`: the newest cap if n is more, else silence at the old end.
inline void fit(const float* in, uint64_t n, float* out, long long cap) {
	const uint64_t c = uint64_t(cap);
	if (n >= c)
		std::memcpy(out, in + (n - c), size_t(c) * sizeof(float));
	else {
		std::memset(out, 0, size_t(c - n) * sizeof(float));
		if (n > 0)   // an empty input's data() may be null, which memcpy may not be given
			std::memcpy(out + (c - n), in, size_t(n) * sizeof(float));
	}
}

// in (n frames at fromRate) into out (capacity frames at toRate): converted if
// the rates differ, then fitted (above). Allocates.
inline void place(const std::vector<float>& in, double fromRate, double toRate, std::vector<float>& out, long long cap) {
	out.assign(size_t(cap), 0.f);
	if (fromRate == toRate) {
		fit(in.data(), in.size(), out.data(), cap);
		return;
	}
	const double ratio = toRate / fromRate;
	std::vector<float> conv(size_t(resampledFrames(in.size(), ratio)));
	resample(in.data(), in.size(), conv.data(), conv.size(), ratio);
	fit(conv.data(), conv.size(), out.data(), cap);
}

inline void writeError(std::string& why, const std::string& w) { why = std::string(STORED_TAPE) + ": " + w; }

// The two sides oldest first, written as tape.wav in dir at rate.
inline bool writeImage(const std::string& dir, const float* l, const float* r, long long frames, float rate, std::string& why) {
	std::string w;
	if (!writeWav(join(dir, STORED_TAPE), l, r, uint64_t(frames), uint32_t(std::floor(double(rate) + 0.5)), 1.f, w)) {
		writeError(why, w);
		return false;
	}
	return true;
}

} // namespace store

// Written oldest frame first, capacity frames, float stereo in volts at `rate`.
inline bool writeTape(const std::string& dir, const Ring& ring, float rate, std::string& why) {
	if (ring.capacity() < 1) {
		why = "no tape to write";
		return false;
	}
	if (!store::usableRate(rate)) {
		why = "unusable sample rate";
		return false;
	}
	try {
		std::vector<float> l(size_t(ring.capacity())), r(l.size());
		store::image(ring, l.data(), r.data());
		return store::writeImage(dir, l.data(), r.data(), ring.capacity(), rate, why);
	} catch (const std::bad_alloc&) {
		why = "not enough memory to write the tape";
		return false;
	}
}

// Reads into a freshly allocated ring for `rate`, converting with Resample.hpp when the file's rate differs,
// padding with silence at the old end or keeping the newest frames when the length differs. False leaves `out` empty.
// On success `why` is empty, or says what was made of the file (for the log).
inline bool readTape(const std::string& dir, float rate, Ring& out, std::string& why) {
	out.release();
	if (!store::usableRate(rate)) {
		why = "unusable sample rate";
		return false;
	}
	const std::string path = store::join(dir, STORED_TAPE);
	WavInfo info;
	std::string w;
	if (!probeWav(path, info, w)) {
		store::writeError(why, w);
		return false;
	}
	if (!store::usableRate(info.sampleRate)) {
		store::writeError(why, "a sample rate (" + std::to_string(info.sampleRate) + " Hz) no tape is made at");
		return false;
	}
	const long long fileTape = tapeFrames(float(info.sampleRate));
	if (double(info.frames) > store::kMostTapes * double(fileTape)) {
		store::writeError(why, "longer than two tapes: not a Doldrums tape");
		return false;
	}
	const long long cap = tapeFrames(rate);
	try {
		std::vector<float> l(size_t(info.frames)), r(l.size());
		WavInfo got;
		if (!readWav(path, l.data(), r.data(), info.frames, got, w) || got.frames != info.frames) {
			store::writeError(why, w.empty() ? std::string("read short") : w);
			return false;
		}
		for (size_t i = 0; i < l.size(); i++) {
			float* x[2] = {&l[i], &r[i]};
			for (float* v : x)
				*v = !(*v - *v == 0.f) ? 0.f : *v > store::kMostVolts ? store::kMostVolts : *v < -store::kMostVolts ? -store::kMostVolts : *v;
		}
		std::vector<float> tl, tr;
		store::place(l, double(info.sampleRate), double(rate), tl, cap);
		store::place(r, double(info.sampleRate), double(rate), tr, cap);
		std::vector<float>().swap(l);
		std::vector<float>().swap(r);
		if (!out.allocate(cap)) {
			why = "no tape at this rate";
			return false;
		}
		out.fill(tl.data(), tr.data());
	} catch (const std::bad_alloc&) {
		out.release();
		why = "not enough memory to read the tape";
		return false;
	}
	why.clear();
	if (double(info.sampleRate) != double(rate)) why = "converted from " + std::to_string(info.sampleRate) + " Hz";
	const double have = double(info.frames) * double(rate) / double(info.sampleRate);
	if (have < double(cap) - 0.5 || have > double(cap) + 0.5)
		why += std::string(why.empty() ? "" : "; ") + (have < double(cap) ? "shorter than a tape: silence at the old end" : "longer than a tape: the newest kept");
	return true;
}

// A frozen ring at one rate into a new ring at another (a rate change).
inline bool convertTape(const Ring& from, float fromRate, float toRate, Ring& out, std::string& why) {
	out.release();
	if (!store::usableRate(fromRate) || !store::usableRate(toRate)) {
		why = "unusable sample rate";
		return false;
	}
	if (from.capacity() < 1) {
		why = "no tape to convert";
		return false;
	}
	const long long cap = tapeFrames(toRate);
	try {
		std::vector<float> l(size_t(from.capacity())), r(l.size());
		store::image(from, l.data(), r.data());
		std::vector<float> tl, tr;
		store::place(l, double(fromRate), double(toRate), tl, cap);
		store::place(r, double(fromRate), double(toRate), tr, cap);
		std::vector<float>().swap(l);
		std::vector<float>().swap(r);
		if (!out.allocate(cap)) {
			why = "no tape at this rate";
			return false;
		}
		out.fill(tl.data(), tr.data());
	} catch (const std::bad_alloc&) {
		out.release();
		why = "not enough memory to convert the tape";
		return false;
	}
	return true;
}

// The worker: the tape a job wants (DoldrumsEngine::tapeWanted), a new ring
// the caller owns (to offer, or delete), or null with why.
inline Ring* makeWanted(const DoldrumsEngine::TapeJob& job, const std::string& dir, std::string& why) {
	Ring* r = new (std::nothrow) Ring;
	if (!r) {
		why = "not enough memory";
		return nullptr;
	}
	const bool ok = job.from ? convertTape(*job.from, job.fromRate, job.toRate, *r, why) : readTape(dir, job.toRate, *r, why);
	if (!ok) {
		delete r;
		return nullptr;
	}
	return r;
}

// The save of the live tape (above: "A whole tape or none"): held still for
// the copy, then written. True: tape.wav holds the tape numbered `since`.
// False: nothing written (why says why); saveStep says what becomes of the old file.
// The hold is tried for up to `waitMs` (a freeze's fade-out is 5 ms).
// Nothing is written either if a tape was taken while the save was under way
// (tapesTaken() read before the hold and again after letGo differ): the
// number read under the hold could then be the new tape's (a tape handed in
// keeps the number) while the frames copied are the outgoing ring's, and
// the file must never hold one tape under another's number. The take after
// the first read and before the hold, or during the copy, is the window this
// closes; a take before the first read is seen by the hold itself (the
// engine publishes the new ring before it clears the wait).
// `duringCopy` is called with the ring held, after the copy (a seam for the
// tests, which make a take land there; the module passes nothing).
namespace store {
struct NoHook {
	void operator()() const {}
};
} // namespace store

template <class Hook>
inline bool saveFrozen(const std::string& dir, DoldrumsEngine& engine, float rate, uint32_t& since, std::string& why, int waitMs,
                       const Hook& duringCopy) {
	if (!store::usableRate(rate)) {
		why = "unusable sample rate";
		return false;
	}
	std::vector<float> l, r;
	try {
		const Ring* live = engine.live();
		const long long cap = live ? live->capacity() : 0;
		l.resize(size_t(cap));
		r.resize(size_t(cap));
		const uint32_t takenBefore = engine.tapesTaken();
		uint32_t at = 0;
		const Ring* held = engine.holdStill(&at);
		for (int i = 0; !held && i < waitMs; i++) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			held = engine.holdStill(&at);
		}
		if (!held) {
			why = "the tape is not standing still (not frozen, still fading, or a tape on its way): not written";
			return false;
		}
		if (held->capacity() != (long long) l.size()) {
			// A new ring since (a rate change): sized under the hold, once.
			try {
				l.resize(size_t(held->capacity()));
				r.resize(size_t(held->capacity()));
			} catch (...) {
				engine.letGo();
				throw;
			}
		}
		if (held->capacity() < 1) {
			engine.letGo();
			why = "no tape to write";
			return false;
		}
		store::image(*held, l.data(), r.data());
		duringCopy();
		engine.letGo();
		if (engine.tapesTaken() != takenBefore) {
			why = "a tape was handed in while saving: not written";
			return false;
		}
		if (!store::writeImage(dir, l.data(), r.data(), (long long) l.size(), rate, why)) return false;
		since = at;
		return true;
	} catch (const std::bad_alloc&) {
		why = "not enough memory to write the tape";
		return false;
	}
}
inline bool saveFrozen(const std::string& dir, DoldrumsEngine& engine, float rate, uint32_t& since, std::string& why, int waitMs = 25) {
	return saveFrozen(dir, engine, rate, since, why, waitMs, store::NoHook());
}

// The save, as the module makes it (3 above): savePlan carried out but for
// the removal, which is the module's (through Rack). Returns SAVE_WRITE when
// tape.wav was written (writtenFor is then its number), SAVE_REMOVE when the
// module is to remove tape.wav (writtenFor is then 0: by the plan, or a
// write that could not be made of a tape that is not still), SAVE_NOTHING
// when the file is to stay exactly as it is (the plan's nothing, or a tape
// that went on its way, or was handed in, between the plan and the write).
template <class Hook>
inline SaveAction saveStep(const SaveFacts& f, const std::string& dir, DoldrumsEngine& engine, float rate, uint32_t& writtenFor,
                           std::string& why, int waitMs, const Hook& duringCopy) {
	switch (savePlan(f)) {
	case SAVE_NOTHING:
		return SAVE_NOTHING;
	case SAVE_REMOVE:
		writtenFor = 0;
		return SAVE_REMOVE;
	case SAVE_WRITE:
		break;
	}
	const uint32_t takenBefore = engine.tapesTaken();
	uint32_t since = 0;
	if (saveFrozen(dir, engine, rate, since, why, waitMs, duringCopy)) {
		writtenFor = since;
		return SAVE_WRITE;
	}
	// A tape on its way, or handed in, since the plan: the file as it is (it
	// may hold that tape); the next save decides again.
	if (engine.tapePending() || engine.tapesTaken() != takenBefore) return SAVE_NOTHING;
	writtenFor = 0;
	return SAVE_REMOVE;
}
inline SaveAction saveStep(const SaveFacts& f, const std::string& dir, DoldrumsEngine& engine, float rate, uint32_t& writtenFor,
                           std::string& why, int waitMs = 25) {
	return saveStep(f, dir, engine, rate, writtenFor, why, waitMs, store::NoHook());
}

}} // namespace rka::doldrums

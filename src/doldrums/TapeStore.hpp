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
// When the file is written (savePlan). Only when the Freeze BUTTON is on (a
// tape frozen by the gate alone is not kept: the button is what the patch
// saves), "Keep the frozen tape" is on, and it has not been written since it
// froze: SaveFacts::frozenSince is the engine's frozenSince(), writtenFor the
// number it had when the file was last written (0: never). Otherwise, button
// off or Keep off, the file is removed. A tape handed in (the stored one
// read, or one converted at a rate change) keeps its number, so a reopened
// patch does not write its file again until the tape is frozen anew (the
// file read at another rate is then still at its first rate, which readTape
// converts).
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
// returns false: the module removes the old file then, since it is of an
// earlier freeze, and leaves writtenFor alone, so the next save tries again.
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
// What the module (Task 12) does, in order:
// 1. Patch opens with a stored tape (dataFromJson / onAdd says the Freeze
//    button is on and Keep is on): engine.expectStored(); writtenFor =
//    engine.frozenSince() (the file holds the tape that is on its way).
// 2. The worker, whenever it wakes (and every few ms while a job is
//    possible):
//    a. while (Ring* r = engine.retired()) delete r;   only the worker frees
//       rings; never a ring it is still reading (a conversion's `from`),
//       which is safe here because it collects only between jobs.
//    b. if engine.tapeWanted(job) and job.ticket is not the last one made:
//       Ring* r = makeWanted(job, dir, why) (the file read, or job.from
//       converted); remember the ticket; if r: build the spare view from it
//       (c), then engine.offer(r, job.ticket), retrying a few ms later while
//       it returns false. A ring the engine refuses (its ticket no longer
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
// 3. Save (onSave; the worker or the UI thread, but not while the module
//    runs reset or onSampleRateChange on another thread, which wait for a
//    hold anyway): f = {keepTape, button, engine.frozenSince(), writtenFor};
//    savePlan(f): SAVE_WRITE: saveFrozen(dir, engine, rate, since, why):
//    true: writtenFor = since; false: remove tape.wav. SAVE_REMOVE: remove
//    it, writtenFor = 0. SAVE_NOTHING: nothing.
// 4. Rate change (onSampleRateChange, Rack holding the engine's lock):
//    engine.allocate(rate), engine.setSampleRate(rate), wake the worker. A
//    frozen tape's ring is now the source of a job (2b) at the new rate.
// 5. Initialize: engine.reset() (nothing on its way is wanted then).
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
// its value when the file was last written, 0 if it was not (counters, not times).
struct SaveFacts { bool keepTape, freezeButton; uint32_t frozenSince, writtenFor; };
enum SaveAction { SAVE_NOTHING, SAVE_WRITE, SAVE_REMOVE };

inline SaveAction savePlan(const SaveFacts& f) {
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
// False: nothing written (why says why); the module removes the old file.
// The hold is tried for up to `waitMs` (a freeze's fade-out is 5 ms).
inline bool saveFrozen(const std::string& dir, DoldrumsEngine& engine, float rate, uint32_t& since, std::string& why, int waitMs = 25) {
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
		engine.letGo();
		if (!store::writeImage(dir, l.data(), r.data(), (long long) l.size(), rate, why)) return false;
		since = at;
		return true;
	} catch (const std::bad_alloc&) {
		why = "not enough memory to write the tape";
		return false;
	}
}

}} // namespace rka::doldrums

#pragma once
// Eddy's engine (docs/specs/2026-11-21-eddy-design.md), Rack-free, C++11,
// header-only. Eddy.cpp only maps params and ports onto it and runs the
// worker thread that makes, fills, copies and frees tapes.
//
// The engine owns no tape. A TapeSet (the tape, the original kept beside it,
// and the overview the window draws) is made off the audio thread, handed in
// by offer() and handed back by collect(), each through atomics; process()
// never allocates, frees, locks or copies a tape in bulk. The two bulk
// copies (original to tape for Restore and at a recording's end, tape to
// original for "Make this the original") are the worker's: the engine says
// which it wants (copyWanted) only once nothing it does touches the tape
// being written, the worker copies on live() and calls copyDone().
//
// Each sample:
//   transport edges (Rec, Dub, Play, Restore: buttons toggle, the REC and
//   DUB gates act on their edges, the PLAY and RESTORE jacks on rising ones);
//   every CONTROL_SAMPLES the controls (laws, window, wear, mix);
//   the speed: the knob plus CV glides, times 2^(V/OCT), ramped over 2 ms;
//   recording, always at speed 1 into the original;
//   the voices read and are summed (Voices.hpp), then move by the speed; the
//   newest one crossing the window's turn starts a new one at the window's
//   start (the join) and gives EOC, or with Loop off fades and stops;
//   every voice writes what it passed last sample (its own TapeWriter and
//   wear state): wear, then keep x old + add x input, then the WriteGuard,
//   at the strength of that voice's own gain (and the Dub, Stopped, Play
//   and hold fades), linear on the tape;
//   the overview is touched where the tape was written and serviced;
//   Mix, and the soft limit.
//
// Decisions the spec leaves to the builder:
// - Each voice writes what it plays, for as long as it sounds, at the
//   strength of its own fade gain g (the controller's ruling, replacing "the
//   write head follows the newest voice"): at g = 1 as written above; as g
//   falls the write fades to "tape unchanged". So a frame heard at gain g
//   was written at weight g, the join's read-on frames past the window's
//   end (the tail, or tape beyond a short window) and the whole overlap
//   included, and a dub laid down by two crossfading voices is heard once at
//   unity next turn (g_old^2 + g_new^2 = 1). Dub and Wear reach the window
//   and its read-on, nothing beyond. A new voice has a fresh writer and wear
//   state (its dropouts are its own); one DC blocker serves every voice. In
//   the first turn after a voice comes in by a time fade (Play, a retrigger,
//   a window move) it writes at full weight frames that later turns read
//   through the join's fade, so a dub made in that turn is heard through the
//   next join within about 2 dB of its level, then exactly.
// - Recording: the first REC_MIN_S goes into the engine's own buffer, so a
//   recording thrown away has touched nothing; once it stands it goes into
//   the original from frame 0 and the buffer is copied in behind it,
//   PRE_COPY_FRAMES a sample. At the end, m more of the input is taken as
//   the tail, the tape plays from the original at once (from Start), and the
//   engine asks for the original to be copied to the tape.
// - While a copy is wanted or under way nothing writes the tape (Dub and
//   Wear are faded out on it, F) and the voices read whichever of the two
//   the copy does not write. A Restore or "Make this the original" that
//   arrives while another copy is under way waits behind it (the later of
//   two such requests replaces the earlier: one waits at most).
// - Requests that cannot be met at once wait for their moment rather than
//   being dropped: Rec waits for a copy under way, a new set for a copy
//   under way and for the last set's voices to fade, Clear likewise. Rec is
//   dropped (not held) with no set at the engine's rate, as are Dub and
//   Restore on an empty tape.
// - setSampleRate retires the set only if its rate differs; clear() empties
//   the set in place (both tape and original, so both change counters move).
//
// Threads: process() and activeVoices() are the audio thread's; the rest
// may be called from any one other thread (the worker, or the UI).
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "FastMath.hpp"
#include "Overview.hpp"
#include "Tape.hpp"
#include "eddy/EddyTuning.hpp"
#include "eddy/Laws.hpp"
#include "eddy/Voices.hpp"
#include "eddy/Wear.hpp"

namespace rka {

// A tape, its original and its overview. Made and freed off the audio thread.
struct TapeSet {
	Tape tape, original;
	Overview overview;
	float fs = 0.f;   // the rate it was made for: the engine plays only a set at its own
	// True: the tape is out of date and must be copied from the original
	// before the set is used or converted (the original is the newer). The
	// engine sets it on a set it retires in the middle of a recording (a
	// rate change: the recording ends as a recording, the original is it)
	// or before the copy that follows a recording; the worker copies the
	// original to the tape and clears it. A set offered with it set is
	// copied by the engine's own path (read from the original, then
	// ORIGINAL_TO_TAPE), which clears it.
	bool tapeStale = false;

	// Capacity min(300 s, 2^24 frames) and a tail of 10 ms at rate fs, both
	// tapes empty. False, with nothing held, if the rate is unusable or the
	// memory is not there.
	bool allocate(float rate) {
		release();
		if (!(rate >= 1.f && rate <= 4e6f))
			return false;
		double cap = std::floor(eddy::TAPE_MAX_S * (double) rate);
		if (cap > eddy::TAPE_MAX_FRAMES)
			cap = eddy::TAPE_MAX_FRAMES;
		const size_t tail = (size_t) std::ceil(eddy::TAIL_S * (double) rate);
		if (!tape.allocate((size_t) cap, tail) || !original.allocate((size_t) cap, tail)) {
			release();
			return false;
		}
		overview.clear();
		fs = rate;
		tapeStale = false;
		return true;
	}
	void release() {
		tape.release();
		original.release();
		overview.clear();
		fs = 0.f;
	}
};

struct EddyFrame {
	enum { SPEED, SPEED_ATT, START, LENGTH, WEAR, SMEAR, DUB_LEVEL, MIX, PARAMS_LEN };
	float params[PARAMS_LEN] = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.5f, 1.f};
	float speedCv = 0, voct = 0, startCv = 0;
	float recGate = 0, dubGate = 0, playTrig = 0, restoreTrig = 0;   // volts
	float inL = 0, inR = 0;
	bool inRPatched = false;
	bool recPress = false, dubPress = false, playPress = false, restorePress = false;   // true for one sample
	bool loop = true;
};

struct EddyResult {
	float l = 0, r = 0;
	bool eoc = false;
	bool recording = false, dubbing = false, playing = false;   // for the lights and the saved params
	bool empty = true;
	float head = 0, winStart = 0, winLength = 1, overlap = 0;   // fractions of the tape, for the window
	float seconds = 0;                                          // the tape's length
};

class EddyEngine {
public:
	enum Copy { NONE, ORIGINAL_TO_TAPE, TAPE_TO_ORIGINAL };

	explicit EddyEngine(uint32_t seed)
	    : seed_(seed), pre_(new float[2 * (size_t) preMax()]) {
		for (int i = 0; i < RETIRE_SLOTS; i++)
			retired_[i].store(nullptr, std::memory_order_relaxed);
		applyRate(48000.f);
	}

	// ---- Any other thread.

	// The set is retired (silence until one at this rate is offered) if its
	// rate differs.
	void setSampleRate(float fs) {
		fsReq_.store(fs, std::memory_order_relaxed);
	}
	// Play and Dub as a patch saved them (Dub only if the tape is not empty
	// when it is applied, which waits for a set already offered to be taken).
	void setTransport(bool play, bool dub) {
		transportReq_.store(4 | (play ? 1 : 0) | (dub ? 2 : 0), std::memory_order_release);
	}
	// Initialize, and the menu's Clear: the tape and the original emptied,
	// Rec, Dub and Play off.
	//
	// clear() and offer() are ordered: the last request wins. A clear()
	// made after an offer() applies to that set when the engine takes it
	// (it becomes the live set, emptied). An offer() made after a clear()
	// wins: the set is taken as it is. A clear() with no live set is
	// remembered and applied to the next set taken, unless a later offer()
	// supersedes it.
	void clear() {
		clearGen_.store(gen_.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
	}
	// A set to use: an empty one to record into, or a loaded tape (tape and
	// original equal, overview built). False if one is still waiting.
	bool offer(TapeSet* set) {
		if (!set || incoming_.load(std::memory_order_acquire))
			return false;
		// Only the audio thread empties `incoming_`, so it stays empty until
		// this offer fills it: the number is this set's.
		offerGen_.store(gen_.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_release);
		TapeSet* none = nullptr;
		return incoming_.compare_exchange_strong(none, set, std::memory_order_acq_rel);
	}
	// A set the engine is done with, or nullptr.
	TapeSet* collect() {
		for (int i = 0; i < RETIRE_SLOTS; i++) {
			TapeSet* s = retired_[i].exchange(nullptr, std::memory_order_acq_rel);
			if (s)
				return s;
		}
		return nullptr;
	}
	// The copy to make on live(), then call copyDone().
	Copy copyWanted() const {
		return (Copy) published_.load(std::memory_order_acquire);
	}
	void requestCopy(Copy c) {
		if (c == ORIGINAL_TO_TAPE || c == TAPE_TO_ORIGINAL)
			userCopy_.store((int) c, std::memory_order_release);
	}
	TapeSet* live() const {
		return livePub_.load(std::memory_order_acquire);
	}
	void copyDone() {
		published_.store(NONE, std::memory_order_release);
		copyDone_.store(true, std::memory_order_release);
	}
	uint32_t tapeChanges() const {
		return tapeChanges_.load(std::memory_order_relaxed);
	}
	uint32_t originalChanges() const {
		return originalChanges_.load(std::memory_order_relaxed);
	}

	// ---- Audio thread.

	// For the tests: voices sounding now.
	int activeVoices() const {
		return voices_.count();
	}

	void process(const EddyFrame& f, EddyResult& out) {
		using namespace eddy;
		const float wantFs = fsReq_.load(std::memory_order_relaxed);
		if (wantFs != fs_)
			applyRate(wantFs);

		if (inFlight_ != NONE && copyDone_.load(std::memory_order_acquire)) {
			copyDone_.store(false, std::memory_order_relaxed);
			finishCopy();
		}
		clearRequests();
		if (userCopy_.load(std::memory_order_relaxed) != NONE) {
			const int c = userCopy_.exchange(NONE, std::memory_order_acq_rel);
			if (c != NONE)
				askCopy(c);
		}
		handoff();
		if (transportReq_.load(std::memory_order_relaxed) >= 0 && !incoming_.load(std::memory_order_relaxed)) {
			const int t = transportReq_.exchange(-1, std::memory_order_acq_rel);
			if (t >= 0)
				applyTransport(t);
		}

		edges(f);

		if (--countdown_ <= 0) {
			countdown_ = CONTROL_SAMPLES;
			controls(f);
		}

		// The speed.
		{
			const float d = speedTarget_ - glide_;
			glide_ = (d < 1e-6f && d > -1e-6f) ? speedTarget_ : glide_ + kGlide_ * d;
			if (voct_ != voctTarget_) {
				const float d2 = voctTarget_ - voct_;
				voct_ = (d2 <= voctStep_ && d2 >= -voctStep_) ? voctTarget_ : voct_ + (d2 > 0.f ? voctStep_ : -voctStep_);
			}
		}
		const float v = speedFinal(glide_, voct_);
		speedNow_ = v;
		if (v > 0.f)
			dir_ = 1;
		else if (v < 0.f)
			dir_ = -1;

		float in[2];
		in[0] = finite(f.inL);
		in[1] = f.inRPatched ? finite(f.inR) : in[0];

		record(in);
		if (pend_.active)
			startPending();
		syncSources();
		ensureHead();

		// The voices, read and summed.
		float tape[2] = {0.f, 0.f};
		for (int i = 0; i < Voices::N; i++) {
			const Voice& x = voices_.v[i];
			if (!x.active)
				continue;
			const float g = x.gain();
			tape[0] += g * x.read(0);
			tape[1] += g * x.read(1);
		}

		// The transport.
		const bool runningTarget = playOn_ || oneShotTail_;
		playGain_ = ramp(playGain_, runningTarget ? 1.f : 0.f, fadeStep_);
		const bool running = runningTarget || playGain_ > 0.f;
		const double mv = running ? (double) v : 0.0;
		moveVoices(mv, in);
		if (oneShotTail_ && voices_.count() == 0)
			oneShotTail_ = false;

		publishCopy();
		finishClear();

		// Write fades.
		const bool usable = usableSet();
		holdGain_ = ramp(holdGain_, holdWanted() ? 0.f : 1.f, fadeStep_);
		const bool dubEffective = dubOn_ && dubLevel_ > 0.f && rec_ == REC_IDLE && !wantOriginal() && inFlight_ == NONE;
		dubGain_ = ramp(dubGain_, dubEffective ? 1.f : 0.f, fadeStep_);
		wearGain_ = ramp(wearGain_, wearOn_ ? 1.f : 0.f, fadeStep_);

		if (usable && (rec_ == REC_ON || wantOriginal() || inFlight_ == ORIGINAL_TO_TAPE))
			live_->overview.service(live_->original, OVERVIEW_BUDGET);
		else if (usable)
			live_->overview.service(live_->tape, OVERVIEW_BUDGET);

		// Mix and the limit.
		const bool inputLoud = rec_ == REC_PRE || rec_ == REC_ON || (dubOn_ && usable);
		inGain_ = ramp(inGain_, inputLoud ? 1.f : dryT_, fadeStep_);
		wet_ = ramp(wet_, wetT_, fadeStep_);
		const float og = outputGain(v) * playGain_;
		out.l = limit(wet_ * (tape[0] * og) + inGain_ * in[0]);
		out.r = limit(wet_ * (tape[1] * og) + inGain_ * in[1]);

		if (eocLeft_ > 0)
			eocLeft_--;
		out.eoc = eocLeft_ > 0;
		out.recording = recWant_ || rec_ == REC_PRE || rec_ == REC_ON;
		out.dubbing = dubOn_;
		out.playing = playOn_;
		view(out);
	}

private:
	typedef eddy::Voice Voice;
	typedef eddy::Voices Voices;
	enum { RETIRE_SLOTS = 4 };
	enum Rec { REC_IDLE, REC_PRE, REC_ON, REC_TAIL };

	struct Pending {
		bool active = false;
		double pos = 0.0, perSample = 0.0, perFrame = 0.0;
	};

	static int preMax() {
		return (int) std::ceil(eddy::REC_MIN_S * (double) eddy::REC_BUFFER_FS);
	}
	static float finite(float x) {
		return (x - x == 0.f) ? x : 0.f;
	}
	static float limit(float x) {
		return eddy::detail::softBound(finite(x));
	}
	// A straight ramp toward the target, arriving exactly.
	static float ramp(float x, float target, float step) {
		if (x < target)
			return x + step < target ? x + step : target;
		if (x > target)
			return x - step > target ? x - step : target;
		return x;
	}

	// ---- The rate.

	void applyRate(float fs) {
		using namespace eddy;
		if (live_ && live_->fs == fs_ && live_->fs != fs)
			endForRate();
		fs_ = fs;
		const double rate = (fs > 0.f && fs < 1e8f) ? (double) fs : 48000.0;
		fadeFrames_ = rate * FADE_S;
		fadeStep_ = (float) (1.0 / (fadeFrames_ > 1.0 ? fadeFrames_ : 1.0));
		fadeRate_ = 1.0 / (fadeFrames_ > 1.0 ? fadeFrames_ : 1.0);
		const double cut = rate * CUT_S;
		cutRate_ = 1.0 / (cut > 1.0 ? cut : 1.0);
		const double eoc = std::floor(rate * EOC_S + 0.5);
		eocN_ = eoc > 1.0 ? (int) eoc : 1;
		// 1 - e^(-1 / (tau fs)), without libm.
		kGlide_ = 1.f - fastExp2((float) (-1.4426950408889634 / (SPEED_GLIDE_S * rate)));
		const double vs = VOCT_SMOOTH_S * rate;
		voctFrames_ = (float) (vs > 1.0 ? vs : 1.0);
		double pre = std::floor(REC_MIN_S * rate + 0.5);
		pre = pre < 1.0 ? 1.0 : pre > (double) preMax() ? (double) preMax() : pre;
		preN_ = (size_t) pre;
		guard_.setSampleRate((float) rate);
		rate_ = (float) rate;
		// A set at another rate is silent at once and retired when no copy
		// is under way on it (handoff).
		voices_.killAll();
		pend_.active = false;
		rec_ = REC_IDLE;
		recWant_ = false;
		dubOn_ = false;
		queued_ = NONE;
		tapeStale_ = false;
		clearing_ = false;
		oneShotTail_ = false;
		if (usableSet())
			computeWindow();
	}

	bool usableSet() const {
		return live_ && live_->fs == fs_;
	}

	// The set is about to be retired by a rate change. A recording that
	// stands ends as a recording (what was recorded is the original, at its
	// length; the tail so far); the set is flagged so the worker copies the
	// original to the tape before it is used or converted. One still under
	// REC_MIN_S is thrown away (the set unchanged). The rest of the start
	// buffer is copied in at once: at most REC_MIN_S of frames, once.
	void endForRate() {
		Tape& o = live_->original;
		if (rec_ == REC_ON || rec_ == REC_TAIL) {
			const float* pre0 = pre_.get();
			const float* pre1 = pre_.get() + preMax();
			for (size_t i = preCopied_; i < preN_ && i < o.capacity(); i++) {
				o.ch[0][i] = pre0[i];
				o.ch[1][i] = pre1[i];
			}
			preCopied_ = preN_;
			o.length = recN_;
			o.tail = rec_ == REC_TAIL ? tailK_ : 0;
			live_->tapeStale = true;
			originalChanges_.fetch_add(1, std::memory_order_relaxed);
		}
		else if (tapeStale_)
			live_->tapeStale = true;   // a recording ended, its copy not yet done
	}

	// ---- Sets.

	bool freeSlot() const {
		for (int i = 0; i < RETIRE_SLOTS; i++)
			if (!retired_[i].load(std::memory_order_relaxed))
				return true;
		return false;
	}
	bool retire(TapeSet* s) {
		for (int i = 0; i < RETIRE_SLOTS; i++)
			if (!retired_[i].load(std::memory_order_relaxed)) {
				retired_[i].store(s, std::memory_order_release);
				return true;
			}
		return false;
	}

	void handoff() {
		if (old_ && !voices_.owns(old_) && retire(old_))
			old_ = nullptr;
		if (live_ && live_->fs != fs_ && inFlight_ == NONE && !voices_.owns(live_) && retire(live_)) {
			live_ = nullptr;
			livePub_.store(nullptr, std::memory_order_release);
		}
		if (!incoming_.load(std::memory_order_relaxed) || inFlight_ != NONE || old_ || !freeSlot())
			return;
		// The offer's number, read while the set still waits (offer() writes
		// it only when none does).
		const uint32_t og = offerGen_.load(std::memory_order_acquire);
		TapeSet* s = incoming_.exchange(nullptr, std::memory_order_acq_rel);
		if (!s)
			return;
		if (s->fs != fs_) {
			retire(s);
			return;
		}
		if (live_) {
			voices_.fadeOutAll(fadeRate_);
			old_ = live_;
		}
		pend_.active = false;
		live_ = s;
		livePub_.store(s, std::memory_order_release);
		rec_ = REC_IDLE;
		recWant_ = false;
		dubOn_ = false;
		queued_ = NONE;
		tapeStale_ = s->tapeStale;
		clearing_ = false;
		computeWindow();
		head_ = winValid_ ? origin(dir_) : 0.0;
		tapeChanges_.fetch_add(1, std::memory_order_relaxed);
		originalChanges_.fetch_add(1, std::memory_order_relaxed);
		// A clear() made after this set was offered applies to it.
		const uint32_t cg = clearGen_.load(std::memory_order_acquire);
		if (cg != clearDone_ && later(cg, og)) {
			clearDone_ = cg;
			beginClear();
		}
	}

	void applyTransport(int t) {
		const bool play = (t & 1) != 0, dub = (t & 2) != 0;
		if (play != playOn_) {
			playOn_ = play;
			oneShotTail_ = false;
		}
		dubOn_ = dub && dubAllowed();
	}

	// ---- Clear.

	// a after b, for numbers that may wrap.
	static bool later(uint32_t a, uint32_t b) {
		return (int32_t) (a - b) > 0;
	}

	// A clear() not yet applied: to the live set now, unless a set waits to
	// be taken (then the later of the two requests wins); with no set at
	// all it waits for the next one taken.
	void clearRequests() {
		const uint32_t cg = clearGen_.load(std::memory_order_acquire);
		if (cg == clearDone_)
			return;
		if (incoming_.load(std::memory_order_acquire)) {
			if (later(offerGen_.load(std::memory_order_acquire), cg))
				clearDone_ = cg;   // a later offer supersedes it
			return;                // else handoff applies it to that set
		}
		if (live_) {
			clearDone_ = cg;
			beginClear();
		}
	}

	void beginClear() {
		if (!live_)
			return;
		clearing_ = true;
		rec_ = REC_IDLE;
		recWant_ = false;
		dubOn_ = false;
		playOn_ = false;
		oneShotTail_ = false;
		queued_ = NONE;
		pend_.active = false;
		voices_.fadeOutAll(fadeRate_);
	}
	void finishClear() {
		if (!clearing_ || inFlight_ != NONE || voices_.owns(live_) || holdGain_ > 0.f)
			return;
		clearing_ = false;
		tapeStale_ = false;
		live_->tape.length = live_->tape.tail = 0;
		live_->original.length = live_->original.tail = 0;
		live_->overview.touch(0, 0, 0);
		winValid_ = false;
		head_ = 0.0;
		tapeChanges_.fetch_add(1, std::memory_order_relaxed);
		originalChanges_.fetch_add(1, std::memory_order_relaxed);
	}

	// ---- Copies.

	// The voices read the original while the tape is out of date or is to be
	// (or is being) copied over from it.
	bool wantOriginal() const {
		if (inFlight_ != NONE)
			return inFlight_ == ORIGINAL_TO_TAPE;
		return tapeStale_ || queued_ == ORIGINAL_TO_TAPE;
	}
	const Tape* source() const {
		return wantOriginal() ? &live_->original : &live_->tape;
	}
	// Nothing writes the tape while a copy is wanted, under way or waiting,
	// while recording, or while clearing.
	bool holdWanted() const {
		return inFlight_ != NONE || tapeStale_ || queued_ != NONE || rec_ != REC_IDLE || clearing_;
	}
	size_t curLength() const {
		return usableSet() ? source()->length : 0;
	}
	bool dubAllowed() const {
		return usableSet() && curLength() > 0 && rec_ == REC_IDLE && !clearing_;
	}

	// Restore (ORIGINAL_TO_TAPE) or "Make this the original": ignored on an
	// empty tape and while recording. Restore ends a dub.
	void askCopy(int c) {
		if (!dubAllowed())
			return;
		queued_ = c;
		if (c == ORIGINAL_TO_TAPE)
			dubOn_ = false;
	}

	void publishCopy() {
		if (inFlight_ != NONE || !usableSet() || rec_ != REC_IDLE || clearing_ || holdGain_ > 0.f || pend_.active)
			return;
		const int next = tapeStale_ ? (int) ORIGINAL_TO_TAPE : queued_;
		if (next == NONE)
			return;
		const Tape* written = next == ORIGINAL_TO_TAPE ? &live_->tape : &live_->original;
		if (voices_.reads(written))
			return;
		if (tapeStale_) {
			if (queued_ == ORIGINAL_TO_TAPE)
				queued_ = NONE;
		}
		else
			queued_ = NONE;
		for (int i = 0; i < Voices::N; i++) {
			voices_.v[i].writer.reset();
			voices_.v[i].nRuns = 0;
		}
		inFlight_ = next;
		copyDone_.store(false, std::memory_order_relaxed);
		published_.store(next, std::memory_order_release);
	}

	void finishCopy() {
		if (inFlight_ == ORIGINAL_TO_TAPE) {
			// The two are now equal: reading goes back to the tape at once.
			voices_.retarget(&live_->original, &live_->tape);
			tapeStale_ = false;
			live_->tapeStale = false;
			live_->overview.touch(0, live_->tape.length, live_->tape.length);
			tapeChanges_.fetch_add(1, std::memory_order_relaxed);
		}
		else
			originalChanges_.fetch_add(1, std::memory_order_relaxed);
		inFlight_ = NONE;
	}

	// ---- Edges.

	struct Schmitt {
		bool high = false;
		// +1 rising, -1 falling, 0 neither.
		int process(float v) {
			v = finite(v);
			if (high && v <= 0.1f) {
				high = false;
				return -1;
			}
			if (!high && v >= 1.f) {
				high = true;
				return 1;
			}
			return 0;
		}
	};

	void edges(const EddyFrame& f) {
		// Rec.
		if (f.recPress)
			recWant_ = !recWant_;
		const int re = recGate_.process(f.recGate);
		if (re > 0)
			recWant_ = true;
		else if (re < 0)
			recWant_ = false;
		if (!usableSet() || clearing_)
			recWant_ = false;
		if (recWant_) {
			if (rec_ == REC_IDLE && inFlight_ == NONE && !tapeStale_)
				startRec();
		}
		else if (rec_ == REC_PRE)
			rec_ = REC_IDLE;   // under REC_MIN_S: thrown away; the tape plays again
		else if (rec_ == REC_ON)
			endRec();

		// Dub.
		if (f.dubPress)
			dubOn_ = !dubOn_;
		const int de = dubGate_.process(f.dubGate);
		if (de > 0)
			dubOn_ = true;
		else if (de < 0)
			dubOn_ = false;
		if (!dubAllowed())
			dubOn_ = false;

		// Play.
		bool retrigger = false;
		if (f.playPress) {
			playOn_ = !playOn_;
			oneShotTail_ = false;
		}
		if (playTrig_.process(f.playTrig) > 0) {
			playOn_ = true;
			retrigger = true;
		}

		// Restore.
		const bool restore = restoreTrig_.process(f.restoreTrig) > 0;
		if (f.restorePress || restore)
			askCopy(ORIGINAL_TO_TAPE);

		if (retrigger && usableSet() && winValid_ && rec_ == REC_IDLE && !clearing_) {
			oneShotTail_ = false;
			jumpTo(origin(dir_), fadeRate_, 0.0);
		}
	}

	// ---- Recording.

	void startRec() {
		rec_ = REC_PRE;
		recN_ = 0;
		dubOn_ = false;
		oneShotTail_ = false;
		if (const Voice* h = voices_.head())
			head_ = h->pos;
		pend_.active = false;
		voices_.fadeOutAll(fadeRate_);
	}

	void endRec() {
		using namespace eddy;
		Tape& o = live_->original;
		o.length = recN_;
		o.tail = 0;
		double m = fadeFrames_ < (double) recN_ / 4.0 ? fadeFrames_ : (double) recN_ / 4.0;
		size_t tn = (size_t) std::ceil(m);
		tailN_ = tn < o.tailCapacity() ? tn : o.tailCapacity();
		tailK_ = 0;
		rec_ = REC_TAIL;
		recWant_ = false;
		tapeStale_ = true;
		queued_ = NONE;
		dubOn_ = false;
		playOn_ = true;
		oneShotTail_ = false;
		playGain_ = 1.f;   // the voice's own fade brings the tape in
		live_->overview.touch(0, recN_, recN_);
		computeWindow();
		head_ = winValid_ ? origin(dir_) : 0.0;
		if (tailN_ == 0)
			finishRec();
	}

	void finishRec() {
		rec_ = REC_IDLE;
		originalChanges_.fetch_add(1, std::memory_order_relaxed);
	}

	void record(const float in[2]) {
		using namespace eddy;
		if (rec_ == REC_IDLE || !usableSet())
			return;
		Tape& o = live_->original;
		float* pre0 = pre_.get();
		float* pre1 = pre_.get() + preMax();
		if (rec_ == REC_PRE) {
			pre0[recN_] = in[0];
			pre1[recN_] = in[1];
			recN_++;
			if (recN_ >= preN_) {
				// It stands: from here on it goes onto the original.
				rec_ = REC_ON;
				preCopied_ = 0;
				o.tail = 0;
				o.length = recN_;
				queued_ = NONE;
				live_->overview.touch(0, recN_, o.capacity());
				if (recN_ >= o.capacity())
					endRec();
			}
			return;
		}
		copyPre();
		if (rec_ == REC_ON) {
			o.ch[0][recN_] = in[0];
			o.ch[1][recN_] = in[1];
			recN_++;
			o.length = recN_;
			live_->overview.touch(recN_ - 1, recN_, o.capacity());
			if (recN_ >= o.capacity())
				endRec();
		}
		else if (rec_ == REC_TAIL) {
			o.ch[0][recN_ + tailK_] = in[0];
			o.ch[1][recN_ + tailK_] = in[1];
			tailK_++;
			o.tail = tailK_;
			if (tailK_ >= tailN_)
				finishRec();
		}
	}

	// The buffer's frames go onto the original behind the recording.
	void copyPre() {
		if (preCopied_ >= preN_)
			return;
		Tape& o = live_->original;
		size_t n = preN_ - preCopied_;
		n = n < (size_t) eddy::PRE_COPY_FRAMES ? n : (size_t) eddy::PRE_COPY_FRAMES;
		const float* pre0 = pre_.get();
		const float* pre1 = pre_.get() + preMax();
		for (size_t i = preCopied_; i < preCopied_ + n; i++) {
			o.ch[0][i] = pre0[i];
			o.ch[1][i] = pre1[i];
		}
		const size_t frames = rec_ == REC_ON ? o.capacity() : recN_;
		live_->overview.touch(preCopied_, preCopied_ + n, frames);
		preCopied_ += n;
	}

	// ---- The window.

	void computeWindow() {
		using namespace eddy;
		winValid_ = false;
		if (!usableSet())
			return;
		const Tape* t = source();
		len_ = (double) t->length;
		if (!(len_ >= 1.0))
			return;
		const Window w = window(startK_, startCv_, lengthK_, smearK_, len_, 0.0, fs_);
		S_ = w.start;
		L_ = w.length;
		m_ = w.minFade;
		o_ = w.overlap;
		wrap_ = S_ + L_ > len_;
		double after, before;
		if (wrap_)
			after = before = len_ - L_;
		else {
			after = len_ + (double) t->tail - (S_ + L_);
			before = S_ - 1.0;
		}
		after = after > 0.0 ? (after < m_ ? after : m_) : 0.0;
		before = before > 0.0 ? (before < m_ ? before : m_) : 0.0;
		turnF_ = L_ - (o_ - after);
		turnB_ = L_ - (o_ - before);
		double ob = S_ + (L_ >= 1.0 ? L_ - 1.0 : 0.0);
		if (wrap_ && ob >= len_)
			ob -= len_;
		originB_ = ob;
		winValid_ = true;
	}

	double origin(int d) const {
		return d < 0 ? originB_ : S_;
	}
	double turn(int d) const {
		return d < 0 ? turnB_ : turnF_;
	}
	// How far into the window p is, going in direction d.
	double rel(double p, int d) const {
		double r = d < 0 ? originB_ - p : p - S_;
		if (wrap_) {
			if (r < 0.0)
				r += len_;
			else if (r >= len_)
				r -= len_;
		}
		return r;
	}
	bool inside(double p, int d) const {
		if (!(p >= 0.0 && p < len_ + 1.0))
			return false;
		const double r = rel(p, d);
		return r >= 0.0 && r < turn(d);
	}

	// ---- Voices.

	// A new voice (Voices::start) with a fresh wear state of its own, seeded
	// from the engine's seed and the voice's number.
	Voice* startVoice(double p, double perSample, double perFrame) {
		Voice* n = voices_.start(source(), live_, p, wrap_, perSample, perFrame);
		if (n) {
			n->wear = eddy::Wear(seed_ * 2654435761u + n->born);
			n->wear.setSampleRate(rate_);
			n->wear.set(wearK_);
		}
		return n;
	}

	// The head goes to p: a new voice there, fading in as the last fades out.
	// Silent (stopped), the voices simply go and the head waits at p.
	void jumpTo(double p, double perSample, double perFrame) {
		if (!(playGain_ > 0.f)) {
			voices_.killAll();
			pend_.active = false;
			head_ = p;
			return;
		}
		if (!startVoice(p, perSample, perFrame)) {
			voices_.cutOldest(cutRate_);
			pend_.active = true;
			pend_.pos = p;
			pend_.perSample = perSample;
			pend_.perFrame = perFrame;
		}
		else
			pend_.active = false;
	}
	void startPending() {
		if (!usableSet() || !winValid_ || rec_ == REC_PRE || rec_ == REC_ON || clearing_) {
			pend_.active = false;
			return;
		}
		if (startVoice(pend_.pos, pend_.perSample, pend_.perFrame))
			pend_.active = false;
		else
			voices_.cutOldest(cutRate_);
	}

	// The voices read what the copies allow: the newest crossfades (F) to the
	// other of tape and original at the same place; the rest fade out within F.
	void syncSources() {
		if (!usableSet())
			return;
		const Tape* want = source();
		const Tape* other = want == &live_->tape ? &live_->original : &live_->tape;
		Voice* h = voices_.head();
		if (h && h->owner == live_ && h->src == other)
			jumpTo(h->pos, fadeRate_, 0.0);
		for (int i = 0; i < Voices::N; i++) {
			Voice& x = voices_.v[i];
			if (x.active && i != voices_.newest && x.src == other)
				x.fadeOut(fadeRate_);
		}
	}

	// Playing with no head: one starts where the head waits (or at the
	// window's start if that is outside it), fading in.
	void ensureHead() {
		if (!playOn_ || voices_.newest >= 0 || pend_.active || !usableSet() || !winValid_ || clearing_)
			return;
		if (rec_ == REC_PRE || rec_ == REC_ON || (rec_ == REC_TAIL && preCopied_ < preN_))
			return;
		double p = head_;
		if (wrap_)
			p = Voice::reduce(p, len_);
		if (!inside(p, dir_))
			p = origin(dir_);
		startVoice(p, fadeRate_, 0.0);
	}

	void moveVoices(double mv, const float in[2]) {
		// Each voice's last move is written now, with this sample's input:
		// the writer lays the input down where a move ends, and the voice
		// read this sample where its last move ended, so a dub played back at
		// the speed it was made at is heard as it was played.
		for (int i = 0; i < Voices::N; i++) {
			Voice& x = voices_.v[i];
			if (!x.active)
				continue;
			for (int k = 0; k < x.nRuns; k++)
				writeRun(x, x.runs[k].a, x.runs[k].b, in);
			x.nRuns = 0;
		}
		const double travel = mv < 0.0 ? -mv : mv;
		Voice* h = voices_.head();
		const bool headOurs = h && h->owner == live_ && usableSet() && winValid_;
		// Every voice moves, fades, and has its move (unwrapped) written next
		// sample: a voice writes what it passes, for as long as it sounds.
		for (int i = 0; i < Voices::N; i++) {
			Voice& x = voices_.v[i];
			if (!x.active)
				continue;
			const double q0 = x.pos;
			x.move(mv);
			// Fades by travel cannot finish without it: those go by time.
			if (travel == 0.0 && i != voices_.newest && x.perFrame != 0.0)
				x.fadeOut(fadeRate_);
			x.fade(travel);
			if (x.active && travel > 0.0)
				x.queue(q0, q0 + mv);
		}
		if (!h || !h->active) {
			if (h && !h->active)
				voices_.newest = -1;
			return;
		}
		head_ = h->pos;
		if (!headOurs || travel == 0.0 || pend_.active)
			return;

		const int d = mv > 0.0 ? 1 : -1;
		const double r1 = rel(h->pos, d);
		const double tn = turn(d);
		if (!(r1 >= tn)) {
			if (r1 < 0.0 || !(h->pos >= 0.0 && h->pos < len_ + 1.0))
				jumpTo(origin(d), fadeRate_, 0.0);
			return;
		}
		double over = r1 - tn;
		if (!(over <= travel + 1e-9)) {
			// Not crossed by this move (the window or the direction changed):
			// a window move.
			jumpTo(origin(d), fadeRate_, 0.0);
			return;
		}
		// The join: the head reads on through the overlap, fading, and a new
		// one starts at the window's start.
		if (over >= tn && tn > 0.0)
			over -= tn * std::floor(over / tn);
		eocLeft_ = eocN_ + 1;
		const double perFrame = 1.0 / (o_ > 1.0 ? o_ : 1.0);
		const double at = origin(d) + d * over;
		if (loop_) {
			jumpTo(at, 0.0, perFrame);
			Voice* n = voices_.head();
			if (n && n != h)
				n->queue(origin(d), at);
		}
		else {
			// One shot: the head fades out through the join and Play goes off.
			h->perSample = 0.0;
			h->perFrame = -perFrame;
			voices_.newest = -1;
			playOn_ = false;
			oneShotTail_ = true;
			head_ = origin(d);
		}
	}

	// ---- The write head.

	// Voice x moved from a to b (b unwrapped: a wrapping voice may pass the
	// tape's end by a sample's move). It writes at the strength of its own
	// gain, times the hold, the Stopped law, Play's fade and the larger of
	// the Dub and Wear fades: at 0 the tape is unchanged.
	void writeRun(Voice& x, double a, double b, const float in[2]) {
		using namespace eddy;
		const float amount = wearGain_ > dubGain_ ? wearGain_ : dubGain_;
		const float g = x.gain() * holdGain_ * writeGain(speedNow_) * playGain_ * amount;
		if (!(g > 0.f) || !usableSet() || x.owner != live_ || x.src != &live_->tape || rec_ != REC_IDLE) {
			x.writer.reset();
			return;
		}
		wroteTick_ = true;
		const double len = x.length();
		if (x.wrap && b >= len) {
			writeStraight(x, a, len, in, g);
			writeStraight(x, 0.0, b - len, in, g);
		}
		else if (x.wrap && b < 0.0) {
			writeStraight(x, a, 0.0, in, g);
			writeStraight(x, len, len + b, in, g);
		}
		else
			writeStraight(x, a, b, in, g);
	}

	// Written frames are held to the tape and its tail by the writer.
	void writeStraight(Voice& x, double p0, double p1, const float in[2], float gain) {
		if (p0 == p1)
			return;
		Tape& t = live_->tape;
		const float keep = keep_, add = add_, dub = dubGain_;
		eddy::Wear& wear = x.wear;
		eddy::WriteGuard& guard = guard_;
		x.writer.write(t, p0, p1, in, [&](int c, float old, float v) -> float {
			const float w = wear.process(c, old, 1.f);
			const float pre = w + dub * ((keep * w + add * v) - w);
			const float y = guard.process(c, pre);
			return old + gain * (y - old);
		});
		const double lo = p0 < p1 ? p0 : p1, hi = p0 < p1 ? p1 : p0;
		const double a = std::floor(lo) - 1.0, b = std::floor(hi) + 2.0;
		live_->overview.touch(a > 0.0 ? (size_t) a : 0, b > 0.0 ? (size_t) b : 0, t.length);
	}

	// ---- Controls.

	void controls(const EddyFrame& f) {
		using namespace eddy;
		speedTarget_ = speedTarget(f.params[EddyFrame::SPEED], f.speedCv, f.params[EddyFrame::SPEED_ATT]);
		const float vo = detail::volts(f.voct);
		if (vo != voctTarget_) {
			voctTarget_ = vo;
			const float d = voctTarget_ - voct_;
			voctStep_ = (d < 0.f ? -d : d) / voctFrames_;
		}
		startK_ = f.params[EddyFrame::START];
		startCv_ = f.startCv;
		lengthK_ = f.params[EddyFrame::LENGTH];
		smearK_ = f.params[EddyFrame::SMEAR];
		loop_ = f.loop;
		wearK_ = f.params[EddyFrame::WEAR];
		wearOn_ = wearK_ > 0.f;   // NaN: off
		for (int i = 0; i < Voices::N; i++)
			if (voices_.v[i].active)
				voices_.v[i].wear.set(wearK_);
		const float dl = f.params[EddyFrame::DUB_LEVEL];
		dubGains(dl, add_, keep_);
		dubLevel_ = add_;
		float mix = f.params[EddyFrame::MIX];
		mix = mix == mix ? mix : 1.f;
		if (mix >= 1.f) {
			dryT_ = 0.f;
			wetT_ = 1.f;
		}
		else if (mix <= 0.f) {
			dryT_ = 1.f;
			wetT_ = 0.f;
		}
		else {
			dryT_ = fastSin2pi(0.25f + 0.25f * mix);
			wetT_ = fastSin2pi(0.25f * mix);
		}
		computeWindow();
		// A window that moved off the head: the head fades to its start.
		Voice* h = voices_.head();
		if (winValid_ && h && h->owner == live_ && !pend_.active && rec_ != REC_PRE && rec_ != REC_ON) {
			if (h->wrap != wrap_) {
				h->wrap = wrap_;
				if (wrap_)
					h->pos = Voice::reduce(h->pos, len_);
			}
			if (!inside(h->pos, dir_))
				jumpTo(origin(dir_), fadeRate_, 0.0);
		}
		if (snap_) {
			glide_ = speedTarget_;
			voct_ = voctTarget_;
			inGain_ = dryT_;
			wet_ = wetT_;
			snap_ = false;
		}
		if (wroteTick_) {
			wroteTick_ = false;
			tapeChanges_.fetch_add(1, std::memory_order_relaxed);
		}
	}

	// ---- The view.

	void view(EddyResult& out) const {
		out.empty = true;
		out.head = out.winStart = out.overlap = out.seconds = 0.f;
		out.winLength = 1.f;
		if (!usableSet())
			return;
		if (rec_ == REC_PRE || rec_ == REC_ON) {
			const double c = (double) live_->original.capacity();
			const double n = (double) recN_;
			out.empty = rec_ == REC_PRE && curLength() == 0;
			out.head = (float) (n / c);
			out.winLength = (float) (n / c);
			out.seconds = (float) (n / fs_);
			return;
		}
		if (!winValid_)
			return;
		out.empty = false;
		const Voice* h = voices_.head();
		const double p = h && h->owner == live_ ? h->pos : head_;
		out.head = (float) (Voice::reduce(p, len_) / len_);
		out.winStart = (float) (S_ / len_);
		out.winLength = (float) (L_ / len_);
		out.overlap = (float) (o_ / len_);
		out.seconds = (float) (len_ / fs_);
	}

	// ---- State.

	float fs_ = 0.f;
	std::atomic<float> fsReq_{48000.f};
	double fadeFrames_ = 480.0, fadeRate_ = 1.0 / 480.0, cutRate_ = 1.0 / 48.0;
	float fadeStep_ = 1.f / 480.f;
	int eocN_ = 48, eocLeft_ = 0;
	float kGlide_ = 0.f, voctFrames_ = 96.f;
	size_t preN_ = 2400;

	// Sets and copies.
	TapeSet* live_ = nullptr;   // the set in use (audio thread)
	TapeSet* old_ = nullptr;    // the last set, while its voices fade
	std::atomic<TapeSet*> livePub_{nullptr};
	std::atomic<TapeSet*> incoming_{nullptr};
	std::atomic<TapeSet*> retired_[RETIRE_SLOTS];
	std::atomic<int> published_{NONE};
	std::atomic<bool> copyDone_{false};
	std::atomic<int> userCopy_{NONE};
	std::atomic<uint32_t> gen_{0}, offerGen_{0}, clearGen_{0};   // the order of offer() and clear()
	uint32_t clearDone_ = 0;   // the last clear() applied (audio thread)
	std::atomic<int> transportReq_{-1};
	std::atomic<uint32_t> tapeChanges_{0}, originalChanges_{0};
	int inFlight_ = NONE;    // the copy published, until it is done
	int queued_ = NONE;      // a Restore or "Make this the original" waiting
	bool tapeStale_ = false; // a recording has ended and the tape is not yet copied
	bool clearing_ = false;

	// Transport.
	bool recWant_ = false, dubOn_ = false, playOn_ = false, oneShotTail_ = false, loop_ = true;
	Schmitt recGate_, dubGate_, playTrig_, restoreTrig_;
	Rec rec_ = REC_IDLE;
	size_t recN_ = 0, preCopied_ = 0, tailN_ = 0, tailK_ = 0;

	// Controls.
	int countdown_ = 0;
	bool snap_ = true;
	float speedTarget_ = 1.f, glide_ = 1.f, voctTarget_ = 0.f, voct_ = 0.f, voctStep_ = 0.f;
	float startK_ = 0.f, startCv_ = 0.f, lengthK_ = 1.f, smearK_ = 0.f;
	float add_ = 1.f, keep_ = 0.75f, dubLevel_ = 1.f;
	float dryT_ = 0.f, wetT_ = 1.f, inGain_ = 0.f, wet_ = 1.f;
	int dir_ = 1;
	float speedNow_ = 1.f;

	// The window, in frames of the tape being read.
	bool winValid_ = false, wrap_ = false;
	double len_ = 0.0, S_ = 0.0, L_ = 0.0, m_ = 0.0, o_ = 0.0, turnF_ = 0.0, turnB_ = 0.0, originB_ = 0.0;

	// Voices and the write head.
	eddy::Voices voices_;
	Pending pend_;
	double head_ = 0.0;
	float playGain_ = 0.f, holdGain_ = 1.f, dubGain_ = 0.f, wearGain_ = 0.f;
	bool wroteTick_ = false;
	uint32_t seed_;
	float rate_ = 48000.f, wearK_ = 0.f;
	bool wearOn_ = false;
	eddy::WriteGuard guard_;   // one DC blocker for every voice's writes
	std::unique_ptr<float[]> pre_;
};

} // namespace rka

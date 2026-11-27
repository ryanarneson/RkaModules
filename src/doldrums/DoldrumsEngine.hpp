#pragma once
// Doldrums' engine: the tape (Ring.hpp), the read point (Pass.hpp), the
// tables (Tables.hpp) and the grain (Grain.hpp) tied to a frame of params and
// inputs, a sample at a time. Rack-free, header-only, C++11; Doldrums.cpp
// only maps params, ports, lights and the menu onto it.
//
// Each sample: the inputs, the Freeze gate and TRIG (NaN and inf read as
// 0 V; IN R unpatched takes IN L); Freeze = the button or the gate (Schmitt 0.1 V / 1 V);
// DoldrumsFrame::restart (the module raises it for a press or a drag's
// move in the tape window: the user's ruling "a press plays from there")
// taken as a TRIG edge, latched with TRIG to the next boundary; the input
// written to the tape (faded over 5 ms at Freeze's edges, Ring.hpp); TRIG's
// rising edge; the pace from Stretch and its CV; then at most one piece of
// staged work (below); the overlap's next sample; RUN; Mix (equal power,
// smoothed by a straight ramp over 10 ms of full travel, exact at 0 and 1)
// and the soft bound (SoftBound.hpp; the identity up to 8 V).
//
// Staging. Grain boundaries come a hop (half a grain) apart. On the
// boundary's sample: Tables::swapIfReady, the grain size (below), then
// PassLogic::step with the edges latched since the last boundary (Freeze
// rising, Freeze falling, TRIG; each reported once, none lost; of Freeze's
// two, only the one that leaves Freeze as it is at the boundary, so a press
// and release within a hop reads as the release), then the slice copied from
// the tape (Ring::copy, by absolute index) and Grain::begin. Then the nine
// Grain::Steps, one every stepGap(rate) samples (`gap`: 64 at 48 kHz, 128 at
// 96 kHz), and Overlap::add a gap after the last: ten pieces at a gap, two
// gaps, ... ten gaps after the boundary. So a grain is first heard
// latencyFrames() = 10 gaps after its boundary (640 samples, 13.3 ms at
// 48 kHz; 1280 at 96 kHz): the same every grain, whatever the grain size, and
// the same whichever way the caller batches its calls (nothing here knows the
// time but its own count). Ten gaps fit in the shortest hop at any rate in
// [8 kHz, 768 kHz] but the lowest: where they would not (Short at 8 kHz) the
// pieces are spaced (hop - 1) / 10 apart instead. Half way between two gaps
// (gap / 2, 3 gap / 2, ... through the hop) the tables are fed: the spec
// from the frame (Shift, Blur, Tilt, Harmonics after CV, Root and every
// V/OCT channel, Partials, Width, at the grain size wanted), Tables::start
// (which does nothing if the spec is what is live or on the way: Harmonics at
// 0 clears the comb's settings, so a moving V/OCT starts nothing), and
// Tables::work with a budget of 32 gap units (2048 at 48 kHz, 4096 at
// 96 kHz; measured 2026-11-26 on the development Mac at -O3, Long grain, a
// worst spec: 4 to 8 us a call on average, 29 us the worst call). A worst
// rebuild (every table, 16 roots of 32 partials: 512 + 22 bins units) takes
// 177 of the 256 calls in a hop (about 0.7 hop at any grain size and rate:
// the units and the calls both scale with the hop), so it is done inside the
// hop it was begun in and swaps in at the next boundary; a change of the controls
// reaches the sound at the next boundary or the one after (the one after if
// it came late in a hop, or while a rebuild was under way). On any one sample
// at most one of these runs: the boundary (a pass step and a copy of the
// slice), one Grain step (one transform or one pass over the bins), the add
// (one pass over the grain) or one work() call. staged() says which.
//
// The grain size. The tables are made for one size at a time (Tables::apply
// works on live().bins), so the grain size heard is the one whose bins the
// live tables have, and it changes only at a boundary where a set made for
// the new size swaps in. A change of Grain in the menu starts a rebuild at
// the new size; grains go on at the old size, with the old tables, until it
// swaps in; at that boundary the pass is told (grainChanged) and the grain
// begins at the new size, so no grain is ever given tables of another size.
// The two sizes overlap for one grain (a bump of up to about 3 dB, no
// click). After setSampleRate no grain is begun until a set made at the new
// rate is live (Tables::allocate leaves a rest set of the largest size and
// 48 kHz); until then a boundary is tried every gap, so the first grain
// begins within a few gaps. Held, when the held slice is no longer on the
// tape at the new size (recording; with a margin of four Long grains for
// the time a rebuild takes), the tables stay at the held slice's size until
// held is let go (Pass.hpp: the held slice keeps its size). Should a grain
// still be held with take false at a size its buffers do not hold (Freeze
// let go during that rebuild, so the place left the tape after the rebuild
// began), the held buffers are laid down at their own size and the
// boundaries keep their hop, without the tables (Grain.hpp skips a table of
// another size), until the tables, asked for at the held size again, swap
// back in: a grain or two without Shift, Blur, Tilt and the comb, dissolved.
//
// While recording (DoldrumsFrame::follow, the menu): Drift or Follow, read
// by the pass at each boundary (Pass.hpp); a change reaches the sound at the
// next boundary. Following, the output is the input late by Position's lag
// and latencyFrames(), within a grain (the slice ends at the anchor),
// whatever Stretch says; Stretch takes effect at Freeze.
// DoldrumsState::following says so for the window.
//
// RUN follows what is heard. It rises on the sample a grain is added to the
// overlap while it is low (a pass's first grain: a trigger's, a freeze's, the
// first after Initialize). A grain that begins a pass while RUN is high
// (go round's return, a trigger in mid pass, a trigger while the last grain
// of a stopped pass is still fading) drops it for runDipFrames (1 ms) from
// that grain's first sample. It falls when the last grain added has been
// heard to its end (a grain after the last one's add): in stop, a grain
// after the stopped pass's last grain began. While held a grain is added
// every hop, so it stays high. Eddy's REC gate (docs/specs/2026-11-21-eddy-
// design.md, "Rec") acts on edges and throws away a recording under 50 ms
// that the gate starts and ends: every high stretch here lasts at least a hop
// less 1 ms (92 ms at Short, 44.1 kHz), so each recording RUN gates stands,
// and a dip ends one recording and starts the next.
//
// Rates and buffers. allocate(rate) makes every buffer for that rate: the
// tape (30 s), a GrainBuffers for each of the three grain sizes, the tables
// for the largest, the slice copies; the overlap is made once, for the
// largest grain any rate has (Grain.hpp's kMaxGrain), and kept. It is slow and
// must not run while process() does (Rack holds the engine's lock round
// onSampleRateChange); at the rate it already holds it does nothing. The
// engine then makes no sound of its own until setSampleRate(rate) adopts it:
// the tape starts again from silence (the recording fading in from 0:
// Ring::fadeIn, so no step is written onto it), the pass starts from reset,
// the tables are rebuilt at the new rate, Mix starts where its knob is and
// RUN starts low. A frozen tape is not lost: the ring it was on
// is kept as the source of a conversion the worker makes (below), and until
// that arrives the new tape is silent and stands frozen (its 5 ms fade-out is
// written as silence). What the overlap already holds plays out, so the wet
// sound fades with its last grains rather than stopping. setSampleRate at the
// running rate, or at one allocate has not made, does nothing. Rates outside
// [8 kHz, 768 kHz] are refused, and so is a new rate while fewer than two of
// the slots for rings handed back are free (the worker has stopped
// collecting them).
//
// reset() (Initialize): a silent tape and a new pass; the params are the
// frame's, so their defaults are the module's. The grains already laid down
// play out (no click), and RUN stays high until they have been heard (in
// stop, which waits for TRIG after reset, it then falls; in go round the new
// pass's first grain dips it, as any new pass under a high RUN does). Mix
// moves to its new value over its ramp, from where it was. The recording
// fades in from 0 (Ring::fadeIn), so no step is written onto the silent
// tape; frozen, the tape stays silent and still. The seed is kept; seed()
// sets it. A tape on its way is no longer wanted.
//
// Edges. After setSampleRate and reset, the first sample arms Freeze's and
// TRIG's detectors from its levels and reports no edge: a patch opened with
// Freeze on, or with a high cable at TRIG, has not just been pressed or
// triggered (Rack's own SchmittTrigger starts high for the same reason).
// Opened frozen, the pass starts as after any reset (Pass.hpp): in go round
// from the anchor, in stop waiting for a trigger unless there is nothing to
// travel through (then held).
//
// Tapes handed in (TapeStore.hpp is the worker's side and says what the
// module does). The engine owns every ring it holds and frees none but in its
// destructor: the live ring, the source of a conversion, one offered and not
// yet looked at, and those handed back and not yet collected (eight slots).
// Every ring it lets go of comes back through retired(), and only the worker
// frees rings, so a ring the worker is reading (a source it converts, the
// live ring it holds still to save) cannot be freed under it.
// - Wanted. The engine says which tape it is waiting for, if any
//   (tapeWanted: a TapeJob, with a ticket): the patch's stored tape, after
//   expectStored() (the module, when a patch opens with a stored tape); or,
//   after allocate at a new rate while frozen, the frozen tape (`from`, the
//   ring it was on, kept as the source) converted from its rate to the new
//   one. A tape on its way is asked for again, under a new ticket, at each
//   new rate (from the same source: never a conversion of a conversion). It
//   stops being wanted when it is taken, on any sample that is not frozen,
//   at reset, and when the worker gives up on it (giveUp: a bad file).
//   tapePending() says whether one is wanted, for the save, which leaves the
//   file alone meanwhile. (Any sample not frozen, not only a frozen-to-thawed
//   one: a patch opened with Freeze on whose first sample is already thawed,
//   the module bypassed and Freeze clicked off, saw no thaw, so the wait
//   stood for ever and every save left an older file in place: the final
//   review's I1.) The cancel cannot let a stale tape go live: it is a CAS
//   from the ticket the audio thread read to 0, so it clears only that wait,
//   never kIssuing or a newer ticket written meanwhile (which a later
//   thawed sample clears in turn); a take happens on the audio thread too,
//   and only frozen with the offer's ticket equal to want_, so no take can
//   fall between a thawed sample's load and its CAS; and an offer made for
//   a cleared ticket is refused (w == 0 or another ticket). A worker that
//   read the job before the cancel offers a ring that is then handed back.
//   The cost is one relaxed load a sample while thawed.
// - Offered. offer(ring, ticket) hands one in; the audio thread looks at it
//   on its next sample and takes it only if its ticket is the one wanted
//   now, the engine is frozen, and its length is this rate's tape; otherwise
//   it is handed back at once, unused. So a ring made for a rate no longer in
//   force, or for a wait that a thaw, a reset or a newer request ended, never
//   goes live. On a take the new ring is published (live(), tapesTaken())
//   before the wait is cleared, so whoever sees nothing wanted any more also
//   sees the new ring; between the two stores a reader may see the new ring
//   with the wait still set, which only makes a save leave the file alone
//   once more (tapePending), shows the worker a ticket it has already made
//   (it does not make it again), or refuses a hold; a second offer for that
//   ticket is looked at on a later sample, after the wait is cleared, and is
//   refused. (The ticket is why offer takes two arguments where the plan
//   wrote one: a ring's length tells its rate, not which wait it was made
//   for.) A taken ring replaces the live one (handed back), the source of its
//   conversion is handed back on the next sample (no longer wanted), tapesTaken() counts it and live() shows it
//   (release; the worker acquires).
// - Taken. A swap is not a restart. The new tape's pass begins from reset at
//   the next boundary (Pass.hpp: in go round from the anchor; in stop
//   waiting for a trigger unless held), and the read point and marks are of
//   the new tape from then on. The grain in flight is laid and the overlap
//   plays out, so the old sound dissolves into the new one through the
//   grains' windows (no click); RUN follows what is heard as always (a new
//   pass begun while RUN is high dips it); Mix goes on; the edge detectors
//   are not re-armed: Freeze has stayed on throughout (a tape is taken only
//   frozen), so no Freeze edge is seen, and a TRIG latched just before is
//   kept. The ring arrives frozen and faded out (Ring::fill), so nothing is
//   written onto it while Freeze stays on.
// - Held still. For the save, the worker may hold the live ring still
//   (holdStill, then letGo): granted only while it stands still (frozen and
//   faded out) with no tape on its way, and while held nothing writes it: a
//   thaw meanwhile is put off until letGo (the recording starts that much
//   later, fading in as ever). The hold is a pair of seq_cst flags (the
//   worker sets hold_ then reads still_; the audio thread, before it writes
//   a ring it said was still, clears still_ then reads hold_), so either the
//   hold is refused or the write waits: never both. allocate, setSampleRate
//   and reset grant no hold while they run and wait for one in force.
// - frozenSince() numbers the frozen tape for the save: new at every Freeze
//   rising edge, at reset, and with a new silent tape at a rate change; the
//   same through a tape handed in and a conversion (the same tape at another
//   rate).
//
// Threads: allocate, setSampleRate, reset and seed off the audio thread, or
// with process() held off; expectStored on the UI thread, one at a time with
// allocate, setSampleRate and reset; tapeWanted, tapePending, live,
// tapesTaken and frozenSince on any thread; offer, retired, giveUp, holdStill
// and letGo on one worker thread; process, ring() and the other read-only calls on the audio
// thread. process() does no allocation, lock, free or libm (FastMath.hpp).
#include "Grain.hpp"
#include "Laws.hpp"
#include "Pass.hpp"
#include "Ring.hpp"
#include "SoftBound.hpp"
#include "Tables.hpp"
#include "hadal/Random.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace rka { namespace doldrums {

struct DoldrumsFrame {
	enum Param { STRETCH, STRETCH_ATT, POSITION, POSITION_ATT, HARMONICS, HARMONICS_ATT, BLUR, ROOT, TILT,
	             SHIFT, PARTIALS, WIDTH, FREEZE, MIX, kParams };
	// The defaults (the plan's Global Constraints).
	// Position 0.9 (about 3 s back): the user's decision at 2.0.46, so that a
	// freeze at the defaults leaves Stretch tape to act on (was 1, now).
	float params[kParams] = {0.5f, 0.f, 0.9f, 0.f, 0.f, 0.f, 0.f, -24.f, 0.f, 0.f, 8.f, 0.393f, 0.f, 1.f};
	float stretchCv = 0.f, positionCv = 0.f, harmonicsCv = 0.f, freezeGate = 0.f, trig = 0.f;
	float inL = 0.f, inR = 0.f;
	bool inRPatched = false;
	const float* voct = nullptr;   // voctChannels values
	int voctChannels = 0;          // 0: unpatched (one root, 0 V); at most 16 are read
	int grainSize = GRAIN_MEDIUM;  // the menu: GrainSize
	bool wide = false;
	bool stopAtEnd = false;
	bool follow = false;           // the menu's While recording: false Drift (the default), true Follow (Pass.hpp)
	bool restart = false;          // a press in the window: a new pass from the anchor, as a TRIG edge
};

struct DoldrumsResult {
	float outL = 0.f, outR = 0.f, run = 0.f;   // volts; RUN 0 / 10
	bool frozen = false;
};

// Read by the view (Task 10); plain values, as of the last process().
struct DoldrumsState {
	long long now = 0, capacity = 0, anchor = 0, end = 0;   // absolute frames (Ring.hpp); end: the last slice's
	int grain = 0;                                          // the grain size heard, frames
	bool frozen = false, held = false, stopped = false;
	bool atEnd = false;   // held because there is nothing to travel through (frozen, the anchor within a grain of now)
	bool following = false;   // While recording: Follow, recording and sounding: the read point on the anchor whatever Stretch says (Pass.hpp)
	double pace = 0.0;
};

struct DoldrumsEngine {
	// What staged() reports: the work done on the last sample, one at most.
	enum Staged {
		STAGED_BOUNDARY = 1,
		STAGED_STEP0 = 2,                            // Grain step s is STAGED_STEP0 << s
		STAGED_ADD = STAGED_STEP0 << Grain::kSteps,
		STAGED_TABLES = STAGED_ADD << 1
	};
	static constexpr int kPieces = Grain::kSteps + 1;   // the steps and the add
	static constexpr float kMinRate = 8000.f, kMaxRate = 768000.f;

	DoldrumsEngine() { rng_.seed(0); }
	DoldrumsEngine(const DoldrumsEngine&) = delete;
	DoldrumsEngine& operator=(const DoldrumsEngine&) = delete;

	~DoldrumsEngine() {
		if (ring_ != &none_) delete ring_;
		delete source_;
		delete incoming_.load(std::memory_order_acquire);
		for (int i = 0; i < kRetireSlots; i++) delete retired_[i].load(std::memory_order_acquire);
	}

	bool allocate(float rate) {
		if (!(rate >= kMinRate && rate <= kMaxRate)) return false;
		if (rate == allocRate_) return true;
		if (freeSlots() < 2) return false;   // the worker has not collected (see "Tapes handed in")
		Ring* fresh = new Ring;
		if (!fresh->allocate(tapeFrames(rate))) {
			delete fresh;
			return false;
		}
		const bool wasRunning = running_;
		running_ = false;
		inFlight_ = false;
		allocRate_ = 0.f;
		for (int i = 0; i < 3; i++) frames_[i] = grainFrames(GrainSize(i), rate);
		bool ok = true;
		for (int i = 0; i < 3; i++) ok = ok && buffers_[i].allocate(frames_[i]);
		if (ok) {
			tables_.allocate(frames_[GRAIN_LONG] / 2);
			sliceL_.assign(size_t(frames_[GRAIN_LONG]), 0.f);
			sliceR_.assign(size_t(frames_[GRAIN_LONG]), 0.f);
			if (!overlapMade_) overlapMade_ = overlap_.allocate(kMaxGrain);
		}
		exclude();
		changeRing(fresh, rate, wasRunning);
		admit();
		if (!ok) return false;
		allocRate_ = rate;
		return true;
	}

	void setSampleRate(float rate) {
		if (rate != allocRate_ || (running_ && rate == rate_)) return;
		exclude();
		rate_ = rate;
		gap_ = stepGap(rate);
		dip_ = runDipFrames(rate);
		budget_ = 32 * gap_;
		mixStep_ = 1.f / (0.01f * rate);
		fade_ = int(detail::roundHalfUp(0.005 * double(rate)));
		ring_->setFadeFrames(fade_);
		ring_->clear();
		if (frozen_)   // frozen, the new tape is silent: its fade-out is written now, as silence
			for (int i = 0; i < fade_; i++) ring_->write(0.f, 0.f, true);
		else
			ring_->fadeIn();   // the recording starts again from 0: no step onto the tape
		if (want_.load(std::memory_order_relaxed) == 0) bumpEpoch();   // a new (silent) tape, unless one is on its way
		primed_ = false;
		active_ = GRAIN_MEDIUM;
		restart(true);
		running_ = true;
		admit();
	}

	void seed(uint32_t s) { rng_.seed(s); }

	void reset() {
		exclude();
		want_.store(0, std::memory_order_release);   // a tape on its way is not wanted now
		ring_->clear();
		ring_->fadeIn();   // recording, it starts again from 0; frozen, nothing more is written
		bumpEpoch();
		restart(false);
		admit();
	}

	void process(const DoldrumsFrame& f, DoldrumsResult& out) {
		typedef DoldrumsFrame F;
		const float inL = finite(f.inL), inR = f.inRPatched ? finite(f.inR) : inL;
		const float gateV = finite(f.freezeGate), trigV = finite(f.trig);   // +inf is 0 V, not high
		if (!armed_) trigHigh_ = trigV >= 1.f;   // the first sample after a restart: levels, no edges
		const bool gate = schmitt(gateV, gateHigh_);
		const bool frozen = f.params[F::FREEZE] >= 0.5f || gate;
		if (frozen && !frozen_ && armed_) {
			rose_ = true;
			bumpEpoch();
		}
		if (!frozen && frozen_ && armed_) fell_ = true;   // a thaw (not the first sample's arming)
		if (!frozen) cancelWant();   // thawed: a tape on its way is not wanted (see "Wanted")
		const bool opened = !armed_ && frozen && !frozen_;   // the first sample, Freeze already on
		frozen_ = frozen;
		const bool wasTrig = trigHigh_;
		if (schmitt(trigV, trigHigh_) && !wasTrig) trig_ = true;
		if (f.restart) trig_ = true;   // a press in the window: as a trigger (latched, so a drag's presses in a hop are one)
		armed_ = true;
		pace_ = paceFromDial(cvValue(f.params[F::STRETCH], f.params[F::STRETCH_ATT], f.stretchCv));
		staged_ = 0;
		if (running_) {
			handOff(frozen);
			// Opened frozen (the first sample after setSampleRate or reset, Freeze
			// already on): the tape is the patch's stand-in, so its fade-out is
			// written as silence, as setSampleRate does for a frozen tape, and no
			// live input lands on it (a patch saved without its tape opens silent).
			if (opened && !stillPub_)
				for (int i = 0; i < fade_; i++) ring_->write(0.f, 0.f, true);
			record(inL, inR, frozen);
			stage(f);
		}

		float wetL, wetR;
		overlap_.next(wetL, wetR);
		out.run = runOn_ && dipLeft_ == 0 ? 10.f : 0.f;
		if (dipLeft_ > 0) dipLeft_--;
		if (soundLeft_ > 0 && --soundLeft_ == 0) runOn_ = false;

		const float m = detail::clampLo(f.params[F::MIX], 0.f, 1.f);
		if (snapMix_) {
			mix_ = m;
			snapMix_ = false;
		} else if (mix_ < m)
			mix_ = mix_ + mixStep_ < m ? mix_ + mixStep_ : m;
		else if (mix_ > m)
			mix_ = mix_ - mixStep_ > m ? mix_ - mixStep_ : m;
		float dry, wet;
		mixGains(mix_, dry, wet);
		out.outL = bound(mixed(inL, finite(wetL), dry, wet));
		out.outR = bound(mixed(inR, finite(wetR), dry, wet));
		out.frozen = frozen;

		state_.now = ring_ ? ring_->now() : 0;
		state_.capacity = ring_ ? ring_->capacity() : 0;
		state_.frozen = frozen;
		state_.pace = pace_;
	}

	const DoldrumsState& state() const { return state_; }
	Ring& ring() { return *ring_; }
	int latencyFrames() const { return kPieces * spacing_; }
	double stretchPace() const { return pace_; }
	int staged() const { return staged_; }              // for the tests and the bench
	long long tableSwaps() const { return swaps_; }     // likewise

	// ---- Tapes handed in (see "Tapes handed in" above; TapeStore.hpp is the worker's side).

	// What the worker is to make: the stored tape read (from null) or `from`
	// converted, at toRate, for `ticket`.
	struct TapeJob {
		uint32_t ticket = 0;
		const Ring* from = nullptr;
		float fromRate = 0.f, toRate = 0.f;
	};
	static constexpr int kRetireSlots = 8;
	static constexpr uint32_t kIssuing = 0xffffffffu;   // want_ while a job's fields change (never a ticket)

	// The UI thread (one thread at a time with allocate, setSampleRate and
	// reset): the patch's stored tape is to be read and handed in. Its ticket.
	uint32_t expectStored() { return issue(nullptr, 0.f, allocRate_ > 0.f ? allocRate_ : rate_); }
	// Any thread: the tape wanted now, if one is; false if none.
	bool tapeWanted(TapeJob& j) const {
		for (int attempt = 0; attempt < 8; attempt++) {
			const uint32_t a = want_.load(std::memory_order_acquire);
			if (a == 0) return false;
			if (a == kIssuing) continue;
			const Ring* from = jobFrom_.load(std::memory_order_acquire);
			const float fromRate = jobFromRate_.load(std::memory_order_acquire);
			const float toRate = jobToRate_.load(std::memory_order_acquire);
			if (want_.load(std::memory_order_acquire) == a) {
				j.ticket = a;
				j.from = from;
				j.fromRate = fromRate;
				j.toRate = toRate;
				return true;
			}
		}
		return false;
	}
	// Any thread (the save, TapeStore.hpp's SaveFacts::tapePending): a tape
	// is on its way (the stored one being read, or a frozen one converted),
	// asked for and not yet taken, refused for good or given up. True too
	// for the moment a request is being made or asked for again.
	bool tapePending() const { return want_.load(std::memory_order_acquire) != 0; }
	// The worker: the tape for `ticket` cannot be made (a bad file, no
	// memory): the engine stops waiting for it, unless a newer request has
	// taken its place.
	void giveUp(uint32_t ticket) {
		uint32_t t = ticket;
		if (t != 0 && t != kIssuing) want_.compare_exchange_strong(t, 0, std::memory_order_acq_rel);
	}
	// The worker: a tape made for `ticket`. False (and still the caller's)
	// if one offered before is not yet taken or refused; true: the engine's,
	// until it comes back through retired().
	bool offer(Ring* fresh, uint32_t ticket) {
		if (!fresh || incoming_.load(std::memory_order_acquire)) return false;
		offerTicket_.store(ticket, std::memory_order_relaxed);
		incoming_.store(fresh, std::memory_order_release);
		return true;
	}
	// The worker: a ring the engine is done with (the caller's to delete), or null.
	Ring* retired() {
		for (int i = 0; i < kRetireSlots; i++)
			if (Ring* r = retired_[i].exchange(nullptr, std::memory_order_acq_rel)) return r;
		return nullptr;
	}
	// Any thread: the ring live now (null before allocate), and how many
	// offers have gone live. The ring stays valid until it comes back
	// through retired().
	const Ring* live() const { return livePub_.load(std::memory_order_acquire); }
	uint32_t tapesTaken() const { return taken_.load(std::memory_order_acquire); }
	// Any thread: the frozen tape's number, for the save (TapeStore.hpp's
	// SaveFacts::frozenSince): a new one at every freeze, reset and new
	// silent tape; the same through a tape handed in. Never 0.
	uint32_t frozenSince() const { return epoch_.load(std::memory_order_acquire); }
	// The worker: the live ring, held still (frozen, faded out, nothing on
	// its way), or null; while held nothing writes it. letGo() soon after.
	const Ring* holdStill(uint32_t* since = nullptr) {
		hold_.store(true, std::memory_order_seq_cst);
		if (blocked_.load(std::memory_order_seq_cst) || !still_.load(std::memory_order_seq_cst) ||
		    want_.load(std::memory_order_seq_cst) != 0) {
			hold_.store(false, std::memory_order_release);
			return nullptr;
		}
		if (since) *since = epoch_.load(std::memory_order_acquire);
		return livePub_.load(std::memory_order_acquire);
	}
	void letGo() { hold_.store(false, std::memory_order_release); }

private:
	Ring none_;           // before allocate
	Ring* ring_ = &none_; // the live ring (the engine's while live)
	GrainBuffers buffers_[3];
	Tables tables_;
	Grain grain_;
	Overlap overlap_;
	PassLogic pass_;
	rka::hadal::Random rng_;
	std::vector<float> sliceL_, sliceR_;
	DoldrumsState state_;

	// Tapes handed in. The job (want_ and the three fields, a seqlock: want_
	// is kIssuing while they change, so a save never sees "nothing pending"
	// in the middle of a request asked for again) is written by issue() off the audio thread and
	// cleared by the audio thread when taken or not wanted. source_ (with its
	// rate and the ticket it is wanted for) is written by allocate with
	// process held off and handed back by the audio thread.
	std::atomic<uint32_t> want_{0};
	std::atomic<const Ring*> jobFrom_{nullptr};
	std::atomic<float> jobFromRate_{0.f}, jobToRate_{0.f};
	uint32_t tickets_ = 0;
	Ring* source_ = nullptr;
	float sourceRate_ = 0.f;
	uint32_t sourceTicket_ = 0;
	std::atomic<Ring*> incoming_{nullptr};
	std::atomic<uint32_t> offerTicket_{0};
	std::atomic<Ring*> retired_[kRetireSlots] = {};
	std::atomic<const Ring*> livePub_{nullptr};
	std::atomic<uint32_t> taken_{0}, epoch_{1};
	// The hold: hold_ the worker's, still_ the audio thread's (the live ring
	// stands still: frozen and faded out), blocked_ set while allocate,
	// setSampleRate or reset change the ring (no hold is granted meanwhile).
	std::atomic<bool> hold_{false}, still_{false}, blocked_{false};
	bool stillPub_ = false;   // the audio thread's copy of still_
	int fade_ = 240;

	float allocRate_ = 0.f, rate_ = 0.f;
	bool running_ = false, overlapMade_ = false, primed_ = false;
	int frames_[3] = {0, 0, 0};
	int gap_ = 64, spacing_ = 64, dip_ = 48, budget_ = 2048;
	float mixStep_ = 1.f / 480.f;

	// The staging.
	int s_ = 0;              // samples since the boundary
	int hop_ = 64;           // the boundary comes every hop_ (every gap until primed)
	int active_ = GRAIN_MEDIUM;
	int passGrain_ = 0;      // the grain size the pass was last stepped at
	int heldSize_ = -1;      // the size whose buffers hold the last loudnesses taken
	bool inFlight_ = false, flightNewPass_ = false, flightWide_ = false;
	int flightFrames_ = 0;
	PassOut last_{};
	long long swaps_ = 0;
	int staged_ = 0;

	// Edges and gates.
	bool gateHigh_ = false, trigHigh_ = false, frozen_ = false, rose_ = false, fell_ = false, trig_ = false;
	bool armed_ = false;   // the edge detectors have seen a sample since the restart
	double pace_ = 1.0;

	// RUN.
	bool runOn_ = false;
	int dipLeft_ = 0;
	int soundLeft_ = 0;

	// Mix.
	float mix_ = 1.f;
	bool snapMix_ = true;

	static float finite(float x) { return x - x == 0.f ? x : 0.f; }
	// The soft bound, after holding to +-100 V (no NaN reaches it).
	static float bound(float x) { return softBound(x > -100.f ? (x < 100.f ? x : 100.f) : x == x ? -100.f : 0.f); }
	// dry d + wet w; at the ends of Mix the other side is left out, so the dry
	// path is the input to the bit (a -0 input stays -0).
	static float mixed(float d, float w, float dg, float wg) {
		if (wg == 0.f) return d * dg;
		if (dg == 0.f) return w * wg;
		return d * dg + w * wg;
	}
	// A Schmitt trigger at 0.1 V and 1 V; NaN reads as low.
	static bool schmitt(float v, bool& high) {
		if (high) {
			if (!(v >= 0.1f)) high = false;
		} else if (v >= 1.f)
			high = true;
		return high;
	}

	// ---- Tapes handed in: the engine's side.

	int freeSlots() const {
		int n = 0;
		for (int i = 0; i < kRetireSlots; i++) n += retired_[i].load(std::memory_order_relaxed) == nullptr;
		return n;
	}
	// Hands a ring back to the worker; false if every slot is full.
	bool retire(Ring* r) {
		if (!r || r == &none_) return true;
		for (int i = 0; i < kRetireSlots; i++)
			if (!retired_[i].load(std::memory_order_relaxed)) {
				retired_[i].store(r, std::memory_order_release);
				return true;
			}
		return false;
	}
	void bumpEpoch() {
		uint32_t e = epoch_.load(std::memory_order_relaxed) + 1;
		if (e == 0) e = 1;
		epoch_.store(e, std::memory_order_release);
	}
	// Off the audio thread, one at a time: a new job, under a new ticket.
	uint32_t issue(const Ring* from, float fromRate, float toRate) {
		want_.store(kIssuing, std::memory_order_relaxed);   // the fields change under this (a reader that sees a new field sees it)
		jobFrom_.store(from, std::memory_order_release);
		jobFromRate_.store(fromRate, std::memory_order_release);
		jobToRate_.store(toRate, std::memory_order_release);
		if (++tickets_ == kIssuing) tickets_ = 1;
		want_.store(tickets_, std::memory_order_release);
		return tickets_;
	}
	void cancelWant() {
		uint32_t t = want_.load(std::memory_order_relaxed);
		if (t && t != kIssuing) want_.compare_exchange_strong(t, 0, std::memory_order_acq_rel);
	}
	// While the ring changes off the audio thread: no hold is granted, and one
	// in force is waited for (the worker holds only for a copy).
	void exclude() {
		blocked_.store(true, std::memory_order_seq_cst);
		while (hold_.load(std::memory_order_seq_cst)) std::this_thread::yield();
	}
	void admit() { blocked_.store(false, std::memory_order_release); }

	// allocate: `fresh` (silent, at `rate`) goes live. The ring it replaces is
	// handed back, or kept as the source of a conversion if it holds a frozen
	// tape; a tape already on its way is asked for again at the new rate.
	void changeRing(Ring* fresh, float rate, bool wasRunning) {
		Ring* old = ring_ != &none_ ? ring_ : nullptr;
		const uint32_t w = want_.load(std::memory_order_acquire);
		const bool converting = w != 0 && source_ && sourceTicket_ == w;
		if (source_ && !converting) {
			retire(source_);
			source_ = nullptr;
		}
		if (w != 0) {
			const uint32_t t = issue(converting ? source_ : nullptr, converting ? sourceRate_ : 0.f, rate);
			if (converting) sourceTicket_ = t;
			retire(old);
		} else if (old && wasRunning && frozen_) {
			source_ = old;
			sourceRate_ = rate_;
			sourceTicket_ = issue(old, rate_, rate);
		} else
			retire(old);
		still_.store(false, std::memory_order_seq_cst);
		stillPub_ = false;
		ring_ = fresh;
		livePub_.store(fresh, std::memory_order_release);
	}

	// The audio thread, each sample: a source no longer wanted goes back; an
	// offer is taken if it is for the tape wanted now, frozen, at this tape's
	// length, else handed back at once.
	void handOff(bool frozen) {
		if (source_ && want_.load(std::memory_order_acquire) != sourceTicket_ && retire(source_)) source_ = nullptr;
		Ring* in = incoming_.load(std::memory_order_acquire);
		if (!in || freeSlots() < 2) return;
		const uint32_t ticket = offerTicket_.load(std::memory_order_relaxed);
		incoming_.store(nullptr, std::memory_order_release);   // only this thread empties it
		uint32_t w = want_.load(std::memory_order_acquire);
		if (w == 0 || ticket != w || !frozen || in->capacity() != ring_->capacity()) {
			retire(in);
			return;
		}
		in->setFadeFrames(fade_);
		Ring* old = ring_;
		ring_ = in;
		// The new ring is published, and counted, BEFORE the wait is cleared:
		// a thread that reads the 0 this CAS writes (it is a release RMW, the
		// reader's load an acquire or seq_cst one) is synchronised with it,
		// so the two stores above happen before its later loads of livePub_
		// and taken_, which must then see the new ring and the new count.
		// (The other way round, holdStill could see no tape on its way and
		// still be handed the outgoing stand-in.)
		livePub_.store(in, std::memory_order_release);
		taken_.fetch_add(1, std::memory_order_release);
		want_.compare_exchange_strong(w, 0, std::memory_order_acq_rel);
		retire(old);   // (the source of its conversion goes back next sample: no longer wanted)
		// The new tape's pass starts from the anchor (Pass.hpp's reset); the
		// grain in flight, the overlap, RUN, Mix and the edge detectors go on.
		pass_.reset();
		last_ = PassOut{};
		heldSize_ = -1;
		state_.anchor = state_.end = in->now();
		state_.held = state_.stopped = state_.atEnd = state_.following = false;
	}

	// The audio thread: the input onto the tape, unless the worker holds it
	// still (then a thaw is put off until it lets go).
	void record(float l, float r, bool frozen) {
		Ring& g = *ring_;
		if (stillPub_) {
			if (frozen && g.fadedOut()) return;
			still_.store(false, std::memory_order_seq_cst);
			if (hold_.load(std::memory_order_seq_cst)) {
				still_.store(true, std::memory_order_seq_cst);
				return;
			}
			stillPub_ = false;
		}
		g.write(l, r, frozen);
		if (frozen && g.fadedOut()) {
			still_.store(true, std::memory_order_release);
			stillPub_ = true;
		}
	}

	// newRate: setSampleRate (RUN starts low, Mix at its knob); otherwise
	// reset(), under which the grains laid down play out with RUN high and
	// Mix moves on its ramp.
	void restart(bool newRate) {
		pass_.reset();
		inFlight_ = false;
		s_ = 0;
		hop_ = gap_;
		spacing_ = gap_;
		passGrain_ = 0;
		heldSize_ = -1;
		last_ = PassOut{};
		rose_ = fell_ = trig_ = false;
		armed_ = false;
		if (newRate) {
			runOn_ = false;
			dipLeft_ = soundLeft_ = 0;
			snapMix_ = true;
		}
		state_ = DoldrumsState{};
		state_.capacity = ring_ ? ring_->capacity() : 0;
		state_.now = ring_ ? ring_->now() : 0;
	}

	static int menuSize(const DoldrumsFrame& f) { return std::max(0, std::min(int(f.grainSize), 2)); }

	void stage(const DoldrumsFrame& f) {
		if (s_ == 0)
			boundary(f);
		else if (s_ % spacing_ == 0) {
			if (s_ / spacing_ <= kPieces) piece(s_ / spacing_);
		} else if (s_ % spacing_ == spacing_ / 2)
			feedTables(f);
		if (++s_ >= hop_) s_ = 0;
	}

	void boundary(const DoldrumsFrame& f) {
		typedef DoldrumsFrame F;
		staged_ |= STAGED_BOUNDARY;
		inFlight_ = false;
		if (tables_.swapIfReady()) swaps_++;
		const TableSpec& live = tables_.live();
		const int menu = menuSize(f);
		if (!primed_) {
			primed_ = live.rate == rate_ && live.bins == frames_[menu] / 2;
			if (!primed_) return;   // try again in a gap
		}
		// The size the live tables were made for (the menu's, if two sizes are one).
		int a = active_;
		if (frames_[a] / 2 != live.bins) {
			const int order[4] = {menu, 0, 1, 2};
			for (int i : order)
				if (frames_[i] / 2 == live.bins) {
					a = i;
					break;
				}
		}
		active_ = a;
		const int grain = frames_[a];
		hop_ = grain / 2;
		spacing_ = std::max(1, std::min(gap_, (hop_ - 1) / kPieces));
		state_.grain = grain;

		PassIn in;
		in.now = ring_->now();
		in.capacity = ring_->capacity();
		in.grain = grain;
		in.hop = hop_;
		in.position = cvValue(f.params[F::POSITION], f.params[F::POSITION_ATT], f.positionCv);
		in.pace = pace_;
		in.frozen = frozen_;
		in.freezeRose = rose_ && frozen_;
		in.freezeFell = fell_ && !frozen_;
		in.trig = trig_;
		in.stopAtEnd = f.stopAtEnd;
		in.follow = f.follow;
		in.grainChanged = passGrain_ != grain;
		rose_ = fell_ = trig_ = false;
		passGrain_ = grain;
		const PassOut o = pass_.step(in);
		last_ = o;
		state_.anchor = o.anchor;
		state_.end = o.end;
		state_.held = o.held;
		state_.stopped = o.stopped;
		// Held for want of tape ahead (Pass.hpp's "nothing to travel through").
		state_.atEnd = o.held && in.frozen && in.now - o.anchor < grain;
		state_.following = o.sound && in.follow && !in.frozen;
		if (!o.sound) return;

		int use = a;
		if (o.take) {
			ring_->copy(o.end, grain, sliceL_.data(), sliceR_.data());
			heldSize_ = a;
		} else {
			if (heldSize_ < 0) return;
			use = heldSize_;   // the buffers that hold the loudnesses
			if (frames_[use] != grain) {
				// Held at a size the tables have left (see the header): the
				// boundaries keep that size's hop until the tables come back to it.
				hop_ = frames_[use] / 2;
				spacing_ = std::max(1, std::min(gap_, (hop_ - 1) / kPieces));
				state_.grain = frames_[use];
			}
		}
		grain_.begin(buffers_[use], o.take ? sliceL_.data() : nullptr, o.take ? sliceR_.data() : nullptr, o.take);
		inFlight_ = true;
		flightFrames_ = frames_[use];
		flightNewPass_ = o.newPass;
		flightWide_ = f.wide;
	}

	// Piece j (1..kPieces): Grain step j - 1, then the add.
	void piece(int j) {
		if (!inFlight_) return;
		if (j <= Grain::kSteps) {
			grain_.step(Grain::Step(j - 1), tables_, rng_, flightWide_);
			staged_ |= STAGED_STEP0 << (j - 1);
			return;
		}
		overlap_.add(grain_.outL(), grain_.outR(), flightFrames_, 1.f);
		staged_ |= STAGED_ADD;
		inFlight_ = false;
		if (flightNewPass_ && runOn_) dipLeft_ = dip_;
		runOn_ = true;
		soundLeft_ = flightFrames_;
	}

	// Held, a slice is taken again at the new size only if it is still on the
	// tape there (as Pass.hpp decides), with room for a rebuild while recording.
	bool retakable(int size) const {
		const long long now = ring_->now(), cap = ring_->capacity();
		const int g = frames_[size];
		const long long margin = frozen_ ? 0 : g / 2 + 4LL * frames_[GRAIN_LONG];
		return g <= cap && last_.end <= now && last_.end >= now - cap + g + margin;
	}

	void feedTables(const DoldrumsFrame& f) {
		typedef DoldrumsFrame F;
		staged_ |= STAGED_TABLES;
		int size = menuSize(f);
		if (primed_ && last_.sound && last_.held && heldSize_ >= 0 && frames_[size] != frames_[heldSize_] && !retakable(size))
			size = heldSize_;
		TableSpec s;
		std::memset(&s, 0, sizeof(s));
		s.bins = frames_[size] / 2;
		s.rate = rate_;
		s.shiftRatio = shiftRatio(f.params[F::SHIFT]);
		s.blurOctaves = detail::clampLo(f.params[F::BLUR], 0.f, 1.f);
		s.tiltDbPerOct = tiltDbPerOctave(f.params[F::TILT]);
		s.harmonics = cvValue(f.params[F::HARMONICS], f.params[F::HARMONICS_ATT], f.harmonicsCv);
		if (s.harmonics > 0.f) {
			const int n = f.voct && f.voctChannels > 0 ? std::min(f.voctChannels, kMaxRoots) : 0;
			s.roots = n > 0 ? n : 1;
			for (int i = 0; i < s.roots; i++) s.rootHz[i] = rootHz(f.params[F::ROOT], n > 0 ? f.voct[i] : 0.f);
			s.partials = int(detail::clampLo(f.params[F::PARTIALS], 1.f, float(kMaxPartials)) + 0.5f);
			s.widthCents = widthCents(f.params[F::WIDTH]);
		}
		tables_.start(s);
		tables_.work(budget_);
	}
};

}} // namespace rka::doldrums

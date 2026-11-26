#pragma once
// Doldrums' engine: the tape (Ring.hpp), the read point (Pass.hpp), the
// tables (Tables.hpp) and the grain (Grain.hpp) tied to a frame of params and
// inputs, a sample at a time. Rack-free, header-only, C++11; Doldrums.cpp
// only maps params, ports, lights and the menu onto it.
//
// Each sample: the inputs, the Freeze gate and TRIG (NaN and inf read as
// 0 V; IN R unpatched takes IN L); Freeze = the button or the gate (Schmitt 0.1 V / 1 V); the input
// written to the tape (faded over 5 ms at Freeze's edges, Ring.hpp); TRIG's
// rising edge; the pace from Stretch and its CV; then at most one piece of
// staged work (below); the overlap's next sample; RUN; Mix (equal power,
// smoothed by a straight ramp over 10 ms of full travel, exact at 0 and 1)
// and the soft bound (SoftBound.hpp; the identity up to 8 V).
//
// Staging. Grain boundaries come a hop (half a grain) apart. On the
// boundary's sample: Tables::swapIfReady, the grain size (below), then
// PassLogic::step with the edges latched since the last boundary (Freeze
// rising, TRIG; each reported once, none lost), then the slice copied from
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
// the tape starts again from silence (a frozen one too, for now: its 5 ms
// fade-out is written as silence, so it stays silent until Freeze is let
// go; Task 11 converts a frozen tape on a worker), the pass starts from
// reset, the tables are
// rebuilt at the new rate. What the overlap already holds plays out, so the
// wet sound fades with its last grains rather than stopping. setSampleRate at
// the running rate, or at one allocate has not made, does nothing. Rates
// outside [8 kHz, 768 kHz] are refused.
//
// reset() (Initialize): a silent tape and a new pass; the params are the
// frame's, so their defaults are the module's. The grains already laid down
// play out (no click). The seed is kept; seed() sets it.
//
// Edges. After setSampleRate and reset, the first sample arms Freeze's and
// TRIG's detectors from its levels and reports no edge: a patch opened with
// Freeze on, or with a high cable at TRIG, has not just been pressed or
// triggered (Rack's own SchmittTrigger starts high for the same reason).
// Opened frozen, the pass starts as after any reset (Pass.hpp): in go round
// from the anchor, in stop waiting for a trigger unless there is nothing to
// travel through (then held).
//
// The ring is reached through a pointer (ring()), so that a later task can
// hand one in under a running engine.
//
// Threads: allocate, setSampleRate, reset and seed off the audio thread, or
// with process() held off; process and the read-only calls on it. process()
// does no allocation, lock, free or libm (FastMath.hpp).
#include "Grain.hpp"
#include "Laws.hpp"
#include "Pass.hpp"
#include "Ring.hpp"
#include "SoftBound.hpp"
#include "Tables.hpp"
#include "hadal/Random.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace rka { namespace doldrums {

struct DoldrumsFrame {
	enum Param { STRETCH, STRETCH_ATT, POSITION, POSITION_ATT, HARMONICS, HARMONICS_ATT, BLUR, ROOT, TILT,
	             SHIFT, PARTIALS, WIDTH, FREEZE, MIX, kParams };
	// The defaults (the plan's Global Constraints).
	float params[kParams] = {0.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, -24.f, 0.f, 0.f, 8.f, 0.393f, 0.f, 1.f};
	float stretchCv = 0.f, positionCv = 0.f, harmonicsCv = 0.f, freezeGate = 0.f, trig = 0.f;
	float inL = 0.f, inR = 0.f;
	bool inRPatched = false;
	const float* voct = nullptr;   // voctChannels values
	int voctChannels = 0;          // 0: unpatched (one root, 0 V); at most 16 are read
	int grainSize = GRAIN_MEDIUM;  // the menu: GrainSize
	bool wide = false;
	bool stopAtEnd = false;
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

	bool allocate(float rate) {
		if (!(rate >= kMinRate && rate <= kMaxRate)) return false;
		if (rate == allocRate_) return true;
		running_ = false;
		inFlight_ = false;
		allocRate_ = 0.f;
		for (int i = 0; i < 3; i++) frames_[i] = grainFrames(GrainSize(i), rate);
		if (!tape_.allocate(tapeFrames(rate))) return false;
		ring_ = &tape_;
		for (int i = 0; i < 3; i++)
			if (!buffers_[i].allocate(frames_[i])) return false;
		tables_.allocate(frames_[GRAIN_LONG] / 2);
		sliceL_.assign(size_t(frames_[GRAIN_LONG]), 0.f);
		sliceR_.assign(size_t(frames_[GRAIN_LONG]), 0.f);
		if (!overlapMade_) overlapMade_ = overlap_.allocate(kMaxGrain);
		allocRate_ = rate;
		return true;
	}

	void setSampleRate(float rate) {
		if (rate != allocRate_ || (running_ && rate == rate_)) return;
		rate_ = rate;
		gap_ = stepGap(rate);
		dip_ = runDipFrames(rate);
		budget_ = 32 * gap_;
		mixStep_ = 1.f / (0.01f * rate);
		const int fade = int(detail::roundHalfUp(0.005 * double(rate)));
		ring_->setFadeFrames(fade);
		ring_->clear();
		if (frozen_)   // frozen, the new tape is silent: its fade-out is written now, as silence
			for (int i = 0; i < fade; i++) ring_->write(0.f, 0.f, true);
		primed_ = false;
		active_ = GRAIN_MEDIUM;
		restart();
		running_ = true;
	}

	void seed(uint32_t s) { rng_.seed(s); }

	void reset() {
		if (ring_) ring_->clear();
		restart();
	}

	void process(const DoldrumsFrame& f, DoldrumsResult& out) {
		typedef DoldrumsFrame F;
		const float inL = finite(f.inL), inR = f.inRPatched ? finite(f.inR) : inL;
		const float gateV = finite(f.freezeGate), trigV = finite(f.trig);   // +inf is 0 V, not high
		if (!armed_) trigHigh_ = trigV >= 1.f;   // the first sample after a restart: levels, no edges
		const bool gate = schmitt(gateV, gateHigh_);
		const bool frozen = f.params[F::FREEZE] >= 0.5f || gate;
		if (frozen && !frozen_ && armed_) rose_ = true;
		frozen_ = frozen;
		const bool wasTrig = trigHigh_;
		if (schmitt(trigV, trigHigh_) && !wasTrig) trig_ = true;
		armed_ = true;
		pace_ = paceFromDial(cvValue(f.params[F::STRETCH], f.params[F::STRETCH_ATT], f.stretchCv));
		staged_ = 0;
		if (running_) {
			ring_->write(inL, inR, frozen);
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

private:
	Ring tape_;
	Ring* ring_ = &tape_;
	GrainBuffers buffers_[3];
	Tables tables_;
	Grain grain_;
	Overlap overlap_;
	PassLogic pass_;
	rka::hadal::Random rng_;
	std::vector<float> sliceL_, sliceR_;
	DoldrumsState state_;

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
	bool gateHigh_ = false, trigHigh_ = false, frozen_ = false, rose_ = false, trig_ = false;
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

	void restart() {
		pass_.reset();
		inFlight_ = false;
		s_ = 0;
		hop_ = gap_;
		spacing_ = gap_;
		passGrain_ = 0;
		heldSize_ = -1;
		last_ = PassOut{};
		rose_ = trig_ = false;
		armed_ = false;
		runOn_ = false;
		dipLeft_ = soundLeft_ = 0;
		snapMix_ = true;
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
		in.freezeRose = rose_;
		in.trig = trig_;
		in.stopAtEnd = f.stopAtEnd;
		in.grainChanged = passGrain_ != grain;
		rose_ = trig_ = false;
		passGrain_ = grain;
		const PassOut o = pass_.step(in);
		last_ = o;
		state_.anchor = o.anchor;
		state_.end = o.end;
		state_.held = o.held;
		state_.stopped = o.stopped;
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

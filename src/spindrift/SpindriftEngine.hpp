#pragma once
// Spindrift's engine: Mutable Instruments Beads' own Processor and Synth
// (vendored, src/spindrift/beads/) behind the glue Beads' hardware gave
// them, re-written Rack-free from beads.cc (eurorack f4cfa76):
//
// - the block loop at Beads' codec rate (24, 32 or 48 kHz, blocks of rate /
//   4000 frames), with Rack's rate converted at the edges (EdgeRate.hpp);
// - the controls as beads.cc builds Parameters, ModulatedParameter and
//   MixParameters (beads.cc:176-238), through CvReader's pot laws and
//   filters (Controls.hpp) and its CV channels (cv_reader.cc, settings.cc's
//   calibration targets) at Beads' 4 kHz control rate;
// - the SEED input read 2.5 ms late (beads.cc:145-156);
// - the input gain (GainControl.hpp);
// - the synth-mode switch, the reconfigurations and their 250 ms mutes
//   (beads.cc:169-176, 276-293, 352-391: MonitorStateChange and
//   ConfigureAndStartCodec), held back while frozen;
// - levels: +-10 V is Beads' full scale both ways; the gate output is 0/10 V.
//
// Where it departs from beads.cc (each a judgement, in the task report):
// - It starts in grain mode, configured for the inputs it first sees,
//   without a mute (beads.cc boots "in synth mode" with the codec stopped
//   until an input is patched); the synth timer starts at the first block,
//   so with nothing patched the synth arrives 10 s after the module does.
// - A jack counts as patched when Rack says it has a cable (beads.cc's
//   normalisation probe takes a few blocks to decide).
// - Three CV jacks replace Beads' one assignable CV, each at amount 1 for its
//   own control and only when patched (beads.cc applies the assign CV's
//   (cv - 0.002) to its destination even unpatched).
// - Controls (knobs, CVs, the jacks' patched state) are read once a block,
//   at the Rack sample that completes it; SEED and FREEZE per frame.
//
// Threads: the constructor, allocate() and setSampleRate() run off the audio
// thread, never during process(); everything else on the engine's thread,
// but for view() and overview(), which any thread may read (GrainView.hpp,
// Overview.hpp: relaxed atomics) for the window.
// `settings` is written by the menu (UI thread) and read once a block; the
// Quality long press writes settings.gain from the engine's thread, as Beads
// does (word-sized, a torn read is impossible).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "../Overview.hpp"
#include "BeadsContext.hpp"
#include "Controls.hpp"
#include "EdgeRate.hpp"
#include "GainControl.hpp"
#include "GrainView.hpp"

#include "beads/dsp/processor.h"
#include "beads/dsp/synth.h"
#include "stmlib/dsp/delay_line.h"
#include "stmlib/utils/gate_flags.h"
#include "stmlib/utils/random.h"

namespace spindrift {

struct Knobs {
	float time, size, shape, pitch, density;  // 0..1 knob positions
	float timeAtt, sizeAtt, shapeAtt, pitchAtt;  // 0..1, 0.5 = noon
	float feedback, dryWet, reverb;
};

struct Jacks {
	float time, size, shape, pitch, density, feedback, dryWet, reverb;  // volts
	bool timeOn, sizeOn, shapeOn, pitchOn, densityOn, feedbackOn, dryWetOn, reverbOn;
	float inL, inR;
	bool inLOn, inROn;
	float freeze, seed;
	bool freezeOn, seedOn;
};

enum SynthPolicy { SYNTH_AUTO, SYNTH_ALWAYS, SYNTH_NEVER };
enum GainMode { GAIN_FIXED, GAIN_AUTO, GAIN_MANUAL };

struct Settings {
	SynthPolicy synth = SYNTH_AUTO;
	GainMode gain = GAIN_FIXED;
	float manualGain = 1.f;  // GAIN_MANUAL's gain, 1..40 (Beads' range)
	bool gateOutput = false;
	bool keepFrozen = true;  // read by the module when saving (Task 8)
};

struct Out {
	float l, r;
};

// The engine's fixed numbers, from beads.cc.
struct EngineConstants {
	enum {
		kBlockRate = 4000,                // control blocks a second (beads.cc: kBlockRate)
		kMuteBlocks = kBlockRate / 4,     // 250 ms: codec_transition_frames
		kSynthBlocks = 10 * kBlockRate,   // 10 s with no input before the synth
		kBlock48 = 48000 / kBlockRate,    // 12 frames: Digital, Clean Tape, synth
		kSeedDelayBlocks = 10,            // event_delay.Read(size * 10): 2.5 ms
		kSeedDelayLength = 256,           // DelayLine<Codec::Event, 256>
		// Ring frames the waveform's overview may read per frame Beads runs.
		// Beads writes at most one, so 8 keeps the column under the head
		// within a few blocks of the audio, and rereads a whole ring (a
		// restored buffer: 192,000 to 768,000 frames) in 0.5 to 4 s.
		kOverviewBudget = 8
	};
};
static_assert(EngineConstants::kBlock48 <= (int) kMaxBlock, "Beads' largest block fits kMaxBlock");
static_assert(EngineConstants::kSeedDelayBlocks * EngineConstants::kBlock48 < EngineConstants::kSeedDelayLength,
              "the SEED delay fits its line");

// A Schmitt trigger at the house thresholds (0.1 V / 1 V); NaN keeps the state.
struct GateIn {
	bool high = false;
	bool process(float v) {
		if (high) {
			if (v <= 0.1f)
				high = false;
		}
		else if (v >= 1.f)
			high = true;
		return high;
	}
};

// One of CvReader's CV channels (cv_reader.h CvChannel), in Beads' units:
// a one-pole low pass from 0 (the ADC's mid-scale) on the calibrated
// value, then the channel's limits. The volts are first held to the ADC's
// range, as the hardware's input is. (The reference test drives Beads
// through this and mixParameter() with its own constants: the same
// expressions, so the compiler's contraction of them is the same.)
struct CvChannel {
	float lp = 0.f;
	float process(float volts, float scale, float adcVolts, float coefficient, float lo, float hi) {
		float v = volts == volts ? volts : 0.f;
		v = std::min(std::max(v, -adcVolts), adcVolts);
		const float target = v * scale;
		lp += coefficient * (target - lp);
		return std::min(std::max(lp, lo), hi);
	}
};

// beads.cc:213-222 for one mix parameter (Feedback, Dry/Wet, Reverb) and its
// own CV: the pot with its 0.5 % headroom, plus (cv - 0.002) when patched.
inline float mixParameter(float pot, float cv, bool patched) {
	float v = pot * 1.005f;
	if (patched)
		v += (cv - 0.002f);
	CONSTRAIN(v, 0.0f, 1.0f);
	return v;
}

class SpindriftEngine : private EngineConstants {
public:
	// Volts to Beads' units and back: +-10 V is full scale (the spec, "Levels").
	static float voltsToBeads() { return 0.1f; }
	static float beadsToVolts() { return 10.f; }

	explicit SpindriftEngine(uint32_t seed)
	    : seed_(seed), processor_(new beads::Processor()), synthVoice_(new beads::Synth()) {
		initPots();
		edge_.setRackRate(48000.f);
	}

	// Off the audio thread: Beads' buffers, and Beads initialised and seeded.
	// False (and silent output) if out of memory.
	bool allocate() {
		if (allocated_)
			return true;
		if (!memory_.allocate())
			return false;
		uint8_t* banks[3];
		memory_.banks(banks);
		BeadsScope scope(ctx_);
		processor_->Init(banks, memory_.reverb);
		synthVoice().Init(memory_.reverb);
		stmlib::Random::Seed(seed_);
		allocated_ = true;
		booted_ = false;
		return true;
	}

	// Off the audio thread. Builds the converters for the new rate; the
	// buffer is cleared (with a mute, as any reconfiguration) unless frozen,
	// since it holds Beads' rate, not Rack's.
	void setSampleRate(float rackRate) {
		edge_.setRackRate(rackRate);
		if (!booted_) {
			return;
		}
		if (frozenBlock_ || buttons_.frozenButton) {
			selectRate(edge_.currentBeadsRate());
			return;
		}
		if (synth_) {
			startCodec(true, false);
		}
		else {
			BeadsScope scope(ctx_);
			processor_->Configure(processor_->media(), processor_->mono());
			bufferCleared();
			startCodec(false, false);
		}
	}

	// One Rack sample, volts in and out.
	void process(const Knobs& k, const Jacks& j, Out& out) {
		if (!allocated_) {
			out.l = out.r = 0.f;
			return;
		}
		if (!booted_)
			boot(j);
		const bool seedHigh = j.seedOn && seedGate_.process(j.seed);
		const bool freezeHigh = j.freezeOn && freezeGate_.process(j.freeze);
		const uint32_t restarts = restarts_;
		edge_.in().push(j.inLOn ? audioIn(j.inL) : 0.f, j.inROn ? audioIn(j.inR) : 0.f);
		while (edge_.in().ready()) {
			float l, r, nearest;
			edge_.in().pop(l, r, nearest);
			// The ADC's full scale.
			in_[fill_].l = std::min(std::max(l, -1.f), 1.f);
			in_[fill_].r = std::min(std::max(r, -1.f), 1.f);
			seedEvent_[fill_] = seedHigh;
			freezeEvent_[fill_] = freezeHigh;
			if (++fill_ == block_) {
				fill_ = 0;
				runBlock(k, j);
			}
		}
		if (restarts != restarts_) {
			// The converters restarted during this sample, after taking its
			// input: the FIFO's silence starts with the next one, so the
			// latency stays what latency() says.
			out.l = out.r = 0.f;
			return;
		}
		const EdgeFrame f = edge_.popOut();
		out.l = f.l;
		out.r = f.r;
	}

	Buttons& buttons() {
		return buttons_;
	}

	Settings settings;

	Mode mode() const {
		if (synth_)
			return MODE_SYNTH;
		return processor_->delayMode() ? MODE_DELAY : MODE_GRAIN;
	}
	// The quality Beads is running (a choice made while frozen waits in
	// buttons().pending).
	Quality quality() const {
		return quality_;
	}
	bool mono() const {
		return processor_->mono();
	}
	// Frozen in the last block, by the button or the gate (Beads' Freeze light).
	bool frozen() const {
		return frozenBlock_;
	}
	// During the 250 ms after a reconfiguration.
	bool muted() const {
		return transition_ > 0;
	}

	// Rack samples from an input to the output it becomes at Dry/Wet 0, for
	// the rate Beads runs at now: the edge converters and one block.
	double latency() const {
		return edge_.latency();
	}
	int beadsRate() const {
		return edge_.currentBeadsRate();
	}
	// The buffer's length in seconds, as Beads holds it for this quality
	// (processor.h: 768,000 bytes at 16, 12, 12 or 8 bits; doubled in mono).
	float bufferSeconds() const {
		const int m = int(processor_->media());
		static const float kSeconds[4] = {
		    768000.f * 8.f / (8.f * 2.f) / 24000.f,        // Dirty Tape
		    768000.f * 8.f / (12.f * 2.f) * 2.f / 48000.f, // Clean Tape (2:1 inside)
		    768000.f * 8.f / (12.f * 2.f) / 32000.f,       // Vintage Digital
		    768000.f * 8.f / (16.f * 2.f) / 48000.f};      // Digital
		return kSeconds[m] * (processor_->mono() ? 2.f : 1.f);
	}
	// The record head's place in the ring, 0..1 (BeadsContext.hpp).
	float writeHead() const {
		return processor_->writeHead();
	}
	// Control blocks run so far (4 kHz): what Buttons' hold lengths count.
	uint32_t ticks() const {
		return ticks_;
	}
	// The Seed light: Buttons' breathing or hold, lit too in a block where
	// the SEED input fired (ui.cc:204-210, beads.cc:157).
	float seedLight(uint32_t ms) const {
		if (!buttons_.seedLatched && seedFlash_)
			return 1.f;
		return buttons_.seedLight(ms);
	}
	// Input level as Beads' VU light shows it (0..255), and whether the
	// automatic gain is still learning.
	uint8_t vuMeter() const {
		return gain_.vu_meter();
	}
	bool gainAdjusting() const {
		return gain_.adjusting();
	}
	// Rack samples the output FIFO ran dry (never, by construction; tests).
	long underruns() const {
		return edge_.underruns() + edge_.overruns();
	}

	// The window's view, published once a block (GrainView.hpp), and the
	// buffer's waveform: an Overview whose frames are the ring's, numbered in
	// the order Beads writes them (Processor::bufferFrame()), so its columns
	// left to right are the ring and the head moves across them. Read by any
	// thread; they live as long as the engine.
	const GrainView& view() const {
		return view_;
	}
	const rka::Overview& overview() const {
		return overview_;
	}

	beads::Processor& processor() {
		return *processor_;
	}
	beads::Synth& synthVoice() {
		return *synthVoice_;
	}
	const beads::Processor& processor() const {
		return *processor_;
	}
	const beads::Synth& synthVoice() const {
		return *synthVoice_;
	}
	BeadsContext& context() {
		return ctx_;
	}

private:
	static float audioIn(float volts) {
		if (!(volts == volts))
			return 0.f;
		return std::min(std::max(volts, -10.f), 10.f) * voltsToBeads();
	}
	static float knob(float v) {
		return v == v ? std::min(std::max(v, 0.f), 1.f) : 0.f;
	}

	// cv_reader.cc:41-53: LAW_ATTENUVERTER on the amounts, LAW_DETUNE on
	// Pitch, lp 0.05 everywhere, hysteresis 0.001 on Time.
	enum Pot {
		POT_TIME, POT_SIZE, POT_SHAPE, POT_PITCH, POT_DENSITY,
		POT_TIME_AMOUNT, POT_SIZE_AMOUNT, POT_SHAPE_AMOUNT, POT_PITCH_AMOUNT,
		POT_FEEDBACK, POT_DRY_WET, POT_REVERB, POT_LAST
	};
	void initPots() {
		for (int i = 0; i < POT_LAST; i++)
			pots_[i].init(0.05f, i == POT_TIME ? 0.001f : 0.f);
	}

	void boot(const Jacks& j) {
		BeadsScope scope(ctx_);
		quality_ = buttons_.quality;
		processor_->Configure(beads::Processor::Media(quality_), !j.inLOn || !j.inROn);
		bufferCleared();
		const bool synth = settings.synth == SYNTH_ALWAYS;
		configureCodec(synth, true);
		transition_ = 0;  // nothing to hide at start
		booted_ = true;
	}

	static GainControl::SoftClippingMode softClipping(beads::Processor::Media media) {
		return media == beads::Processor::MEDIA_DIRTY_TAPE
		           ? GainControl::SOFT_CLIPPING_MODE_ON
		           : (media == beads::Processor::MEDIA_CLEAN_TAPE ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
		                                                         : GainControl::SOFT_CLIPPING_MODE_OFF);
	}

	// ConfigureAndStartCodec (beads.cc:295-313), with the codec's restart as
	// the converters starting from silence.
	void startCodec(bool newSynth, bool resetGain) {
		configureCodec(newSynth, resetGain);
		transition_ = kMuteBlocks;
	}
	void configureCodec(bool newSynth, bool resetGain) {
		const int sr = newSynth ? 48000 : int(processor_->sample_rate());
		synth_ = newSynth;
		synthTimer_ = -1;
		gain_.Init(float(sr), softClipping(processor_->media()), resetGain);
		seedDelay_.Init();
		selectRate(sr);
	}
	void selectRate(int sr) {
		restarts_++;
		edge_.select(sr);
		block_ = size_t(sr / kBlockRate);
		fill_ = 0;
	}

	// beads.cc Process(), one block of block_ frames in in_[].
	void runBlock(const Knobs& k, const Jacks& j) {
		BeadsScope scope(ctx_);
		const size_t size = block_;
		ticks_++;
		buttons_.tick();
		if (buttons_.autoGainRequest) {
			// ui.cc:405-411: the long press restores the automatic gain.
			buttons_.autoGainRequest = false;
			settings.gain = GAIN_AUTO;
			gain_.Restart();
		}
		readControls(k, j);

		bool freeze[kMaxBlock];
		bool freezeBlock = false;
		for (size_t i = 0; i < size; ++i) {
			freeze[i] = buttons_.frozenButton || freezeEvent_[i];
			freezeBlock |= freeze[i];
		}
		frozenBlock_ = freezeBlock;

		bool seedBlock = false;
		stmlib::GateFlags seed[kMaxBlock];
		const stmlib::GateFlags* seedInput = nullptr;
		if (j.seedOn) {
			for (size_t i = 0; i < size; ++i) {
				seedDelay_.Write(uint8_t(seedEvent_[i]));
				// Delay the seed input by 2.5ms.
				const bool hasSeed = seedDelay_.Read(size_t(size * kSeedDelayBlocks)) != 0;
				previousSeed_ = seed[i] = stmlib::ExtractGateFlags(previousSeed_, hasSeed);
				seedBlock |= hasSeed;
			}
			seedInput = seed;
		}
		else {
			previousSeed_ = stmlib::GATE_FLAG_LOW;
		}
		seedFlash_ = seedBlock && !transition_;

		beads::FloatFrame gained[kMaxBlock];
		gain_.Process(!transition_, settings.gain == GAIN_AUTO,
		              settings.gain == GAIN_MANUAL ? settings.manualGain : 1.f, !j.inLOn, !j.inROn, in_,
		              gained, size);

		beads::FloatFrame out[kMaxBlock];
		const bool gate = settings.gateOutput;
		if (transition_) {
			--transition_;
			for (size_t i = 0; i < size; ++i)
				out[i].l = out[i].r = 0.f;
		}
		else {
			beads::Parameters p;
			beads::ModulatedParameter d;
			beads::Processor::MixParameters m;
			buildParameters(j, &p, &d, &m);
			m.output_gate = gate;
			const beads::SeedButtonState seedButton =
			    buttons_.seedLatched ? beads::SEED_BUTTON_STATE_LOCKED
			                         : (buttons_.seedHeld ? beads::SEED_BUTTON_STATE_ON : beads::SEED_BUTTON_STATE_OFF);
			if (synth_) {
				synthVoice_->Render(seedButton, seedInput, freeze, beads::Synth::Resolution(quality_), d, p, m, out,
				               size);
			}
			else {
				const int before = processor_->bufferFrame();
				processor_->Process(seedButton, seedInput, freeze, d, p, m, gained, out, size);
				touchWritten(before, processor_->bufferFrame());
			}
			for (size_t i = 0; i < size; ++i) {
				float l = out[i].l, r = out[i].r;
				CONSTRAIN(l, -1.0f, 0.999999f);
				CONSTRAIN(r, -1.0f, 0.999999f);
				if (std::fabs(l) > 0.01f || std::fabs(r) > 0.01f)
					synthTimer_ = -1;
				out[i].l = l;
				out[i].r = r;
			}
		}

		// Back to Rack's rate. In gate output mode Beads' right channel is
		// -0.8 during a grain; it leaves as a 0/10 V gate, sampled (not
		// filtered) so it does not ring.
		Resampler& o = edge_.out();
		for (size_t i = 0; i < size; ++i) {
			o.push(out[i].l, out[i].r);
			while (o.ready()) {
				float l, r, nearest;
				o.pop(l, r, nearest);
				EdgeFrame f;
				f.l = l * beadsToVolts();
				f.r = gate ? (nearest < -0.4f ? 10.f : 0.f) : r * beadsToVolts();
				edge_.pushOut(f);
			}
		}

		monitorStateChange(j);
		if (!synth_)
			overview_.serviceFrom(BufferSource{*processor_}, int(kOverviewBudget * size));
		publishView();
	}

	// The overview's source: Beads' ring read back through its own format
	// (the samples as Beads stored them: quantised, mu-law, after feedback
	// and the freeze tail's crossfade), via the marked Processor::bufferPeaks.
	struct BufferSource {
		const beads::Processor& p;
		void scan(size_t f0, size_t f1, float& l, float& h) const {
			if (f1 <= f0) {
				l = h = 0.f;
				return;
			}
			l = 1e30f;
			h = -1e30f;
			p.bufferPeaks(int(f0), int(f1), &l, &h);
		}
	};

	// Configure() cleared the ring (DelayLine::Reset), perhaps to a new
	// length: the waveform is silence over the new length at once.
	void bufferCleared() {
		overview_.clear(size_t(processor_->bufferFrames()));
	}

	// Beads wrote frames [a, b) of the ring this block (b < a: it wrapped).
	void touchWritten(int a, int b) {
		const size_t frames = size_t(processor_->bufferFrames());
		if (a == b)
			return;
		if (a < b) {
			overview_.touch(size_t(a), size_t(b), frames);
		}
		else {
			overview_.touch(size_t(a), frames, frames);
			overview_.touch(0, size_t(b), frames);
		}
	}

	// Once a block, after any reconfiguration: what the window shows. Muted
	// (the 250 ms after a reconfiguration) nothing sounds, so no grains.
	void publishView() {
		GrainSnap g[GrainView::MAX];
		TapSnap t[GrainView::MAX];
		int n = 0, nt = 0;
		if (!transition_) {
			n = synth_ ? synthVoice_->grains(g, GrainView::MAX) : processor_->grains(g, GrainView::MAX);
			nt = synth_ ? 0 : processor_->taps(t, GrainView::MAX);
		}
		view_.publish(g, n, t, nt, processor_->writeHead(), mode(), quality_, mono(), bufferSeconds(),
		              synthVoice_->bank(), frozenBlock_, buttons_.hasPending(), buttons_.pending, synthVoice_->wave());
	}

	// cv_reader.cc and beads.cc:176-238. Calibration (settings.cc): Time,
	// Size and Shape read +-8 V as +-1.6 (+-5 V is +-1), lp 0.5; Pitch 12
	// semitones a volt, +-60, no lp; Density +-5 V as +-1, no lp; the
	// assignable CV (here each of the three mix CVs) +-5 V as +-1, lp 0.1.
	void readControls(const Knobs& k, const Jacks& j) {
		pot_[POT_TIME] = pots_[POT_TIME].process(knob(k.time));
		pot_[POT_SIZE] = pots_[POT_SIZE].process(knob(k.size));
		pot_[POT_SHAPE] = pots_[POT_SHAPE].process(knob(k.shape));
		pot_[POT_PITCH] = pots_[POT_PITCH].process(detuneLaw(knob(k.pitch)));
		pot_[POT_DENSITY] = pots_[POT_DENSITY].process(knob(k.density));
		pot_[POT_TIME_AMOUNT] = pots_[POT_TIME_AMOUNT].process(attenuverterLaw(knob(k.timeAtt)));
		pot_[POT_SIZE_AMOUNT] = pots_[POT_SIZE_AMOUNT].process(attenuverterLaw(knob(k.sizeAtt)));
		pot_[POT_SHAPE_AMOUNT] = pots_[POT_SHAPE_AMOUNT].process(attenuverterLaw(knob(k.shapeAtt)));
		pot_[POT_PITCH_AMOUNT] = pots_[POT_PITCH_AMOUNT].process(attenuverterLaw(knob(k.pitchAtt)));
		pot_[POT_FEEDBACK] = pots_[POT_FEEDBACK].process(knob(k.feedback));
		pot_[POT_DRY_WET] = pots_[POT_DRY_WET].process(knob(k.dryWet));
		pot_[POT_REVERB] = pots_[POT_REVERB].process(knob(k.reverb));

		cv_[CV_TIME] = cvs_[CV_TIME].process(j.time, 0.2f, 8.f, 0.5f, -1.6f, 1.6f);
		cv_[CV_SIZE] = cvs_[CV_SIZE].process(j.size, 0.2f, 8.f, 0.5f, -1.6f, 1.6f);
		cv_[CV_SHAPE] = cvs_[CV_SHAPE].process(j.shape, 0.2f, 8.f, 0.5f, -1.6f, 1.6f);
		cv_[CV_PITCH] = cvs_[CV_PITCH].process(j.pitch, 12.f, 5.f, 1.f, -60.f, 60.f);
		cv_[CV_DENSITY] = cvs_[CV_DENSITY].process(j.density, 0.2f, 5.f, 1.f, -1.f, 1.f);
		cv_[CV_FEEDBACK] = cvs_[CV_FEEDBACK].process(j.feedback, 0.2f, 5.f, 0.1f, -1.f, 1.f);
		cv_[CV_DRY_WET] = cvs_[CV_DRY_WET].process(j.dryWet, 0.2f, 5.f, 0.1f, -1.f, 1.f);
		cv_[CV_REVERB] = cvs_[CV_REVERB].process(j.reverb, 0.2f, 5.f, 0.1f, -1.f, 1.f);
	}

	void buildParameters(const Jacks& j, beads::Parameters* p, beads::ModulatedParameter* d,
	                     beads::Processor::MixParameters* m) const {
		p->time.base = pot_[POT_TIME];
		p->time.attenuverter = pot_[POT_TIME_AMOUNT];
		p->time.has_cv = j.timeOn;
		p->time.cv = cv_[CV_TIME];

		p->size.base = pot_[POT_SIZE];
		p->size.attenuverter = pot_[POT_SIZE_AMOUNT];
		p->size.has_cv = j.sizeOn;
		p->size.cv = cv_[CV_SIZE];

		p->shape.base = pot_[POT_SHAPE];
		p->shape.attenuverter = pot_[POT_SHAPE_AMOUNT];
		p->shape.has_cv = j.shapeOn;
		p->shape.cv = cv_[CV_SHAPE];

		p->pitch.base = pot_[POT_PITCH];
		p->pitch.attenuverter = pot_[POT_PITCH_AMOUNT];
		p->pitch.has_cv = j.pitchOn;
		p->pitch.cv = cv_[CV_PITCH];

		p->monophonic_mode = false;  // beads.cc without ENABLE_ADVANCED_FEATURES

		d->base = pot_[POT_DENSITY];
		// Avoid small but annoying modulation due to noise on the density CV input.
		d->cv = j.densityOn ? cv_[CV_DENSITY] : 0.0f;

		m->feedback = mix(POT_FEEDBACK, CV_FEEDBACK, j.feedbackOn);
		m->dry_wet = mix(POT_DRY_WET, CV_DRY_WET, j.dryWetOn);
		m->reverb_amount = mix(POT_REVERB, CV_REVERB, j.reverbOn);
		m->output_gate = settings.gateOutput;
	}

	float mix(int pot, int cv, bool patched) const {
		return mixParameter(pot_[pot], cv_[cv], patched);
	}

	// MonitorStateChange (beads.cc:352-391), once a block, with the menu's
	// synth policy.
	void monitorStateChange(const Jacks& j) {
		if (frozenBlock_ || transition_) {
			synthTimer_ = -1;
			return;
		}
		// The quality chosen while frozen is applied now (ui.cc:416-418).
		if (buttons_.hasPending())
			buttons_.applyPending();
		const Quality q = buttons_.quality;
		const bool noInput = !j.inLOn && !j.inROn;
		const bool mono = !j.inLOn || !j.inROn;

		if (settings.synth == SYNTH_ALWAYS) {
			if (!synth_)
				startCodec(true, true);
			quality_ = q;  // the synth's resolution follows the setting live
			return;
		}
		if (noInput) {
			if (synth_) {
				if (settings.synth == SYNTH_AUTO) {
					quality_ = q;
					return;
				}
				// SYNTH_NEVER while in the synth: leave it, below.
			}
			else {
				if (settings.synth == SYNTH_AUTO) {
					if (synthTimer_ < 0) {
						synthTimer_ = 0;
					}
					else if (++synthTimer_ >= kSynthBlocks) {
						// If 10 seconds have elapsed with nothing patched in the audio
						// inputs, enable the synth mode.
						startCodec(true, true);
					}
				}
				return;
			}
		}

		const bool mediaChanged = beads::Processor::Media(q) != processor_->media();
		const bool monoChanged = mono != processor_->mono();
		if (synth_ || mediaChanged || monoChanged) {
			const bool wasSynth = synth_;
			processor_->Configure(beads::Processor::Media(q), mono);
			bufferCleared();
			quality_ = q;
			startCodec(false, monoChanged || wasSynth);
		}
		synthTimer_ = -1;
	}

	enum Cv { CV_TIME, CV_SIZE, CV_SHAPE, CV_PITCH, CV_DENSITY, CV_FEEDBACK, CV_DRY_WET, CV_REVERB, CV_LAST };

	uint32_t seed_;
	BeadsContext ctx_;
	BeadsMemory memory_;
	std::unique_ptr<beads::Processor> processor_;
	std::unique_ptr<beads::Synth> synthVoice_;
	EdgeRate edge_;
	GainControl gain_;
	Buttons buttons_;
	PotFilter pots_[POT_LAST];
	float pot_[POT_LAST] = {};
	CvChannel cvs_[CV_LAST];
	float cv_[CV_LAST] = {};
	GateIn seedGate_, freezeGate_;
	stmlib::DelayLine<uint8_t, kSeedDelayLength> seedDelay_;
	stmlib::GateFlags previousSeed_ = stmlib::GATE_FLAG_LOW;

	beads::FloatFrame in_[kMaxBlock];
	bool seedEvent_[kMaxBlock] = {};
	bool freezeEvent_[kMaxBlock] = {};
	size_t block_ = kBlock48, fill_ = 0;

	bool allocated_ = false, booted_ = false;
	bool synth_ = false;
	bool frozenBlock_ = false, seedFlash_ = false;
	int transition_ = 0;
	int synthTimer_ = -1;  // blocks since the synth timer started; -1: not running
	Quality quality_ = DIGITAL;
	uint32_t ticks_ = 0;
	uint32_t restarts_ = 0;  // converter restarts (selectRate), to spot one inside process()

	GrainView view_;
	rka::Overview overview_;
};

}  // namespace spindrift

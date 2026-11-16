#pragma once
#include "Rate.hpp"
#include "RateLight.hpp"
#include "Shaper.hpp"
#include "Triggers.hpp"
#include "Voice.hpp"
#include "Waves.hpp"
#include <algorithm>
#include <cmath>

// Rack-free per-sample logic of the Sway module: everything its process()
// does between reading the ports and writing them. The module fills a
// SwayFrame from its params and ports; the bench (src/bench/bench.cpp) and
// the tests fill one directly, so all three run the same code.
namespace rka {

// rack::clamp: NaN lands on the low end.
inline float clampF(float x, float lo, float hi) {
	return std::fmax(std::fmin(x, hi), lo);
}

// One input port as Rack holds it: 16 voltages and a channel count (0 when
// unplugged, when Rack keeps the voltages at 0).
struct SwayPort {
	const float* v = nullptr;
	int channels = 0;

	// rack::Input::getPolyVoltage: a mono cable drives every channel.
	float poly(int c) const {
		return channels == 1 ? v[0] : v[c];
	}
};

struct SwayFrame {
	enum Param {
		RATE, PW, PHASE, SKEW, SMOOTH, MORPH, SPREAD, CHANNELS,
		FM_ATT, PW_ATT, PHASE_ATT, SKEW_ATT, SMOOTH_ATT, MORPH_ATT,
		OFFSET, ONESHOT, PARAMS_LEN
	};
	enum Input { FM, PW_IN, PHASE_IN, SKEW_IN, SMOOTH_IN, MORPH_IN, CLOCK, RESET, HOLD, INPUTS_LEN };
	enum Output { SIN, TRI, SAW, SQR, MORPH_OUT, RAND, EOC, OUTPUTS_LEN };
	static const int MAX_CHANNELS = 16;

	float params[PARAMS_LEN] = {};
	SwayPort inputs[INPUTS_LEN];
	// Whether a cable is plugged into each output.
	bool outputConnected[OUTPUTS_LEN] = {};
	// rka::Range.
	int range = RANGE_NORMAL;
};

struct SwayResult {
	float out[SwayFrame::OUTPUTS_LEN][SwayFrame::MAX_CHANNELS];
	int channels = 1;
	float oneShotLight = 0.f;
	// The rate light's target brightness (the module smooths it).
	float rateLight = 0.f;
};

struct SwayEngine {
	// Timing for all 16 channels, advanced every sample even when inactive.
	VoiceBank bank;
	VoiceInput voiceInputs[VoiceBank::SIZE];
	SchmittTrigger resetTriggers[SwayFrame::MAX_CHANNELS];
	SchmittTrigger clockTriggers[SwayFrame::MAX_CHANNELS];
	SchmittTrigger holdTriggers[SwayFrame::MAX_CHANNELS];
	PulseGenerator eocPulses[SwayFrame::MAX_CHANNELS];
	bool wasOneShot = false;
	bool wasClockConnected = false;
	// Channel 1 is following a clock: RATE shows a ratio.
	bool clocked = false;
	RateLight rateLightCalc;

private:
	// updateRates()'s cache.
	bool ratesKnown = false;
	bool sharedFm = false;
	float knownRate = 0.f;
	Range knownRange = RANGE_NORMAL;
	float knownSampleRate = 0.f;
	float knownFm[VoiceBank::SIZE] = {};

public:

	// Each knob's range (max - min) for modulated(), as the module's
	// configParam sets it.
	static float span(int param) {
		switch (param) {
			case SwayFrame::PW: return 0.99f - 0.01f;
			case SwayFrame::SMOOTH: return 2.f;
			default: return 1.f;
		}
	}

	// The module's Initialize: phases and clocks back to the start.
	void reset() {
		for (Voice& v : bank.voices) {
			v.phasor.reset();
			v.clock.clear();
		}
		wasOneShot = false;
	}

	// Knob plus attenuated CV, where +/-5 V at full attenuverter sweeps the
	// knob's whole range.
	static float modulated(const SwayFrame& f, int param, int input, int att, int c) {
		const float cv = f.inputs[input].poly(c) / 10.f * f.params[att];
		return f.params[param] + cv * span(param);
	}

	// Each channel's clock ratio and free-running frequency from RATE, the
	// range and its FM, worked out again only when one of those changes,
	// and once for all channels when FM is mono or unplugged.
	void updateRates(const SwayFrame& f, float rate, Range r, float sampleRate) {
		const bool same = ratesKnown && rate == knownRate && r == knownRange && sampleRate == knownSampleRate;
		ratesKnown = true;
		knownRate = rate;
		knownRange = r;
		knownSampleRate = sampleRate;
		const SwayPort& fmIn = f.inputs[SwayFrame::FM];
		const float att = f.params[SwayFrame::FM_ATT];
		if (fmIn.channels >= 2) {
			sharedFm = false;
			for (int c = 0; c < VoiceBank::SIZE; c++) {
				const float fm = fmIn.v[c] * att;
				if (same && fm == knownFm[c])
					continue;
				knownFm[c] = fm;
				voiceInputs[c].ratio = ratioIndex(rate, fm);
				voiceInputs[c].freeHz = expToHz(rateExponent(rate, fm, r), sampleRate);
			}
			return;
		}
		const float fm = (fmIn.channels == 1 ? fmIn.v[0] : 0.f) * att;
		if (same && fm == knownFm[0] && sharedFm)
			return;
		const int ratio = ratioIndex(rate, fm);
		const double hz = expToHz(rateExponent(rate, fm, r), sampleRate);
		for (int c = 0; c < VoiceBank::SIZE; c++) {
			knownFm[c] = fm;
			voiceInputs[c].ratio = ratio;
			voiceInputs[c].freeHz = hz;
		}
		sharedFm = true;
	}

	void process(const SwayFrame& f, float sampleTime, float sampleRate, SwayResult& res) {
		int channels = int(f.params[SwayFrame::CHANNELS]);
		for (int i = 0; i < SwayFrame::INPUTS_LEN; i++)
			channels = std::max(channels, f.inputs[i].channels);

		const Range r = Range(f.range);
		const float rate = f.params[SwayFrame::RATE];
		const float spread = f.params[SwayFrame::SPREAD];
		const bool unipolar = f.params[SwayFrame::OFFSET] > 0.5f;
		const bool oneShot = f.params[SwayFrame::ONESHOT] > 0.5f;
		res.oneShotLight = oneShot ? 1.f : 0.f;

		// Switching one-shot on parks every channel until the next RESET.
		if (oneShot && !wasOneShot)
			for (Voice& v : bank.voices)
				v.phasor.park();
		wasOneShot = oneShot;

		// Unplugging the clock returns to free-running at once.
		const bool clockConnected = f.inputs[SwayFrame::CLOCK].channels > 0;
		if (!clockConnected && wasClockConnected)
			for (Voice& v : bank.voices)
				v.clock.clear();
		wasClockConnected = clockConnected;

		// Timing inputs for every channel; poly() lets a mono cable drive
		// them all. An unplugged trigger input is 0 V on every channel
		// (Rack holds it there), which leaves each trigger low.
		const SwayPort& resetIn = f.inputs[SwayFrame::RESET];
		const SwayPort& holdIn = f.inputs[SwayFrame::HOLD];
		const SwayPort& clockIn = f.inputs[SwayFrame::CLOCK];
		for (int c = 0; c < VoiceBank::SIZE; c++) {
			VoiceInput& in = voiceInputs[c];
			if (resetIn.channels > 0)
				in.reset = resetTriggers[c].process(resetIn.poly(c), 0.1f, 1.f);
			else {
				resetTriggers[c].s = SchmittTrigger::LOW;
				in.reset = false;
			}
			if (holdIn.channels > 0)
				holdTriggers[c].process(holdIn.poly(c), 0.1f, 1.f);
			else
				holdTriggers[c].s = SchmittTrigger::LOW;
			in.hold = holdTriggers[c].isHigh();
			if (clockIn.channels > 0)
				in.edge = clockTriggers[c].process(clockIn.poly(c), 0.1f, 1.f);
			else {
				clockTriggers[c].s = SchmittTrigger::LOW;
				in.edge = false;
			}
		}
		updateRates(f, rate, r, sampleRate);
		bank.tick(voiceInputs, clockConnected, oneShot, sampleTime, sampleRate);
		clocked = clockConnected && bank.voices[0].clock.locked();

		// The rate light follows channel 1 (rateLightHz).
		const Voice& first = bank.voices[0];
		res.rateLight = rateLightCalc.process(first.phasor.phase, rateLightHz(first, voiceInputs[0].hold));

		for (int c = 0; c < channels; c++) {
			const Voice& voice = bank.voices[c];
			const float phaseOffset = modulated(f, SwayFrame::PHASE, SwayFrame::PHASE_IN, SwayFrame::PHASE_ATT, c);
			const float p = readPhase(voice.phasor.phase, phaseOffset + spread * c / channels);
			const float pw = clampF(modulated(f, SwayFrame::PW, SwayFrame::PW_IN, SwayFrame::PW_ATT, c), 0.01f, 0.99f);
			const float skew = clampF(modulated(f, SwayFrame::SKEW, SwayFrame::SKEW_IN, SwayFrame::SKEW_ATT, c), 0.f, 1.f);
			const float smooth = clampF(modulated(f, SwayFrame::SMOOTH, SwayFrame::SMOOTH_IN, SwayFrame::SMOOTH_ATT, c), -1.f, 1.f);
			const float morph = clampF(modulated(f, SwayFrame::MORPH, SwayFrame::MORPH_IN, SwayFrame::MORPH_ATT, c), 0.f, 1.f);
			// SMOOTH left of centre also glides RAND: 0 stepped, -1 glides the
			// whole cycle.
			const float glide = std::max(0.f, -smooth);
			if (voice.wrapped)
				eocPulses[c].trigger(1e-3f);
			// Band-limit edges only where they can alias.
			const float dt = r == RANGE_AUDIO ? float(voice.freq * sampleTime) : 0.f;

			// Only the patched outputs are worked out; the rest read 0 V.
			const bool* used = f.outputConnected;
			res.out[SwayFrame::SIN][c] = used[SwayFrame::SIN] ? lfoVolts(waveSin(p), unipolar) : 0.f;
			res.out[SwayFrame::TRI][c] = used[SwayFrame::TRI] ? lfoVolts(waveTri(p), unipolar) : 0.f;
			res.out[SwayFrame::SAW][c] = used[SwayFrame::SAW] ? lfoVolts(sawBlep(p, dt), unipolar) : 0.f;
			res.out[SwayFrame::SQR][c] = used[SwayFrame::SQR] ? lfoVolts(sqrBlep(p, pw, dt), unipolar) : 0.f;
			res.out[SwayFrame::MORPH_OUT][c] = used[SwayFrame::MORPH_OUT] ? lfoVolts(shape(p, skew, morph, smooth, pw, dt), unipolar) : 0.f;
			res.out[SwayFrame::RAND][c] = used[SwayFrame::RAND] ? lfoVolts(voice.random.value(float(voice.phasor.phase), glide), unipolar) : 0.f;
			// The pulse runs on unpatched, so a cable plugged in mid-pulse
			// sees what is left of it, not a stale one.
			const bool eoc = eocPulses[c].process(sampleTime);
			res.out[SwayFrame::EOC][c] = eoc ? 10.f : 0.f;
		}
		res.channels = channels;
	}
};

} // namespace rka

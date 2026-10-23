#include "plugin.hpp"
#include "Rate.hpp"
#include "Shaper.hpp"
#include "Voice.hpp"
#include "Waves.hpp"
#include "SwayLayout.hpp"
#include "widgets.hpp"


struct Sway : Module {
	enum ParamId {
		RATE_PARAM,
		PW_PARAM,
		PHASE_PARAM,
		SKEW_PARAM,
		SMOOTH_PARAM,
		MORPH_PARAM,
		SPREAD_PARAM,
		CHANNELS_PARAM,
		FM_ATT_PARAM,
		PW_ATT_PARAM,
		PHASE_ATT_PARAM,
		SKEW_ATT_PARAM,
		SMOOTH_ATT_PARAM,
		MORPH_ATT_PARAM,
		OFFSET_PARAM,
		ONESHOT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		FM_INPUT,
		PW_INPUT,
		PHASE_INPUT,
		SKEW_INPUT,
		SMOOTH_INPUT,
		MORPH_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		HOLD_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		SIN_OUTPUT,
		TRI_OUTPUT,
		SAW_OUTPUT,
		SQR_OUTPUT,
		MORPH_OUTPUT,
		RAND_OUTPUT,
		EOC_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ONESHOT_LIGHT,
		LIGHTS_LEN
	};

	// rka::Range, as an int so the context menu can point at it.
	int range = rka::RANGE_NORMAL;
	// Channel 1 is following a clock: RATE shows a ratio.
	bool clocked = false;

	// Timing for all 16 channels, advanced every sample even when inactive.
	rka::VoiceBank bank;
	rka::VoiceInput voiceInputs[rka::VoiceBank::SIZE];
	dsp::SchmittTrigger resetTriggers[PORT_MAX_CHANNELS];
	dsp::SchmittTrigger clockTriggers[PORT_MAX_CHANNELS];
	dsp::SchmittTrigger holdTriggers[PORT_MAX_CHANNELS];
	dsp::PulseGenerator eocPulses[PORT_MAX_CHANNELS];
	bool wasOneShot = false;
	bool wasClockConnected = false;

	// RATE's tooltip follows the range and the clock.
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Sway* sway = static_cast<Sway*>(module);
			if (!sway)
				return ParamQuantity::getDisplayValueString();
			if (sway->clocked) {
				const float ratio = rka::clockRatio(rka::ratioIndex(getValue(), 0.f));
				return ratio >= 1.f ? string::f("×%d", int(ratio)) : string::f("÷%d", int(std::round(1.f / ratio)));
			}
			const double hz = std::exp2(rka::rateExponent(getValue(), 0.f, rka::Range(sway->range)));
			if (sway->range == rka::RANGE_SLOW && hz < 1.0)
				return cycleTime(1.0 / hz) + " per cycle";
			return string::f("%.3g Hz", hz);
		}

		std::string getUnit() override {
			return "";
		}

		static std::string cycleTime(double s) {
			if (s >= 3600.0)
				return string::f("%dh %02dm", int(s / 3600.0), int(std::fmod(s, 3600.0) / 60.0));
			if (s >= 60.0)
				return string::f("%dm %02ds", int(s / 60.0), int(std::fmod(s, 60.0)));
			return string::f("%.3g s", s);
		}
	};

	Sway() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<RateQuantity>(RATE_PARAM, -7.f, 7.f, 1.f, "Rate");
		configParam(PW_PARAM, 0.01f, 0.99f, 0.5f, "Pulse width", "%", 0.f, 100.f);
		configParam(PHASE_PARAM, 0.f, 1.f, 0.f, "Phase", "°", 0.f, 360.f);
		configParam(SKEW_PARAM, 0.f, 1.f, 0.5f, "Skew (MORPH out)", "%", 0.f, 100.f);
		configParam(SMOOTH_PARAM, -1.f, 1.f, 0.f, "Smooth: round MORPH and glide RAND ← → fold MORPH", "%", 0.f, 100.f);
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph: sin → tri → saw → square", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Phase spread across channels", "%", 0.f, 100.f);
		configParam(CHANNELS_PARAM, 1.f, 16.f, 1.f, "Channels")->snapEnabled = true;
		configParam(FM_ATT_PARAM, -1.f, 1.f, 0.f, "FM amount (1 V/oct at 100%)", "%", 0.f, 100.f);
		configParam(PW_ATT_PARAM, -1.f, 1.f, 0.f, "PW CV amount", "%", 0.f, 100.f);
		configParam(PHASE_ATT_PARAM, -1.f, 1.f, 0.f, "Phase CV amount", "%", 0.f, 100.f);
		configParam(SKEW_ATT_PARAM, -1.f, 1.f, 0.f, "Skew CV amount", "%", 0.f, 100.f);
		configParam(SMOOTH_ATT_PARAM, -1.f, 1.f, 0.f, "Smooth CV amount", "%", 0.f, 100.f);
		configParam(MORPH_ATT_PARAM, -1.f, 1.f, 0.f, "Morph CV amount", "%", 0.f, 100.f);
		configSwitch(OFFSET_PARAM, 0.f, 1.f, 0.f, "Offset", {"Bipolar (±5 V)", "Unipolar (0–10 V)"});
		configSwitch(ONESHOT_PARAM, 0.f, 1.f, 0.f, "One-shot (RESET runs one cycle)", {"Off", "On"});
		configInput(FM_INPUT, "FM (exponential)");
		configInput(PW_INPUT, "Pulse width CV");
		configInput(PHASE_INPUT, "Phase CV");
		configInput(SKEW_INPUT, "Skew CV");
		configInput(SMOOTH_INPUT, "Smooth CV");
		configInput(MORPH_INPUT, "Morph CV");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(HOLD_INPUT, "Hold (freezes while high)");
		configOutput(SIN_OUTPUT, "Sine");
		configOutput(TRI_OUTPUT, "Triangle");
		configOutput(SAW_OUTPUT, "Saw");
		configOutput(SQR_OUTPUT, "Square");
		configOutput(MORPH_OUTPUT, "Morph (shaped)");
		configOutput(RAND_OUTPUT, "Random (stepped; SMOOTH left glides)");
		configOutput(EOC_OUTPUT, "End of cycle trigger");
		configLight(ONESHOT_LIGHT, "One-shot");
	}

	void onReset() override {
		range = rka::RANGE_NORMAL;
		for (rka::Voice& v : bank.voices) {
			v.phasor.reset();
			v.clock.clear();
		}
		wasOneShot = false;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "range", json_integer(range));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* r = json_object_get(root, "range");
		if (r)
			range = clamp(int(json_integer_value(r)), int(rka::RANGE_SLOW), int(rka::RANGE_AUDIO));
	}

	// Knob plus attenuated CV, where ±5 V at full attenuverter sweeps the
	// knob's whole range.
	float modulated(int param, int input, int att, int c) {
		ParamQuantity* q = paramQuantities[param];
		const float cv = inputs[input].getPolyVoltage(c) / 10.f * params[att].getValue();
		return params[param].getValue() + cv * (q->getMaxValue() - q->getMinValue());
	}

	void process(const ProcessArgs& args) override {
		int channels = int(params[CHANNELS_PARAM].getValue());
		for (int i = 0; i < INPUTS_LEN; i++)
			channels = std::max(channels, inputs[i].getChannels());

		const rka::Range r = rka::Range(range);
		const float rate = params[RATE_PARAM].getValue();
		const float spread = params[SPREAD_PARAM].getValue();
		const bool unipolar = params[OFFSET_PARAM].getValue() > 0.5f;
		const bool oneShot = params[ONESHOT_PARAM].getValue() > 0.5f;
		lights[ONESHOT_LIGHT].setBrightness(oneShot);

		// Switching one-shot on parks every channel until the next RESET.
		if (oneShot && !wasOneShot)
			for (rka::Voice& v : bank.voices)
				v.phasor.park();
		wasOneShot = oneShot;

		// Unplugging the clock returns to free-running at once.
		const bool clockConnected = inputs[CLOCK_INPUT].isConnected();
		if (!clockConnected && wasClockConnected)
			for (rka::Voice& v : bank.voices)
				v.clock.clear();
		wasClockConnected = clockConnected;

		// Timing inputs for every channel; getPolyVoltage() lets a mono
		// cable drive them all.
		for (int c = 0; c < rka::VoiceBank::SIZE; c++) {
			rka::VoiceInput& in = voiceInputs[c];
			in.reset = resetTriggers[c].process(inputs[RESET_INPUT].getPolyVoltage(c), 0.1f, 1.f);
			holdTriggers[c].process(inputs[HOLD_INPUT].getPolyVoltage(c), 0.1f, 1.f);
			in.hold = holdTriggers[c].isHigh();
			in.edge = clockTriggers[c].process(inputs[CLOCK_INPUT].getPolyVoltage(c), 0.1f, 1.f);
			const float fm = inputs[FM_INPUT].getPolyVoltage(c) * params[FM_ATT_PARAM].getValue();
			in.ratio = rka::ratioIndex(rate, fm);
			in.freeHz = rka::expToHz(rka::rateExponent(rate, fm, r), args.sampleRate);
		}
		bank.tick(voiceInputs, clockConnected, oneShot, args.sampleTime, args.sampleRate);
		clocked = clockConnected && bank.voices[0].clock.locked();

		for (int c = 0; c < channels; c++) {
			const rka::Voice& voice = bank.voices[c];
			const float phaseOffset = modulated(PHASE_PARAM, PHASE_INPUT, PHASE_ATT_PARAM, c);
			const float p = rka::readPhase(voice.phasor.phase, phaseOffset + spread * c / channels);
			const float pw = clamp(modulated(PW_PARAM, PW_INPUT, PW_ATT_PARAM, c), 0.01f, 0.99f);
			const float skew = clamp(modulated(SKEW_PARAM, SKEW_INPUT, SKEW_ATT_PARAM, c), 0.f, 1.f);
			const float smooth = clamp(modulated(SMOOTH_PARAM, SMOOTH_INPUT, SMOOTH_ATT_PARAM, c), -1.f, 1.f);
			const float morph = clamp(modulated(MORPH_PARAM, MORPH_INPUT, MORPH_ATT_PARAM, c), 0.f, 1.f);
			// SMOOTH left of centre also glides RAND: 0 stepped, -1 glides the
			// whole cycle.
			const float glide = std::max(0.f, -smooth);
			if (voice.wrapped)
				eocPulses[c].trigger(1e-3f);
			// Band-limit edges only where they can alias.
			const float dt = r == rka::RANGE_AUDIO ? float(voice.freq * args.sampleTime) : 0.f;

			outputs[SIN_OUTPUT].setVoltage(rka::lfoVolts(rka::waveSin(p), unipolar), c);
			outputs[TRI_OUTPUT].setVoltage(rka::lfoVolts(rka::waveTri(p), unipolar), c);
			outputs[SAW_OUTPUT].setVoltage(rka::lfoVolts(rka::sawBlep(p, dt), unipolar), c);
			outputs[SQR_OUTPUT].setVoltage(rka::lfoVolts(rka::sqrBlep(p, pw, dt), unipolar), c);
			outputs[MORPH_OUTPUT].setVoltage(rka::lfoVolts(rka::shape(p, skew, morph, smooth, pw, dt), unipolar), c);
			outputs[RAND_OUTPUT].setVoltage(rka::lfoVolts(voice.random.value(float(voice.phasor.phase), glide), unipolar), c);
			outputs[EOC_OUTPUT].setVoltage(eocPulses[c].process(args.sampleTime) ? 10.f : 0.f, c);
		}
		for (int o = 0; o < OUTPUTS_LEN; o++)
			outputs[o].setChannels(channels);
	}
};


struct SwayWidget : ModuleWidget {
	SwayWidget(Sway* module) {
		using namespace SwayLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/sway/Sway.svg"), asset::plugin(pluginInstance, "res/sway/Sway-dark.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<rka::BigKnob>(mm2px(PARAM_RATE), module, Sway::RATE_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_SPREAD), module, Sway::SPREAD_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_CHANNELS), module, Sway::CHANNELS_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(PARAM_OFFSET), module, Sway::OFFSET_PARAM));
		addParam(createLightParamCentered<rka::AmberLatch>(mm2px(PARAM_ONESHOT), module, Sway::ONESHOT_PARAM, Sway::ONESHOT_LIGHT));

		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_CLOCK), module, Sway::CLOCK_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_RESET), module, Sway::RESET_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_HOLD), module, Sway::HOLD_INPUT));

		// Modulation columns: knob, attenuverter, CV input. FM's knob is RATE.
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_PW), module, Sway::PW_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_PHASE), module, Sway::PHASE_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_SKEW), module, Sway::SKEW_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_SMOOTH), module, Sway::SMOOTH_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_MORPH), module, Sway::MORPH_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_FM_ATT), module, Sway::FM_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_PW_ATT), module, Sway::PW_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_PHASE_ATT), module, Sway::PHASE_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_SKEW_ATT), module, Sway::SKEW_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_SMOOTH_ATT), module, Sway::SMOOTH_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_MORPH_ATT), module, Sway::MORPH_ATT_PARAM));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_FM), module, Sway::FM_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_PW), module, Sway::PW_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_PHASE), module, Sway::PHASE_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_SKEW), module, Sway::SKEW_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_SMOOTH), module, Sway::SMOOTH_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_MORPH), module, Sway::MORPH_INPUT));

		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SIN), module, Sway::SIN_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_TRI), module, Sway::TRI_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SAW), module, Sway::SAW_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SQR), module, Sway::SQR_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_MORPH), module, Sway::MORPH_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_RAND), module, Sway::RAND_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_EOC), module, Sway::EOC_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Sway* module = getModule<Sway>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Range",
			{"Slow (4.5 h – 1 s per cycle)", "Normal (0.008–128 Hz)", "Audio (8 Hz–8 kHz)"},
			&module->range));
	}
};


Model* modelSway = createModel<Sway, SwayWidget>("Sway");

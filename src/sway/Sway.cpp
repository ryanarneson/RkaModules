#include "plugin.hpp"
#include "SwayEngine.hpp"
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
		RATE_LIGHT,
		LIGHTS_LEN
	};

	// rka::Range, as an int so the context menu can point at it.
	int range = rka::RANGE_NORMAL;

	// The per-sample logic (rka::SwayEngine), fed from the ports each sample.
	rka::SwayEngine engine;
	rka::SwayFrame frame;
	rka::SwayResult result;

	// RATE's tooltip follows the range and the clock.
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Sway* sway = static_cast<Sway*>(module);
			if (!sway)
				return ParamQuantity::getDisplayValueString();
			if (sway->engine.clocked) {
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
		configLight(RATE_LIGHT, "Rate (channel 1)");
	}

	void onReset() override {
		range = rka::RANGE_NORMAL;
		engine.reset();
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

	void process(const ProcessArgs& args) override {
		static_assert(int(PARAMS_LEN) == int(rka::SwayFrame::PARAMS_LEN), "SwayFrame params follow ParamId");
		static_assert(int(INPUTS_LEN) == int(rka::SwayFrame::INPUTS_LEN), "SwayFrame inputs follow InputId");
		static_assert(int(OUTPUTS_LEN) == int(rka::SwayFrame::OUTPUTS_LEN), "SwayFrame outputs follow OutputId");
		static_assert(int(ONESHOT_PARAM) == int(rka::SwayFrame::ONESHOT) && int(MORPH_ATT_PARAM) == int(rka::SwayFrame::MORPH_ATT)
			&& int(CHANNELS_PARAM) == int(rka::SwayFrame::CHANNELS), "SwayFrame::Param ids");
		static_assert(int(HOLD_INPUT) == int(rka::SwayFrame::HOLD) && int(CLOCK_INPUT) == int(rka::SwayFrame::CLOCK)
			&& int(MORPH_INPUT) == int(rka::SwayFrame::MORPH_IN), "SwayFrame::Input ids");
		static_assert(int(EOC_OUTPUT) == int(rka::SwayFrame::EOC) && int(MORPH_OUTPUT) == int(rka::SwayFrame::MORPH_OUT), "SwayFrame::Output ids");
		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		for (int i = 0; i < INPUTS_LEN; i++) {
			frame.inputs[i].v = inputs[i].getVoltages();
			frame.inputs[i].channels = inputs[i].getChannels();
		}
		for (int o = 0; o < OUTPUTS_LEN; o++)
			frame.outputConnected[o] = outputs[o].isConnected();
		frame.range = range;

		engine.process(frame, args.sampleTime, args.sampleRate, result);

		lights[ONESHOT_LIGHT].setBrightness(result.oneShotLight);
		lights[RATE_LIGHT].setBrightnessSmooth(result.rateLight, args.sampleTime);
		for (int o = 0; o < OUTPUTS_LEN; o++) {
			for (int c = 0; c < result.channels; c++)
				outputs[o].setVoltage(result.out[o][c], c);
			outputs[o].setChannels(result.channels);
		}
	}
};


struct SwayWidget : ModuleWidget {
	SwayWidget(Sway* module) {
		using namespace SwayLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/sway/Sway.svg"), asset::plugin(pluginInstance, "res/sway/Sway-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_RATE), module, Sway::RATE_PARAM));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_RATE), module, Sway::RATE_LIGHT));
		addParam(createParamCentered<rka::FaceKnob>(mm2px(PARAM_SPREAD), module, Sway::SPREAD_PARAM));
		addParam(createParamCentered<rka::FaceKnob>(mm2px(PARAM_CHANNELS), module, Sway::CHANNELS_PARAM));

		// Modulation columns: knob on the limb, attenuverter, CV input. FM's
		// knob is RATE.
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_PW), module, Sway::PW_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_PHASE), module, Sway::PHASE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SKEW), module, Sway::SKEW_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SMOOTH), module, Sway::SMOOTH_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_MORPH), module, Sway::MORPH_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_FM_ATT), module, Sway::FM_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_PW_ATT), module, Sway::PW_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_PHASE_ATT), module, Sway::PHASE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_SKEW_ATT), module, Sway::SKEW_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_SMOOTH_ATT), module, Sway::SMOOTH_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_MORPH_ATT), module, Sway::MORPH_ATT_PARAM));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FM), module, Sway::FM_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_PW), module, Sway::PW_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_PHASE), module, Sway::PHASE_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SKEW), module, Sway::SKEW_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SMOOTH), module, Sway::SMOOTH_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_MORPH), module, Sway::MORPH_INPUT));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CLOCK), module, Sway::CLOCK_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RESET), module, Sway::RESET_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_HOLD), module, Sway::HOLD_INPUT));
		addParam(createParamCentered<rka::Toggle2>(mm2px(PARAM_OFFSET), module, Sway::OFFSET_PARAM));
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_ONESHOT), module, Sway::ONESHOT_PARAM, Sway::ONESHOT_LIGHT));

		// Outputs, each on its own ivory tile.
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_SIN), module, Sway::SIN_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_TRI), module, Sway::TRI_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_SAW), module, Sway::SAW_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_SQR), module, Sway::SQR_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_MORPH), module, Sway::MORPH_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_RAND), module, Sway::RAND_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_EOC), module, Sway::EOC_OUTPUT));
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

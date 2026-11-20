// Wake: a stereo echo (spec: docs/specs/2026-11-20-wake-design.md). The
// module maps params, ports, the Style button and patch state onto
// rka::WakeEngine; all behaviour lives there.
#include "plugin.hpp"
#include "WakeEngine.hpp"
#include "WakeLayout.hpp"
#include "widgets.hpp"

#include <cstdlib>


struct Wake : Module {
	enum ParamId {
		TIME_PARAM,
		TIME_ATT_PARAM,
		FEEDBACK_PARAM,
		TONE_PARAM,
		RATIO_PARAM,
		MIX_PARAM,
		DIFFUSE_PARAM,
		DRIFT_PARAM,
		STYLE_PARAM,
		FREEZE_PARAM,
		PINGPONG_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		FEEDBACK_CV_INPUT,
		CLOCK_INPUT,
		TIME_CV_INPUT,
		FREEZE_INPUT,
		MIX_CV_INPUT,
		L_INPUT,
		R_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		L_OUTPUT,
		R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		TAPE_LIGHT,
		ANALOG_LIGHT,
		DIGITAL_LIGHT,
		FREEZE_LIGHT,
		LIGHTS_LEN
	};

	// Seeded per instance, so two Wakes never drift alike.
	rka::WakeEngine engine;
	rka::WakeFrame frame;
	rka::WakeResult result;
	// 0 Tape, 1 Analog, 2 Digital; stepped by the Style button, saved.
	int style = 0;
	dsp::BooleanTrigger styleTrigger;

	// Time shows milliseconds or seconds; following a clock, the multiple.
	struct TimeQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Wake* wake = static_cast<Wake*>(module);
			if (wake && wake->engine.clockStep() >= 0)
				return string::f("%s clock", rka::wake::clockName(wake->engine.clockStep()));
			float s = rka::wake::timeSeconds(getValue());
			if (wake && s > wake->engine.maxSeconds())
				s = wake->engine.maxSeconds();
			return s < 1.f ? string::f("%.0f ms", s * 1000.f) : string::f("%.2f s", s);
		}
		// A typed value is milliseconds, or seconds if it ends in "s" alone
		// ("1.5 s"); "250" and "250 ms" are both a quarter of a second.
		void setDisplayValueString(std::string s) override {
			char* end = nullptr;
			const float v = std::strtof(s.c_str(), &end);
			if (end == s.c_str() || !(v > 0.f))
				return;
			while (*end == ' ')
				end++;
			const bool seconds = (*end == 's' || *end == 'S');
			setValue(rka::wake::timeKnob(seconds ? v : v / 1000.f));
		}
		std::string getUnit() override {
			return "";
		}
	};

	// Ratio shows which side is shorter, and by how much.
	struct RatioQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return rka::wake::ratioName(int(std::round(getValue())));
		}
		// A typed value is a setting's name ("L 2:3", "1:1") or its number,
		// 1 to 7.
		void setDisplayValueString(std::string s) override {
			for (int r = 0; r < rka::wake::kRatios; r++)
				if (string::lowercase(s) == string::lowercase(rka::wake::ratioName(r))) {
					setValue((float) r);
					return;
				}
			char* end = nullptr;
			const long n = std::strtol(s.c_str(), &end, 10);
			if (end != s.c_str() && *end == '\0' && n >= 1 && n <= rka::wake::kRatios)
				setValue((float) (n - 1));
		}
		std::string getUnit() override {
			return "";
		}
	};

	Wake() : engine(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const rka::WakeFrame defaults;
		typedef rka::WakeFrame F;
		configParam<TimeQuantity>(TIME_PARAM, 0.f, 1.f, defaults.params[F::TIME], "Time");
		configParam(TIME_ATT_PARAM, -1.f, 1.f, 0.f, "Time CV (1 V doubles the time at full)", "%", 0.f, 100.f);
		configParam(FEEDBACK_PARAM, 0.f, 1.f, defaults.params[F::FEEDBACK], "Feedback (over 100 % the repeats grow)", "%", 0.f,
		            100.f * rka::wake::FEEDBACK_MAX);
		configParam(TONE_PARAM, 0.f, 1.f, 0.5f, "Tone (left: repeats darken; right: they thin)", "%", 0.f, 200.f, -100.f);
		configParam<RatioQuantity>(RATIO_PARAM, 0.f, 6.f, 3.f, "Ratio (the shorter side's time against the other)");
		paramQuantities[RATIO_PARAM]->snapEnabled = true;
		configParam(MIX_PARAM, 0.f, 1.f, defaults.params[F::MIX], "Mix (input to echoes)", "%", 0.f, 100.f);
		configParam(DIFFUSE_PARAM, 0.f, 1.f, 0.f, "Diffuse (smears each repeat)", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, defaults.params[F::DRIFT], "Drift (slow waver of the echo time)", "%", 0.f, 100.f);
		configButton(STYLE_PARAM, "Style (Tape, Analog, Digital)");
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze (holds what is in the delay)", {"Off", "On"});
		// Randomize must not freeze the module (usually on an empty delay).
		paramQuantities[FREEZE_PARAM]->randomizeEnabled = false;
		configSwitch(PINGPONG_PARAM, 0.f, 1.f, 0.f, "Ping pong (echoes bounce left and right)", {"Off", "On"});
		configInput(FEEDBACK_CV_INPUT, "Feedback CV");
		configInput(CLOCK_INPUT, "Clock (Time becomes a multiple of it)");
		configInput(TIME_CV_INPUT, "Time CV");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(MIX_CV_INPUT, "Mix CV");
		configInput(L_INPUT, "Left (alone: both sides)");
		configInput(R_INPUT, "Right (alone: both sides)");
		configOutput(L_OUTPUT, "Left (alone: both sides, averaged)");
		configOutput(R_OUTPUT, "Right");
		configLight(TAPE_LIGHT, "Tape");
		configLight(ANALOG_LIGHT, "Analog");
		configLight(DIGITAL_LIGHT, "Digital");
		configLight(FREEZE_LIGHT, "Frozen");
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		style = 0;
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "style", json_integer(style));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "style");
		const json_int_t v = s && json_is_integer(s) ? json_integer_value(s) : 0;
		style = v >= 0 && v <= 2 ? (int) v : 0;
	}

	void process(const ProcessArgs& args) override {
		static_assert(int(TIME_PARAM) == int(rka::WakeFrame::TIME) && int(DRIFT_PARAM) == int(rka::WakeFrame::DRIFT)
			&& int(STYLE_PARAM) == int(rka::WakeFrame::PARAMS_LEN), "ParamId ids");

		if (styleTrigger.process(params[STYLE_PARAM].getValue() > 0.f))
			style = style + 1 < 3 ? style + 1 : 0;

		for (int i = 0; i < rka::WakeFrame::PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		// A polyphonic cable gives its first channel to a CV, and its sum
		// to an audio input.
		frame.timeCv = inputs[TIME_CV_INPUT].getVoltage();
		frame.feedbackCv = inputs[FEEDBACK_CV_INPUT].getVoltage();
		frame.mixCv = inputs[MIX_CV_INPUT].getVoltage();
		frame.clock = inputs[CLOCK_INPUT].getVoltage();
		frame.clockPatched = inputs[CLOCK_INPUT].isConnected();
		frame.freezeGate = inputs[FREEZE_INPUT].getVoltage();
		frame.inLPatched = inputs[L_INPUT].isConnected();
		frame.inRPatched = inputs[R_INPUT].isConnected();
		frame.inL = inputs[L_INPUT].getVoltageSum();
		frame.inR = inputs[R_INPUT].getVoltageSum();
		frame.outRPatched = outputs[R_OUTPUT].isConnected();
		frame.style = style;
		frame.freeze = params[FREEZE_PARAM].getValue() > 0.5f;
		frame.pingPong = params[PINGPONG_PARAM].getValue() > 0.5f;

		engine.process(frame, result);

		outputs[L_OUTPUT].setVoltage(result.l);
		outputs[R_OUTPUT].setVoltage(result.r);
		lights[TAPE_LIGHT].setBrightness(style == 0 ? 1.f : 0.f);
		lights[ANALOG_LIGHT].setBrightness(style == 1 ? 1.f : 0.f);
		lights[DIGITAL_LIGHT].setBrightness(style == 2 ? 1.f : 0.f);
		lights[FREEZE_LIGHT].setBrightness(result.frozen ? 1.f : 0.f);
	}

	// Bypassed: the dry input, either jack alone to both sides. The style
	// still shows.
	void processBypass(const ProcessArgs&) override {
		const bool l = inputs[L_INPUT].isConnected(), r = inputs[R_INPUT].isConnected();
		const float inL = l ? inputs[L_INPUT].getVoltageSum() : r ? inputs[R_INPUT].getVoltageSum() : 0.f;
		const float inR = r ? inputs[R_INPUT].getVoltageSum() : inL;
		outputs[L_OUTPUT].setVoltage(inL);
		outputs[R_OUTPUT].setVoltage(inR);
		lights[FREEZE_LIGHT].setBrightness(0.f);
	}
};


struct WakeWidget : ModuleWidget {
	WakeWidget(Wake* module) {
		using namespace WakeLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/wake/Wake.svg"),
		                     asset::plugin(pluginInstance, "res/wake/Wake-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_TIME), module, Wake::TIME_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_TIME_ATT), module, Wake::TIME_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_FEEDBACK), module, Wake::FEEDBACK_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_TONE), module, Wake::TONE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_RATIO), module, Wake::RATIO_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_MIX), module, Wake::MIX_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DIFFUSE), module, Wake::DIFFUSE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRIFT), module, Wake::DRIFT_PARAM));
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_STYLE), module, Wake::STYLE_PARAM));
		// Freeze's cap is lit whenever the delay is frozen, by the button
		// or by the gate; Ping Pong's by its own latch.
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_FREEZE), module, Wake::FREEZE_PARAM, Wake::FREEZE_LIGHT));
		addParam(createParamCentered<rka::CapLatch>(mm2px(PARAM_PINGPONG), module, Wake::PINGPONG_PARAM));

		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_TAPE), module, Wake::TAPE_LIGHT));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_ANALOG), module, Wake::ANALOG_LIGHT));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_DIGITAL), module, Wake::DIGITAL_LIGHT));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FEEDBACK_CV), module, Wake::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CLOCK), module, Wake::CLOCK_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_TIME_CV), module, Wake::TIME_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FREEZE), module, Wake::FREEZE_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_MIX_CV), module, Wake::MIX_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_L), module, Wake::L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_R), module, Wake::R_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Wake::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Wake::R_OUTPUT));
	}
};


Model* modelWake = createModel<Wake, WakeWidget>("Wake");

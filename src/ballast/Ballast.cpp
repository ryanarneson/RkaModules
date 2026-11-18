#include "plugin.hpp"
#include "BallastChain.hpp"
#include "BallastEngine.hpp"
#include "BallastLayout.hpp"
#include "widgets.hpp"

#include <cmath>


// A Level or Master knob shown in dB on the knob curve (2 p^2.41, unity at
// three quarters); "-inf" fully left.
struct BallastLevelQuantity : ParamQuantity {
	float getDisplayValue() override {
		const float g = rka::BallastEngine::curveExact(getValue());
		return g > 0.f ? 20.f * std::log10(g) : -INFINITY;
	}
	void setDisplayValue(float db) override {
		if (!std::isfinite(db)) {
			setValue(0.f);
			return;
		}
		// Invert 2 p^k: p = (10^(db/20) / 2)^(1/k).
		const float g = std::pow(10.f, db / 20.f);
		setValue(clamp(std::pow(g / 2.f, 1.f / 2.4094208f), 0.f, 1.f));
	}
	std::string getDisplayValueString() override {
		const float v = getDisplayValue();
		return std::isfinite(v) ? string::f("%+.1f", v) : "-inf";
	}
};


struct Ballast : Module {
	enum ParamId {
		LEVEL_1_PARAM,
		LEVEL_2_PARAM,
		LEVEL_3_PARAM,
		LEVEL_4_PARAM,
		MASTER_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_1L_INPUT,
		IN_1R_INPUT,
		IN_2L_INPUT,
		IN_2R_INPUT,
		IN_3L_INPUT,
		IN_3R_INPUT,
		IN_4L_INPUT,
		IN_4R_INPUT,
		CV_1_INPUT,
		CV_2_INPUT,
		CV_3_INPUT,
		CV_4_INPUT,
		MASTER_CV_INPUT,
		CHAIN_L_INPUT,
		CHAIN_R_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		L_OUTPUT,
		R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		CLIP_LIGHT,
		LIGHTS_LEN
	};

	// The most expanders walked to the right; a longer chain is cut here.
	static const int MAX_EXPANDERS = 8;

	rka::BallastEngine engine;
	rka::BallastFrame frame;
	rka::BallastResult result;
	bool dcBlock = true;
	// Whether the right-hand neighbour is an expander, kept up to date by
	// onExpanderChange, so the walk costs nothing with no expander there.
	bool expanderRight = false;

	Ballast() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < 4; c++) {
			const std::string n = string::f("%d", c + 1);
			configParam<BallastLevelQuantity>(LEVEL_1_PARAM + c, 0.f, 1.f, 0.75f, "Level " + n, " dB");
			configInput(IN_1L_INPUT + 2 * c, "Channel " + n + " left");
			configInput(IN_1R_INPUT + 2 * c, "Channel " + n + " right (normalled to left)");
			configInput(CV_1_INPUT + c, "Channel " + n + " level CV (0 V silent, 10 V opens to the knob)");
		}
		configParam<BallastLevelQuantity>(MASTER_PARAM, 0.f, 1.f, 0.75f, "Master", " dB");
		configInput(MASTER_CV_INPUT, "Master CV (0 V silent, 10 V opens to the knob)");
		configInput(CHAIN_L_INPUT, "Chain left (added before Master)");
		configInput(CHAIN_R_INPUT, "Chain right (normalled to chain left)");
		configOutput(L_OUTPUT, "Left (the mono sum when R is unpatched)");
		configOutput(R_OUTPUT, "Right");
		configLight(CLIP_LIGHT, "Clip (the output is in the soft limit, above 8 V)");
		// No bypass routes: bypass silences both outputs.
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onExpanderChange(const ExpanderChangeEvent& e) override {
		if (e.side == 1)
			expanderRight = rightExpander.module && dynamic_cast<rka::BallastExpander*>(rightExpander.module);
	}

	void onReset() override {
		dcBlock = true;
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "dcBlock", json_boolean(dcBlock));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "dcBlock");
		dcBlock = j ? json_boolean_value(j) : true;
	}

	static void port(rka::BallastPort& p, Input& in) {
		p.v = in.getVoltages();
		p.channels = in.getChannels();
	}

	void process(const ProcessArgs& args) override {
		static_assert(int(LEVEL_1_PARAM) == int(rka::BallastFrame::LEVEL_1)
			&& int(MASTER_PARAM) == int(rka::BallastFrame::MASTER)
			&& int(PARAMS_LEN) == int(rka::BallastFrame::PARAMS_LEN), "ParamId ids");

		for (int p = 0; p < PARAMS_LEN; p++)
			frame.params[p] = params[p].getValue();
		for (int c = 0; c < 4; c++) {
			port(frame.inL[c], inputs[IN_1L_INPUT + 2 * c]);
			port(frame.inR[c], inputs[IN_1R_INPUT + 2 * c]);
			port(frame.cv[c], inputs[CV_1_INPUT + c]);
		}
		port(frame.masterCv, inputs[MASTER_CV_INPUT]);
		port(frame.chainL, inputs[CHAIN_L_INPUT]);
		port(frame.chainR, inputs[CHAIN_R_INPUT]);
		frame.outL = outputs[L_OUTPUT].isConnected();
		frame.outR = outputs[R_OUTPUT].isConnected();
		frame.dcBlock = dcBlock;

		// The expanders to the right, walked on this sample (Rack holds its
		// engine lock while modules are added or removed, so the neighbours
		// cannot change under us; nothing is kept across samples).
		rka::BallastExpander* chain[MAX_EXPANDERS];
		int n = 0;
		if (expanderRight)
			n = rka::collectBallastExpanders(
				rightExpander.module, chain, MAX_EXPANDERS, [](Module* m) { return m->rightExpander.module; },
				[](Module* m) { return m->isBypassed(); },
				[](Module* m) { return dynamic_cast<rka::BallastExpander*>(m); });
		rka::linkBallastExpanders(chain, n);

		engine.process(frame, result, chain, n);

		outputs[L_OUTPUT].setVoltage(result.l);
		outputs[R_OUTPUT].setVoltage(result.r);
		lights[CLIP_LIGHT].setBrightnessSmooth(result.clip ? 1.f : 0.f, args.sampleTime);
	}
};


struct BallastWidget : ModuleWidget {
	BallastWidget(Ballast* module) {
		using namespace BallastLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/ballast/Ballast.svg"),
		                     asset::plugin(pluginInstance, "res/ballast/Ballast-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		const Vec levels[4] = {PARAM_LEVEL_1, PARAM_LEVEL_2, PARAM_LEVEL_3, PARAM_LEVEL_4};
		const Vec cvs[4] = {INPUT_CV_1, INPUT_CV_2, INPUT_CV_3, INPUT_CV_4};
		const Vec insL[4] = {INPUT_IN_1L, INPUT_IN_2L, INPUT_IN_3L, INPUT_IN_4L};
		const Vec insR[4] = {INPUT_IN_1R, INPUT_IN_2R, INPUT_IN_3R, INPUT_IN_4R};
		for (int c = 0; c < 4; c++) {
			addParam(createParamCentered<rka::FaceKnob>(mm2px(levels[c]), module, Ballast::LEVEL_1_PARAM + c));
			addInput(createInputCentered<rka::BrassPort>(mm2px(cvs[c]), module, Ballast::CV_1_INPUT + c));
			addInput(createInputCentered<rka::BrassPort>(mm2px(insL[c]), module, Ballast::IN_1L_INPUT + 2 * c));
			addInput(createInputCentered<rka::BrassPort>(mm2px(insR[c]), module, Ballast::IN_1R_INPUT + 2 * c));
		}
		addParam(createParamCentered<rka::FaceKnob>(mm2px(PARAM_MASTER), module, Ballast::MASTER_PARAM));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_MASTER_CV), module, Ballast::MASTER_CV_INPUT));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_CLIP), module, Ballast::CLIP_LIGHT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CHAIN_L), module, Ballast::CHAIN_L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CHAIN_R), module, Ballast::CHAIN_R_INPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Ballast::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Ballast::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Ballast* module = getModule<Ballast>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("DC blocker (3 Hz high-pass on the outputs)", "", &module->dcBlock));
	}
};


Model* modelBallast = createModel<Ballast, BallastWidget>("Ballast");

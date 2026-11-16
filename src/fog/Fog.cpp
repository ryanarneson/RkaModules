#include "plugin.hpp"
#include "FogEngine.hpp"
#include "FogLayout.hpp"
#include "widgets.hpp"


struct Fog : Module {
	enum ParamId {
		VISIBILITY_PARAM,
		SIZE_PARAM,
		DAMP_PARAM,
		BLOOM_PARAM,
		DRIFT_PARAM,
		MIX_PARAM,
		SIZE_ATT_PARAM,
		DAMP_ATT_PARAM,
		BLOOM_ATT_PARAM,
		DRIFT_ATT_PARAM,
		MIX_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_L_INPUT,
		IN_R_INPUT,
		SIZE_CV_INPUT,
		DAMP_CV_INPUT,
		BLOOM_CV_INPUT,
		DRIFT_CV_INPUT,
		MIX_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// The per-sample logic (rka::FogEngine), fed from the ports each sample.
	rka::FogEngine engine;
	rka::FogFrame frame;
	rka::FogResult result;

	int mode = 0;
	int driftRate = 0;
	int lowCut = 1;
	bool dryKill = false;

	Fog() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(VISIBILITY_PARAM, 0.f, 1.f, 0.5f, "Visibility", " s", 200.f, 0.4f);
		configParam(SIZE_PARAM, 0.f, 1.f, 0.5f, "Size", "%", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, 0.5f, "Damp", "%", 0.f, 100.f);
		configParam(BLOOM_PARAM, 0.f, 1.f, 0.5f, "Bloom", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.5f, "Drift", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);
		static const char* const names[5] = {"Size", "Damp", "Bloom", "Drift", "Mix"};
		for (int i = 0; i < 5; i++) {
			const std::string n = names[i];
			configParam(SIZE_ATT_PARAM + i, -1.f, 1.f, 0.f, n + " CV amount", "%", 0.f, 100.f);
			configInput(SIZE_CV_INPUT + i, n + " CV");
		}
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (normalled to left)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		mode = 0;
		driftRate = 0;
		lowCut = 1;
		dryKill = false;
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "mode", json_integer(mode));
		json_object_set_new(root, "driftRate", json_integer(driftRate));
		json_object_set_new(root, "lowCut", json_integer(lowCut));
		json_object_set_new(root, "dryKill", json_boolean(dryKill));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "mode");
		if (j)
			mode = clamp(int(json_integer_value(j)), 0, 3);
		j = json_object_get(root, "driftRate");
		if (j)
			driftRate = clamp(int(json_integer_value(j)), 0, 1);
		j = json_object_get(root, "lowCut");
		if (j)
			lowCut = clamp(int(json_integer_value(j)), 0, 2);
		j = json_object_get(root, "dryKill");
		if (j)
			dryKill = json_boolean_value(j);
	}

	void process(const ProcessArgs& args) override {
		static_assert(VISIBILITY_PARAM == 0 && int(VISIBILITY_PARAM) == int(rka::FogFrame::VISIBILITY)
			&& SIZE_PARAM == 1 && int(SIZE_PARAM) == int(rka::FogFrame::SIZE)
			&& DAMP_PARAM == 2 && int(DAMP_PARAM) == int(rka::FogFrame::DAMP)
			&& BLOOM_PARAM == 3 && int(BLOOM_PARAM) == int(rka::FogFrame::BLOOM)
			&& DRIFT_PARAM == 4 && int(DRIFT_PARAM) == int(rka::FogFrame::DRIFT)
			&& MIX_PARAM == 5 && int(MIX_PARAM) == int(rka::FogFrame::MIX)
			&& SIZE_ATT_PARAM == 6 && int(SIZE_ATT_PARAM) == int(rka::FogFrame::SIZE_ATT)
			&& DAMP_ATT_PARAM == 7 && int(DAMP_ATT_PARAM) == int(rka::FogFrame::DAMP_ATT)
			&& BLOOM_ATT_PARAM == 8 && int(BLOOM_ATT_PARAM) == int(rka::FogFrame::BLOOM_ATT)
			&& DRIFT_ATT_PARAM == 9 && int(DRIFT_ATT_PARAM) == int(rka::FogFrame::DRIFT_ATT)
			&& MIX_ATT_PARAM == 10 && int(MIX_ATT_PARAM) == int(rka::FogFrame::MIX_ATT)
			&& PARAMS_LEN == 11 && int(PARAMS_LEN) == int(rka::FogFrame::PARAMS_LEN), "ParamId ids");
		static_assert(IN_L_INPUT == 0 && IN_R_INPUT == 1 && SIZE_CV_INPUT == 2
			&& DAMP_CV_INPUT == 3 && BLOOM_CV_INPUT == 4 && DRIFT_CV_INPUT == 5
			&& MIX_CV_INPUT == 6 && INPUTS_LEN == 7, "InputId ids");
		static_assert(OUT_L_OUTPUT == 0 && OUT_R_OUTPUT == 1 && OUTPUTS_LEN == 2, "OutputId ids");
		static_assert(LIGHTS_LEN == 0, "LightId ids");

		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		frame.inL.v = inputs[IN_L_INPUT].getVoltages();
		frame.inL.channels = inputs[IN_L_INPUT].getChannels();
		frame.inR.v = inputs[IN_R_INPUT].getVoltages();
		frame.inR.channels = inputs[IN_R_INPUT].getChannels();
		for (int i = 0; i < 5; i++) {
			frame.cv[i].v = inputs[SIZE_CV_INPUT + i].getVoltages();
			frame.cv[i].channels = inputs[SIZE_CV_INPUT + i].getChannels();
		}
		frame.mode = mode;
		frame.driftRate = driftRate;
		frame.lowCut = lowCut;
		frame.dryKill = dryKill;

		engine.process(frame, result);

		outputs[OUT_L_OUTPUT].setVoltage(result.outL);
		outputs[OUT_R_OUTPUT].setVoltage(result.outR);
	}
};


struct FogWidget : ModuleWidget {
	FogWidget(Fog* module) {
		using namespace FogLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/fog/Fog.svg"), asset::plugin(pluginInstance, "res/fog/Fog-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_VISIBILITY), module, Fog::VISIBILITY_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SIZE), module, Fog::SIZE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DAMP), module, Fog::DAMP_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_BLOOM), module, Fog::BLOOM_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRIFT), module, Fog::DRIFT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_MIX), module, Fog::MIX_PARAM));

		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_SIZE_ATT), module, Fog::SIZE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DAMP_ATT), module, Fog::DAMP_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_BLOOM_ATT), module, Fog::BLOOM_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DRIFT_ATT), module, Fog::DRIFT_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_MIX_ATT), module, Fog::MIX_ATT_PARAM));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SIZE_CV), module, Fog::SIZE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DAMP_CV), module, Fog::DAMP_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_BLOOM_CV), module, Fog::BLOOM_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DRIFT_CV), module, Fog::DRIFT_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_MIX_CV), module, Fog::MIX_CV_INPUT));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_IN_L), module, Fog::IN_L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_IN_R), module, Fog::IN_R_INPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Fog::OUT_L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Fog::OUT_R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Fog* module = getModule<Fog>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Mode",
			{"Haar", "Fret", "Smoke", "Ice"}, &module->mode));
		menu->addChild(createIndexPtrSubmenuItem("Drift rate",
			{"Slow", "Very slow"}, &module->driftRate));
		menu->addChild(createIndexPtrSubmenuItem("Low cut",
			{"Off", "50 Hz", "100 Hz"}, &module->lowCut));
		menu->addChild(createBoolPtrMenuItem("Dry kill (wet only)", "", &module->dryKill));
	}
};


Model* modelFog = createModel<Fog, FogWidget>("Fog");

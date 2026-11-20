#include "plugin.hpp"
#include "HadalEngine.hpp"
#include "HadalLayout.hpp"
#include "widgets.hpp"


struct Hadal : Module {
	enum ParamId {
		DEPTH_PARAM,
		RUMBLE_PARAM,
		HISS_PARAM,
		CRACKLE_PARAM,
		DRIFT_PARAM,
		DEPTH_ATT_PARAM,
		RUMBLE_ATT_PARAM,
		HISS_ATT_PARAM,
		CRACKLE_ATT_PARAM,
		DRIFT_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		DEPTH_CV_INPUT,
		RUMBLE_CV_INPUT,
		HISS_CV_INPUT,
		CRACKLE_CV_INPUT,
		DRIFT_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		RUMBLE_OUTPUT,
		HISS_OUTPUT,
		CRACKLE_OUTPUT,
		L_OUTPUT,
		R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// The per-sample logic (rka::HadalEngine), seeded per instance so two
	// Hadals never sound alike.
	rka::HadalEngine engine;
	rka::HadalFrame frame;
	rka::HadalResult result;

	Hadal() : engine(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.5f, "Depth", " km", 0.f, 11.f);
		configParam(RUMBLE_PARAM, 0.f, 1.f, 0.7f, "Rumble", "%", 0.f, 100.f);
		configParam(HISS_PARAM, 0.f, 1.f, 0.3f, "Hiss", "%", 0.f, 100.f);
		configParam(CRACKLE_PARAM, 0.f, 1.f, 0.f, "Crackle", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.25f, "Drift", "%", 0.f, 100.f);
		static const char* const names[5] = {"Depth", "Rumble", "Hiss", "Crackle", "Drift"};
		for (int i = 0; i < 5; i++) {
			const std::string n = names[i];
			configParam(DEPTH_ATT_PARAM + i, -1.f, 1.f, 0.f, n + " CV amount", "%", 0.f, 100.f);
			configInput(DEPTH_CV_INPUT + i, n + " CV");
		}
		configOutput(RUMBLE_OUTPUT, "Rumble (patching it removes Rumble from the L/R mix)");
		configOutput(HISS_OUTPUT, "Hiss (patching it removes Hiss from the L/R mix)");
		configOutput(CRACKLE_OUTPUT, "Crackle (patching it removes Crackle from the L/R mix)");
		configOutput(L_OUTPUT, "Left (the mono sum when R is unpatched)");
		configOutput(R_OUTPUT, "Right");
		// No bypass routes: Hadal has no input, so bypass silences it.
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		engine.reset();
	}

	void process(const ProcessArgs& args) override {
		static_assert(int(DEPTH_PARAM) == int(rka::HadalFrame::DEPTH) && int(DRIFT_PARAM) == int(rka::HadalFrame::DRIFT)
			&& int(DEPTH_ATT_PARAM) == int(rka::HadalFrame::DEPTH_ATT)
			&& int(DRIFT_ATT_PARAM) == int(rka::HadalFrame::DRIFT_ATT)
			&& int(PARAMS_LEN) == int(rka::HadalFrame::PARAMS_LEN), "ParamId ids");
		static_assert(int(RUMBLE_OUTPUT) == int(rka::HadalFrame::OUT_RUMBLE) && int(R_OUTPUT) == int(rka::HadalFrame::OUT_R)
			&& int(OUTPUTS_LEN) == int(rka::HadalFrame::OUTPUTS_LEN), "OutputId ids");

		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		for (int i = 0; i < 5; i++) {
			frame.cv[i].v = inputs[DEPTH_CV_INPUT + i].getVoltages();
			frame.cv[i].channels = inputs[DEPTH_CV_INPUT + i].getChannels();
		}
		for (int o = 0; o < OUTPUTS_LEN; o++)
			frame.patched[o] = outputs[o].isConnected();

		engine.process(frame, result);

		outputs[RUMBLE_OUTPUT].setVoltage(result.rumble);
		outputs[HISS_OUTPUT].setVoltage(result.hiss);
		outputs[CRACKLE_OUTPUT].setVoltage(result.crackle);
		outputs[L_OUTPUT].setVoltage(result.l);
		outputs[R_OUTPUT].setVoltage(result.r);
	}
};


struct HadalWidget : ModuleWidget {
	HadalWidget(Hadal* module) {
		using namespace HadalLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/hadal/Hadal.svg"),
		                     asset::plugin(pluginInstance, "res/hadal/Hadal-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_DEPTH), module, Hadal::DEPTH_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_RUMBLE), module, Hadal::RUMBLE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_HISS), module, Hadal::HISS_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_CRACKLE), module, Hadal::CRACKLE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRIFT), module, Hadal::DRIFT_PARAM));

		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DEPTH_ATT), module, Hadal::DEPTH_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_RUMBLE_ATT), module, Hadal::RUMBLE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_HISS_ATT), module, Hadal::HISS_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_CRACKLE_ATT), module, Hadal::CRACKLE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DRIFT_ATT), module, Hadal::DRIFT_ATT_PARAM));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DEPTH_CV), module, Hadal::DEPTH_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RUMBLE_CV), module, Hadal::RUMBLE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_HISS_CV), module, Hadal::HISS_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CRACKLE_CV), module, Hadal::CRACKLE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DRIFT_CV), module, Hadal::DRIFT_CV_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_RUMBLE), module, Hadal::RUMBLE_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_HISS), module, Hadal::HISS_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_CRACKLE), module, Hadal::CRACKLE_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Hadal::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Hadal::R_OUTPUT));
	}
};


Model* modelHadal = createModel<Hadal, HadalWidget>("Hadal");

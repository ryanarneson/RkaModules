#include "plugin.hpp"
#include "PanCore.hpp"
#include "PanLayout.hpp"
#include "widgets.hpp"

#include <atomic>


// Pan, Ballast's pan expander (spec: docs/specs/2026-11-17-pan-design.md).
// The work happens in rka::PanCore, inside Ballast's calls; this module only
// supplies its controls (readFrame), counts its own process() calls between
// links (idle), and lights LINK.
struct Pan : Module, rka::PanCore {
	enum ParamId {
		PAN_1_PARAM,
		PAN_2_PARAM,
		PAN_3_PARAM,
		PAN_4_PARAM,
		PAN_1_ATT_PARAM,
		PAN_2_ATT_PARAM,
		PAN_3_ATT_PARAM,
		PAN_4_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		PAN_1_CV_INPUT,
		PAN_2_CV_INPUT,
		PAN_3_CV_INPUT,
		PAN_4_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUTPUTS_LEN
	};
	enum LightId {
		LINK_LIGHT,
		LIGHTS_LEN
	};

	// Pan's process() calls since Ballast last linked it: Pan's thread
	// increments, Ballast's thread (in linked()) resets.
	std::atomic<int> idle_{1 << 20};
	int stereoMode = 0;   // 0 true pan, 1 balance
	int panLaw = 0;       // 0 no boost, 1 constant loudness
	float sampleRate = 48000.f;

	Pan() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < 4; c++) {
			const std::string n = string::f("%d", c + 1);
			configParam(PAN_1_PARAM + c, 0.f, 1.f, 0.5f, "Pan " + n, "%", 0.f, 200.f, -100.f);
			configParam(PAN_1_ATT_PARAM + c, -1.f, 1.f, 0.f, "Pan " + n + " CV amount", "%", 0.f, 100.f);
			configInput(PAN_1_CV_INPUT + c, "Pan " + n + " CV (+-5 V sweeps the width at full amount)");
		}
		configLight(LINK_LIGHT, "Linked to a Ballast");
		sampleRate = APP ? APP->engine->getSampleRate() : 48000.f;
		setSampleRate(sampleRate);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		sampleRate = e.sampleRate;
		setSampleRate(e.sampleRate);
	}

	void onReset() override {
		stereoMode = 0;
		panLaw = 0;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "stereoMode", json_integer(stereoMode));
		json_object_set_new(root, "panLaw", json_integer(panLaw));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "stereoMode");
		const int m = j && json_is_integer(j) ? (int) json_integer_value(j) : 0;
		stereoMode = m == 1 ? 1 : 0;
		json_t* l = json_object_get(root, "panLaw");
		panLaw = l && json_is_integer(l) && json_integer_value(l) == 1 ? 1 : 0;
	}

	// Called from Ballast's thread, once per sample.
	void readFrame(rka::PanFrame& f) override {
		for (int c = 0; c < 4; c++) {
			f.knob[c] = params[PAN_1_PARAM + c].getValue();
			f.att[c] = params[PAN_1_ATT_PARAM + c].getValue();
			f.cv[c].v = inputs[PAN_1_CV_INPUT + c].getVoltages();
			f.cv[c].channels = inputs[PAN_1_CV_INPUT + c].getChannels();
		}
		f.stereoMode = stereoMode;
		f.panLaw = panLaw;
	}

	int idle() const override {
		return idle_.load(std::memory_order_relaxed);
	}

	void linked() override {
		rka::PanCore::linked();
		idle_.store(0, std::memory_order_relaxed);
	}

	void process(const ProcessArgs& args) override {
		const int n = idle_.load(std::memory_order_relaxed);
		if (n < (1 << 20))
			idle_.store(n + 1, std::memory_order_relaxed);
		lights[LINK_LIGHT].setBrightness(n < (int) (0.05f * args.sampleRate) ? 1.f : 0.f);
	}
};


struct PanWidget : ModuleWidget {
	PanWidget(Pan* module) {
		using namespace PanLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/pan/Pan.svg"),
		                     asset::plugin(pluginInstance, "res/pan/Pan-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		const Vec knobs[4] = {PARAM_PAN_1, PARAM_PAN_2, PARAM_PAN_3, PARAM_PAN_4};
		const Vec atts[4] = {PARAM_PAN_1_ATT, PARAM_PAN_2_ATT, PARAM_PAN_3_ATT, PARAM_PAN_4_ATT};
		const Vec cvs[4] = {INPUT_PAN_1_CV, INPUT_PAN_2_CV, INPUT_PAN_3_CV, INPUT_PAN_4_CV};
		for (int c = 0; c < 4; c++) {
			addParam(createParamCentered<rka::LimbKnob>(mm2px(knobs[c]), module, Pan::PAN_1_PARAM + c));
			addParam(createParamCentered<rka::Atten>(mm2px(atts[c]), module, Pan::PAN_1_ATT_PARAM + c));
			addInput(createInputCentered<rka::BrassPort>(mm2px(cvs[c]), module, Pan::PAN_1_CV_INPUT + c));
		}
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_LINK), module, Pan::LINK_LIGHT));
	}

	void appendContextMenu(Menu* menu) override {
		Pan* module = getModule<Pan>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Pan law",
		                                         {"No boost (panning never raises a level)",
		                                          "Constant loudness (up to +3 dB at the sides)"},
		                                         &module->panLaw));
		menu->addChild(createIndexPtrSubmenuItem("Stereo sources", {"True pan (move the whole image)", "Balance"},
		                                         &module->stereoMode));
	}
};


Model* modelPan = createModel<Pan, PanWidget>("Pan");

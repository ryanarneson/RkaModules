// Neap: eight slow cycles and their tide (spec: docs/specs/2026-11-24-neap-design.md).
// The module maps params, ports, menu and lights onto rka::NeapEngine; all
// behaviour lives there. Each sample it also hands the shaped cycles to a
// NeapExpander (Spring) on its right.
#include "plugin.hpp"
#include "NeapEngine.hpp"
#include "NeapState.hpp"
#include "NeapExpander.hpp"
#include "NeapLayout.hpp"
#include "NeapRate.hpp"
#include "widgets.hpp"

#include <cmath>

struct Neap : Module {
	enum ParamId { RATE_PARAM, RATE_ATT_PARAM, WEIGHT_PARAM, MARK_PARAM, PARAMS_LEN };
	enum InputId { RATE_CV_INPUT, WEIGHT_CV_INPUT, RESET_INPUT, INPUTS_LEN };
	enum OutputId {
		C1_OUTPUT, C2_OUTPUT, C3_OUTPUT, C4_OUTPUT, C5_OUTPUT, C6_OUTPUT, C7_OUTPUT, C8_OUTPUT,
		ALL_OUTPUT, TIDE_OUTPUT, HIGH_OUTPUT, OUTPUTS_LEN
	};
	enum LightId {
		P1_LIGHT, P2_LIGHT, P3_LIGHT, P4_LIGHT, P5_LIGHT, P6_LIGHT, P7_LIGHT, P8_LIGHT,
		HIGH_LIGHT, LIGHTS_LEN
	};

	rka::NeapEngine engine;
	rka::NeapFrame frame;
	rka::NeapResult result;

	// Menu state (the frame's defaults), saved with the patch.
	bool drift = true;
	int wave = int(rka::neap::WAVE_TRIANGLE);
	int level = 0;   // 0: +-5 V, 1: 0-10 V

	// RATE shows the periods of cycles 1 and 8 at the knob, with the
	// attenuverter and the RATE CV input as they are now.
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			float att = 0.f, cv = 0.f;
			if (module) {
				att = module->params[RATE_ATT_PARAM].getValue();
				Input& in = module->inputs[RATE_CV_INPUT];
				cv = in.isConnected() ? in.getVoltage() : 0.f;
			}
			char buf[64];
			rka::neap::rateTooltip(getValue(), att, cv, buf, sizeof buf);
			return buf;
		}
		std::string getUnit() override {
			return "";
		}
	};

	Neap() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<RateQuantity>(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate (periods of cycles 1 and 8)");
		configParam(RATE_ATT_PARAM, -1.f, 1.f, 0.f, "Rate CV amount", "%", 0.f, 100.f);
		configParam(WEIGHT_PARAM, -1.f, 1.f, 0.f, "Weight (fast \xE2\x86\x90 \xE2\x86\x92 slow)", "%", 0.f, 100.f);
		configParam(MARK_PARAM, -5.f, 5.f, 2.f, "Mark (HIGH threshold)", " V");
		configInput(RATE_CV_INPUT, "Rate CV (1 V/oct)");
		configInput(WEIGHT_CV_INPUT, "Weight CV (5 V: the knob's travel)");
		configInput(RESET_INPUT, "Reset (all cycles to phase 0)");
		for (int i = 0; i < 8; i++) {
			configOutput(C1_OUTPUT + i, string::f("Cycle %d", i + 1));
			configLight(P1_LIGHT + i, string::f("Cycle %d", i + 1));
		}
		configOutput(ALL_OUTPUT, "All cycles (polyphonic, 8 channels)");
		configOutput(TIDE_OUTPUT, "Tide (the weighted sum)");
		configOutput(HIGH_OUTPUT, "High (gate: Tide above Mark)");
		configLight(HIGH_LIGHT, "High");
		frame.sampleTime = APP ? APP->engine->getSampleTime() : 1.f / 48000.f;
	}

	void onReset() override {
		drift = true;
		wave = int(rka::neap::WAVE_TRIANGLE);
		level = 0;
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "drift", json_boolean(drift));
		json_object_set_new(root, "wave", json_integer(wave));
		json_object_set_new(root, "level", json_integer(level));
		return root;
	}

	static rka::neap::JsonValue jv(json_t* j) {
		if (!j)
			return rka::neap::JsonValue::missing();
		if (json_is_boolean(j))
			return rka::neap::JsonValue::boolean(json_is_true(j));
		if (json_is_integer(j))
			return rka::neap::JsonValue::integer(json_integer_value(j));
		return rka::neap::JsonValue::other();
	}

	void dataFromJson(json_t* root) override {
		const rka::neap::MenuState m = rka::neap::parseMenu(
			jv(json_object_get(root, "drift")), jv(json_object_get(root, "wave")),
			jv(json_object_get(root, "level")));
		drift = m.drift;
		wave = m.wave;
		level = m.level;
	}

	static float cv(Input& in) {
		return in.isConnected() ? in.getVoltage() : 0.f;
	}

	void process(const ProcessArgs& args) override {
		frame.sampleTime = args.sampleTime;
		frame.rate = params[RATE_PARAM].getValue();
		frame.rateAtt = params[RATE_ATT_PARAM].getValue();
		frame.rateCv = cv(inputs[RATE_CV_INPUT]);
		frame.weight = params[WEIGHT_PARAM].getValue();
		frame.weightCv = cv(inputs[WEIGHT_CV_INPUT]);
		frame.mark = params[MARK_PARAM].getValue();
		frame.reset = cv(inputs[RESET_INPUT]);
		frame.drift = drift;
		frame.unipolar = level == 1;
		frame.wave = wave == 1 ? rka::neap::WAVE_SINE : rka::neap::WAVE_TRIANGLE;

		engine.process(frame, result);

		for (int i = 0; i < 8; i++) {
			outputs[C1_OUTPUT + i].setVoltage(result.cycle[i]);
			lights[P1_LIGHT + i].setBrightness(result.light[i]);
		}
		outputs[ALL_OUTPUT].setChannels(8);
		outputs[ALL_OUTPUT].writeVoltages(result.cycle);
		outputs[TIDE_OUTPUT].setVoltage(result.tide);
		outputs[HIGH_OUTPUT].setVoltage(result.high);
		lights[HIGH_LIGHT].setBrightness(result.high > 5.f ? 1.f : 0.f);

		Module* m = rightExpander.module;
		if (m && !m->isBypassed())
			if (rka::NeapExpander* e = dynamic_cast<rka::NeapExpander*>(m))
				e->fromNeap(result.shaped, args.sampleTime);
	}
};


struct NeapWidget : ModuleWidget {
	NeapWidget(Neap* module) {
		using namespace NeapLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/neap/Neap.svg"),
		                     asset::plugin(pluginInstance, "res/neap/Neap-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::DialKnob>(mm2px(PARAM_RATE), module, Neap::RATE_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_RATE_ATT), module, Neap::RATE_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_WEIGHT), module, Neap::WEIGHT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_MARK), module, Neap::MARK_PARAM));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RATE_CV), module, Neap::RATE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_WEIGHT_CV), module, Neap::WEIGHT_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RESET), module, Neap::RESET_INPUT));

		const Vec outs[8] = {OUTPUT_C1, OUTPUT_C2, OUTPUT_C3, OUTPUT_C4, OUTPUT_C5, OUTPUT_C6, OUTPUT_C7, OUTPUT_C8};
		for (int i = 0; i < 8; i++)
			addOutput(createOutputCentered<rka::BrassPort>(mm2px(outs[i]), module, Neap::C1_OUTPUT + i));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_ALL), module, Neap::ALL_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_TIDE), module, Neap::TIDE_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_HIGH), module, Neap::HIGH_OUTPUT));

		const Vec lamps[8] = {LIGHT_P1, LIGHT_P2, LIGHT_P3, LIGHT_P4, LIGHT_P5, LIGHT_P6, LIGHT_P7, LIGHT_P8};
		for (int i = 0; i < 8; i++)
			addChild(createLightCentered<rka::JewelLight>(mm2px(lamps[i]), module, Neap::P1_LIGHT + i));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_HIGH), module, Neap::HIGH_LIGHT));
	}

	// Places a new module directly to the right, pushing neighbours along
	// (as Ballast does for Pan), as one undoable action.
	void addExpander(Model* model) {
		Module* m = model->createModule();
		APP->engine->addModule(m);
		ModuleWidget* w = model->createModuleWidget(m);
		APP->scene->rack->setModulePosForce(w, Vec(box.pos.x + box.size.x, box.pos.y));
		APP->scene->rack->addModule(w);
		history::ModuleAdd* h = new history::ModuleAdd;
		h->name = "create " + model->name;
		h->setModule(w);
		APP->history->push(h);
	}

	void appendContextMenu(Menu* menu) override {
		Neap* module = getModule<Neap>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Drift (slow random wander of each rate)", "", &module->drift));
		menu->addChild(createIndexPtrSubmenuItem("Wave", {"Triangle", "Sine"}, &module->wave));
		menu->addChild(createIndexPtrSubmenuItem("Level", {"\xC2\xB1" "5 V", "0\xE2\x80\x93" "10 V"}, &module->level));

		// A Spring already sitting directly to the right greys the item out.
		Module* right = module->rightExpander.module;
		const bool hasSpring = right && dynamic_cast<rka::NeapExpander*>(right);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Add Spring expander", "", [this]() { addExpander(modelSpring); }, hasSpring));
	}
};


Model* modelNeap = createModel<Neap, NeapWidget>("Neap");

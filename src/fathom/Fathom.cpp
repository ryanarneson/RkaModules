#include "plugin.hpp"
#include "FathomEngine.hpp"
#include "FathomLayout.hpp"
#include "widgets.hpp"


struct Fathom : Module {
	enum ParamId {
		PITCH_PARAM,
		SUB_PARAM,
		DETUNE_PARAM,
		SPREAD_PARAM,
		DRIFT_PARAM,
		CUTOFF_PARAM,
		DETUNE_ATT_PARAM,
		DRIFT_ATT_PARAM,
		CUTOFF_ATT_PARAM,
		// Phase 2, appended so phase 1 patches load.
		RES_PARAM,
		DRIVE_PARAM,
		RES_ATT_PARAM,
		DRIVE_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		DETUNE_INPUT,
		DRIFT_INPUT,
		CUTOFF_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		SUB_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	// The per-sample logic (rka::FathomEngine), fed from the ports each sample.
	rka::FathomEngine engine;
	rka::FathomFrame frame;
	// Menu: 0 = one octave below, 1 = two.
	int subOctave = 0;
	// Menu: PITCH snaps to semitones unless free.
	bool freePitch = false;
	// Menu: index into rka::LadderType (LP24, LP12, BP, HP).
	int filterType = 0;
	// Menu: the sub goes through the filter instead of around it.
	bool subThroughFilter = false;

	// PITCH shows the note name while snapped.
	struct PitchQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Fathom* fathom = static_cast<Fathom*>(module);
			if (!fathom || fathom->freePitch)
				return string::f("%.2f st", getValue());
			static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
			const int n = int(std::round(getValue()));
			const int octave = 2 + int(std::floor(n / 12.f));
			return string::f("%s%d", names[((n % 12) + 12) % 12], octave);
		}

		std::string getUnit() override {
			return "";
		}
	};

	Fathom() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<PitchQuantity>(PITCH_PARAM, -24.f, 24.f, 0.f, "Pitch");
		configParam(SUB_PARAM, 0.f, 1.f, 0.5f, "Sub level", "%", 0.f, 100.f);
		configParam(DETUNE_PARAM, 0.f, 1.f, 0.25f, "Detune", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.6f, "Stereo spread", "%", 0.f, 100.f);
		configParam(DRIFT_PARAM, 0.f, 1.f, 0.25f, "Drift", "%", 0.f, 100.f);
		configParam(CUTOFF_PARAM, 0.f, 10.f, std::log2(1200.f / 20.f), "Cutoff", " Hz", 2.f, 20.f);
		configParam(DETUNE_ATT_PARAM, -1.f, 1.f, 0.f, "Detune CV amount", "%", 0.f, 100.f);
		configParam(DRIFT_ATT_PARAM, -1.f, 1.f, 0.f, "Drift CV amount", "%", 0.f, 100.f);
		configParam(CUTOFF_ATT_PARAM, -1.f, 1.f, 0.f, "Cutoff CV amount (1 V/oct at 100%)", "%", 0.f, 100.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.2f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.25f, "Drive", "%", 0.f, 100.f);
		configParam(RES_ATT_PARAM, -1.f, 1.f, 0.f, "Resonance CV amount", "%", 0.f, 100.f);
		configParam(DRIVE_ATT_PARAM, -1.f, 1.f, 0.f, "Drive CV amount", "%", 0.f, 100.f);
		configInput(VOCT_INPUT, "1 V/oct pitch");
		configInput(DETUNE_INPUT, "Detune CV");
		configInput(DRIFT_INPUT, "Drift CV");
		configInput(CUTOFF_INPUT, "Cutoff CV");
		configInput(RES_INPUT, "Resonance CV");
		configInput(DRIVE_INPUT, "Drive CV");
		configOutput(LEFT_OUTPUT, "Left");
		configOutput(RIGHT_OUTPUT, "Right");
		configOutput(SUB_OUTPUT, "Sub (sine, before SUB and CUTOFF)");
		setFreePitch(false);
	}

	// Snapping back on also rounds the knob, so the note the tooltip shows
	// is the note that sounds.
	void setFreePitch(bool free) {
		freePitch = free;
		paramQuantities[PITCH_PARAM]->snapEnabled = !free;
		if (!free)
			params[PITCH_PARAM].setValue(std::round(params[PITCH_PARAM].getValue()));
	}

	void onReset() override {
		subOctave = 0;
		filterType = 0;
		subThroughFilter = false;
		setFreePitch(false);
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "subOctave", json_integer(subOctave));
		json_object_set_new(root, "freePitch", json_boolean(freePitch));
		json_object_set_new(root, "filterType", json_integer(filterType));
		json_object_set_new(root, "subThroughFilter", json_boolean(subThroughFilter));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "subOctave");
		if (s)
			subOctave = clamp(int(json_integer_value(s)), 0, 1);
		json_t* f = json_object_get(root, "freePitch");
		if (f)
			setFreePitch(json_boolean_value(f));
		json_t* t = json_object_get(root, "filterType");
		if (t)
			filterType = clamp(int(json_integer_value(t)), 0, 3);
		json_t* b = json_object_get(root, "subThroughFilter");
		if (b)
			subThroughFilter = json_boolean_value(b);
	}

	void process(const ProcessArgs& args) override {
		// Every id as on main (saved patches depend on them) and as the frame
		// numbers it.
		static_assert(PITCH_PARAM == 0 && int(PITCH_PARAM) == int(rka::FathomFrame::PITCH)
			&& SUB_PARAM == 1 && int(SUB_PARAM) == int(rka::FathomFrame::SUB)
			&& DETUNE_PARAM == 2 && int(DETUNE_PARAM) == int(rka::FathomFrame::DETUNE)
			&& SPREAD_PARAM == 3 && int(SPREAD_PARAM) == int(rka::FathomFrame::SPREAD)
			&& DRIFT_PARAM == 4 && int(DRIFT_PARAM) == int(rka::FathomFrame::DRIFT)
			&& CUTOFF_PARAM == 5 && int(CUTOFF_PARAM) == int(rka::FathomFrame::CUTOFF)
			&& DETUNE_ATT_PARAM == 6 && int(DETUNE_ATT_PARAM) == int(rka::FathomFrame::DETUNE_ATT)
			&& DRIFT_ATT_PARAM == 7 && int(DRIFT_ATT_PARAM) == int(rka::FathomFrame::DRIFT_ATT)
			&& CUTOFF_ATT_PARAM == 8 && int(CUTOFF_ATT_PARAM) == int(rka::FathomFrame::CUTOFF_ATT)
			&& RES_PARAM == 9 && int(RES_PARAM) == int(rka::FathomFrame::RES)
			&& DRIVE_PARAM == 10 && int(DRIVE_PARAM) == int(rka::FathomFrame::DRIVE)
			&& RES_ATT_PARAM == 11 && int(RES_ATT_PARAM) == int(rka::FathomFrame::RES_ATT)
			&& DRIVE_ATT_PARAM == 12 && int(DRIVE_ATT_PARAM) == int(rka::FathomFrame::DRIVE_ATT)
			&& PARAMS_LEN == 13 && int(PARAMS_LEN) == int(rka::FathomFrame::PARAMS_LEN), "ParamId ids");
		static_assert(VOCT_INPUT == 0 && int(VOCT_INPUT) == int(rka::FathomFrame::VOCT)
			&& DETUNE_INPUT == 1 && int(DETUNE_INPUT) == int(rka::FathomFrame::DETUNE_IN)
			&& DRIFT_INPUT == 2 && int(DRIFT_INPUT) == int(rka::FathomFrame::DRIFT_IN)
			&& CUTOFF_INPUT == 3 && int(CUTOFF_INPUT) == int(rka::FathomFrame::CUTOFF_IN)
			&& RES_INPUT == 4 && int(RES_INPUT) == int(rka::FathomFrame::RES_IN)
			&& DRIVE_INPUT == 5 && int(DRIVE_INPUT) == int(rka::FathomFrame::DRIVE_IN)
			&& INPUTS_LEN == 6 && int(INPUTS_LEN) == int(rka::FathomFrame::INPUTS_LEN), "InputId ids");
		static_assert(LEFT_OUTPUT == 0 && int(LEFT_OUTPUT) == int(rka::FathomFrame::LEFT)
			&& RIGHT_OUTPUT == 1 && int(RIGHT_OUTPUT) == int(rka::FathomFrame::RIGHT)
			&& SUB_OUTPUT == 2 && int(SUB_OUTPUT) == int(rka::FathomFrame::SUB_OUT)
			&& OUTPUTS_LEN == 3 && int(OUTPUTS_LEN) == int(rka::FathomFrame::OUTPUTS_LEN), "OutputId ids");
		static_assert(LIGHTS_LEN == 0, "LightId ids");
		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		for (int i = 0; i < INPUTS_LEN; i++)
			frame.inputs[i] = inputs[i].getVoltage();
		for (int o = 0; o < OUTPUTS_LEN; o++)
			frame.outputConnected[o] = outputs[o].isConnected();
		frame.subOctave = subOctave;
		frame.filterType = filterType;
		frame.subThroughFilter = subThroughFilter;

		float out[OUTPUTS_LEN];
		engine.process(frame, args.sampleRate, out);
		for (int o = 0; o < OUTPUTS_LEN; o++)
			outputs[o].setVoltage(out[o]);
	}
};


struct FathomWidget : ModuleWidget {
	FathomWidget(Fathom* module) {
		using namespace FathomLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/fathom/Fathom.svg"), asset::plugin(pluginInstance, "res/fathom/Fathom-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		// PITCH, the origin, on its numbered face; the voice knobs and
		// V/OCT on the limb around it.
		addParam(createParamCentered<rka::DialKnob>(mm2px(PARAM_PITCH), module, Fathom::PITCH_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SUB), module, Fathom::SUB_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SPREAD), module, Fathom::SPREAD_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DETUNE), module, Fathom::DETUNE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRIFT), module, Fathom::DRIFT_PARAM));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_VOCT), module, Fathom::VOCT_INPUT));

		// The filter on the next ping out.
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_RES), module, Fathom::RES_PARAM));
		addParam(createParamCentered<rka::FaceKnob>(mm2px(PARAM_CUTOFF), module, Fathom::CUTOFF_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRIVE), module, Fathom::DRIVE_PARAM));

		// Attenuverters and CV jacks: RES, DRIFT, CUTOFF, DETUNE, DRIVE.
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_RES_ATT), module, Fathom::RES_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DRIFT_ATT), module, Fathom::DRIFT_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_CUTOFF_ATT), module, Fathom::CUTOFF_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DETUNE_ATT), module, Fathom::DETUNE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DRIVE_ATT), module, Fathom::DRIVE_ATT_PARAM));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RES_CV), module, Fathom::RES_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DRIFT_CV), module, Fathom::DRIFT_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CUTOFF_CV), module, Fathom::CUTOFF_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DETUNE_CV), module, Fathom::DETUNE_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DRIVE_CV), module, Fathom::DRIVE_INPUT));

		// Outputs, each on its own ivory tile.
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_SUBOUT), module, Fathom::SUB_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_LEFT), module, Fathom::LEFT_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_RIGHT), module, Fathom::RIGHT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Fathom* module = getModule<Fathom>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Sub octave",
			{"One octave down", "Two octaves down"},
			&module->subOctave));
		menu->addChild(createBoolMenuItem("Free pitch (no semitone snap)", "",
			[=]() { return module->freePitch; },
			[=](bool free) { module->setFreePitch(free); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Filter type",
			{"Low-pass 24 dB", "Low-pass 12 dB", "Band-pass", "High-pass"},
			&module->filterType));
		menu->addChild(createBoolPtrMenuItem("Sub through filter", "", &module->subThroughFilter));
	}
};


Model* modelFathom = createModel<Fathom, FathomWidget>("Fathom");

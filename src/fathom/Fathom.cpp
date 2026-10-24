#include "plugin.hpp"
#include "DroneVoice.hpp"
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
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		DETUNE_INPUT,
		DRIFT_INPUT,
		CUTOFF_INPUT,
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

	// PITCH 0 and V/OCT 0 V is C2.
	static constexpr float C2_HZ = 65.4064f;

	rka::DroneVoice voice;
	rka::OutputStage stage;
	// Menu: 0 = one octave below, 1 = two.
	int subOctave = 0;
	// Menu: PITCH snaps to semitones unless free.
	bool freePitch = false;

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
		configInput(VOCT_INPUT, "1 V/oct pitch");
		configInput(DETUNE_INPUT, "Detune CV");
		configInput(DRIFT_INPUT, "Drift CV");
		configInput(CUTOFF_INPUT, "Cutoff CV");
		configOutput(LEFT_OUTPUT, "Left");
		configOutput(RIGHT_OUTPUT, "Right");
		configOutput(SUB_OUTPUT, "Sub (sine, before SUB and CUTOFF)");
		setFreePitch(false);
		voice.reset(C2_HZ);
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
		setFreePitch(false);
		voice.reset(C2_HZ);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "subOctave", json_integer(subOctave));
		json_object_set_new(root, "freePitch", json_boolean(freePitch));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* s = json_object_get(root, "subOctave");
		if (s)
			subOctave = clamp(int(json_integer_value(s)), 0, 1);
		json_t* f = json_object_get(root, "freePitch");
		if (f)
			setFreePitch(json_boolean_value(f));
	}

	// Knob plus attenuated CV, where ±5 V at full attenuverter sweeps the
	// knob's whole range (as Sway).
	float modulated(int param, int input, int att) {
		ParamQuantity* q = paramQuantities[param];
		const float cv = inputs[input].getVoltage() / 10.f * params[att].getValue();
		return params[param].getValue() + cv * (q->getMaxValue() - q->getMinValue());
	}

	void process(const ProcessArgs& args) override {
		const float semis = params[PITCH_PARAM].getValue() + 12.f * inputs[VOCT_INPUT].getVoltage();
		const float f0 = C2_HZ * std::exp2(semis / 12.f);
		const float detune = clamp(modulated(DETUNE_PARAM, DETUNE_INPUT, DETUNE_ATT_PARAM), 0.f, 1.f);
		const float drift = clamp(modulated(DRIFT_PARAM, DRIFT_INPUT, DRIFT_ATT_PARAM), 0.f, 1.f);
		const float cutoffOct = clamp(params[CUTOFF_PARAM].getValue() + params[CUTOFF_ATT_PARAM].getValue() * inputs[CUTOFF_INPUT].getVoltage(), 0.f, 10.f);
		const float cutoffHz = 20.f * std::exp2(cutoffOct);

		const rka::DroneVoice::Out o = voice.process(f0, detune, params[SPREAD_PARAM].getValue(), drift,
		                                             params[SUB_PARAM].getValue(), subOctave + 1, args.sampleRate);
		float l = o.left, r = o.right;
		stage.process(cutoffHz, args.sampleRate, l, r);
		outputs[LEFT_OUTPUT].setVoltage(5.f * l);
		outputs[RIGHT_OUTPUT].setVoltage(5.f * r);
		outputs[SUB_OUTPUT].setVoltage(5.f * o.sub);
	}
};


struct FathomWidget : ModuleWidget {
	FathomWidget(Fathom* module) {
		using namespace FathomLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/fathom/Fathom.svg"), asset::plugin(pluginInstance, "res/fathom/Fathom-dark.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<rka::BigKnob>(mm2px(PARAM_PITCH), module, Fathom::PITCH_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_DRIFT), module, Fathom::DRIFT_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_SPREAD), module, Fathom::SPREAD_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_SUB), module, Fathom::SUB_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_DETUNE), module, Fathom::DETUNE_PARAM));
		addParam(createParamCentered<rka::BigKnob>(mm2px(PARAM_CUTOFF), module, Fathom::CUTOFF_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_CUTOFF_ATT), module, Fathom::CUTOFF_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_DETUNE_ATT), module, Fathom::DETUNE_ATT_PARAM));
		addParam(createParamCentered<rka::Trim>(mm2px(PARAM_DRIFT_ATT), module, Fathom::DRIFT_ATT_PARAM));

		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_VOCT), module, Fathom::VOCT_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_CUTOFF_CV), module, Fathom::CUTOFF_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_DETUNE_CV), module, Fathom::DETUNE_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_DRIFT_CV), module, Fathom::DRIFT_INPUT));

		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SUBOUT), module, Fathom::SUB_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_LEFT), module, Fathom::LEFT_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_RIGHT), module, Fathom::RIGHT_OUTPUT));
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
	}
};


Model* modelFathom = createModel<Fathom, FathomWidget>("Fathom");

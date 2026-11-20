// Rigging: six tuned strings (spec: docs/specs/2026-11-19-rigging-design.md).
// The module maps params, ports and the menu onto rka::RiggingEngine; all
// behaviour lives there.
#include "plugin.hpp"
#include "RiggingEngine.hpp"
#include "RiggingLayout.hpp"
#include "widgets.hpp"

#include <cstdlib>


struct Rigging : Module {
	enum ParamId {
		LEVEL_1_PARAM,
		LEVEL_2_PARAM,
		LEVEL_3_PARAM,
		LEVEL_4_PARAM,
		LEVEL_5_PARAM,
		LEVEL_6_PARAM,
		TUNE_PARAM,
		CHORD_PARAM,
		RING_PARAM,
		DAMP_PARAM,
		SPREAD_PARAM,
		MIX_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		CHORD_CV_INPUT,
		RING_CV_INPUT,
		DAMP_CV_INPUT,
		STRUM_INPUT,
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
		STRIKE_LIGHT,
		LIGHTS_LEN
	};

	// Seeded per instance, so two Riggings never strike identically.
	rka::RiggingEngine engine;
	rka::RiggingFrame frame;
	rka::RiggingResult result;
	// Menu: Tune moves freely instead of in semitones.
	bool freePitch = false;

	// Tune shows the note of string 1 while snapped.
	struct TuneQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			Rigging* rigging = static_cast<Rigging*>(module);
			if (!rigging || rigging->freePitch)
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

	// Chord shows the tuning's name.
	struct ChordQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return rka::rigging::chordName(int(std::round(getValue())));
		}
		// A typed value is a chord's name (or the start of one), or its
		// number, 1 to 7.
		void setDisplayValueString(std::string s) override {
			for (int c = 0; c < rka::rigging::kChords; c++) {
				const std::string name = rka::rigging::chordName(c);
				if (!s.empty() && s.size() <= name.size() && string::lowercase(name.substr(0, s.size())) == string::lowercase(s)) {
					setValue((float) c);
					return;
				}
			}
			char* end = nullptr;
			const long n = std::strtol(s.c_str(), &end, 10);
			if (end != s.c_str() && n >= 1 && n <= rka::rigging::kChords)
				setValue((float) (n - 1));
		}
		std::string getUnit() override {
			return "";
		}
	};

	// Ring shows the time to fall silent (at Damp 0).
	struct RingQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			const float s = rka::RiggingEngine::ringSeconds(getValue());
			return s < 10.f ? string::f("%.2f s", s) : string::f("%.0f s", s);
		}
		// A typed value is seconds.
		void setDisplayValueString(std::string s) override {
			const float seconds = std::strtof(s.c_str(), nullptr);
			if (seconds > 0.f)
				setValue(rka::RiggingEngine::ringKnob(seconds));
		}
		std::string getUnit() override {
			return "";
		}
	};

	// Spread shows the time from string 1's strike to string 6's.
	struct SpreadQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			const float s = rka::RiggingEngine::spreadSeconds(getValue());
			return s < 1.f ? string::f("%.0f ms", s * 1000.f) : string::f("%.2f s", s);
		}
		// A typed value is milliseconds, or seconds if it ends in "s" alone
		// ("0.5 s"); "250" and "250 ms" are both a quarter of a second.
		void setDisplayValueString(std::string s) override {
			char* end = nullptr;
			float v = std::strtof(s.c_str(), &end);
			if (end == s.c_str() || !(v >= 0.f))
				return;
			while (*end == ' ')
				end++;
			const bool seconds = (*end == 's' || *end == 'S');
			setValue(rka::RiggingEngine::spreadKnob(seconds ? v : v / 1000.f));
		}
		std::string getUnit() override {
			return "";
		}
	};

	Rigging() : engine(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const rka::RiggingFrame defaults;
		for (int i = 0; i < 6; i++)
			configParam(LEVEL_1_PARAM + i, 0.f, 1.f, 1.f, string::f("String %d level", i + 1), "%", 0.f, 100.f);
		configParam<TuneQuantity>(TUNE_PARAM, -24.f, 24.f, 0.f, "Tune (string 1)");
		configParam<ChordQuantity>(CHORD_PARAM, 0.f, 6.f, 2.f, "Chord");
		paramQuantities[CHORD_PARAM]->snapEnabled = true;
		configParam<RingQuantity>(RING_PARAM, 0.f, 1.f, defaults.params[rka::RiggingFrame::RING], "Ring (time to fall silent)");
		configParam(DAMP_PARAM, 0.f, 1.f, defaults.params[rka::RiggingFrame::DAMP], "Damp (how fast the brightness dies)", "%", 0.f, 100.f);
		configParam<SpreadQuantity>(SPREAD_PARAM, 0.f, 1.f, defaults.params[rka::RiggingFrame::SPREAD], "Spread (string 1 to string 6 of a strike)");
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix (input to strings)", "%", 0.f, 100.f);
		configInput(VOCT_INPUT, "1 V/oct pitch (STRUM unpatched: a new note strikes)");
		configInput(CHORD_CV_INPUT, "Chord CV");
		configInput(RING_CV_INPUT, "Ring CV");
		configInput(DAMP_CV_INPUT, "Damp CV");
		configInput(STRUM_INPUT, "Strum (trigger: strikes the strings)");
		configInput(L_INPUT, "Left (alone: both sides)");
		configInput(R_INPUT, "Right (alone: both sides)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configLight(STRIKE_LIGHT, "Strike");
		setFreePitch(false);
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	// Snapping back on also rounds the knob, so the note the tooltip shows
	// is the note that sounds.
	void setFreePitch(bool free) {
		freePitch = free;
		paramQuantities[TUNE_PARAM]->snapEnabled = !free;
		if (!free)
			params[TUNE_PARAM].setValue(std::round(params[TUNE_PARAM].getValue()));
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		setFreePitch(false);
		engine.reset();
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "freePitch", json_boolean(freePitch));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* f = json_object_get(root, "freePitch");
		setFreePitch(f && json_is_true(f));
	}

	void process(const ProcessArgs& args) override {
		static_assert(int(LEVEL_1_PARAM) == int(rka::RiggingFrame::LEVEL_1) && int(MIX_PARAM) == int(rka::RiggingFrame::MIX)
			&& int(PARAMS_LEN) == int(rka::RiggingFrame::PARAMS_LEN), "ParamId ids");

		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		// A polyphonic cable gives its first channel to a CV, and its sum
		// to an audio input.
		frame.voct = inputs[VOCT_INPUT].getVoltage();
		frame.chordCv = inputs[CHORD_CV_INPUT].getVoltage();
		frame.ringCv = inputs[RING_CV_INPUT].getVoltage();
		frame.dampCv = inputs[DAMP_CV_INPUT].getVoltage();
		frame.strum = inputs[STRUM_INPUT].getVoltage();
		frame.strumPatched = inputs[STRUM_INPUT].isConnected();
		frame.inLPatched = inputs[L_INPUT].isConnected();
		frame.inRPatched = inputs[R_INPUT].isConnected();
		frame.inL = inputs[L_INPUT].getVoltageSum();
		frame.inR = inputs[R_INPUT].getVoltageSum();
		frame.freePitch = freePitch;

		engine.process(frame, result);

		outputs[L_OUTPUT].setVoltage(result.l);
		outputs[R_OUTPUT].setVoltage(result.r);
		lights[STRIKE_LIGHT].setBrightnessSmooth(result.light ? 1.f : 0.f, args.sampleTime);
	}

	// Bypassed: the dry input, either jack alone to both sides.
	void processBypass(const ProcessArgs&) override {
		const bool l = inputs[L_INPUT].isConnected(), r = inputs[R_INPUT].isConnected();
		const float inL = l ? inputs[L_INPUT].getVoltageSum() : r ? inputs[R_INPUT].getVoltageSum() : 0.f;
		const float inR = r ? inputs[R_INPUT].getVoltageSum() : inL;
		outputs[L_OUTPUT].setVoltage(inL);
		outputs[R_OUTPUT].setVoltage(inR);
		lights[STRIKE_LIGHT].setBrightness(0.f);
	}
};


struct RiggingWidget : ModuleWidget {
	RiggingWidget(Rigging* module) {
		using namespace RiggingLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/rigging/Rigging.svg"),
		                     asset::plugin(pluginInstance, "res/rigging/Rigging-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		const Vec levels[6] = {PARAM_LEVEL_1, PARAM_LEVEL_2, PARAM_LEVEL_3, PARAM_LEVEL_4, PARAM_LEVEL_5, PARAM_LEVEL_6};
		for (int i = 0; i < 6; i++)
			addParam(createParamCentered<rka::LimbKnob>(mm2px(levels[i]), module, Rigging::LEVEL_1_PARAM + i));
		addParam(createParamCentered<rka::DialKnob>(mm2px(PARAM_TUNE), module, Rigging::TUNE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_CHORD), module, Rigging::CHORD_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_RING), module, Rigging::RING_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DAMP), module, Rigging::DAMP_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SPREAD), module, Rigging::SPREAD_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_MIX), module, Rigging::MIX_PARAM));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_VOCT), module, Rigging::VOCT_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CHORD_CV), module, Rigging::CHORD_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RING_CV), module, Rigging::RING_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DAMP_CV), module, Rigging::DAMP_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_STRUM), module, Rigging::STRUM_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_L), module, Rigging::L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_R), module, Rigging::R_INPUT));

		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_STRIKE), module, Rigging::STRIKE_LIGHT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Rigging::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Rigging::R_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Rigging* module = getModule<Rigging>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolMenuItem("Free pitch (no semitone snap)", "",
			[=]() { return module->freePitch; },
			[=](bool free) { module->setFreePitch(free); }));
	}
};


Model* modelRigging = createModel<Rigging, RiggingWidget>("Rigging");

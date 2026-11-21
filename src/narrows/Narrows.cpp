// Narrows: a polyphonic stereo filter (spec:
// docs/specs/2026-11-20-narrows-design.md). The module maps params, ports
// and channel counts onto rka::NarrowsEngine; all behaviour lives there.
#include "plugin.hpp"
#include "NarrowsEngine.hpp"
#include "NarrowsLayout.hpp"
#include "widgets.hpp"


struct Narrows : Module {
	enum ParamId {
		CUTOFF_PARAM,
		DRIVE_PARAM,
		FM_PARAM,
		RES_PARAM,
		COMB_PARAM,
		SPREAD_PARAM,
		CORE_PARAM,
		MODE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		VOCT_INPUT,
		FM_CV_INPUT,
		RES_CV_INPUT,
		COMB_CV_INPUT,
		PING_INPUT,
		L_INPUT,
		R_INPUT,
		MODE_CV_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		L_OUTPUT,
		R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	rka::NarrowsEngine engine;
	rka::NarrowsFrame frame;
	rka::NarrowsResult result;

	// Cutoff shows hertz and takes a typed frequency ("440", "2.5 k").
	struct CutoffQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			const float hz = rka::narrows::cutoffHz(getValue());
			return hz < 1000.f ? string::f("%.1f Hz", hz) : string::f("%.2f kHz", hz / 1000.f);
		}
		void setDisplayValueString(std::string s) override {
			float hz = 0.f;
			if (rka::narrows::parseHz(s, hz))
				setValue(rka::narrows::cutoffKnob(hz));
		}
		std::string getUnit() override {
			return "";
		}
	};

	// Mode shows the response, or the blend between two.
	struct ModeQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return rka::narrows::modeName(getValue());
		}
		// A typed value is "LP", "BP" or "HP".
		void setDisplayValueString(std::string s) override {
			const std::string t = string::lowercase(string::trim(s));
			if (t == "lp")
				setValue(0.f);
			else if (t == "bp")
				setValue(0.5f);
			else if (t == "hp")
				setValue(1.f);
		}
		std::string getUnit() override {
			return "";
		}
	};

	Narrows() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<CutoffQuantity>(CUTOFF_PARAM, 0.f, 1.f, rka::narrows::CUTOFF_DEFAULT, "Cutoff");
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive (into the filter's saturation)", "%", 0.f, 100.f);
		configParam(FM_PARAM, -1.f, 1.f, 0.f, "FM (1 V per octave at full)", "%", 0.f, 100.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.f, "Resonance (self-oscillates from 95 %)", "%", 0.f, 100.f);
		configParam(COMB_PARAM, -1.f, 1.f, 0.f, "Comb (right: peaks at the cutoff; left: between)", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Spread (left cutoff down, right up; half an octave each at full)", "%", 0.f,
		            100.f);
		configSwitch(CORE_PARAM, 0.f, 1.f, 1.f, "Core", {"State (12 dB)", "Ladder (24 dB)"});
		configParam<ModeQuantity>(MODE_PARAM, 0.f, 1.f, 0.f, "Mode");
		configInput(VOCT_INPUT, "1 V/octave (polyphonic)");
		configInput(FM_CV_INPUT, "FM");
		configInput(RES_CV_INPUT, "Resonance CV");
		configInput(COMB_CV_INPUT, "Comb CV");
		configInput(PING_INPUT, "Ping (a trigger strikes the filter)");
		configInput(L_INPUT, "Left (alone: both sides; polyphonic)");
		configInput(R_INPUT, "Right (alone: both sides; polyphonic)");
		configInput(MODE_CV_INPUT, "Mode CV");
		configOutput(L_OUTPUT, "Left (alone: both sides, averaged)");
		configOutput(R_OUTPUT, "Right");
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		engine.reset();
	}

	static void read(Input& in, rka::NarrowsPort& port) {
		port.channels = in.getChannels();   // 0 with nothing patched
		const float* v = in.getVoltages();
		for (int c = 0; c < port.channels; c++)
			port.v[c] = v[c];
	}

	void process(const ProcessArgs&) override {
		static_assert(int(CUTOFF_PARAM) == int(rka::NarrowsFrame::CUTOFF) && int(MODE_PARAM) == int(rka::NarrowsFrame::MODE)
			&& int(PARAMS_LEN) == int(rka::NarrowsFrame::PARAMS_LEN), "ParamId ids");

		for (int i = 0; i < rka::NarrowsFrame::PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		read(inputs[L_INPUT], frame.inL);
		read(inputs[R_INPUT], frame.inR);
		read(inputs[VOCT_INPUT], frame.voct);
		read(inputs[FM_CV_INPUT], frame.fm);
		read(inputs[RES_CV_INPUT], frame.res);
		read(inputs[COMB_CV_INPUT], frame.comb);
		read(inputs[MODE_CV_INPUT], frame.mode);
		read(inputs[PING_INPUT], frame.ping);
		frame.outRConnected = outputs[R_OUTPUT].isConnected();

		engine.process(frame, result);

		outputs[L_OUTPUT].setChannels(result.channels);
		outputs[R_OUTPUT].setChannels(result.channels);
		outputs[L_OUTPUT].writeVoltages(result.l);
		outputs[R_OUTPUT].writeVoltages(result.r);
	}

	// Bypassed: the inputs, every channel, either jack alone to both sides.
	void processBypass(const ProcessArgs&) override {
		Input& l = inputs[L_INPUT].isConnected() ? inputs[L_INPUT] : inputs[R_INPUT];
		Input& r = inputs[R_INPUT].isConnected() ? inputs[R_INPUT] : inputs[L_INPUT];
		const int nl = std::max(l.getChannels(), 1), nr = std::max(r.getChannels(), 1);
		outputs[L_OUTPUT].setChannels(nl);
		outputs[R_OUTPUT].setChannels(nr);
		for (int c = 0; c < nl; c++)
			outputs[L_OUTPUT].setVoltage(l.getVoltage(c), c);
		for (int c = 0; c < nr; c++)
			outputs[R_OUTPUT].setVoltage(r.getVoltage(c), c);
	}
};


// Mode's face is a half turn: LP at nine o'clock, BP at noon, HP at three.
struct NarrowsModeKnob : rka::FaceKnob {
	NarrowsModeKnob() {
		minAngle = -0.5f * float(M_PI);
		maxAngle = 0.5f * float(M_PI);
	}
};


struct NarrowsWidget : ModuleWidget {
	NarrowsWidget(Narrows* module) {
		using namespace NarrowsLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/narrows/Narrows.svg"),
		                     asset::plugin(pluginInstance, "res/narrows/Narrows-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_CUTOFF), module, Narrows::CUTOFF_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRIVE), module, Narrows::DRIVE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_FM), module, Narrows::FM_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_RES), module, Narrows::RES_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_COMB), module, Narrows::COMB_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SPREAD), module, Narrows::SPREAD_PARAM));
		addParam(createParamCentered<rka::Toggle2>(mm2px(PARAM_CORE), module, Narrows::CORE_PARAM));
		addParam(createParamCentered<NarrowsModeKnob>(mm2px(PARAM_MODE), module, Narrows::MODE_PARAM));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_VOCT), module, Narrows::VOCT_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FM_CV), module, Narrows::FM_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RES_CV), module, Narrows::RES_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_COMB_CV), module, Narrows::COMB_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_PING), module, Narrows::PING_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_L), module, Narrows::L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_R), module, Narrows::R_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_MODE_CV), module, Narrows::MODE_CV_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Narrows::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Narrows::R_OUTPUT));
	}
};


Model* modelNarrows = createModel<Narrows, NarrowsWidget>("Narrows");

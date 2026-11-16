#include "plugin.hpp"
#include "SluiceEngine.hpp"
#include "SluiceLayout.hpp"
#include "widgets.hpp"


struct Sluice : Module {
	enum ParamId {
		LEVEL_A_PARAM,
		LEVEL_B_PARAM,
		LEVEL_C_PARAM,
		LEVEL_D_PARAM,
		CURVE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		IN_A_INPUT,
		IN_B_INPUT,
		IN_C_INPUT,
		IN_D_INPUT,
		CV_A_INPUT,
		CV_B_INPUT,
		CV_C_INPUT,
		CV_D_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		OUT_A_OUTPUT,
		OUT_B_OUTPUT,
		OUT_C_OUTPUT,
		OUT_D_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		A_LIGHT,
		B_LIGHT,
		C_LIGHT,
		D_LIGHT,
		LIGHTS_LEN
	};

	// The per-sample logic (rka::SluiceEngine), fed from the ports each sample.
	rka::SluiceEngine engine;
	rka::SluiceFrame frame;
	rka::SluiceResult result;

	Sluice() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		static const char* const names[4] = {"A", "B", "C", "D"};
		for (int r = 0; r < 4; r++) {
			const std::string n = names[r];
			configParam(LEVEL_A_PARAM + r, 0.f, 1.f, 0.f, "Level " + n, "%", 0.f, 100.f);
			configInput(IN_A_INPUT + r, n)->description = r == 0
				? "Silence when unpatched" : "Normalled from the row above when unpatched";
			configInput(CV_A_INPUT + r, n + " level CV");
			configOutput(OUT_A_OUTPUT + r, n)->description = r == 3
				? "Final mix" : "Mixes into the row below when unpatched";
			configLight(A_LIGHT + r, n + " open");
			configBypass(IN_A_INPUT + r, OUT_A_OUTPUT + r);
		}
		configSwitch(CURVE_PARAM, 0.f, 1.f, 1.f, "Response", {"Linear", "Exponential"});
	}

	void onReset() override {
		engine.reset();
	}

	void process(const ProcessArgs& args) override {
		static_assert(LEVEL_A_PARAM == 0 && int(LEVEL_A_PARAM) == int(rka::SluiceFrame::LEVEL_A)
			&& LEVEL_B_PARAM == 1 && int(LEVEL_B_PARAM) == int(rka::SluiceFrame::LEVEL_B)
			&& LEVEL_C_PARAM == 2 && int(LEVEL_C_PARAM) == int(rka::SluiceFrame::LEVEL_C)
			&& LEVEL_D_PARAM == 3 && int(LEVEL_D_PARAM) == int(rka::SluiceFrame::LEVEL_D)
			&& CURVE_PARAM == 4 && int(CURVE_PARAM) == int(rka::SluiceFrame::CURVE)
			&& PARAMS_LEN == 5 && int(PARAMS_LEN) == int(rka::SluiceFrame::PARAMS_LEN), "ParamId ids");
		static_assert(IN_A_INPUT == 0 && IN_D_INPUT == 3 && CV_A_INPUT == 4 && CV_D_INPUT == 7
			&& INPUTS_LEN == 8, "InputId ids");
		static_assert(OUT_A_OUTPUT == 0 && OUT_D_OUTPUT == 3 && OUTPUTS_LEN == 4, "OutputId ids");
		static_assert(A_LIGHT == 0 && D_LIGHT == 3 && LIGHTS_LEN == 4
			&& rka::SluiceFrame::ROWS == 4 && rka::SluiceFrame::MAX_CHANNELS == PORT_MAX_CHANNELS, "LightId ids");
		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		for (int r = 0; r < 4; r++) {
			frame.in[r].v = inputs[IN_A_INPUT + r].getVoltages();
			frame.in[r].channels = inputs[IN_A_INPUT + r].getChannels();
			frame.cv[r].v = inputs[CV_A_INPUT + r].getVoltages();
			frame.cv[r].channels = inputs[CV_A_INPUT + r].getChannels();
			frame.outConnected[r] = outputs[OUT_A_OUTPUT + r].isConnected();
		}

		engine.process(frame, args.sampleRate, result);

		for (int r = 0; r < 4; r++) {
			outputs[OUT_A_OUTPUT + r].setChannels(result.channels[r]);
			outputs[OUT_A_OUTPUT + r].writeVoltages(result.out[r]);
			lights[A_LIGHT + r].setBrightness(result.light[r]);
		}
	}
};


struct SluiceWidget : ModuleWidget {
	SluiceWidget(Sluice* module) {
		using namespace SluiceLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/sluice/Sluice.svg"), asset::plugin(pluginInstance, "res/sluice/Sluice-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		const Vec levels[4] = {PARAM_LEVEL_A, PARAM_LEVEL_B, PARAM_LEVEL_C, PARAM_LEVEL_D};
		const Vec ins[4] = {INPUT_A, INPUT_B, INPUT_C, INPUT_D};
		const Vec cvs[4] = {INPUT_A_CV, INPUT_B_CV, INPUT_C_CV, INPUT_D_CV};
		const Vec outs[4] = {OUTPUT_A, OUTPUT_B, OUTPUT_C, OUTPUT_D};
		const Vec lights[4] = {LIGHT_A, LIGHT_B, LIGHT_C, LIGHT_D};
		addParam(createParamCentered<rka::Toggle2>(mm2px(PARAM_CURVE), module, Sluice::CURVE_PARAM));
		for (int r = 0; r < 4; r++) {
			addParam(createParamCentered<rka::FaceKnob>(mm2px(levels[r]), module, Sluice::LEVEL_A_PARAM + r));
			addInput(createInputCentered<rka::BrassPort>(mm2px(ins[r]), module, Sluice::IN_A_INPUT + r));
			addInput(createInputCentered<rka::BrassPort>(mm2px(cvs[r]), module, Sluice::CV_A_INPUT + r));
			addOutput(createOutputCentered<rka::BrassPort>(mm2px(outs[r]), module, Sluice::OUT_A_OUTPUT + r));
			addChild(createLightCentered<rka::JewelLight>(mm2px(lights[r]), module, Sluice::A_LIGHT + r));
		}
	}
};


Model* modelSluice = createModel<Sluice, SluiceWidget>("Sluice");

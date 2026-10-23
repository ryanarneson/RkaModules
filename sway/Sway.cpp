#include "plugin.hpp"
#include "LfoCore.hpp"
#include "SwayLayout.hpp"
#include "widgets.hpp"


struct Sway : Module {
	enum ParamId {
		FREQ_PARAM,
		PW_PARAM,
		OFFSET_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		FM_INPUT,
		RESET_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		SIN_OUTPUT,
		TRI_OUTPUT,
		SAW_OUTPUT,
		SQR_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(PHASE_LIGHT, 2),
		LIGHTS_LEN
	};

	rka::LfoCore cores[PORT_MAX_CHANNELS];
	dsp::SchmittTrigger resetTriggers[PORT_MAX_CHANNELS];
	dsp::ClockDivider lightDivider;

	Sway() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// Knob value is log2(Hz): -7..7 is ~0.008 Hz to 128 Hz, default 2 Hz.
		configParam(FREQ_PARAM, -7.f, 7.f, 1.f, "Rate", " Hz", 2.f, 1.f);
		configParam(PW_PARAM, 0.01f, 0.99f, 0.5f, "Pulse width", "%", 0.f, 100.f);
		configSwitch(OFFSET_PARAM, 0.f, 1.f, 0.f, "Offset", {"Bipolar (±5 V)", "Unipolar (0–10 V)"});
		configInput(FM_INPUT, "Rate V/oct");
		configInput(RESET_INPUT, "Reset");
		configOutput(SIN_OUTPUT, "Sine");
		configOutput(TRI_OUTPUT, "Triangle");
		configOutput(SAW_OUTPUT, "Saw");
		configOutput(SQR_OUTPUT, "Square");
		configLight(PHASE_LIGHT, "Phase");
		lightDivider.setDivision(16);
	}

	void onReset() override {
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			cores[c].reset();
	}

	void process(const ProcessArgs& args) override {
		const int channels = std::max({1, inputs[FM_INPUT].getChannels(), inputs[RESET_INPUT].getChannels()});
		const float rate = params[FREQ_PARAM].getValue();
		const float pw = params[PW_PARAM].getValue();
		const bool unipolar = params[OFFSET_PARAM].getValue() > 0.5f;

		for (int c = 0; c < channels; c++) {
			rka::LfoCore& core = cores[c];
			// getPolyVoltage() lets a mono cable drive every channel.
			if (resetTriggers[c].process(inputs[RESET_INPUT].getPolyVoltage(c), 0.1f, 1.f))
				core.reset();

			core.step(rka::lfoFrequency(rate, inputs[FM_INPUT].getPolyVoltage(c)), args.sampleTime);

			outputs[SIN_OUTPUT].setVoltage(rka::lfoVolts(core.sin(), unipolar), c);
			outputs[TRI_OUTPUT].setVoltage(rka::lfoVolts(core.tri(), unipolar), c);
			outputs[SAW_OUTPUT].setVoltage(rka::lfoVolts(core.saw(), unipolar), c);
			outputs[SQR_OUTPUT].setVoltage(rka::lfoVolts(core.sqr(pw), unipolar), c);
		}
		for (int o = 0; o < OUTPUTS_LEN; o++)
			outputs[o].setChannels(channels);

		if (lightDivider.process()) {
			const float s = cores[0].sin();
			const float dt = args.sampleTime * lightDivider.getDivision();
			lights[PHASE_LIGHT + 0].setBrightnessSmooth(std::max(s, 0.f), dt);
			lights[PHASE_LIGHT + 1].setBrightnessSmooth(std::max(-s, 0.f), dt);
		}
	}
};


struct SwayWidget : ModuleWidget {
	SwayWidget(Sway* module) {
		using namespace SwayLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "sway/res/Sway.svg"), asset::plugin(pluginInstance, "sway/res/Sway-dark.svg")));

		addChild(createWidget<ThemedScrew>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ThemedScrew>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParamCentered<rka::BigKnob>(mm2px(PARAM_FREQ), module, Sway::FREQ_PARAM));
		addParam(createParamCentered<rka::SmallKnob>(mm2px(PARAM_PW), module, Sway::PW_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(PARAM_OFFSET), module, Sway::OFFSET_PARAM));

		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_FM), module, Sway::FM_INPUT));
		addInput(createInputCentered<ThemedPJ301MPort>(mm2px(INPUT_RESET), module, Sway::RESET_INPUT));

		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SIN), module, Sway::SIN_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_TRI), module, Sway::TRI_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SAW), module, Sway::SAW_OUTPUT));
		addOutput(createOutputCentered<ThemedPJ301MPort>(mm2px(OUTPUT_SQR), module, Sway::SQR_OUTPUT));

		addChild(createLightCentered<MediumLight<rka::CyanMagentaLight>>(mm2px(LIGHT_PHASE), module, Sway::PHASE_LIGHT));
	}
};


Model* modelSway = createModel<Sway, SwayWidget>("Sway");

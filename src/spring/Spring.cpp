// Spring: Neap's expander (spec: docs/specs/2026-11-24-neap-design.md).
// Everything that touches SpringCore happens inside fromNeap, on Neap's
// thread. Spring's own process() only copies the outputs and sets LINK, or
// writes zeros when Neap has stopped calling.
#include "plugin.hpp"
#include "neap/NeapExpander.hpp"
#include "SpringCore.hpp"
#include "SpringLayout.hpp"
#include "widgets.hpp"

struct Spring : Module, rka::NeapExpander {
	enum ParamId { PARAMS_LEN };
	enum InputId { INPUTS_LEN };
	enum OutputId {
		F1_OUTPUT, F3_OUTPUT, F5_OUTPUT, F7_OUTPUT,
		MIN23_OUTPUT, MAX23_OUTPUT, MIN67_OUTPUT, MAX67_OUTPUT,
		T2_OUTPUT, T4_OUTPUT, T6_OUTPUT, T8_OUTPUT,
		S14_OUTPUT, S58_OUTPUT, SODD_OUTPUT, SEVEN_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId { LINK_LIGHT, LIGHTS_LEN };

	rka::SpringRun run;

	Spring() {
		static_assert(int(OUTPUTS_LEN) == int(rka::SpringCore::N_OUT), "output ids");
		static_assert(int(T4_OUTPUT) - int(T2_OUTPUT) == 1 && int(T8_OUTPUT) - int(T2_OUTPUT) == 3, "T outputs");
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const char* names[OUTPUTS_LEN] = {
			"Fold 1 (0..10 V, 2 |x|)", "Fold 3", "Fold 5", "Fold 7",
			"Min of 2 and 3", "Max of 2 and 3", "Min of 6 and 7", "Max of 6 and 7",
			"Trigger 2 (rising zero crossing)", "Trigger 4", "Trigger 6", "Trigger 8",
			"Steps 1-4 (4 bits: above zero)", "Steps 5-8", "Steps odd (1 3 5 7)", "Steps even (2 4 6 8)"};
		for (int i = 0; i < OUTPUTS_LEN; i++)
			configOutput(i, names[i]);
		configLight(LINK_LIGHT, "Link (Neap at the left)");
	}

	void onReset() override {
		run.requestReset();
	}
	// While bypassed neither process() nor fromNeap runs, so the trigger
	// history would resume stale: ask for a reset on the next fromNeap.
	void onBypass(const BypassEvent& e) override {
		run.requestReset();
	}
	void onUnBypass(const UnBypassEvent& e) override {
		run.requestReset();
	}

	// Neap's thread: all per-sample work on the core.
	void fromNeap(const float x[8], float sampleTime) override {
		bool patched[4];
		for (int k = 0; k < 4; k++)
			patched[k] = outputs[T2_OUTPUT + k].isConnected();
		run.fromNeap(x, sampleTime, patched);
	}

	void process(const ProcessArgs& args) override {
		if (run.link.linkedAfterProcess()) {
			for (int i = 0; i < OUTPUTS_LEN; i++)
				outputs[i].setVoltage(run.core.out[i]);
			lights[LINK_LIGHT].setBrightness(1.f);
		}
		else {
			for (int i = 0; i < OUTPUTS_LEN; i++)
				outputs[i].setVoltage(0.f);
			lights[LINK_LIGHT].setBrightness(0.f);
			run.requestReset();
		}
	}
};


struct SpringWidget : ModuleWidget {
	SpringWidget(Spring* module) {
		using namespace SpringLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/spring/Spring.svg"),
		                     asset::plugin(pluginInstance, "res/spring/Spring-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		const Vec outs[16] = {
			OUTPUT_F1, OUTPUT_F3, OUTPUT_F5, OUTPUT_F7, OUTPUT_MIN23, OUTPUT_MAX23, OUTPUT_MIN67, OUTPUT_MAX67,
			OUTPUT_T2, OUTPUT_T4, OUTPUT_T6, OUTPUT_T8, OUTPUT_S14, OUTPUT_S58, OUTPUT_SODD, OUTPUT_SEVEN};
		for (int i = 0; i < 16; i++)
			addOutput(createOutputCentered<rka::BrassPort>(mm2px(outs[i]), module, i));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_LINK), module, Spring::LINK_LIGHT));
	}
};


Model* modelSpring = createModel<Spring, SpringWidget>("Spring");

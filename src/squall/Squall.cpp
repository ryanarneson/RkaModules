// Squall: random gates and CV (spec: docs/specs/2026-11-18-squall-design.md).
// The module maps params, ports, menus and the patch's saved state onto
// rka::SquallEngine; all behaviour lives there.
#include "plugin.hpp"
#include "SquallEngine.hpp"
#include "SquallLayout.hpp"
#include "widgets.hpp"

#include <cstdio>


// Weather's tooltip shows the average rate free-running: "1 per 9 s",
// "1 per 10 min", "8 per s".
struct WeatherQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		const double rate = rka::squall::weatherRate(getValue());
		char buf[64];
		if (rate >= 1.0) {
			std::snprintf(buf, sizeof(buf), "%.1f per s", rate);
		}
		else {
			const double gap = 1.0 / rate;
			if (gap >= 90.0)
				std::snprintf(buf, sizeof(buf), "1 per %.1f min", gap / 60.0);
			else
				std::snprintf(buf, sizeof(buf), "1 per %.1f s", gap);
		}
		return buf;
	}
	std::string getUnit() override {
		return "";
	}
};


struct Squall : Module {
	enum ParamId {
		WEATHER_PARAM,
		GUST_PARAM,
		MEMORY_PARAM,
		WEATHER_ATT_PARAM,
		GUST_ATT_PARAM,
		MEMORY_ATT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		WEATHER_CV_INPUT,
		GUST_CV_INPUT,
		MEMORY_CV_INPUT,
		CLOCK_INPUT,
		FREEZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		STEP_OUTPUT,
		SWELL_OUTPUT,
		GATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		EVENT_LIGHT,
		LIGHTS_LEN
	};

	// Seeded per instance, so two Squalls never make the same weather.
	rka::SquallEngine engine;
	rka::SquallFrame frame;
	rka::SquallResult result;
	int range = 0;       // 0 +-5 V, 1 0 ... 10 V, 2 +-2 V, 3 +-1 V
	int loopLength = 8;  // 4, 8 or 16

	Squall() : engine(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<WeatherQuantity>(WEATHER_PARAM, 0.f, 1.f, 0.5f, "Weather (average events, free-running)");
		configParam(GUST_PARAM, 0.f, 1.f, 0.3f, "Gust (how events bunch up)", "%", 0.f, 100.f);
		configParam(MEMORY_PARAM, 0.f, 1.f, 0.f, "Memory (chance of replaying the past)", "%", 0.f, 100.f);
		static const char* const names[3] = {"Weather", "Gust", "Memory"};
		for (int i = 0; i < 3; i++) {
			const std::string n = names[i];
			configParam(WEATHER_ATT_PARAM + i, -1.f, 1.f, 0.f, n + " CV amount", "%", 0.f, 100.f);
			configInput(WEATHER_CV_INPUT + i, n + " CV");
		}
		configInput(CLOCK_INPUT, "Clock (unpatched: Squall runs free)");
		configInput(FREEZE_INPUT, "Freeze (high: hold still)");
		configOutput(STEP_OUTPUT, "Step (a new value at each event)");
		configOutput(SWELL_OUTPUT, "Swell (a smooth drift through the same values)");
		configOutput(GATE_OUTPUT, "Gate (length follows Weather)");
		configLight(EVENT_LIGHT, "Event");
		// No bypass routes: Squall has no input to pass through, so bypass
		// holds every output at 0 V.
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		engine.reset();
		range = 0;
		loopLength = 8;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "range", json_integer(range));
		json_object_set_new(root, "loopLength", json_integer(loopLength));
		rka::SquallState s;
		engine.getState(s);
		json_t* hist = json_array();
		for (int i = 0; i < s.count; i++) {
			json_t* e = json_array();
			json_array_append_new(e, json_real(s.history[i].gap));
			json_array_append_new(e, json_real(s.history[i].value));
			json_array_append_new(e, json_real(s.history[i].length));
			json_array_append_new(hist, e);
		}
		json_object_set_new(root, "history", hist);
		json_object_set_new(root, "step", json_real(s.step01));
		json_t* sw = json_array();
		json_array_append_new(sw, json_real(s.swellStage1));
		json_array_append_new(sw, json_real(s.swellOut));
		json_object_set_new(root, "swell", sw);
		return root;
	}

	static float num(json_t* j, float fallback) {
		return j && json_is_number(j) ? (float) json_number_value(j) : fallback;
	}

	void dataFromJson(json_t* root) override {
		json_t* r = json_object_get(root, "range");
		const int rv = r && json_is_integer(r) ? (int) json_integer_value(r) : 0;
		range = (rv >= 0 && rv <= 3) ? rv : 0;
		json_t* l = json_object_get(root, "loopLength");
		const int lv = l && json_is_integer(l) ? (int) json_integer_value(l) : 8;
		loopLength = (lv == 4 || lv == 16) ? lv : 8;

		// The engine validates every field; anything missing or bad loads
		// as its default.
		rka::SquallState s;
		json_t* hist = json_object_get(root, "history");
		s.count = 0;
		if (hist && json_is_array(hist) && json_array_size(hist) <= (size_t) rka::squall::Memory::CAP) {
			for (size_t i = 0; i < json_array_size(hist); i++) {
				json_t* e = json_array_get(hist, i);
				rka::squall::Event ev;
				if (e && json_is_array(e) && json_array_size(e) == 3) {
					ev.gap = num(json_array_get(e, 0), -1.f);
					ev.value = num(json_array_get(e, 1), -1.f);
					ev.length = num(json_array_get(e, 2), -1.f);
				}
				s.history[s.count++] = ev;
			}
		}
		s.step01 = num(json_object_get(root, "step"), -1.f);
		json_t* sw = json_object_get(root, "swell");
		if (sw && json_is_array(sw) && json_array_size(sw) == 2) {
			s.swellStage1 = num(json_array_get(sw, 0), 1e9f);
			s.swellOut = num(json_array_get(sw, 1), 1e9f);
		}
		else {
			s.swellStage1 = s.swellOut = 1e9f;   // out of range: Swell starts at Step
		}
		engine.setState(s);
	}

	void process(const ProcessArgs& args) override {
		static_assert(int(WEATHER_PARAM) == int(rka::SquallFrame::WEATHER) && int(MEMORY_ATT_PARAM) == int(rka::SquallFrame::MEMORY_ATT)
			&& int(PARAMS_LEN) == int(rka::SquallFrame::PARAMS_LEN), "ParamId ids");

		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		for (int i = 0; i < 3; i++) {
			frame.cv[i].v = inputs[WEATHER_CV_INPUT + i].getVoltages();
			frame.cv[i].channels = inputs[WEATHER_CV_INPUT + i].getChannels();
		}
		frame.clockPatched = inputs[CLOCK_INPUT].isConnected();
		frame.clock = inputs[CLOCK_INPUT].getVoltage();
		frame.freeze = inputs[FREEZE_INPUT].getVoltage();
		frame.range = range;
		frame.loopLength = loopLength;

		engine.process(frame, result);

		outputs[STEP_OUTPUT].setVoltage(result.step);
		outputs[SWELL_OUTPUT].setVoltage(result.swell);
		outputs[GATE_OUTPUT].setVoltage(result.gate);
		lights[EVENT_LIGHT].setBrightnessSmooth(result.light ? 1.f : 0.f, args.sampleTime);
	}
};


struct SquallWidget : ModuleWidget {
	SquallWidget(Squall* module) {
		using namespace SquallLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/squall/Squall.svg"),
		                     asset::plugin(pluginInstance, "res/squall/Squall-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_WEATHER), module, Squall::WEATHER_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_GUST), module, Squall::GUST_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_MEMORY), module, Squall::MEMORY_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_WEATHER_ATT), module, Squall::WEATHER_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_GUST_ATT), module, Squall::GUST_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_MEMORY_ATT), module, Squall::MEMORY_ATT_PARAM));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_WEATHER_CV), module, Squall::WEATHER_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_GUST_CV), module, Squall::GUST_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_MEMORY_CV), module, Squall::MEMORY_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_CLOCK), module, Squall::CLOCK_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FREEZE), module, Squall::FREEZE_INPUT));

		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_EVENT), module, Squall::EVENT_LIGHT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_STEP), module, Squall::STEP_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_SWELL), module, Squall::SWELL_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_GATE), module, Squall::GATE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Squall* module = getModule<Squall>();
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Range (Step and Swell)",
		                                         {"±5 V", "0 to 10 V", "±2 V", "±1 V"}, &module->range));
		static const int lengths[3] = {4, 8, 16};
		menu->addChild(createSubmenuItem("Loop length", std::to_string(module->loopLength) + " events", [=](Menu* sub) {
			for (int k = 0; k < 3; k++) {
				const int L = lengths[k];
				sub->addChild(createCheckMenuItem(std::to_string(L) + " events", "",
				                                  [=]() { return module->loopLength == L; },
				                                  [=]() { module->loopLength = L; }));
			}
		}));
	}
};


Model* modelSquall = createModel<Squall, SquallWidget>("Squall");

#include "plugin.hpp"
#include "FriendlyList.hpp"
#include "Host.hpp"
#include "PatchState.hpp"
#include "ShoalLayout.hpp"
#include "ScopeScreen.hpp"
#include "widgets.hpp"

// Shoal: the Ornament & Crime firmware on a fake O&C. shoal::Host runs it;
// the module only maps jacks, buttons and lights onto the host, and the
// widget draws the screens and turns the encoders into host events. The
// lower screen is the friendly list (FriendlyList.hpp) or, with UI MODE set
// to hardware, the firmware's own menu.

static_assert(shoal::kFrameBytes == rka::PixelScreen::BYTES, "PixelScreen draws a Shoal frame");

// The firmware state in the patch ("data"): the format and the rules for its
// fields are in PatchState.hpp; this reads and writes the JSON and base64.

// A patch's state image: true, with `state` and `range` set, only for a
// version and a decoded state that PatchState.hpp accepts. Anything else is
// false: the caller restores the defaults.
static bool decodeState(json_t* rootJ, std::vector<uint8_t>& state, shoal::InputRange& range) {
	json_t* versionJ = json_object_get(rootJ, "version");
	json_t* stateJ = json_object_get(rootJ, "state");
	const bool versionIsInt = json_is_integer(versionJ);
	if (!shoal::patchVersionOk(versionIsInt, versionIsInt ? (long long) json_integer_value(versionJ) : 0) || !json_is_string(stateJ))
		return false;
	try {
		state = string::fromBase64(std::string(json_string_value(stateJ), json_string_length(stateJ)));
	}
	catch (std::exception&) {
		return false;
	}
	if (!shoal::patchStateSizeOk(state.size()))
		return false;
	json_t* rangeJ = json_object_get(rootJ, "inputRange");
	const bool rangeIsInt = json_is_integer(rangeJ);
	range = shoal::patchInputRange(rangeIsInt, rangeIsInt ? (long long) json_integer_value(rangeJ) : 0);
	return true;
}


struct Shoal : Module {
	enum ParamId {
		ENC_L_PUSH_PARAM,
		ENC_R_PUSH_PARAM,
		UP_PARAM,
		DOWN_PARAM,
		UI_MODE_PARAM,   // 1: friendly (the list), 0: hardware (the firmware's menu)
		PARAMS_LEN
	};
	enum InputId {
		TR1_INPUT,
		TR2_INPUT,
		TR3_INPUT,
		TR4_INPUT,
		CV1_INPUT,
		CV2_INPUT,
		CV3_INPUT,
		CV4_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		A_OUTPUT,
		B_OUTPUT,
		C_OUTPUT,
		D_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		TR1_LIGHT,
		TR2_LIGHT,
		TR3_LIGHT,
		TR4_LIGHT,
		// Two ids each, as when a pair showed the sign. Only the first is
		// used now (the voltage's size); the second stays so no id moves.
		ENUMS(A_LIGHT, 2),
		ENUMS(B_LIGHT, 2),
		ENUMS(C_LIGHT, 2),
		ENUMS(D_LIGHT, 2),
		// No UI MODE light: the latch lights its own cap (rka::CapLatch).
		LIGHTS_LEN
	};

	shoal::Host host;

	Shoal() : host(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configButton(ENC_L_PUSH_PARAM, "Left encoder push (shift-click to hold)");
		configButton(ENC_R_PUSH_PARAM, "Right encoder push (shift-click to hold)");
		configButton(UP_PARAM, "Up (shift-click to hold)");
		configButton(DOWN_PARAM, "Down (shift-click to hold)");
		// Saved with the patch as any param; a patch from before it existed
		// has no value for it and gets the default, the list.
		configSwitch(UI_MODE_PARAM, 0.f, 1.f, 1.f, "UI mode", {"Hardware (the firmware's menu)", "Friendly (the mouse list)"});
		paramQuantities[UI_MODE_PARAM]->randomizeEnabled = false;
		for (int i = 0; i < 4; i++) {
			configInput(TR1_INPUT + i, string::f("Trigger %d", i + 1));
			configInput(CV1_INPUT + i, string::f("CV %d", i + 1));
			configOutput(A_OUTPUT + i, string::f("%c", 'A' + i));
			configLight(TR1_LIGHT + i, string::f("Trigger %d", i + 1));
			configLight(A_LIGHT + 2 * i, string::f("Output %c", 'A' + i));
		}
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		host.setSampleRate(e.sampleRate);
	}

	// Rack calls the four below off the engine thread. A restore or reset
	// boots the replacement firmware here; the engine only swaps it in.

	json_t* dataToJson() override {
		uint8_t state[shoal::kStateBytes];
		shoal::InputRange range = shoal::InputRange::Hardware;
		host.snapshot(state, &range);
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "version", json_integer(shoal::kPatchVersion));
		json_object_set_new(rootJ, "state", json_string(string::toBase64(state, sizeof(state)).c_str()));
		json_object_set_new(rootJ, "inputRange", json_integer(shoal::patchInputRangeValue(range)));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		std::vector<uint8_t> state;
		shoal::InputRange range = shoal::InputRange::Hardware;
		if (decodeState(rootJ, state, range))
			host.requestRestore(state.data(), range);
		else
			host.requestRestore(nullptr, shoal::InputRange::Hardware);
		releaseButtons();
	}

	// Initialize: a new core's defaults (CopierMaschine), outputs at 0 V.
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		releaseButtons();
		shoal::ControlEvent ev;
		ev.type = shoal::ControlEvent::Reset;
		ev.index = 0;
		ev.value = 0;
		host.post(ev);
	}

	// Randomize leaves the firmware alone (and the buttons are not randomisable).
	void onRandomize(const RandomizeEvent& e) override {}

	// A button latched when the patch was saved is not held on load or reset.
	// Only the four buttons: any other param keeps its value.
	void releaseButtons() {
		params[ENC_L_PUSH_PARAM].setValue(0.f);
		params[ENC_R_PUSH_PARAM].setValue(0.f);
		params[UP_PARAM].setValue(0.f);
		params[DOWN_PARAM].setValue(0.f);
	}

	void process(const ProcessArgs& args) override {
		for (int ch = 0; ch < 4; ch++) {
			// An unpatched input reads 0 V; a poly cable gives channel 0.
			host.setCv(ch, inputs[CV1_INPUT + ch].getVoltage(0));
			host.setGate(ch, inputs[TR1_INPUT + ch].getVoltage(0));
		}
		host.setButton(shoal::BUTTON_L, params[ENC_L_PUSH_PARAM].getValue() > 0.5f);
		host.setButton(shoal::BUTTON_R, params[ENC_R_PUSH_PARAM].getValue() > 0.5f);
		host.setButton(shoal::BUTTON_UP, params[UP_PARAM].getValue() > 0.5f);
		host.setButton(shoal::BUTTON_DOWN, params[DOWN_PARAM].getValue() > 0.5f);

		host.process();

		for (int ch = 0; ch < 4; ch++) {
			const float v = host.out(ch);
			outputs[A_OUTPUT + ch].setVoltage(v);
			// Full at 5 V of either sign.
			lights[A_LIGHT + 2 * ch].setBrightness(std::fabs(v) / 5.f);
			// Smoothed, so a 1 ms trigger still shows.
			lights[TR1_LIGHT + ch].setBrightnessSmooth(host.gateHigh(ch) ? 1.f : 0.f, args.sampleTime);
		}
	}
};


// The output voltages over the scope: "+1.83V" four times, each under its
// channel letter on the panel.
struct OutputStrip : widget::Widget {
	Shoal* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (font) {
				nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, box.size.y * 0.85f);
				nvgFillColor(args.vg, RkaTheme::SIGNAL);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				const float cell = box.size.x / 4.f;
				for (int ch = 0; ch < 4; ch++) {
					float v = module ? module->outputs[Shoal::A_OUTPUT + ch].getVoltage() : 0.f;
					if (std::fabs(v) < 0.005f)
						v = 0.f;   // no "-0.00V"
					const std::string s = string::f("%+.2fV", v);
					nvgText(args.vg, cell * (ch + 0.5f), box.size.y / 2.f, s.c_str(), NULL);
				}
			}
		}
		Widget::drawLayer(args, layer);
	}
};


struct ShoalWidget : ModuleWidget {
	// With no module (module browser, screenshots): a host of our own, run
	// for a moment so the screens show the firmware.
	std::unique_ptr<shoal::Host> previewHost;
	// The lower screen: the firmware's menu (hardware mode) and the friendly
	// list over it; step() shows one of them.
	rka::PixelScreen* menuScreen = nullptr;
	// The upper screen: the firmware's scope frame, and in friendly mode its
	// lanes and the views Shoal draws itself.
	shoal::ScopeScreen* scopeScreen = nullptr;
	shoal::FriendlyList* friendlyList = nullptr;

	shoal::Host* host() {
		Shoal* m = getModule<Shoal>();
		return m ? &m->host : previewHost.get();
	}

	ShoalWidget(Shoal* module) {
		using namespace ShoalLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/shoal/Shoal.svg"), asset::plugin(pluginInstance, "res/shoal/Shoal-dark.svg")));

		if (!module) {
			previewHost.reset(new shoal::Host(random::u32()));
			const float rate = 48000.f;
			previewHost->setSampleRate(rate);
			for (int i = 0; i < int(rate / 4); i++)
				previewHost->process();
		}

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		scopeScreen = createWidget<shoal::ScopeScreen>(mm2px(SCREEN_SCOPE.pos));
		scopeScreen->box.size = mm2px(SCREEN_SCOPE.size);
		scopeScreen->colour = RkaTheme::SIGNAL;
		scopeScreen->source = [=](uint8_t* frame) {
			shoal::Host* h = host();
			if (!h)
				return false;
			uint8_t other[shoal::kFrameBytes];
			h->frames(other, frame);
			return true;
		};
		scopeScreen->viewSource = [=](shoal::ScopeView& view, shoal::ScopeHistory& history) {
			shoal::Host* h = host();
			if (!h)
				return false;
			h->scope(view, history);
			return true;
		};
		addChild(scopeScreen);
		menuScreen = addMenuScreen(SCREEN_EDITOR);
		friendlyList = createWidget<shoal::FriendlyList>(mm2px(SCREEN_EDITOR.pos));
		friendlyList->box.size = mm2px(SCREEN_EDITOR.size);
		friendlyList->host = host();
		friendlyList->live = module != nullptr;
		friendlyList->onHardwareMode = [=]() {
			if (module)
				rka::setParamUndoable(module->paramQuantities[Shoal::UI_MODE_PARAM], 0.f, "move switch");
		};
		addChild(friendlyList);
		OutputStrip* strip = createWidget<OutputStrip>(mm2px(SCREEN_STRIP.pos));
		strip->box.size = mm2px(SCREEN_STRIP.size);
		strip->module = module;
		addChild(strip);

		addEncoder(WIDGET_ENC_L, shoal::ENCODER_L);
		addEncoder(WIDGET_ENC_R, shoal::ENCODER_R);
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_ENC_L_PUSH), module, Shoal::ENC_L_PUSH_PARAM));
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_ENC_R_PUSH), module, Shoal::ENC_R_PUSH_PARAM));
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_UP), module, Shoal::UP_PARAM));
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_DOWN), module, Shoal::DOWN_PARAM));
		// No light id: the cap is lit from the param itself.
		rka::CapLatch* uiMode = createParamCentered<rka::CapLatch>(mm2px(PARAM_UI_MODE), module, Shoal::UI_MODE_PARAM);
		uiMode->litWithoutModule = true;  // the default, friendly mode, as step() shows it
		addParam(uiMode);

		const Vec trIn[4] = {INPUT_TR1, INPUT_TR2, INPUT_TR3, INPUT_TR4};
		const Vec trLight[4] = {LIGHT_TR1, LIGHT_TR2, LIGHT_TR3, LIGHT_TR4};
		const Vec cvIn[4] = {INPUT_CV1, INPUT_CV2, INPUT_CV3, INPUT_CV4};
		const Vec out[4] = {OUTPUT_A, OUTPUT_B, OUTPUT_C, OUTPUT_D};
		const Vec outLight[4] = {LIGHT_A, LIGHT_B, LIGHT_C, LIGHT_D};
		for (int i = 0; i < 4; i++) {
			addInput(createInputCentered<rka::BrassPort>(mm2px(trIn[i]), module, Shoal::TR1_INPUT + i));
			addChild(createLightCentered<rka::JewelLight>(mm2px(trLight[i]), module, Shoal::TR1_LIGHT + i));
			addInput(createInputCentered<rka::BrassPort>(mm2px(cvIn[i]), module, Shoal::CV1_INPUT + i));
			addOutput(createOutputCentered<rka::BrassPort>(mm2px(out[i]), module, Shoal::A_OUTPUT + i));
			// One light per output, on its tile: the first id of its pair.
			addChild(createLightCentered<rka::JewelLight>(mm2px(outLight[i]), module, Shoal::A_LIGHT + 2 * i));
		}
	}

	// Friendly mode (and the module browser) shows the list on the lower
	// screen; hardware mode the firmware's menu. Set here rather than in
	// process(), so it follows a click (or an undo) while the engine is
	// paused; the UI MODE latch draws its own lit state from the param.
	void step() override {
		Shoal* m = getModule<Shoal>();
		const bool friendly = !m || m->params[Shoal::UI_MODE_PARAM].getValue() > 0.5f;
		friendlyList->setVisible(friendly);
		menuScreen->setVisible(!friendly);
		scopeScreen->friendly = friendly;
		// A hidden widget is not stepped: edits the host refused before the
		// switch to hardware mode are offered here every frame as well.
		friendlyList->flush();
		ModuleWidget::step();
	}

	// The menu (editor) frame of whichever host is live.
	rka::PixelScreen* addMenuScreen(const math::Rect& mm) {
		rka::PixelScreen* s = createWidget<rka::PixelScreen>(mm2px(mm.pos));
		s->box.size = mm2px(mm.size);
		s->colour = RkaTheme::SIGNAL;
		s->source = [=](uint8_t* frame) {
			shoal::Host* h = host();
			if (!h)
				return false;
			uint8_t other[shoal::kFrameBytes];
			h->frames(frame, other);
			return true;
		};
		addChild(s);
		return s;
	}

	void addEncoder(const Vec& mm, shoal::Encoder which) {
		rka::KnurlEncoder* e = createWidgetCentered<rka::KnurlEncoder>(mm2px(mm));
		e->onTurn = [=](int detents) {
			Shoal* m = getModule<Shoal>();
			if (!m)
				return true;   // no module (browser): nothing to turn, nothing to keep
			shoal::ControlEvent ev;
			ev.type = shoal::ControlEvent::Turn;
			ev.index = (int8_t) which;
			ev.value = (int16_t) clamp(detents, -32768, 32767);
			return m->host.post(ev);   // false: the queue was full; the encoder offers the turn again
		};
		addChild(e);
	}
};


Model* modelShoal = createModel<Shoal, ShoalWidget>("Shoal");

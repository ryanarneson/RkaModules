// Beacon: a polyphonic envelope (spec: docs/specs/2026-11-23-beacon-design.md).
// The module maps params, ports, polyphony and lights onto rka::BeaconEngine;
// all behaviour lives there. The window (EnvelopeWindow, below) draws the
// envelope the knobs make, from EnvelopeView.hpp, and the engine's published
// channel-1 state.
#include "plugin.hpp"
#include "BeaconEngine.hpp"
#include "BeaconLayout.hpp"
#include "EnvelopeView.hpp"
#include "widgets.hpp"

#include <atomic>
#include <cmath>

// The window reads channel 1's stage, phase, level, start and goal from the
// audio thread through relaxed atomics; they must not take a lock. C++11 has
// a macro for int only; float's lock-freedom is asserted at compile time in
// test_engine.cpp (C++17, is_always_lock_free), and checked in the shipped
// binary at construction (a WARN in Rack's log if not).
static_assert(ATOMIC_INT_LOCK_FREE == 2, "std::atomic<int> is lock-free");


struct Beacon : Module {
	enum ParamId {
		SHAPE_PARAM,
		MODE_PARAM,
		ATTACK_PARAM,
		DECAY_PARAM,
		SUSTAIN_PARAM,
		RELEASE_PARAM,
		PUSH_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		ATTACK_CV_INPUT,
		DECAY_CV_INPUT,
		SUSTAIN_CV_INPUT,
		RELEASE_CV_INPUT,
		GATE_INPUT,
		RETRIG_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ATTACK_OUTPUT,
		DECAY_OUTPUT,
		SUSTAIN_OUTPUT,
		RELEASE_OUTPUT,
		TRIG_OUTPUT,
		OUT_OUTPUT,
		INV_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ATTACK_LIGHT,
		DECAY_LIGHT,
		SUSTAIN_LIGHT,
		RELEASE_LIGHT,
		LIGHTS_LEN
	};

	rka::BeaconEngine engine;
	rka::BeaconFrame frame;
	rka::BeaconResult result;
	// The stage lights: noted every sample, set every DIVISION samples.
	dsp::ClockDivider lightDivider;
	rka::BeaconLights lightHold;

	// Attack, Decay and Release show ms or s; a typed value is milliseconds,
	// or seconds if it ends in "s" ("1.5 s"); out of range it is held to
	// 1 ms .. 60 s, garbage is ignored.
	struct TimeQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			char buf[32];
			rka::beacon::formatSeconds(rka::beacon::stageSeconds(getValue()), buf, sizeof buf);
			return buf;
		}
		void setDisplayValueString(std::string s) override {
			float seconds;
			if (rka::beacon::parseSeconds(s.c_str(), seconds))
				setValue(rka::beacon::stageKnob(seconds));
		}
		std::string getUnit() override {
			return "";
		}
	};

	// Shape: -100 % (reverse) through "Linear" at noon to +100 % (classic).
	struct ShapeQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return getValue() == 0.5f ? "Linear" : ParamQuantity::getDisplayValueString();
		}
		void setDisplayValueString(std::string s) override {
			if (string::lowercase(s) == "linear")
				setValue(0.5f);
			else
				ParamQuantity::setDisplayValueString(s);
		}
		std::string getUnit() override {
			return getValue() == 0.5f ? "" : ParamQuantity::getUnit();
		}
	};

	Beacon() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const rka::BeaconFrame defaults;
		typedef rka::BeaconFrame F;
		configParam<ShapeQuantity>(SHAPE_PARAM, 0.f, 1.f, defaults.params[F::SHAPE],
		                           "Shape (left: reverse, noon: linear, right: classic)", " %", 0.f, 200.f, -100.f);
		configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", {"Gate", "Trig", "Hold"});
		configParam<TimeQuantity>(ATTACK_PARAM, 0.f, 1.f, defaults.params[F::ATTACK], "Attack (Hold: time to rise 10 V)");
		configParam<TimeQuantity>(DECAY_PARAM, 0.f, 1.f, defaults.params[F::DECAY], "Decay (Hold: time to fall 10 V)");
		configParam(SUSTAIN_PARAM, 0.f, 1.f, defaults.params[F::SUSTAIN], "Sustain (Hold: offset)", " V", 0.f, 10.f);
		configParam<TimeQuantity>(RELEASE_PARAM, 0.f, 1.f, defaults.params[F::RELEASE], "Release");
		configButton(PUSH_PARAM, "Push (a gate on every channel while held)");
		configInput(ATTACK_CV_INPUT, "Attack CV (10 V: the knob's travel)");
		configInput(DECAY_CV_INPUT, "Decay CV (10 V: the knob's travel)");
		configInput(SUSTAIN_CV_INPUT, "Sustain CV (Hold: the signal to slew; its channels set the count)");
		configInput(RELEASE_CV_INPUT, "Release CV (10 V: the knob's travel)");
		configInput(GATE_INPUT, "Gate (polyphonic: an envelope per channel)");
		configInput(RETRIG_INPUT, "Retrig (restarts Attack while the gate is high)");
		configOutput(ATTACK_OUTPUT, "Attack stage gate");
		configOutput(DECAY_OUTPUT, "Decay stage gate");
		configOutput(SUSTAIN_OUTPUT, "Sustain stage gate");
		configOutput(RELEASE_OUTPUT, "Release stage gate");
		configOutput(TRIG_OUTPUT, "Stage trigger (1 ms at each stage start)");
		configOutput(OUT_OUTPUT, "Envelope");
		configOutput(INV_OUTPUT, "Inverted envelope");
		configLight(ATTACK_LIGHT, "Attack");
		configLight(DECAY_LIGHT, "Decay");
		configLight(SUSTAIN_LIGHT, "Sustain");
		configLight(RELEASE_LIGHT, "Release");
		engine.setSampleRate(APP ? APP->engine->getSampleRate() : 48000.f);
		lightDivider.setDivision(rka::BeaconLights::DIVISION);
		if (!engine.published().phase.is_lock_free())
			WARN("Beacon: std::atomic<float> is not lock-free here; the window's reads may take a lock");
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset() override {
		engine.reset();
		lightHold.reset();
	}

	// The engine reads the cable's own voltages (16 floats, always there);
	// 0 channels when unpatched.
	static void read(Input& in, rka::BeaconPort& port) {
		port.channels = in.getChannels();
		port.v = in.getVoltages();
	}

	void process(const ProcessArgs& args) override {
		typedef rka::BeaconFrame F;
		static_assert(int(SHAPE_PARAM) == int(F::SHAPE) && int(MODE_PARAM) == int(F::MODE)
			&& int(ATTACK_PARAM) == int(F::ATTACK) && int(DECAY_PARAM) == int(F::DECAY)
			&& int(SUSTAIN_PARAM) == int(F::SUSTAIN) && int(RELEASE_PARAM) == int(F::RELEASE)
			&& int(PUSH_PARAM) == int(F::PUSH) && int(PARAMS_LEN) == int(F::PARAMS_LEN), "ParamId ids");

		for (int i = 0; i < F::PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		read(inputs[ATTACK_CV_INPUT], frame.attackCv);
		read(inputs[DECAY_CV_INPUT], frame.decayCv);
		read(inputs[SUSTAIN_CV_INPUT], frame.sustainCv);
		read(inputs[RELEASE_CV_INPUT], frame.releaseCv);
		read(inputs[GATE_INPUT], frame.gate);
		read(inputs[RETRIG_INPUT], frame.retrig);

		engine.process(frame, result);

		const int n = result.channels;
		for (int k = 0; k < 4; k++) {
			outputs[ATTACK_OUTPUT + k].setChannels(n);
			outputs[ATTACK_OUTPUT + k].writeVoltages(result.stageGate[k]);
			// The lights follow channel 1's stage gates (BeaconLights).
			lightHold.note(k, result.stageGate[k][0] > 0.f);
		}
		if (lightDivider.process()) {
			float b[4];
			lightHold.update(args.sampleTime * float(rka::BeaconLights::DIVISION), b);
			for (int k = 0; k < 4; k++)
				lights[ATTACK_LIGHT + k].setBrightness(b[k]);
		}
		outputs[TRIG_OUTPUT].setChannels(n);
		outputs[TRIG_OUTPUT].writeVoltages(result.trig);
		outputs[OUT_OUTPUT].setChannels(n);
		outputs[OUT_OUTPUT].writeVoltages(result.out);
		outputs[INV_OUTPUT].setChannels(n);
		outputs[INV_OUTPUT].writeVoltages(result.inv);
	}

	// Bypass: Rack's default (outputs silent); nothing to pass through.

	// What the window draws, channel 1's: the knobs with channel 1's CVs.
	// Read on the UI thread (the ports' floats as Rack's own scopes read
	// them; a torn read costs one frame's drawing).
	rka::beacon::ViewParams viewParams() {
		auto cv = [&](int id) {
			const float v = inputs[id].isConnected() ? inputs[id].getVoltage(0) : 0.f;
			return std::isfinite(v) ? v : 0.f;
		};
		rka::beacon::ViewParams p;
		p.attack = rka::beacon::stageSecondsCv(params[ATTACK_PARAM].getValue(), cv(ATTACK_CV_INPUT));
		p.decay = rka::beacon::stageSecondsCv(params[DECAY_PARAM].getValue(), cv(DECAY_CV_INPUT));
		p.release = rka::beacon::stageSecondsCv(params[RELEASE_PARAM].getValue(), cv(RELEASE_CV_INPUT));
		p.sustain01 = math::clamp(params[SUSTAIN_PARAM].getValue() + cv(SUSTAIN_CV_INPUT) * 0.1f, 0.f, 1.f);
		p.shape = params[SHAPE_PARAM].getValue();
		return p;
	}
};


// What the window shows: the envelope's params and channel 1's state.
struct EnvelopeWindowView {
	rka::beacon::ViewParams params;
	int stage = rka::beacon::IDLE;    // rka::beacon::Stage
	float phase = 0.f, level = 0.f;   // 0..1, volts
	float start = 0.f, goal = 0.f;    // the running stage's, volts
	bool hold = false;
};

// Beacon's window (its spec, "The window"), drawn in vectors in the
// self-illuminating layer on the glass, in the theme's cyan as TapeWindow.
// Gate and Trig: the envelope the knobs make (layoutEnvelope), each stage
// its own stroke, dim; the running Attack, Decay or Release drawn bright
// over its span from its true start to its goal (liveStageHeight), with the
// dot on it; Sustain running: its span bright (dim at a 0 V sustain, the dot
// then at the end of the line); A D S R under the spans, the running letter
// bright. Hold: a
// scrolling line of channel 1's output (+-10 V), one point per UI frame,
// its ring kept here (UI thread only). It knows nothing of the engine:
// `source` fills an EnvelopeWindowView, or returns false to draw nothing.
// No mouse.
struct EnvelopeWindow : widget::Widget {
	std::function<bool(EnvelopeWindowView&)> source;
	// Points along the curve per stage.
	static constexpr int CURVE_POINTS = 48;
	// The Hold line: one level per UI frame, newest at `ringPos - 1`.
	static constexpr int RING = 160;
	float ring[RING] = {};
	int ringPos = 0;
	bool ringLive = false;

	float pad() const {
		return mm2px(0.8f);
	}
	// The band under the curve that holds the letters.
	float lettersH() const {
		return mm2px(2.7f);
	}
	float plotLeft() const {
		return pad() + mm2px(0.6f);
	}
	float plotRight() const {
		return box.size.x - pad() - mm2px(0.6f);
	}
	float plotTop() const {
		return pad() + mm2px(0.8f);
	}
	float plotBottom() const {
		return box.size.y - pad() - lettersH();
	}
	float xAt(float u) const {
		return plotLeft() + u * (plotRight() - plotLeft());
	}
	float yAt(float h) const {
		return plotBottom() - h * (plotBottom() - plotTop());
	}

	void step() override {
		// The Hold line is sampled once per frame, here, not in draw (which
		// may run more than once a frame).
		EnvelopeWindowView v;
		if (source && source(v) && v.hold) {
			const float level = std::isfinite(v.level) ? math::clamp(v.level, -10.f, 10.f) : 0.f;
			if (!ringLive) {
				for (int i = 0; i < RING; i++)
					ring[i] = level;
				ringLive = true;
			}
			ring[ringPos] = level;
			ringPos = (ringPos + 1) % RING;
		}
		else
			ringLive = false;
		Widget::step();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && source) {
			EnvelopeWindowView v;
			if (source(v)) {
				nvgSave(args.vg);
				nvgScissor(args.vg, 0.f, 0.f, box.size.x, box.size.y);
				if (v.hold)
					drawHold(args.vg);
				else
					drawEnvelope(args.vg, v);
				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

	void line(NVGcontext* vg, float x0, float y0, float x1, float y1, NVGcolor c, float width) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x0, y0);
		nvgLineTo(vg, x1, y1);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, width);
		nvgStroke(vg);
	}

	std::shared_ptr<window::Font> font() {
		return APP->window->loadFont(asset::plugin(pluginInstance, "tools/fonts/D-DINCondensed.ttf"));
	}

	void drawEnvelope(NVGcontext* vg, const EnvelopeWindowView& v) {
		using namespace rka::beacon;
		const NVGcolor signal = RkaTheme::SIGNAL;
		const NVGcolor dim = nvgTransRGBAf(signal, 0.38f);
		const EnvelopeLayout e = layoutEnvelope(v.params);
		// A stage number from another sample than the phase (separate
		// relaxed atomics) only misplaces the dot for one frame; anything
		// out of range reads as idle.
		const int stage = (v.stage >= ATTACK && v.stage <= RELEASE) ? v.stage : IDLE;
		const int running = stage == IDLE ? -1 : stage - ATTACK;   // span 0..3

		// The baseline, faint, under the whole envelope.
		line(vg, xAt(0.f), yAt(0.f), xAt(1.f), yAt(0.f), nvgTransRGBAf(signal, 0.18f), 0.6f);

		// Each stage its own stroke, the knobs' shape: dim, except a running
		// Sustain above 0 V.
		const bool sustainLit = stage == SUSTAIN && e.sustain01 > 0.f;
		for (int s = 0; s < 4; s++) {
			const float u0 = e.x[s], u1 = e.x[s + 1];
			nvgBeginPath(vg);
			for (int i = 0; i <= CURVE_POINTS; i++) {
				// The last point just inside the span (heightAt takes the
				// next stage at its left edge).
				const float f = i < CURVE_POINTS ? float(i) / CURVE_POINTS : 1.f - 1e-6f;
				const float u = u0 + f * (u1 - u0);
				const float x = xAt(u), y = yAt(e.heightAt(u));
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			const bool lit = s == 2 && sustainLit;
			nvgStrokeColor(vg, lit ? signal : dim);
			nvgStrokeWidth(vg, lit ? 1.6f : 1.1f);
			nvgLineJoin(vg, NVG_ROUND);
			nvgStroke(vg);
		}

		// The running Attack, Decay or Release, bright over its span, from
		// where it really started to its goal along the curve the voice
		// follows (drawn with the Shape knob; the voice's own Shape trails
		// it by a 5 ms smoothing). Start and goal may be a sample apart
		// from the stage (separate relaxed atomics): clamped, one frame.
		const float start01 = std::isfinite(v.start) ? v.start * 0.1f : 0.f;
		const float goal01 = std::isfinite(v.goal) ? v.goal * 0.1f : 0.f;
		if (stage == ATTACK || stage == DECAY || stage == RELEASE) {
			const int s = running;
			const float u0 = e.x[s], u1 = e.x[s + 1];
			nvgBeginPath(vg);
			for (int i = 0; i <= CURVE_POINTS; i++) {
				const float f = float(i) / CURVE_POINTS;
				const float x = xAt(u0 + f * (u1 - u0)), y = yAt(liveStageHeight(f, start01, goal01, e.shape));
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, signal);
			nvgStrokeWidth(vg, 1.6f);
			nvgLineJoin(vg, NVG_ROUND);
			nvgStroke(vg);
		}

		// The dot: channel 1's stage and how far through it, on the bright
		// line (Sustain: at its level, or at the end at 0 V). Idle: at rest
		// at the start, dim.
		float u, h;
		e.dot(Stage(stage), math::clamp(v.phase, 0.f, 1.f), std::isfinite(v.level) ? v.level * 0.1f : 0.f, start01, goal01, u, h);
		nvgBeginPath(vg);
		nvgCircle(vg, xAt(u), yAt(h), mm2px(running >= 0 ? 0.75f : 0.55f));
		nvgFillColor(vg, running >= 0 ? signal : dim);
		nvgFill(vg);

		// A D S R under the spans.
		std::shared_ptr<window::Font> f = font();
		if (f && f->handle >= 0) {
			static const char* letters[4] = {"A", "D", "S", "R"};
			nvgFontFaceId(vg, f->handle);
			nvgFontSize(vg, mm2px(2.1f));
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			const float y = box.size.y - pad() - 0.5f * lettersH();
			for (int s = 0; s < 4; s++) {
				nvgFillColor(vg, s == running ? signal : dim);
				nvgText(vg, xAt(0.5f * (e.x[s] + e.x[s + 1])), y, letters[s], NULL);
			}
		}
	}

	// Hold: the output over the last RING frames, +-10 V filling the plot
	// and the letters' band; 0 V a faint line; the newest point a dot.
	void drawHold(NVGcontext* vg) {
		const NVGcolor signal = RkaTheme::SIGNAL;
		const float top = plotTop(), bottom = box.size.y - pad() - mm2px(0.8f);
		const float mid = 0.5f * (top + bottom), half = 0.5f * (bottom - top);
		auto yOf = [&](float volts) { return mid - volts * 0.1f * half; };
		line(vg, plotLeft(), mid, plotRight(), mid, nvgTransRGBAf(signal, 0.18f), 0.6f);
		nvgBeginPath(vg);
		for (int i = 0; i < RING; i++) {
			const float x = plotLeft() + float(i) / (RING - 1) * (plotRight() - plotLeft());
			const float y = yOf(ring[(ringPos + i) % RING]);
			if (i == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		nvgStrokeColor(vg, signal);
		nvgStrokeWidth(vg, 1.4f);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, plotRight(), yOf(ring[(ringPos + RING - 1) % RING]), mm2px(0.75f));
		nvgFillColor(vg, signal);
		nvgFill(vg);
		std::shared_ptr<window::Font> f = font();
		if (f && f->handle >= 0) {
			nvgFontFaceId(vg, f->handle);
			nvgFontSize(vg, mm2px(2.4f));
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, nvgTransRGBAf(signal, 0.6f));
			nvgText(vg, plotLeft(), pad(), "HOLD", NULL);
		}
	}
};


struct BeaconWidget : ModuleWidget {
	BeaconWidget(Beacon* module) {
		using namespace BeaconLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/beacon/Beacon.svg"),
		                     asset::plugin(pluginInstance, "res/beacon/Beacon-dark.svg")));

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		EnvelopeWindow* window = createWidget<EnvelopeWindow>(mm2px(SCREEN_ENVELOPE.pos));
		window->box.size = mm2px(SCREEN_ENVELOPE.size);
		window->source = [this](EnvelopeWindowView& v) {
			// Asked each frame: once the module is gone, nothing is read.
			Beacon* m = getModule<Beacon>();
			if (!m) {
				// The module browser: the default envelope, idle.
				const rka::BeaconFrame d;
				typedef rka::BeaconFrame F;
				v.params.attack = rka::beacon::stageSeconds(d.params[F::ATTACK]);
				v.params.decay = rka::beacon::stageSeconds(d.params[F::DECAY]);
				v.params.release = rka::beacon::stageSeconds(d.params[F::RELEASE]);
				v.params.sustain01 = d.params[F::SUSTAIN];
				v.params.shape = d.params[F::SHAPE];
				return true;
			}
			v.params = m->viewParams();
			const rka::BeaconPublished& p = m->engine.published();
			v.stage = p.stage.load(std::memory_order_relaxed);
			v.phase = p.phase.load(std::memory_order_relaxed);
			v.level = p.level.load(std::memory_order_relaxed);
			v.start = p.start.load(std::memory_order_relaxed);
			v.goal = p.goal.load(std::memory_order_relaxed);
			v.hold = p.mode.load(std::memory_order_relaxed) == int(rka::beacon::HOLD_MODE);
			return true;
		};
		addChild(window);

		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SHAPE), module, Beacon::SHAPE_PARAM));
		addParam(createParamCentered<rka::Toggle3>(mm2px(PARAM_MODE), module, Beacon::MODE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_ATTACK), module, Beacon::ATTACK_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DECAY), module, Beacon::DECAY_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SUSTAIN), module, Beacon::SUSTAIN_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_RELEASE), module, Beacon::RELEASE_PARAM));
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_PUSH), module, Beacon::PUSH_PARAM));

		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_ATTACK), module, Beacon::ATTACK_LIGHT));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_DECAY), module, Beacon::DECAY_LIGHT));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_SUSTAIN), module, Beacon::SUSTAIN_LIGHT));
		addChild(createLightCentered<rka::JewelLight>(mm2px(LIGHT_RELEASE), module, Beacon::RELEASE_LIGHT));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_ATTACK_CV), module, Beacon::ATTACK_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DECAY_CV), module, Beacon::DECAY_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SUSTAIN_CV), module, Beacon::SUSTAIN_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RELEASE_CV), module, Beacon::RELEASE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_GATE), module, Beacon::GATE_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RETRIG), module, Beacon::RETRIG_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_ATTACK), module, Beacon::ATTACK_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_DECAY), module, Beacon::DECAY_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_SUSTAIN), module, Beacon::SUSTAIN_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_RELEASE), module, Beacon::RELEASE_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_TRIG), module, Beacon::TRIG_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_OUT), module, Beacon::OUT_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_INV), module, Beacon::INV_OUTPUT));
	}
};


Model* modelBeacon = createModel<Beacon, BeaconWidget>("Beacon");

// Spindrift: Mutable Instruments Beads in the Instrument look (spec:
// docs/specs/2026-11-22-spindrift-design.md). The module maps the panel,
// the jacks, the menu and the patch's data onto spindrift::SpindriftEngine
// (Rack-free; Beads' own code runs inside it); no DSP here.
//
// Threads, and what each touches:
// - The audio thread: process(): the knobs and jacks into the engine, the
//   buttons' presses and their hold lengths (Buttons, the engine's), the
//   lights; it publishes the quality and Seed's latch for the save
//   (savedQuality_, savedSeedLatched_).
// - The UI thread: the menu (the engine's settings: relaxed atomics), the
//   window (the engine's view and overview: relaxed atomics), onSave (the
//   engine's bufferImage(): a seqlock, SpindriftEngine.hpp) and dataToJson.
// - Under Rack's exclusive engine lock (never during process()): the
//   constructor (the engine's memory), onAdd (the stored buffer loaded into
//   the engine before its first process()), onSampleRateChange, onReset,
//   and dataFromJson (Engine::moduleFromJson, or a patch's load before
//   onAdd), which set the engine's buttons directly.
#include "plugin.hpp"
#include "BufferFile.hpp"
#include "SavedState.hpp"
#include "SpindriftEngine.hpp"
#include "SpindriftLayout.hpp"
#include "widgets.hpp"

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

struct Spindrift : Module {
	// Never renumber (saved patches refer to them).
	enum ParamId {
		TIME_PARAM, SIZE_PARAM, DENSITY_PARAM, SHAPE_PARAM, PITCH_PARAM,
		TIME_ATT_PARAM, SIZE_ATT_PARAM, SHAPE_ATT_PARAM, PITCH_ATT_PARAM,
		FEEDBACK_PARAM, DRYWET_PARAM, REVERB_PARAM,
		FREEZE_PARAM, SEED_PARAM, QUALITY_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		TIME_CV_INPUT, SIZE_CV_INPUT, DENSITY_CV_INPUT, SHAPE_CV_INPUT, PITCH_CV_INPUT,
		FEEDBACK_CV_INPUT, DRYWET_CV_INPUT, REVERB_CV_INPUT,
		L_INPUT, R_INPUT, FREEZE_INPUT, SEED_INPUT,
		INPUTS_LEN
	};
	enum OutputId { L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { FREEZE_LIGHT, SEED_LIGHT, QUALITY_LIGHT, LIGHTS_LEN };

	static constexpr const char* BUFFER_FILE = "buffer.bin";
	// Lights and the published state, every this many samples.
	static constexpr int LIGHT_SAMPLES = 32;

	spindrift::SpindriftEngine engine;
	spindrift::Knobs knobs = {};
	spindrift::Jacks jacks = {};

	// The audio thread's: the buttons as last seen, and the engine's tick
	// (4 kHz) at each press, for the hold lengths Buttons decides on.
	bool freezeWas_ = false, seedWas_ = false, qualityWas_ = false;
	uint32_t seedDownAt_ = 0, qualityDownAt_ = 0;
	int lightCountdown_ = 0;
	// For dataToJson (UI thread), published by the audio thread, and set by
	// dataFromJson.
	std::atomic<int> savedQuality_{spindrift::DIGITAL};
	std::atomic<bool> savedSeedLatched_{true};

	// The UI thread's: added to the engine; a stored buffer to load in onAdd;
	// buffer.bin holds the image the engine's ring had at storedSeq_.
	bool added_ = false, restoreWanted_ = false, stored_ = false;
	uint32_t storedSeq_ = 1;

	// Pitch: Beads' detune law, in semitones (snapping to 0, 1, 3, 4, 7, 12
	// and 24 either way).
	struct PitchQuantity : ParamQuantity {
		float getDisplayValue() override {
			return spindrift::detuneLaw(math::clamp(getValue(), 0.f, 1.f));
		}
		// The knob for typed semitones: the middle of the law's flat for that
		// many (the law only rises, so a bisection finds it).
		void setDisplayValue(float st) override {
			if (!std::isfinite(st))
				return;
			st = math::clamp(st, -24.f, 24.f);
			float lo = 0.f, hi = 1.f;
			for (int i = 0; i < 40; i++) {
				const float mid = 0.5f * (lo + hi);
				(spindrift::detuneLaw(mid) < st ? lo : hi) = mid;
			}
			float a = hi;
			lo = 0.f;
			hi = 1.f;
			for (int i = 0; i < 40; i++) {
				const float mid = 0.5f * (lo + hi);
				(spindrift::detuneLaw(mid) <= st ? lo : hi) = mid;
			}
			setValue(0.5f * (a + lo));
		}
	};

	// Defaults (ruling R11): a new Spindrift makes a cloud. Density 0.65 is
	// on the random side, clear of the dead zone (0.48 to 0.52), and Dry/Wet
	// 0.5 plays the grains with the input; the rest at noon or 0.
	Spindrift() : engine(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time", "%", 0.f, 100.f)->description =
		    "Where in the buffer the grains start: from just recorded to the oldest. Delay mode: the delay time "
		    "(with a clock into SEED, a ratio of it)";
		configParam(SIZE_PARAM, 0.f, 1.f, 0.5f, "Size", "%", 0.f, 100.f)->description =
		    "The grains' length; below a third they play backwards. Fully clockwise: delay mode";
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.65f, "Density", "%", 0.f, 100.f)->description =
		    "Left of noon: regular grains, faster as it turns. Right: random grains. At noon: none of its own "
		    "(each Seed press or trigger makes one). With a clock into SEED: a divider (left) or a probability (right)";
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.5f, "Shape", "%", 0.f, 100.f)->description =
		    "The grains' envelope: from a short click through a ramp to a smooth bell";
		configParam<PitchQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch", " semitones")->description =
		    "The grains' transposition, ±24 semitones, catching at 0, 1, 3, 4, 7, 12 and 24";
		paramQuantities[PITCH_PARAM]->displayPrecision = 3;
		const char* attDescription =
		    "No cable: how much the grains wander from the knob (clockwise: each grain a random offset; "
		    "anticlockwise: a random amount of randomness each). A cable: how much of the CV (anticlockwise: the "
		    "CV sets the randomness)";
		const char* attNames[4] = {"Time attenuverter", "Size attenuverter", "Shape attenuverter", "Pitch attenuverter"};
		const int atts[4] = {TIME_ATT_PARAM, SIZE_ATT_PARAM, SHAPE_ATT_PARAM, PITCH_ATT_PARAM};
		for (int i = 0; i < 4; i++)
			configParam(atts[i], 0.f, 1.f, 0.5f, attNames[i], "%", 0.f, 200.f, -100.f)->description = attDescription;
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.f, "Feedback", "%", 0.f, 100.f)->description =
		    "How much of the output goes back into the buffer. In the synth: the wavetable bank";
		configParam(DRYWET_PARAM, 0.f, 1.f, 0.5f, "Dry/Wet", "%", 0.f, 100.f)->description =
		    "The input against the grains. In the synth: the voice against its grain cloud";
		configParam(REVERB_PARAM, 0.f, 1.f, 0.f, "Reverb", "%", 0.f, 100.f)->description =
		    "Beads' reverb: its amount and its time together";
		configButton(FREEZE_PARAM, "Freeze")->description =
		    "Stops recording: the grains play what the buffer holds. Pressed while holding Seed: latches Seed";
		configButton(SEED_PARAM, "Seed")->description =
		    "Latched (lit, breathing) at the start, so grains come by Density. A press unlatches it: then grains "
		    "come while it is held. Hold 4 s, or press Freeze while holding it, to latch it again";
		configButton(QUALITY_PARAM, "Quality")->description =
		    "Digital, Vintage Digital, Clean Tape, Dirty Tape, and round: each clears the buffer and mutes for "
		    "250 ms (lit). Frozen, the choice waits until Freeze is released. Hold 1.5 s: the input gain goes "
		    "back to automatic";
		configInput(TIME_CV_INPUT, "Time CV (±5 V: the knob's travel, through its attenuverter)");
		configInput(SIZE_CV_INPUT, "Size CV (±5 V: the knob's travel, through its attenuverter)");
		configInput(DENSITY_CV_INPUT, "Density CV (±5 V: the knob's travel)");
		configInput(SHAPE_CV_INPUT, "Shape CV (±5 V: the knob's travel, through its attenuverter)");
		configInput(PITCH_CV_INPUT, "Pitch V/OCT (at the attenuverter's full clockwise)");
		configInput(FEEDBACK_CV_INPUT, "Feedback CV (±5 V: the knob's travel)");
		configInput(DRYWET_CV_INPUT, "Dry/Wet CV (±5 V: the knob's travel)");
		configInput(REVERB_CV_INPUT, "Reverb CV (±5 V: the knob's travel)");
		configInput(L_INPUT, "Left (±10 V is full scale; with one input patched, Beads runs mono)");
		configInput(R_INPUT, "Right (±10 V is full scale; with one input patched, Beads runs mono)");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(SEED_INPUT, "Seed trigger (Seed latched: a clock for Density and delay mode)");
		configOutput(L_OUTPUT, "Left (gate output mode: the mono mix)");
		configOutput(R_OUTPUT, "Right (gate output mode: a 0/10 V gate per grain)");
		configLight(FREEZE_LIGHT, "Frozen");
		configLight(SEED_LIGHT, "Seed");
		configLight(QUALITY_LIGHT, "Changing quality (muted)");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);

		// Beads' memory (about 850 kB), here, off the audio thread.
		if (!engine.allocate())
			WARN("Spindrift: out of memory for Beads' buffers; the module is silent");
		engine.setSampleRate(APP && APP->engine ? APP->engine->getSampleRate() : 48000.f);
	}

	// ---- Rack's events.

	void onAdd(const AddEvent& e) override {
		added_ = true;
		if (restoreWanted_) {
			restoreWanted_ = false;
			restoreBuffer();
		}
	}

	void onRemove(const RemoveEvent& e) override {
		added_ = false;
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		engine.setSampleRate(e.sampleRate);
	}

	// Initialize: the knobs (Module::onReset), the buttons (Seed latched,
	// Digital, not frozen) and the menu to their defaults.
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.buttons().reset();
		engine.settings = spindrift::Settings();
		savedQuality_.store(engine.buttons().quality);
		savedSeedLatched_.store(engine.buttons().seedLatched);
	}

	// The stored buffer, from the patch's storage, before the first sample
	// (beads.cc:344-347). Anything that does not fit (BufferFile.hpp, or a
	// quality other than the patch's) loads nothing: an empty, unfrozen
	// buffer, and a warning in the log.
	void restoreBuffer() {
		const std::string dir = getPatchStorageDirectory();
		const std::string path = dir.empty() ? "" : system::join(dir, BUFFER_FILE);
		spindrift::BufferHeader h;
		std::vector<uint8_t> payload;
		std::string why;
		if (path.empty() || !spindrift::readBufferFile(path, h, payload, why)) {
			engine.buttons().frozenButton = false;
			WARN("Spindrift: the frozen buffer %s was not loaded (%s); the buffer starts empty", path.c_str(),
			     why.c_str());
			return;
		}
		if (!engine.loadBufferImage(h, payload.data(), payload.size())) {
			WARN("Spindrift: the frozen buffer %s was not loaded (recorded in %s, the patch says %s); the buffer "
			     "starts empty",
			     path.c_str(), spindrift::qualityName(spindrift::Quality(h.quality & 3)),
			     spindrift::qualityName(engine.buttons().quality));
			return;
		}
		stored_ = true;
		storedSeq_ = engine.ringSeq();
		INFO("Spindrift: restored the frozen buffer (%s, %s)", spindrift::qualityName(spindrift::Quality(h.quality)),
		     h.mono ? "mono" : "stereo");
	}

	// The frozen buffer is written here, synchronously (Rack archives the
	// folder as soon as this returns), when it is frozen, by the button or
	// the gate, and "Keep the frozen buffer" is on; otherwise buffer.bin is
	// removed. A buffer already stored and frozen since is not written again.
	void onSave(const SaveEvent& e) override {
		const std::string dir = getPatchStorageDirectory();
		if (dir.empty())
			return;
		const std::string path = system::join(dir, BUFFER_FILE);
		if (engine.settings.keepFrozen.load(std::memory_order_relaxed)) {
			const uint32_t seq = engine.ringSeq();
			if (!(seq & 1u) && stored_ && seq == storedSeq_ && system::exists(path))
				return;
			spindrift::BufferHeader h;
			std::vector<uint8_t> payload(spindrift::kBufferPayloadBytes);
			uint32_t imageSeq = 1;
			// Frozen when asked but written during the copy (the freeze
			// ended meanwhile, or ended and began again): the copy is
			// discarded (SpindriftEngine::bufferImage). Try again while it is
			// frozen anew; say so if it never holds still.
			bool wasFrozen = false, got = false;
			for (int attempt = 0; attempt < 3 && !got; attempt++) {
				wasFrozen |= !(engine.ringSeq() & 1u);
				got = engine.bufferImage(h, payload.data(), &imageSeq);
			}
			if (got) {
				createPatchStorageDirectory();
				std::string why;
				if (spindrift::writeBufferFile(path, h, payload.data(), why)) {
					stored_ = true;
					storedSeq_ = imageSeq;
					return;
				}
				WARN("Spindrift: cannot store the frozen buffer in %s: %s", path.c_str(), why.c_str());
			}
			else if (wasFrozen)
				WARN("Spindrift: the frozen buffer changed while saving (the freeze ended); not stored");
		}
		stored_ = false;
		// Rack's filesystem calls throw on failure (a file held open
		// elsewhere, on Windows): that must not fail the patch's save.
		try {
			if (system::exists(path))
				system::remove(path);
		}
		catch (std::exception& ex) {
			WARN("Spindrift: cannot remove %s: %s", path.c_str(), ex.what());
		}
	}

	spindrift::SavedState savedState() {
		spindrift::SavedState s;
		s.quality = spindrift::Quality(savedQuality_.load() & 3);
		s.seedLatched = savedSeedLatched_.load();
		// Frozen as stored: buffer.bin holds the ring as it is now.
		s.frozen = stored_ && engine.ringSeq() == storedSeq_;
		s.synth = engine.settings.synth.load();
		s.gain = engine.settings.gain.load();
		s.manualGain = engine.settings.manualGain.load();
		s.gateOutput = engine.settings.gateOutput.load();
		s.keepFrozen = engine.settings.keepFrozen.load();
		return s;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		for (const auto& kv : spindrift::savedToMap(savedState())) {
			json_t* v;
			switch (kv.second.kind) {
				case spindrift::SavedValue::BOOL: v = json_boolean(kv.second.value != 0.0); break;
				case spindrift::SavedValue::INT: v = json_integer(json_int_t(kv.second.value)); break;
				case spindrift::SavedValue::REAL:
				default: v = json_real(kv.second.value); break;
			}
			json_object_set_new(root, kv.first.c_str(), v);
		}
		return root;
	}

	// SavedState.hpp decides what loads. Onto a running module (a preset, an
	// undo) the buffer stays as it is: only a patch's load (before onAdd)
	// restores one.
	void dataFromJson(json_t* root) override {
		spindrift::SavedMap m;
		const char* key;
		json_t* v;
		json_object_foreach(root, key, v) {
			if (json_is_boolean(v))
				m[key] = {spindrift::SavedValue::BOOL, json_is_true(v) ? 1.0 : 0.0};
			else if (json_is_integer(v))
				m[key] = {spindrift::SavedValue::INT, double(json_integer_value(v))};
			else if (json_is_real(v))
				m[key] = {spindrift::SavedValue::REAL, json_real_value(v)};
		}
		const spindrift::SavedState s = spindrift::savedFromMap(m);
		engine.settings.synth.store(s.synth);
		engine.settings.gain.store(s.gain);
		engine.settings.manualGain.store(s.manualGain);
		engine.settings.gateOutput.store(s.gateOutput);
		engine.settings.keepFrozen.store(s.keepFrozen);
		spindrift::Buttons& b = engine.buttons();
		b.quality = b.pending = s.quality;
		b.seedLatched = s.seedLatched;
		savedQuality_.store(s.quality);
		savedSeedLatched_.store(s.seedLatched);
		if (!added_) {
			b.frozenButton = false;  // latched by onAdd if the buffer loads
			restoreWanted_ = s.frozen && s.keepFrozen;
		}
	}

	// ---- The audio thread.

	void process(const ProcessArgs& args) override {
		knobs.time = params[TIME_PARAM].getValue();
		knobs.size = params[SIZE_PARAM].getValue();
		knobs.shape = params[SHAPE_PARAM].getValue();
		knobs.pitch = params[PITCH_PARAM].getValue();
		knobs.density = params[DENSITY_PARAM].getValue();
		knobs.timeAtt = params[TIME_ATT_PARAM].getValue();
		knobs.sizeAtt = params[SIZE_ATT_PARAM].getValue();
		knobs.shapeAtt = params[SHAPE_ATT_PARAM].getValue();
		knobs.pitchAtt = params[PITCH_ATT_PARAM].getValue();
		knobs.feedback = params[FEEDBACK_PARAM].getValue();
		knobs.dryWet = params[DRYWET_PARAM].getValue();
		knobs.reverb = params[REVERB_PARAM].getValue();

		cv(TIME_CV_INPUT, jacks.time, jacks.timeOn);
		cv(SIZE_CV_INPUT, jacks.size, jacks.sizeOn);
		cv(SHAPE_CV_INPUT, jacks.shape, jacks.shapeOn);
		cv(PITCH_CV_INPUT, jacks.pitch, jacks.pitchOn);
		cv(DENSITY_CV_INPUT, jacks.density, jacks.densityOn);
		cv(FEEDBACK_CV_INPUT, jacks.feedback, jacks.feedbackOn);
		cv(DRYWET_CV_INPUT, jacks.dryWet, jacks.dryWetOn);
		cv(REVERB_CV_INPUT, jacks.reverb, jacks.reverbOn);
		jacks.inL = inputs[L_INPUT].getVoltageSum();
		jacks.inLOn = inputs[L_INPUT].isConnected();
		jacks.inR = inputs[R_INPUT].getVoltageSum();
		jacks.inROn = inputs[R_INPUT].isConnected();
		cv(FREEZE_INPUT, jacks.freeze, jacks.freezeOn);
		cv(SEED_INPUT, jacks.seed, jacks.seedOn);

		// The buttons (ui.cc), with how long each was held in the engine's
		// 4 kHz ticks.
		spindrift::Buttons& b = engine.buttons();
		const uint32_t now = engine.ticks();
		const bool freeze = params[FREEZE_PARAM].getValue() > 0.f;
		if (freeze && !freezeWas_)
			b.freezePress();
		freezeWas_ = freeze;
		const bool seed = params[SEED_PARAM].getValue() > 0.f;
		if (seed && !seedWas_) {
			b.seedDown();
			seedDownAt_ = now;
		}
		else if (!seed && seedWas_)
			b.seedUp(now - seedDownAt_);
		seedWas_ = seed;
		const bool quality = params[QUALITY_PARAM].getValue() > 0.f;
		if (quality && !qualityWas_) {
			b.qualityDown();
			qualityDownAt_ = now;
		}
		else if (!quality && qualityWas_)
			b.qualityUp(now - qualityDownAt_);
		qualityWas_ = quality;

		spindrift::Out out;
		engine.process(knobs, jacks, out);
		outputs[L_OUTPUT].setVoltage(out.l);
		outputs[R_OUTPUT].setVoltage(out.r);

		// The caps (the spec, "Panel"): Freeze while frozen, by the button or
		// the gate; Quality during the 250 ms mute after a change; Seed as
		// Beads' LED (breathing while latched, else lit while held or as the
		// SEED jack fires). Beads' breathing counts milliseconds: a tick is
		// 0.25 ms.
		if (--lightCountdown_ <= 0) {
			lightCountdown_ = LIGHT_SAMPLES;
			lights[FREEZE_LIGHT].setBrightness(engine.frozen() ? 1.f : 0.f);
			lights[QUALITY_LIGHT].setBrightness(engine.muted() ? 1.f : 0.f);
			lights[SEED_LIGHT].setBrightness(engine.seedLight(engine.ticks() / 4));
			savedQuality_.store(b.quality, std::memory_order_relaxed);
			savedSeedLatched_.store(b.seedLatched, std::memory_order_relaxed);
		}
	}

	void cv(int id, float& volts, bool& patched) {
		patched = inputs[id].isConnected();
		volts = patched ? inputs[id].getVoltage() : 0.f;
	}
};


// The manual input gain as the menu's slider shows it: 0 to 32 dB (Beads'
// 1 to 40 times). Moving it chooses Manual.
struct ManualGainQuantity : Quantity {
	Spindrift* module;
	explicit ManualGainQuantity(Spindrift* m) : module(m) {}
	void setValue(float db) override {
		if (!std::isfinite(db))
			return;
		db = math::clamp(db, getMinValue(), getMaxValue());
		module->engine.settings.manualGain.store(math::clamp(std::pow(10.f, db / 20.f), 1.f, 40.f));
		module->engine.settings.gain.store(spindrift::GAIN_MANUAL);
	}
	float getValue() override {
		return 20.f * std::log10(math::clamp(module->engine.settings.manualGain.load(), 1.f, 40.f));
	}
	float getMinValue() override {
		return 0.f;
	}
	float getMaxValue() override {
		return 20.f * std::log10(40.f);
	}
	float getDefaultValue() override {
		return 0.f;
	}
	int getDisplayPrecision() override {
		return 3;
	}
	std::string getLabel() override {
		return "Manual gain";
	}
	std::string getUnit() override {
		return " dB";
	}
};

struct ManualGainSlider : ui::Slider {
	explicit ManualGainSlider(Spindrift* m) {
		quantity = new ManualGainQuantity(m);
		box.size.x = 200.f;
	}
	~ManualGainSlider() {
		delete quantity;
	}
};


struct SpindriftWidget : ModuleWidget {
	// The waveform the window draws: the engine's overview copied each
	// frame (so the window never holds a pointer into the module), or with
	// no module (the browser, screenshots) a picture of its own.
	rka::Overview shown;
	spindrift::GrainView::Copy preview;

	SpindriftWidget(Spindrift* module) {
		using namespace SpindriftLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/spindrift/Spindrift.svg"),
		                     asset::plugin(pluginInstance, "res/spindrift/Spindrift-dark.svg")));
		if (!module)
			makePreview();

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		rka::GrainWindow* window = createWidget<rka::GrainWindow>(mm2px(SCREEN_GRAINS.pos));
		window->box.size = mm2px(SCREEN_GRAINS.size);
		window->source = [this](rka::GrainWindowView& v) {
			// Asked each frame: once the module is gone (it is deleted before
			// this widget's children), nothing is read from it.
			Spindrift* m = getModule<Spindrift>();
			if (m) {
				m->engine.view().copy(v.copy);
				const rka::Overview& o = m->engine.overview();
				for (int c = 0; c < rka::Overview::COLS; c++) {
					shown.lo[c].store(o.lo[c].load(std::memory_order_relaxed), std::memory_order_relaxed);
					shown.hi[c].store(o.hi[c].load(std::memory_order_relaxed), std::memory_order_relaxed);
				}
			}
			else
				v.copy = preview;
			v.overview = &shown;
			return true;
		};
		addChild(window);

		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_TIME), module, Spindrift::TIME_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SIZE), module, Spindrift::SIZE_PARAM));
		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_DENSITY), module, Spindrift::DENSITY_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SHAPE), module, Spindrift::SHAPE_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_PITCH), module, Spindrift::PITCH_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_TIME_ATT), module, Spindrift::TIME_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_SIZE_ATT), module, Spindrift::SIZE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_SHAPE_ATT), module, Spindrift::SHAPE_ATT_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_PITCH_ATT), module, Spindrift::PITCH_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_FEEDBACK), module, Spindrift::FEEDBACK_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_DRYWET), module, Spindrift::DRYWET_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_REVERB), module, Spindrift::REVERB_PARAM));
		// Momentary caps, lit by the module (the spec, "Panel").
		addParam(createLightParamCentered<rka::CapButton>(mm2px(PARAM_FREEZE), module, Spindrift::FREEZE_PARAM,
		                                                  Spindrift::FREEZE_LIGHT));
		addParam(createLightParamCentered<rka::CapButton>(mm2px(PARAM_SEED), module, Spindrift::SEED_PARAM,
		                                                  Spindrift::SEED_LIGHT));
		addParam(createLightParamCentered<rka::CapButton>(mm2px(PARAM_QUALITY), module, Spindrift::QUALITY_PARAM,
		                                                  Spindrift::QUALITY_LIGHT));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_TIME_CV), module, Spindrift::TIME_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SIZE_CV), module, Spindrift::SIZE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DENSITY_CV), module, Spindrift::DENSITY_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SHAPE_CV), module, Spindrift::SHAPE_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_PITCH_CV), module, Spindrift::PITCH_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FEEDBACK_CV), module, Spindrift::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DRYWET_CV), module, Spindrift::DRYWET_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_REVERB_CV), module, Spindrift::REVERB_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_L), module, Spindrift::L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_R), module, Spindrift::R_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FREEZE), module, Spindrift::FREEZE_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SEED), module, Spindrift::SEED_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Spindrift::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Spindrift::R_OUTPUT));
	}

	// The browser's picture: a phrase recorded into a Digital stereo buffer,
	// the head two thirds across, a dozen grains sounding.
	void makePreview() {
		const int N = rka::Overview::COLS;
		const float head = 0.64f;
		for (int c = 0; c < N; c++) {
			const float x = float(c) / N;
			// The newest audio just left of the head, the oldest just right.
			const float age = x <= head ? head - x : 1.f + head - x;
			const float swell = 0.5f + 0.5f * std::sin(6.2832f * (x * 3.f + 0.1f));
			const float a = (0.12f + 0.55f * swell * (0.6f + 0.4f * std::sin(41.f * x))) * (1.f - 0.35f * age);
			shown.lo[c].store(-a);
			shown.hi[c].store(0.92f * a);
		}
		spindrift::GrainView::Copy& c = preview;
		c = spindrift::GrainView::Copy();
		static const float g[][4] = {
		    // position behind the head, span, pan, envelope
		    {0.05f, 0.03f, 0.20f, 0.95f}, {0.12f, 0.05f, 0.70f, 0.60f}, {0.18f, 0.02f, 0.45f, 0.85f},
		    {0.26f, 0.06f, 0.10f, 0.40f}, {0.31f, 0.04f, 0.90f, 0.75f}, {0.40f, 0.03f, 0.55f, 0.30f},
		    {0.47f, 0.05f, 0.30f, 0.90f}, {0.55f, 0.02f, 0.80f, 0.50f}, {0.63f, 0.04f, 0.15f, 0.65f},
		    {0.72f, 0.03f, 0.60f, 0.35f}, {0.80f, 0.05f, 0.40f, 0.80f}, {0.88f, 0.02f, 0.75f, 0.45f},
		};
		c.n = int(sizeof g / sizeof g[0]);
		for (int i = 0; i < c.n; i++)
			c.grains[i] = {g[i][0], g[i][1], g[i][2], g[i][3]};
		c.nt = 0;
		c.head = head;
		c.mode = spindrift::MODE_GRAIN;
		c.quality = c.pending = spindrift::DIGITAL;
		c.mono = false;
		c.frozen = false;
		c.pendingQuality = false;
		c.seconds = 4.f;
		c.bank = 0;
		c.wave = 0.f;
	}

	void appendContextMenu(Menu* menu) override {
		Spindrift* m = getModule<Spindrift>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem(
		    "Synth mode", {"Automatic (as Beads)", "Always", "Never"},
		    [=]() { return size_t(m->engine.settings.synth.load()); },
		    [=](size_t i) { m->engine.settings.synth.store(spindrift::SynthPolicy(std::min<size_t>(i, 2))); }));
		menu->addChild(createIndexSubmenuItem(
		    "Input gain", {"Fixed (±10 V is full scale)", "Automatic (as Beads)", "Manual"},
		    [=]() { return size_t(m->engine.settings.gain.load()); },
		    [=](size_t i) { m->engine.settings.gain.store(spindrift::GainMode(std::min<size_t>(i, 2))); }));
		menu->addChild(new ManualGainSlider(m));
		menu->addChild(createBoolMenuItem(
		    "Gate output (R: a gate per grain; L: the mono mix)", "",
		    [=]() { return m->engine.settings.gateOutput.load(); },
		    [=](bool on) { m->engine.settings.gateOutput.store(on); }));
		menu->addChild(createBoolMenuItem(
		    "Keep the frozen buffer with the patch", "", [=]() { return m->engine.settings.keepFrozen.load(); },
		    [=](bool on) { m->engine.settings.keepFrozen.store(on); }));
	}
};


Model* modelSpindrift = createModel<Spindrift, SpindriftWidget>("Spindrift");

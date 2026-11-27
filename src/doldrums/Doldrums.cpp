// Doldrums: freeze and stretch (spec: docs/specs/2026-11-26-doldrums-design.md).
// The module maps params, ports, the Freeze cap's light, the menu and the
// patch's data onto the Rack-free engine (DoldrumsEngine.hpp) and the
// keeper of its tape (TapeKeeper.hpp: the worker thread that reads,
// converts and writes the frozen tape's file, TapeStore.hpp's sequence, and
// the window's two views), and draws the tape's window from TapeView.hpp's
// snapshots.
//
// Threads (TapeKeeper.hpp has the worker's and the window's side in full):
// - The audio thread: process(): the frame, engine.process(), the outputs,
//   the Freeze cap's light, then keeper.afterProcess() (the window's view)
//   and, when it published, the figures the tooltips and the drag read
//   (relaxed atomics).
// - Rack's threads holding the engine's lock exclusively (process() held
//   off): the constructor (seed, allocate, setSampleRate), onAdd
//   (keeper.expectStored), onSampleRateChange (allocate, setSampleRate) and
//   onReset (reset), each with the keeper's call for the fed view.
// - The worker (the keeper's thread): retired, tapeWanted, giveUp, offer and
//   the save's hold, and nothing of Rack's APP (APP is per thread). Its
//   log, makeDir and exists hooks call only Rack's logger and system::.
// - The UI thread: onSave (keeper.save, which waits for the worker, then
//   tape.wav removed through Rack if the save says so), the menu (relaxed
//   atomics), dataToJson / dataFromJson, the tooltips and the window
//   (keeper.read).
//
// The rules carried from the engine's and the store's reviews, and where
// each is kept:
// - The worker is joined before the engine is destroyed: onRemove, and the
//   keeper's destructor, which runs before the engine's (it is declared
//   after it). Only the worker frees rings; it collects retired() every
//   time it wakes (every 5 ms at the latest), between jobs, so a
//   conversion's source is never freed while it reads it and the engine's
//   eight slots never fill.
// - The save's hold is taken only on the worker, only for the copy
//   (saveStep). One save at a time (the keeper's save lock; its worker
//   serves one request at a time); none meets expectStored, allocate,
//   setSampleRate or reset, which run with the engine's lock held
//   exclusively, while onSave holds it shared (Rack's prepareSave) and does
//   not return until the worker has written tape.wav or declined. A save
//   while a tape is on its way leaves the file alone (savePlan's
//   tapePending).
// - A failed load: giveUp(ticket) and a warning in the log; a stored file
//   that exists and could not be read, or a frozen tape that could not be
//   written, is also shown in the window ("TAPE NOT READ", "TAPE NOT
//   SAVED"), since the user can do something about it. A patch saved
//   without its tape opens silent with a line of INFO.
// - The window: wrote() once for each frame that went onto the tape, after
//   engine.process(), and publish() about 60 times a second, on the audio
//   thread; a tape handed in has the spare view rebuilt by the worker
//   before the offer, and the audio thread switches to it when tapesTaken()
//   moves (TapeStore.hpp 2c and 2d).
// - The edge detectors arm on the first sample after setSampleRate or reset
//   (the engine's own rule), so a patch opened with Freeze on is not a press.
// - Only a tape frozen by the BUTTON is kept (savePlan); the Freeze cap is
//   lit while frozen by the button or the gate (DoldrumsResult::frozen).
//
// Choices (the builder's, the design's "Left to the builder"):
// - Tooltips (Readout.hpp): Stretch "x8.0" / "x128" / "held"; Root a note
//   ("C2"); Width "25 cents"; Shift "+7.0" semitones ("0" in the detent);
//   Position "now" or "12.3 s ago"; Partials a whole number; Tilt dB per
//   octave; Blur octaves; Harmonics % ; Mix "% wet". With a CV moving
//   Stretch, Position or Harmonics, the tooltip adds what is heard with it.
// - While recording (Task 14, the user's request at 2.0.46): a submenu,
//   "Drift" and "Follow", each with a line under it saying what is heard:
//   Drift "Stretch acts on the input as it records", Follow "Stretch
//   waits for Freeze; the input plays on at the flag" (each true at every
//   Stretch, its 1 and its stop included; fix round 1 of the review: the
//   first wording, "falls behind, then back to the flag", was false at x1
//   and at the stop). Saved as "whileRecording" (SavedState.hpp); Initialize sets
//   Drift. While the engine says it is following (recording in Follow,
//   sounding), Stretch's tooltip adds " (waits for Freeze)" after its figure
//   (and the CV's), and the window's corner says FOLLOW. Stopped in Follow
//   the corner says STOPPED and the tooltip adds nothing (it is waiting for
//   TRIG; after TRIG it follows and says so); frozen, the frozen words and
//   no addition, in either setting.
// - Randomize leaves Freeze, Mix and the menu alone (Freeze would freeze the
//   tape, Mix could silence the effect); the rest is fair game. A Stretch at
//   the stop is held, not silent.
// - The window (DoldrumsWindow below), in the theme's colours only: the
//   tape as cyan peaks, bright where the pass has still to play (Readout.hpp's
//   toPlay) and at 30 % elsewhere; the slice as a pale cyan band at its true
//   width with a cyan bar along its foot; the read point (the slice's end,
//   the manual's read head) a brass line, bolder than the flag's; the
//   anchor (the manual's Position flag) a cyan line with a small cyan flag
//   at the top. The state word (TapeView.hpp: FROZEN, HELD, STOPPED,
//   FOLLOW, FROZEN HELD, FROZEN AT END, FROZEN STOPPED) top right, and a file's trouble
//   top left, each on a patch of the glass's own colour so the peaks never
//   cross the letters. Before the first snapshot: a line along the middle,
//   as Eddy's empty tape. A silent tape draws its 0 V line dimly.
#include "plugin.hpp"
#include "DoldrumsEngine.hpp"
#include "DoldrumsLayout.hpp"
#include "Readout.hpp"
#include "SavedState.hpp"
#include "TapeKeeper.hpp"
#include "TapeStore.hpp"
#include "TapeView.hpp"
#include "widgets.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

using rka::doldrums::DoldrumsEngine;
using rka::doldrums::DoldrumsFrame;
using rka::doldrums::DoldrumsResult;
using rka::doldrums::Ring;
using rka::doldrums::TapeSnapshot;

struct Doldrums : Module {
	// Never renumber (saved patches refer to them); the plan's order.
	enum ParamId {
		STRETCH_PARAM, STRETCH_ATT_PARAM, POSITION_PARAM, POSITION_ATT_PARAM, HARMONICS_PARAM, HARMONICS_ATT_PARAM,
		BLUR_PARAM, ROOT_PARAM, TILT_PARAM, SHIFT_PARAM, PARTIALS_PARAM, WIDTH_PARAM, FREEZE_PARAM, MIX_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		STRETCH_CV_INPUT, POSITION_CV_INPUT, HARMONICS_CV_INPUT, L_INPUT, R_INPUT, FREEZE_INPUT, TRIG_INPUT, VOCT_INPUT,
		INPUTS_LEN
	};
	enum OutputId { RUN_OUTPUT, L_OUTPUT, R_OUTPUT, OUTPUTS_LEN };
	enum LightId { FREEZE_LIGHT, LIGHTS_LEN };

	DoldrumsEngine engine;
	DoldrumsFrame frame;
	DoldrumsResult result;
	// The worker, the window's views and the save (TapeKeeper.hpp).
	rka::doldrums::TapeKeeper keeper{engine};

	// The menu (UI writes, audio reads; saved by dataToJson).
	std::atomic<int> grain_{rka::doldrums::GRAIN_MEDIUM};
	std::atomic<bool> wide_{false}, stopAtEnd_{false}, keepTape_{true};
	std::atomic<bool> follow_{false};   // While recording: false Drift (the default), true Follow

	// What the audio thread last published (with the window's snapshot), for
	// the tooltips and the window's drag.
	int publishEvery_ = 800;
	std::atomic<float> fs_{48000.f};
	std::atomic<long long> capacity_{0};
	std::atomic<int> grainFrames_{0};
	std::atomic<float> pace_{0.125f}, heardStretch_{0.5f}, heardPosition_{1.f}, heardHarmonics_{0.f};
	std::atomic<bool> following_{false};   // DoldrumsState::following: Stretch is waiting for Freeze
	// A press, or a drag's move, in the tape window (the UI thread counts,
	// after setting Position): the audio thread raises frame.restart on the
	// sample it sees the count change, and the engine takes it as a trigger
	// (the user's ruling: a press plays from there). Only the window counts:
	// the knob, the CV, undo and redo, Initialize and a patch opening move
	// Position by its own rule and start no pass.
	std::atomic<uint32_t> windowPresses_{0};
	uint32_t pressesSeen_ = 0;   // the audio thread's

	// ---- The tooltips.

	struct StretchQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			char buf[16];
			rka::doldrums::stretchText(rka::doldrums::paceFromDial(getValue()), buf, sizeof buf);
			std::string s = buf;
			Doldrums* m = static_cast<Doldrums*>(module);
			if (m && m->inputs[STRETCH_CV_INPUT].isConnected()) {
				const float heard = m->heardStretch_.load(std::memory_order_relaxed);
				if (std::fabs(heard - getValue()) > 1e-4f) {
					rka::doldrums::stretchText(rka::doldrums::paceFromDial(heard), buf, sizeof buf);
					s += std::string(" (") + buf + " with CV)";
				}
			}
			// While recording: Follow, recording: the dial's figure takes effect at Freeze.
			if (m && m->following_.load(std::memory_order_relaxed))
				s += " (waits for Freeze)";
			return s;
		}
		void setDisplayValueString(std::string s) override {
			float dial;
			if (rka::doldrums::parseStretch(s, dial))
				setValue(dial);
		}
		std::string getUnit() override {
			return "";
		}
	};

	struct PositionQuantity : ParamQuantity {
		void tape(long long& cap, int& grain, float& rate) {
			Doldrums* m = static_cast<Doldrums*>(module);
			rate = m ? m->fs_.load(std::memory_order_relaxed) : 48000.f;
			cap = m ? m->capacity_.load(std::memory_order_relaxed) : 0;
			grain = m ? m->grainFrames_.load(std::memory_order_relaxed) : 0;
			// Before the first publish: the tape and the menu's grain at this rate.
			if (cap <= 0)
				cap = rka::doldrums::tapeFrames(rate);
			if (grain <= 0)
				grain = rka::doldrums::grainFrames(rka::doldrums::GrainSize(m ? m->grain_.load() : 1), rate);
		}
		std::string getDisplayValueString() override {
			long long cap;
			int grain;
			float rate;
			tape(cap, grain, rate);
			std::string s = rka::doldrums::positionText(getValue(), cap, grain, rate);
			Doldrums* m = static_cast<Doldrums*>(module);
			if (m && m->inputs[POSITION_CV_INPUT].isConnected()) {
				const float heard = m->heardPosition_.load(std::memory_order_relaxed);
				if (std::fabs(heard - getValue()) > 1e-4f)
					s += " (" + rka::doldrums::positionText(heard, cap, grain, rate) + " with CV)";
			}
			return s;
		}
		void setDisplayValueString(std::string s) override {
			long long cap;
			int grain;
			float rate;
			tape(cap, grain, rate);
			float p;
			if (rka::doldrums::parsePosition(s, cap, grain, rate, p))
				setValue(p);
		}
		std::string getUnit() override {
			return "";
		}
	};

	struct HarmonicsQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			std::string s = string::f("%.0f %%", getValue() * 100.f);
			Doldrums* m = static_cast<Doldrums*>(module);
			if (m && m->inputs[HARMONICS_CV_INPUT].isConnected()) {
				const float heard = m->heardHarmonics_.load(std::memory_order_relaxed);
				if (std::fabs(heard - getValue()) > 1e-4f)
					s += string::f(" (%.0f %% with CV)", heard * 100.f);
			}
			return s;
		}
		void setDisplayValueString(std::string s) override {
			float v;
			if (rka::doldrums::parseHarmonics(s, v))
				setValue(v);
		}
		std::string getUnit() override {
			return "";
		}
	};

	struct RootQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return rka::doldrums::noteName(getValue());
		}
		void setDisplayValueString(std::string s) override {
			float semis;
			if (rka::doldrums::parseNote(s, semis))
				setValue(semis);
		}
		std::string getUnit() override {
			return "";
		}
	};

	struct WidthQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return rka::doldrums::widthText(getValue());
		}
		void setDisplayValueString(std::string s) override {
			float w;
			if (rka::doldrums::parseWidth(s, w))
				setValue(w);
		}
		std::string getUnit() override {
			return "";
		}
	};

	struct ShiftQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return rka::doldrums::shiftText(getValue());
		}
		std::string getUnit() override {
			return " semitones";
		}
	};

	Doldrums() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		// process() copies params[i] to frame.params[i] by index, and saved
		// patches hold the ids: every one is pinned.
		static_assert(int(STRETCH_PARAM) == int(DoldrumsFrame::STRETCH) && int(STRETCH_ATT_PARAM) == int(DoldrumsFrame::STRETCH_ATT) &&
		                  int(POSITION_PARAM) == int(DoldrumsFrame::POSITION) &&
		                  int(POSITION_ATT_PARAM) == int(DoldrumsFrame::POSITION_ATT) &&
		                  int(HARMONICS_PARAM) == int(DoldrumsFrame::HARMONICS) &&
		                  int(HARMONICS_ATT_PARAM) == int(DoldrumsFrame::HARMONICS_ATT) && int(BLUR_PARAM) == int(DoldrumsFrame::BLUR) &&
		                  int(ROOT_PARAM) == int(DoldrumsFrame::ROOT) && int(TILT_PARAM) == int(DoldrumsFrame::TILT) &&
		                  int(SHIFT_PARAM) == int(DoldrumsFrame::SHIFT) && int(PARTIALS_PARAM) == int(DoldrumsFrame::PARTIALS) &&
		                  int(WIDTH_PARAM) == int(DoldrumsFrame::WIDTH) && int(FREEZE_PARAM) == int(DoldrumsFrame::FREEZE) &&
		                  int(MIX_PARAM) == int(DoldrumsFrame::MIX) && int(PARAMS_LEN) == int(DoldrumsFrame::kParams) &&
		                  int(PARAMS_LEN) == 14,
		              "ParamId follows DoldrumsFrame::Param, all 14 pinned");
		const DoldrumsFrame d;
		typedef DoldrumsFrame F;
		configParam<StretchQuantity>(STRETCH_PARAM, 0.f, 1.f, d.params[F::STRETCH], "Stretch")->description =
		    "How slowly the read point moves through the sound: 1 to 32 over the lettered travel, rising without limit "
		    "after 32, and held (still) at the end";
		configParam(STRETCH_ATT_PARAM, -1.f, 1.f, d.params[F::STRETCH_ATT], "Stretch CV", "%", 0.f, 100.f);
		configParam<PositionQuantity>(POSITION_PARAM, 0.f, 1.f, d.params[F::POSITION], "Position")->description =
		    "Where a pass starts: now at the right, 30 s back at the left. Press in the window to play from there";
		configParam(POSITION_ATT_PARAM, -1.f, 1.f, d.params[F::POSITION_ATT], "Position CV", "%", 0.f, 100.f);
		configParam<HarmonicsQuantity>(HARMONICS_PARAM, 0.f, 1.f, d.params[F::HARMONICS], "Harmonics")->description =
		    "The tuned comb: 0 % out of the path, 100 % only its teeth pass";
		configParam(HARMONICS_ATT_PARAM, -1.f, 1.f, d.params[F::HARMONICS_ATT], "Harmonics CV", "%", 0.f, 100.f);
		configParam(BLUR_PARAM, 0.f, 1.f, d.params[F::BLUR], "Blur", " octaves")->description =
		    "Smooths the spectrum over this width of pitch, keeping its energy";
		RootQuantity* root = configParam<RootQuantity>(ROOT_PARAM, -36.f, 36.f, d.params[F::ROOT], "Root");
		root->snapEnabled = true;
		root->description = "The comb's note (V/OCT moves it from here)";
		configParam(TILT_PARAM, -1.f, 1.f, d.params[F::TILT], "Tilt", " dB per octave", 0.f, 6.f)->description =
		    "A slope across the spectrum about 1 kHz: left darker, right brighter";
		configParam<ShiftQuantity>(SHIFT_PARAM, -24.f, 24.f, d.params[F::SHIFT], "Shift")->description =
		    "Moves the spectrum up or down; nothing at noon";
		ParamQuantity* partials = configParam(PARTIALS_PARAM, 1.f, 32.f, d.params[F::PARTIALS], "Partials");
		partials->snapEnabled = true;
		partials->description = "The comb's teeth: the note and its overtones";
		configParam<WidthQuantity>(WIDTH_PARAM, 0.f, 1.f, d.params[F::WIDTH], "Width")->description =
		    "How wide each tooth of the comb is";
		configSwitch(FREEZE_PARAM, 0.f, 1.f, d.params[F::FREEZE], "Freeze", {"Off", "On"})->description =
		    "Stops the recording; a pass starts from Position. Kept with the patch, with the tape";
		configParam(MIX_PARAM, 0.f, 1.f, d.params[F::MIX], "Mix", " % wet", 0.f, 100.f);
		// Randomize must not freeze the tape or take the effect away.
		paramQuantities[FREEZE_PARAM]->randomizeEnabled = false;
		paramQuantities[MIX_PARAM]->randomizeEnabled = false;

		configInput(STRETCH_CV_INPUT, "Stretch CV (10 V crosses the dial at full attenuverter)");
		configInput(POSITION_CV_INPUT, "Position CV (10 V crosses the tape at full attenuverter)");
		configInput(HARMONICS_CV_INPUT, "Harmonics CV (10 V crosses it at full attenuverter)");
		configInput(L_INPUT, "Left");
		configInput(R_INPUT, "Right (unpatched: the left)");
		configInput(FREEZE_INPUT, "Freeze gate (frozen while high, as the button)");
		configInput(TRIG_INPUT, "Trigger (a new pass from Position)");
		configInput(VOCT_INPUT, "V/OCT (the comb's note; polyphonic: a comb for each channel)");
		configOutput(RUN_OUTPUT, "Run (10 V while a pass sounds; 1 ms low where a pass begins)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configLight(FREEZE_LIGHT, "Frozen (by the button or the gate)");

		engine.seed(random::u32());
		keeper.log = [](bool warn, const std::string& s) {
			if (warn)
				WARN("Doldrums: %s", s.c_str());
			else
				INFO("Doldrums: %s", s.c_str());
		};
		keeper.makeDir = [](const std::string& dir) {
			try {
				system::createDirectories(dir);
			}
			catch (std::exception& ex) {
				WARN("Doldrums: cannot make %s: %s", dir.c_str(), ex.what());
			}
		};
		keeper.exists = [](const std::string& path) { return system::exists(path); };
		const float rate = APP && APP->engine ? APP->engine->getSampleRate() : 48000.f;
		adoptRate(rate);
	}

	// The engine's buffers for `rate`, off the audio path (the constructor, or
	// onSampleRateChange with process() held off), and the fed view for its tape.
	void adoptRate(float rate) {
		// fs_ (the rate a save stamps on the file) changes only with the
		// engine's: a failed allocate leaves the engine at its old rate.
		if (engine.allocate(rate))
			fs_.store(rate);
		else
			WARN("Doldrums: no tape at %g Hz (out of memory, an unusable rate, or rings not yet collected); the "
			     "module stays at its old rate or is silent",
			     rate);
		engine.setSampleRate(rate);
		keeper.configured();
		publishEvery_ = std::max(1, int(rate / 60.f));
	}

	// ---- Rack's events.

	void onAdd(const AddEvent& e) override {
		keeper.start();
		// A patch opening frozen with its tape kept: the tape is read on the
		// worker and handed in (TapeStore.hpp, 1). Until then the tape is a
		// silent stand-in, frozen, and every save leaves the file alone.
		const std::string dir = getPatchStorageDirectory();
		if (params[FREEZE_PARAM].getValue() >= 0.5f && keepTape_.load() && !dir.empty())
			keeper.expectStored(dir);
	}

	// The worker is joined here and in the destructor (the keeper's, which
	// runs before the engine's: it is declared after it).
	void onRemove(const RemoveEvent& e) override {
		keeper.stop();
	}

	// Rack holds the engine's lock: process() does not run meanwhile. A frozen
	// tape becomes the source of a conversion the worker makes; a tape that
	// is not frozen starts again from silence (DoldrumsEngine.hpp).
	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		adoptRate(e.sampleRate);
		keeper.wake();
	}

	// Initialize: the knobs and Freeze to their defaults, a silent tape, the menu
	// to its defaults.
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.reset();
		keeper.cleared();
		grain_.store(rka::doldrums::GRAIN_MEDIUM);
		wide_.store(false);
		stopAtEnd_.store(false);
		keepTape_.store(true);
		follow_.store(false);
	}

	// The save (TapeStore.hpp, 3): made by the worker, which alone takes the
	// hold; keeper.save waits for it, because Rack packs the storage folder as
	// soon as this returns. Then tape.wav is removed if the save says so.
	void onSave(const SaveEvent& e) override {
		rka::doldrums::TapeKeeper::SaveAsk ask;
		ask.dir = getPatchStorageDirectory();
		ask.keepTape = keepTape_.load();
		ask.button = params[FREEZE_PARAM].getValue() >= 0.5f;
		ask.rate = fs_.load();
		if (ask.dir.empty())
			return;
		rka::doldrums::SaveAction action;
		if (!keeper.save(ask, action)) {
			WARN("Doldrums: saved with no worker running; the stored tape in %s is left as it is", ask.dir.c_str());
			return;
		}
		if (action != rka::doldrums::SAVE_REMOVE)
			return;
		const std::string path = rka::doldrums::store::join(ask.dir, rka::doldrums::STORED_TAPE);
		try {
			if (system::exists(path))
				system::remove(path);
		}
		catch (std::exception& ex) {
			WARN("Doldrums: cannot remove %s: %s", path.c_str(), ex.what());
		}
	}

	json_t* dataToJson() override {
		rka::doldrums::SavedState s;
		s.grain = grain_.load();
		s.wide = wide_.load();
		s.stopAtEnd = stopAtEnd_.load();
		s.keepTape = keepTape_.load();
		s.follow = follow_.load();
		json_t* root = json_object();
		for (const auto& kv : rka::doldrums::savedToMap(s))
			json_object_set_new(root, kv.first.c_str(),
			                    kv.second.kind == rka::doldrums::SavedValue::BOOL ? json_boolean(kv.second.value != 0.0)
			                                                                       : json_integer(json_int_t(kv.second.value)));
		return root;
	}

	// SavedState.hpp decides what loads. The tape comes in onAdd (a patch's
	// load); onto a running module (a preset, an undo) the tape stays.
	void dataFromJson(json_t* root) override {
		rka::doldrums::SavedMap m;
		const char* key;
		json_t* v;
		json_object_foreach(root, key, v) {
			if (json_is_boolean(v))
				m[key] = {rka::doldrums::SavedValue::BOOL, json_is_true(v) ? 1.0 : 0.0};
			else if (json_is_integer(v))
				m[key] = {rka::doldrums::SavedValue::INT, double(json_integer_value(v))};
			else if (json_is_real(v))
				m[key] = {rka::doldrums::SavedValue::REAL, json_real_value(v)};
		}
		const rka::doldrums::SavedState s = rka::doldrums::savedFromMap(m);
		grain_.store(s.grain);
		wide_.store(s.wide);
		stopAtEnd_.store(s.stopAtEnd);
		keepTape_.store(s.keepTape);
		follow_.store(s.follow);
	}

	// ---- The audio thread.

	void process(const ProcessArgs& args) override {
		for (int i = 0; i < PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		frame.stretchCv = inputs[STRETCH_CV_INPUT].getVoltage();
		frame.positionCv = inputs[POSITION_CV_INPUT].getVoltage();
		frame.harmonicsCv = inputs[HARMONICS_CV_INPUT].getVoltage();
		frame.freezeGate = inputs[FREEZE_INPUT].getVoltage();
		frame.trig = inputs[TRIG_INPUT].getVoltage();
		const uint32_t presses = windowPresses_.load(std::memory_order_acquire);
		frame.restart = presses != pressesSeen_;
		pressesSeen_ = presses;
		frame.inL = inputs[L_INPUT].getVoltageSum();
		frame.inR = inputs[R_INPUT].getVoltageSum();
		frame.inRPatched = inputs[R_INPUT].isConnected();
		frame.voctChannels = inputs[VOCT_INPUT].getChannels();
		frame.voct = inputs[VOCT_INPUT].getVoltages();
		frame.grainSize = grain_.load(std::memory_order_relaxed);
		frame.wide = wide_.load(std::memory_order_relaxed);
		frame.stopAtEnd = stopAtEnd_.load(std::memory_order_relaxed);
		frame.follow = follow_.load(std::memory_order_relaxed);

		engine.process(frame, result);

		outputs[L_OUTPUT].setVoltage(result.outL);
		outputs[R_OUTPUT].setVoltage(result.outR);
		outputs[RUN_OUTPUT].setVoltage(result.run);
		lights[FREEZE_LIGHT].setBrightness(result.frozen ? 1.f : 0.f);

		if (keeper.afterProcess(publishEvery_)) {
			const rka::doldrums::DoldrumsState& s = engine.state();
			capacity_.store(s.capacity, std::memory_order_relaxed);
			grainFrames_.store(s.grain, std::memory_order_relaxed);
			pace_.store(float(s.pace), std::memory_order_relaxed);
			following_.store(s.following && !s.frozen, std::memory_order_relaxed);
			typedef DoldrumsFrame F;
			heardStretch_.store(rka::doldrums::cvValue(frame.params[F::STRETCH], frame.params[F::STRETCH_ATT], frame.stretchCv),
			                    std::memory_order_relaxed);
			heardPosition_.store(rka::doldrums::cvValue(frame.params[F::POSITION], frame.params[F::POSITION_ATT], frame.positionCv),
			                     std::memory_order_relaxed);
			heardHarmonics_.store(
			    rka::doldrums::cvValue(frame.params[F::HARMONICS], frame.params[F::HARMONICS_ATT], frame.harmonicsCv),
			    std::memory_order_relaxed);
		}
	}

	// Bypassed: the inputs to the outputs, IN R taking IN L when unpatched.
	void processBypass(const ProcessArgs&) override {
		const float l = inputs[L_INPUT].getVoltageSum();
		outputs[L_OUTPUT].setVoltage(l);
		outputs[R_OUTPUT].setVoltage(inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() : l);
		outputs[RUN_OUTPUT].setVoltage(0.f);
		lights[FREEZE_LIGHT].setBrightness(0.f);
	}
};


// ---- The window.

// What the window draws, from the module or the browser's preview.
struct DoldrumsWindowView {
	TapeSnapshot snap;
	float pace = 0.f;
	int trouble = rka::doldrums::TapeKeeper::FINE;
};

// The tape's window (the design's "The window"; the choices are at the top of
// this file). It knows nothing of the engine: `source` fills a view, or
// returns false while there is nothing to read yet. The mouse: a left press
// sets Position at once, a drag follows it, the release ends one undo step
// (onPosition, onBegin, onEnd); the right button is the module's menu.
struct DoldrumsWindow : widget::Widget {
	std::function<bool(DoldrumsWindowView&)> source;
	std::function<void()> onBegin, onEnd;
	std::function<void(float fraction)> onPosition;
	bool dragging = false;
	// Volts at the glass's top and bottom edges, as Eddy's window.
	static constexpr float RANGE_VOLTS = 6.f;
	// The least spacing of the strokes, in px at 100 % zoom.
	static constexpr float STROKE_PX = 1.6f;

	float pad() const {
		return mm2px(0.6f);
	}
	float xAt(float f) const {
		return pad() + f * (box.size.x - 2.f * pad());
	}
	float fractionAt(float x) const {
		return math::clamp((x - pad()) / (box.size.x - 2.f * pad()), 0.f, 1.f);
	}
	float yAt(float volts) const {
		float v = (volts - volts == 0.f) ? volts : 0.f;
		v = math::clamp(v, -RANGE_VOLTS, RANGE_VOLTS);
		const float half = 0.5f * box.size.y - pad();
		return 0.5f * box.size.y - v / RANGE_VOLTS * half;
	}
	float mouseX() {
		return APP->scene->rack->getMousePos().x - getRelativeOffset(Vec(), APP->scene->rack).x;
	}

	void onButton(const ButtonEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		e.consume(this);
		if (e.action != GLFW_PRESS || !onPosition)
			return;
		if (dragging && onEnd)   // a release that was lost
			onEnd();
		dragging = true;
		if (onBegin)
			onBegin();
		onPosition(fractionAt(e.pos.x));
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && dragging && onPosition)
			onPosition(fractionAt(mouseX()));
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !dragging)
			return;
		dragging = false;
		if (onEnd)
			onEnd();
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && source) {
			DoldrumsWindowView v;
			const bool have = source(v);
			drawView(args, have ? &v : nullptr);
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

	// The words' patches, as drawn: a mark under one starts below it.
	float patchX0[2] = {0.f, 0.f}, patchX1[2] = {0.f, 0.f}, patchY1[2] = {0.f, 0.f};
	float markTop(float x, float top) const {
		for (int i = 0; i < 2; i++)
			if (x >= patchX0[i] && x <= patchX1[i])
				top = std::max(top, patchY1[i]);
		return top;
	}

	// Words in a corner: first a patch of the glass's colour (`patch`), which
	// neither the peaks nor the marks cross, then the letters.
	void corner(NVGcontext* vg, const char* text, bool right, bool patch) {
		const int side = right ? 1 : 0;
		if (patch)
			patchX0[side] = patchX1[side] = patchY1[side] = 0.f;
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "tools/fonts/D-DINCondensed.ttf"));
		if (!font || font->handle < 0 || !text || !*text)
			return;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, mm2px(2.6f));
		nvgTextAlign(vg, (right ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);
		const float x = right ? box.size.x - mm2px(1.0f) : mm2px(1.0f), y = mm2px(0.7f);
		if (patch) {
			float b[4];
			nvgTextBounds(vg, x, y, text, NULL, b);
			const float m = mm2px(0.4f);
			nvgBeginPath(vg);
			nvgRect(vg, b[0] - m, b[1] - m, b[2] - b[0] + 2.f * m, b[3] - b[1] + 2.f * m);
			patchX0[side] = b[0] - m - 1.f;
			patchX1[side] = b[2] + m + 1.f;
			patchY1[side] = b[3] + m;
			nvgFillColor(vg, settings::preferDarkPanels ? RkaTheme::GLASS_DARK : RkaTheme::GLASS);
			nvgFill(vg);
		}
		else {
			nvgFillColor(vg, RkaTheme::SIGNAL);
			nvgText(vg, x, y, text, NULL);
		}
	}

	void drawView(const DrawArgs& args, const DoldrumsWindowView* v) {
		NVGcontext* vg = args.vg;
		const NVGcolor signal = RkaTheme::SIGNAL;
		const NVGcolor brass = settings::preferDarkPanels ? RkaTheme::BRASS_DARK : RkaTheme::BRASS;
		const float top = pad(), bottom = box.size.y - pad();
		nvgSave(vg);
		nvgScissor(vg, 0.f, 0.f, box.size.x, box.size.y);
		if (!v) {
			line(vg, xAt(0.f), yAt(0.f), xAt(1.f), yAt(0.f), nvgTransRGBAf(signal, 0.6f), 1.f);
			nvgRestore(vg);
			return;
		}
		const TapeSnapshot& s = v->snap;
		// 0 V, dimly, so a silent tape still reads as a tape.
		line(vg, xAt(0.f), yAt(0.f), xAt(1.f), yAt(0.f), nvgTransRGBAf(signal, 0.2f), 0.5f);

		// The slice, at its true width (at least a pixel).
		const float sx0 = xAt(s.readStart), sx1 = std::max(xAt(s.readEnd), sx0 + 1.f);
		nvgBeginPath(vg);
		nvgRect(vg, sx0, top, sx1 - sx0, bottom - top);
		nvgFillColor(vg, nvgTransRGBAf(signal, 0.16f));
		nvgFill(vg);
		line(vg, sx0, bottom - mm2px(0.25f), sx1, bottom - mm2px(0.25f), signal, mm2px(0.5f));

		// The peaks, a few columns to a stroke where they are narrow; bright
		// where the pass has still to play.
		float from, to;
		rka::doldrums::toPlay(s, v->pace, from, to);
		const int N = TapeSnapshot::COLS;
		const float colW = (box.size.x - 2.f * pad()) / N;
		int group = 1;
		while (group < N && group * colW < STROKE_PX)
			group *= 2;
		for (int bright = 0; bright < 2; bright++) {
			nvgBeginPath(vg);
			for (int c0 = 0; c0 < N; c0 += group) {
				const float f = (c0 + 0.5f * group) / N;
				if ((f >= from && f <= to) != (bright == 1))
					continue;
				float lo = s.low[c0], hi = s.high[c0];
				for (int c = c0 + 1; c < c0 + group && c < N; c++) {
					lo = std::min(lo, s.low[c]);
					hi = std::max(hi, s.high[c]);
				}
				const float y0 = yAt(hi), y1 = yAt(lo);
				if (!(y1 - y0 > 0.f))
					continue;
				const float x = xAt(f);
				nvgMoveTo(vg, x, y0);
				nvgLineTo(vg, x, y1);
			}
			nvgStrokeColor(vg, bright ? signal : nvgTransRGBAf(signal, 0.3f));
			nvgStrokeWidth(vg, 0.55f * group * colW);
			nvgStroke(vg);
		}

		// The words' patches, then the marks, then the letters.
		const char* trouble = v->trouble == rka::doldrums::TapeKeeper::NOT_READ    ? "TAPE NOT READ"
		                      : v->trouble == rka::doldrums::TapeKeeper::NOT_SAVED ? "TAPE NOT SAVED"
		                                                               : "";
		corner(vg, s.state, true, true);
		corner(vg, trouble, false, true);

		// The anchor: a cyan line with a flag at its top.
		const float ax = xAt(s.anchor), at = markTop(ax, top);
		line(vg, ax, at, ax, bottom, nvgTransRGBAf(signal, 0.8f), 0.6f);
		const float flag = mm2px(0.9f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, ax - 0.6f * flag, at);
		nvgLineTo(vg, ax + 0.6f * flag, at);
		nvgLineTo(vg, ax, at + flag);
		nvgClosePath(vg);
		nvgFillColor(vg, signal);
		nvgFill(vg);

		// The read head (the read point, the slice's end): a brass line, bolder
		// than the flag's, the "you are here" mark (the user's decision at
		// 2.0.46: the ivory line was hard to tell from the flag).
		const float rx = xAt(s.readEnd);
		line(vg, rx, markTop(rx, top), rx, bottom, brass, 1.5f);

		corner(vg, s.state, true, false);
		corner(vg, trouble, false, false);
		nvgRestore(vg);
	}
};


// The browser's picture (no module). With RKA_DOLDRUMS_PREVIEW naming a state
// (make smoke sets it: running, frozen, held, stopped, frozen-held,
// frozen-at-end, frozen-stopped, follow), a real engine at 48 kHz is run offline on a phrase of
// chords and the snapshot it publishes is drawn, so the screenshot shows
// what the module draws in that state. Without it, a picture made up from a
// formula (the browser must not pay for an engine).
namespace {

// A phrase: five swelling chords, 2.6 s apart, a breath between.
void phrase(double t, float& l, float& r) {
	static const double kRoots[5] = {110.0, 98.0, 130.81, 87.31, 116.54};
	const double k = std::fmod(t, 13.0);
	const int i = int(k / 2.6);
	const double u = k - 2.6 * i;
	const double env = u < 0.15 ? u / 0.15 : (u < 2.2 ? 1.0 - 0.6 * (u - 0.15) / 2.05 : 0.4 * (2.6 - u) / 0.4);
	const double f = kRoots[i];
	const double x = env * (2.0 * std::sin(2.0 * M_PI * f * t) + 1.2 * std::sin(2.0 * M_PI * f * 1.5 * t) +
	                        0.8 * std::sin(2.0 * M_PI * f * 2.0 * t));
	l = float(x);
	r = float(0.9 * x);
}

bool previewFromEngine(const std::string& state, DoldrumsWindowView& out) {
	using namespace rka::doldrums;
	std::unique_ptr<DoldrumsEngine> e(new DoldrumsEngine);
	std::unique_ptr<TapeView> view(new TapeView);
	const float rate = 48000.f;
	e->seed(1);
	if (!e->allocate(rate))
		return false;
	e->setSampleRate(rate);
	view->configure(e->state().capacity, e->state().now);
	DoldrumsFrame f;
	DoldrumsResult res;
	f.inRPatched = true;
	long long t = 0, lastNow = e->ring().now();
	auto run = [&](double seconds) {
		const long long n = (long long) (seconds * rate);
		for (long long i = 0; i < n; i++, t++) {
			phrase(double(t) / rate, f.inL, f.inR);
			e->process(f, res);
			f.trig = 0.f;
			Ring& g = e->ring();
			if (g.now() == lastNow + 1) {
				const long long p = g.physical(g.now() - 1);
				view->wrote(g.channel(0)[p], g.channel(1)[p]);
			}
			lastNow = g.now();
		}
	};
	typedef DoldrumsFrame F;
	if (state == "running") {
		f.params[F::POSITION] = 0.55f;
		run(32.0);
	}
	else if (state == "frozen") {
		f.params[F::POSITION] = 0.35f;
		f.params[F::STRETCH] = 1.f / 6.f;   // x2
		run(31.0);
		f.params[F::FREEZE] = 1.f;
		run(5.0);
	}
	else if (state == "held") {
		f.params[F::POSITION] = 0.6f;
		run(30.0);
		f.params[F::STRETCH] = 1.f;   // the stop
		run(6.0);
	}
	else if (state == "stopped") {
		// Near the oldest edge at x32: the oldest sound catches the read
		// point about 3 s after the trigger. (Fully left would never stop
		// while recording: the plan's second deliberate difference.)
		f.stopAtEnd = true;
		f.params[F::POSITION] = 0.1f;
		f.params[F::STRETCH] = 5.f / 6.f;
		run(31.0);
		f.trig = 10.f;
		run(5.0);
	}
	else if (state == "frozen-held") {
		f.params[F::POSITION] = 0.6f;
		run(31.0);
		f.params[F::FREEZE] = 1.f;
		run(0.5);
		f.params[F::STRETCH] = 1.f;   // the stop: FROZEN HELD
		run(2.0);
	}
	else if (state == "frozen-at-end") {
		f.params[F::POSITION] = 1.f;
		run(31.0);
		f.params[F::FREEZE] = 1.f;   // Position fully right: nothing to travel through, FROZEN AT END
		run(2.0);
	}
	else if (state == "follow") {
		// While recording: Follow at x8: the read head on the flag, FOLLOW.
		f.follow = true;
		f.params[F::POSITION] = 0.55f;
		run(32.0);
	}
	else if (state == "frozen-stopped") {
		f.stopAtEnd = true;
		f.params[F::POSITION] = 0.88f;
		f.params[F::STRETCH] = 0.f;   // x1: about 3.5 s to the end
		run(31.0);
		f.params[F::FREEZE] = 1.f;
		run(0.1);
		f.trig = 10.f;
		run(5.0);
	}
	else
		return false;
	view->publish(e->state());
	out.pace = float(e->state().pace);
	return view->read(out.snap);
}

void previewFromFormula(DoldrumsWindowView& out) {
	TapeSnapshot& s = out.snap;
	std::memset(&s, 0, sizeof s);
	for (int c = 0; c < TapeSnapshot::COLS; c++) {
		float l, r;
		const double t = 30.0 * c / TapeSnapshot::COLS;
		float peak = 0.f;
		for (int k = 0; k < 8; k++) {
			phrase(t + 0.007 * k, l, r);
			peak = std::max(peak, std::fabs(l));
		}
		s.low[c] = -peak;
		s.high[c] = 0.95f * peak;
	}
	s.anchor = 0.45f;
	s.readEnd = 0.58f;
	s.readStart = s.readEnd - 0.0113f;
	s.frozen = true;
	std::strcpy(s.state, "FROZEN");
	out.pace = 0.125f;
}

} // namespace


struct DoldrumsWidget : ModuleWidget {
	DoldrumsWindowView preview;
	bool havePreview = false;
	float positionBefore = 1.f;

	DoldrumsWidget(Doldrums* module) {
		using namespace DoldrumsLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/doldrums/Doldrums.svg"),
		                     asset::plugin(pluginInstance, "res/doldrums/Doldrums-dark.svg")));
		if (!module) {
			const char* state = std::getenv("RKA_DOLDRUMS_PREVIEW");
			if (state && *state) {
				havePreview = previewFromEngine(state, preview);
				if (!havePreview)
					WARN("Doldrums: no preview for RKA_DOLDRUMS_PREVIEW=%s", state);
			}
			else {
				previewFromFormula(preview);
				havePreview = true;
			}
		}

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		DoldrumsWindow* window = createWidget<DoldrumsWindow>(mm2px(SCREEN_TAPE.pos));
		window->box.size = mm2px(SCREEN_TAPE.size);
		window->source = [this](DoldrumsWindowView& v) {
			Doldrums* m = getModule<Doldrums>();
			if (!m) {
				v = preview;
				return havePreview;
			}
			if (!m->keeper.read(v.snap))
				return false;
			v.pace = m->pace_.load(std::memory_order_relaxed);
			v.trouble = m->keeper.trouble();
			return true;
		};
		if (module) {
			window->onBegin = [=]() { positionBefore = module->params[Doldrums::POSITION_PARAM].getValue(); };
			window->onPosition = [=](float fraction) { setPosition(module, fraction); };
			window->onEnd = [=]() { endPosition(module); };
		}
		addChild(window);

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_STRETCH), module, Doldrums::STRETCH_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_STRETCH_ATT), module, Doldrums::STRETCH_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_POSITION), module, Doldrums::POSITION_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_POSITION_ATT), module, Doldrums::POSITION_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_HARMONICS), module, Doldrums::HARMONICS_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_HARMONICS_ATT), module, Doldrums::HARMONICS_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_BLUR), module, Doldrums::BLUR_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_ROOT), module, Doldrums::ROOT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_TILT), module, Doldrums::TILT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SHIFT), module, Doldrums::SHIFT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_PARTIALS), module, Doldrums::PARTIALS_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_WIDTH), module, Doldrums::WIDTH_PARAM));
		// The cap is lit while frozen, by the button or the gate.
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_FREEZE), module, Doldrums::FREEZE_PARAM,
		                                                 Doldrums::FREEZE_LIGHT));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_MIX), module, Doldrums::MIX_PARAM));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_STRETCH_CV), module, Doldrums::STRETCH_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_POSITION_CV), module, Doldrums::POSITION_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_HARMONICS_CV), module, Doldrums::HARMONICS_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_L), module, Doldrums::L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_R), module, Doldrums::R_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_FREEZE), module, Doldrums::FREEZE_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_TRIG), module, Doldrums::TRIG_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_VOCT), module, Doldrums::VOCT_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_RUN), module, Doldrums::RUN_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Doldrums::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Doldrums::R_OUTPUT));
	}

	// The window sets the knob so that the anchor lands under the mouse:
	// positionForX at the tape and grain in force, less what the CV adds.
	void setPosition(Doldrums* m, float fraction) {
		const long long cap = m->capacity_.load(std::memory_order_relaxed);
		const int grain = m->grainFrames_.load(std::memory_order_relaxed);
		const float want = rka::doldrums::positionForX(fraction, cap, grain);
		// The CV's share as cvValue (Laws.hpp) reckons it: att x volts / 10.
		const float att = rka::doldrums::detail::volts(m->params[Doldrums::POSITION_ATT_PARAM].getValue(), 1.f);
		const float v = rka::doldrums::detail::volts(m->inputs[Doldrums::POSITION_CV_INPUT].getVoltage(), 100.f);
		m->paramQuantities[Doldrums::POSITION_PARAM]->setValue(math::clamp(want - att * v * 0.1f, 0.f, 1.f));
		m->windowPresses_.fetch_add(1, std::memory_order_release);   // play from here (after Position is set)
	}
	// One undo step per gesture.
	void endPosition(Doldrums* m) {
		const float now = m->params[Doldrums::POSITION_PARAM].getValue();
		if (now == positionBefore)
			return;
		history::ParamChange* h = new history::ParamChange;
		h->name = "set Doldrums' Position";
		h->moduleId = m->id;
		h->paramId = Doldrums::POSITION_PARAM;
		h->oldValue = positionBefore;
		h->newValue = now;
		APP->history->push(h);
	}

	// While recording: Drift or Follow, each with a line under it saying what
	// is heard (the choices at the top of this file); the item shows the
	// setting in force, as Rack's index submenus do.
	static MenuItem* createWhileRecordingItem(Doldrums* m) {
		struct Item : MenuItem {
			Doldrums* m = nullptr;
			void step() override {
				rightText = std::string(m->follow_.load() ? "Follow" : "Drift") + "  " + RIGHT_ARROW;
				MenuItem::step();
			}
			Menu* createChildMenu() override {
				Menu* menu = new Menu;
				Doldrums* mm = m;
				menu->addChild(createCheckMenuItem("Drift", "", [=]() { return !mm->follow_.load(); }, [=]() { mm->follow_.store(false); }));
				menu->addChild(createMenuLabel("Stretch acts on the input as it records"));
				menu->addChild(createCheckMenuItem("Follow", "", [=]() { return mm->follow_.load(); }, [=]() { mm->follow_.store(true); }));
				menu->addChild(createMenuLabel("Stretch waits for Freeze; the input plays on at the flag"));
				return menu;
			}
		};
		Item* item = createMenuItem<Item>("While recording");
		item->m = m;
		return item;
	}

	void appendContextMenu(Menu* menu) override {
		Doldrums* m = getModule<Doldrums>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem(
		    "Grain", {"Short (0.17 s)", "Medium (0.34 s)", "Long (0.68 s)"}, [=]() { return size_t(m->grain_.load()); },
		    [=](size_t i) { m->grain_.store(int(std::min<size_t>(i, 2))); }));
		menu->addChild(createBoolMenuItem(
		    "Wide (each side its own random phases)", "", [=]() { return m->wide_.load(); },
		    [=](bool on) { m->wide_.store(on); }));
		menu->addChild(createIndexSubmenuItem(
		    "At the end of a pass", {"Go round", "Stop (until TRIG)"}, [=]() { return size_t(m->stopAtEnd_.load() ? 1 : 0); },
		    [=](size_t i) { m->stopAtEnd_.store(i == 1); }));
		menu->addChild(createWhileRecordingItem(m));
		menu->addChild(createBoolMenuItem(
		    "Keep the frozen tape with the patch", "", [=]() { return m->keepTape_.load(); },
		    [=](bool on) { m->keepTape_.store(on); }));
	}
};


Model* modelDoldrums = createModel<Doldrums, DoldrumsWidget>("Doldrums");

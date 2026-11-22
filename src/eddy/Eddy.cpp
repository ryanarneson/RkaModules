// Eddy: a tape loop that also plays files (spec:
// docs/specs/2026-11-21-eddy-design.md). The module maps params, ports and
// the patch's state onto rka::EddyEngine, and owns the one worker thread
// that makes, loads, converts, copies, saves and frees tapes (the jobs are
// in TapeJobs.hpp).
//
// Threads, and which calls which of the engine's members:
// - The audio thread: process(); live(), the change counters and
//   readsOriginal(), published together for the save (a seqlock), and the
//   moment the patch's stored tape goes live (the save's baseline).
// - The worker: offer(), setTransport() for a patch's tape, collect(),
//   copyWanted() and copyDone() (the copies), live() (copies, export).
// - The UI (menu, onSave, dataFromJson, onReset, the window): clear() and
//   setTransport() under orderLock_, the lock the worker takes around
//   offer(), so the last of a Clear and an offer wins in the order they
//   were asked for; requestCopy() (the UI's alone); live() under viewLock_.
// - setSampleRate() from onSampleRateChange (an atomic store).
//
// The last request wins (Review Focus 4). Every job and every Clear or
// Initialize is numbered in the order it was asked for; what the worker does
// with the sets it makes, offers and takes back is decided by WorkerBook.hpp
// (Rack-free, tested in test_book.cpp) from those numbers, and carried out
// here: a job that finishes at a rate no longer in force runs again from
// its source; a set handed back for its rate is made again only if it is the
// newest (loaded again if it never went live, converted if it did); a set
// from before a Clear is not offered over a usable live set; and the stored
// load's flag for the save is cleared when that load is over, however it
// ends.
//
// Freeing a set. The engine hands back (collect) the sets it is done with,
// but the window and the save may have read one through live() a moment
// before. They use a set only while holding viewLock_; the worker frees a
// handed-back set only once live() no longer returns it and after taking
// and dropping viewLock_. A UI thread that read the set before then has
// finished with it, and one that reads live() after cannot get it (a set
// handed back is never offered again). The worker keeps every set it made
// in sets_; whatever is left is freed by the destructor, after the worker
// is joined and the audio thread no longer runs.
#include "plugin.hpp"
#include "EddyEngine.hpp"
#include "EddyLayout.hpp"
#include "Select.hpp"
#include "TapeJobs.hpp"
#include "WorkerBook.hpp"
#include "widgets.hpp"

#include <osdialog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>


struct Eddy : Module {
	enum ParamId {
		SPEED_PARAM,
		SPEED_ATT_PARAM,
		START_PARAM,
		LENGTH_PARAM,
		WEAR_PARAM,
		SMEAR_PARAM,
		DUB_LEVEL_PARAM,
		MIX_PARAM,
		REC_PARAM,
		DUB_PARAM,
		PLAY_PARAM,
		LOOP_PARAM,
		RESTORE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		START_CV_INPUT,
		VOCT_INPUT,
		SPEED_CV_INPUT,
		L_INPUT,
		R_INPUT,
		REC_INPUT,
		DUB_INPUT,
		PLAY_INPUT,
		RESTORE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		EOC_OUTPUT,
		L_OUTPUT,
		R_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		REC_LIGHT,
		DUB_LIGHT,
		PLAY_LIGHT,
		LOOP_LIGHT,
		LIGHTS_LEN
	};

	typedef rka::TapeSet TapeSet;
	typedef rka::EddyEngine EddyEngine;

	// Seeded per instance, so two Eddys never wear alike.
	EddyEngine engine;
	rka::EddyFrame frame;
	rka::EddyResult result;

	// Saved with the patch (UI thread; `file` also the worker's, under fileLock_).
	bool keepTape = true;
	std::string file;
	std::mutex fileLock_;
	// A recording has stood since `file` was loaded (audio thread): the tape
	// is no longer the file, which filePath() then forgets (the final
	// review's M2).
	std::atomic<bool> fileStale_{false};
	uint32_t takesSeen_ = 0;   // audio thread

	// Rec, Dub and Play as the module last saw or set them (audio thread):
	// a param that differs was pressed (or undone); it is then set to what
	// the engine decides.
	bool shown_[3] = {false, false, false};
	dsp::BooleanTrigger restoreButton_;

	// The window's numbers, from the audio thread every VIEW_SAMPLES.
	static constexpr int VIEW_SAMPLES = 32;
	int viewCountdown_ = 0;
	std::atomic<float> viewHead_{0.f}, viewStart_{0.f}, viewLength_{1.f}, viewOverlap_{0.f}, viewSeconds_{0.f};
	std::atomic<bool> viewEmpty_{true}, viewRecording_{false};

	// What the patch's storage folder holds (TapeJobs.hpp, StoreRecord): a
	// fact the save keeps (UI thread), never an inference. Every write or
	// removal moves storeEpoch_, so a baseline from a load posted before it
	// is not taken.
	rka::eddy::StoreRecord record_;
	std::atomic<uint32_t> storeEpoch_{0};
	// From onAdd's stored load until its set is live, or the load fails, is
	// superseded, cancelled or freed (WorkerBook says when): the save leaves
	// the folder alone (Review Focus 5; the folder may be being read, and an
	// empty tape is live meanwhile). Set only by onAdd; cleared by the audio
	// thread (live) or the worker (the rest).
	std::atomic<bool> storeLoading_{false};
	// The set from the stored load (worker, at its offer), whether it is
	// whole (exactly the files, at this rate) or cut (held short by the
	// capacity), and the epoch it was asked at.
	std::atomic<TapeSet*> storeSet_{nullptr};
	std::atomic<bool> storeWhole_{false}, storeCut_{false};
	std::atomic<uint32_t> storeSetEpoch_{0};
	// The baseline the audio thread posts when that set goes live, whole or
	// cut: the counters then; the save takes it.
	std::atomic<bool> baseReady_{false}, baseCut_{false};
	std::atomic<uint32_t> baseTape_{0}, baseOriginal_{0}, baseEpoch_{0};
	std::atomic<float> baseFs_{0.f};
	// The stored load failed (worker), on the folder of this epoch: the save
	// records it (StoreRecord::held).
	std::atomic<bool> storeFailed_{false};
	std::atomic<uint32_t> storeFailedEpoch_{0};
	TapeSet* seenLive_ = nullptr;   // audio thread
	// The live set, the counters and readsOriginal() as of one sample, for
	// the save: a seqlock the audio thread writes when they change.
	std::atomic<uint32_t> snapSeq_{0};
	std::atomic<TapeSet*> snapSet_{nullptr};
	std::atomic<uint32_t> snapTape_{0}, snapOriginal_{0};
	std::atomic<bool> snapBehind_{false};
	TapeSet* pubSet_ = nullptr;   // audio thread: what was last published
	uint32_t pubTape_ = 0, pubOriginal_ = 0;
	bool pubBehind_ = false, pubAny_ = false;

	// ---- The worker.
	typedef rka::eddy::Request Request;
	typedef rka::eddy::WorkerBook WorkerBook;
	struct Job {
		bool exportTape = false;   // else make the set `req` asks for
		Request req;
		std::string path;          // export
	};
	std::thread worker_;
	std::mutex queueLock_;
	std::condition_variable wake_;
	std::deque<Job> queue_;   // under queueLock_
	bool stop_ = false;       // under queueLock_
	std::mutex orderLock_;    // offer() against clear() and setTransport(), and the numbers below
	uint32_t seq_ = 0;        // under orderLock_: the last request's number (jobs and Clears)
	uint32_t clearSeq_ = 0;   // under orderLock_: the last Clear's
	std::mutex viewLock_;     // held by a UI thread while it uses a set read through live()
	std::atomic<float> fs_{48000.f};
	std::atomic<bool> transportPlay_{false}, transportDub_{false};   // the patch's Play and Dub
	bool added_ = false;   // between onAdd and onRemove (UI)
	// The worker's alone (and the destructor's, after the join).
	std::vector<TapeSet*> sets_;    // every set made and not yet freed
	std::vector<TapeSet*> limbo_;   // handed back or superseded, to be freed
	WorkerBook book_;

	// Speed as the knob's law makes it (flat at -1, 0 and +1), as "x".
	struct SpeedQuantity : ParamQuantity {
		float getDisplayValue() override {
			return rka::eddy::speedKnob(getValue());
		}
		// The knob for a typed speed (the middle of a flat stretch).
		void setDisplayValue(float speed) override {
			if (!std::isfinite(speed))
				return;
			const float w = rka::eddy::SPEED_FLAT_WIDTH, e = rka::eddy::SPEED_KNOB_MAX;
			const float a = std::min(std::fabs(speed), 2.f);
			float k;
			if (a == 0.f)
				k = 0.f;
			else if (a < 1.f)
				k = w + a * (1.f - 2.f * w);
			else if (a == 1.f)
				k = 1.f;
			else
				k = 1.f + w + (a - 1.f) * (e - 1.f - w);
			setValue(speed < 0.f ? -k : k);
		}
	};

	// Length in seconds of the tape there is (in % with no tape).
	struct LengthQuantity : ParamQuantity {
		float tapeSeconds() {
			Eddy* m = static_cast<Eddy*>(module);
			return m ? m->viewSeconds_.load(std::memory_order_relaxed) : 0.f;
		}
		static float seconds(float k, float tape) {
			const float lmin = std::min((float) rka::eddy::LENGTH_MIN_S, tape);
			return math::clamp(lmin * std::pow(tape / lmin, math::clamp(k, 0.f, 1.f)), lmin, tape);
		}
		std::string getDisplayValueString() override {
			const float t = tapeSeconds();
			if (!(t > 0.f))
				return string::f("%.0f %%", getValue() * 100.f);
			const float l = seconds(getValue(), t);
			return l < 1.f ? string::f("%.0f ms", l * 1000.f) : string::f("%.2f s", l);
		}
		// Seconds, or milliseconds if it ends in "ms" ("1.5", "250 ms").
		void setDisplayValueString(std::string s) override {
			char* end = nullptr;
			float v = std::strtof(s.c_str(), &end);
			if (end == s.c_str() || !std::isfinite(v))
				return;
			while (*end == ' ')
				end++;
			const float t = tapeSeconds();
			if (!(t > 0.f)) {
				setValue(v / 100.f);
				return;
			}
			if (end[0] == 'm' || end[0] == 'M')
				v /= 1000.f;
			const float lmin = std::min((float) rka::eddy::LENGTH_MIN_S, t);
			if (!(v > lmin) || !(t > lmin)) {
				setValue(0.f);
				return;
			}
			setValue(std::log(v / lmin) / std::log(t / lmin));
		}
		std::string getUnit() override {
			return "";
		}
	};

	Eddy() : engine(random::u32()) {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const rka::EddyFrame defaults;
		typedef rka::EddyFrame F;
		configParam<SpeedQuantity>(SPEED_PARAM, -rka::eddy::SPEED_KNOB_MAX, rka::eddy::SPEED_KNOB_MAX, defaults.params[F::SPEED],
		                           "Speed (backwards below 0)", "×");
		paramQuantities[SPEED_PARAM]->displayPrecision = 3;
		configParam(SPEED_ATT_PARAM, -1.f, 1.f, defaults.params[F::SPEED_ATT], "Speed CV", "%", 0.f, 100.f);
		configParam(START_PARAM, 0.f, 1.f, defaults.params[F::START], "Start", "%", 0.f, 100.f);
		configParam<LengthQuantity>(LENGTH_PARAM, 0.f, 1.f, defaults.params[F::LENGTH], "Length");
		configParam(WEAR_PARAM, 0.f, 1.f, defaults.params[F::WEAR], "Wear (each pass costs the tape a little)", "%", 0.f, 100.f);
		configParam(SMEAR_PARAM, 0.f, 1.f, defaults.params[F::SMEAR], "Smear (the overlap at the join)", "%", 0.f, 100.f);
		configParam(DUB_LEVEL_PARAM, 0.f, 1.f, defaults.params[F::DUB_LEVEL], "Dub level", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, defaults.params[F::MIX], "Mix", "%", 0.f, 100.f)->description =
		    "0 %: the input alone; 50 %: the input and the tape both at full; 100 %: the tape alone";
		configSwitch(REC_PARAM, 0.f, 1.f, 0.f, "Rec", {"Off", "On"});
		configSwitch(DUB_PARAM, 0.f, 1.f, 0.f, "Dub", {"Off", "On"});
		configSwitch(PLAY_PARAM, 0.f, 1.f, 0.f, "Play", {"Off", "On"});
		configSwitch(LOOP_PARAM, 0.f, 1.f, 1.f, "Loop", {"Off", "On"});
		// Randomize must not start a recording, a dub or the tape.
		for (int p : {REC_PARAM, DUB_PARAM, PLAY_PARAM, LOOP_PARAM})
			paramQuantities[p]->randomizeEnabled = false;
		configButton(RESTORE_PARAM, "Restore (the original back on the tape)");
		configInput(START_CV_INPUT, "Start CV (5 V: the whole tape)");
		configInput(VOCT_INPUT, "V/OCT (speed)");
		configInput(SPEED_CV_INPUT, "Speed CV");
		configInput(L_INPUT, "Left");
		configInput(R_INPUT, "Right (unpatched: the left)");
		configInput(REC_INPUT, "Rec gate");
		configInput(DUB_INPUT, "Dub gate");
		configInput(PLAY_INPUT, "Play trigger (from Start)");
		configInput(RESTORE_INPUT, "Restore trigger");
		configOutput(EOC_OUTPUT, "End of each turn");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configLight(REC_LIGHT, "Recording");
		configLight(DUB_LIGHT, "Dubbing");
		configLight(PLAY_LIGHT, "Playing");
		configLight(LOOP_LIGHT, "Loop");
		const float fs = APP ? APP->engine->getSampleRate() : 48000.f;
		fs_.store(fs);
		engine.setSampleRate(fs);
	}

	~Eddy() {
		stopWorker();
		for (TapeSet* s : sets_)
			delete s;
	}

	// ---- Rack's events.

	void onAdd(const AddEvent& e) override {
		added_ = true;
		startWorker();
		// An empty tape at once (to record into), then the patch's.
		post(Request::FRESH);
		const std::string dir = getPatchStorageDirectory();
		const std::string path = filePath();
		if (keepTape && !dir.empty() && system::isDirectory(dir)) {
			storeLoading_.store(true);
			post(Request::LOAD_STORED, dir, true);
		}
		else if (!keepTape && !path.empty())
			post(Request::LOAD, path, true);
		else
			applyTransport();
	}

	void onRemove(const RemoveEvent& e) override {
		added_ = false;
		stopWorker();
	}

	void onSampleRateChange(const SampleRateChangeEvent& e) override {
		fs_.store(e.sampleRate);
		engine.setSampleRate(e.sampleRate);
		wake_.notify_one();
	}

	// Initialize: the params to their defaults (Loop on), the tape emptied,
	// Rec, Dub and Play off, no file, the tape kept.
	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		clearTape();
		shown_[0] = shown_[1] = shown_[2] = false;
		{
			std::lock_guard<std::mutex> lk(fileLock_);
			file.clear();
		}
		keepTape = true;
	}

	// The stored tape is written here, synchronously: Rack archives the
	// folder as soon as this returns. What to write is planSave's
	// (TapeJobs.hpp) from the facts now and the record of what the folder
	// holds: nothing while the stored tape loads or with no live set; the
	// folder removed with "Keep the tape with the patch" off or an empty
	// tape; each file unless the record says it already holds this tape at
	// this rate; tape.wav from the original while the tape is to become it.
	void onSave(const SaveEvent& e) override {
		const std::string dir = getPatchStorageDirectory();
		std::lock_guard<std::mutex> view(viewLock_);
		TapeSet* s = engine.live();
		// A stored load that went live whole: the files hold it. Cut short by
		// the capacity: they hold more, and stay while the tape is unchanged.
		// Failed: they stay while the tape is empty.
		if (baseReady_.exchange(false) && baseEpoch_.load() == storeEpoch_.load()) {
			if (baseCut_.load())
				rka::eddy::loadedCut(record_, baseTape_.load(), baseOriginal_.load(), baseFs_.load());
			else
				rka::eddy::loadedWhole(record_, baseTape_.load(), baseOriginal_.load(), baseFs_.load());
		}
		if (storeFailed_.exchange(false) && storeFailedEpoch_.load() == storeEpoch_.load())
			rka::eddy::loadFailed(record_);
		rka::eddy::SaveFacts f;
		f.keepTape = keepTape;
		f.loading = storeLoading_.load();
		f.haveSet = s != nullptr;
		if (s) {
			TapeSet* snapSet;
			readSnapshot(snapSet, f.tapeChanges, f.originalChanges, f.readsOriginal);
			f.known = snapSet == s;
			// The accepted race (below) covers these lengths too.
			f.tapeLength = s->tape.length;
			f.originalLength = s->original.length;
			f.fs = s->fs;
		}
		const rka::eddy::SavePlan plan = rka::eddy::planSave(f, record_);
		if (plan.remove) {
			// Rack's filesystem calls throw on failure (a file held open
			// elsewhere, on Windows): a failed removal must not fail the
			// patch's save (the final review's M5). The record is reset
			// either way, so the next save writes both files.
			try {
				if (!dir.empty() && system::exists(dir))
					system::removeRecursively(dir);
			}
			catch (std::exception& ex) {
				WARN("Eddy: cannot remove the stored tape in %s: %s", dir.c_str(), ex.what());
			}
			storeEpoch_.fetch_add(1);
			rka::eddy::savedAs(record_, f, plan, true);
			return;
		}
		if (!plan.tape && !plan.original)
			return;
		createPatchStorageDirectory();
		// THE ACCEPTED RACE (TESTING.md, rule 7): the live tape is read here
		// while the audio thread may write it (Wear, Dub) and the worker may
		// copy onto it (Restore). A torn read is one sample of wear either
		// side; the counters moved, so the next save writes it again.
		std::string why;
		const double t0 = system::getTime();
		const bool ok = rka::eddy::storePlanned(dir, *s, s->fs, plan, why);
		storeEpoch_.fetch_add(1);
		rka::eddy::savedAs(record_, f, plan, ok);
		if (ok)
			INFO("Eddy: stored the tape (%s%s%s) in %.0f ms", plan.tape ? "tape" : "", plan.tape && plan.original ? ", " : "",
			     plan.original ? "original" : "", (system::getTime() - t0) * 1000.0);
		else
			WARN("Eddy: cannot store the tape in %s: %s", dir.c_str(), why.c_str());
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "file", json_string(filePath().c_str()));
		json_object_set_new(root, "keepTape", json_boolean(keepTape));
		return root;
	}

	// Rec is never loaded as on. Play and Dub as saved are applied once the
	// patch's tape is offered (until then the buttons show the engine).
	void dataFromJson(json_t* root) override {
		json_t* f = json_object_get(root, "file");
		const std::string had = filePath();
		const std::string path = f && json_is_string(f) ? json_string_value(f) : "";
		{
			std::lock_guard<std::mutex> lk(fileLock_);
			file = path;
		}
		json_t* k = json_object_get(root, "keepTape");
		keepTape = k && json_is_boolean(k) ? json_boolean_value(k) : true;
		params[REC_PARAM].setValue(0.f);
		transportPlay_.store(params[PLAY_PARAM].getValue() > 0.5f);
		transportDub_.store(params[DUB_PARAM].getValue() > 0.5f);
		shown_[0] = false;
		shown_[1] = params[DUB_PARAM].getValue() > 0.5f;
		shown_[2] = params[PLAY_PARAM].getValue() > 0.5f;
		// A preset onto a module already running: a file other than the one
		// the tape holds, if the tape is not kept; otherwise the tape stays
		// (an undo or a preset with the same file does not load it again
		// over recordings and dubs since; M2).
		if (added_) {
			if (!keepTape && !path.empty() && path != had)
				post(Request::LOAD, path, true);
			else
				applyTransport();
		}
	}

	// ---- The audio thread.

	void process(const ProcessArgs& args) override {
		static_assert(int(SPEED_PARAM) == int(rka::EddyFrame::SPEED) && int(MIX_PARAM) == int(rka::EddyFrame::MIX)
			&& int(REC_PARAM) == int(rka::EddyFrame::PARAMS_LEN), "ParamId ids");
		for (int i = 0; i < rka::EddyFrame::PARAMS_LEN; i++)
			frame.params[i] = params[i].getValue();
		frame.speedCv = inputs[SPEED_CV_INPUT].getVoltage();
		frame.voct = inputs[VOCT_INPUT].getVoltage();
		frame.startCv = inputs[START_CV_INPUT].getVoltage();
		frame.recGate = inputs[REC_INPUT].getVoltage();
		frame.dubGate = inputs[DUB_INPUT].getVoltage();
		frame.playTrig = inputs[PLAY_INPUT].getVoltage();
		frame.restoreTrig = inputs[RESTORE_INPUT].getVoltage();
		frame.inL = inputs[L_INPUT].getVoltageSum();
		frame.inR = inputs[R_INPUT].getVoltageSum();
		frame.inRPatched = inputs[R_INPUT].isConnected();
		frame.loop = params[LOOP_PARAM].getValue() > 0.5f;
		// A latch that differs from what the engine last said was pressed.
		bool* press[3] = {&frame.recPress, &frame.dubPress, &frame.playPress};
		for (int k = 0; k < 3; k++) {
			const bool v = params[REC_PARAM + k].getValue() > 0.5f;
			*press[k] = v != shown_[k];
			shown_[k] = v;
		}
		frame.restorePress = restoreButton_.process(params[RESTORE_PARAM].getValue() > 0.f);

		engine.process(frame, result);
		if (result.takes != takesSeen_) {
			takesSeen_ = result.takes;
			fileStale_.store(true, std::memory_order_relaxed);
		}

		// The engine decides: the latches show what it does.
		const bool state[3] = {result.recording, result.dubbing, result.playing};
		for (int k = 0; k < 3; k++)
			if (state[k] != shown_[k]) {
				params[REC_PARAM + k].setValue(state[k] ? 1.f : 0.f);
				shown_[k] = state[k];
			}
		outputs[L_OUTPUT].setVoltage(result.l);
		outputs[R_OUTPUT].setVoltage(result.r);
		outputs[EOC_OUTPUT].setVoltage(result.eoc ? 10.f : 0.f);
		lights[REC_LIGHT].setBrightness(result.recording ? 1.f : 0.f);
		lights[DUB_LIGHT].setBrightness(result.dubbing ? 1.f : 0.f);
		lights[PLAY_LIGHT].setBrightness(result.playing ? 1.f : 0.f);
		lights[LOOP_LIGHT].setBrightness(frame.loop ? 1.f : 0.f);

		if (--viewCountdown_ <= 0) {
			viewCountdown_ = VIEW_SAMPLES;
			viewHead_.store(result.head, std::memory_order_relaxed);
			viewStart_.store(result.winStart, std::memory_order_relaxed);
			viewLength_.store(result.winLength, std::memory_order_relaxed);
			viewOverlap_.store(result.overlap, std::memory_order_relaxed);
			viewSeconds_.store(result.seconds, std::memory_order_relaxed);
			viewEmpty_.store(result.empty, std::memory_order_relaxed);
			viewRecording_.store(result.recording, std::memory_order_relaxed);
		}

		// For the save: the live set, the counters and readsOriginal() of
		// this sample, published together (only when they change).
		TapeSet* live = engine.live();
		const uint32_t tc = engine.tapeChanges(), oc = engine.originalChanges();
		const bool behind = engine.readsOriginal();
		if (!pubAny_ || live != pubSet_ || tc != pubTape_ || oc != pubOriginal_ || behind != pubBehind_) {
			pubAny_ = true;
			pubSet_ = live;
			pubTape_ = tc;
			pubOriginal_ = oc;
			pubBehind_ = behind;
			const uint32_t q = snapSeq_.load(std::memory_order_relaxed);
			snapSeq_.store(q + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			snapSet_.store(live, std::memory_order_relaxed);
			snapTape_.store(tc, std::memory_order_relaxed);
			snapOriginal_.store(oc, std::memory_order_relaxed);
			snapBehind_.store(behind, std::memory_order_relaxed);
			snapSeq_.store(q + 2, std::memory_order_release);
		}
		// The stored load's set has just gone live: the load is over, and
		// if it was whole the files hold it as of these counters.
		if (live != seenLive_) {
			seenLive_ = live;
			TapeSet* expected = live;
			// Taken once (a later set at the same address is not it).
			if (live && storeSet_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
				if (storeWhole_.load(std::memory_order_relaxed) || storeCut_.load(std::memory_order_relaxed)) {
					baseTape_.store(tc);
					baseOriginal_.store(oc);
					baseFs_.store(live->fs);
					baseEpoch_.store(storeSetEpoch_.load());
					baseCut_.store(storeCut_.load(std::memory_order_relaxed));
					baseReady_.store(true, std::memory_order_release);
				}
				storeLoading_.store(false, std::memory_order_release);
			}
		}
	}

	// Bypassed: the inputs to the outputs, IN R taking IN L when unpatched.
	void processBypass(const ProcessArgs&) override {
		const float l = inputs[L_INPUT].getVoltageSum();
		outputs[L_OUTPUT].setVoltage(l);
		outputs[R_OUTPUT].setVoltage(inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() : l);
		outputs[EOC_OUTPUT].setVoltage(0.f);
	}

	// ---- The UI's requests.

	std::string filePath() {
		std::lock_guard<std::mutex> lk(fileLock_);
		if (fileStale_.exchange(false))
			file.clear();
		return file;
	}

	// Clear (the menu) and Initialize: after every job asked for before it.
	void clearTape() {
		std::lock_guard<std::mutex> lk(orderLock_);
		clearSeq_ = ++seq_;
		engine.clear();
	}

	uint32_t clearSeq() {
		std::lock_guard<std::mutex> lk(orderLock_);
		return clearSeq_;
	}

	void loadPath(const std::string& path) {
		post(Request::LOAD, path);
	}

	void exportPath(const std::string& path) {
		Job j;
		j.exportTape = true;
		j.path = path;
		{
			std::lock_guard<std::mutex> lk(queueLock_);
			queue_.push_back(j);
		}
		wake_.notify_one();
	}

	void makeOriginal() {
		engine.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	}

	// The patch's Play and Dub (engine: once no set is waiting).
	void applyTransport() {
		std::lock_guard<std::mutex> lk(orderLock_);
		engine.setTransport(transportPlay_.load(), transportDub_.load());
	}

	// The audio thread's last snapshot (a seqlock read).
	void readSnapshot(TapeSet*& set, uint32_t& tape, uint32_t& original, bool& behind) {
		for (;;) {
			const uint32_t q = snapSeq_.load(std::memory_order_acquire);
			if (q & 1u)
				continue;
			set = snapSet_.load(std::memory_order_relaxed);
			tape = snapTape_.load(std::memory_order_relaxed);
			original = snapOriginal_.load(std::memory_order_relaxed);
			behind = snapBehind_.load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (snapSeq_.load(std::memory_order_relaxed) == q)
				return;
		}
	}

	// For the window: the live set's overview copied into `into`, and the
	// numbers. Under viewLock_, so the worker cannot free the set meanwhile.
	void view(rka::TapeView& v, rka::Overview& into) {
		{
			std::lock_guard<std::mutex> lk(viewLock_);
			TapeSet* s = engine.live();
			for (int c = 0; c < rka::Overview::COLS; c++) {
				into.lo[c].store(s ? s->overview.lo[c].load(std::memory_order_relaxed) : 0.f, std::memory_order_relaxed);
				into.hi[c].store(s ? s->overview.hi[c].load(std::memory_order_relaxed) : 0.f, std::memory_order_relaxed);
			}
			v.empty = !s || viewEmpty_.load(std::memory_order_relaxed);
			v.tape = s;
		}
		v.overview = &into;
		v.head = viewHead_.load(std::memory_order_relaxed);
		v.start = viewStart_.load(std::memory_order_relaxed);
		v.length = viewLength_.load(std::memory_order_relaxed);
		v.overlap = viewOverlap_.load(std::memory_order_relaxed);
		v.seconds = viewSeconds_.load(std::memory_order_relaxed);
		v.recording = viewRecording_.load(std::memory_order_relaxed);
	}

	// ---- The worker.

	void post(Request::Kind kind, const std::string& path = "", bool transport = false) {
		Job j;
		j.req.kind = kind;
		j.req.path = path;
		j.req.transport = transport;
		{
			std::lock_guard<std::mutex> lk(orderLock_);
			j.req.seq = ++seq_;
		}
		j.req.epoch = storeEpoch_.load();
		{
			std::lock_guard<std::mutex> lk(queueLock_);
			queue_.push_back(j);
		}
		wake_.notify_one();
	}

	void startWorker() {
		if (worker_.joinable())
			return;
		{
			std::lock_guard<std::mutex> lk(queueLock_);
			stop_ = false;
		}
		worker_ = std::thread(&Eddy::run, this);
	}

	// Joins the worker (a job under way finishes first). Jobs not yet run
	// are dropped; the sets stay in sets_ for the destructor.
	void stopWorker() {
		{
			std::lock_guard<std::mutex> lk(queueLock_);
			stop_ = true;
			queue_.clear();
		}
		wake_.notify_all();
		if (worker_.joinable())
			worker_.join();
	}

	void run() {
		for (;;) {
			Job job;
			bool have = false;
			{
				std::unique_lock<std::mutex> lk(queueLock_);
				wake_.wait_for(lk, std::chrono::milliseconds(5), [&] { return stop_ || !queue_.empty(); });
				if (stop_)
					return;
				if (!queue_.empty()) {
					job = queue_.front();
					queue_.pop_front();
					have = true;
				}
			}
			service();
			if (have)
				runJob(job);
		}
	}

	// Between jobs: the copy the engine asks for, the sets it hands back,
	// the set waiting to be offered, and the frees.
	void service() {
		// The copy contract: the samples AND the length and tail, in the
		// direction wanted, on live(); then copyDone().
		const EddyEngine::Copy c = engine.copyWanted();
		if (c != EddyEngine::NONE) {
			TapeSet* s = engine.live();
			if (s) {
				if (c == EddyEngine::ORIGINAL_TO_TAPE)
					rka::eddy::jobs::copyTape(s->tape, s->original);
				else
					rka::eddy::jobs::copyTape(s->original, s->tape);
			}
			engine.copyDone();
		}
		while (TapeSet* s = engine.collect())
			handedBack(s);
		// The last offered set is live: a conversion's source can go.
		// (live() is read with acquire after the engine marked the set used.)
		TapeSet* live = engine.live();
		if (live && live == book_.lastOffered())
			book_.wentLive(live);
		offerPending();
		freeLimbo();
	}

	// A set the engine is done with (retired by a rate change, refused for
	// being at another rate than the engine's when it was taken, or replaced
	// by a newer one). WorkerBook says whether it is made again: loaded again
	// from its source (first in the queue) or converted now (a stale tape
	// copied from the original first, as the engine asks). A handed-back set
	// itself is never offered again.
	void handedBack(TapeSet* s) {
		if (s->tapeStale) {
			rka::eddy::jobs::copyTape(s->tape, s->original);
			s->tapeStale = false;
		}
		const WorkerBook::HandedBack h = book_.handedBack(s, s->used);
		if (h.storeOff)
			storeLoading_.store(false);
		if (h.remake == WorkerBook::HandedBack::RERUN) {
			INFO("Eddy: the rate is now %g Hz before the tape went live: making it again from its source", fs_.load());
			Job j;
			j.req = h.next;
			std::lock_guard<std::mutex> lk(queueLock_);
			queue_.push_front(j);
		}
		else if (h.remake == WorkerBook::HandedBack::CONVERT)
			produce(h.next, (const TapeSet*) h.from);
		limbo_.push_back(s);
	}

	void runJob(const Job& job) {
		if (job.exportTape) {
			// The accepted race, as at the save: the live tape read while
			// the engine may write it. The worker frees sets, so this one
			// stays.
			std::string why;
			TapeSet* s = engine.live();
			if (!s || !rka::eddy::exportTape(job.path, *s, s->fs, why))
				WARN("Eddy: cannot save the tape to %s: %s", job.path.c_str(), s ? why.c_str() : "no tape");
		}
		else
			produce(job.req, nullptr);
		service();
	}

	// Makes the set `r` asks for at the rate in force (from `from`, for a
	// conversion), and does what WorkerBook says with it: again at the new
	// rate if the rate moved meanwhile, an empty set in place of a re-make
	// that cannot be made, or wait to be offered.
	void produce(Request r, const TapeSet* from) {
		for (;;) {
			book_.started(r);
			const float fs = fs_.load();
			TapeSet* s = nullptr;
			bool whole = false, cut = false;
			make(r, from, fs, s, whole, cut);
			if (s)
				keep(s);
			const WorkerBook::Finished d = book_.finished(r, fs, fs_.load(), s, whole, cut, clearSeq(), usableLive(), from);
			if (d.storeOff)
				storeLoading_.store(false);
			if (d.storeFailed) {
				storeFailedEpoch_.store(r.epoch);
				storeFailed_.store(true);
			}
			if (d.limbo)
				limbo_.push_back((TapeSet*) d.limbo);
			if (d.setFile) {
				std::lock_guard<std::mutex> lk(fileLock_);
				fileStale_.store(false);
				file = r.path;
			}
			if (d.applyTransport)
				applyTransport();
			if (!d.rerun)
				break;
			if (d.next.kind == r.kind)
				INFO("Eddy: the rate changed to %g Hz while the tape was made: making it again", fs_.load());
			r = d.next;
			freeLimbo();
		}
		offerPending();
	}

	// One set for `r` at fs, or none (logged).
	void make(const Request& r, const TapeSet* from, float fs, TapeSet*& s, bool& whole, bool& cut) {
		std::string why;
		switch (r.kind) {
			case Request::FRESH:
				s = rka::eddy::jobs::newSet(fs, why);
				if (!s)
					WARN("Eddy: no tape at %g Hz: %s", fs, why.c_str());
				break;
			case Request::LOAD:
				if (rka::eddy::loadFile(r.path, fs, s, why, &cut)) {
					INFO("Eddy: loaded %s (%.2f s)%s", r.path.c_str(), s->tape.length / fs, cut ? "; longer than the tape: its start" : "");
				}
				else
					WARN("Eddy: cannot load %s: %s", r.path.c_str(), why.c_str());
				break;
			case Request::LOAD_STORED:
				if (rka::eddy::loadStored(r.path, fs, s, why, whole, &cut)) {
					INFO("Eddy: loaded the stored tape (%.2f s)%s", s->tape.length / fs,
					     whole ? "" : "; not as stored (converted or partial): the next save writes both files");
					// A partial or converted store loads what it can; it is
					// not "whole", so the next save writes both files.
					if (!why.empty())
						WARN("Eddy: the stored tape in %s: %s", r.path.c_str(), why.c_str());
					if (cut)
						WARN("Eddy: the stored tape is longer than the tape holds at %g Hz: cut to %.2f s; the stored files stay "
						     "as they are until the tape changes",
						     fs, s->tape.length / fs);
				}
				else
					WARN("Eddy: the stored tape in %s: %s; the tape is empty", r.path.c_str(), why.c_str());
				break;
			case Request::CONVERT:
				if (from && rka::eddy::convert(*from, from->fs, fs, s, why, &cut)) {
					if (cut)
						WARN("Eddy: the tape is longer than it can be at %g Hz: cut to %.2f s", fs, s->tape.length / fs);
				}
				else
					WARN("Eddy: cannot convert the tape to %g Hz: %s; the tape is empty", fs, from ? why.c_str() : "no tape");
				break;
		}
	}

	void keep(TapeSet* s) {
		sets_.push_back(s);
	}

	// The live set if it is at the rate in force (a set's rate is fixed when
	// it is made), else null.
	TapeSet* usableLive() {
		TapeSet* live = engine.live();
		return live && live->fs == fs_.load() ? live : nullptr;
	}

	// The set waiting, offered as WorkerBook says: dropped if a Clear came
	// after it was asked for and there is a usable live set; emptied if one
	// came and there is none; published for the save if it is the stored
	// load's.
	void offerPending() {
		if (!book_.pending())
			return;
		std::lock_guard<std::mutex> lk(orderLock_);
		const WorkerBook::Offer o = book_.offer(usableLive(), clearSeq_);
		TapeSet* s = (TapeSet*) o.set;
		if (o.storeOff)
			storeLoading_.store(false);
		if (o.drop) {
			limbo_.push_back(s);
			return;
		}
		if (o.empty) {
			s->tape.length = s->tape.tail = 0;
			s->original.length = s->original.tail = 0;
			s->tapeStale = false;
			s->overview.clear();
		}
		if (o.watch) {
			storeWhole_.store(o.whole, std::memory_order_relaxed);
			storeCut_.store(o.cut, std::memory_order_relaxed);
			storeSetEpoch_.store(o.epoch, std::memory_order_relaxed);
			storeSet_.store(s, std::memory_order_release);
		}
		if (!engine.offer(s))
			return;   // one is still waiting: again next time
		if (book_.offered())
			engine.setTransport(transportPlay_.load(), transportDub_.load());
	}

	// Frees the sets nobody can hold: not live, and no UI thread inside
	// viewLock_ since that was so (see the top of this file).
	void freeLimbo() {
		if (limbo_.empty())
			return;
		TapeSet* live = engine.live();
		std::vector<TapeSet*> gone;
		for (TapeSet* s : limbo_)
			if (s != live && s != book_.pending() && !book_.needed(s) && std::find(gone.begin(), gone.end(), s) == gone.end())
				gone.push_back(s);
		if (gone.empty())
			return;
		{
			std::lock_guard<std::mutex> lk(viewLock_);
		}
		for (TapeSet* s : gone) {
			TapeSet* expected = s;
			storeSet_.compare_exchange_strong(expected, nullptr);
			if (book_.freed(s))
				storeLoading_.store(false);
			limbo_.erase(std::remove(limbo_.begin(), limbo_.end(), s), limbo_.end());
			sets_.erase(std::find(sets_.begin(), sets_.end(), s));
			delete s;
		}
	}
};


struct EddyWidget : ModuleWidget {
	// With no module (the browser, screenshots): a tape of our own if
	// RKA_EDDY_PREVIEW_WAV names a WAV (make smoke makes one), else empty.
	std::unique_ptr<rka::TapeSet> preview;
	rka::Overview shown;   // the copy the window draws
	// START and LENGTH as they were when the mouse began a drag in the window.
	rka::eddy::WindowKnobs selectBefore;

	// The mouse in the window sets START and LENGTH through their params, as
	// the knobs do (eddy/Select.hpp); one undo step per gesture.
	void selectBegin(Eddy* m) {
		selectBefore.start = m->params[Eddy::START_PARAM].getValue();
		selectBefore.length = m->params[Eddy::LENGTH_PARAM].getValue();
	}
	void select(Eddy* m, rka::TapeGrab grab, float press, float now) {
		const float cv = m->inputs[Eddy::START_CV_INPUT].getVoltage() / rka::eddy::START_CV_VOLTS;
		const double seconds = m->viewSeconds_.load(std::memory_order_relaxed);
		const rka::eddy::WindowKnobs k = rka::eddy::dragWindow(grab, press, now, selectBefore, cv, seconds);
		m->paramQuantities[Eddy::START_PARAM]->setValue(k.start);
		m->paramQuantities[Eddy::LENGTH_PARAM]->setValue(k.length);
	}
	void selectEnd(Eddy* m) {
		history::ComplexAction* h = new history::ComplexAction;
		h->name = "set Eddy's window";
		const int ids[2] = {Eddy::START_PARAM, Eddy::LENGTH_PARAM};
		const float was[2] = {selectBefore.start, selectBefore.length};
		for (int i = 0; i < 2; i++) {
			const float now = m->params[ids[i]].getValue();
			if (now == was[i])
				continue;
			history::ParamChange* c = new history::ParamChange;
			c->name = h->name;
			c->moduleId = m->id;
			c->paramId = ids[i];
			c->oldValue = was[i];
			c->newValue = now;
			h->push(c);
		}
		if (h->isEmpty())
			delete h;
		else
			APP->history->push(h);
	}
	// Double-click: the whole tape.
	void selectWhole(Eddy* m) {
		selectBegin(m);
		m->paramQuantities[Eddy::START_PARAM]->setValue(0.f);
		m->paramQuantities[Eddy::LENGTH_PARAM]->setValue(1.f);
		selectEnd(m);
	}

	EddyWidget(Eddy* module) {
		using namespace EddyLayout;
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/eddy/Eddy.svg"), asset::plugin(pluginInstance, "res/eddy/Eddy-dark.svg")));

		if (!module) {
			const char* path = std::getenv("RKA_EDDY_PREVIEW_WAV");
			rka::TapeSet* s = nullptr;
			std::string why;
			if (path && *path && rka::eddy::loadFile(path, 48000.f, s, why))
				preview.reset(s);
			else if (path && *path)
				WARN("Eddy: preview %s: %s", path, why.c_str());
		}

		for (const Vec& screw : SCREWS)
			addChild(createWidgetCentered<rka::Screw>(mm2px(screw)));

		rka::TapeWindow* window = createWidget<rka::TapeWindow>(mm2px(SCREEN_TAPE.pos));
		window->box.size = mm2px(SCREEN_TAPE.size);
		window->source = [=](rka::TapeView& v) {
			if (module) {
				module->view(v, shown);
				return true;
			}
			if (!preview) {
				v.empty = true;
				return true;
			}
			// As the panel opens in the browser: a window from a fifth of the
			// way, the head inside it.
			for (int c = 0; c < rka::Overview::COLS; c++) {
				shown.lo[c].store(preview->overview.lo[c].load());
				shown.hi[c].store(preview->overview.hi[c].load());
			}
			v.overview = &shown;
			v.empty = false;
			v.start = 0.22f;
			v.length = 0.42f;
			v.head = 0.37f;
			v.overlap = 0.04f;
			v.seconds = (float) preview->tape.length / preview->fs;
			return true;
		};
		if (module) {
			window->onSelectBegin = [=]() { selectBegin(module); };
			window->onSelect = [=](rka::TapeGrab grab, float press, float now) { select(module, grab, press, now); };
			window->onSelectEnd = [=]() { selectEnd(module); };
			window->onWhole = [=]() { selectWhole(module); };
		}
		addChild(window);

		addParam(createParamCentered<rka::HeroKnob>(mm2px(PARAM_SPEED), module, Eddy::SPEED_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_SPEED_ATT), module, Eddy::SPEED_ATT_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_START), module, Eddy::START_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_LENGTH), module, Eddy::LENGTH_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_WEAR), module, Eddy::WEAR_PARAM));
		addParam(createParamCentered<rka::LimbKnob>(mm2px(PARAM_SMEAR), module, Eddy::SMEAR_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_DUB_LEVEL), module, Eddy::DUB_LEVEL_PARAM));
		addParam(createParamCentered<rka::Atten>(mm2px(PARAM_MIX), module, Eddy::MIX_PARAM));
		// The caps light as the engine runs (Rec, Dub, Play) and as Loop is set.
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_REC), module, Eddy::REC_PARAM, Eddy::REC_LIGHT));
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_DUB), module, Eddy::DUB_PARAM, Eddy::DUB_LIGHT));
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_PLAY), module, Eddy::PLAY_PARAM, Eddy::PLAY_LIGHT));
		addParam(createLightParamCentered<rka::CapLatch>(mm2px(PARAM_LOOP), module, Eddy::LOOP_PARAM, Eddy::LOOP_LIGHT));
		addParam(createParamCentered<rka::CapButton>(mm2px(PARAM_RESTORE), module, Eddy::RESTORE_PARAM));

		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_START_CV), module, Eddy::START_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_VOCT), module, Eddy::VOCT_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_SPEED_CV), module, Eddy::SPEED_CV_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_L), module, Eddy::L_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_R), module, Eddy::R_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_REC), module, Eddy::REC_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_DUB), module, Eddy::DUB_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_PLAY), module, Eddy::PLAY_INPUT));
		addInput(createInputCentered<rka::BrassPort>(mm2px(INPUT_RESTORE), module, Eddy::RESTORE_INPUT));

		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_EOC), module, Eddy::EOC_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_L), module, Eddy::L_OUTPUT));
		addOutput(createOutputCentered<rka::BrassPort>(mm2px(OUTPUT_R), module, Eddy::R_OUTPUT));
	}

	// A WAV dropped on the panel: the first path ending in .wav.
	void onPathDrop(const PathDropEvent& e) override {
		Eddy* m = getModule<Eddy>();
		if (!m)
			return;
		for (const std::string& p : e.paths)
			if (string::lowercase(system::getExtension(p)) == ".wav") {
				m->loadPath(p);
				e.consume(this);
				return;
			}
	}

	void appendContextMenu(Menu* menu) override {
		Eddy* m = getModule<Eddy>();
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load file", "", [=]() {
			const std::string last = m->filePath();
			const std::string dir = last.empty() ? "" : system::getDirectory(last);
			osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
			char* path = osdialog_file(OSDIALOG_OPEN, dir.empty() ? NULL : dir.c_str(), NULL, filters);
			osdialog_filters_free(filters);
			if (path) {
				m->loadPath(path);
				std::free(path);
			}
		}));
		menu->addChild(createMenuItem("Save file", "", [=]() {
			const std::string last = m->filePath();
			const std::string dir = last.empty() ? "" : system::getDirectory(last);
			osdialog_filters* filters = osdialog_filters_parse("WAV:wav,WAV");
			char* path = osdialog_file(OSDIALOG_SAVE, dir.empty() ? NULL : dir.c_str(), "Eddy.wav", filters);
			osdialog_filters_free(filters);
			if (path) {
				std::string p = path;
				std::free(path);
				if (string::lowercase(system::getExtension(p)) != ".wav")
					p += ".wav";
				m->exportPath(p);
			}
		}));
		menu->addChild(createMenuItem("Make this the original", "", [=]() { m->makeOriginal(); }));
		menu->addChild(createMenuItem("Clear", "", [=]() { m->clearTape(); }));
		menu->addChild(createBoolPtrMenuItem("Keep the tape with the patch", "", &m->keepTape));
	}
};


Model* modelEddy = createModel<Eddy, EddyWidget>("Eddy");

// Eddy: a tape loop that also plays files (spec:
// docs/specs/2026-11-21-eddy-design.md). The module maps params, ports and
// the patch's state onto rka::EddyEngine, and owns the one worker thread
// that makes, loads, converts, copies, saves and frees tapes (the jobs are
// in TapeJobs.hpp).
//
// Threads, and which calls which of the engine's members:
// - The audio thread: process(); live() and the change counters, to note
//   when the patch's stored tape goes live (the save's baseline).
// - The worker: offer(), setTransport() for a patch's tape, collect(),
//   copyWanted() and copyDone() (the copies), live() (copies, export).
// - The UI (menu, onSave, dataFromJson, onReset, the window): clear() and
//   setTransport() under orderLock_, the lock the worker takes around
//   offer(), so the last of a Clear and an offer wins in the order they
//   were asked for; requestCopy() (the UI's alone); live() under viewLock_.
// - setSampleRate() from onSampleRateChange (an atomic store).
//
// The last request wins (Review Focus 4). Each job is stamped with the
// generation at the time it was asked for; a Clear or Initialize moves the
// generation on, and a set made by a job from before it is emptied before
// it is offered, so a load or a conversion that finishes after a Clear
// cannot bring the old tape back. Two loads in a row run in order and the
// second wins; a set waiting to be offered is replaced by a newer one.
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
#include "TapeJobs.hpp"
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

	// The save's baseline: the change counters when the stored files were
	// last written (or the stored tape went live). savedValid_ false: write
	// both at the next save.
	std::atomic<uint32_t> savedTape_{0}, savedOriginal_{0};
	std::atomic<bool> savedValid_{false};
	std::atomic<TapeSet*> storedSet_{nullptr};   // the set loaded whole from the patch's storage
	TapeSet* seenLive_ = nullptr;                // audio thread

	// ---- The worker.
	struct Job {
		enum Kind { FRESH, LOAD, LOAD_STORED, EXPORT } kind = FRESH;
		std::string path;
		uint32_t gen = 0;
		bool transport = false;   // apply the patch's Play and Dub once its tape is offered
	};
	struct Pending {
		TapeSet* set = nullptr;
		uint32_t gen = 0;
		bool transport = false, stored = false;
	};
	std::thread worker_;
	std::mutex queueLock_;
	std::condition_variable wake_;
	std::deque<Job> queue_;   // under queueLock_
	bool stop_ = false;       // under queueLock_
	std::mutex orderLock_;    // offer() against clear() and setTransport(), and gen_
	uint32_t gen_ = 0;        // under orderLock_
	std::mutex viewLock_;     // held by a UI thread while it uses a set read through live()
	std::atomic<float> fs_{48000.f};
	std::atomic<bool> transportPlay_{false}, transportDub_{false};   // the patch's Play and Dub
	bool added_ = false;   // between onAdd and onRemove (UI)
	// The worker's alone (and the destructor's, after the join).
	std::vector<TapeSet*> sets_;    // every set made and not yet freed
	std::vector<TapeSet*> limbo_;   // handed back or superseded, to be freed
	TapeSet* lastOffered_ = nullptr;
	uint32_t lastGen_ = 0;
	Pending pending_;

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
		configParam(MIX_PARAM, 0.f, 1.f, defaults.params[F::MIX], "Mix (input to tape)", "%", 0.f, 100.f);
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
		post(Job::FRESH);
		const std::string dir = getPatchStorageDirectory();
		const std::string path = filePath();
		if (keepTape && !dir.empty() && system::isDirectory(dir))
			post(Job::LOAD_STORED, dir, true);
		else if (!keepTape && !path.empty())
			post(Job::LOAD, path, true);
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
	// folder as soon as this returns. Each file only if its tape has
	// changed since it was last written; nothing (the folder removed) for
	// an empty tape or with "Keep the tape with the patch" off.
	void onSave(const SaveEvent& e) override {
		const std::string dir = getPatchStorageDirectory();
		if (!keepTape) {
			removeStored(dir);
			return;
		}
		std::lock_guard<std::mutex> view(viewLock_);
		TapeSet* s = engine.live();
		if (!s)
			return;   // between rates (or none yet): what is stored stays
		const uint32_t t = engine.tapeChanges(), o = engine.originalChanges();
		if (s->tape.length == 0 || s->original.length == 0) {
			removeStored(dir);
			return;
		}
		const bool valid = savedValid_.load();
		const bool wantTape = !valid || t != savedTape_.load(), wantOriginal = !valid || o != savedOriginal_.load();
		if (!wantTape && !wantOriginal)
			return;
		createPatchStorageDirectory();
		// THE ACCEPTED RACE (TESTING.md, rule 7): the live tape is read here
		// while the audio thread may write it (Wear, Dub) and the worker may
		// copy onto it (Restore). A torn read is one sample of wear either
		// side; the counters moved, so the next save writes it again.
		std::string why;
		const double t0 = system::getTime();
		if (rka::eddy::storeTape(dir, *s, s->fs, wantTape, wantOriginal, why)) {
			savedTape_.store(t);
			savedOriginal_.store(o);
			savedValid_.store(true);
			INFO("Eddy: stored the tape (%s%s) in %.0f ms", wantTape ? "tape" : "", wantOriginal ? (wantTape ? ", original" : "original") : "",
			     (system::getTime() - t0) * 1000.0);
		}
		else {
			savedValid_.store(false);
			WARN("Eddy: cannot store the tape in %s: %s", dir.c_str(), why.c_str());
		}
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
		{
			std::lock_guard<std::mutex> lk(fileLock_);
			file = f && json_is_string(f) ? json_string_value(f) : "";
		}
		json_t* k = json_object_get(root, "keepTape");
		keepTape = k && json_is_boolean(k) ? json_boolean_value(k) : true;
		params[REC_PARAM].setValue(0.f);
		transportPlay_.store(params[PLAY_PARAM].getValue() > 0.5f);
		transportDub_.store(params[DUB_PARAM].getValue() > 0.5f);
		shown_[0] = false;
		shown_[1] = params[DUB_PARAM].getValue() > 0.5f;
		shown_[2] = params[PLAY_PARAM].getValue() > 0.5f;
		// A preset onto a module already running: the file, if the tape
		// is not kept; otherwise the tape stays.
		if (added_) {
			const std::string path = filePath();
			if (!keepTape && !path.empty())
				post(Job::LOAD, path, true);
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

		// The stored tape has just gone live: the files hold it, so the
		// save's baseline is the counters now.
		TapeSet* live = engine.live();
		if (live != seenLive_) {
			seenLive_ = live;
			if (live && live == storedSet_.load(std::memory_order_acquire)) {
				savedTape_.store(engine.tapeChanges());
				savedOriginal_.store(engine.originalChanges());
				savedValid_.store(true);
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
		return file;
	}

	// Clear (the menu) and Initialize: after every job asked for before it.
	void clearTape() {
		std::lock_guard<std::mutex> lk(orderLock_);
		gen_++;
		engine.clear();
	}

	void loadPath(const std::string& path) {
		post(Job::LOAD, path);
	}

	void exportPath(const std::string& path) {
		post(Job::EXPORT, path);
	}

	void makeOriginal() {
		engine.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	}

	// The patch's Play and Dub (engine: once no set is waiting).
	void applyTransport() {
		std::lock_guard<std::mutex> lk(orderLock_);
		engine.setTransport(transportPlay_.load(), transportDub_.load());
	}

	void removeStored(const std::string& dir) {
		if (!dir.empty() && system::exists(dir))
			system::removeRecursively(dir);
		savedValid_.store(false);
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

	void post(Job::Kind kind, const std::string& path = "", bool transport = false) {
		Job j;
		j.kind = kind;
		j.path = path;
		j.transport = transport;
		{
			std::lock_guard<std::mutex> lk(orderLock_);
			j.gen = gen_;
		}
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
		offerPending();
		freeLimbo();
	}

	// A set the engine is done with. The last one offered comes back only
	// for its rate (retired by a rate change, or refused for being at
	// another rate than the engine's when it was taken): it is the tape the
	// user has, so it is converted to the rate now (a stale tape copied from
	// the original first, as the engine asks; the same rate is a copy) and
	// the new set offered in its place, with the generation the old one was
	// offered with. A handed-back set itself is never offered again.
	void handedBack(TapeSet* s) {
		if (s->tapeStale) {
			rka::eddy::jobs::copyTape(s->tape, s->original);
			s->tapeStale = false;
		}
		if (s == lastOffered_) {
			lastOffered_ = nullptr;
			const float fs = fs_.load();
			TapeSet* n = nullptr;
			std::string why;
			if (rka::eddy::convert(*s, s->fs, fs, n, why))
				keep(n);
			else {
				WARN("Eddy: cannot convert the tape to %g Hz: %s; the tape is empty", fs, why.c_str());
				n = rka::eddy::jobs::newSet(fs, why);
				if (n)
					keep(n);
			}
			if (n)
				setPending(n, lastGen_, false, false);
		}
		limbo_.push_back(s);
	}

	void runJob(const Job& job) {
		const float fs = fs_.load();
		std::string why;
		switch (job.kind) {
			case Job::FRESH: {
				TapeSet* s = rka::eddy::jobs::newSet(fs, why);
				if (s) {
					keep(s);
					setPending(s, job.gen, job.transport, false);
				}
				else
					WARN("Eddy: no tape at %g Hz: %s", fs, why.c_str());
				break;
			}
			case Job::LOAD: {
				TapeSet* s = nullptr;
				if (rka::eddy::loadFile(job.path, fs, s, why)) {
					INFO("Eddy: loaded %s (%.2f s)", job.path.c_str(), s->tape.length / fs);
					keep(s);
					setPending(s, job.gen, job.transport, false);
					std::lock_guard<std::mutex> lk(fileLock_);
					file = job.path;
				}
				else {
					WARN("Eddy: cannot load %s: %s", job.path.c_str(), why.c_str());
					if (job.transport)
						applyTransport();
				}
				break;
			}
			case Job::LOAD_STORED: {
				TapeSet* s = nullptr;
				if (rka::eddy::loadStored(job.path, fs, s, why)) {
					INFO("Eddy: loaded the stored tape (%.2f s)", s->tape.length / fs);
					keep(s);
					// A partial store loads what it can; the next save
					// writes it whole.
					if (!why.empty())
						WARN("Eddy: the stored tape in %s: %s", job.path.c_str(), why.c_str());
					setPending(s, job.gen, job.transport, why.empty());
				}
				else {
					WARN("Eddy: the stored tape in %s: %s; the tape is empty", job.path.c_str(), why.c_str());
					if (job.transport)
						applyTransport();
				}
				break;
			}
			case Job::EXPORT: {
				// The accepted race, as at the save: the live tape read
				// while the engine may write it. The worker frees sets, so
				// this one stays.
				TapeSet* s = engine.live();
				if (!s || !rka::eddy::exportTape(job.path, *s, s->fs, why))
					WARN("Eddy: cannot save the tape to %s: %s", job.path.c_str(), s ? why.c_str() : "no tape");
				break;
			}
		}
		service();
	}

	void keep(TapeSet* s) {
		sets_.push_back(s);
	}

	// The next set to offer; one already waiting is superseded.
	void setPending(TapeSet* s, uint32_t gen, bool transport, bool stored) {
		if (pending_.set) {
			transport = transport || pending_.transport;
			limbo_.push_back(pending_.set);
		}
		pending_.set = s;
		pending_.gen = gen;
		pending_.transport = transport;
		pending_.stored = stored;
		offerPending();
	}

	void offerPending() {
		if (!pending_.set)
			return;
		std::lock_guard<std::mutex> lk(orderLock_);
		if (pending_.gen != gen_) {
			// A Clear came after this was asked for: the tape is empty, and
			// the patch's Play and Dub no longer apply.
			TapeSet* s = pending_.set;
			s->tape.length = s->tape.tail = 0;
			s->original.length = s->original.tail = 0;
			s->tapeStale = false;
			s->overview.clear();
			pending_.gen = gen_;
			pending_.stored = false;
			pending_.transport = false;
		}
		storedSet_.store(pending_.stored ? pending_.set : nullptr, std::memory_order_release);
		if (!engine.offer(pending_.set))
			return;   // one is still waiting: again next time
		lastOffered_ = pending_.set;
		lastGen_ = pending_.gen;
		if (pending_.transport)
			engine.setTransport(transportPlay_.load(), transportDub_.load());
		pending_ = Pending();
	}

	// Frees the sets nobody can hold: not live, and no UI thread inside
	// viewLock_ since that was so (see the top of this file).
	void freeLimbo() {
		if (limbo_.empty())
			return;
		TapeSet* live = engine.live();
		std::vector<TapeSet*> gone;
		for (TapeSet* s : limbo_)
			if (s != live && s != pending_.set)
				gone.push_back(s);
		if (gone.empty())
			return;
		{
			std::lock_guard<std::mutex> lk(viewLock_);
		}
		for (TapeSet* s : gone) {
			limbo_.erase(std::find(limbo_.begin(), limbo_.end(), s));
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

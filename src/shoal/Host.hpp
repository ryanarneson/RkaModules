#pragma once
// Shoal host: one shoal::Core driven from an audio engine, with the screens,
// the app and a saved state image published for one other thread (the UI).
// Header-only, Rack-free, C++11; includes no firmware header.
//
// Threading: the engine-thread members (setSampleRate .. gateHigh) are the
// only ones that touch the running Core, and never block, allocate or boot
// firmware: they publish with try_lock and retry at the next tick. The
// UI-thread members (post .. appMenuOpen) may be called from any one other
// thread; they lock briefly and never touch the running Core. A restore or
// reset builds and boots a replacement Core on the UI thread; the engine
// swaps it in before its next tick, and the Core it retires is deleted on
// the UI thread later. Control events travel through a fixed single-producer
// single-consumer ring. Events posted before a restore request are applied
// after it (on the replacement Core).
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "Core.hpp"
#include "shim/Volts.hpp"

namespace shoal {

// A control change from the UI thread, applied before the engine's next tick.
struct ControlEvent {
	// Reset never reaches the engine: post() turns it into requestReset().
	enum Type : uint8_t { Turn, SelectApp, Reset, SetInputRange, SetAcceleration };
	Type type;
	int8_t index;    // Turn: Encoder
	int16_t value;   // Turn: detents; SelectApp: app; SetInputRange: InputRange; SetAcceleration: 0/1
};

class Host {
public:
	static constexpr int kFrameTicks = 278;   // frames published at 60 Hz
	static constexpr int kSaveTicks = 8333;   // state saved and published at 2 Hz
	static constexpr uint32_t kEventSlots = 64;

	// Seeds the core and publishes its frames and its (new core's) state
	// image, so the UI has both before the engine first runs.
	explicit Host(uint32_t seed) : seed_(seed), core_(new Core()) {
		core_->seed(seed_);
		core_->save(defaults_);
		std::memcpy(state_, defaults_, kStateBytes);
		drawFrames();
		std::memcpy(menu_, engineMenu_, kFrameBytes);
		std::memcpy(scope_, engineScope_, kFrameBytes);
		app_.store(engineApp_);
		appMenuOpen_.store(engineAppMenuOpen_);
	}
	~Host() {
		delete core_;
		delete pending_;
		delete retired_;
	}
	Host(const Host&) = delete;
	Host& operator=(const Host&) = delete;

	// --- Engine thread only. ---

	void setSampleRate(double hz) { clock_.setSampleRate(hz); }
	void setCv(int ch, float volts) {
		if (ch < 0 || ch >= kNumChannels)
			return;
		cv_[ch] = volts;
		core_->setCv(ch, volts);
	}
	void setGate(int ch, float volts) {
		if (ch < 0 || ch >= kNumChannels)
			return;
		gates_[ch].process(std::isnan(volts) ? 0.f : volts);
		core_->setGate(ch, volts);
	}
	// Call every sample or on change; only changes reach the core.
	void setButton(Button b, bool down) {
		if (b > BUTTON_R || buttons_[b] == down)
			return;
		buttons_[b] = down;
		core_->setButton(b, down);
	}
	// One engine sample: the ticks due, after a pending restore and then the
	// queued control events. If a restore is pending but the UI holds the
	// lock, the events wait too, so the restore cannot wipe them next time.
	void process() {
		const int n = clock_.advance();
		if (n <= 0)
			return;
		if (applyRestore())
			applyEvents();
		for (int i = 0; i < n; i++) {
			core_->tick();
			afterTick();
		}
	}
	float out(int ch) const { return core_->out(ch); }
	// The gate input's level (Schmitt trigger at 0.1 V / 1 V).
	bool gateHigh(int ch) const { return ch >= 0 && ch < kNumChannels && gates_[ch].high; }

	// --- Any one other thread (the UI thread). None of these touch the running Core. ---

	// False: the queue is full and the event was dropped. A Reset event is
	// requestReset() and always succeeds.
	bool post(ControlEvent e) {
		if (e.type == ControlEvent::Reset) {
			requestReset();
			return true;
		}
		const uint32_t tail = eventTail_.load(std::memory_order_relaxed);
		if (tail - eventHead_.load(std::memory_order_acquire) >= kEventSlots)
			return false;
		events_[tail % kEventSlots] = e;
		eventTail_.store(tail + 1, std::memory_order_release);
		return true;
	}
	// The latest published frames; kFrameBytes each.
	void frames(uint8_t* menu, uint8_t* scope) {
		std::lock_guard<std::mutex> lock(frameMutex_);
		std::memcpy(menu, menu_, kFrameBytes);
		std::memcpy(scope, scope_, kFrameBytes);
	}
	// The latest saved image (kStateBytes) and its input range. Also deletes
	// a Core the engine has retired.
	void snapshot(uint8_t* state, InputRange* range) {
		Core* retired;
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			std::memcpy(state, state_, kStateBytes);
			if (range)
				*range = range_;
			retired = retired_;
			retired_ = nullptr;
		}
		delete retired;
	}
	// Restore `state` (null: a new core's defaults) with input range `range`
	// before the engine's next tick. Builds and boots the replacement Core
	// here, on the calling thread. The image is the snapshot at once; an
	// image the firmware rejects boots a new core's defaults, and the
	// defaults image is the snapshot at once instead.
	void requestRestore(const uint8_t* state, InputRange range) {
		Core* c = new Core();
		c->seed(seed_);
		c->setInputRange(range);
		const bool restored = state && c->restore(state);
		install(c, restored ? state : defaults_, false, range);
	}
	// A new core's defaults, keeping the engine's input range: as
	// requestRestore(nullptr, <current range>).
	void requestReset() {
		Core* c = new Core();
		c->seed(seed_);
		install(c, defaults_, true, InputRange::Hardware);
	}
	// As of the last frame publish.
	int app() const { return app_.load(); }
	bool appMenuOpen() const { return appMenuOpen_.load(); }

private:
	const uint32_t seed_;
	Core* core_;  // the running Core; engine thread only
	TickClock clock_;
	Schmitt gates_[kNumChannels];
	float cv_[kNumChannels] = {};
	bool buttons_[4] = {};
	InputRange engineRange_ = InputRange::Hardware;
	bool saveEnabled_ = true;  // test hook (HostTest)

	// Engine-owned: drawn or saved, waiting to be published.
	int frameCountdown_ = kFrameTicks, saveCountdown_ = kSaveTicks;
	bool framesDue_ = false, stateDue_ = false;
	uint8_t engineMenu_[kFrameBytes] = {}, engineScope_[kFrameBytes] = {};
	int engineApp_ = 0;
	bool engineAppMenuOpen_ = false;
	uint8_t engineState_[kStateBytes] = {};
	uint8_t defaults_[kStateBytes] = {};  // a new core's image; constant after construction

	// Published frames.
	std::mutex frameMutex_;
	uint8_t menu_[kFrameBytes] = {}, scope_[kFrameBytes] = {};
	std::atomic<int> app_{0};
	std::atomic<bool> appMenuOpen_{false};

	// Published state, the pending replacement Core and the retired one,
	// all under stateMutex_.
	std::mutex stateMutex_;
	uint8_t state_[kStateBytes] = {};
	InputRange range_ = InputRange::Hardware;
	Core* pending_ = nullptr;
	Core* retired_ = nullptr;
	bool pendingKeepsRange_ = false;
	InputRange pendingRange_ = InputRange::Hardware;
	std::atomic<bool> restorePending_{false};  // written under stateMutex_

	// Control events: UI thread writes the tail, engine thread the head.
	ControlEvent events_[kEventSlots] = {};
	std::atomic<uint32_t> eventHead_{0}, eventTail_{0};

	friend struct HostTest;  // test-only hooks (test_host.cpp)

	// UI thread: make booted Core c the pending replacement and `image` the
	// snapshot; delete, outside the lock, a replacement never picked up and
	// a Core the engine retired.
	void install(Core* c, const uint8_t* image, bool keepsRange, InputRange range) {
		Core* unused;
		Core* retired;
		{
			std::lock_guard<std::mutex> lock(stateMutex_);
			unused = pending_;
			retired = retired_;
			retired_ = nullptr;
			pending_ = c;
			pendingKeepsRange_ = keepsRange;
			pendingRange_ = range;
			std::memcpy(state_, image, kStateBytes);
			if (!keepsRange)
				range_ = range;
			restorePending_.store(true, std::memory_order_release);
		}
		delete unused;
		delete retired;
	}

	void drawFrames() {
		core_->drawMenu(engineMenu_);
		core_->drawScope(engineScope_);
		engineApp_ = core_->app();
		engineAppMenuOpen_ = core_->appMenuOpen();
	}

	// Swap in a pending replacement Core. False when one is pending but the
	// UI holds the lock (try again at the next sample).
	bool applyRestore() {
		if (!restorePending_.load(std::memory_order_acquire))
			return true;
		std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
		// retired_ is always empty here (install() empties it before it sets
		// pending_); never overwrite it, as that would leak.
		if (!lock.owns_lock() || retired_)
			return false;
		retired_ = core_;
		core_ = pending_;
		pending_ = nullptr;
		if (!pendingKeepsRange_)
			engineRange_ = pendingRange_;
		restorePending_.store(false, std::memory_order_relaxed);
		lock.unlock();
		// The replacement is a new Core (requestRestore, requestReset), not
		// the running one rebooted, so only what the host re-applies here
		// carries over: the input range, the CV levels and the held buttons
		// (a held button is pressed again on the new Core). Everything else
		// starts as on a new Core: the random sources restart from the
		// Host's original seed, the clocks (millis, ticks) at 0, the gates
		// low (the next setGate brings them back; a gate held high counts as
		// one rising edge), and encoder acceleration is what the restored
		// image's global settings hold (on for the defaults), as the app's
		// resume then leaves it (Automatonnetz turns its L encoder's off).
		core_->setInputRange(engineRange_);
		for (int ch = 0; ch < kNumChannels; ch++)
			core_->setCv(ch, cv_[ch]);
		for (int b = BUTTON_UP; b <= BUTTON_R; b++)
			if (buttons_[b])
				core_->setButton((Button) b, true);
		stateDue_ = false;  // an unpublished save predates the restore
		return true;
	}

	void applyEvents() {
		uint32_t head = eventHead_.load(std::memory_order_relaxed);
		const uint32_t tail = eventTail_.load(std::memory_order_acquire);
		for (; head != tail; head++)
			apply(events_[head % kEventSlots]);
		eventHead_.store(head, std::memory_order_release);
	}

	void apply(const ControlEvent& e) {
		switch (e.type) {
			case ControlEvent::Turn:
				if (e.index >= ENCODER_L && e.index <= ENCODER_R)
					core_->turnEncoder((Encoder) e.index, e.value);
				break;
			case ControlEvent::SelectApp:
				core_->selectApp(e.value);
				break;
			case ControlEvent::Reset:
				break;  // never queued (post)
			case ControlEvent::SetInputRange:
				engineRange_ = e.value == (int16_t) InputRange::Bipolar5 ? InputRange::Bipolar5 : InputRange::Hardware;
				core_->setInputRange(engineRange_);
				break;
			case ControlEvent::SetAcceleration:
				core_->setEncoderAcceleration(e.value != 0);
				break;
		}
	}

	// Draw and save when due; publish what is due unless the UI holds the
	// lock, in which case the next tick tries again.
	void afterTick() {
		if (--frameCountdown_ <= 0) {
			frameCountdown_ = kFrameTicks;
			drawFrames();
			framesDue_ = true;
		}
		if (framesDue_ && frameMutex_.try_lock()) {
			std::memcpy(menu_, engineMenu_, kFrameBytes);
			std::memcpy(scope_, engineScope_, kFrameBytes);
			frameMutex_.unlock();
			app_.store(engineApp_);
			appMenuOpen_.store(engineAppMenuOpen_);
			framesDue_ = false;
		}
		if (--saveCountdown_ <= 0) {
			saveCountdown_ = kSaveTicks;
			if (saveEnabled_) {
				core_->save(engineState_);
				stateDue_ = true;
			}
		}
		if (stateDue_ && stateMutex_.try_lock()) {
			// A restore requested since the save makes the save stale: the
			// requested image stays the snapshot until the restore runs.
			if (!restorePending_.load(std::memory_order_relaxed)) {
				std::memcpy(state_, engineState_, kStateBytes);
				range_ = engineRange_;
			}
			stateMutex_.unlock();
			stateDue_ = false;
		}
	}
};

} // namespace shoal

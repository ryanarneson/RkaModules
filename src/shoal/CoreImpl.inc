// shoal::Core on top of the firmware, and the definitions behind the shim
// headers. Included last by Firmware.cpp, so every firmware name is visible.
//
// Each Core owns a shoal::Context (FirmwareContext.inc) holding all firmware
// and shim state; every Core member function that reaches firmware code makes
// that context current with a shoal::Scope first.
#include <cassert>
#include <cmath>
#include <memory>

#include "Core.hpp"
#include "CoreTest.hpp"
#include "Volts.hpp"

namespace shoal {
namespace hw {

// The current instance's fake hardware.
inline State& state() {
	return current()->hw;
}

} // namespace hw
} // namespace shoal

// ---- Shim definitions -------------------------------------------------------

const ShoalSerial Serial{};
const FreqMeasureClass FreqMeasure{};

void pinMode(uint8_t, uint8_t) { }

int digitalReadFast(uint8_t pin) {
	return pin < shoal::hw::kNumPins ? shoal::hw::state().pins[pin] : HIGH;
}

void digitalWriteFast(uint8_t pin, uint8_t value) {
	if (pin < shoal::hw::kNumPins)
		shoal::hw::state().pins[pin] = value ? HIGH : LOW;
}

// Both count from the shim's 64-bit tick counter, so millis() wraps at 2^32 ms
// (as on hardware), not when the firmware's 32-bit OC::CORE::ticks does.
uint32_t micros() {
	return (uint32_t) (shoal::hw::state().ticks * OC_CORE_TIMER_RATE);
}

uint32_t millis() {
	return (uint32_t) (shoal::hw::state().ticks * OC_CORE_TIMER_RATE / 1000u);
}

void randomSeed(uint32_t seed) {
	shoal::hw::state().rng = seed;
}

// Arduino semantics: random(n) is in [0, n), 0 when n <= 0;
// random(lo, hi) is in [lo, hi), lo when hi <= lo.
int32_t random(int32_t howbig) {
	if (howbig <= 0)
		return 0;
	uint32_t& s = shoal::hw::state().rng;
	s = s * 1664525u + 1013904223u;
	return (int32_t) ((((uint64_t) s) * (uint32_t) howbig) >> 32);
}

int32_t random(int32_t howsmall, int32_t howbig) {
	if (howsmall >= howbig)
		return howsmall;
	return howsmall + random(howbig - howsmall);
}

uint16_t shoal_adc_code12(int channel) {
	return shoal::hw::state().adc12[channel & 3];
}

uint8_t* shoal_eeprom_data() {
	return shoal::hw::state().eeprom;
}

// ---- shoal::Core ------------------------------------------------------------

namespace shoal {

// Firmware pins of shoal::Button and shoal::Encoder (OC_gpio.h); the
// firmware's UI::Button and UI::Encoder poll them in OC::Ui::Poll.
static const uint8_t kButtonPins[] = { but_top, but_bot, butL, butR };
static const uint8_t kEncoderPins[][2] = { { encL1, encL2 }, { encR1, encR2 } };
static const uint8_t kTriggerPins[kNumChannels] = { TR1, TR2, TR3, TR4 };

// One UI timer pass: a quadrature sample per encoder (a detent takes two
// passes), then OC::Ui::Poll, which queues the events the main pass's
// DispatchEvents hands to the app.
static void pollUi() {
	for (int e = 0; e < 2; e++) {
		bool a, b;
		hw::state().encoders[e].poll(a, b);
		hw::state().pins[kEncoderPins[e][0]] = a ? HIGH : LOW;
		hw::state().pins[kEncoderPins[e][1]] = b ? HIGH : LOW;
	}
	OC::g_ui.Poll();
}

struct Core::Impl {
	// Value-initialised: zeroed first, like the globals it replaces.
	std::unique_ptr<Context> ctx{ new Context() };
	float cv[kNumChannels] = {};
	Schmitt gates[kNumChannels];
	bool edges[kNumChannels] = {};  // rising edges latched since the last tick
	uint32_t dac[kNumChannels] = {}; // codes the DAC is holding
	int adcOffset = hw::kAdcOffsetHardware; // ADC code of 0 V for the current input range
	uint32_t uiMicros = 0;           // tick time since the last UI poll
	uint32_t mainTicks = 0;          // ticks since the last main-loop pass

	Impl() {
		Scope scope(ctx.get());
		shoal_boot();
		zeroOutputs();
	}

	// Outputs start at 0 V after boot, reset and restore, by the user's
	// decision of 2026-11-15 (the Teensy powers up at code 0xffff: DAC::Init
	// sets every channel to it). Through the firmware's own setter, so the
	// first tick sends 0 V too; the app's next write replaces it. Call with
	// this core's context current.
	void zeroOutputs() {
		OC::DAC::set_all(kZeroVoltCode);
		for (int ch = 0; ch < kNumChannels; ch++)
			dac[ch] = kZeroVoltCode;
	}

	// The ADC offsets for the current input range (Core::setInputRange).
	void applyInputRange() {
		for (int ch = 0; ch < kNumChannels; ch++)
			OC::g_calibration_data.adc.offset[ch] = (uint16_t) adcOffset;
	}

	// Replace the firmware instance with a newly constructed one booted from
	// `image` (null: an erased EEPROM, as a new core has), so nothing of the
	// old instance's firmware state survives. What the host owns carries over:
	// the input range, the levels on the input pins and ADC, the random
	// sources and the clocks. Queued encoder detents and latched gate edges
	// are dropped; the outputs return to 0 V. Returns whether both the global
	// settings page and the app data page loaded from the image. Call outside
	// any Scope on this core: the old context is gone afterwards.
	bool reboot(const uint8_t* image) {
		assert(current() != ctx.get());  // meant to stay on in release builds too (nothing defines NDEBUG)
		std::unique_ptr<Context> next(new Context());
		const hw::State& was = ctx->hw;
		hw::State& now = next->hw;
		if (image)
			memcpy(now.eeprom, image, kStateBytes);
		for (uint8_t pin : kButtonPins)
			now.pins[pin] = was.pins[pin];
		for (uint8_t pin : kTriggerPins)
			now.pins[pin] = was.pins[pin];
		memcpy(now.adc12, was.adc12, sizeof(now.adc12));
		now.rng = was.rng;
		now.ticks = was.ticks;
		next->Random_rng_state_ = ctx->Random_rng_state_;
		next->CORE_ticks = ctx->CORE_ticks;
		for (bool& e : edges)
			e = false;
		{
			// Firmware objects have no destructors that reach firmware state,
			// but destroy the old instance as its own current context anyway.
			Scope scope(ctx.get());
			ctx = std::move(next);
		}
		Scope scope(ctx.get());
		const OC::apps::InitResult loaded = shoal_boot();
		applyInputRange();  // shoal_boot loaded the calibration defaults
		zeroOutputs();
		return loaded.global_settings_loaded && loaded.app_data_loaded;
	}

	// Present the latched inputs to the firmware for the coming tick.
	void applyInputs() {
		for (int ch = 0; ch < kNumChannels; ch++)
			hw::state().adc12[ch] = (uint16_t) voltsToAdc12(cv[ch], adcOffset);
		if (edges[0]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_1>();
		if (edges[1]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_2>();
		if (edges[2]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_3>();
		if (edges[3]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_4>();
		for (int ch = 0; ch < kNumChannels; ch++) {
			edges[ch] = false;
			// Trigger inputs are active low.
			hw::state().pins[kTriggerPins[ch]] = gates[ch].high ? LOW : HIGH;
		}
	}
};

Core::Core() : impl_(new Impl) {
}

Core::~Core() {
	// Firmware objects have no destructors that reach firmware state, but
	// make this instance current anyway in case one ever does.
	Scope scope(impl_->ctx.get());
	delete impl_;
}

// NaN reads as 0 V; infinities are clamped later (voltsToAdc12, Schmitt).
static float finiteVolts(float volts) {
	return std::isnan(volts) ? 0.f : volts;
}

void Core::setCv(int ch, float volts) {
	if (ch >= 0 && ch < kNumChannels)
		impl_->cv[ch] = finiteVolts(volts);
}

void Core::setGate(int ch, float volts) {
	// A rising edge stays latched until the next tick, even if the gate has
	// fallen again by then; several edges in one tick are one clock.
	if (ch >= 0 && ch < kNumChannels && impl_->gates[ch].process(finiteVolts(volts)))
		impl_->edges[ch] = true;
}

void Core::setInputRange(InputRange r) {
	Scope scope(impl_->ctx.get());
	impl_->adcOffset = r == InputRange::Bipolar5 ? hw::kAdcOffsetBipolar5 : hw::kAdcOffsetHardware;
	// The firmware subtracts the offset from the smoothed code; the new
	// codes arrive with the next ADC update.
	impl_->applyInputRange();
}

void Core::tick() {
	Scope scope(impl_->ctx.get());
	impl_->applyInputs();
	// The tick ISR starts by sending the codes set since the last tick to the
	// DAC, which then holds them for the whole tick.
	for (int ch = 0; ch < kNumChannels; ch++)
		impl_->dac[ch] = OC::DAC::value(ch);
	hw::state().ticks++;  // shoal_tick() advances OC::CORE::ticks in step
	shoal_tick();

	impl_->uiMicros += OC_CORE_TIMER_RATE;
	if (impl_->uiMicros >= OC_UI_TIMER_RATE) {
		impl_->uiMicros -= OC_UI_TIMER_RATE;
		pollUi();
	}
	if (++impl_->mainTicks >= 17) {
		impl_->mainTicks = 0;
		shoal_main_pass();
	}
}

bool Core::encodersPending() const {
	Scope scope(impl_->ctx.get());
	// A detent's event is queued by the UI poll that plays its last sample.
	// A main pass usually empties the firmware's queue, but not always: the
	// R long press that opens the app menu ends the dispatch, and what is
	// queued behind it goes to the app menu at the next pass. (Closing the
	// app menu flushes the queue, as on the hardware.) So ask the queue; a
	// queued button event counts too, which only delays an edit by a pass.
	return OC::g_ui.events_queued() || !hw::state().encoders[ENCODER_L].idle()
		|| !hw::state().encoders[ENCODER_R].idle();
}

float Core::out(int ch) const {
	return dacToVolts(outCode(ch));
}

uint32_t Core::outCode(int ch) const {
	return ch >= 0 && ch < kNumChannels ? impl_->dac[ch] : 0;
}

void Core::setButton(Button b, bool down) {
	if (b >= sizeof(kButtonPins))
		return;
	Scope scope(impl_->ctx.get());
	// Buttons are active low.
	hw::state().pins[kButtonPins[b]] = down ? LOW : HIGH;
}

void Core::turnEncoder(Encoder e, int detents) {
	if (e > ENCODER_R)
		return;
	Scope scope(impl_->ctx.get());
	hw::state().encoders[e].turn(detents);
}

void Core::setEncoderAcceleration(bool on) {
	Scope scope(impl_->ctx.get());
	// What the app menu's UP button does.
	OC::global_settings.encoders_enable_acceleration = on;
	OC::g_ui.encoders_enable_acceleration(on);
}

void Core::drawMenu(uint8_t* dst) {
	Scope scope(impl_->ctx.get());
	uint8_t* frame = impl_->ctx->menu_frame;
	graphics.Begin(frame, true);
	if (OC::g_ui.app_menu_open())
		OC::draw_app_menu(OC::app_menu_cursor);
	else
		OC::apps::current_app->DrawMenu();
	graphics.End();
	memcpy(dst, frame, kFrameBytes);
}

void Core::drawScope(uint8_t* dst) {
	Scope scope(impl_->ctx.get());
	uint8_t* frame = impl_->ctx->scope_frame;
	graphics.Begin(frame, true);
	OC::apps::current_app->DrawScreensaver();
	graphics.End();
	memcpy(dst, frame, kFrameBytes);
}

bool Core::pixel(const uint8_t* frame, int x, int y) {
	if (x < 0 || x >= kFrameW || y < 0 || y >= kFrameH)
		return false;
	return (frame[(y >> 3) * kFrameW + x] >> (y & 7)) & 1;
}

int Core::app() const {
	Scope scope(impl_->ctx.get());
	return OC::apps::index_of(OC::global_settings.current_app_id);
}

void Core::selectApp(int index) {
	Scope scope(impl_->ctx.get());
	if (index < 0 || index >= kNumApps)
		return;
	if (OC::g_ui.app_menu_open())
		OC::g_ui.AppMenuClose(false);
	if (index == app())
		return;
	// The app menu's change path (OC::Ui::AppMenuClose).
	OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_SUSPEND);
	OC::apps::set_current_app(index);
	FreqMeasure.end();
	OC::DigitalInputs::reInit();
	OC::g_ui.encoders_enable_acceleration(OC::global_settings.encoders_enable_acceleration);
	OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_RESUME);
}

const char* Core::appName(int index) {
	return index >= 0 && index < kNumApps ? available_apps[index].name : "";
}

bool Core::appMenuOpen() const {
	Scope scope(impl_->ctx.get());
	return OC::g_ui.app_menu_open();
}

int Core::channels() const {
	Scope scope(impl_->ctx.get());
	const AppSettings* a = currentAppSettings();
	return a ? a->channels : 0;
}

int Core::settingCount() const {
	Scope scope(impl_->ctx.get());
	const AppSettings* a = currentAppSettings();
	return a ? a->count : 0;
}

SettingInfo Core::settingInfo(int index) const {
	Scope scope(impl_->ctx.get());
	const AppSettings* a = currentAppSettings();
	if (!a || index < 0 || index >= a->count)
		return SettingInfo{ "", 0, 0, 0, nullptr };
	const settings::value_attr& v = a->attr((size_t) index);
	return SettingInfo{ v.name, v.min_, v.max_, v.default_, v.value_names };
}

int Core::setting(int channel, int index) const {
	Scope scope(impl_->ctx.get());
	const AppSettings* a = currentAppSettings();
	if (!a || channel < 0 || channel >= a->channels || index < 0 || index >= a->count)
		return 0;
	return a->get(channel, index);
}

bool Core::setSettingRaw(int channel, int index, int value) {
	Scope scope(impl_->ctx.get());
	const AppSettings* a = currentAppSettings();
	if (!a || channel < 0 || channel >= a->channels || index < 0 || index >= a->count)
		return false;
	return a->set(channel, index, value);
}

void Core::friendlyModel(FriendlyModel& out) const {
	Scope scope(impl_->ctx.get());
	friendly::model(friendly::adapterFor(OC::apps::index_of(OC::global_settings.current_app_id)), out);
}

bool Core::friendlyEdit(const FriendlyEdit& e) {
	Scope scope(impl_->ctx.get());
	return friendly::edit(friendly::adapterFor(OC::apps::index_of(OC::global_settings.current_app_id)), e);
}

void Core::save(uint8_t* out) {
	Scope scope(impl_->ctx.get());
	// The firmware's own save: each rewrites its page only if the contents
	// changed, so with no change the image is unchanged.
	OC::save_global_settings();
	OC::save_app_data();
	memcpy(out, hw::state().eeprom, kStateBytes);
}

bool Core::restore(const uint8_t* in) {
	static_assert(kStateBytes == EEPROMClass::kLength, "image is the whole EEPROM");
	if (impl_->reboot(in))
		return true;
	// Rejected (either page missing or damaged): a new core's defaults over
	// an erased EEPROM, so that no stale page of the rejected image can
	// outrank what the next save writes.
	impl_->reboot(nullptr);
	return false;
}

void Core::resetToDefaults() {
	impl_->reboot(nullptr);
}

void Core::seed(uint32_t s) {
	Scope scope(impl_->ctx.get());
	randomSeed(s);
	stmlib::Random::Seed(s);
}

static_assert(kNumApps == NUM_AVAILABLE_APPS, "app count");

void CoreTest::setTicks(Core& c, uint64_t ticks) {
	Scope scope(c.impl_->ctx.get());
	hw::state().ticks = ticks;
	OC::CORE::ticks_ref() = (uint32_t) ticks;
}

uint32_t CoreTest::millis(Core& c) {
	Scope scope(c.impl_->ctx.get());
	return ::millis();
}

uint64_t CoreTest::ticks(Core& c) {
	Scope scope(c.impl_->ctx.get());
	return hw::state().ticks;
}

int32_t CoreTest::pitchIn(Core& c, int ch) {
	Scope scope(c.impl_->ctx.get());
	return OC::ADC::pitch_value((ADC_CHANNEL) ch);
}

uint32_t CoreTest::smoothedCode(Core& c, int ch) {
	Scope scope(c.impl_->ctx.get());
	return OC::ADC::smoothed_raw_value((ADC_CHANNEL) ch);
}

int CoreTest::menuSetting(Core& c) {
	Scope scope(c.impl_->ctx.get());
	if (c.app() == 5)
		return poly_lfo_state.cursor.cursor_pos();
	if (c.app() == 6)
		return lorenz_generator_state.cursor.cursor_pos();
	if (c.app() == 2 && automatonnetz_state.ui.edit_cell)
		return -1;  // the cell view: its cursor is on a cell setting
	if (c.app() == 7 && envgen.ui.edit_mode == QuadEnvelopeGenerator::MODE_EDIT_SEGMENTS)
		return ENV_SETTING_SEG1_VALUE + envgen.ui.selected_segment;  // the envelope page: the selected segment
	// The others (enabledSettings knows them): the setting at the cursor's
	// menu row, -1 when that row is past the end of the list (a stale cursor).
	int list[128];
	const int n = enabledSettings(c, list, 128);
	const int row = menuRow(c);
	return n >= 0 && row >= 0 && row < n ? list[row] : -1;
}

int CoreTest::menuRow(Core& c) {
	Scope scope(c.impl_->ctx.get());
	switch (c.app()) {
		case 0: return asr_state.cursor_pos();
		case 1: return h1200_state.cursor.cursor_pos();
		case 2: return automatonnetz_state.ui.grid_cursor.cursor_pos();
		case 3: return qq_state.cursor_pos();
		case 4: return dq_state.cursor.cursor_pos();
		case 5: return poly_lfo_state.cursor.cursor_pos();
		case 6: return lorenz_generator_state.cursor.cursor_pos();
		case 7: return envgen.ui.cursor.cursor_pos();
		case 8: return seq_state.cursor_pos();
		case 9: return bbgen.ui.cursor.cursor_pos();
		case 10: return bytebeatgen.ui.cursor.cursor_pos();
		case 11: return chords_state.cursor_pos();
		case 12: return references_app.ui.cursor.cursor_pos();
		default: return -1;
	}
}

bool CoreTest::menuEditing(Core& c) {
	Scope scope(c.impl_->ctx.get());
	switch (c.app()) {
		case 0: return asr_state.cursor.editing();
		case 1: return h1200_state.cursor.editing();
		case 2: return automatonnetz_state.ui.grid_cursor.editing();
		case 3: return qq_state.cursor.editing();
		case 4: return dq_state.cursor.editing();
		case 5: return poly_lfo_state.cursor.editing();
		case 6: return lorenz_generator_state.cursor.editing();
		case 7: return envgen.ui.edit_mode == QuadEnvelopeGenerator::MODE_EDIT_SEGMENTS ? envgen.ui.segment_editing : envgen.ui.cursor.editing();
		case 8: return seq_state.cursor.editing();
		case 9: return bbgen.ui.cursor.editing();
		case 10: return bytebeatgen.ui.cursor.editing();
		case 11: return chords_state.cursor.editing();
		case 12: return references_app.ui.cursor.editing();
		default: return false;
	}
}

int CoreTest::selectedChannel(Core& c) {
	Scope scope(c.impl_->ctx.get());
	switch (c.app()) {
		case 3: return qq_state.selected_channel;
		case 4: return dq_state.selected_channel;
		case 7: return envgen.ui.selected_channel;
		case 8: return seq_state.selected_channel;
		case 9: return bbgen.ui.selected_channel;
		case 10: return bytebeatgen.ui.selected_channel;
		case 12: return references_app.ui.selected_channel;
		default: return 0;
	}
}

int CoreTest::enabledSettings(Core& c, int* out, int max) {
	Scope scope(c.impl_->ctx.get());
	int n = -1;
	auto list = [&](const auto& obj) {
		n = obj.num_enabled_settings();
		for (int i = 0; i < n && i < max; i++)
			out[i] = (int) obj.enabled_setting_at(i);
	};
	auto table = [&](int count) {
		n = count;
		for (int i = 0; i < n && i < max; i++)
			out[i] = i;
	};
	switch (c.app()) {
		case 0: list(asr); break;
		case 1: list(h1200_settings); break;
		case 2: table(GRID_SETTING_LAST); break;
		case 3: list(quantizer_channels[qq_state.selected_channel]); break;
		case 4: list(dq_quantizer_channels[dq_state.selected_channel]); break;
		case 7: list(envgen.selected()); break;
		case 8: list(seq_channel[seq_state.selected_channel]); break;
		case 9: table(BB_SETTING_LAST); break;
		case 10: list(bytebeatgen.selected()); break;
		case 11: list(chords); break;
		case 12: list(references_app.selected_channel()); break;
		default: break;
	}
	return n < max ? n : max;
}

int CoreTest::menuPage(Core& c) {
	Scope scope(c.impl_->ctx.get());
	switch (c.app()) {
		case 7: return envgen.ui.edit_mode == QuadEnvelopeGenerator::MODE_EDIT_SETTINGS ? 1 : 0;
		case 8: return seq_channel[seq_state.selected_channel].get_menu_page();
		case 11: return chords.get_menu_page();
		default: return -1;
	}
}

bool CoreTest::piquedEuclidLength(Core& c) {
	Scope scope(c.impl_->ctx.get());
	return envgen.ui.euclidean_edit_length;
}

void CoreTest::pollUi(Core& c) {
	Scope scope(c.impl_->ctx.get());
	shoal::pollUi();
}

bool CoreTest::friendlyAccepting(Core& c) {
	Scope scope(c.impl_->ctx.get());
	const friendly::Adapter& a = friendly::adapterFor(c.app());
	return !a.accepting || a.accepting();
}

bool CoreTest::friendlyHasAdapter(int app) {
	return app >= 0 && app < kNumApps && friendly::adapterFor(app).edit != nullptr;
}

bool CoreTest::friendlyFake(Core& c, FriendlyFake fake, const FriendlyEdit* e, FriendlyModel* out) {
	Scope scope(c.impl_->ctx.get());
	const friendly::Adapter& a = friendly::fake::kFakes[fake];
	const bool taken = e && friendly::edit(a, *e);
	if (out)
		friendly::model(a, *out);
	return taken;
}

void CoreTest::fixedText(double value, int decimals, int width, char (&out)[24]) {
	friendly::Text(out).fixed(value, decimals, width);
}

void CoreTest::encoderEvent(Core& c, Encoder e, int delta) {
	Scope scope(c.impl_->ctx.get());
	if (delta)
		OC::apps::current_app->HandleEncoderEvent(UI::Event(UI::EVENT_ENCODER,
			e == ENCODER_L ? OC::CONTROL_ENCODER_L : OC::CONTROL_ENCODER_R, (int16_t) delta, 0));
}

void CoreTest::appMenuEvents(Core& c) {
	Scope scope(c.impl_->ctx.get());
	OC::g_ui.AppMenuOpen();
	OC::g_ui.AppMenuHandle(UI::Event(UI::EVENT_BUTTON_PRESS, OC::CONTROL_BUTTON_R, 0, 0));
}

void CoreTest::buttonEvent(Core& c, Button b, bool longPress) {
	static const uint16_t kControls[] = { OC::CONTROL_BUTTON_UP, OC::CONTROL_BUTTON_DOWN, OC::CONTROL_BUTTON_L, OC::CONTROL_BUTTON_R };
	Scope scope(c.impl_->ctx.get());
	OC::apps::current_app->HandleButtonEvent(UI::Event(
		longPress ? UI::EVENT_BUTTON_LONG_PRESS : UI::EVENT_BUTTON_PRESS, kControls[b & 3], 0, 0));
}

} // namespace shoal

namespace {
// Reaches the page layout and checksum that PageStorage keeps protected.
template <typename Storage, typename Data>
struct PageProbe : Storage {
	using Page = typename Storage::page_data;
	static size_t dataOffset() { return offsetof(Page, data); }
	static size_t dataSize() { return sizeof(Data); }
	static void fix(uint8_t* page) {
		Page p;
		memcpy(&p, page, sizeof(p));
		p.header.checksum = Storage::checksum(p);
		memcpy(page, &p, sizeof(p));
	}
};
} // namespace

namespace shoal {

void CoreTest::pageLayout(bool appData, size_t& pageOffset, size_t& dataOffset, size_t& dataSize) {
	if (appData) {
		using P = PageProbe<OC::AppDataStorage, OC::AppData>;
		pageOffset = EEPROM_APPDATA_START;
		dataOffset = pageOffset + P::dataOffset();
		dataSize = P::dataSize();
	} else {
		using P = PageProbe<OC::GlobalSettingsStorage, OC::GlobalSettings>;
		pageOffset = EEPROM_GLOBALSETTINGS_START;
		dataOffset = pageOffset + P::dataOffset();
		dataSize = P::dataSize();
	}
}

void CoreTest::fixChecksum(uint8_t* image, bool appData) {
	if (appData)
		PageProbe<OC::AppDataStorage, OC::AppData>::fix(image + EEPROM_APPDATA_START);
	else
		PageProbe<OC::GlobalSettingsStorage, OC::GlobalSettings>::fix(image + EEPROM_GLOBALSETTINGS_START);
}

void CoreTest::userScaleImageOffsets(int slot, size_t& span, size_t& numNotes, size_t& notes) {
	size_t pageOff, dataOff, dataSize;
	pageLayout(false, pageOff, dataOff, dataSize);
	const size_t base = dataOff + offsetof(OC::GlobalSettings, user_scales) + slot * sizeof(OC::Scale);
	span = base + offsetof(OC::Scale, span);
	numNotes = base + offsetof(OC::Scale, num_notes);
	notes = base + offsetof(OC::Scale, notes);
}

void CoreTest::inputMapRun(int numSlots, int range, const int32_t* inputs, int n, int8_t* slots) {
	OC::Input_Map map;
	map.Init();
	map.Configure(OC::InputMaps::GetInputMap(numSlots), (uint8_t) range);
	for (int i = 0; i < n; i++)
		slots[i] = map.Process(inputs[i]);
}

void CoreTest::quantizerRun(const int32_t* pitches, int n, int32_t* out) {
	int16_t notes[16];
	for (int i = 0; i < 16; i++)
		notes[i] = (int16_t) (96 * i);
	quantizerRun(notes, pitches, n, out);
}

void CoreTest::quantizerRun(const int16_t* notes, const int32_t* pitches, int n, int32_t* out) {
	braids::Quantizer q;  // zeroed, then Init, as the firmware's
	memset(static_cast<void*>(&q), 0, sizeof(q));
	q.Init();
	braids::Scale scale;
	scale.span = 1536;
	scale.num_notes = 16;
	for (int i = 0; i < 16; i++)
		scale.notes[i] = notes[i];
	q.Configure(scale, 0xffff);
	for (int i = 0; i < n; i++)
		out[i] = q.Process(pitches[i], 0, 0);
}

void CoreTest::intSeqRun(Core& c, int seq, int start, int length, int n, uint16_t* out) {
	Scope scope(c.impl_->ctx.get());
	util::IntegerSequence s;
	s.Init((int16_t) start, (int16_t) length);
	s.set_int_seq((int16_t) seq);
	s.set_int_seq_modulus(256);
	for (int i = 0; i < n; i++)
		out[i] = s.Clock();
}

int CoreTest::userScaleSlots() { return OC::Scales::SCALE_USER_LAST; }
int CoreTest::userPatternSlots() { return OC::Patterns::PATTERN_USER_ALL; }
int CoreTest::userChordSlots() { return OC::Chords::CHORDS_USER_LAST; }

int16_t& CoreTest::userScaleNote(Core& c, int slot, int note) {
	Scope scope(c.impl_->ctx.get());
	return OC::g_user_scales[slot].notes[note];
}

uint32_t& CoreTest::userScaleNumNotes(Core& c, int slot) {
	Scope scope(c.impl_->ctx.get());
	return OC::g_user_scales[slot].num_notes;
}

int16_t& CoreTest::userPatternNote(Core& c, int slot, int note) {
	Scope scope(c.impl_->ctx.get());
	return OC::g_user_patterns[slot].notes[note];
}

int8_t& CoreTest::userChordField(Core& c, int slot, int field) {
	Scope scope(c.impl_->ctx.get());
	OC::Chord& ch = OC::g_user_chords[slot];
	int8_t* fields[5] = { &ch.quality, &ch.inversion, &ch.voicing, &ch.base_note, &ch.octave };
	return *fields[field];
}

} // namespace shoal

bool shoal_test_current_is_null() {
	return shoal::current() == nullptr;
}

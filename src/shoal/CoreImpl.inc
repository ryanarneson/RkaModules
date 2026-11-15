// shoal::Core on top of the firmware, and the definitions behind the shim
// headers. Included last by Firmware.cpp, so every firmware name is visible.
//
// Task 2: one global firmware instance. All shim hardware state lives in
// shoal::hw::state; Task 3 moves it (and the firmware globals) per instance.
#include <cmath>

#include "Core.hpp"
#include "Volts.hpp"

namespace shoal {
namespace hw {

constexpr int kNumPins = 64;
constexpr int kAdcOffsetHardware = 2730;

// What the fake Teensy's peripherals see.
struct State {
	uint8_t pins[kNumPins];                // pin levels, read by digitalReadFast
	uint16_t adc12[kNumChannels];          // 12-bit codes on the CV inputs
	uint8_t eeprom[EEPROMClass::kLength];  // EEPROM image
	uint32_t rng;                          // Arduino random() state

	void reset() {
		memset(pins, HIGH, sizeof(pins));
		for (auto& code : adc12)
			code = kAdcOffsetHardware; // 0 V
		memset(eeprom, 0xff, sizeof(eeprom)); // erased flash reads 0xff
		rng = 1;
	}
};

State state;

} // namespace hw
} // namespace shoal

// ---- Shim definitions -------------------------------------------------------

ShoalSerial Serial;
FreqMeasureClass FreqMeasure;

void pinMode(uint8_t, uint8_t) { }

int digitalReadFast(uint8_t pin) {
	return pin < shoal::hw::kNumPins ? shoal::hw::state.pins[pin] : HIGH;
}

void digitalWriteFast(uint8_t pin, uint8_t value) {
	if (pin < shoal::hw::kNumPins)
		shoal::hw::state.pins[pin] = value ? HIGH : LOW;
}

uint32_t micros() {
	return OC::CORE::ticks * OC_CORE_TIMER_RATE;
}

uint32_t millis() {
	return (uint32_t) ((uint64_t) OC::CORE::ticks * OC_CORE_TIMER_RATE / 1000u);
}

void randomSeed(uint32_t seed) {
	shoal::hw::state.rng = seed;
}

// Arduino semantics: random(n) is in [0, n), 0 when n <= 0;
// random(lo, hi) is in [lo, hi), lo when hi <= lo.
int32_t random(int32_t howbig) {
	if (howbig <= 0)
		return 0;
	uint32_t& s = shoal::hw::state.rng;
	s = s * 1664525u + 1013904223u;
	return (int32_t) ((((uint64_t) s) * (uint32_t) howbig) >> 32);
}

int32_t random(int32_t howsmall, int32_t howbig) {
	if (howsmall >= howbig)
		return howsmall;
	return howsmall + random(howbig - howsmall);
}

uint16_t shoal_adc_code12(int channel) {
	return shoal::hw::state.adc12[channel & 3];
}

uint8_t* shoal_eeprom_data() {
	return shoal::hw::state.eeprom;
}

// ---- shoal::Core ------------------------------------------------------------

namespace shoal {

struct Core::Impl {
	float cv[kNumChannels] = {};
	Schmitt gates[kNumChannels];
	bool edges[kNumChannels] = {};  // rising edges latched since the last tick
	uint32_t dac[kNumChannels] = {}; // codes the DAC is holding
	uint32_t uiMicros = 0;           // tick time since the last UI poll
	uint32_t mainTicks = 0;          // ticks since the last main-loop pass

	// Present the latched inputs to the firmware for the coming tick.
	void applyInputs() {
		for (int ch = 0; ch < kNumChannels; ch++)
			hw::state.adc12[ch] = (uint16_t) voltsToAdc12(cv[ch], hw::kAdcOffsetHardware);
		if (edges[0]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_1>();
		if (edges[1]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_2>();
		if (edges[2]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_3>();
		if (edges[3]) OC::DigitalInputs::clock<OC::DIGITAL_INPUT_4>();
		static const uint8_t kTriggerPins[kNumChannels] = { TR1, TR2, TR3, TR4 };
		for (int ch = 0; ch < kNumChannels; ch++) {
			edges[ch] = false;
			// Trigger inputs are active low.
			hw::state.pins[kTriggerPins[ch]] = gates[ch].high ? LOW : HIGH;
		}
	}
};

Core::Core() : impl_(new Impl) {
	hw::state.reset();
	OC::CORE::ticks = 0;
	OC::CORE::app_isr_enabled = false;
	shoal_boot();
}

Core::~Core() {
	delete impl_;
}

void Core::setCv(int ch, float volts) {
	if (ch >= 0 && ch < kNumChannels)
		impl_->cv[ch] = volts;
}

void Core::setGate(int ch, float volts) {
	if (ch >= 0 && ch < kNumChannels && impl_->gates[ch].process(volts))
		impl_->edges[ch] = true;
}

void Core::setInputRange(InputRange) {
	// Task 4
}

void Core::tick() {
	impl_->applyInputs();
	// The tick ISR starts by sending the codes set since the last tick to the
	// DAC, which then holds them for the whole tick.
	for (int ch = 0; ch < kNumChannels; ch++)
		impl_->dac[ch] = OC::DAC::value(ch);
	shoal_tick();

	impl_->uiMicros += OC_CORE_TIMER_RATE;
	if (impl_->uiMicros >= OC_UI_TIMER_RATE) {
		impl_->uiMicros -= OC_UI_TIMER_RATE;
		OC::ui.Poll();
	}
	if (++impl_->mainTicks >= 17) {
		impl_->mainTicks = 0;
		shoal_main_pass();
	}
}

float Core::out(int ch) const {
	return dacToVolts(outCode(ch));
}

uint32_t Core::outCode(int ch) const {
	return ch >= 0 && ch < kNumChannels ? impl_->dac[ch] : 0;
}

void Core::setButton(Button, bool) {
	// Task 5
}

void Core::turnEncoder(Encoder, int) {
	// Task 5
}

void Core::setEncoderAcceleration(bool) {
	// Task 5
}

void Core::drawMenu(uint8_t* dst) {
	// Task 5
	memset(dst, 0, kFrameBytes);
}

void Core::drawScope(uint8_t* dst) {
	// Task 5
	memset(dst, 0, kFrameBytes);
}

bool Core::pixel(const uint8_t*, int, int) {
	// Task 5
	return false;
}

int Core::app() const {
	return OC::apps::index_of(OC::global_settings.current_app_id);
}

void Core::selectApp(int index) {
	if (index < 0 || index >= kNumApps || index == app())
		return;
	OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_SUSPEND);
	OC::apps::set_current_app(index);
	OC::apps::current_app->HandleAppEvent(OC::APP_EVENT_RESUME);
}

const char* Core::appName(int index) {
	return index >= 0 && index < kNumApps ? available_apps[index].name : "";
}

bool Core::appMenuOpen() const {
	// Task 5
	return false;
}

int Core::channels() const {
	// Task 6
	return 0;
}

int Core::settingCount() const {
	// Task 6
	return 0;
}

SettingInfo Core::settingInfo(int) const {
	// Task 6
	return SettingInfo{ "", 0, 0, 0, nullptr };
}

int Core::setting(int, int) const {
	// Task 6
	return 0;
}

bool Core::setSettingRaw(int, int, int) {
	// Task 6
	return false;
}

void Core::save(uint8_t* out) {
	// Task 7
	memset(out, 0, kStateBytes);
}

bool Core::restore(const uint8_t*) {
	// Task 7
	return false;
}

void Core::resetToDefaults() {
	// Task 7
}

void Core::seed(uint32_t s) {
	randomSeed(s);
	stmlib::Random::Seed(s);
}

static_assert(kNumApps == NUM_AVAILABLE_APPS, "app count");

} // namespace shoal

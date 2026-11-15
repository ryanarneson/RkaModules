// shoal::Context: every mutable firmware global, one set per shoal::Core.
// Included by Firmware.cpp after all firmware files (so every firmware type
// is complete) and before CoreImpl.inc. See shim/Context.hpp for the
// convention that routes the firmware's names here.
//
// Members follow the order of the original definitions in the unity
// translation unit. Former globals were zero-initialised before their
// constructors ran; Core::Impl value-initialises its Context (`new
// Context()`), which zero-initialises it the same way before the default
// member initialisers and member constructors run.

#include "Core.hpp" // kNumChannels

// The name macros would rewrite the member names below; switch them off for
// this file and restore them at the end (CoreImpl.inc uses them).
#pragma push_macro("g_auto_calibration_data")
#pragma push_macro("g_user_chords")
#pragma push_macro("circle_pos_lut")
#pragma push_macro("scope_history")
#pragma push_macro("averaged_scope_history")
#pragma push_macro("averaged_scope_tail")
#pragma push_macro("scope_update_channel")
#pragma push_macro("g_user_patterns")
#pragma push_macro("dummy_pattern")
#pragma push_macro("g_user_scales")
#pragma push_macro("dummy_scale")
#pragma push_macro("g_ui")
#pragma push_macro("MENU_REDRAW")
#pragma push_macro("graphics")
#pragma push_macro("frame_buffer")
#pragma push_macro("asr_state")
#pragma push_macro("asr")
#pragma push_macro("channel_history")
#pragma push_macro("automatonnetz_state")
#pragma push_macro("ticks_src1")
#pragma push_macro("ticks_src2")
#pragma push_macro("copy_sequence")
#pragma push_macro("copy_length")
#pragma push_macro("copy_mask")
#pragma push_macro("copy_timeout")
#pragma push_macro("ext_frequency")
#pragma push_macro("seq_state")
#pragma push_macro("seq_channel")
#pragma push_macro("bbgen")
#pragma push_macro("bytebeatgen")
#pragma push_macro("bb_history")
#pragma push_macro("chords_state")
#pragma push_macro("chords")
#pragma push_macro("dq_state")
#pragma push_macro("dq_quantizer_channels")
#pragma push_macro("dq_history")
#pragma push_macro("envgen")
#pragma push_macro("preview_values")
#pragma push_macro("preview_segment_starts")
#pragma push_macro("preview_loop_points")
#pragma push_macro("segment_editing_attr")
#pragma push_macro("fast_preview_values")
#pragma push_macro("h1200_settings")
#pragma push_macro("h1200_state")
#pragma push_macro("lorenz_generator")
#pragma push_macro("lorenz_generator_state")
#pragma push_macro("poly_lfo")
#pragma push_macro("poly_lfo_state")
#pragma push_macro("preview_buffer")
#pragma push_macro("qq_state")
#pragma push_macro("quantizer_channels")
#pragma push_macro("g_history")
#pragma push_macro("references_app")
#pragma push_macro("global_settings")
#pragma push_macro("global_settings_storage")
#pragma push_macro("app_settings")
#pragma push_macro("app_data_storage")
#pragma push_macro("current_app")
#pragma push_macro("g_calibration_data")
#pragma push_macro("LAST_REDRAW_TIME")
#pragma push_macro("ui_mode")
#undef g_auto_calibration_data
#undef g_user_chords
#undef circle_pos_lut
#undef scope_history
#undef averaged_scope_history
#undef averaged_scope_tail
#undef scope_update_channel
#undef g_user_patterns
#undef dummy_pattern
#undef g_user_scales
#undef dummy_scale
#undef g_ui
#undef MENU_REDRAW
#undef graphics
#undef frame_buffer
#undef asr_state
#undef asr
#undef channel_history
#undef automatonnetz_state
#undef ticks_src1
#undef ticks_src2
#undef copy_sequence
#undef copy_length
#undef copy_mask
#undef copy_timeout
#undef ext_frequency
#undef seq_state
#undef seq_channel
#undef bbgen
#undef bytebeatgen
#undef bb_history
#undef chords_state
#undef chords
#undef dq_state
#undef dq_quantizer_channels
#undef dq_history
#undef envgen
#undef preview_values
#undef preview_segment_starts
#undef preview_loop_points
#undef segment_editing_attr
#undef fast_preview_values
#undef h1200_settings
#undef h1200_state
#undef lorenz_generator
#undef lorenz_generator_state
#undef poly_lfo
#undef poly_lfo_state
#undef preview_buffer
#undef qq_state
#undef quantizer_channels
#undef g_history
#undef references_app
#undef global_settings
#undef global_settings_storage
#undef app_settings
#undef app_data_storage
#undef current_app
#undef g_calibration_data
#undef LAST_REDRAW_TIME
#undef ui_mode

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
	uint64_t ticks;                        // core ticks since boot (64-bit: millis() wraps at 2^32 ms)

	State() {
		memset(pins, HIGH, sizeof(pins));
		for (auto& code : adc12)
			code = kAdcOffsetHardware; // 0 V
		memset(eeprom, 0xff, sizeof(eeprom)); // erased flash reads 0xff
		rng = 1;
		ticks = 0;
	}
};

} // namespace hw

struct Context {
	// OC_ADC.cpp
	OC::ADC::CalibrationData* ADC_calibration_data_;
	uint32_t ADC_raw_[ADC_CHANNEL_LAST];
	uint32_t ADC_smoothed_[ADC_CHANNEL_LAST];
	uint32_t ADC_scan_count_;
	// OC_DAC.cpp
	OC::DAC::CalibrationData* DAC_calibration_data_ = nullptr;
	uint32_t DAC_values_[DAC_CHANNEL_LAST];
	uint16_t DAC_history_[DAC_CHANNEL_LAST][OC::DAC::kHistoryDepth];
	volatile size_t DAC_history_tail_;
	uint8_t DAC_DAC_scaling[DAC_CHANNEL_LAST];
	// OC_autotune.cpp, OC_chords.cpp
	OC::Autotune_data g_auto_calibration_data[DAC_CHANNEL_LAST];
	OC::Chord g_user_chords[OC::Chords::CHORDS_USER_LAST];
	// OC_digital_inputs.cpp
	uint32_t DigitalInputs_clocked_mask_;
	volatile uint32_t DigitalInputs_clocked_[OC::DIGITAL_INPUT_LAST];
	// OC_menus.cpp
	OC::coords circle_pos_lut[12];
	uint16_t scope_history[OC::DAC::kHistoryDepth];
	uint16_t averaged_scope_history[DAC_CHANNEL_LAST][OC::kScopeDepth];
	size_t averaged_scope_tail = 0;
	DAC_CHANNEL scope_update_channel = DAC_CHANNEL_A;
	// OC_patterns.cpp, OC_scales.cpp, OC_ui.cpp
	OC::Pattern g_user_patterns[OC::Patterns::PATTERN_USER_ALL];
	OC::Pattern dummy_pattern;
	OC::Scale g_user_scales[OC::Scales::SCALE_USER_LAST];
	OC::Scale dummy_scale;
	OC::Ui g_ui;
	// extern/stmlib_utils_random.cpp
	uint32_t Random_rng_state_ = 0x21;
	// src/drivers/display.cpp
	weegfx::Graphics graphics;
	display::FrameBufferType frame_buffer;
	// APP_ASR.ino
	ASRState asr_state;
	ASR asr;
	uint16_t channel_history[ASR::kHistoryDepth];
	// APP_AUTOMATONNETZ.ino
	AutomatonnetzState automatonnetz_state;
	// APP_A_SEQ.ino
	uint32_t ticks_src1 = 0;
	uint32_t ticks_src2 = 0;
	uint8_t copy_sequence = 0;
	uint8_t copy_length = OC::Patterns::kMax;
	uint16_t copy_mask = 0xFFFF;
	uint64_t copy_timeout = COPYTIMEOUT;
	uint64_t ext_frequency[SEQ_CHANNEL_TRIGGER_NONE + 1];
	SEQ_State seq_state;
	SEQ_Channel seq_channel[NUM_CHANNELS];
	// APP_BBGEN.ino, APP_BYTEBEATGEN.ino
	QuadBouncingBalls bbgen;
	QuadByteBeats bytebeatgen;
	uint8_t bb_history[ByteBeat::kHistoryDepth];
	// APP_CHORDS.ino
	ChordQuantizer chords_state;
	Chords chords;
	// APP_DQ.ino
	DualQuantizer dq_state;
	DQ_QuantizerChannel dq_quantizer_channels[NUMCHANNELS];
	int32_t dq_history[5];
	// APP_ENVGEN.ino
	QuadEnvelopeGenerator envgen;
	int16_t preview_values[128 + 64];
	uint16_t preview_segment_starts[peaks::kMaxNumSegments];
	uint16_t preview_loop_points[peaks::kMaxNumSegments];
	settings::value_attr segment_editing_attr = { 128, 0, 255, "DOH!", NULL, settings::STORAGE_TYPE_U16 };
	int16_t fast_preview_values[peaks::kFastPreviewWidth + 32];
	// APP_H1200.ino
	H1200Settings h1200_settings;
	H1200State h1200_state;
	// APP_LORENZ.ino
	LorenzGenerator lorenz_generator;
	LorenzGeneratorState lorenz_generator_state;
	// APP_POLYLFO.ino
	PolyLfo poly_lfo;
	PolyLfoState poly_lfo_state;
	uint16_t preview_buffer[kSmallPreviewBufferSize];
	// APP_QQ.ino, APP_REFS.ino
	QuadQuantizer qq_state;
	QuantizerChannel quantizer_channels[4];
	int32_t g_history[5];
	ReferencesApp references_app;
	// OC_apps.ino
	OC::GlobalSettings global_settings;
	OC::GlobalSettingsStorage global_settings_storage;
	OC::AppData app_settings;
	OC::AppDataStorage app_data_storage;
	const OC::App* current_app = &available_apps[OC::DEFAULT_APP_INDEX];
	// OC_calibration.ino
	OC::CalibrationData g_calibration_data;
	// o_c_REV.ino
	unsigned long LAST_REDRAW_TIME = 0;
	uint_fast8_t MENU_REDRAW = true;
	OC::UiMode ui_mode = OC::UI_MODE_MENU;
	volatile bool CORE_app_isr_enabled = false;
	volatile uint32_t CORE_ticks = 0;
	// The shim's fake hardware (CoreImpl.inc).
	hw::State hw;
};

Context*& current() {
	static thread_local Context* context = nullptr;
	return context;
}

} // namespace shoal

// ---- Accessors behind the name macros and the static-member accessors ----

#define SHOAL_CONTEXT_REF(TYPE, QUALIFIED, MEMBER) \
	::shoal::Ref<TYPE> QUALIFIED() { return ::shoal::current()->MEMBER; }

// Static data members.
SHOAL_CONTEXT_REF(OC::ADC::CalibrationData*, OC::ADC::calibration_data_, ADC_calibration_data_)
SHOAL_CONTEXT_REF(uint32_t[ADC_CHANNEL_LAST], OC::ADC::raw_, ADC_raw_)
SHOAL_CONTEXT_REF(uint32_t[ADC_CHANNEL_LAST], OC::ADC::smoothed_, ADC_smoothed_)
SHOAL_CONTEXT_REF(uint32_t, OC::ADC::scan_count_, ADC_scan_count_)
SHOAL_CONTEXT_REF(OC::DAC::CalibrationData*, OC::DAC::calibration_data_, DAC_calibration_data_)
SHOAL_CONTEXT_REF(uint32_t[DAC_CHANNEL_LAST], OC::DAC::values_, DAC_values_)
SHOAL_CONTEXT_REF(uint16_t[DAC_CHANNEL_LAST][OC::DAC::kHistoryDepth], OC::DAC::history_, DAC_history_)
SHOAL_CONTEXT_REF(volatile size_t, OC::DAC::history_tail_, DAC_history_tail_)
SHOAL_CONTEXT_REF(uint8_t[DAC_CHANNEL_LAST], OC::DAC::DAC_scaling, DAC_DAC_scaling)
SHOAL_CONTEXT_REF(uint32_t, OC::DigitalInputs::clocked_mask_, DigitalInputs_clocked_mask_)
SHOAL_CONTEXT_REF(volatile uint32_t[OC::DIGITAL_INPUT_LAST], OC::DigitalInputs::clocked_, DigitalInputs_clocked_)
SHOAL_CONTEXT_REF(uint32_t, stmlib::Random::rng_state_, Random_rng_state_)
SHOAL_CONTEXT_REF(volatile bool, OC::CORE::app_isr_enabled_ref, CORE_app_isr_enabled)
SHOAL_CONTEXT_REF(volatile uint32_t, OC::CORE::ticks_ref, CORE_ticks)

// Former namespace-scope globals.
SHOAL_CONTEXT_REF(OC::Autotune_data[DAC_CHANNEL_LAST], OC::g_auto_calibration_data_ref, g_auto_calibration_data)
SHOAL_CONTEXT_REF(OC::Chord[OC::Chords::CHORDS_USER_LAST], OC::g_user_chords_ref, g_user_chords)
SHOAL_CONTEXT_REF(OC::coords[12], OC::circle_pos_lut_ref, circle_pos_lut)
SHOAL_CONTEXT_REF(uint16_t[OC::DAC::kHistoryDepth], OC::scope_history_ref, scope_history)
SHOAL_CONTEXT_REF(uint16_t[DAC_CHANNEL_LAST][OC::kScopeDepth], OC::averaged_scope_history_ref, averaged_scope_history)
SHOAL_CONTEXT_REF(size_t, OC::averaged_scope_tail_ref, averaged_scope_tail)
SHOAL_CONTEXT_REF(DAC_CHANNEL, OC::scope_update_channel_ref, scope_update_channel)
SHOAL_CONTEXT_REF(OC::Pattern[OC::Patterns::PATTERN_USER_ALL], OC::g_user_patterns_ref, g_user_patterns)
SHOAL_CONTEXT_REF(OC::Pattern, OC::dummy_pattern_ref, dummy_pattern)
SHOAL_CONTEXT_REF(OC::Scale[OC::Scales::SCALE_USER_LAST], OC::g_user_scales_ref, g_user_scales)
SHOAL_CONTEXT_REF(OC::Scale, OC::dummy_scale_ref, dummy_scale)
SHOAL_CONTEXT_REF(OC::Ui, OC::g_ui_ref, g_ui)
SHOAL_CONTEXT_REF(weegfx::Graphics, graphics_ref, graphics)
SHOAL_CONTEXT_REF(display::FrameBufferType, display::frame_buffer_ref, frame_buffer)
SHOAL_CONTEXT_REF(ASRState, asr_state_ref, asr_state)
SHOAL_CONTEXT_REF(ASR, asr_ref, asr)
SHOAL_CONTEXT_REF(uint16_t[ASR::kHistoryDepth], channel_history_ref, channel_history)
SHOAL_CONTEXT_REF(AutomatonnetzState, automatonnetz_state_ref, automatonnetz_state)
SHOAL_CONTEXT_REF(uint32_t, ticks_src1_ref, ticks_src1)
SHOAL_CONTEXT_REF(uint32_t, ticks_src2_ref, ticks_src2)
SHOAL_CONTEXT_REF(uint8_t, copy_sequence_ref, copy_sequence)
SHOAL_CONTEXT_REF(uint8_t, copy_length_ref, copy_length)
SHOAL_CONTEXT_REF(uint16_t, copy_mask_ref, copy_mask)
SHOAL_CONTEXT_REF(uint64_t, copy_timeout_ref, copy_timeout)
SHOAL_CONTEXT_REF(uint64_t[SEQ_CHANNEL_TRIGGER_NONE + 1], ext_frequency_ref, ext_frequency)
SHOAL_CONTEXT_REF(SEQ_State, seq_state_ref, seq_state)
SHOAL_CONTEXT_REF(SEQ_Channel[NUM_CHANNELS], seq_channel_ref, seq_channel)
SHOAL_CONTEXT_REF(QuadBouncingBalls, bbgen_ref, bbgen)
SHOAL_CONTEXT_REF(QuadByteBeats, bytebeatgen_ref, bytebeatgen)
SHOAL_CONTEXT_REF(uint8_t[ByteBeat::kHistoryDepth], bb_history_ref, bb_history)
SHOAL_CONTEXT_REF(ChordQuantizer, chords_state_ref, chords_state)
SHOAL_CONTEXT_REF(Chords, chords_ref, chords)
SHOAL_CONTEXT_REF(DualQuantizer, dq_state_ref, dq_state)
SHOAL_CONTEXT_REF(DQ_QuantizerChannel[NUMCHANNELS], dq_quantizer_channels_ref, dq_quantizer_channels)
SHOAL_CONTEXT_REF(int32_t[5], dq_history_ref, dq_history)
SHOAL_CONTEXT_REF(QuadEnvelopeGenerator, envgen_ref, envgen)
SHOAL_CONTEXT_REF(int16_t[128 + 64], preview_values_ref, preview_values)
SHOAL_CONTEXT_REF(uint16_t[peaks::kMaxNumSegments], preview_segment_starts_ref, preview_segment_starts)
SHOAL_CONTEXT_REF(uint16_t[peaks::kMaxNumSegments], preview_loop_points_ref, preview_loop_points)
SHOAL_CONTEXT_REF(settings::value_attr, segment_editing_attr_ref, segment_editing_attr)
SHOAL_CONTEXT_REF(int16_t[peaks::kFastPreviewWidth + 32], fast_preview_values_ref, fast_preview_values)
SHOAL_CONTEXT_REF(H1200Settings, h1200_settings_ref, h1200_settings)
SHOAL_CONTEXT_REF(H1200State, h1200_state_ref, h1200_state)
SHOAL_CONTEXT_REF(LorenzGenerator, lorenz_generator_ref, lorenz_generator)
SHOAL_CONTEXT_REF(LorenzGeneratorState, lorenz_generator_state_ref, lorenz_generator_state)
SHOAL_CONTEXT_REF(PolyLfo, poly_lfo_ref, poly_lfo)
SHOAL_CONTEXT_REF(PolyLfoState, poly_lfo_state_ref, poly_lfo_state)
SHOAL_CONTEXT_REF(uint16_t[kSmallPreviewBufferSize], preview_buffer_ref, preview_buffer)
SHOAL_CONTEXT_REF(QuadQuantizer, qq_state_ref, qq_state)
SHOAL_CONTEXT_REF(QuantizerChannel[4], quantizer_channels_ref, quantizer_channels)
SHOAL_CONTEXT_REF(int32_t[5], g_history_ref, g_history)
SHOAL_CONTEXT_REF(ReferencesApp, references_app_ref, references_app)
SHOAL_CONTEXT_REF(OC::GlobalSettings, OC::global_settings_ref, global_settings)
SHOAL_CONTEXT_REF(OC::GlobalSettingsStorage, OC::global_settings_storage_ref, global_settings_storage)
SHOAL_CONTEXT_REF(OC::AppData, OC::app_settings_ref, app_settings)
SHOAL_CONTEXT_REF(OC::AppDataStorage, OC::app_data_storage_ref, app_data_storage)
SHOAL_CONTEXT_REF(const OC::App*, OC::apps::current_app_ref, current_app)
SHOAL_CONTEXT_REF(OC::CalibrationData, OC::g_calibration_data_ref, g_calibration_data)
SHOAL_CONTEXT_REF(unsigned long, LAST_REDRAW_TIME_ref, LAST_REDRAW_TIME)
SHOAL_CONTEXT_REF(uint_fast8_t, MENU_REDRAW_ref, MENU_REDRAW)
SHOAL_CONTEXT_REF(OC::UiMode, ui_mode_ref, ui_mode)

#undef SHOAL_CONTEXT_REF

#pragma pop_macro("g_auto_calibration_data")
#pragma pop_macro("g_user_chords")
#pragma pop_macro("circle_pos_lut")
#pragma pop_macro("scope_history")
#pragma pop_macro("averaged_scope_history")
#pragma pop_macro("averaged_scope_tail")
#pragma pop_macro("scope_update_channel")
#pragma pop_macro("g_user_patterns")
#pragma pop_macro("dummy_pattern")
#pragma pop_macro("g_user_scales")
#pragma pop_macro("dummy_scale")
#pragma pop_macro("g_ui")
#pragma pop_macro("MENU_REDRAW")
#pragma pop_macro("graphics")
#pragma pop_macro("frame_buffer")
#pragma pop_macro("asr_state")
#pragma pop_macro("asr")
#pragma pop_macro("channel_history")
#pragma pop_macro("automatonnetz_state")
#pragma pop_macro("ticks_src1")
#pragma pop_macro("ticks_src2")
#pragma pop_macro("copy_sequence")
#pragma pop_macro("copy_length")
#pragma pop_macro("copy_mask")
#pragma pop_macro("copy_timeout")
#pragma pop_macro("ext_frequency")
#pragma pop_macro("seq_state")
#pragma pop_macro("seq_channel")
#pragma pop_macro("bbgen")
#pragma pop_macro("bytebeatgen")
#pragma pop_macro("bb_history")
#pragma pop_macro("chords_state")
#pragma pop_macro("chords")
#pragma pop_macro("dq_state")
#pragma pop_macro("dq_quantizer_channels")
#pragma pop_macro("dq_history")
#pragma pop_macro("envgen")
#pragma pop_macro("preview_values")
#pragma pop_macro("preview_segment_starts")
#pragma pop_macro("preview_loop_points")
#pragma pop_macro("segment_editing_attr")
#pragma pop_macro("fast_preview_values")
#pragma pop_macro("h1200_settings")
#pragma pop_macro("h1200_state")
#pragma pop_macro("lorenz_generator")
#pragma pop_macro("lorenz_generator_state")
#pragma pop_macro("poly_lfo")
#pragma pop_macro("poly_lfo_state")
#pragma pop_macro("preview_buffer")
#pragma pop_macro("qq_state")
#pragma pop_macro("quantizer_channels")
#pragma pop_macro("g_history")
#pragma pop_macro("references_app")
#pragma pop_macro("global_settings")
#pragma pop_macro("global_settings_storage")
#pragma pop_macro("app_settings")
#pragma pop_macro("app_data_storage")
#pragma pop_macro("current_app")
#pragma pop_macro("g_calibration_data")
#pragma pop_macro("LAST_REDRAW_TIME")
#pragma pop_macro("ui_mode")

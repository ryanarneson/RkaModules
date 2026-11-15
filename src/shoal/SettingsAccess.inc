// Raw access to every app's settings table, for shoal::Core::channels,
// settingCount, settingInfo, setting and setSettingRaw. Included by
// Firmware.cpp after FirmwareContext.inc and before CoreImpl.inc, so the app
// classes and the Context-backed firmware names are visible.
//
// One entry per app, in available_apps order. Each app's settings objects
// derive from settings::SettingsBase<T, N>; the helper below turns one into
// the function pointers the table needs. The table is const: every value it
// reaches lives in the current shoal::Context.
namespace shoal {
namespace {

struct AppSettings {
	int channels;   // settings objects per app (what Core::channels reports)
	int count;      // settings per object
	const settings::value_attr& (*attr)(size_t index);
	int (*get)(int channel, int index);
	bool (*set)(int channel, int index, int value);
};

// T: the settings class; N: its settings count; Obj: the object for a channel.
template <class T, int N, int Channels, T& (*Obj)(int)>
struct Entry {
	static int get(int ch, int i) { return Obj(ch).get_value((size_t) i); }
	static bool set(int ch, int i, int v) { Obj(ch).apply_value((size_t) i, v); return true; }
	static constexpr AppSettings make() {
		return AppSettings{ Channels, N, &T::value_attr, &get, &set };
	}
};

// Object accessors. `channel` is already range-checked by the caller.
ASR& asrObj(int) { return asr; }
H1200Settings& h1200Obj(int) { return h1200_settings; }
// Grid-level settings only; the per-cell settings are a later stage.
AutomatonnetzState& automatonnetzObj(int) { return automatonnetz_state; }
QuantizerChannel& qqObj(int ch) { return quantizer_channels[ch]; }
DQ_QuantizerChannel& dqObj(int ch) { return dq_quantizer_channels[ch]; }
PolyLfo& polyLfoObj(int) { return poly_lfo; }
LorenzGenerator& lorenzObj(int) { return lorenz_generator; }
EnvelopeGenerator& envObj(int ch) { return envgen.envelopes_[ch]; }
SEQ_Channel& seqObj(int ch) { return seq_channel[ch]; }
BouncingBall& bbObj(int ch) { return bbgen.balls_[ch]; }
ByteBeat& bytebeatObj(int ch) { return bytebeatgen.bytebeats_[ch]; }
Chords& chordsObj(int) { return chords; }
ReferenceChannel& refsObj(int ch) { return references_app.channels_[ch]; }

constexpr AppSettings kAppSettings[] = {
	Entry<ASR, ASR_SETTING_LAST, 1, &asrObj>::make(),
	Entry<H1200Settings, H1200_SETTING_LAST, 1, &h1200Obj>::make(),
	Entry<AutomatonnetzState, GRID_SETTING_LAST, 1, &automatonnetzObj>::make(),
	Entry<QuantizerChannel, CHANNEL_SETTING_LAST, 4, &qqObj>::make(),
	Entry<DQ_QuantizerChannel, DQ_CHANNEL_SETTING_LAST, 2, &dqObj>::make(),
	Entry<PolyLfo, POLYLFO_SETTING_LAST, 1, &polyLfoObj>::make(),
	Entry<LorenzGenerator, LORENZ_SETTING_LAST, 1, &lorenzObj>::make(),
	Entry<EnvelopeGenerator, ENV_SETTING_LAST, 4, &envObj>::make(),
	Entry<SEQ_Channel, SEQ_CHANNEL_SETTING_LAST, 2, &seqObj>::make(),
	Entry<BouncingBall, BB_SETTING_LAST, 4, &bbObj>::make(),
	Entry<ByteBeat, BYTEBEAT_SETTING_LAST, 4, &bytebeatObj>::make(),
	Entry<Chords, CHORDS_SETTING_LAST, 1, &chordsObj>::make(),
	Entry<ReferenceChannel, REF_SETTING_LAST, 4, &refsObj>::make(),
};
static_assert(sizeof(kAppSettings) / sizeof(kAppSettings[0]) == NUM_AVAILABLE_APPS,
	"one settings entry per app");

// The current app's entry; null only if the app id is unknown.
const AppSettings* currentAppSettings() {
	const int app = OC::apps::index_of(OC::global_settings.current_app_id);
	return app >= 0 && app < NUM_AVAILABLE_APPS ? &kAppSettings[app] : nullptr;
}

} // namespace
} // namespace shoal

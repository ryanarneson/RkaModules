// Settings tables of all 13 apps, and the behaviour of CopierMaschine and
// Quantermain under default settings.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "testing.hpp"
#include "test_util.hpp"

namespace {

// Per app: settings objects (channels) as in the firmware's APP_*.ino files.
const int kChannels[shoal::kNumApps] = { 1, 1, 1, 4, 2, 1, 1, 4, 2, 4, 4, 1, 4 };

// Settings whose value on a fresh core is not the table default because the
// app's Init sets it after InitDefaults.
struct InitSet { int app, channel; const char* name; int value; const char* why; };
const InitSet kInitSet[] = {
	// APP_QQ.ino:1196-1197: each channel i takes CV i and trigger TR i.
	{ 3, 1, "CV Source", 1, "APP_QQ.ino:1196 Init(CHANNEL_SOURCE_CV1 + i, ...)" },
	{ 3, 2, "CV Source", 2, "APP_QQ.ino:1196 Init(CHANNEL_SOURCE_CV1 + i, ...)" },
	{ 3, 3, "CV Source", 3, "APP_QQ.ino:1196 Init(CHANNEL_SOURCE_CV1 + i, ...)" },
	{ 3, 0, "Trigger source", 0, "APP_QQ.ino:1197 Init(..., CHANNEL_TRIGGER_TR1 + i)" },
	{ 3, 1, "Trigger source", 1, "APP_QQ.ino:1197 Init(..., CHANNEL_TRIGGER_TR1 + i)" },
	{ 3, 2, "Trigger source", 2, "APP_QQ.ino:1197 Init(..., CHANNEL_TRIGGER_TR1 + i)" },
	{ 3, 3, "Trigger source", 3, "APP_QQ.ino:1197 Init(..., CHANNEL_TRIGGER_TR1 + i)" },
	// APP_DQ.ino:1157: channel i takes CV 1 + 2i and trigger 1 + 2i (value 2i).
	{ 4, 1, "CV source", 2, "APP_DQ.ino:1157 Init(DQ_CHANNEL_SOURCE_CV1 + 2*i, ...)" },
	{ 4, 0, "trigger source", 0, "APP_DQ.ino:1157 Init(..., DQ_CHANNEL_TRIGGER_TR1 + 2*i)" },
	{ 4, 1, "trigger source", 2, "APP_DQ.ino:1157 Init(..., DQ_CHANNEL_TRIGGER_TR1 + 2*i)" },
	// APP_ENVGEN.ino:766-771, APP_BBGEN.ino:228-233, APP_BYTEBEATGEN.ino:385-390:
	// channel i takes digital input DIGITAL_INPUT_1 + i as its trigger input.
	{ 7, 1, "Trigger input", 1, "APP_ENVGEN.ino:766 Init(DIGITAL_INPUT_1 + i)" },
	{ 7, 2, "Trigger input", 2, "APP_ENVGEN.ino:766 Init(DIGITAL_INPUT_1 + i)" },
	{ 7, 3, "Trigger input", 3, "APP_ENVGEN.ino:766 Init(DIGITAL_INPUT_1 + i)" },
	{ 9, 1, "Trigger input", 1, "APP_BBGEN.ino:228 Init(DIGITAL_INPUT_1 + i)" },
	{ 9, 2, "Trigger input", 2, "APP_BBGEN.ino:228 Init(DIGITAL_INPUT_1 + i)" },
	{ 9, 3, "Trigger input", 3, "APP_BBGEN.ino:228 Init(DIGITAL_INPUT_1 + i)" },
	{ 10, 1, "Trigger input", 1, "APP_BYTEBEATGEN.ino:385 Init(DIGITAL_INPUT_1 + i)" },
	{ 10, 2, "Trigger input", 2, "APP_BYTEBEATGEN.ino:385 Init(DIGITAL_INPUT_1 + i)" },
	{ 10, 3, "Trigger input", 3, "APP_BYTEBEATGEN.ino:385 Init(DIGITAL_INPUT_1 + i)" },
};

const InitSet* initSetFor(int app, int ch, const char* name) {
	for (const InitSet& e : kInitSet)
		if (e.app == app && e.channel == ch && std::strcmp(e.name, name) == 0)
			return &e;
	return nullptr;
}

void testTables() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		shoal::Core c;
		c.selectApp(app);
		const char* an = shoal::Core::appName(app);
		CHECK(c.channels() == kChannels[app], "%s: channels %d, want %d", an, c.channels(), kChannels[app]);
		const int n = c.settingCount();
		CHECK(n > 0, "%s: settingCount %d", an, n);
		for (int i = 0; i < n; i++) {
			const shoal::SettingInfo s = c.settingInfo(i);
			CHECK(s.name != nullptr, "%s #%d: null name", an, i);
			CHECK(s.min <= s.def && s.def <= s.max, "%s #%d: min %d def %d max %d", an, i, s.min, s.def, s.max);
			for (int ch = 0; ch < c.channels(); ch++) {
				const InitSet* e = initSetFor(app, ch, s.name);
				const int want = e ? e->value : s.def;
				CHECK(c.setting(ch, i) == want, "%s ch %d '%s': fresh value %d, want %d",
					an, ch, s.name, c.setting(ch, i), want);
			}
		}
		// Every table entry in kInitSet must name a real setting.
		for (const InitSet& e : kInitSet) {
			if (e.app != app)
				continue;
			bool found = false;
			for (int i = 0; i < n; i++)
				found = found || std::strcmp(c.settingInfo(i).name, e.name) == 0;
			CHECK(found, "%s: Init-set entry '%s' names no setting", an, e.name);
		}
		for (int i = 0; i < n; i++) {
			const shoal::SettingInfo s = c.settingInfo(i);
			for (int ch = 0; ch < c.channels(); ch++) {
				CHECK(c.setSettingRaw(ch, i, s.max + 1000), "%s ch %d #%d: set high", an, ch, i);
				CHECK(c.setting(ch, i) == s.max, "%s ch %d '%s': high clamps to %d, got %d",
					an, ch, s.name, s.max, c.setting(ch, i));
				CHECK(c.setSettingRaw(ch, i, s.min - 1000), "%s ch %d #%d: set low", an, ch, i);
				CHECK(c.setting(ch, i) == s.min, "%s ch %d '%s': low clamps to %d, got %d",
					an, ch, s.name, s.min, c.setting(ch, i));
			}
		}
		CHECK(!c.setSettingRaw(c.channels(), 0, 0), "%s: channel past the end", an);
		CHECK(!c.setSettingRaw(-1, 0, 0), "%s: negative channel", an);
		CHECK(!c.setSettingRaw(0, c.settingCount(), 0), "%s: index past the end", an);
		CHECK(!c.setSettingRaw(0, -1, 0), "%s: negative index", an);
		CHECK(c.setting(c.channels(), 0) == 0 && c.setting(0, n) == 0, "%s: out-of-range reads are 0", an);
	}
}

// One clocked sample: hold `v` on CV1, let it settle, clock on TR1.
void step(shoal::Core& c, float v) {
	c.setCv(0, v);
	run(c, 0.01);
	pulse(c, 0);
	run(c, 0.005);
}

// Spec: "shifts sampled values down A, B, C, D on each clock".
void testCopierMaschineShifts() {
	shoal::Core c;
	CHECK(c.app() == 0, "boots into CopierMaschine");
	const float tol = 0.01f;
	step(c, 1.f);
	CHECK(std::fabs(c.out(0) - 1.f) < tol, "A %.3f", c.out(0));
	step(c, 2.f);
	CHECK(std::fabs(c.out(0) - 2.f) < tol && std::fabs(c.out(1) - 1.f) < tol, "A %.3f B %.3f", c.out(0), c.out(1));
	step(c, 3.f);
	CHECK(std::fabs(c.out(0) - 3.f) < tol && std::fabs(c.out(1) - 2.f) < tol
		&& std::fabs(c.out(2) - 1.f) < tol, "A %.3f B %.3f C %.3f", c.out(0), c.out(1), c.out(2));
	step(c, 4.f);
	const float four[4] = { 4.f, 3.f, 2.f, 1.f };
	for (int ch = 0; ch < 4; ch++)
		CHECK(std::fabs(c.out(ch) - four[ch]) < tol, "step 4: out %d = %.3f, want %.1f", ch, c.out(ch), four[ch]);

	// Freeze (TR2 high): no more samples are taken; each clock steps the four
	// taps through the buffer instead (manual, "hold (freeze)"). The default
	// "hold (buflen)" is 4. RingBuffer::Freeze (util/util_ringbuffer.h) starts
	// the window at its oldest sample, so tap A walks the last four samples
	// oldest first and taps B..D trail it through older history; after four
	// clocks the window wraps. Four more samples first (kept within the 9 V
	// input range, on semitones) so that history is non-zero and 0.0 V can only
	// be the input.
	const float hist[8] = { 1.f, 2.f, 3.f, 4.f, 4.5f, 5.f, 5.25f, 5.5f };
	for (int i = 4; i < 8; i++)
		step(c, hist[i]);
	c.setGate(1, 10.f);
	run(c, 0.005);
	for (int k = 0; k < 5; k++) {
		step(c, 0.f);  // the input is 0 V; a frozen buffer must not sample it
		for (int ch = 0; ch < 4; ch++) {
			const float want = hist[4 + (k % 4) - ch];
			CHECK(std::fabs(c.out(ch) - want) < tol, "frozen clock %d: out %d = %.3f, want %.2f",
				k, ch, c.out(ch), want);
		}
	}
}

// Spec: "in-scale 1 V/octave voltages for a sweep of inputs".
void testQuantermainInScale() {
	shoal::Core c;
	c.selectApp(3);
	// The scale setting, and the entry whose value name is the semitone scale.
	int scaleIdx = -1;
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, "Scale") == 0)
			scaleIdx = i;
	CHECK(scaleIdx >= 0, "found the Scale setting");
	if (scaleIdx < 0)
		return;
	const shoal::SettingInfo si = c.settingInfo(scaleIdx);
	int semi = -1;
	for (int v = si.min; v <= si.max && si.valueNames; v++)
		if (si.valueNames[v] && std::strcmp(si.valueNames[v], "Semitone") == 0)
			semi = v;
	CHECK(semi >= 0, "found the semitone scale");
	if (semi < 0)
		return;
	c.setSettingRaw(0, scaleIdx, semi);

	float prev = -1000.f;
	for (float v = -3.f; v <= 6.f; v += 0.013f) {
		c.setCv(0, v);
		run(c, 0.01);
		pulse(c, 0);
		run(c, 0.005);
		const float o = c.out(0);
		const float s = o * 12.f;
		CHECK(std::fabs(s - std::round(s)) < 0.02f, "in %.3f V: out %.4f is not on a semitone", v, o);
		CHECK(std::fabs(s - v * 12.f) <= 0.6f, "in %.3f V: out %.4f is not the nearest note", v, o);
		CHECK(o >= prev - 1e-4f, "in %.3f V: output fell from %.4f to %.4f", v, prev, o);
		prev = o;
	}
}

// Channel i's settings are its own: write min + ch to each channel of a
// multi-channel app, read all back (catches channel-0 aliasing).
void testPerChannelWrites() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		shoal::Core c;
		c.selectApp(app);
		const char* an = shoal::Core::appName(app);
		const int chs = c.channels();
		if (chs < 2)
			continue;
		int tested = 0;
		for (int i = 0; i < c.settingCount(); i++) {
			const shoal::SettingInfo s = c.settingInfo(i);
			if (s.max - s.min + 1 < chs)
				continue;
			tested++;
			for (int ch = 0; ch < chs; ch++)
				CHECK(c.setSettingRaw(ch, i, s.min + ch), "%s ch %d '%s': write", an, ch, s.name);
			for (int ch = 0; ch < chs; ch++)
				CHECK(c.setting(ch, i) == s.min + ch, "%s '%s': channel %d holds %d, want %d",
					an, s.name, ch, c.setting(ch, i), s.min + ch);
		}
		CHECK(tested > 0, "%s: a setting with %d values", an, chs);
	}
}

// An independent oracle: the o_C documentation's scale list gives Ionian as
// "0, 256, 512, 640, 896, 1152, 1408" in o_C DAC values (128 per semitone,
// 1536 per octave; reference/scales.html).
void testQuantermainIonian() {
	shoal::Core c;
	c.selectApp(3);
	int scaleIdx = -1;
	for (int i = 0; i < c.settingCount(); i++)
		if (std::strcmp(c.settingInfo(i).name, "Scale") == 0)
			scaleIdx = i;
	CHECK(scaleIdx >= 0, "found the Scale setting");
	if (scaleIdx < 0)
		return;
	const shoal::SettingInfo si = c.settingInfo(scaleIdx);
	int ionian = -1;
	for (int v = si.min; v <= si.max && si.valueNames; v++)
		if (si.valueNames[v] && std::strcmp(si.valueNames[v], "Ionian") == 0)
			ionian = v;
	CHECK(ionian >= 0, "found the Ionian scale");
	if (ionian < 0)
		return;
	c.setSettingRaw(0, scaleIdx, ionian);

	const int notes[7] = { 0, 256, 512, 640, 896, 1152, 1408 };
	bool seen[7] = {};
	float prev = -1000.f;
	for (float v = -3.f; v <= 6.f; v += 0.013f) {
		c.setCv(0, v);
		run(c, 0.01);
		pulse(c, 0);
		run(c, 0.005);
		const float o = c.out(0);
		float units = std::fmod(o * 1536.f, 1536.f);
		if (units < 0.f)
			units += 1536.f;
		bool near = false;
		for (int n = 0; n < 7; n++) {
			// Circular: the top of an octave is near its 0.
			float d = std::fabs(units - (float) notes[n]);
			d = std::fmin(d, 1536.f - d);
			if (d <= 3.f) {
				near = true;
				seen[n] = true;
			}
		}
		CHECK(near, "in %.3f V: out %.4f V = %.1f units mod 1536 is not an Ionian note", v, o, units);
		CHECK(o >= prev - 1e-4f, "in %.3f V: output fell from %.4f to %.4f", v, prev, o);
		prev = o;
	}
	for (int n = 0; n < 7; n++)
		CHECK(seen[n], "Ionian note %d never occurred", notes[n]);
}

} // namespace

int main() {
	testTables();
	testPerChannelWrites();
	testCopierMaschineShifts();
	testQuantermainInScale();
	testQuantermainIonian();
	return rkatest::finish("test_apps");
}

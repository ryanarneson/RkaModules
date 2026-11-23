#pragma once
// What Spindrift's window shows (GrainWindow, src/widgets.hpp), Rack-free:
// the view the engine publishes once a control block, the captions, and the
// geometry of the grains' dashes and the taps' ticks in the glass.
//
// The window (the spec, "The window"): the buffer as a faint waveform over
// the ring (an rka::Overview the engine keeps, SpindriftEngine::overview()),
// the ring's frames left to right in the order Beads writes them, so the
// head, an ivory line, moves from left to right and wraps; the audio just
// left of it is the newest and the audio just right of it the oldest. Each
// grain is a dash at its read point (behind the head by its position), as
// long as the buffer it still has to read, as low as its pan (left at the
// top), as bright as its envelope. In delay mode each tap is a tick at its
// delay, as tall as its gain. In the synth the window draws the voice's
// wave (synthWave()) and the grains at their wave across the bank.
//
// Threads. publish() is the engine's thread's alone; copy() any other
// thread's (the UI's), at any time. Every field is a relaxed atomic and
// publish() writes only values the window can draw (fractions held to 0..1,
// counts to the arrays), so a copy taken while a publish is under way may mix
// two blocks (250 us apart) but is always drawable. No lock, no allocation in
// either. viewCaption() and viewMode() build strings: UI thread only.
// synthWave() reads Beads' const tables and is defined in BeadsFirmware.cpp
// (the one file that compiles Beads); any thread.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>

#include "BeadsContext.hpp"
#include "Controls.hpp"

namespace spindrift {

enum Mode { MODE_GRAIN, MODE_DELAY, MODE_SYNTH };

// A fraction held to 0..1, NaN to 0.
inline float viewUnit(float v) {
	return v >= 0.f ? (v <= 1.f ? v : 1.f) : 0.f;
}

struct GrainView {
	enum { MAX = 32 };  // Beads' grains (32 in either player), and room for its 8 taps

	GrainView() {
		for (int i = 0; i < MAX; i++) {
			position_[i].store(0.f, std::memory_order_relaxed);
			span_[i].store(0.f, std::memory_order_relaxed);
			pan_[i].store(0.5f, std::memory_order_relaxed);
			env_[i].store(0.f, std::memory_order_relaxed);
			tapPosition_[i].store(0.f, std::memory_order_relaxed);
			tapGain_[i].store(0.f, std::memory_order_relaxed);
		}
	}
	GrainView(const GrainView&) = delete;
	GrainView& operator=(const GrainView&) = delete;

	// Engine's thread, once a block. `bank` is Beads' bank index (0..11);
	// `wave` the synth voice's wave across the bank, 0..1.
	void publish(const GrainSnap* g, int n, const TapSnap* t, int nt, float head, Mode m, Quality q, bool mono,
	             float seconds, int bank, bool frozen, bool pendingQuality, Quality pending, float wave = 0.f) {
		n = n < 0 ? 0 : (n > MAX ? MAX : n);
		nt = nt < 0 ? 0 : (nt > MAX ? MAX : nt);
		for (int i = 0; i < n; i++) {
			position_[i].store(viewUnit(g[i].position), std::memory_order_relaxed);
			span_[i].store(viewUnit(g[i].span), std::memory_order_relaxed);
			pan_[i].store(viewUnit(g[i].pan), std::memory_order_relaxed);
			env_[i].store(viewUnit(g[i].env), std::memory_order_relaxed);
		}
		for (int i = 0; i < nt; i++) {
			const float gain = t[i].gain;
			tapPosition_[i].store(viewUnit(t[i].position), std::memory_order_relaxed);
			tapGain_[i].store(gain >= 0.f && gain < 1e3f ? gain : 0.f, std::memory_order_relaxed);
		}
		n_.store(n, std::memory_order_relaxed);
		nt_.store(nt, std::memory_order_relaxed);
		head_.store(viewUnit(head), std::memory_order_relaxed);
		wave_.store(viewUnit(wave), std::memory_order_relaxed);
		mode_.store(m, std::memory_order_relaxed);
		quality_.store(q, std::memory_order_relaxed);
		pending_.store(pending, std::memory_order_relaxed);
		mono_.store(mono, std::memory_order_relaxed);
		frozen_.store(frozen, std::memory_order_relaxed);
		pendingQuality_.store(pendingQuality, std::memory_order_relaxed);
		seconds_.store(seconds > 0.f && seconds < 1e3f ? seconds : 0.f, std::memory_order_relaxed);
		bank_.store(bank < 0 ? 0 : (bank > 11 ? 11 : bank), std::memory_order_relaxed);
	}

	struct Copy {
		GrainSnap grains[MAX];
		int n;
		TapSnap taps[MAX];
		int nt;
		float head;
		Mode mode;
		Quality quality, pending;
		bool mono, frozen, pendingQuality;
		float seconds;
		int bank;    // Beads' index, 0..11 (shown 1..12)
		float wave;  // synth: the voice's wave across the bank, 0..1
	};

	// Any thread but the engine's.
	void copy(Copy& out) const {
		out.n = n_.load(std::memory_order_relaxed);
		out.nt = nt_.load(std::memory_order_relaxed);
		for (int i = 0; i < out.n; i++) {
			out.grains[i].position = position_[i].load(std::memory_order_relaxed);
			out.grains[i].span = span_[i].load(std::memory_order_relaxed);
			out.grains[i].pan = pan_[i].load(std::memory_order_relaxed);
			out.grains[i].env = env_[i].load(std::memory_order_relaxed);
		}
		for (int i = 0; i < out.nt; i++) {
			out.taps[i].position = tapPosition_[i].load(std::memory_order_relaxed);
			out.taps[i].gain = tapGain_[i].load(std::memory_order_relaxed);
		}
		out.head = head_.load(std::memory_order_relaxed);
		out.wave = wave_.load(std::memory_order_relaxed);
		out.mode = mode_.load(std::memory_order_relaxed);
		out.quality = quality_.load(std::memory_order_relaxed);
		out.pending = pending_.load(std::memory_order_relaxed);
		out.mono = mono_.load(std::memory_order_relaxed);
		out.frozen = frozen_.load(std::memory_order_relaxed);
		out.pendingQuality = pendingQuality_.load(std::memory_order_relaxed);
		out.seconds = seconds_.load(std::memory_order_relaxed);
		out.bank = bank_.load(std::memory_order_relaxed);
	}

private:
	std::atomic<float> position_[MAX], span_[MAX], pan_[MAX], env_[MAX];
	std::atomic<float> tapPosition_[MAX], tapGain_[MAX];
	std::atomic<int> n_{0}, nt_{0};
	std::atomic<float> head_{0.f}, wave_{0.f}, seconds_{0.f};
	std::atomic<Mode> mode_{MODE_GRAIN};
	std::atomic<Quality> quality_{DIGITAL}, pending_{DIGITAL};
	std::atomic<bool> mono_{false}, frozen_{false}, pendingQuality_{false};
	std::atomic<int> bank_{0};
};

// The qualities as the window letters them.
inline const char* qualityName(Quality q) {
	switch (q) {
		case DIRTY_TAPE: return "DIRTY TAPE";
		case CLEAN_TAPE: return "CLEAN TAPE";
		case VINTAGE: return "VINTAGE DIGITAL";
		case DIGITAL:
		default: return "DIGITAL";
	}
}

// Top left: "DIGITAL · STEREO · 4.0 S"; a quality chosen while frozen,
// "DIGITAL → DIRTY TAPE"; in the synth, which plays no buffer, the quality
// alone. UTF-8; D-DIN Condensed has the middle dot but not the arrow, which
// GrainWindow draws itself. UI thread.
inline std::string viewCaption(const GrainView::Copy& c) {
	std::string s = qualityName(c.quality);
	if (c.pendingQuality && c.pending != c.quality)
		return s + " \xE2\x86\x92 " + qualityName(c.pending);
	if (c.mode == MODE_SYNTH)
		return s;
	const float secs = c.seconds > 0.f && c.seconds < 1e3f ? c.seconds : 0.f;
	char buf[32];
	std::snprintf(buf, sizeof buf, "%.1f S", secs);
	return s + (c.mono ? " \xC2\xB7 MONO \xC2\xB7 " : " \xC2\xB7 STEREO \xC2\xB7 ") + buf;
}

// Top right, in two parts: the word ("GRAINS", "SYNTH · BANK", "DELAY") and
// its number ("23", "7", none), with the widest number the mode can show
// ("32", "12"), which sets the number's slot (layoutMode). UI thread.
struct ModeParts {
	std::string word, number, widest;
};
inline ModeParts viewModeParts(const GrainView::Copy& c) {
	ModeParts p;
	char buf[16];
	switch (c.mode) {
		case MODE_DELAY:
			p.word = "DELAY";
			break;
		case MODE_SYNTH: {
			const int bank = c.bank < 0 ? 0 : (c.bank > 11 ? 11 : c.bank);
			std::snprintf(buf, sizeof buf, "%d", bank + 1);
			p.word = "SYNTH \xC2\xB7 BANK";
			p.number = buf;
			p.widest = "12";
			break;
		}
		case MODE_GRAIN:
		default: {
			const int n = c.n < 0 ? 0 : (c.n > GrainView::MAX ? GrainView::MAX : c.n);
			std::snprintf(buf, sizeof buf, "%d", n);
			p.word = "GRAINS";
			p.number = buf;
			p.widest = "32";
			break;
		}
	}
	return p;
}

// "GRAINS 23", "DELAY", "SYNTH · BANK 7". UI thread.
inline std::string viewMode(const GrainView::Copy& c) {
	const ModeParts p = viewModeParts(c);
	return p.number.empty() ? p.word : p.word + " " + p.number;
}

// Where the mode caption goes, right-aligned at `right`: the number
// right-aligned in a slot as wide as the mode's widest number, and the word
// `gap` left of the slot. The word's place depends only on the mode, never
// on the number, so a count that changes does not move it (2.0.29: the
// caption, drawn right-aligned as one string, shook with every count).
// `width(text)` measures text in the face.
struct ModeLayout {
	float wordRight, numberRight;
};
template <class Width>
ModeLayout layoutMode(const ModeParts& p, float right, float gap, Width width) {
	ModeLayout l;
	l.numberRight = right;
	if (p.number.empty()) {
		l.wordRight = right;
		return l;
	}
	float slot = width(p.widest);
	const float own = width(p.number);  // a face whose digits outdo the widest's
	if (own > slot)
		slot = own;
	l.wordRight = right - slot - gap;
	return l;
}

// The grain count as the window shows it, calm enough to read: the most
// grains sounding over each PERIOD seconds, shown from that period's end
// to the next's (the count itself changes every block). The first count,
// and any after the clock steps back, shows at once. UI thread, one per
// window; `now` in seconds.
struct CountHold {
	static float period() {
		return 0.2f;
	}
	int update(int n, double now) {
		if (shown_ < 0 || now < start_) {
			shown_ = n;
			peak_ = -1;
			start_ = now;
			return shown_;
		}
		if (n > peak_)
			peak_ = n;
		if (now - start_ >= period()) {
			shown_ = peak_;
			peak_ = -1;
			start_ = now;
		}
		return shown_;
	}

private:
	int shown_ = -1, peak_ = -1;
	double start_ = 0.0;
};

// A grain's dash in a glass w wide and h high (x from the left, y from the
// top): x0..x1 at height y, drawn at `alpha` of its colour.
struct Dash {
	float x0, x1, y, alpha;
};
// A dash that passes the ring's end goes on from its start: two per grain.
constexpr int kMaxDashes = 2 * GrainView::MAX;
// The least dash, as a fraction of the width (a grain with no buffer left
// to read, and the synth's, whose grains have no span).
constexpr float kMinDash = 0.01f;
// Below this envelope a grain is not drawn.
constexpr float kMinEnv = 1.f / 256.f;

// The place in the ring, 0..1, of a point `position` behind the head.
inline float ringPlace(float head, float position) {
	float x = viewUnit(head) - viewUnit(position);
	if (x < 0.f)
		x += 1.f;
	return x < 1.f ? x : 0.f;
}

// The dashes of the copy's grains into out[kMaxDashes]; returns how many.
inline int grainDashes(const GrainView::Copy& c, float w, float h, Dash* out) {
	const int n = c.n < 0 ? 0 : (c.n > GrainView::MAX ? GrainView::MAX : c.n);
	int k = 0;
	for (int i = 0; i < n; i++) {
		const GrainSnap& g = c.grains[i];
		const float alpha = viewUnit(g.env);
		if (!(alpha >= kMinEnv))
			continue;
		const float span = viewUnit(g.span);
		const float len = span > kMinDash ? span : kMinDash;
		const float y = viewUnit(g.pan) * h;
		if (c.mode == MODE_SYNTH) {
			float x0 = viewUnit(g.position);
			if (x0 > 1.f - len)
				x0 = 1.f - len;
			const float x1 = x0 + len < 1.f ? x0 + len : 1.f;
			out[k++] = {x0 * w, x1 * w, y, alpha};
			continue;
		}
		const float x0 = ringPlace(c.head, g.position), x1 = x0 + len;
		if (x1 <= 1.f) {
			out[k++] = {x0 * w, x1 * w, y, alpha};
		}
		else {
			out[k++] = {x0 * w, w, y, alpha};
			out[k++] = {0.f, (x1 - 1.f) * w, y, alpha};
		}
	}
	return k;
}

// A tap's tick: at x, from y0 (the bottom) up to y1.
struct Tick {
	float x, y0, y1;
};

// The ticks of the copy's taps (delay mode only) into out[GrainView::MAX].
inline int tapTicks(const GrainView::Copy& c, float w, float h, Tick* out) {
	if (c.mode != MODE_DELAY)
		return 0;
	const int n = c.nt < 0 ? 0 : (c.nt > GrainView::MAX ? GrainView::MAX : c.nt);
	int k = 0;
	for (int i = 0; i < n; i++) {
		const float gain = viewUnit(c.taps[i].gain);
		if (!(gain > 0.f))
			continue;
		out[k++] = {ringPlace(c.head, c.taps[i].position) * w, h, h - gain * h};
	}
	return k;
}

// The synth voice's wave as the window draws it: `n` points over one cycle
// of bank `bank` (0..11) at `wave` (0..1 across it), the two neighbouring
// waves blended as the voice blends them (wavetable_voice.h), each the slope
// of Beads' integrated table as the voice differentiates it, scaled so its
// peak is 1. Returns n. Defined in BeadsFirmware.cpp.
int synthWave(int bank, float wave, float* out, int n);

}  // namespace spindrift

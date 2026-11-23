// Spindrift's window, Rack-free (GrainView.hpp): the captions, the geometry
// of the grains' dashes and the taps' ticks, and the synth's wave as the
// window draws it; and the waveform the engine keeps for it. Built with the
// sanitizers.
#include <cmath>
#include <cstdio>
#include <string>

#include "testing.hpp"
#include "GrainView.hpp"
#include "SpindriftEngine.hpp"

using spindrift::Dash;
using spindrift::GrainView;
using spindrift::SpindriftEngine;
using spindrift::Tick;

namespace {

GrainView::Copy blank() {
	GrainView::Copy c = {};
	c.mode = spindrift::MODE_GRAIN;
	c.quality = spindrift::DIGITAL;
	c.pending = spindrift::DIGITAL;
	c.seconds = 4.f;
	return c;
}

bool near(float a, float b) {
	return std::fabs(a - b) < 1e-4f;
}

void captions() {
	struct Case {
		spindrift::Quality q;
		bool mono;
		float seconds;
		const char* want;
	};
	const Case cases[] = {
	    {spindrift::DIGITAL, false, 4.f, "DIGITAL \xC2\xB7 STEREO \xC2\xB7 4.0 S"},
	    {spindrift::DIGITAL, true, 8.f, "DIGITAL \xC2\xB7 MONO \xC2\xB7 8.0 S"},
	    {spindrift::VINTAGE, false, 8.f, "VINTAGE DIGITAL \xC2\xB7 STEREO \xC2\xB7 8.0 S"},
	    {spindrift::CLEAN_TAPE, false, 10.6667f, "CLEAN TAPE \xC2\xB7 STEREO \xC2\xB7 10.7 S"},
	    {spindrift::DIRTY_TAPE, true, 32.f, "DIRTY TAPE \xC2\xB7 MONO \xC2\xB7 32.0 S"},
	    {spindrift::DIGITAL, false, 3.96f, "DIGITAL \xC2\xB7 STEREO \xC2\xB7 4.0 S"},     // one decimal, rounded
	    {spindrift::DIGITAL, false, NAN, "DIGITAL \xC2\xB7 STEREO \xC2\xB7 0.0 S"},       // never "nan"
	};
	for (const Case& k : cases) {
		GrainView::Copy c = blank();
		c.quality = c.pending = k.q;
		c.mono = k.mono;
		c.seconds = k.seconds;
		const std::string got = spindrift::viewCaption(c);
		CHECK(got == k.want, "caption: got \"%s\", want \"%s\"", got.c_str(), k.want);
	}

	// A quality chosen while frozen: the spec's example.
	GrainView::Copy c = blank();
	c.pendingQuality = true;
	c.pending = spindrift::DIRTY_TAPE;
	std::string got = spindrift::viewCaption(c);
	CHECK(got == "DIGITAL \xE2\x86\x92 DIRTY TAPE", "pending: got \"%s\"", got.c_str());

	// The synth plays no buffer: the quality alone.
	c = blank();
	c.mode = spindrift::MODE_SYNTH;
	c.quality = c.pending = spindrift::CLEAN_TAPE;
	got = spindrift::viewCaption(c);
	CHECK(got == "CLEAN TAPE", "synth caption: got \"%s\"", got.c_str());

	// The mode, top right.
	c = blank();
	c.n = 23;
	got = spindrift::viewMode(c);
	CHECK(got == "GRAINS 23", "grains: got \"%s\"", got.c_str());
	c.n = 0;
	got = spindrift::viewMode(c);
	CHECK(got == "GRAINS 0", "no grains: got \"%s\"", got.c_str());
	c.mode = spindrift::MODE_DELAY;
	got = spindrift::viewMode(c);
	CHECK(got == "DELAY", "delay: got \"%s\"", got.c_str());
	c.mode = spindrift::MODE_SYNTH;
	c.bank = 6;  // Beads' bank index; banks read 1..12
	got = spindrift::viewMode(c);
	CHECK(got == "SYNTH \xC2\xB7 BANK 7", "synth: got \"%s\"", got.c_str());
	c.bank = 0;
	got = spindrift::viewMode(c);
	CHECK(got == "SYNTH \xC2\xB7 BANK 1", "first bank: got \"%s\"", got.c_str());
}

void dashes() {
	const float w = 320.f, h = 40.f;
	Dash d[spindrift::kMaxDashes];

	// The brief's grain: at 0.5 behind a head at 0, span 0.1, pan 0, env 1.
	GrainView::Copy c = blank();
	c.n = 1;
	c.grains[0] = {0.5f, 0.1f, 0.f, 1.f};
	int n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 1, "one dash (%d)", n);
	CHECK(near(d[0].x0, w / 2) && near(d[0].x1, w / 2 + 0.1f * w), "x %g..%g", d[0].x0, d[0].x1);
	CHECK(near(d[0].y, 0.f), "pan 0 at the top (%g)", d[0].y);
	CHECK(near(d[0].alpha, 1.f), "env 1 is full (%g)", d[0].alpha);

	// Envelope 0: nothing drawn.
	c.grains[0].env = 0.f;
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 0, "env 0 draws nothing (%d)", n);

	// Pan and envelope.
	c.grains[0] = {0.5f, 0.1f, 1.f, 0.25f};
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 1 && near(d[0].y, h) && near(d[0].alpha, 0.25f), "pan 1 at the bottom, alpha the env (%g, %g)", d[0].y,
	      d[0].alpha);
	c.grains[0].pan = 0.5f;
	spindrift::grainDashes(c, w, h, d);
	CHECK(near(d[0].y, h / 2), "pan 0.5 in the middle (%g)", d[0].y);

	// The read point is behind the head: head 0.7, 0.5 behind it, at 0.2.
	c.head = 0.7f;
	c.grains[0] = {0.5f, 0.1f, 0.f, 1.f};
	spindrift::grainDashes(c, w, h, d);
	CHECK(near(d[0].x0, 0.2f * w) && near(d[0].x1, 0.3f * w), "behind the head: %g..%g", d[0].x0, d[0].x1);

	// A dash that passes the ring's end goes on from its start.
	c.head = 0.f;
	c.grains[0] = {0.05f, 0.1f, 0.f, 1.f};
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 2, "split in two (%d)", n);
	if (n == 2) {
		CHECK(near(d[0].x0, 0.95f * w) && near(d[0].x1, w), "first piece %g..%g", d[0].x0, d[0].x1);
		CHECK(near(d[1].x0, 0.f) && near(d[1].x1, 0.05f * w), "second piece %g..%g", d[1].x0, d[1].x1);
	}

	// A grain with no span left is still seen: the least dash.
	c.grains[0] = {0.5f, 0.f, 0.f, 1.f};
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 1 && near(d[0].x1 - d[0].x0, spindrift::kMinDash * w), "least dash (%g)", d[0].x1 - d[0].x0);

	// Synth: at the wave across the bank, whatever the head; held to the glass.
	c.mode = spindrift::MODE_SYNTH;
	c.head = 0.6f;
	c.grains[0] = {0.25f, 0.f, 0.f, 1.f};
	spindrift::grainDashes(c, w, h, d);
	CHECK(near(d[0].x0, 0.25f * w), "synth grain at its wave (%g)", d[0].x0);
	c.grains[0] = {1.f, 0.f, 0.f, 1.f};
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 1 && d[0].x1 <= w && near(d[0].x1 - d[0].x0, spindrift::kMinDash * w), "synth's last wave inside (%g..%g)",
	      d[0].x0, d[0].x1);

	// Thirty-two grains all passing the end: every dash fits.
	c = blank();
	c.n = 32;
	for (int i = 0; i < 32; i++)
		c.grains[i] = {0.01f, 1.f, 0.f, 1.f};
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n == 64, "32 grains split: 64 dashes (%d)", n);

	// A hostile copy: n out of range and NaNs draw nothing wild.
	c = blank();
	c.n = 99;
	for (int i = 0; i < 32; i++)
		c.grains[i] = {NAN, NAN, NAN, 1.f};
	n = spindrift::grainDashes(c, w, h, d);
	CHECK(n <= spindrift::kMaxDashes, "held to the array (%d)", n);
	for (int i = 0; i < n; i++)
		CHECK(d[i].x0 >= 0.f && d[i].x1 <= w && d[i].x0 <= d[i].x1 && d[i].y >= 0.f && d[i].y <= h,
		      "dash %d in the glass", i);
}

void ticks() {
	const float w = 320.f, h = 40.f;
	Tick t[GrainView::MAX];
	GrainView::Copy c = blank();
	c.mode = spindrift::MODE_DELAY;
	c.nt = 2;
	c.taps[0] = {0.25f, 0.5f};
	c.taps[1] = {0.1f, 0.f};  // silent: not drawn
	int n = spindrift::tapTicks(c, w, h, t);
	CHECK(n == 1, "one tick (%d)", n);
	CHECK(near(t[0].x, 0.75f * w) && near(t[0].y0, h) && near(t[0].y1, h / 2), "tick at %g, %g..%g", t[0].x, t[0].y0,
	      t[0].y1);
	c.taps[0] = {0.25f, 3.f};
	spindrift::tapTicks(c, w, h, t);
	CHECK(near(t[0].y1, 0.f), "a gain over 1 is held to the glass (%g)", t[0].y1);
	c.mode = spindrift::MODE_GRAIN;
	CHECK(spindrift::tapTicks(c, w, h, t) == 0, "no ticks outside delay mode");
}

void synthWave() {
	float y[128];
	for (int bank = 0; bank < 12; bank += 11) {
		for (float wave = 0.f; wave <= 1.f; wave += 0.37f) {
			const int n = spindrift::synthWave(bank, wave, y, 128);
			CHECK(n == 128, "points (%d)", n);
			float peak = 0.f;
			bool finite = true;
			for (int i = 0; i < n; i++) {
				finite &= std::isfinite(y[i]);
				peak = std::fmax(peak, std::fabs(y[i]));
			}
			CHECK(finite && near(peak, 1.f), "bank %d wave %g: finite, peak 1 (%g)", bank, wave, peak);
		}
	}
	// Out of range: held, never read past the tables.
	CHECK(spindrift::synthWave(99, 7.f, y, 128) == 128 && std::isfinite(y[0]), "held to the banks");
}

// The waveform the engine keeps (SpindriftEngine::overview()): 1 s of a
// 5 V sine (half Beads' full scale) recorded, then frozen while the overview
// catches up. The columns the head has passed hold the sine's peaks, the
// ring ahead of it (never written) is silent, and the view's head is where
// the overview's columns say the recording stopped. Stereo Digital is 4 s of
// ring, mono 8 s.
void waveform() {
	for (int mono = 0; mono < 2; mono++) {
		SpindriftEngine e(0x5eed);
		CHECK(e.allocate(), "allocated");
		e.setSampleRate(48000.f);
		spindrift::Knobs k = {};
		k.time = k.size = k.shape = k.pitch = k.density = 0.5f;
		k.timeAtt = k.sizeAtt = k.shapeAtt = k.pitchAtt = 0.5f;
		spindrift::Jacks j = {};
		j.inLOn = true;
		j.inROn = !mono;
		spindrift::Out o;
		for (long n = 0; n < 48000 + 4800; n++) {
			if (n == 48000)
				e.buttons().freezePress();
			j.inL = j.inR = 5.f * std::sin(2.f * 3.14159265f * 1000.f * float(n) / 48000.f);
			e.process(k, j, o);
		}
		GrainView::Copy c;
		e.view().copy(c);
		const float ringSeconds = mono ? 8.f : 4.f;
		CHECK(near(c.seconds, ringSeconds) && c.frozen && c.mono == bool(mono), "%s: %g s, frozen", mono ? "mono" : "stereo",
		      c.seconds);
		const float recorded = 1.f / ringSeconds;  // of the ring
		CHECK(std::fabs(c.head - recorded) < 0.01f, "%s: head %g after %g of the ring", mono ? "mono" : "stereo", c.head,
		      recorded);
		const rka::Overview& ov = e.overview();
		const int N = rka::Overview::COLS;
		int loud = 0, quiet = 0, wrong = 0;
		for (int col = 0; col < N; col++) {
			const float place = (col + 0.5f) / N;
			const float lo = ov.lo[col].load(), hi = ov.hi[col].load();
			if (place > 0.01f && place < recorded - 0.01f) {
				loud++;
				if (!(std::fabs(hi - 0.5f) < 0.02f && std::fabs(lo + 0.5f) < 0.02f)) {
					if (!wrong++)
						std::printf("column %d: %g..%g\n", col, lo, hi);
				}
			}
			else if (place > recorded + 0.01f) {
				quiet++;
				if (!(lo == 0.f && hi == 0.f) && !wrong++)
					std::printf("column %d: %g..%g\n", col, lo, hi);
				wrong += !(lo == 0.f && hi == 0.f);
			}
		}
		CHECK(loud > 0 && quiet > 0 && wrong == 0, "%s: %d loud, %d silent columns; %d wrong", mono ? "mono" : "stereo", loud,
		      quiet, wrong);
	}
}

// The mode caption, top right, stays put (2.0.29: it shook). The word sits
// at a place fixed by the widest number the mode can show, so the count
// changing ("GRAINS 1", "GRAINS 28") never moves it, in a face whose digits
// differ in width (a '1' narrower than the rest, as most faces).
void modeCaption() {
	auto width = [](const std::string& t) {
		float w = 0.f;
		for (char ch : t)
			w += ch == '1' ? 1.f : (ch >= '0' && ch <= '9' ? 2.f : 1.5f);
		return w;
	};
	const float right = 100.f, gap = 1.f;
	GrainView::Copy c = blank();
	float wordRight = -1.f;
	for (int n = 0; n <= GrainView::MAX; n++) {
		c.n = n;
		const spindrift::ModeParts p = spindrift::viewModeParts(c);
		CHECK(p.word == "GRAINS", "word (%s)", p.word.c_str());
		CHECK(p.word + " " + p.number == spindrift::viewMode(c), "the parts are the caption (%d)", n);
		const spindrift::ModeLayout l = spindrift::layoutMode(p, right, gap, width);
		if (n == 0)
			wordRight = l.wordRight;
		CHECK(l.wordRight == wordRight, "GRAINS %d: the word stays at %g (%g)", n, wordRight, l.wordRight);
		CHECK(l.numberRight == right, "the number right-aligned in its slot (%g)", l.numberRight);
		CHECK(right - width(p.number) >= l.wordRight + gap, "GRAINS %d: the number clear of the word", n);
	}
	c.mode = spindrift::MODE_SYNTH;
	for (int bank = 0; bank < 12; bank++) {
		c.bank = bank;
		const spindrift::ModeParts p = spindrift::viewModeParts(c);
		CHECK(p.word == "SYNTH \xC2\xB7 BANK" && p.word + " " + p.number == spindrift::viewMode(c), "synth parts (%s)",
		      p.word.c_str());
		const spindrift::ModeLayout l = spindrift::layoutMode(p, right, gap, width);
		if (bank == 0)
			wordRight = l.wordRight;
		CHECK(l.wordRight == wordRight, "BANK %d: the word stays (%g)", bank + 1, l.wordRight);
		CHECK(right - width(p.number) >= l.wordRight + gap, "BANK %d: the number clear of the word", bank + 1);
	}
	c.mode = spindrift::MODE_DELAY;
	const spindrift::ModeParts p = spindrift::viewModeParts(c);
	const spindrift::ModeLayout l = spindrift::layoutMode(p, right, gap, width);
	CHECK(p.word == "DELAY" && p.number.empty() && l.wordRight == right, "delay: the word alone, at the edge");
}

// The count shown is calm (2.0.29: it changed every frame): the most grains
// sounding over each 0.2 s, shown from the end of that 0.2 s until the end
// of the next; the first count at once.
void countHold() {
	spindrift::CountHold hold;
	CHECK(hold.update(5, 10.0) == 5, "the first count at once");
	// A count that flickers every frame (60 fps) changes what is shown at
	// most five times a second, and what is shown is each window's peak.
	int changes = 0, shown = 5;
	for (int f = 1; f <= 60; f++) {
		const double t = 10.0 + f / 60.0;
		const int n = (f % 2) ? 3 : 9 + f % 5;  // 3, 9..13 alternating
		const int s = hold.update(n, t);
		changes += s != shown;
		shown = s;
	}
	CHECK(changes <= 5, "at most 5 changes in 1 s (%d)", changes);
	CHECK(shown >= 9, "shows the peak, not the dip (%d)", shown);
	// A steady count shows within one period.
	int s = 0;
	for (int f = 1; f <= 30; f++)
		s = hold.update(20, 11.0 + f / 60.0);
	CHECK(s == 20, "a steady 20 shows (%d)", s);
	// Back to 0 (nothing sounding) within two periods.
	for (int f = 1; f <= 30; f++)
		s = hold.update(0, 11.5 + f / 60.0);
	CHECK(s == 0, "silence shows 0 (%d)", s);
	// A clock that steps back (a new session) shows the count at once.
	CHECK(hold.update(7, 1.0) == 7, "time going back restarts the hold");
}

}  // namespace

int main() {
	captions();
	dashes();
	ticks();
	synthWave();
	waveform();
	modeCaption();
	countHold();
	return rkatest::finish("spindrift test_view");
}

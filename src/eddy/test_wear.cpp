// Eddy's wear and the write path's guards (src/eddy/Wear.hpp) against the numbers in the
// plan's Global Constraints. Built with the plugin's floating-point flags.
#include "eddy/Wear.hpp"
#include "hadal/Random.hpp"
#include "testing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace rka::eddy;

const float NAN_F = std::nanf("");
const float INF_F = INFINITY;
const double PI = 3.14159265358979323846;

struct Tape {
	std::vector<float> ch[2];
	explicit Tape(int n) { ch[0].assign(n, 0.f); ch[1].assign(n, 0.f); }
	int size() const { return (int) ch[0].size(); }
	// One pass of the head over the whole tape, one frame per call.
	void pass(Wear& w) {
		for (int i = 0; i < size(); i++)
			for (int c = 0; c < 2; c++)
				ch[c][i] = w.process(c, ch[c][i], 1.f);
	}
	double rms() const {
		double s = 0;
		for (int c = 0; c < 2; c++)
			for (float v : ch[c])
				s += (double) v * v;
		return std::sqrt(s / (2.0 * size()));
	}
	float peak() const {
		float p = 0.f;
		for (int c = 0; c < 2; c++)
			for (float v : ch[c])
				p = std::max(p, std::fabs(v));
		return p;
	}
	// Spectral centroid of side 0, in bins of a size()-point DFT.
	double centroid() const {
		const int n = size();
		std::vector<double> co(n), si(n);
		for (int i = 0; i < n; i++) {
			co[i] = std::cos(2 * PI * i / n);
			si[i] = std::sin(2 * PI * i / n);
		}
		double num = 0, den = 0;
		for (int k = 1; k < n / 2; k++) {
			double re = 0, im = 0;
			for (int i = 0; i < n; i++) {
				const int j = (int) (((int64_t) k * i) % n);
				re += ch[0][i] * co[j];
				im -= ch[0][i] * si[j];
			}
			const double m = std::sqrt(re * re + im * im);
			num += k * m;
			den += m;
		}
		return den > 0 ? num / den : 0.0;
	}
};

void noise(Tape& t, uint32_t seed, float amp) {
	rka::hadal::Random r;
	r.seed(seed);
	for (int c = 0; c < 2; c++)
		for (float& v : t.ch[c])
			v = amp * r.uniform();
}

void off() {
	Wear w(1);
	CHECK(!w.active(), "a new wear is off");
	w.set(0.f);
	CHECK(!w.active(), "amount 0 is off");
	w.set(0.5f);
	CHECK(w.active(), "amount 0.5 is on");
	w.set(0.f);
	CHECK(!w.active(), "back to 0 is off");
	w.set(NAN_F);
	CHECK(!w.active(), "NaN reads 0");
	w.set(-1.f);
	CHECK(!w.active(), "negative reads 0");
	w.set(5.f);
	CHECK(w.active(), "above 1 reads 1");
}

void never_grows() {
	const float amounts[3] = {0.1f, 0.5f, 1.f};
	for (float a : amounts) {
		Wear w(3);
		w.set(a);
		Tape t(1024);
		noise(t, 11, 4.f);   // 2.3 V RMS: far above the hiss
		t.pass(w);
		double prev = t.rms();
		const float peak1 = t.peak();
		bool falls = true, peakOk = true;
		int bad = -1;
		for (int p = 2; p <= 2000; p++) {
			t.pass(w);
			const double r = t.rms();
			// While the signal is well above the hiss floor (5 mV RMS at 1).
			if (prev > 0.1 && r > prev * (1.0 + 1e-6)) {
				falls = false;
				if (bad < 0) bad = p;
			}
			peakOk = peakOk && t.peak() <= peak1 + 1e-6f;
			prev = r;
		}
		CHECK(falls, "amount %g: RMS rose at pass %d", a, bad);
		CHECK(peakOk, "amount %g: the peak went over the first pass's %g", a, peak1);
	}
}

void rate_follows_knob() {
	const float amounts[3] = {0.1f, 0.5f, 1.f};
	double level[3], cent[3];
	for (int k = 0; k < 3; k++) {
		Wear w(5);
		w.set(amounts[k]);
		Tape t(1024);
		noise(t, 21, 3.f);
		for (int p = 0; p < 50; p++)
			t.pass(w);
		level[k] = t.rms();
		cent[k] = t.centroid();
	}
	CHECK(level[2] < level[1] && level[1] < level[0], "level after 50 passes: %g, %g, %g", level[0], level[1], level[2]);
	CHECK(cent[2] < cent[1] && cent[1] < cent[0], "centroid after 50 passes: %g, %g, %g", cent[0], cent[1], cent[2]);

	// The level per pass on a 100 Hz sine, far under the 4 kHz low-pass: the
	// median over 10 ms chunks (one cycle each) leaves out the dropouts, and
	// the hiss is far under the signal.
	const float fs = 48000.f;
	const int n = 96000, chunk = 480;
	for (int k = 1; k < 3; k++) {
		const float a = amounts[k];
		Wear w(9);
		w.set(a);
		std::vector<double> ratios;
		double ei = 0, eo = 0;
		for (int i = 0; i < n; i++) {
			const float x = 2.f * (float) std::sin(2 * PI * 100.0 * i / fs);
			w.process(1, x, 1.f);   // side 1 unused here
			const float y = w.process(0, x, 1.f);
			ei += (double) x * x;
			eo += (double) y * y;
			if ((i + 1) % chunk == 0) {
				ratios.push_back(10.0 * std::log10(eo / ei));
				ei = eo = 0;
			}
		}
		// Skip the first chunk, where the low-pass starts from zero.
		std::sort(ratios.begin() + 1, ratios.end());
		const double med = ratios[1 + (ratios.size() - 1) / 2];
		const double want = -0.5 * a * a;
		CHECK(std::fabs(med - want) < (a == 1.f ? 0.2 : 0.05), "amount %g: %g dB per pass (want %g)", a, med, want);
	}
}

void hiss_bounded() {
	Wear w(13);
	w.set(1.f);
	Tape t(2048);
	for (int p = 0; p < 5000; p++)
		t.pass(w);
	const double limit = 5.0 * std::pow(10.0, -40.0 / 20.0);   // -40 dB re 5 V
	for (int c = 0; c < 2; c++) {
		double s = 0, m = 0;
		for (float v : t.ch[c]) {
			s += (double) v * v;
			m += v;
		}
		const double r = std::sqrt(s / t.size()), dc = m / t.size();
		CHECK(r < limit, "side %d: hiss builds to %g V RMS (limit %g)", c, r, limit);
		CHECK(std::fabs(dc) < 1e-3, "side %d: DC %g V", c, dc);
		CHECK(r > 1e-4, "side %d: some hiss is there (%g V)", c, r);
	}
}

struct Dip { int len; float depth, slope; };

// Runs a constant 4 V through process, `frames` of tape per call, for
// `seconds` of tape; returns the gain seen on each side for each call.
void dipGains(Wear& w, float fs, float frames, double seconds, std::vector<float> g[2]) {
	const float base = 4.f * (float) std::pow(10.0, -0.5 / 20.0);   // 4 V through the level step
	const int calls = (int) (seconds * fs / frames);
	g[0].reserve(calls);
	g[1].reserve(calls);
	for (int i = 0; i < calls; i++)
		for (int c = 0; c < 2; c++)
			g[c].push_back(w.process(c, 4.f, frames) / base);
}

int countDips(const std::vector<float>& g, std::vector<Dip>* out) {
	int count = 0;
	const int n = (int) g.size();
	int i = 2000;   // past the low-pass settling
	while (i < n) {
		if (g[i] >= 0.99f) {
			i++;
			continue;
		}
		const int start = i;
		float mn = 1.f;
		while (i < n && g[i] < 0.99f) {
			mn = std::min(mn, g[i]);
			i++;
		}
		if (i >= n)
			break;   // cut off by the end
		count++;
		if (out) {
			float slope = 0.f;   // steepest mean slope over 8 calls
			for (int j = std::max(start - 4, 8); j < i + 4 && j < n; j++)
				slope = std::max(slope, std::fabs(g[j] - g[j - 8]) / 8.f);
			out->push_back({i - start, 1.f - mn, slope});
		}
	}
	return count;
}

void dropouts() {
	const float fs = 16000.f;
	Wear w(17);
	w.setSampleRate(fs);
	w.set(1.f);
	std::vector<float> g[2];
	dipGains(w, fs, 1.f, 600.0, g);   // 600 s of tape
	std::vector<Dip> dips;
	const int n = countDips(g[0], &dips);
	CHECK(n >= 150 && n <= 450, "%d dips in 600 s (want 150 to 450)", n);
	int badLen = 0, badDepth = 0, badEdge = 0;
	for (const Dip& d : dips) {
		const float ms = 1000.f * (float) d.len / fs;
		if (ms < 4.7f || ms > 40.3f) badLen++;
		if (d.depth < 0.19f || d.depth > 0.71f) badDepth++;   // gain 0.3 to 0.8, so 0.2 to 0.7 down
		// An edge takes depth / 2 ms at the slowest slope: none faster.
		if (d.slope > d.depth / (0.002f * fs) * 1.15f) badEdge++;
	}
	CHECK(badLen == 0, "%d dips outside 5 to 40 ms", badLen);
	CHECK(badDepth == 0, "%d dips outside gain 0.3 to 0.8", badDepth);
	CHECK(badEdge == 0, "%d dips with an edge faster than 2 ms", badEdge);
	// Both sides dip together.
	float worst = 0.f;
	for (size_t i = 0; i < g[0].size(); i++)
		worst = std::max(worst, std::fabs(g[0][i] - g[1][i]));
	CHECK(worst < 3e-3f, "the sides differ by %g in gain", worst);

	// The clock counts tape passed, not calls: 4 frames a call gives the same rate.
	Wear w4(17);
	w4.set(1.f);
	std::vector<float> h[2];
	dipGains(w4, 48000.f, 4.f, 150.0, h);
	const int n4 = countDips(h[0], nullptr);
	CHECK(n4 >= 35 && n4 <= 115, "%d dips in 150 s at 4 frames a call (want 35 to 115)", n4);

	// And the rate follows the knob: a quarter of the dips at half the amount.
	Wear wh(17);
	wh.setSampleRate(fs);
	wh.set(0.5f);
	std::vector<float> k[2];
	dipGains(wh, fs, 1.f, 600.0, k);
	const int nh = countDips(k[0], nullptr);
	CHECK(nh >= 40 && nh <= 120, "%d dips at amount 0.5 (want about 75)", nh);
}

void seeded() {
	Wear a(7), b(7), c(8);
	a.set(1.f);
	b.set(1.f);
	c.set(1.f);
	rka::hadal::Random r;
	r.seed(1);
	bool same = true, differs = false;
	for (int i = 0; i < 400000; i++) {
		const float x = 3.f * r.uniform();
		const float fr = 1.f + (float) (i % 3);
		for (int ch = 0; ch < 2; ch++) {
			const float ya = a.process(ch, x, fr), yb = b.process(ch, x, fr), yc = c.process(ch, x, fr);
			same = same && ya == yb;
			differs = differs || ya != yc;
		}
	}
	CHECK(same, "one seed gives one result");
	CHECK(differs, "two seeds differ");
	// The hiss differs between sides.
	Wear d(7);
	d.set(1.f);
	bool sides = false;
	for (int i = 0; i < 100; i++)
		sides = sides || d.process(0, 0.f, 1.f) != d.process(1, 0.f, 1.f);
	CHECK(sides, "the two sides have their own hiss");
}

void guard() {
	const float fs = 48000.f;
	// The dub's input block: unity in its passband (a 1 kHz sine within
	// 0.005 dB, 30 Hz within 0.15 dB: a one-pole at 5 Hz), 1 V of DC under
	// 1 mV in 2 s, and the sides do not share a state.
	for (double hz : {1000.0, 30.0}) {
		InputDcBlock g;
		g.setSampleRate(fs);
		double ei = 0, eo = 0;
		for (int i = 0; i < 96000; i++) {
			const float x = 5.f * (float) std::sin(2 * PI * hz * i / fs);
			const float y = g.process(0, x);
			if (i >= 48000) {
				ei += (double) x * x;
				eo += (double) y * y;
			}
		}
		const double db = 10.0 * std::log10(eo / ei);
		CHECK(hz == 1000.0 ? std::fabs(db) < 0.005 : db < 0.0 && db > -0.15, "the input's block: a %.0f Hz sine at 5 V changes by %g dB", hz, db);
	}
	{
		InputDcBlock g;
		g.setSampleRate(fs);
		float last = 1.f, other = 0.f;
		for (int i = 0; i < 96000; i++) {
			last = g.process(0, 1.f);
			other = std::max(other, std::fabs(g.process(1, 0.f)));
		}
		CHECK(std::fabs(last) < 1e-3f, "the input's block: 1 V of DC leaves %g V after 2 s", last);
		CHECK(other == 0.f, "the input's block: side 1 is untouched by side 0 (%g)", other);
	}

	// The bound: the exact identity up to 8 V, a ceiling of 10 V.
	bool exact = true;
	for (int i = -8000; i <= 8000; i++) {
		const float x = (float) i * 1e-3f;
		exact = exact && detail::softBound(x) == x;
	}
	CHECK(exact, "identity below 8 V is bit for bit");
	float top = 0.f, prev = 0.f;
	bool mono = true;
	for (int i = 0; i <= 40000; i++) {
		const float y = detail::softBound((float) i * 1e-3f);
		top = std::max(top, y);
		mono = mono && y >= prev;
		prev = y;
	}
	CHECK(top <= 10.f && mono, "the bound tops at %g and is monotonic: %d", top, (int) mono);
	WriteBound h;
	h.setSampleRate(fs);
	float hi = 0.f, lo = 0.f;
	for (int i = 0; i < 4800; i++) {
		hi = std::max(hi, h.process(0, 20.f));
		lo = std::min(lo, h.process(1, -20.f));
	}
	CHECK(hi <= 10.f && hi > 9.f, "20 V in gives %g V out", hi);
	CHECK(lo >= -10.f && lo < -9.f, "-20 V in gives %g V out", lo);

	// The tape's trim, far under the audio band: a 30 Hz sine through it
	// changes by under 1e-4 dB (a pass of the tape, I2), yet 1 V of DC on
	// the tape is under 1 mV after 2 minutes of writing (at 8 kHz, to be
	// quick: it counts frames written, and the corner is per second).
	{
		WriteBound b;
		b.setSampleRate(fs);
		double ei = 0, eo = 0;
		for (int i = 0; i < 4 * 48000; i++) {
			const float x = 5.f * (float) std::sin(2 * PI * 30.0 * i / fs);
			const float y = b.process(0, x);
			if (i >= 48000) {
				ei += (double) x * x;
				eo += (double) y * y;
			}
		}
		const double db = 10.0 * std::log10(eo / ei);
		CHECK(std::fabs(db) < 1e-4, "the tape's trim: a 30 Hz sine at 5 V changes by %g dB a pass", db);
		WriteBound d;
		d.setSampleRate(8000.f);
		float last = 1.f, at10 = 1.f, other = 0.f;
		for (int i = 0; i < 120 * 8000; i++) {
			last = d.process(0, 1.f);
			if (i == 10 * 8000)
				at10 = last;
			other = std::max(other, std::fabs(d.process(1, 0.f)));
		}
		CHECK(std::fabs(last) < 1e-3f && at10 < 0.6f, "the tape's trim: 1 V of DC leaves %g V after 10 s, %g V after 120 s", at10, last);
		CHECK(other == 0.f, "the tape's trim: side 1 is untouched by side 0 (%g)", other);
	}
}

// Wear's saturation, on asymmetric material, takes the DC of what it changes
// off: a 25 % pulse of 11 V (no DC) worn at 1, by Wear alone, comes out with
// its mean within 30 mV after the first pass and every pass after (the
// saturation alone would leave about -0.7 V on it).
void saturation_dc() {
	Wear w(23);
	w.set(1.f);
	Tape t(48000);
	for (int i = 0; i < t.size(); i++)
		t.ch[0][i] = t.ch[1][i] = (i % 96) < 24 ? 8.25f : -2.75f;
	double worst = 0;
	for (int p = 0; p < 20; p++) {
		t.pass(w);
		double m = 0;
		for (float v : t.ch[0])
			m += v;
		m /= t.size();
		worst = std::max(worst, std::fabs(m));
	}
	CHECK(worst < 0.03, "a hot asymmetric pulse worn at 1: the tape's mean reaches %.1f mV in 20 passes (want under 30)", worst * 1e3);
}

void hostile() {
	const float vals[6] = {NAN_F, INF_F, -INF_F, 1e30f, -1e30f, 0.5f};
	const float fr[6] = {0.f, -1.f, 1e9f, NAN_F, INF_F, 1.f};
	for (float a : {0.f, 0.3f, 1.f}) {
		Wear w(2);
		w.set(a);
		InputDcBlock g;
		g.setSampleRate(48000.f);
		WriteBound b;
		b.setSampleRate(48000.f);
		bool fin = true;
		for (float v : vals)
			for (float f : fr)
				for (int c = 0; c < 2; c++) {
					fin = fin && std::isfinite(w.process(c, v, f));
					fin = fin && std::isfinite(g.process(c, v)) && std::fabs(g.process(c, v)) <= 200.f;
					fin = fin && std::isfinite(b.process(c, v)) && std::fabs(b.process(c, v)) <= 10.f;
				}
		CHECK(fin, "amount %g: every hostile call gives a finite result", a);
		bool ok = true;
		for (int i = 0; i < 1000; i++)
			for (int c = 0; c < 2; c++)
				ok = ok && std::isfinite(w.process(c, 1.f, 1.f)) && std::isfinite(g.process(c, 1.f)) && std::isfinite(b.process(c, 1.f));
		CHECK(ok, "amount %g: normal calls are finite afterwards", a);
	}
	InputDcBlock g;
	g.setSampleRate(NAN_F);
	CHECK(std::isfinite(g.process(0, 1.f)), "a NaN rate leaves the input's block working");
	WriteBound b;
	b.setSampleRate(NAN_F);
	CHECK(std::isfinite(b.process(0, 1.f)), "a NaN rate leaves the write bound working");
	Wear w(1);
	w.setSampleRate(0.f);
	w.set(1.f);
	CHECK(std::isfinite(w.process(0, 1.f, 1.f)), "a zero rate leaves wear working");
}

} // namespace

int main() {
	off();
	never_grows();
	rate_follows_knob();
	hiss_bounded();
	dropouts();
	seeded();
	guard();
	saturation_dc();
	hostile();
	return rkatest::finish("eddy test_wear");
}

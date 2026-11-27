// Doldrums' per-bin tables (doldrums/Tables.hpp): Shift, Blur, Tilt, the comb
// and the 16 Hz cut, each against the design's "Grains", "Blur, Tilt, Shift"
// and "Harmonics" and the plan's Global Constraints (Spectral order, Tilt /
// Blur / Shift, Comb, Staging), and the rebuild a part at a time. Medium grain
// at 48 kHz (8192 bins, 2.93 Hz apart) unless a check says otherwise.
#include "doldrums/Tables.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace {

using namespace rka::doldrums;

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
const int kBins = 8192;
const float kRate = 48000.f;

// Everything at rest; the comb's controls set as the panel's defaults would
// leave them (C2 was the default; here C2 an octave up, 8 partials, 25 cents).
TableSpec restSpec(int bins = kBins, float rate = kRate) {
	TableSpec s;
	std::memset(&s, 0, sizeof(s));
	s.bins = bins;
	s.rate = rate;
	s.shiftRatio = 1.f;
	s.blurOctaves = 0.f;
	s.tiltDbPerOct = 0.f;
	s.harmonics = 0.f;
	s.roots = 1;
	s.rootHz[0] = 65.406f;
	s.partials = 8;
	s.widthCents = 25.f;
	return s;
}

TableSpec combSpec(float root, int partials, float cents, float h = 1.f) {
	TableSpec s = restSpec();
	s.harmonics = h;
	s.roots = 1;
	s.rootHz[0] = root;
	s.partials = partials;
	s.widthCents = cents;
	return s;
}

double freqOf(double k, int bins = kBins, double rate = kRate) { return k * rate / (2.0 * bins); }
double binOf(double f, int bins = kBins, double rate = kRate) { return f * 2.0 * bins / rate; }
int cutOf(int bins, double rate) {
	int k = 0;
	while (freqOf(k, bins, rate) < 16.0) k++;
	return k;
}

// Tables with a spec made live at once.
struct Live {
	Tables t;
	std::vector<float> scratch;
	float g = 1.f;   // the comb's makeup on the last run
	explicit Live(int maxBins = 65536) {
		t.allocate(maxBins);
		scratch.assign(maxBins, 0.f);
	}
	void set(const TableSpec& s) {
		t.start(s);
		while (!t.work(1 << 20)) {}
		t.swapIfReady();
	}
	std::vector<float> run(std::vector<float> loud) {
		g = t.apply(loud.data(), scratch.data());
		return loud;
	}
	// The comb's mask: ones in, h = 1, divided by the makeup.
	std::vector<float> mask() {
		std::vector<float> m = run(std::vector<float>(t.live().bins, 1.f));
		for (float& x : m) x /= g;
		return m;
	}
};

std::vector<float> noise(int n, uint32_t seed) {
	std::vector<float> v(n);
	for (int i = 0; i < n; i++) {
		seed = seed * 1664525u + 1013904223u;
		v[i] = 0.05f + float(seed >> 8) * (1.f / 16777216.f);
	}
	return v;
}

std::vector<float> oneBin(int n, int k) {
	std::vector<float> v(n, 0.f);
	v[k] = 1.f;
	return v;
}

double energy(const std::vector<float>& v, int from = 0) {
	double e = 0;
	for (size_t i = from; i < v.size(); i++) e += double(v[i]) * v[i];
	return e;
}

double dB(double ratio) { return 10.0 * std::log10(ratio); }

bool same(const TableSpec& a, const TableSpec& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

// Half-power points of a power spectrum p around its peak, by linear
// interpolation between bins; the width in octaves.
double halfPowerOctaves(const std::vector<float>& loud, double* lo = nullptr, double* hi = nullptr) {
	int peak = 0;
	for (size_t i = 0; i < loud.size(); i++)
		if (loud[i] > loud[peak]) peak = int(i);
	const double half = 0.5 * double(loud[peak]) * loud[peak];
	auto p = [&](int i) { return double(loud[i]) * loud[i]; };
	int a = peak;
	while (a > 1 && p(a - 1) > half) a--;
	int b = peak;
	while (b + 2 < int(loud.size()) && p(b + 1) > half) b++;
	const double l = a - 1 + (half - p(a - 1)) / (p(a) - p(a - 1));
	const double h = b + (p(b) - half) / (p(b) - p(b + 1));
	if (lo) *lo = l;
	if (hi) *hi = h;
	return std::log2(h / l);
}

// ---------------------------------------------------------------- at rest

void rest_is_exact() {
	Live L;
	// Reached from busy tables in both sets, so nothing left in them counts.
	TableSpec busy = restSpec();
	busy.shiftRatio = 1.5f;
	busy.blurOctaves = 0.5f;
	busy.tiltDbPerOct = 5.f;
	busy.harmonics = 0.7f;
	L.set(busy);
	busy.tiltDbPerOct = -5.f;
	L.set(busy);
	L.set(restSpec());
	const std::vector<float> in = noise(kBins, 1);
	const std::vector<float> out = L.run(in);
	const int cut = cutOf(kBins, kRate);
	int diff = 0;
	for (int k = cut; k < kBins; k++)
		if (std::memcmp(&in[k], &out[k], sizeof(float)) != 0) diff++;
	CHECK(diff == 0, "%d bins above the cut changed at rest", diff);
	// Each control alone at its rest value, the others moved and put back.
	TableSpec s = restSpec();
	s.harmonics = 0.f;
	s.roots = 16;
	for (int i = 0; i < 16; i++) s.rootHz[i] = 100.f + 37.f * i;
	L.set(s);
	const std::vector<float> out2 = L.run(in);
	diff = 0;
	for (int k = cut; k < kBins; k++)
		if (std::memcmp(&in[k], &out2[k], sizeof(float)) != 0) diff++;
	CHECK(diff == 0, "h = 0 with a 16-note comb set: %d bins changed", diff);
	CHECK(L.g == 1.f && !L.t.combOn(), "no comb, no makeup: %g", L.g);
}

void low_cut() {
	const int sizes[] = {8192, 16384, 32768, 65536, 131072};
	const float rates[] = {44100.f, 48000.f, 88200.f, 96000.f, 192000.f};
	Live L;
	for (int g : sizes)
		for (float r : rates) {
			const int bins = g / 2;
			L.set(restSpec(bins, r));
			const std::vector<float> in = noise(bins, 7);
			const std::vector<float> out = L.run(in);
			int wrong = 0;
			for (int k = 0; k < bins; k++) {
				const bool under = freqOf(k, bins, r) < 16.0;
				if (under ? out[k] != 0.f : out[k] != in[k]) wrong++;
			}
			CHECK(wrong == 0, "grain %d at %g Hz: %d bins wrong", g, r, wrong);
			// And with every table in use.
			TableSpec s = restSpec(bins, r);
			s.shiftRatio = 0.7f;
			s.blurOctaves = 0.5f;
			s.tiltDbPerOct = 3.f;
			s.harmonics = 0.5f;
			L.set(s);
			const std::vector<float> busy = L.run(in);
			wrong = 0;
			for (int k = 0; k < bins && freqOf(k, bins, r) < 16.0; k++)
				if (busy[k] != 0.f) wrong++;
			CHECK(wrong == 0, "busy, grain %d at %g Hz: %d bins under 16 Hz not 0", g, r, wrong);
			// What is under 16 Hz is cut before Shift moves it: a DC offset's skirt
			// is not lifted into the band.
			s = restSpec(bins, r);
			s.shiftRatio = 4.f;
			L.set(s);
			std::vector<float> low(bins, 0.f);
			for (int k = 0; k < bins && freqOf(k, bins, r) < 16.0; k++) low[k] = 1.f;
			const std::vector<float> lifted = L.run(low);
			double sum = 0;
			for (float x : lifted) sum += x;
			CHECK(sum == 0.0, "grain %d at %g Hz: under 16 Hz shifted up two octaves gives %g", g, r, sum);
		}
}

// ---------------------------------------------------------------- Shift

// One bin in at k: the output's power in exactly two neighbours of k * ratio,
// summing to the input's, their centroid at k * ratio within 0.1 bin.
void checkShiftedBin(Live& L, int k, float ratio, const char* what) {
	const std::vector<float> out = L.run(oneBin(kBins, k));
	const double target = double(k) * ratio;
	double sum = 0, moment = 0;
	int lo = kBins, hi = -1;
	for (int i = 0; i < kBins; i++)
		if (out[i] != 0.f) {
			const double p = double(out[i]) * out[i];
			sum += p;
			moment += p * i;
			lo = std::min(lo, i);
			hi = std::max(hi, i);
		}
	CHECK(hi >= 0, "%s: bin %d: nothing out", what, k);
	if (hi < 0) return;
	CHECK(hi - lo <= 1, "%s: bin %d spread over %d..%d", what, k, lo, hi);
	CHECK(lo == int(std::floor(target)), "%s: bin %d lands at %d, wanted floor(%g)", what, k, lo, target);
	CHECK(std::fabs(sum - 1.0) < 1e-5, "%s: bin %d: weights sum to %.8f", what, k, sum);
	CHECK(std::fabs(moment / sum - target) < 0.1, "%s: bin %d: centroid %g, wanted %g", what, k, moment / sum, target);
}

// Up, a gather: one bin in at k comes out spread round k * ratio, its power
// kept within 1 dB, its centroid at k * ratio within 0.1 bin, all of it
// within +-ratio bins of that. Measured: exact at whole ratios; at +7 the
// power is -0.49 .. +0.35 dB by where k r falls between bins, the centroid
// within 0.03 bin.
void checkShiftedUp(Live& L, int k, float ratio, const char* what) {
	const std::vector<float> out = L.run(oneBin(kBins, k));
	const double target = double(k) * ratio;
	double sum = 0, moment = 0, far = 0;
	for (int i = 0; i < kBins; i++) {
		const double p = double(out[i]) * out[i];
		sum += p;
		moment += p * i;
		if (std::fabs(i - target) > ratio) far += p;
	}
	CHECK(sum > 0, "%s: bin %d: nothing out", what, k);
	if (!(sum > 0)) return;
	CHECK(std::fabs(dB(sum)) < 1.0, "%s: bin %d: power %+.3f dB", what, k, dB(sum));
	CHECK(std::fabs(moment / sum - target) < 0.1, "%s: bin %d: centroid %g, wanted %g", what, k, moment / sum, target);
	CHECK(far == 0.0, "%s: bin %d: %g of the power further than %g bins from %g", what, k, far / sum, ratio, target);
}

void shift_octave_up() {
	Live L;
	TableSpec s = restSpec();
	s.shiftRatio = 2.f;
	L.set(s);
	const int ks[] = {10, 100, 341, 1000, 2047, 4094};
	for (int k : ks) checkShiftedUp(L, k, 2.f, "octave up");
}

void shift_octave_down() {
	Live L;
	TableSpec s = restSpec();
	s.shiftRatio = 0.5f;
	L.set(s);
	const int ks[] = {40, 101, 1000, 1001, 5000, 8191};
	for (int k : ks) checkShiftedBin(L, k, 0.5f, "octave down");
	// Two octaves down, noise keeps its power (all of it lands in the band).
	s.shiftRatio = 0.25f;
	L.set(s);
	const int cut = cutOf(kBins, kRate);
	std::vector<float> in = noise(kBins, 3);
	for (int k = 0; k < 4 * cut; k++) in[k] = 0.f;  // nothing that lands under the cut
	const std::vector<float> out = L.run(in);
	CHECK(std::fabs(dB(energy(out) / energy(in))) < 0.01, "two octaves down: %g dB", dB(energy(out) / energy(in)));
}

void shift_seven_semitones() {
	Live L;
	TableSpec s = restSpec();
	s.shiftRatio = std::pow(2.f, 7.f / 12.f);
	L.set(s);
	const int ks[] = {7, 50, 333, 1000, 3000, 5000};
	for (int k : ks) checkShiftedUp(L, k, s.shiftRatio, "+7");
	s.shiftRatio = std::pow(2.f, -7.f / 12.f);
	L.set(s);
	const int ksDown[] = {10, 50, 333, 1000, 3000, 8000};   // each lands over the cut
	for (int k : ksDown) checkShiftedBin(L, k, s.shiftRatio, "-7");
	s.shiftRatio = std::pow(2.f, 24.f / 12.f);
	L.set(s);
	const int ks2[] = {11, 999, 2046};
	for (int k : ks2) checkShiftedUp(L, k, s.shiftRatio, "+24");
}

void shift_past_nyquist_is_dropped() {
	Live L;
	TableSpec s = restSpec();
	s.shiftRatio = 2.f;
	L.set(s);
	// The top half, moved up an octave: all past the band.
	std::vector<float> in(kBins, 0.f);
	for (int k = kBins / 2; k < kBins; k++) in[k] = 1.f;
	std::vector<float> out = L.run(in);
	double sum = 0;
	for (float x : out) sum += x;
	CHECK(sum == 0.0, "top half up an octave: output sums to %g", sum);
	// Two octaves up: the top three quarters likewise; the bottom quarter stays.
	s.shiftRatio = 4.f;
	L.set(s);
	in.assign(kBins, 0.f);
	for (int k = kBins / 4; k < kBins; k++) in[k] = 1.f;
	out = L.run(in);
	sum = 0;
	for (float x : out) sum += x;
	CHECK(sum == 0.0, "top three quarters up two octaves: output sums to %g", sum);
	// Up, nothing past where the source's top bin could land and nothing at
	// the bottom: a band near the top, up a fifth, lands only in the band's
	// image and the top, and the bins under it stay empty.
	s.shiftRatio = 1.5f;
	L.set(s);
	in.assign(kBins, 0.f);
	for (int k = 5000; k < kBins; k++) in[k] = 1.f;
	out = L.run(in);
	double below = 0;
	for (int k = 0; k < int(5000 * 1.5) - 2; k++) below += out[k];
	CHECK(below == 0.0, "a fifth up: %g under where the band lands", below);
	CHECK(out[kBins - 1] > 0.f, "a fifth up: the top bin filled");
}

// A flat spectrum shifted up: no gaps, no alternating bins, up to where the
// source's top lands; the power of the part that stays in band kept.
// Measured 2026-11-26 at +7, +12, +24: every bin 1.000000 of the mean, the
// power within 0.0003 dB; pinned at 1e-4 and 0.001 dB.
void shift_up_leaves_no_gaps() {
	Live L;
	const int cut = cutOf(kBins, kRate);
	for (float semis : {12.f, 7.f, 24.f}) {
		TableSpec s = restSpec();
		s.shiftRatio = std::pow(2.f, semis / 12.f);
		L.set(s);
		std::vector<float> in(kBins, 1.f);
		for (int k = 0; k < cut; k++) in[k] = 0.f;
		const std::vector<float> out = L.run(in);
		// From clear of the cut's image to where the last source bin kept lands
		// (shift_past_nyquist_is_dropped checks there is nothing beyond).
		int lastKept = cut;
		while ((lastKept + 1) * double(s.shiftRatio) < kBins) lastKept++;
		const int from = int(std::ceil(cut * s.shiftRatio)) + 1, to = int(lastKept * double(s.shiftRatio));
		double mean = 0;
		for (int k = from; k <= to; k++) mean += double(out[k]) * out[k];
		mean /= (to - from + 1);
		double lo = 1e9, hi = 0;
		for (int k = from; k <= to; k++) {
			const double p = double(out[k]) * out[k];
			lo = std::min(lo, p / mean);
			hi = std::max(hi, p / mean);
		}
		CHECK(lo > 0.9999 && hi < 1.0001, "+%g semitones: power per bin %.5f .. %.5f of the mean", semis, lo, hi);
		// What stays in band: source bins below bins / ratio.
		double kept = 0;
		for (int k = cut; k < kBins && k * s.shiftRatio < kBins; k++) kept += 1.0;
		const double got = dB(energy(out) / kept);
		CHECK(std::fabs(got) < 0.001, "+%g semitones: %+.4f dB of the part kept in band", semis, got);
	}
}

// Shift acts first: what moves meets the comb, the tilt and the blur at its
// new frequency.
void shift_comes_first() {
	Live L;
	// A tooth at 1 kHz; a bin at 500 Hz shifted up an octave passes it.
	TableSpec s = combSpec(1000.f, 1, 25.f);
	s.shiftRatio = 2.f;
	L.set(s);
	const int k = int(std::lround(binOf(500.0)));
	std::vector<float> out = L.run(oneBin(kBins, k));
	double e = energy(out);
	CHECK(e > 0.5, "500 Hz up an octave meets the 1 kHz tooth: %g", e);
	// A bin at the tooth itself shifted away does not.
	out = L.run(oneBin(kBins, int(std::lround(binOf(1000.0)))));
	CHECK(energy(out) == 0.0, "1 kHz moved to 2 kHz misses the tooth: %g", energy(out));
	// And down: 2 kHz down an octave meets the 1 kHz tooth; 1 kHz misses it.
	s.shiftRatio = 0.5f;
	L.set(s);
	out = L.run(oneBin(kBins, int(std::lround(binOf(2000.0)))));
	e = energy(out);
	CHECK(e > 0.5, "2 kHz down an octave meets the 1 kHz tooth: %g", e);
	out = L.run(oneBin(kBins, int(std::lround(binOf(1000.0)))));
	CHECK(energy(out) == 0.0, "1 kHz moved to 500 Hz misses the tooth: %g", energy(out));
	// Tilt at the new frequency: 1 kHz up an octave gets +6 dB at 6 dB/oct.
	s = restSpec();
	s.shiftRatio = 2.f;
	s.tiltDbPerOct = 6.f;
	L.set(s);
	out = L.run(oneBin(kBins, 2048));  // 6 kHz to 12 kHz
	const double want = 6.0 * std::log2(freqOf(4096) / 1000.0);
	CHECK(std::fabs(dB(energy(out)) - want) < 0.05, "tilt at 12 kHz: %g dB, wanted %g", dB(energy(out)), want);
}

// ---------------------------------------------------------------- Blur

void blur_keeps_energy() {
	Live L;
	const int cut = cutOf(kBins, kRate);
	std::vector<float> n = noise(kBins, 5);
	for (int k = 0; k < cut; k++) n[k] = 0.f;
	const std::vector<float> one = oneBin(kBins, int(std::lround(binOf(1000.0))));
	for (int i = 0; i <= 10; i++) {
		TableSpec s = restSpec();
		s.blurOctaves = 0.1f * i;
		L.set(s);
		const double en = dB(energy(L.run(n)) / energy(n));
		const double eo = dB(energy(L.run(one)) / energy(one));
		CHECK(std::fabs(en) < 1.0, "blur %d/10: noise %+.3f dB", i, en);
		CHECK(std::fabs(eo) < 1.0, "blur %d/10: one bin %+.3f dB", i, eo);
	}
	// A flat spectrum stays flat: power is moved along, not tilted, and what
	// the upward pass carries past the top comes back. Measured at full:
	// within 0.07 dB from 50 Hz up, 0.01 dB from 100 Hz (the bass droops a
	// little, carried under the cut).
	for (float b : {0.3f, 1.f}) {
		TableSpec s = restSpec();
		s.blurOctaves = b;
		L.set(s);
		const std::vector<float> flat = L.run(std::vector<float>(kBins, 1.f));
		double worst = 0;
		for (int k = int(binOf(100.0)); k < kBins; k++) worst = std::max(worst, std::fabs(20.0 * std::log10(flat[k])));
		CHECK(worst < 0.03, "blur %g: flat in, %.3f dB from flat out over 100 Hz", b, worst);
	}
	// Under a shift too.
	TableSpec s = restSpec();
	s.blurOctaves = 1.f;
	s.shiftRatio = 0.5f;
	L.set(s);
	std::vector<float> lowHalf(kBins, 0.f);
	for (int k = 2 * cut; k < kBins; k++) lowHalf[k] = n[k];
	const double es = dB(energy(L.run(lowHalf)) / energy(lowHalf));
	CHECK(std::fabs(es) < 1.0, "blur under an octave down: %+.3f dB", es);
}

// Measured 2026-11-26: 0.9996 octaves between the half-power points at full
// (651 .. 1302 Hz), a single bin at 1 kHz, Medium at 48 kHz; 0.4997 at half.
// Pinned within 5%.
void blur_width_at_full() {
	Live L;
	TableSpec s = restSpec();
	s.blurOctaves = 1.f;
	L.set(s);
	const std::vector<float> out = L.run(oneBin(kBins, int(std::lround(binOf(1000.0)))));
	double lo = 0, hi = 0;
	const double w = halfPowerOctaves(out, &lo, &hi);
	CHECK(w > 0.95 && w < 1.05, "width at full: %.3f octaves (%.0f .. %.0f Hz)", w, freqOf(lo), freqOf(hi));
	// Half the knob, half the width.
	s.blurOctaves = 0.5f;
	L.set(s);
	const double w2 = halfPowerOctaves(L.run(oneBin(kBins, int(std::lround(binOf(1000.0))))));
	CHECK(w2 > 0.475 && w2 < 0.525, "width at half: %.3f octaves", w2);
}

void blur_same_in_bass_and_treble() {
	Live L;
	for (float b : {0.5f, 1.f}) {
		TableSpec s = restSpec();
		s.blurOctaves = b;
		L.set(s);
		const double wBass = halfPowerOctaves(L.run(oneBin(kBins, int(std::lround(binOf(200.0))))));
		const double wMid = halfPowerOctaves(L.run(oneBin(kBins, int(std::lround(binOf(1000.0))))));
		const double wTreble = halfPowerOctaves(L.run(oneBin(kBins, int(std::lround(binOf(5000.0))))));
		CHECK(std::fabs(wBass - wTreble) < 0.02 * b, "blur %g: %.3f octaves at 200 Hz, %.3f at 5 kHz", b, wBass, wTreble);
		CHECK(std::fabs(wMid - wTreble) < 0.02 * b, "blur %g: %.3f octaves at 1 kHz, %.3f at 5 kHz", b, wMid, wTreble);
	}
}

// ---------------------------------------------------------------- Tilt

void tilt_slope() {
	Live L;
	for (float sign : {1.f, -1.f}) {
		TableSpec s = restSpec();
		s.tiltDbPerOct = 6.f * sign;  // TILT +-1
		L.set(s);
		const std::vector<float> g = L.run(std::vector<float>(kBins, 1.f));
		double worst = 0;
		for (int k = int(binOf(100.0)); k <= int(binOf(10000.0)); k++) {
			const double want = 6.0 * sign * std::log2(freqOf(k) / 1000.0);
			worst = std::max(worst, std::fabs(20.0 * std::log10(g[k]) - want));
		}
		CHECK(worst < 0.1, "TILT %+g: worst %.4f dB from 6 dB/oct over 100 Hz .. 10 kHz", sign, worst);
		const int cut = cutOf(kBins, kRate);
		double top = -1e9, bottom = 1e9;
		for (int k = cut; k < kBins; k++) {
			const double d = 20.0 * std::log10(g[k]);
			top = std::max(top, d);
			bottom = std::min(bottom, d);
		}
		CHECK(top <= 24.0 + 1e-3 && bottom >= -24.0 - 1e-3, "TILT %+g: %.3f .. %.3f dB", sign, bottom, top);
		// 6 dB/oct from 1 kHz reaches 24 dB at 62.5 Hz, so the bass is held.
		CHECK(sign > 0 ? bottom <= -23.99 : top >= 23.99, "TILT %+g reaches the bound in the bass: %.3f .. %.3f", sign,
		      bottom, top);
	}
	// A slope past the knob's never goes past the bound either way.
	TableSpec s = restSpec();
	s.tiltDbPerOct = 40.f;
	L.set(s);
	const std::vector<float> g = L.run(std::vector<float>(kBins, 1.f));
	bool inside = true;
	for (int k = cutOf(kBins, kRate); k < kBins; k++)
		inside = inside && std::fabs(20.0 * std::log10(g[k])) <= 24.0 + 1e-3;
	CHECK(inside, "40 dB/oct held within +-24 dB");
}

// ---------------------------------------------------------------- the comb

// The bin with the greatest mask within a third of the way to each
// neighbouring tooth.
int peakNear(const std::vector<float>& m, double f) {
	const int a = std::max(1, int(binOf(f * std::pow(2.0, -200.0 / 1200))));
	const int b = std::min(kBins - 1, int(binOf(f * std::pow(2.0, 200.0 / 1200))) + 1);
	int best = a;
	for (int k = a; k <= b; k++)
		if (m[k] > m[best]) best = k;
	return best;
}

double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

int countPeaks(const std::vector<float>& m, float floor = 0.5f) {
	int n = 0;
	for (size_t k = 1; k + 1 < m.size(); k++)
		if (m[k] >= floor && m[k] > m[k - 1] && m[k] >= m[k + 1]) n++;
	return n;
}

void comb_teeth() {
	Live L;
	L.set(combSpec(65.406f, 8, 25.f));
	const std::vector<float> m = L.mask();
	const double halfBin = 0.5 * kRate / (2.0 * kBins);
	for (int n = 1; n <= 8; n++) {
		const double f = 65.406 * n;
		const int k = peakNear(m, f);
		const double off = std::fabs(freqOf(k) - f);
		CHECK(std::fabs(cents(freqOf(k), f)) <= 5.0 || off <= halfBin, "tooth %d: peak %g Hz for %g", n, freqOf(k), f);
		CHECK(m[k] >= 0.99f, "tooth %d: mask %g at its peak", n, m[k]);
	}
	const int mid = int(std::lround(binOf(65.406 * 1.5)));
	CHECK(m[mid] <= 0.02f, "midway between the first two: %g", m[mid]);
	CHECK(countPeaks(m) == 8, "%d peaks", countPeaks(m));
}

void comb_count() {
	Live L;
	for (int p : {1, 2, 5, 8, 17, 32}) {
		L.set(combSpec(110.f, p, 25.f));
		const std::vector<float> m = L.mask();
		CHECK(countPeaks(m) == p, "partials %d: %d peaks", p, countPeaks(m));
		// Nothing past the last tooth: from 6 sigma above it (300 cents: at
		// 110 Hz a bin is 45 cents wide, and sigma never less).
		const int k = int(binOf(110.0 * p * std::pow(2.0, 300.0 / 1200))) + 1;
		float above = 0.f;
		for (int i = k; i < kBins; i++) above = std::max(above, m[i]);
		CHECK(above < 0.01f, "partials %d: %g above the last tooth", p, above);
	}
}

void comb_stops_at_nyquist() {
	Live L;
	// 5 kHz: teeth at 5, 10, 15, 20 kHz; 25 kHz and up are past 24 kHz.
	L.set(combSpec(5000.f, 8, 25.f));
	std::vector<float> m = L.mask();
	CHECK(countPeaks(m) == 4, "5 kHz, 8 partials: %d peaks", countPeaks(m));
	float top = 0.f;
	for (int k = int(binOf(21000.0)); k < kBins; k++) top = std::max(top, m[k]);
	CHECK(top < 0.01f, "nothing from teeth past the top: %g", top);
	CHECK(std::isfinite(L.g) && L.g <= 8.f, "makeup %g", L.g);
	// A tooth just over the top is left out, one just under kept.
	L.set(combSpec(12100.f, 4, 25.f));
	CHECK(countPeaks(L.mask()) == 1, "12.1 kHz: %d peaks", countPeaks(L.mask()));
	L.set(combSpec(11900.f, 4, 25.f));
	CHECK(countPeaks(L.mask()) == 2, "11.9 kHz: %d peaks", countPeaks(L.mask()));
}

// The -3 dB width of a tooth, in cents, against sigma: for the bell
// exp(-(d / sigma)^2 / 2), 2 sqrt(2 ln sqrt 2) sigma = 1.665 sigma, plus the
// width of the bin that holds the tooth (d is measured to the bin's edges).
void comb_width() {
	Live L;
	for (float w : {10.f, 25.f, 50.f, 100.f, 300.f}) {
		const float root = 3000.f;  // a bin is 1.7 cents here
		L.set(combSpec(root, 1, w));
		const std::vector<float> m = L.mask();
		const int pk = peakNear(m, root);
		int a = pk, b = pk;
		while (m[a - 1] > 0.70710678f) a--;
		while (m[b + 1] > 0.70710678f) b++;
		const double lo = a - 1 + (0.70710678 - m[a - 1]) / (m[a] - m[a - 1]);
		const double hi = b + (m[b] - 0.70710678) / (m[b] - m[b + 1]);
		const double got = cents(hi, lo);
		const double binCents = cents(freqOf(pk + 0.5), freqOf(pk - 0.5));
		const double want = 1.6651 * w + binCents;
		CHECK(std::fabs(got - want) < 0.01 * want + 0.2, "width %g cents: -3 dB over %.2f cents, wanted %.2f", w, got, want);
		// The bell's shape out to its tail, d measured to the bin's near edge:
		// exp(-(d / sigma)^2 / 2) within 1% wherever it is over 1e-6.
		double worst = 0;
		for (int k = pk + 1; k < kBins; k++) {
			const double u = cents(freqOf(k - 0.5), root) / w;
			const double want = std::exp(-0.5 * u * u);
			if (want < 1e-6) break;
			worst = std::max(worst, std::fabs(m[k] - want) / want);
		}
		CHECK(worst < 0.01, "width %g cents: the bell off its formula by %.4f", w, worst);
	}
}

void comb_low_note_keeps_its_teeth() {
	Live L;
	for (int bins : {4096, 8192, 16384}) {
		TableSpec s = combSpec(32.703f, 8, 5.f);  // C1 at the narrowest Width
		s.bins = bins;
		L.set(s);
		const std::vector<float> m = L.mask();
		for (int n = 1; n <= 8; n++) {
			const double f = 32.703 * n;
			const int a = int(binOf(f * std::pow(2.0, -50.0 / 1200), bins)) - 1;
			const int b = int(binOf(f * std::pow(2.0, 50.0 / 1200), bins)) + 2;
			float best = 0.f;
			for (int k = std::max(a, 0); k <= b && k < bins; k++) best = std::max(best, m[k]);
			CHECK(best >= 0.5f, "%d bins: C1 tooth %d at %g Hz: best %g", bins, n, f, best);
			// Never narrower than a bin: the bins either side of the one that
			// holds the tooth pass at least half (the tooth is within a bin's
			// width of each of their near edges).
			const int k = int(binOf(f, bins) + 0.5);
			CHECK(m[k - 1] >= 0.5f && m[k + 1] >= 0.5f, "%d bins: C1 tooth %d: neighbours %g %g", bins, n, m[k - 1], m[k + 1]);
		}
	}
}

void comb_chord() {
	Live L;
	const float roots[3] = {130.813f, 164.814f, 195.998f};
	std::vector<float> single[3];
	for (int i = 0; i < 3; i++) {
		L.set(combSpec(roots[i], 6, 25.f));
		single[i] = L.mask();
	}
	TableSpec s = combSpec(roots[0], 6, 25.f);
	s.roots = 3;
	for (int i = 0; i < 3; i++) s.rootHz[i] = roots[i];
	L.set(s);
	const std::vector<float> m = L.mask();
	float over = 0.f, worst = 0.f;
	for (int k = 0; k < kBins; k++) {
		over = std::max(over, m[k]);
		const float want = std::max(single[0][k], std::max(single[1][k], single[2][k]));
		worst = std::max(worst, std::fabs(m[k] - want));
	}
	CHECK(over <= 1.f + 1e-6f, "mask over 1: %g", over);
	CHECK(worst < 1e-6f, "the chord's mask is the greatest of the three: off by %g", worst);
	for (int i = 0; i < 3; i++)
		for (int n = 1; n <= 6; n++) {
			const int k = peakNear(m, roots[i] * n);
			CHECK(m[k] >= 0.99f, "root %d tooth %d: %g", i, n, m[k]);
		}
	// 6 x 3 teeth, two pairs of which fall in neighbouring bins (G3 x 2 by
	// C3 x 3, G3 x 4 by C3 x 6): at least 16 peaks.
	CHECK(countPeaks(m) >= 16 && countPeaks(m) <= 18, "%d peaks", countPeaks(m));
}

void harmonics_zero_is_out() {
	Live L;
	const std::vector<float> in = noise(kBins, 11);
	L.set(restSpec());
	const std::vector<float> rest = L.run(in);
	TableSpec s = combSpec(55.f, 32, 5.f, 0.f);
	s.roots = 16;
	for (int i = 0; i < 16; i++) s.rootHz[i] = 55.f * (1 + i);
	L.set(s);
	const std::vector<float> out = L.run(in);
	CHECK(std::memcmp(rest.data(), out.data(), kBins * sizeof(float)) == 0, "h = 0: the comb is out to the bit");
	// Out of the path altogether: nothing of the comb is kept, and a moving
	// V/OCT (or Partials, or Width) with h at 0 starts no rebuild.
	CHECK(L.t.live().roots == 0 && L.t.live().partials == 0 && L.t.live().widthCents == 0.f && L.g == 1.f && !L.t.combOn(),
	      "h = 0: the comb's settings cleared: %d roots, %d partials, %g cents, makeup %g", L.t.live().roots,
	      L.t.live().partials, L.t.live().widthCents, L.g);
	TableSpec moved = s;
	for (int i = 0; i < 16; i++) moved.rootHz[i] *= 1.0594631f;
	moved.partials = 7;
	moved.widthCents = 300.f;
	L.t.start(moved);
	CHECK(L.t.work(1), "h = 0, V/OCT, Partials and Width moved: nothing to build");
	CHECK(!L.t.swapIfReady(), "h = 0, V/OCT, Partials and Width moved: no swap");
	// A tilt with the comb out: the tilt's gains alone.
	s.tiltDbPerOct = 3.f;
	L.set(s);
	const std::vector<float> withComb = L.run(in);
	TableSpec t = restSpec();
	t.tiltDbPerOct = 3.f;
	L.set(t);
	const std::vector<float> tiltOnly = L.run(in);
	CHECK(std::memcmp(withComb.data(), tiltOnly.data(), kBins * sizeof(float)) == 0, "h = 0 under a tilt: the tilt alone");
}

void harmonics_full_passes_only_teeth() {
	Live L;
	const std::vector<float> in = noise(kBins, 13);
	L.set(combSpec(220.f, 8, 25.f, 1.f));
	std::vector<float> out = L.run(in);
	const float g = L.g;   // the makeup measured from this input
	// Between the teeth, nothing at all where they are 6 sigma apart or more
	// (from tooth 5 up their bells overlap a little: 4e-5 between 7 and 8).
	for (int n = 1; n < 5; n++) {
		const int k = int(std::lround(binOf(220.0 * (n + 0.5))));
		CHECK(out[k] == 0.f, "between teeth %d and %d: %g", n, n + 1, out[k]);
	}
	// At a tooth, the makeup.
	for (int n = 1; n <= 8; n++) {
		const int k = peakNear(L.mask(), 220.0 * n);
		out = L.run(in);
		CHECK(std::fabs(out[k] - g * in[k]) <= 1e-5f * g * in[k], "tooth %d: %g for %g x %g", n, out[k], g, in[k]);
	}
	// Half way: (1 - h) + h g mask, g measured at h = 0.5 (comb_makeup_measured).
	L.set(combSpec(220.f, 8, 25.f, 0.5f));
	out = L.run(std::vector<float>(kBins, 1.f));
	const float g2 = L.g;
	const int gap = int(std::lround(binOf(220.0 * 2.5)));
	CHECK(out[gap] == 0.5f, "h = 0.5 between teeth: %g", out[gap]);
	const int tooth = peakNear(out, 440.0);
	CHECK(std::fabs(out[tooth] - (0.5f + 0.5f * g2)) < 1e-5f, "h = 0.5 at a tooth: %g, wanted %g", out[tooth], 0.5f + 0.5f * g2);
}

void makeup_formula_and_bound() {
	Live L;
	const int cut = cutOf(kBins, kRate);
	struct Case { float root; int partials; float cents; int roots; } cases[] = {
		{55.f, 32, 300.f, 1}, {110.f, 8, 100.f, 1}, {65.406f, 32, 300.f, 4}, {440.f, 1, 5.f, 1}, {130.8f, 16, 50.f, 16},
	};
	for (const Case& c : cases) {
		TableSpec s = combSpec(c.root, c.partials, c.cents);
		s.roots = c.roots;
		for (int i = 0; i < c.roots; i++) s.rootHz[i] = c.root * std::pow(2.f, i / 7.f);
		L.set(s);
		// The mask straight from h = 1 with the gain not yet applied is g x mask;
		// recover the mask's mean square and check g = min(8, 1 / rms).
		const std::vector<float> gm = L.run(std::vector<float>(kBins, 1.f));
		const double g = L.g;
		double sq = 0;
		for (int k = cut; k < kBins; k++) sq += (double(gm[k]) / g) * (double(gm[k]) / g);
		const double want = std::min(8.0, 1.0 / std::sqrt(sq / (kBins - cut)));
		CHECK(std::fabs(g - want) < 1e-4 * want, "root %g x %d, %d partials, %g cents: g %g, wanted %g", c.root, c.roots,
		      c.partials, c.cents, g, want);
		CHECK(g >= 1.0 && g <= 8.0, "g %g inside [1, 8]", g);
	}
	L.set(combSpec(440.f, 1, 5.f));
	L.mask();
	CHECK(L.g == 8.f, "one narrow tooth: the bound, %g", L.g);
}

// The makeup measured from the sound (Tables.hpp, "The makeup"; C1 of the
// final review): g = sqrt(before / after) held to [1, 8], before the power
// reaching the comb and after what the comb leaves at g = 1.
void comb_makeup_measured() {
	Live L;
	const int cut = cutOf(kBins, kRate);
	// The formula, on noise and on a sound shaped like the comb, at three h.
	for (float h : {1.f, 0.5f, 0.1f}) {
		L.set(combSpec(65.406f, 8, 25.f, h));
		const std::vector<float> m = [&] {
			Live M;
			M.set(combSpec(65.406f, 8, 25.f, 1.f));
			return M.mask();
		}();
		{
			// A sound shaped like the comb (its loudness the mask).
			double before = 0, after = 0;
			for (int k = cut; k < kBins; k++) {
				const double x = m[k], y = x * ((1.0 - h) + h * m[k]);
				before += x * x;
				after += y * y;
			}
			const double want = std::min(8.0, std::max(1.0, std::sqrt(before / after)));
			const std::vector<float> out = L.run(m);
			CHECK(std::fabs(L.g - want) < 1e-4 * want, "h %g, a sound on the teeth: g %g, wanted %g", h, L.g, want);
			// On the teeth the level is kept: at h = 1 the output's power is the
			// input's; below, g scales only the comb's share of the gain, so the
			// level comes a little short (-0.29 dB at h = 0.5), never over.
			const double got = dB(energy(out) / before);
			CHECK(want < 8.0 && (h == 1.f ? std::fabs(got) < 0.01 : got <= 0.0 && got > -0.5), "h %g, a sound on the teeth: %+.4f dB (g %g)", h,
			      got, want);
		}
		const std::vector<float> n = noise(kBins, 21);
		double before = 0, after = 0;
		for (int k = cut; k < kBins; k++) {
			const double x = n[k], y = x * ((1.0 - h) + h * m[k]);
			before += x * x;
			after += y * y;
		}
		const double need = std::sqrt(before / after), want = std::min(8.0, std::max(1.0, need));
		L.run(n);
		CHECK(std::fabs(L.g - want) < 1e-4 * want, "h %g, noise: g %g, wanted %g (needs %g)", h, L.g, want, need);
		if (h == 1.f) {
			CHECK(need > 8.0 && L.g == 8.f, "noise at the defaults needs %.1f (%+.1f dB): the bound, %g", need, 20 * std::log10(need), L.g);
			std::printf("  comb_makeup_measured: noise at C2, 8 partials, 25 cents, h 1 needs %+.1f dB; the bound gives +18.1\n",
			            20 * std::log10(need));
		}
	}
	// A tone on a tooth, through a tilt: Tilt is before the comb, so the
	// makeup does not undo it.
	TableSpec s = combSpec(1000.f, 1, 25.f);
	s.tiltDbPerOct = 6.f;
	L.set(s);
	std::vector<float> out = L.run(oneBin(kBins, int(std::lround(binOf(2000.0)))));
	CHECK(energy(out) == 0.0 && L.g == 1.f, "a tone off the only tooth: silent, g 1 (after 0): %g, %g", energy(out), L.g);
	const int k1 = int(std::lround(binOf(1000.0)));
	out = L.run(oneBin(kBins, k1));
	CHECK(std::fabs(L.g - 1.f) < 1e-3f && std::fabs(out[k1] - 1.f) < 1e-3f, "a tone at 1 kHz on its tooth: g %g, out %g", L.g, out[k1]);
	// Silence: g 1 and silence, to the bit.
	L.set(combSpec(65.406f, 8, 25.f));
	out = L.run(std::vector<float>(kBins, 0.f));
	CHECK(L.g == 1.f && energy(out) == 0.0, "silence: g %g, %g out", L.g, energy(out));
	// Sums that overflow: g 1, every bin finite.
	s = combSpec(65.406f, 8, 25.f);
	s.tiltDbPerOct = 48.f;
	L.set(s);
	out = L.run(std::vector<float>(kBins, 1e18f));
	int bad = 0;
	for (float x : out) bad += !std::isfinite(x);
	CHECK(L.g == 1.f && bad == 0, "1e18 everywhere under +24 dB of tilt: g %g, %d bins not finite", L.g, bad);
	const double inf = std::numeric_limits<double>::infinity();
	CHECK(Tables::makeupFrom(CombPower{0.0, 1.0}) == 1.f && Tables::makeupFrom(CombPower{1.0, 0.0}) == 1.f &&
	          Tables::makeupFrom(CombPower{inf, 1.0}) == 1.f && Tables::makeupFrom(CombPower{1.0, inf}) == 1.f &&
	          Tables::makeupFrom(CombPower{4.0, 1.0}) == 2.f && Tables::makeupFrom(CombPower{100.0, 1.0}) == 8.f &&
	          Tables::makeupFrom(CombPower{1.0, 2.0}) == 1.f,
	      "makeupFrom at its edges");
	// Two sides, one scale: comb(l, r, g) gives both the same gain per bin.
	L.set(combSpec(220.f, 8, 25.f, 0.7f));
	std::vector<float> l = noise(kBins, 3), r = noise(kBins, 4);
	CombPower p;
	L.t.apply(l.data(), L.scratch.data(), p);
	const std::vector<float> l0 = l;
	L.t.apply(r.data(), L.scratch.data(), p);
	const std::vector<float> r0 = r;
	const float g = Tables::makeupFrom(p);
	L.t.comb(l.data(), r.data(), g);
	int differ = 0;
	for (int k = cut; k < kBins; k++)
		if (l0[k] > 0.f && r0[k] > 0.f && std::fabs(l[k] / l0[k] - r[k] / r0[k]) > 1e-6f * (l[k] / l0[k])) differ++;
	CHECK(g > 1.f && differ == 0, "both sides one gain per bin (g %g): %d bins differ", g, differ);
}

// ---------------------------------------------------------------- rebuild

TableSpec busySpec(int bins) {
	TableSpec s = restSpec(bins, 96000.f);
	s.shiftRatio = 1.3f;
	s.blurOctaves = 0.7f;
	s.tiltDbPerOct = -4.f;
	s.harmonics = 0.6f;
	s.roots = 16;
	for (int i = 0; i < 16; i++) s.rootHz[i] = 40.f * std::pow(2.f, i / 5.f);
	s.partials = 32;
	s.widthCents = 300.f;
	return s;
}

void rebuild_in_parts() {
	const int bins = 65536;
	const TableSpec A = busySpec(bins);
	std::unique_ptr<Live> once(new Live(bins)), parts(new Live(bins));
	once->t.start(A);
	CHECK(once->t.work(1 << 30), "one big call finishes");
	CHECK(once->t.swapIfReady(), "and swaps");
	parts->t.start(A);
	int calls = 1;
	while (!parts->t.work(64)) calls++;
	CHECK(calls == (512 + 65536 * 22 + 63) / 64, "64 at a time: %d calls", calls);
	CHECK(parts->t.swapIfReady(), "swaps");
	CHECK(same(once->t.live(), parts->t.live()), "same spec");
	for (uint32_t seed : {1u, 2u}) {
		const std::vector<float> in = noise(bins, seed);
		const std::vector<float> a = once->run(in), b = parts->run(in);
		CHECK(std::memcmp(a.data(), b.data(), bins * sizeof(float)) == 0, "seed %u: the same to the bit", seed);
	}
	// The worst case's count, as Tables.hpp states it.
	for (int budget : {2048, 4096}) {
		once->t.start(restSpec(bins));
		while (!once->t.work(1 << 30)) {}
		once->t.swapIfReady();
		once->t.start(A);
		int n = 1;
		while (!once->t.work(budget)) n++;
		CHECK(n == (budget == 2048 ? 705 : 353), "worst case, budget %d: %d calls", budget, n);
		once->t.swapIfReady();
	}
	const std::vector<float> ones(bins, 1.f);
	CHECK(std::memcmp(once->run(ones).data(), parts->run(ones).data(), bins * sizeof(float)) == 0, "ones: the same to the bit");

	// The live set holds until swapIfReady.
	Live L(kBins);
	L.set(restSpec());
	const std::vector<float> in = noise(kBins, 9);
	const std::vector<float> before = L.run(in);
	TableSpec B = busySpec(kBins);
	B.rate = kRate;
	L.t.start(B);
	CHECK(!L.t.swapIfReady(), "nothing to swap mid-build");
	int n = 0;
	while (!L.t.work(64)) {
		n++;
		const std::vector<float> mid = L.run(in);
		if (std::memcmp(mid.data(), before.data(), kBins * sizeof(float)) != 0) {
			CHECK(false, "live set changed during the build, call %d", n);
			break;
		}
	}
	CHECK(std::memcmp(L.run(in).data(), before.data(), kBins * sizeof(float)) == 0, "built, not yet swapped: unchanged");
	CHECK(L.t.live().shiftRatio == 1.f && L.t.live().roots == 0 && !L.t.combOn(), "live spec still the old one");
	CHECK(L.t.work(64), "stays whole");
	CHECK(L.t.swapIfReady(), "swaps at the boundary");
	CHECK(!L.t.swapIfReady(), "once");
	CHECK(L.t.live().shiftRatio == 1.3f && L.t.live().roots == 16, "the new spec is live");
	CHECK(std::memcmp(L.run(in).data(), before.data(), kBins * sizeof(float)) != 0, "and sounds");

	// A start during a rebuild is taken up after it, at the newest values.
	TableSpec C = restSpec();
	C.tiltDbPerOct = 2.f;
	TableSpec D = restSpec();
	D.tiltDbPerOct = -2.f;
	TableSpec E = restSpec();
	E.blurOctaves = 0.25f;
	L.t.start(C);
	CHECK(!L.t.work(64), "C under way");
	L.t.start(D);
	L.t.start(E);
	while (!L.t.work(64)) {}
	CHECK(L.t.swapIfReady(), "C swaps");
	CHECK(L.t.live().tiltDbPerOct == 2.f, "C, begun first, is finished first: tilt %g", L.t.live().tiltDbPerOct);
	CHECK(!L.t.work(1), "E begins after the swap");
	while (!L.t.work(64)) {}
	CHECK(L.t.swapIfReady(), "E swaps");
	CHECK(L.t.live().blurOctaves == 0.25f && L.t.live().tiltDbPerOct == 0.f, "E, the newest, is live; D was passed over");
	CHECK(L.t.work(64) && !L.t.swapIfReady(), "nothing more to do");
	// A start with the spec already live does nothing; one that returns to
	// the spec under way, mid-build, drops the one waiting.
	L.t.start(E);
	CHECK(L.t.work(1) && !L.t.swapIfReady(), "the live spec again: no rebuild");
	L.t.start(C);
	L.t.start(D);
	L.t.start(C);
	while (!L.t.work(64)) {}
	CHECK(L.t.swapIfReady() && L.t.live().tiltDbPerOct == 2.f, "C live");
	CHECK(L.t.work(64) && !L.t.swapIfReady(), "D was dropped");
	// A start while a built set waits for its boundary waits too.
	L.t.start(D);
	while (!L.t.work(64)) {}
	L.t.start(E);
	CHECK(L.t.work(64), "D waits, whole");
	CHECK(L.t.swapIfReady() && L.t.live().tiltDbPerOct == -2.f, "D live");
	while (!L.t.work(64)) {}
	CHECK(L.t.swapIfReady() && L.t.live().blurOctaves == 0.25f, "then E");
}

// Review Focus 4: hostile roots and the ends of every range.
void hostile_roots() {
	Live L;
	const float bad[] = {0.f, 1e-3f, 30000.f, 1e9f, kNaN, kInf, -kInf, -440.f};
	for (float width : {1e-6f, 5.f, 300.f, 1e6f, kNaN})
		for (float rate : {44100.f, 48000.f, 96000.f}) {
			TableSpec s = combSpec(0.f, 32, width);
			s.rate = rate;
			s.roots = 16;
			for (int i = 0; i < 8; i++) s.rootHz[i] = bad[i];
			for (int i = 8; i < 16; i++) s.rootHz[i] = 20.f * std::pow(3.f, float(i - 8));  // 20 Hz .. 44 kHz
			L.set(s);
			const std::vector<float> g = L.run(std::vector<float>(kBins, 1.f));
			int badGain = 0;
			for (float x : g)
				if (!(x >= 0.f && x <= 8.f)) badGain++;
			CHECK(badGain == 0, "width %g, %g Hz: %d gains outside [0, 8]", width, rate, badGain);
			CHECK(std::isfinite(L.g) && L.g >= 1.f && L.g <= 8.f, "makeup %g", L.g);
			// All bad: an empty comb (30 kHz is in the band at 96 kHz; 0.625 x
			// the rate never is).
			for (int i = 8; i < 16; i++) s.rootHz[i] = bad[i - 8];
			s.rootHz[2] = s.rootHz[10] = 0.625f * rate;
			L.set(s);
			const std::vector<float> z = L.run(std::vector<float>(kBins, 1.f));
			double sum = 0;
			for (float x : z) sum += x;
			CHECK(sum == 0.0, "width %g, %g Hz: an empty comb passes %g", width, rate, sum);
			CHECK(L.g == 1.f, "empty comb: makeup %g (after is 0)", L.g);
		}
	// Counts past their ends, NaN everywhere else.
	TableSpec s = combSpec(110.f, 99, 25.f);
	s.roots = 99;
	for (int i = 0; i < 16; i++) s.rootHz[i] = 110.f;
	s.shiftRatio = kNaN;
	s.blurOctaves = kNaN;
	s.tiltDbPerOct = kNaN;
	L.set(s);
	std::vector<float> g = L.run(std::vector<float>(kBins, 1.f));
	int badGain = 0;
	for (float x : g)
		if (!(x >= 0.f && x <= 8.f)) badGain++;
	CHECK(badGain == 0, "counts past their ends, NaN controls: %d bad gains", badGain);
	CHECK(L.t.live().roots == 16 && L.t.live().partials == 32, "held to 16 roots, 32 partials: %d %d", L.t.live().roots,
	      L.t.live().partials);
	s.harmonics = kNaN;
	s.roots = -4;
	s.partials = -4;
	s.shiftRatio = kInf;
	s.blurOctaves = kInf;
	s.tiltDbPerOct = -kInf;
	s.bins = 1 << 30;
	s.rate = kNaN;
	L.set(s);
	g = L.run(std::vector<float>(L.t.live().bins, 1.f));
	badGain = 0;
	for (float x : g)
		if (!std::isfinite(x) || x < 0.f) badGain++;
	CHECK(badGain == 0, "inf and NaN controls, bins past the allocation: %d bad", badGain);
	CHECK(L.t.live().bins == 65536, "bins held to the allocation: %d", L.t.live().bins);
	// Before allocate, nothing to apply to; no crash.
	Tables none;
	float x = 1.f;
	none.apply(&x, &x);
	CHECK(none.work(64) && !none.swapIfReady(), "unallocated: idle");
}

} // namespace

int main() {
	rest_is_exact();
	low_cut();
	shift_octave_up();
	shift_octave_down();
	shift_seven_semitones();
	shift_past_nyquist_is_dropped();
	shift_up_leaves_no_gaps();
	shift_comes_first();
	blur_keeps_energy();
	blur_width_at_full();
	blur_same_in_bass_and_treble();
	tilt_slope();
	comb_teeth();
	comb_count();
	comb_stops_at_nyquist();
	comb_width();
	comb_low_note_keeps_its_teeth();
	comb_chord();
	harmonics_zero_is_out();
	harmonics_full_passes_only_teeth();
	makeup_formula_and_bound();
	comb_makeup_measured();
	rebuild_in_parts();
	hostile_roots();
	return rkatest::finish("doldrums/test_tables");
}

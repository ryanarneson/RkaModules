// rka::BandLimitedRead (src/BandLimitedRead.hpp), apart from Eddy's engine:
// the Hermite read to the bit at |speed| <= 1, the band limit above it (no
// alias above the target, the passband kept), the crossfade between them,
// and the edges. Built with the plugin's floating-point flags
// (PLUGIN_MATH_TESTS in tests.mk), so "to the bit" is what the plugin does.
#include "BandLimitedRead.hpp"
#include "testing.hpp"
#include "testing_alias.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using rkatest::AliasMeter;

namespace {

const double PI = 3.14159265358979323846;

bool sameBits(float a, float b) {
	return std::memcmp(&a, &b, sizeof(float)) == 0;
}

struct Noise {
	uint32_t s = 0x2545f491u;
	float next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (float) ((double) s / 4294967296.0 * 2.0 - 1.0);
	}
};

// A tape of n frames (and `tail` more) from gen(k, side).
template <class Gen>
void fill(rka::Tape& t, size_t n, size_t tail, Gen gen) {
	t.allocate(n, tail > 0 ? tail : 1);
	t.length = n;
	t.tail = tail;
	for (size_t k = 0; k < n + tail; k++)
		for (int c = 0; c < 2; c++)
			t.ch[c][k] = gen(k, c);
}

// The reader is large (its table): one for every test.
const rka::BandLimitedRead& reader() {
	static const rka::BandLimitedRead r;
	return r;
}

// At |speed| <= 1 the reader calls Tape::read (readWrapped). Compiled into
// another caller, -funsafe-math-optimizations may contract the Hermite
// polynomial differently (1.7e-6 V on a 5 V noise tape at worst, where the
// taps cancel), so here the fractional reads are held to 1e-5 V and whole
// frames to the bit. That
// Eddy's engine is unchanged to the bit at these speeds is test_engine's
// (speed 1) and was checked against the engine before this change on 24 MB
// of output and tape (2026-11-22, task 10c report).
void hermiteAtOrBelowOne() {
	rka::Tape t;
	Noise nz;
	fill(t, 4000, 40, [&](size_t, int) { return 5.f * nz.next(); });
	Noise pos;
	double worst = 0.0;
	bool whole = true;
	for (float s : {1.f, -1.f, 0.999f, 0.5f, -0.25f, 0.f, 1e-9f, std::nanf("")})
		for (int i = 0; i < 2000; i++) {
			const double p = 2100.0 + 2200.0 * (double) pos.next();   // some past the end, some before 0
			for (bool wrap : {false, true}) {
				float o[2];
				reader().read(t, p, s, wrap, o);
				for (int c = 0; c < 2; c++)
					worst = std::fmax(worst, std::fabs(o[c] - (wrap ? t.readWrapped(c, p) : t.read(c, p))));
				const double k = std::floor(p);
				reader().read(t, k, s, wrap, o);
				for (int c = 0; c < 2; c++)
					whole = whole && sameBits(o[c], wrap ? t.readWrapped(c, k) : t.read(c, k));
			}
		}
	CHECK(worst < 1e-5, "|speed| <= 1 (and NaN): the Hermite read (worst %.2g V)", worst);
	CHECK(whole, "|speed| <= 1 (and NaN), at whole frames: the Hermite read, to the bit");
}

void constantReadsItself() {
	rka::Tape t;
	fill(t, 3000, 30, [](size_t, int c) { return c ? -3.25f : 4.5f; });
	double worst = 0.0;
	for (float s : {1.1f, 1.3f, 2.f, 2.37f, 4.f, -3.f, 9.f})
		for (double p : {0.0, 0.4, 17.3, 1500.61, 2999.0, 3029.0, 3100.0, -5.0})
			for (bool wrap : {false, true}) {
				float o[2];
				reader().read(t, p, s, wrap, o);
				worst = std::fmax(worst, std::fabs(o[0] - 4.5));
				worst = std::fmax(worst, std::fabs(o[1] + 3.25));
			}
	// Measured 2026-11-22: 2.9e-6 V.
	CHECK(worst < 1e-5, "a constant reads itself at any speed and place, edges included (worst error %.2g V)", worst);
}

// A 5 V sine at `ratio` of the tape's rate read at speed s from a
// fractional frame: the strongest component other than the played tone (if
// it lands below Nyquist), dB re 5 V, and the tone's level.
struct Swept {
	double other, level, lands;
};
Swept sweepOne(rka::Tape& t, double ratio, double s) {
	for (size_t k = 0; k < t.length; k++)
		t.ch[0][k] = t.ch[1][k] = (float) (5.0 * std::sin(2.0 * PI * ratio * (double) k + 0.1));
	const size_t N = 16384;
	std::vector<float> y(N);
	double p = s > 0 ? 1000.37 : (double) t.length - 1000.37;
	for (size_t i = 0; i < N; i++) {
		float o[2];
		reader().read(t, p, (float) s, false, o);
		y[i] = o[0];
		p += s;
	}
	const AliasMeter m(y.data(), N, 1.0);   // frequencies as fractions of the rate
	Swept w;
	w.lands = 2.0 * ratio * std::fabs(s);   // of Nyquist
	std::vector<double> want;
	if (w.lands < 1.0)
		want.push_back(ratio * std::fabs(s));
	w.other = m.otherDb(want, 5.0);
	w.level = want.empty() ? -999.0 : AliasMeter::db(m.amplitude(ratio * std::fabs(s)) / 5.0);
	return w;
}

void response() {
	rka::Tape t;
	fill(t, 80000, 0, [](size_t, int) { return 0.f; });
	for (double s : {1.5, 2.0, 2.37, 3.0, 4.0, -2.0, -4.0}) {
		double alias = -999.0, aliasAt = 0.0, lo = 0.0, hi = 0.0;
		for (double r = 0.005; r < 0.4999; r += 0.0049) {
			const Swept w = sweepOne(t, r, s);
			if (w.lands >= 1.0) {
				if (w.other > alias) {
					alias = w.other;
					aliasAt = r;
				}
			}
			if (w.lands <= 0.8) {
				lo = std::fmin(lo, w.level);
				hi = std::fmax(hi, w.level);
			}
		}
		// The target (brief 10c, B3): 50 dB below a full tone for anything
		// landing above Nyquist. Measured 2026-11-22: -57.1 dB at worst
		// (speed 3, content landing on Nyquist itself), -59 to -62 elsewhere.
		CHECK(alias <= -50.0, "speed %.2f: the strongest alias of tape content landing above Nyquist is %.1f dB (tone at %.3f fs)",
		      s, alias, aliasAt);
		// Measured 2026-11-22: within -0.005 .. +0.010 dB.
		CHECK(lo >= -0.1 && hi <= 0.1, "speed %.2f: content landing up to 0.8 of Nyquist keeps its level (%.3f .. %.3f dB)", s,
		      lo, hi);
	}
}

void blendIsContinuous() {
	rka::Tape t;
	fill(t, 4000, 0, [](size_t k, int) { return (float) (5.0 * std::sin(2.0 * PI * 0.31 * (double) k)); });
	double atOne = 0.0, atEnd = 0.0, step = 0.0;
	const float e = 1e-5f;
	const float b = 1.f + rka::BandLimitedRead::BLEND;
	for (double p = 1000.0; p < 1100.0; p += 0.37) {
		float a[2], c[2];
		reader().read(t, p, 1.f + e, false, a);
		atOne = std::fmax(atOne, std::fabs(a[0] - t.read(0, p)));
		reader().read(t, p, b - e, false, a);
		reader().read(t, p, b + e, false, c);
		atEnd = std::fmax(atEnd, std::fabs(a[0] - c[0]));
		// Across the blend in small steps: no jump bigger than the speed's own change makes.
		float prev[2];
		reader().read(t, p, 1.f, false, prev);
		for (float s = 1.f + 0.001f; s <= 1.4f; s += 0.001f) {
			reader().read(t, p, s, false, a);
			step = std::fmax(step, std::fabs(a[0] - prev[0]));
			prev[0] = a[0];
		}
	}
	// A 5 V tone at 0.31 of the rate, where Hermite and the band-limited
	// read differ most: the blend meets each within 1e-3 V at its ends, and
	// a speed step of 0.001 moves the output by at most 0.05 V.
	// Measured 2026-11-22: 4.9e-5 V, 5.5e-5 V and 0.022 V.
	CHECK(atOne < 1e-3, "just above speed 1 the read is the Hermite read (%.2g V)", atOne);
	CHECK(atEnd < 1e-3, "at 1 + BLEND the blend meets the band-limited read (%.2g V)", atEnd);
	CHECK(step < 0.05, "a speed step of 0.001 between 1 and 1.4 moves the read by at most %.3g V", step);
}

void wrapsAtTheSeam() {
	// A sine with a whole number of cycles on a wrapping tape: read across
	// the seam it is the same as far from it.
	rka::Tape t;
	const size_t n = 4000;   // 4000 x 0.1175 = 470 cycles
	fill(t, n, 0, [](size_t k, int) { return (float) (5.0 * std::sin(2.0 * PI * 0.1175 * (double) k)); });
	double worst = 0.0;
	for (float s : {1.6f, 3.f, -2.5f})
		for (double d = -40.0; d <= 40.0; d += 0.7) {
			float seam[2], mid[2];
			reader().read(t, (double) n + d, s, true, seam);   // past the end: wraps
			reader().read(t, 2000.0 + d, s, true, mid);
			worst = std::fmax(worst, std::fabs(seam[0] - mid[0]));
		}
	// Measured 2026-11-22: 0 (the same bits).
	CHECK(worst < 1e-3, "a wrapped read across the seam reads as it does away from it (%.2g V)", worst);
}

void hostile() {
	rka::Tape t;
	Noise nz;
	fill(t, 100, 3, [&](size_t, int) { return 5.f * nz.next(); });
	bool finite = true;
	for (float s : {4.f, 1e30f, -INFINITY, INFINITY})
		for (double p : {0.0, 50.5, 102.9, 1e300, -1e300, (double) NAN, (double) INFINITY})
			for (bool wrap : {false, true}) {
				float o[2];
				reader().read(t, p, s, wrap, o);
				finite = finite && std::isfinite(o[0]) && std::isfinite(o[1]);
			}
	CHECK(finite, "infinite speeds and positions read finite values");
	// Wider than 4 reads as 4.
	float a[2], b[2];
	reader().read(t, 50.5, 4.f, false, a);
	reader().read(t, 50.5, 1e9f, false, b);
	CHECK(sameBits(a[0], b[0]), "a speed over 4 reads as 4");
	rka::Tape empty;
	float o[2] = {1.f, 1.f};
	reader().read(empty, 3.0, 2.f, false, o);
	CHECK(o[0] == 0.f && o[1] == 0.f, "an unallocated tape reads 0");
}

} // namespace

int main() {
	hermiteAtOrBelowOne();
	constantReadsItself();
	response();
	blendIsContinuous();
	wrapsAtTheSeam();
	hostile();
	return rkatest::finish("eddy/test_band_read");
}

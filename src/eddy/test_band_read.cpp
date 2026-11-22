// rka::BandLimitedRead (src/BandLimitedRead.hpp), apart from Eddy's engine:
// whole frames read to the bit at 1 and slower, the interpolation between
// them (flat, images held down), the band limit above 1 (no alias above the
// target, the passband kept), the crossfade between them, and the edges. Built with the plugin's floating-point flags
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

bool table = false;   // --table: print the measurements instead of checking them

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

// At 1 and slower a whole frame reads itself, to the bit, at the edges and
// wrapped too; and the read does not depend on the speed there (stopped,
// reversing, a step per sample going to 0, NaN).
void atOneAndSlower() {
	rka::Tape t;
	Noise nz;
	fill(t, 4000, 40, [&](size_t, int) { return 5.f * nz.next(); });
	Noise pos;
	bool whole = true;
	double apart = 0.0;
	for (int i = 0; i < 3000; i++) {
		const double p = std::floor(2020.0 + 2040.0 * (double) pos.next());   // the edges included
		const double q = p + 0.5 + 0.49 * (double) pos.next();
		float ref[2];
		reader().read(t, q, 1.f, false, ref);
		for (float s : {1.f, -1.f, 0.999f, 0.5f, -0.25f, 0.f, 1e-9f, -1e-9f, std::nanf("")})
			for (bool wrap : {false, true}) {
				float o[2];
				reader().read(t, p, s, wrap, o);
				// Held to the tape and its tail, or wrapped round the tape.
				const long long ip = (long long) p;
				const long long k = wrap ? ((ip % 4000) + 4000) % 4000 : ip < 0 ? 0 : ip > 4039 ? 4039 : ip;
				if (!(sameBits(o[0], t.ch[0][k]) && sameBits(o[1], t.ch[1][k]))) {
					if (whole)
						std::printf("  speed %g, frame %.0f%s: %.9g, the frame %.9g\n", s, p, wrap ? " wrapped" : "", o[0], t.ch[0][k]);
					whole = false;
				}
				if (!wrap) {
					reader().read(t, q, s, false, o);
					apart = std::fmax(apart, std::fmax(std::fabs(o[0] - ref[0]), std::fabs(o[1] - ref[1])));
				}
			}
	}
	CHECK(whole, "at 1 and slower (and stopped, reversing, NaN) a whole frame reads itself, to the bit");
	// The same code at every speed; compiled into two callers, fast maths
	// may round it differently (1.2e-7 V seen), hence not to the bit.
	CHECK(apart < 1e-5, "at 1 and slower the read between frames is the same at every speed (%.2g V apart)", apart);
}

void constantReadsItself() {
	rka::Tape t;
	fill(t, 3000, 30, [](size_t, int c) { return c ? -3.25f : 4.5f; });
	double worst = 0.0;
	for (float s : {0.f, 0.5f, 1.f, 1.1f, 1.3f, 2.f, 2.37f, 4.f, -3.f, 9.f})
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
	for (double s : {1.02, 1.05, 1.1, 1.15, 1.2, 1.26, 1.5, 2.0, 2.37, 3.0, 4.0, -1.05, -2.0, -4.0}) {
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
		if (table) {
			std::printf("fast speed %+.2f: alias %.1f dB (tone at %.3f fs); passband %.3f .. %.3f dB\n", s, alias, aliasAt, lo, hi);
			continue;
		}
		// The target (brief 10c, B3; from x1.02 up, fix round 1): 50 dB below
		// a full tone for anything landing above Nyquist. Measured
		// 2026-11-22: -57.1 dB at worst (speed 3, content landing on Nyquist
		// itself), -57.4 at 1.26, -59 to -62 elsewhere from 1.02 to 4.
		CHECK(alias <= -50.0, "speed %.2f: the strongest alias of tape content landing above Nyquist is %.1f dB (tone at %.3f fs)",
		      s, alias, aliasAt);
		// Measured 2026-11-22: within -0.005 .. +0.010 dB.
		CHECK(lo >= -0.1 && hi <= 0.1, "speed %.2f: content landing up to 0.8 of Nyquist keeps its level (%.3f .. %.3f dB)", s,
		      lo, hi);
	}
}

// At 1 and slower: tape content up to 0.8 of the tape's Nyquist (0.4 of
// its rate) keeps its level and leaves no images above SLOW_IMAGES_DB;
// content between 0.4 and 0.5 of the rate is rolled off (-6 dB at its
// Nyquist) and its images are recorded, not bounded.
// Measured 2026-11-22: images -65.6 dB at worst (content at 0.397 of the
// rate), at 0.5, 0.25, 0.1 and backwards alike; levels -0.004 .. +0.008
// dB; above 0.4 of the rate the images reach -7.8 dB (Hermite: -11 dB
// already at 0.4).
const double SLOW_IMAGES_DB = -62.6;

void slowResponse() {
	rka::Tape t;
	fill(t, 80000, 0, [](size_t, int) { return 0.f; });
	for (double s : {1.0, 0.5, 0.25, 0.1, -0.5, -0.25}) {
		double img = -999.0, imgAt = 0.0, top = -999.0, lo = 0.0, hi = 0.0;
		for (double r = 0.005; r < 0.4999; r += 0.0049) {
			const Swept w = sweepOne(t, r, s);
			if (r <= 0.4) {
				if (w.other > img) {
					img = w.other;
					imgAt = r;
				}
				lo = std::fmin(lo, w.level);
				hi = std::fmax(hi, w.level);
			}
			else
				top = std::fmax(top, w.other);
		}
		if (table) {
			std::printf("slow speed %+.2f: images (content to 0.4 fs) %.1f dB at %.3f fs; above 0.4 fs %.1f dB; level %.3f .. %.3f dB\n", s,
			            img, imgAt, top, lo, hi);
			continue;
		}
		CHECK(img <= SLOW_IMAGES_DB, "speed %.2f: the strongest image of content up to 0.4 of the rate is %.1f dB re 5 V (tone at %.3f fs)",
		      s, img, imgAt);
		CHECK(lo >= -0.1 && hi <= 0.1, "speed %.2f: content up to 0.4 of the rate keeps its level (%.3f .. %.3f dB)", s, lo, hi);
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
		reader().read(t, p, 1.f, false, c);
		atOne = std::fmax(atOne, std::fabs(a[0] - c[0]));
		reader().read(t, p, b - e, false, a);
		reader().read(t, p, b + e, false, c);
		atEnd = std::fmax(atEnd, std::fabs(a[0] - c[0]));
		// Across the blend in small steps: no jump bigger than the speed's own change makes.
		float prev[2];
		reader().read(t, p, 0.6f, false, prev);
		for (float s = 0.6f + 0.001f; s <= 1.4f; s += 0.001f) {
			reader().read(t, p, s, false, a);
			step = std::fmax(step, std::fabs(a[0] - prev[0]));
			prev[0] = a[0];
		}
	}
	// A 5 V tone at 0.31 of the rate, where the two kernels differ most: the
	// blend meets each within 1e-3 V at its ends, and a speed step of 0.001
	// anywhere from 0.6 to 1.4 moves the output by at most 0.05 V.
	// Measured 2026-11-22 (BLEND 0.02): 9.5e-7 V, 3.7e-5 V and 0.022 V.
	CHECK(atOne < 1e-3, "just above speed 1 the read is the speed-1 read (%.2g V)", atOne);
	CHECK(atEnd < 1e-3, "at 1 + BLEND the blend meets the band-limited read (%.2g V)", atEnd);
	CHECK(step < 0.05, "a speed step of 0.001 between 0.6 and 1.4 moves the read by at most %.3g V", step);
}

void wrapsAtTheSeam() {
	// A sine with a whole number of cycles on a wrapping tape: read across
	// the seam it is the same as far from it.
	rka::Tape t;
	const size_t n = 4000;   // 4000 x 0.1175 = 470 cycles
	fill(t, n, 0, [](size_t k, int) { return (float) (5.0 * std::sin(2.0 * PI * 0.1175 * (double) k)); });
	double worst = 0.0;
	for (float s : {0.f, 0.5f, 1.f, 1.6f, 3.f, -2.5f})
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

int main(int argc, char** argv) {
	table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
	atOneAndSlower();
	constantReadsItself();
	response();
	slowResponse();
	blendIsContinuous();
	wrapsAtTheSeam();
	hostile();
	return table ? 0 : rkatest::finish("eddy/test_band_read");
}

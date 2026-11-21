// rka::Tape and rka::TapeWriter (src/Tape.hpp): the still stereo buffer, its
// Hermite read in either direction, and the write head that travels with the
// read at any speed. Built with the plugin's floating-point flags
// (PLUGIN_MATH_TESTS in tests.mk), so "to the bit" is what the plugin does.
#include "Tape.hpp"
#include "testing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const double FS = 48000.0;
const double TWO_PI = 6.283185307179586;

// A small deterministic noise source (xorshift), for tape contents.
struct Noise {
	uint32_t s = 0x9e3779b9u;
	float next() {
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return (float) ((double) s / 4294967296.0 * 2.0 - 1.0);
	}
};

bool sameBits(float a, float b) {
	return std::memcmp(&a, &b, sizeof(float)) == 0;
}

// The tape's frames [0, n) of side c, for comparing whole tapes.
std::vector<float> frames(const rka::Tape& t, int c, size_t n) {
	return std::vector<float>(t.ch[c], t.ch[c] + n);
}

// Write `in(t)` with the head at p(t) = start + speed * t, t = 0..n, as the
// engine will: one call per sample from the last position to this one. The
// first call stands still and only primes the head's memory of the input.
template <class In, class Mix>
void run(rka::Tape& tape, rka::TapeWriter& w, double start, double speed, int n, In in, Mix mix) {
	double p = start;
	float x[2] = {in(0, 0), in(1, 0)};
	w.write(tape, p, p, x, mix);
	for (int t = 1; t <= n; t++) {
		const double q = start + speed * t;
		x[0] = in(0, t);
		x[1] = in(1, t);
		w.write(tape, p, q, x, mix);
		p = q;
	}
}

auto replace = [](int, float, float x) { return x; };
auto keep = [](int, float old, float) { return old; };

// RMS of (read-back - input) over RMS of the input, in dB: write a sine of
// `hz` at `speed` frames per sample, then read it at the same positions.
double writeReadErrorDb(double hz, double speed, double start) {
	rka::Tape tape;
	tape.allocate(200000, 480);
	tape.length = 200000;
	rka::TapeWriter w;
	w.reset();
	const int n = 20000;
	auto sine = [hz](int c, int t) { return (float) std::sin(TWO_PI * hz * t / FS + c * 0.5); };
	run(tape, w, start, speed, n, sine, replace);
	double err = 0, ref = 0;
	for (int t = 64; t <= n - 64; t++) {
		const double p = start + speed * t;
		for (int c = 0; c < 2; c++) {
			const double x = sine(c, t);
			const double y = tape.read(c, p);
			err += (y - x) * (y - x);
			ref += x * x;
		}
	}
	return 10.0 * std::log10(err / ref + 1e-30);
}

void read_exact() {
	rka::Tape tape;
	CHECK(tape.allocate(1000, 10), "allocate");
	CHECK(tape.capacity() == 1000 && tape.tailCapacity() == 10, "capacity %zu tail %zu", tape.capacity(),
	      tape.tailCapacity());
	tape.length = 1000;
	tape.tail = 10;
	Noise nz;
	for (int i = 0; i < 1010; i++) {
		tape.ch[0][i] = nz.next();
		tape.ch[1][i] = nz.next();
	}
	int bad = 0;
	for (int i = 0; i < 1010; i++)
		for (int c = 0; c < 2; c++)
			bad += !sameBits(tape.read(c, (double) i), tape.ch[c][i]);
	CHECK(bad == 0, "%d whole-frame reads are not the stored sample to the bit", bad);
	// The wrapped read at whole frames too, including past the end.
	bad = 0;
	for (int i = 0; i < 1000; i++)
		bad += !sameBits(tape.readWrapped(0, (double) i + 3000.0), tape.ch[0][i]);
	CHECK(bad == 0, "%d wrapped whole-frame reads are not the stored sample", bad);

	// A ramp reads midway at .5 (Hermite reproduces a line).
	for (int i = 0; i < 1010; i++)
		tape.ch[0][i] = tape.ch[1][i] = 0.001f * i;
	double worst = 0;
	for (int i = 2; i < 1005; i++)
		worst = std::fmax(worst, std::fabs(tape.read(1, i + 0.5) - 0.001 * (i + 0.5)));
	CHECK(worst < 1e-6, "a ramp at .5 is off midway by %g", worst);
	// Wrapped: a window passing the end reads across the seam from both sides.
	// Frames 998, 999 | 0, 1 make a line 0, 1, 2, 3 only if the read wraps.
	tape.ch[0][998] = 0.f;
	tape.ch[0][999] = 1.f;
	tape.ch[0][0] = 2.f;
	tape.ch[0][1] = 3.f;
	const float seam = tape.readWrapped(0, 999.5);
	CHECK(std::fabs(seam - 1.5f) < 1e-6f, "the wrapped read across the seam reads %g, not 1.5", seam);
	const float seamBack = tape.readWrapped(0, -0.5);
	CHECK(std::fabs(seamBack - 1.5f) < 1e-6f, "the wrapped read at -0.5 reads %g, not 1.5", seamBack);
}

void read_backwards() {
	rka::Tape tape;
	tape.allocate(48000, 0);
	tape.length = 48000;
	const double hz = 440.0;
	for (int i = 0; i < 48000; i++)
		tape.ch[0][i] = tape.ch[1][i] = (float) std::sin(TWO_PI * hz * i / FS);
	// Read from 40000.3 down at 0.7 frames a sample: the phase runs the other way.
	double err = 0, ref = 0, fwdRms = 0, backRms = 0;
	const int n = 40000;
	for (int t = 0; t < n; t++) {
		const double p = 40000.3 - 0.7 * t;
		const double y = tape.read(0, p);
		const double want = std::sin(TWO_PI * hz * p / FS);
		err += (y - want) * (y - want);
		ref += want * want;
		backRms += y * y;
		const double yf = tape.read(0, 100.3 + 0.7 * t);
		fwdRms += yf * yf;
	}
	const double errDb = 10.0 * std::log10(err / ref);
	const double levelDb = 10.0 * std::log10(backRms / fwdRms);
	std::printf("  backwards at 0.7: error %.1f dB, level vs forwards %+.3f dB\n", errDb, levelDb);
	CHECK(errDb < -60.0, "a sine read backwards is not the sine with its phase reversed (%.1f dB)", errDb);
	CHECK(std::fabs(levelDb) < 0.05, "backwards plays at a different level (%+.3f dB)", levelDb);
	// A falling read is not the rising one: the error against the forward
	// phase is large, so the check above can tell the directions apart.
	double errFwd = 0;
	for (int t = 0; t < 2000; t++) {
		const double p = 40000.3 - 0.7 * t;
		const double y = tape.read(0, p);
		const double wrong = std::sin(TWO_PI * hz * (40000.3 + 0.7 * t) / FS);
		errFwd += (y - wrong) * (y - wrong);
	}
	CHECK(errFwd > 100.0, "the reversed read also matches the forward phase (%g)", errFwd);
}

void read_bounds() {
	// A short tape with a tail inside a bigger allocation; everything that is
	// not tape or tail (the guards, and the unused capacity, as a longer tape
	// would leave it) holds a huge sentinel, so a read that strays shows.
	rka::Tape tape;
	tape.allocate(100, 8);
	tape.length = 20;
	tape.tail = 3;
	const float SENTINEL = 1e30f;
	for (int c = 0; c < 2; c++) {
		for (int i = -rka::Tape::GUARD; i < 108 + rka::Tape::GUARD; i++)
			tape.ch[c][i] = SENTINEL;
		for (int i = 0; i < 23; i++)
			tape.ch[c][i] = 0.1f * (i + 1);
	}
	const double ps[] = {-1.0, -1e30, NAN, INFINITY, -INFINITY, 1e12, 23.0, 22.999, 22.5, 21.9, 21.5, 19.5, 0.0, 0.5, 1e-300};
	int bad = 0;
	for (double p : ps)
		for (int c = 0; c < 2; c++) {
			const float a = tape.read(c, p);
			const float b = tape.readWrapped(c, p);
			if (!(std::isfinite(a) && std::fabs(a) <= 2.4f && std::isfinite(b) && std::fabs(b) <= 2.4f)) {
				bad++;
				std::printf("  p %g: read %g, wrapped %g\n", p, a, b);
			}
		}
	CHECK(bad == 0, "%d reads strayed outside the tape and its tail", bad);
	CHECK(tape.read(0, 1e12) == tape.ch[0][22], "a read past the end is the last frame of the tail (%g)",
	      tape.read(0, 1e12));
	CHECK(tape.read(0, -1.0) == tape.ch[0][0], "a read before the start is the first frame (%g)", tape.read(0, -1.0));
	CHECK(tape.read(0, NAN) == tape.ch[0][0], "a NaN read is the first frame (%g)", tape.read(0, NAN));
	// The wrapped read never reaches the tail.
	const float w = tape.readWrapped(0, 19.0);
	CHECK(w == tape.ch[0][19], "wrapped at 19 reads %g", w);

	// Lengths that are wrong or empty: nothing is read outside the allocation.
	// The unused capacity now holds ordinary audio; only the guards keep the
	// sentinel, so a read past the allocation shows.
	for (int c = 0; c < 2; c++)
		for (int i = 0; i < 108; i++)
			tape.ch[c][i] = 0.5f;
	tape.length = 1000000;
	tape.tail = 1000000;
	bad = 0;
	for (double p : ps)
		for (int c = 0; c < 2; c++) {
			const float a = tape.read(c, p), b = tape.readWrapped(c, p);
			bad += !(std::isfinite(a) && std::fabs(a) <= 2.4f && std::isfinite(b) && std::fabs(b) <= 2.4f);
		}
	CHECK(bad == 0, "%d reads strayed outside the allocation with a length larger than it", bad);
	tape.length = 0;
	tape.tail = 0;
	CHECK(tape.read(0, 5.0) == 0.f && tape.readWrapped(0, 5.0) == 0.f, "an empty tape reads silence");
	tape.tail = 2;
	CHECK(tape.readWrapped(0, 5.0) == 0.f, "an empty tape with a tail reads silence wrapped");
	rka::Tape none;
	CHECK(none.read(0, 1.0) == 0.f && none.readWrapped(1, 1.0) == 0.f, "an unallocated tape reads silence");
}

void write_unity() {
	rka::Tape tape;
	tape.allocate(10000, 0);
	tape.length = 10000;
	rka::TapeWriter w;
	w.reset();
	Noise nz;
	std::vector<float> xs[2];
	for (int t = 0; t <= 5000; t++)
		for (int c = 0; c < 2; c++)
			xs[c].push_back(nz.next() * 5.f);
	auto in = [&](int c, int t) { return xs[c][t]; };
	// Forwards from frame 1000: at time t the head is at 1000 + t.
	run(tape, w, 1000.0, 1.0, 5000, in, replace);
	int bad = 0;
	for (int t = 0; t < 5000; t++)
		for (int c = 0; c < 2; c++)
			bad += !sameBits(tape.ch[c][1000 + t], xs[c][t]);
	CHECK(bad == 0, "%d frames written forwards at speed 1 are not the input to the bit", bad);
	CHECK(tape.ch[0][999] == 0.f && tape.ch[0][6000] == 0.f, "frames outside the run were written");
	// Backwards from 9000 the same (the head starts at 9000 and goes down, so
	// it passes 8999 first).
	w.reset();
	run(tape, w, 9000.0, -1.0, 2000, in, replace);
	bad = 0;
	for (int t = 1; t < 2000; t++)
		for (int c = 0; c < 2; c++)
			bad += !sameBits(tape.ch[c][9000 - t], xs[c][t]);
	CHECK(bad == 0, "%d frames written backwards at speed 1 are not the input to the bit", bad);
}

void write_half_speed() {
	const double e = writeReadErrorDb(220.0, 0.5, 1000.37);
	std::printf("  220 Hz written and read at 0.5: %.1f dB\n", e);
	CHECK(e < -40.0, "a 220 Hz sine at half speed comes back %.1f dB off", e);
	// Slower than 1 the head averages the input between frames instead of
	// dropping it: a 20 kHz tone (over the tape's Nyquist at half speed) is
	// cut, not folded back at full level.
	rka::Tape tape;
	tape.allocate(20000, 0);
	tape.length = 20000;
	rka::TapeWriter w;
	w.reset();
	auto hi = [](int, int t) { return (float) std::sin(TWO_PI * 20000.0 * t / FS); };
	run(tape, w, 100.37, 0.5, 30000, hi, replace);
	double sum = 0;
	for (int i = 200; i < 15000; i++)
		sum += (double) tape.ch[0][i] * tape.ch[0][i];
	const double db = 10.0 * std::log10(sum / (15000 - 200) / 0.5);
	std::printf("  20 kHz written at 0.5: on the tape at %.1f dB\n", db);
	CHECK(db < -12.0, "a 20 kHz tone written at half speed is on the tape at %.1f dB (dropped, not averaged)", db);
}

void write_reverse() {
	const double e = writeReadErrorDb(220.0, -1.0, 150000.37);
	std::printf("  220 Hz written and read at -1: %.1f dB\n", e);
	CHECK(e < -40.0, "a 220 Hz sine written backwards comes back %.1f dB off", e);
	const double e2 = writeReadErrorDb(220.0, -0.5, 150000.81);
	std::printf("  220 Hz written and read at -0.5: %.1f dB\n", e2);
	CHECK(e2 < -40.0, "a 220 Hz sine written backwards at half speed comes back %.1f dB off", e2);
}

void write_fast() {
	const double e = writeReadErrorDb(220.0, 1.5, 1000.37);
	std::printf("  220 Hz written and read at 1.5: %.1f dB\n", e);
	CHECK(e < -40.0, "a 220 Hz sine at 1.5 comes back %.1f dB off", e);
	const double e4 = writeReadErrorDb(220.0, -3.7, 190000.2);
	std::printf("  220 Hz written and read at -3.7: %.1f dB\n", e4);
	CHECK(e4 < -40.0, "a 220 Hz sine at -3.7 comes back %.1f dB off", e4);
}

void write_keeps() {
	rka::Tape tape;
	tape.allocate(60000, 100);
	tape.length = 60000;
	tape.tail = 100;
	Noise nz;
	for (int i = 0; i < 60100; i++) {
		tape.ch[0][i] = nz.next();
		tape.ch[1][i] = nz.next();
	}
	const std::vector<float> before0 = frames(tape, 0, 60100), before1 = frames(tape, 1, 60100);
	long calls = 0;
	auto counted = [&](int c, float old, float x) {
		calls++;
		return keep(c, old, x);
	};
	const double speeds[] = {0.13, 0.5, 0.99, 1.0, 1.5, 4.0, -0.13, -0.5, -1.0, -2.2, -4.0};
	for (double s : speeds) {
		rka::TapeWriter w;
		w.reset();
		run(tape, w, s > 0 ? 100.5 : 59900.25, s, 10000, [&](int, int) { return nz.next() * 10.f; }, counted);
	}
	CHECK(calls > 100000, "the head wrote only %ld frames", calls);
	CHECK(std::memcmp(before0.data(), tape.ch[0], 60100 * sizeof(float)) == 0 &&
	          std::memcmp(before1.data(), tape.ch[1], 60100 * sizeof(float)) == 0,
	      "mix returning old changed the tape");
}

// Every frame strictly between the first and last position is visited
// exactly once; none outside the run.
void checkVisits(const std::vector<int>& visits, double first, double last, const char* what) {
	const double lo = first < last ? first : last, hi = first < last ? last : first;
	int twice = 0, missed = 0, outside = 0;
	for (size_t i = 0; i < visits.size(); i++) {
		const double k = (double) i;
		if (k > lo + 1.0 && k < hi - 1.0) {
			twice += visits[i] > 1;
			missed += visits[i] == 0;
		}
		else if (k < lo - 1.0 || k > hi + 1.0)
			outside += visits[i] != 0;
		else
			twice += visits[i] > 1;
	}
	CHECK(twice == 0 && missed == 0 && outside == 0, "%s: %d frames visited twice, %d missed, %d outside", what,
	      twice, missed, outside);
}

void write_no_gaps() {
	const int N = 10000;
	rka::Tape tape;
	tape.allocate(100000, 0);
	tape.length = 100000;
	std::vector<int> visits(100000);
	// Each frame of side 0 holds its own index, so the mix sees which frame it is.
	for (int i = 0; i < 100000; i++)
		tape.ch[0][i] = (float) i;
	auto mark = [&](int c, float old, float) {
		if (c == 0)
			visits[(size_t) old]++;
		return old;
	};
	const double speeds[] = {0.13, 0.25, 0.5, 0.77, 1.0, 1.25, 2.0, 3.3, 4.0};
	Noise nz;
	for (double s : speeds)
		for (int dir = -1; dir <= 1; dir += 2) {
			std::fill(visits.begin(), visits.end(), 0);
			rka::TapeWriter w;
			w.reset();
			const double start = dir > 0 ? 50.31 : 99950.69;
			run(tape, w, start, dir * s, N, [](int, int) { return 0.f; }, mark);
			char what[64];
			std::snprintf(what, sizeof what, "speed %+.2f", dir * s);
			checkVisits(visits, start, start + dir * s * N, what);
		}
	// A speed that wanders over 0.13 .. 4 every sample, both ways.
	for (int dir = -1; dir <= 1; dir += 2) {
		std::fill(visits.begin(), visits.end(), 0);
		rka::TapeWriter w;
		w.reset();
		const double start = dir > 0 ? 10.4 : 99990.6;
		double p = start;
		const float x[2] = {0.f, 0.f};
		w.write(tape, p, p, x, mark);
		for (int t = 0; t < N; t++) {
			const double s = 0.13 + (4.0 - 0.13) * (0.5 + 0.5 * nz.next());
			const double q = p + dir * s;
			w.write(tape, p, q, x, mark);
			p = q;
		}
		checkVisits(visits, start, p, dir > 0 ? "wandering speed forwards" : "wandering speed backwards");
	}
	// A loop window the caller wraps by splitting the move: every frame of the
	// window is visited once per turn, give or take the turn in progress.
	for (double v : {-0.61, 0.61, -2.3, 2.3, -1.0, 1.0}) {
		std::fill(visits.begin(), visits.end(), 0);
		const double S = 1000.0, L = 777.0, speed = v;
		rka::TapeWriter w;
		w.reset();
		double p = S + 300.25;
		const float x[2] = {0.f, 0.f};
		w.write(tape, p, p, x, mark);
		double travelled = 0;
		for (int t = 0; t < N; t++) {
			double q = p + speed;
			if (q >= S + L) {
				w.write(tape, p, S + L, x, mark);
				p = S;
				q -= L;
			}
			else if (q < S) {
				w.write(tape, p, S, x, mark);
				p = S + L;
				q += L;
			}
			w.write(tape, p, q, x, mark);
			p = q;
			travelled += std::fabs(speed);
		}
		const int turns = (int) (travelled / L);
		int bad = 0, outside = 0;
		for (int i = 0; i < 100000; i++) {
			if (i >= (int) S && i < (int) (S + L))
				bad += visits[i] < turns || visits[i] > turns + 1;
			else
				outside += visits[i] != 0;
		}
		CHECK(bad == 0 && outside == 0, "a wrapped window (speed %+.2f, %d turns): %d frames not visited once a turn, %d outside",
		      speed, turns, bad, outside);
	}
}

void write_stall() {
	for (double speed : {1.0, 0.3, -0.3, 2.5}) {
		rka::Tape a, b;
		a.allocate(100000, 0);
		b.allocate(100000, 0);
		a.length = b.length = 100000;
		rka::TapeWriter wa, wb;
		wa.reset();
		wb.reset();
		long calls = 0;
		auto counted = [&](int, float, float x) {
			calls++;
			return x;
		};
		auto sine = [](int c, int t) { return (float) std::sin(TWO_PI * 330.0 * t / FS + c); };
		const double start = speed > 0 ? 1000.37 : 90000.37;
		// `a` moves 2000 samples, stands still for a second, then moves 2000 more.
		// `b` makes the same moves without the stop. During the stop the input
		// keeps changing; slower than 1 it must leave no trace, and at 1 or
		// faster only the input as the head leaves counts, so the stop's
		// input there ends on the value it started with.
		double p = start;
		float x[2] = {sine(0, 0), sine(1, 0)};
		wa.write(a, p, p, x, counted);
		wb.write(b, p, p, x, replace);
		int t = 1;
		for (; t <= 2000; t++) {
			x[0] = sine(0, t);
			x[1] = sine(1, t);
			wa.write(a, p, p + speed, x, counted);
			wb.write(b, p, p + speed, x, replace);
			p += speed;
		}
		const float last[2] = {x[0], x[1]};
		const long before = calls;
		for (int s = 0; s < 48000; s++) {
			float y[2] = {3.f * sine(0, s * 7), 3.f * sine(1, s * 7)};
			if (s == 47999 || std::fabs(speed) >= 1.0) {
				y[0] = last[0];
				y[1] = last[1];
			}
			wa.write(a, p, p, y, counted);
		}
		CHECK(calls == before, "speed %+.1f: %ld frames written while standing still", speed, calls - before);
		long most = 0;
		for (; t <= 4000; t++) {
			x[0] = sine(0, t);
			x[1] = sine(1, t);
			const long c0 = calls;
			wa.write(a, p, p + speed, x, counted);
			wb.write(b, p, p + speed, x, replace);
			p += speed;
			most = std::max(most, (calls - c0) / 2);
		}
		CHECK(most <= (long) std::ceil(std::fabs(speed)) + 1, "speed %+.1f: %ld frames written in one sample after the stop",
		      speed, most);
		int diff = 0;
		for (int i = 0; i < 100000; i++)
			diff += !sameBits(a.ch[0][i], b.ch[0][i]) + !sameBits(a.ch[1][i], b.ch[1][i]);
		CHECK(diff == 0, "speed %+.1f: the stop left %d frames different from a run without it", speed, diff);
	}
}

void hostile_writes() {
	// Not in the brief's list, but the writer's guards: NaN and huge inputs and
	// positions, a move over 4, an unallocated tape. Nothing outside the tape.
	rka::Tape tape;
	tape.allocate(1000, 10);
	tape.length = 1000;
	tape.tail = 10;
	for (int c = 0; c < 2; c++)
		for (int i = -rka::Tape::GUARD; i < 1010 + rka::Tape::GUARD; i++)
			tape.ch[c][i] = i < 0 || i >= 1010 ? 7.f : 0.f;
	rka::TapeWriter w;
	w.reset();
	long calls = 0;
	auto counted = [&](int, float, float x) {
		calls++;
		return x;
	};
	const float bad[2] = {NAN, INFINITY};
	w.write(tape, 10.0, 11.0, bad, counted);
	w.write(tape, 11.0, 12.0, bad, counted);
	CHECK(std::isfinite(tape.ch[0][11]) && std::isfinite(tape.ch[1][11]), "NaN and infinite input reached the tape");
	const float one[2] = {1.f, 1.f};
	const double ps[][2] = {{NAN, 5.0}, {5.0, NAN}, {INFINITY, 3.0}, {0.0, 1e9}, {1e18, 1e18 + 3}, {-3.0, 2.0},
	                        {1005.5, 1009.5}, {1009.5, 1013.0}, {2.0, -2.0}, {-1e300, -1e300 + 1}};
	const long c0 = calls;
	for (auto& p : ps)
		w.write(tape, p[0], p[1], one, counted);
	CHECK(calls - c0 < 40, "hostile moves wrote %ld frames", calls - c0);
	int strayed = 0;
	for (int c = 0; c < 2; c++)
		for (int i = -rka::Tape::GUARD; i < 0; i++)
			strayed += tape.ch[c][i] != 7.f || tape.ch[c][1010 - i - 1] != 7.f;
	CHECK(strayed == 0, "%d writes outside the tape and its tail", strayed);
	rka::Tape none;
	w.reset();
	const long c1 = calls;
	w.write(none, 0.0, 1.0, one, counted);
	w.write(none, 1.0, 2.0, one, counted);
	CHECK(calls == c1, "an unallocated tape was written");
}

} // namespace

int main() {
	read_exact();
	read_backwards();
	read_bounds();
	write_unity();
	write_half_speed();
	write_reverse();
	write_fast();
	write_keeps();
	write_no_gaps();
	write_stall();
	hostile_writes();
	return rkatest::finish("test_tape");
}

// Beacon under hostile conditions (plan Task 6), built with the address and
// undefined-behaviour sanitizers and a counting operator new: NaN, infinity
// and 1e30 on every input and parameter, MODE out of range, extreme and
// changing sample rates, GATE and RETRIG at audio rate on 16 channels, the
// channel count changing every block, seeded fuzz of everything, and no
// allocation in process(). Every output is finite and inside +/-10 V, INV is
// -OUT bit for bit, and the stage gates and TRIG are 0 or 10 V.
#include "testing.hpp"
#include "beacon/BeaconEngine.hpp"
#include "hadal/Random.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

static thread_local long tAllocs = 0;

void* operator new(std::size_t n) {
	tAllocs++;
	if (void* p = std::malloc(n ? n : 1))
		return p;
	throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
	std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
	std::free(p);
}

typedef rka::BeaconFrame F;

namespace {

// Each port's buffer is its own 16-float heap block, so ASan sees a read
// past it.
struct Rig {
	rka::BeaconEngine e;
	F f;
	rka::BeaconResult r;
	float* buf[6];   // attackCv, decayCv, sustainCv, releaseCv, gate, retrig
	Rig() {
		for (int i = 0; i < 6; i++) {
			buf[i] = new float[16];
			for (int c = 0; c < 16; c++)
				buf[i][c] = 0.f;
		}
		port(0).v = buf[0];
		port(1).v = buf[1];
		port(2).v = buf[2];
		port(3).v = buf[3];
		port(4).v = buf[4];
		port(5).v = buf[5];
	}
	~Rig() {
		for (int i = 0; i < 6; i++)
			delete[] buf[i];
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	rka::BeaconPort& port(int i) {
		rka::BeaconPort* p[6] = {&f.attackCv, &f.decayCv, &f.sustainCv, &f.releaseCv, &f.gate, &f.retrig};
		return *p[i];
	}
	void fill(int i, float v) {
		for (int c = 0; c < 16; c++)
			buf[i][c] = v;
	}
};

// Finite, inside +/-10 V, INV = -OUT exactly, gates and TRIG 0 or 10 V,
// nothing past the channel count.
bool ok(const rka::BeaconResult& r) {
	if (r.channels < 1 || r.channels > 16)
		return false;
	for (int c = 0; c < 16; c++) {
		if (!(r.out[c] >= -10.f && r.out[c] <= 10.f))
			return false;
		const float neg = -r.out[c];
		if (c < r.channels ? std::memcmp(&r.inv[c], &neg, sizeof(float)) != 0 : r.inv[c] != 0.f)
			return false;
		if (!(r.trig[c] == 0.f || r.trig[c] == 10.f))
			return false;
		for (int k = 0; k < 4; k++)
			if (!(r.stageGate[k][c] == 0.f || r.stageGate[k][c] == 10.f))
				return false;
		if (c >= r.channels && (r.out[c] != 0.f || r.trig[c] != 0.f))
			return false;
	}
	return true;
}

const float BAD[] = {NAN, INFINITY, -INFINITY, 1e30f, -1e30f};

// Every param and every port, one at a time and all together, with a gate
// running, in every mode.
void bad_values_everywhere() {
	for (int mode = 0; mode < 3; mode++)
		for (float bad : BAD) {
			for (int which = 0; which <= F::PARAMS_LEN + 6; which++) {
				Rig g;
				g.f.params[F::MODE] = (float) mode;
				bool good = true;
				for (int phase = 0; phase < 2; phase++) {
					for (int i = 0; i < 6; i++) {
						g.port(i).channels = 16;
						g.fill(i, 0.f);
					}
					g.fill(4, 10.f);
					for (long n = 0; n < 6000; n++) {
						// a gate that toggles so the stages run
						g.fill(4, (n / 700) % 2 ? 0.f : 10.f);
						g.fill(5, (n / 300) % 2 ? 10.f : 0.f);
						if (which < F::PARAMS_LEN) {
							g.f.params[which] = bad;
						} else if (which < F::PARAMS_LEN + 6) {
							g.fill(which - F::PARAMS_LEN, bad);
						} else {
							for (int p = 0; p < F::PARAMS_LEN; p++)
								if (p != F::MODE)
									g.f.params[p] = bad;
							for (int i = 0; i < 6; i++)
								g.fill(i, bad);
						}
						g.e.process(g.f, g.r);
						good = good && ok(g.r);
					}
					// then back to sane values: the voices must recover
					F sane;
					sane.params[F::MODE] = (float) mode;
					for (int p = 0; p < F::PARAMS_LEN; p++)
						g.f.params[p] = sane.params[p];
					for (int i = 0; i < 6; i++)
						g.fill(i, 0.f);
				}
				CHECK(good, "mode %d, %g on %s must keep every output finite and in range", mode, bad,
				      which < F::PARAMS_LEN ? "a param" : which < F::PARAMS_LEN + 6 ? "a port" : "everything");
			}
		}
}

// After the hostile values are gone, an ordinary gate gives an ordinary
// envelope again (no stuck state, no NaN left in a voice).
void recovers_after_nan() {
	Rig g;
	g.f.gate.channels = 16;
	for (int i = 0; i < 4; i++)
		g.port(i).channels = 16;
	for (int p = 0; p < F::PARAMS_LEN; p++)
		g.f.params[p] = NAN;
	for (int i = 0; i < 4; i++)
		g.fill(i, NAN);
	g.fill(4, NAN);
	for (int n = 0; n < 2000; n++)
		g.e.process(g.f, g.r);
	F sane;
	for (int p = 0; p < F::PARAMS_LEN; p++)
		g.f.params[p] = sane.params[p];
	for (int i = 0; i < 4; i++)
		g.fill(i, 0.f);
	g.fill(4, 10.f);
	float peak = 0.f;
	for (int n = 0; n < 48000; n++) {
		g.e.process(g.f, g.r);
		peak = std::fmax(peak, g.r.out[15]);
	}
	CHECK(std::isfinite(g.r.out[0]) && peak > 9.9f && std::fabs(g.r.out[0] - 5.f) < 0.05f,
	      "after NaN everywhere an ordinary gate must give an ordinary envelope (peak %g, sustain %g)", peak,
	      g.r.out[0]);
}

void mode_out_of_range() {
	for (float m : {7.f, -3.f, 2.9f, -0.4f, 1e9f, -1e9f, 0.49f, 0.5f, 1.49f, 1.5f}) {
		Rig g;
		g.f.params[F::MODE] = m;
		g.f.gate.channels = 16;
		g.f.sustainCv.channels = 16;
		g.fill(2, 3.f);
		bool good = true;
		for (long n = 0; n < 20000; n++) {
			g.fill(4, (n / 900) % 2 ? 0.f : 10.f);
			g.e.process(g.f, g.r);
			good = good && ok(g.r);
		}
		CHECK(good, "MODE %g must keep every output finite and in range", m);
	}
	// 7 is HOLD, -3 is GATE: the channel count follows the mode.
	Rig h;
	h.f.gate.channels = 3;
	h.f.sustainCv.channels = 5;
	h.f.params[F::MODE] = 7.f;
	h.e.process(h.f, h.r);
	CHECK(h.r.channels == 5, "MODE 7 reads as HOLD (Sustain CV's 5 channels), got %d", h.r.channels);
	h.f.params[F::MODE] = -3.f;
	h.e.process(h.f, h.r);
	CHECK(h.r.channels == 3, "MODE -3 reads as GATE (Gate's 3 channels), got %d", h.r.channels);
}

void extreme_rates() {
	for (float fs : {1.f, 8000.f, 44100.f, 768000.f}) {
		for (int mode = 0; mode < 3; mode++) {
			Rig g;
			g.e.setSampleRate(fs);
			g.f.params[F::MODE] = (float) mode;
			g.f.gate.channels = 16;
			g.f.sustainCv.channels = 16;
			g.f.params[F::ATTACK] = 0.f;
			g.f.params[F::DECAY] = 0.f;
			g.f.params[F::RELEASE] = 0.f;
			bool good = true;
			const long n = fs < 100.f ? 3000 : fs > 100000.f ? 400000 : 100000;
			const long per = fs < 100.f ? 5 : (long) (fs / 100.f);
			for (long i = 0; i < n; i++) {
				for (int c = 0; c < 16; c++) {
					g.buf[4][c] = ((i + c * 7) / per) % 2 ? 0.f : 10.f;
					g.buf[2][c] = (((i + c * 3) / (per * 3)) % 2 ? 1.f : -1.f) * 8.f;
				}
				g.e.process(g.f, g.r);
				good = good && ok(g.r);
			}
			CHECK(good, "at %g Hz in mode %d the outputs must stay finite and in range", fs, mode);
		}
	}
	// Bad rates are ignored.
	Rig g;
	g.f.gate.channels = 1;
	g.fill(4, 10.f);
	bool good = true;
	for (float fs : {0.f, -48000.f, NAN, INFINITY, 1e30f}) {
		g.e.setSampleRate(fs);
		for (int i = 0; i < 5000; i++) {
			g.e.process(g.f, g.r);
			good = good && ok(g.r);
		}
	}
	CHECK(good, "a zero, negative, NaN or huge sample rate must be ignored");
}

// The rate changes every few hundred samples while stages run, in every
// mode: the stage, phase and level are kept, nothing jumps outside range.
void rate_changes_mid_stage() {
	Rig g;
	g.f.gate.channels = 16;
	g.f.sustainCv.channels = 16;
	rka::hadal::Random rng;
	rng.seed(77);
	const float rates[] = {1.f, 100.f, 8000.f, 44100.f, 48000.f, 768000.f};
	bool good = true;
	for (long n = 0; n < 600000; n++) {
		if (n % 257 == 0)
			g.e.setSampleRate(rates[rng.next() % 6]);
		if (n % 5000 == 0)
			g.f.params[F::MODE] = (float) (rng.next() % 3);
		for (int c = 0; c < 16; c++) {
			g.buf[4][c] = ((n + c * 3000) / 20000) % 2 ? 0.f : 10.f;
			g.buf[2][c] = rng.uniform() * 10.f;
		}
		g.e.process(g.f, g.r);
		good = good && ok(g.r);
	}
	CHECK(good, "rate changes mid-stage in every mode must keep the outputs finite and in range");
}

// The level is continuous across a rate change: no jump bigger than a
// stage step could give.
void rate_change_keeps_level() {
	Rig g;
	g.f.gate.channels = 1;
	g.fill(4, 10.f);
	g.f.params[F::ATTACK] = 0.8f;   // a few seconds
	g.e.setSampleRate(48000.f);
	for (int i = 0; i < 20000; i++)
		g.e.process(g.f, g.r);
	const float before = g.r.out[0];
	g.e.setSampleRate(768000.f);
	g.e.process(g.f, g.r);
	CHECK(before > 0.5f && std::fabs(g.r.out[0] - before) < 0.05f,
	      "a rate change kept the level (was %g, now %g)", before, g.r.out[0]);
}

// GATE and RETRIG toggling every sample, every other sample and at random,
// on 16 channels, in every mode.
void audio_rate_gates() {
	for (int mode = 0; mode < 3; mode++)
		for (int kind = 0; kind < 4; kind++) {
			Rig g;
			g.f.params[F::MODE] = (float) mode;
			g.f.gate.channels = 16;
			g.f.retrig.channels = 16;
			g.f.sustainCv.channels = 16;
			rka::hadal::Random rng;
			rng.seed(5 + kind);
			bool good = true;
			for (long n = 0; n < 200000; n++) {
				for (int c = 0; c < 16; c++) {
					float gv, rv;
					switch (kind) {
					case 0: gv = (n + c) % 2 ? 10.f : 0.f; rv = (n + c) % 2 ? 0.f : 10.f; break;
					case 1: gv = (n / 2 + c) % 2 ? 10.f : 0.f; rv = (n / 3) % 2 ? 10.f : 0.f; break;
					case 2: gv = rng.unit() < 0.5f ? 10.f : 0.f; rv = rng.unit() < 0.5f ? 10.f : 0.f; break;
					default: gv = rng.uniform() * 12.f; rv = rng.uniform() * 12.f; break;
					}
					g.buf[4][c] = gv;
					g.buf[5][c] = rv;
					g.buf[2][c] = rng.uniform() * 10.f;
				}
				g.e.process(g.f, g.r);
				good = good && ok(g.r);
			}
			CHECK(good, "mode %d, gate pattern %d at audio rate must keep every output finite and in range", mode, kind);
		}
}

// The channel count of GATE (or of the Sustain CV in HOLD) changes every
// block, including 0 and values past 16, while voices run: the survivors
// keep their level, the vanished read 0.
void channel_counts_change() {
	Rig g;
	rka::hadal::Random rng;
	rng.seed(31);
	bool good = true;
	for (int blk = 0; blk < 4000; blk++) {
		if (blk % 700 == 0)
			g.f.params[F::MODE] = (float) (rng.next() % 3);
		const int counts[] = {-2, 0, 1, 2, 7, 15, 16, 17, 40};
		g.f.gate.channels = counts[rng.next() % 9];
		g.f.sustainCv.channels = counts[rng.next() % 9];
		g.f.attackCv.channels = counts[rng.next() % 9];
		g.f.decayCv.channels = counts[rng.next() % 9];
		g.f.releaseCv.channels = counts[rng.next() % 9];
		g.f.retrig.channels = counts[rng.next() % 9];
		for (int i = 0; i < 6; i++)
			for (int c = 0; c < 16; c++)
				g.buf[i][c] = i == 4 ? (rng.unit() < 0.8f ? 10.f : 0.f) : rng.uniform() * 10.f;
		for (int k = 0; k < 16; k++) {
			g.e.process(g.f, g.r);
			good = good && ok(g.r);
		}
	}
	CHECK(good, "a channel count changing every block must keep every output finite and in range");

	// A polyphonic cable gives each channel its own volts and 0 V past its
	// own count (here the Sustain CV: 2 channels at 10 V, the buffer's
	// other floats also 10 V, on a 4-channel gate; the knob is 0).
	Rig k;
	k.f.params[F::SUSTAIN] = 0.f;
	k.f.gate.channels = 4;
	k.fill(4, 10.f);
	k.f.sustainCv.channels = 2;
	k.fill(2, 10.f);
	for (int i = 0; i < 48000; i++)
		k.e.process(k.f, k.r);
	CHECK(k.r.out[0] > 9.9f && k.r.out[1] > 9.9f && std::fabs(k.r.out[2]) < 0.01f && std::fabs(k.r.out[3]) < 0.01f,
	      "a 2-channel CV reads 0 V on channels 3 and 4 (sustain %g %g %g %g)", k.r.out[0], k.r.out[1],
	      k.r.out[2], k.r.out[3]);

	// Survivors keep their state, the vanished stop, new ones start idle.
	Rig h;
	h.f.gate.channels = 8;
	h.fill(4, 10.f);
	for (int i = 0; i < 4800; i++)
		h.e.process(h.f, h.r);
	const float held = h.r.out[2];
	h.f.gate.channels = 3;
	h.e.process(h.f, h.r);
	CHECK(h.r.channels == 3 && std::fabs(h.r.out[2] - held) < 0.05f && h.r.out[5] == 0.f,
	      "a channel that survives keeps its level (%g -> %g), one that vanished reads 0 (%g)", held, h.r.out[2],
	      h.r.out[5]);
	h.f.gate.channels = 8;
	h.e.process(h.f, h.r);
	CHECK(std::fabs(h.r.out[2] - held) < 0.1f && h.r.out[5] < 0.1f,
	      "channels that return start at 0 V, idle (ch 6 reads %g), the survivor carries on (%g)", h.r.out[5],
	      h.r.out[2]);
}

void no_allocation_in_process() {
	Rig g;
	g.f.gate.channels = 16;
	g.f.retrig.channels = 16;
	for (int i = 0; i < 4; i++)
		g.port(i).channels = 16;
	const long before = tAllocs;
	rka::hadal::Random rng;
	rng.seed(8);
	for (long n = 0; n < 400000; n++) {
		if (n % 1000 == 0) {
			g.f.params[F::MODE] = (float) (n / 1000 % 3);
			g.f.gate.channels = 1 + (int) (rng.next() % 16);
			g.e.setSampleRate(rng.unit() < 0.5f ? 48000.f : 96000.f);
		}
		for (int c = 0; c < 16; c++) {
			g.buf[4][c] = ((n + c * 100) / 600) % 2 ? 0.f : 10.f;
			g.buf[5][c] = (n % 1700 < 20) ? 10.f : 0.f;
			g.buf[0][c] = g.buf[1][c] = g.buf[3][c] = rng.uniform() * 5.f;
			g.buf[2][c] = rng.uniform() * 10.f;
		}
		g.f.params[F::SHAPE] = (n / 20000 % 2) ? 0.1f : 0.9f;
		g.e.process(g.f, g.r);
	}
	g.e.reset();
	CHECK(tAllocs == before, "%ld allocations in process(), setSampleRate() or reset()", tAllocs - before);
}

void seeded_fuzz() {
	for (uint32_t seed : {2026u, 1u, 99u}) {
		rka::hadal::Random rng;
		rng.seed(seed);
		Rig g;
		bool good = true;
		for (long n = 0; n < 1000000; n++) {
			if (n % 512 == 0) {
				for (int p = 0; p < F::PARAMS_LEN; p++)
					g.f.params[p] = rng.unit() < 0.05f ? BAD[rng.next() % 5] : rng.unit();
				g.f.params[F::MODE] = rng.unit() < 0.9f ? (float) (rng.next() % 3) : rng.uniform() * 12.f;
				g.f.params[F::PUSH] = rng.unit() < 0.2f ? 1.f : 0.f;
				for (int i = 0; i < 6; i++)
					g.port(i).channels = (int) (rng.next() % 19) - 1;
				if (n % 65536 == 0) {
					const float rates[6] = {1.f, 8000.f, 44100.f, 48000.f, 192000.f, 768000.f};
					g.e.setSampleRate(rates[rng.next() % 6]);
				}
				if (n % 100000 == 0)
					g.e.reset();
			}
			for (int i = 0; i < 6; i++)
				for (int c = 0; c < 16; c++) {
					const float u = rng.unit();
					g.buf[i][c] = u < 0.01f ? BAD[rng.next() % 5] : i == 4 || i == 5 ? (u < 0.5f ? 10.f : u < 0.6f ? 0.5f : 0.f)
					                                                              : rng.uniform() * 12.f;
				}
			g.e.process(g.f, g.r);
			good = good && ok(g.r);
		}
		CHECK(good, "seed %u: fuzzed knobs, CVs, gates, modes, channel counts and rates must keep every output "
		            "finite and in range", seed);
	}
}

} // namespace

int main() {
	bad_values_everywhere();
	recovers_after_nan();
	mode_out_of_range();
	extreme_rates();
	rate_changes_mid_stage();
	rate_change_keeps_level();
	audio_rate_gates();
	channel_counts_change();
	no_allocation_in_process();
	seeded_fuzz();
	return rkatest::finish("beacon test_hostile");
}

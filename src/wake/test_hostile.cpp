// Wake under hostile conditions, built with the address and
// undefined-behaviour sanitizers and a counting operator new: extreme and
// changing sample rates (also frozen and at Feedback 110 %), NaN and inf on
// every input, gates and clocks at audio rate, Time under an audio-rate CV,
// minutes at 110 %, seeded fuzz of everything, no allocation in process(),
// and a loop that decays to exact silence.
#include "testing.hpp"
#include "hadal/Random.hpp"
#include "wake/WakeEngine.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
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

typedef rka::WakeFrame F;

namespace {

struct Rig {
	rka::WakeEngine e;
	F f;
	rka::WakeResult r;
	explicit Rig(uint32_t seed = 1) : e(seed) {
		f.inLPatched = f.inRPatched = true;
		f.outRPatched = true;
	}
	// Finite and inside the output limit.
	bool step() {
		e.process(f, r);
		return std::isfinite(r.l) && std::isfinite(r.r) && std::fabs(r.l) <= 10.f && std::fabs(r.r) <= 10.f;
	}
};

void loud(Rig& g, rka::hadal::Random& rng) {
	g.f.inL = 10.f * rng.uniform();
	g.f.inR = 10.f * rng.uniform();
}

// Review Focus 2: any rate, and a rate change in any state.
void extreme_rates() {
	for (float fs : {1.f, 10.f, 1000.f, 44100.f, 192000.f, 768000.f}) {
		Rig g(3);
		rka::hadal::Random rng;
		rng.seed(1);
		g.e.setSampleRate(fs);
		g.f.params[F::FEEDBACK] = 1.f;
		g.f.params[F::DIFFUSE] = 1.f;
		g.f.params[F::DRIFT] = 1.f;
		g.f.params[F::TIME] = 1.f;
		g.f.params[F::TIME_ATT] = 1.f;
		g.f.timeCv = 3.f;   // and a CV asking for 8 times as long
		bool ok = true;
		float longest = 0.f;
		for (int i = 0; i < 60000; i++) {
			loud(g, rng);
			if (i == 20000)
				g.f.freeze = true;
			if (i == 40000)
				g.f.freeze = false;
			g.f.params[F::TIME] = (i / 3000) % 2 ? 0.f : 1.f;
			ok = ok && g.step();
			longest = std::fmax(longest, std::fmax(g.e.seconds(0), g.e.seconds(1)));
		}
		CHECK(ok, "at %g Hz the output left its bounds", fs);
		const float tMax = g.e.maxSeconds();
		CHECK(tMax > 0.f && tMax <= 10.f && longest <= tMax * 1.0001f, "at %g Hz the longest time is %g s and Time reached %g s", fs,
		      tMax, longest);
		if (fs == 768000.f)
			CHECK(std::fabs(tMax - 2097152.f / 768000.f) < 1e-3f, "at 768 kHz the longest time is %g s (want 2.73)", tMax);
		if (fs <= 192000.f && fs >= 1000.f)
			CHECK(tMax == 10.f, "at %g Hz the longest time is %g s (want 10)", fs, tMax);
	}
	// Rates the engine refuses: the last good one stays.
	Rig g;
	for (float bad : {0.f, -48000.f, std::nanf(""), INFINITY, 1e9f})
		g.e.setSampleRate(bad);
	CHECK(g.e.maxSeconds() == 10.f, "an unusable rate must be ignored (%g)", g.e.maxSeconds());
}

void rate_change_recovers() {
	Rig g(4);
	rka::hadal::Random rng;
	rng.seed(2);
	g.f.params[F::FEEDBACK] = 1.f;
	bool ok = true;
	const float rates[6] = {48000.f, 96000.f, 44100.f, 768000.f, 8000.f, 48000.f};
	for (int k = 0; k < 6; k++) {
		g.f.freeze = k % 2 == 1;   // change rate while frozen too
		for (int i = 0; i < 30000; i++) {
			loud(g, rng);
			ok = ok && g.step();
		}
		g.e.setSampleRate(rates[k]);
		// A rate change clears the delay: with nothing in, nothing out.
		g.f.inL = g.f.inR = 0.f;
		g.f.freeze = false;
		float after = 0.f;
		for (int i = 0; i < 2000; i++) {
			ok = ok && g.step();
			after = std::fmax(after, std::fmax(std::fabs(g.r.l), std::fabs(g.r.r)));
		}
		CHECK(after == 0.f, "after a change to %g Hz the delay must be empty (%g V)", rates[k], after);
	}
	CHECK(ok, "rate changes left the output out of bounds");
	// Initialize empties the delay and unfreezes.
	g.f.params[F::MIX] = 1.f;
	for (int i = 0; i < 30000; i++) {
		loud(g, rng);
		g.f.freezeGate = i > 20000 ? 10.f : 0.f;
		g.step();
	}
	g.e.reset();
	g.f.inL = g.f.inR = g.f.freezeGate = 0.f;
	float after = 0.f;
	for (int i = 0; i < 60000; i++) {
		g.step();
		after = std::fmax(after, std::fmax(std::fabs(g.r.l), std::fabs(g.r.r)));
	}
	CHECK(after == 0.f && !g.r.frozen, "after reset the delay must be empty and unfrozen (%g V, frozen %d)", after, (int) g.r.frozen);
}

void hostile_inputs() {
	const float nan = std::nanf("");
	const float junk[7] = {nan, INFINITY, -INFINITY, 1e30f, -1e30f, 3.4e38f, -0.f};
	Rig g(5);
	g.f.clockPatched = true;
	bool ok = true;
	for (int i = 0; i < 400000; i++) {
		const float j = junk[(i / 97) % 7];
		const int which = (i / 5000) % 16;
		g.f.inL = which == 0 ? j : 1.f;
		g.f.inR = which == 1 ? j : -1.f;
		g.f.timeCv = which == 2 ? j : 0.f;
		g.f.feedbackCv = which == 3 ? j : 0.f;
		g.f.mixCv = which == 4 ? j : 0.f;
		g.f.clock = which == 5 ? j : (i % 4000 < 100 ? 10.f : 0.f);
		g.f.freezeGate = which == 6 ? j : 0.f;
		for (int p = 0; p < F::PARAMS_LEN; p++)
			g.f.params[p] = which == 7 + p ? j : F().params[p];
		g.f.style = which == 15 ? (i % 3 == 0 ? -7 : 99) : 1;
		ok = ok && g.step();
	}
	CHECK(ok, "NaN, inf and huge values on the inputs and params left the output out of bounds");
	// And afterwards it still works: an echo comes back.
	Rig h(5);
	for (int i = 0; i < 2000; i++) {
		h.f.inL = h.f.inR = nan;
		h.step();
	}
	h.f.params[F::MIX] = 1.f;
	float peak = 0.f;
	for (int i = 0; i < 48000; i++) {
		h.f.inL = h.f.inR = i == 100 ? 5.f : 0.f;
		h.step();
		peak = std::fmax(peak, std::fabs(h.r.l));
	}
	CHECK(peak > 1.f, "after NaN input an echo still returns (%g V)", peak);
}

// Review Focus 1 and 3: the gate and the clock at audio rate.
void gates_at_audio_rate() {
	Rig g(6);
	rka::hadal::Random rng;
	rng.seed(3);
	g.f.clockPatched = true;
	g.f.params[F::FEEDBACK] = 0.9f;
	g.f.params[F::TIME] = 0.f;   // the shortest loop
	bool ok = true;
	for (int i = 0; i < 480000; i++) {
		loud(g, rng);
		// Gate from every other sample down to every 2000th; the clock
		// irregular, from 2 samples to seconds apart.
		const int gp = 2 + (i / 40000) * 150;
		g.f.freezeGate = (i % gp) < gp / 2 ? 10.f : 0.f;
		const int cp = 2 + (int) (rng.unit() * (i < 240000 ? 40.f : 200000.f));
		g.f.clock = (i % cp) == 0 ? 10.f : 0.f;
		if (i % 60000 == 0)
			g.f.params[F::TIME] = rng.unit();
		ok = ok && g.step();
		ok = ok && g.e.seconds(0) >= 0.0099f && g.e.seconds(0) <= 10.001f && g.e.seconds(1) >= 0.0099f;
	}
	CHECK(ok, "an audio-rate gate and clock left the output or the time out of bounds (%g s)", g.e.seconds(0));
}

// Review Focus 4: Time's CV as noise, in Tape.
void time_cv_as_noise() {
	for (int style = 0; style < 3; style++) {
		Rig g(7);
		rka::hadal::Random rng;
		rng.seed(4);
		g.f.style = style;
		g.f.params[F::TIME_ATT] = 1.f;
		g.f.params[F::FEEDBACK] = 0.9f;
		bool ok = true;
		for (int i = 0; i < 480000; i++) {
			loud(g, rng);
			g.f.timeCv = 10.f * rng.uniform();
			if (i % 50000 == 0)
				g.f.params[F::TIME_ATT] = -g.f.params[F::TIME_ATT];
			ok = ok && g.step();
			const float d = g.e.tap(0).delay();
			ok = ok && d >= 1.f && d <= 480000.f;
		}
		CHECK(ok, "style %d: Time under +-10 V of noise left the output or the read point out of bounds", style);
	}
}

// Review Focus 5: minutes at 110 %.
void minutes_at_110() {
	for (int style = 0; style < 3; style++) {
		Rig g(8);
		rka::hadal::Random rng;
		rng.seed(5);
		g.f.style = style;
		g.f.params[F::FEEDBACK] = 1.f;
		g.f.params[F::TIME] = 0.2f;
		g.f.params[F::MIX] = 1.f;
		bool ok = true;
		double sum = 0;
		long count = 0;
		for (int phase = 0; phase < 3; phase++)
			for (int i = 0; i < 48000 * 200; i++) {
				if (phase == 0)
					loud(g, rng);
				else
					g.f.inL = g.f.inR = phase == 1 ? 10.f : 0.f;
				ok = ok && g.step();
				if (phase == 1 && i > 48000 * 100) {
					sum += g.r.l;
					count++;
				}
			}
		CHECK(ok, "style %d: 10 minutes at 110 %% left the output out of bounds", style);
		CHECK(std::fabs(sum / count) < 0.5, "style %d: 10 V DC in at 110 %% leaves a mean of %.3f V at the output", style, sum / count);
		// Feedback down: it dies away to exact silence.
		g.f.params[F::FEEDBACK] = 0.3f;
		float last = 1.f;
		for (int i = 0; i < 48000 * 30; i++) {
			g.step();
			last = std::fmax(std::fabs(g.r.l), std::fabs(g.r.r));
		}
		CHECK(last == 0.f, "style %d: with Feedback down the loop must reach silence (%g V)", style, last);
	}
}

void no_allocation_in_process() {
	Rig g(9);
	rka::hadal::Random rng;
	rng.seed(6);
	g.f.clockPatched = true;
	for (int i = 0; i < 100; i++)
		g.step();
	const long before = tAllocs;
	for (int i = 0; i < 300000; i++) {
		loud(g, rng);
		if (i % 20000 == 0) {
			g.f.style = (i / 20000) % 3;
			g.f.freeze = (i / 20000) % 4 == 3;
			g.f.pingPong = (i / 20000) % 2 == 1;
			g.f.params[F::TIME] = rng.unit();
			g.f.params[F::DIFFUSE] = rng.unit();
			g.f.params[F::RATIO] = (float) (i / 20000 % 7);
		}
		g.f.clock = i % 9000 < 100 ? 10.f : 0.f;
		g.step();
	}
	CHECK(tAllocs == before, "process() allocated %ld times", tAllocs - before);
	g.e.reset();
	CHECK(tAllocs == before, "reset() allocated %ld times", tAllocs - before);
}

void seeded_fuzz() {
	for (uint32_t seed = 1; seed <= 6; seed++) {
		Rig g(seed);
		rka::hadal::Random rng;
		rng.seed(seed * 77u);
		bool ok = true;
		for (int i = 0; i < 300000; i++) {
			if (i % 16 == 0) {
				for (int p = 0; p < F::PARAMS_LEN; p++)
					if (rng.unit() < 0.02f)
						g.f.params[p] = p == F::RATIO ? 8.f * rng.unit() - 1.f : p == F::TIME_ATT ? 1.2f * rng.uniform() : 1.2f * rng.unit() - 0.1f;
				g.f.timeCv = 12.f * rng.uniform();
				g.f.feedbackCv = 12.f * rng.uniform();
				g.f.mixCv = 12.f * rng.uniform();
				if (rng.unit() < 0.002f)
					g.f.style = (int) (rng.unit() * 3.f);
				if (rng.unit() < 0.002f)
					g.f.freeze = !g.f.freeze;
				if (rng.unit() < 0.002f)
					g.f.pingPong = !g.f.pingPong;
				if (rng.unit() < 0.001f)
					g.f.clockPatched = !g.f.clockPatched;
				if (rng.unit() < 0.001f)
					g.f.outRPatched = !g.f.outRPatched;
				if (rng.unit() < 0.001f)
					g.f.inLPatched = !g.f.inLPatched;
				if (rng.unit() < 0.0005f)
					g.e.reset();
			}
			g.f.clock = rng.unit() < 0.001f ? 10.f : 0.f;
			g.f.freezeGate = rng.unit() < 0.0005f ? 10.f : g.f.freezeGate * (rng.unit() < 0.0005f ? 0.f : 1.f);
			loud(g, rng);
			ok = ok && g.step();
		}
		CHECK(ok, "fuzz seed %u left the output out of bounds", seed);
	}
}

// An echo's tail: no subnormals at the output, exact silence, no slowdown.
void denormals() {
	for (int style = 0; style < 3; style++) {
		Rig g(10);
		g.f.style = style;
		g.f.params[F::FEEDBACK] = 0.6f;
		g.f.params[F::TIME] = 0.25f;   // 56 ms
		g.f.params[F::DIFFUSE] = 0.5f;
		g.f.params[F::MIX] = 1.f;
		g.f.params[F::DRIFT] = 0.f;
		bool normal = true;
		float worst = 0.f;
		double firstSecond = 0.0, lastSecond = 0.0;
		for (int s = 0; s < 30; s++) {
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < 48000; i++) {
				g.f.inL = g.f.inR = s == 0 && i < 480 ? 5.f : 0.f;
				g.step();
				for (float v : {g.r.l, g.r.r}) {
					const float a = std::fabs(v);
					if (a != 0.f && a < 1.17549435e-38f) {
						normal = false;
						worst = v;
					}
				}
			}
			const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			if (s == 0)
				firstSecond = dt;
			if (s == 29)
				lastSecond = dt;
		}
		CHECK(normal, "style %d: an echo's tail passes through subnormals (%g)", style, worst);
		CHECK(g.r.l == 0.f && g.r.r == 0.f, "style %d: 30 s after a short sound the output is %g, %g", style, g.r.l, g.r.r);
		CHECK(lastSecond < 4.0 * firstSecond + 0.05, "style %d: the silent tail runs slow: %.3g s for its last second, %.3g s for the first",
		      style, lastSecond, firstSecond);
	}
}

} // namespace

int main() {
	extreme_rates();
	rate_change_recovers();
	hostile_inputs();
	gates_at_audio_rate();
	time_cv_as_noise();
	minutes_at_110();
	no_allocation_in_process();
	seeded_fuzz();
	denormals();
	return rkatest::finish("wake test_hostile");
}

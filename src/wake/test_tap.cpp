// rka::DelayTap (src/DelayTap.hpp): a moving read point over a DelayLine.
// Steady reads, the glide (pitch bends, then lands), the crossfade (no
// bend), the frozen loop and its join, and hostile targets.
#include "DelayLine.hpp"
#include "DelayTap.hpp"
#include "hadal/Random.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

const float FS = 48000.f;
const double TWO_PI = 6.283185307179586;

struct Rig {
	rka::DelayLine line;
	rka::DelayTap tap;
	long n = 0;
	explicit Rig(int maxDelay = 30000) {
		line.allocate(maxDelay);
		tap.setSampleRate(FS);
		tap.setRange(1.f, (float) (maxDelay - 4));
		tap.setGlide(0.15f, 2.f);
		tap.setCrossfade(0.04f);
	}
	// Write a sample of `hz` (or what `in` gives) and read.
	float sine(double hz, float mod = 0.f) {
		line.write((float) std::sin(TWO_PI * hz * n / FS));
		n++;
		return tap.process(line, mod);
	}
};

// Rising zero crossings per second over a block.
double crossingsHz(const std::vector<float>& x) {
	int c = 0;
	for (size_t i = 1; i < x.size(); i++)
		c += x[i - 1] < 0.f && x[i] >= 0.f;
	return c * (double) FS / x.size();
}

void steady() {
	Rig g;
	g.tap.snap(1000.25f);
	double worst = 0;
	for (int i = 0; i < 5000; i++) {
		g.line.write(0.001f * i);
		const float y = g.tap.process(g.line, 0.f);
		if (i > 2000)
			worst = std::max(worst, std::fabs((double) y - 0.001 * (i - 1000.25)));
	}
	CHECK(worst < 1e-3, "a ramp read 1000.25 samples back is off by %g", worst);
	CHECK(!g.tap.moving() && g.tap.delay() == 1000.25f, "a snapped tap is at rest at its delay (%g)", g.tap.delay());
}

void glide_bends_and_lands() {
	Rig g;
	g.tap.snap(4800.f);
	for (int i = 0; i < 20000; i++)
		g.sine(440.0);
	g.tap.setTarget(9600.f, false);
	CHECK(g.tap.moving(), "a new target starts a glide");
	double lowest = 1e9, highest = 0;
	int blocks = 0;
	while (g.tap.moving() && blocks < 2000) {
		std::vector<float> x(2400);
		for (float& v : x)
			v = g.sine(440.0);
		const double hz = crossingsHz(x);
		lowest = std::min(lowest, hz);
		highest = std::max(highest, hz);
		blocks++;
	}
	std::printf("  glide 4800 -> 9600: %d blocks of 50 ms, pitch %.0f .. %.0f Hz\n", blocks, lowest, highest);
	CHECK(lowest < 400.0, "a lengthening delay must bend the pitch down (lowest %.0f Hz)", lowest);
	CHECK(lowest >= 105.0, "the bend must stay within two octaves (lowest %.0f Hz)", lowest);
	CHECK(highest <= 460.0, "a lengthening delay must not bend up (highest %.0f Hz)", highest);
	CHECK(!g.tap.moving() && g.tap.delay() == 9600.f, "the glide lands exactly (%.6f, moving %d)", g.tap.delay(),
	      (int) g.tap.moving());
	// Shortening bends up, by two octaves at most.
	g.tap.setTarget(480.f, false);
	highest = 0;
	blocks = 0;
	while (g.tap.moving() && blocks < 2000) {
		std::vector<float> x(2400);
		for (float& v : x)
			v = g.sine(440.0);
		highest = std::max(highest, crossingsHz(x));
		blocks++;
	}
	CHECK(highest > 500.0 && highest <= 1800.0, "a shortening delay bends up, within two octaves (%.0f Hz)", highest);
	CHECK(g.tap.delay() == 480.f, "and lands (%.6f)", g.tap.delay());
}

void crossfade_no_bend() {
	Rig g;
	g.tap.snap(4800.f);
	for (int i = 0; i < 20000; i++)
		g.sine(440.0);
	g.tap.setTarget(9600.f, true);
	CHECK(g.tap.moving(), "a new target starts a fade");
	std::vector<float> x;
	float prev = g.sine(440.0), jump = 0.f;
	x.push_back(prev);
	for (int i = 0; i < 9600; i++) {
		const float y = g.sine(440.0);
		jump = std::max(jump, std::fabs(y - prev));
		prev = y;
		x.push_back(y);
	}
	const double hz = crossingsHz(x);
	const float slope = (float) (TWO_PI * 440.0 / FS);
	CHECK(std::fabs(hz / 440.0 - 1.0) < 0.01, "a crossfaded change must not bend pitch (%.1f Hz)", hz);
	CHECK(jump <= 1.5f * slope, "a crossfaded change steps %.4f a sample (the sine's own slope is %.4f)", jump, slope);
	CHECK(!g.tap.moving() && g.tap.delay() == 9600.f, "the fade ends at the new delay (%.3f)", g.tap.delay());
	// The fade takes its set time.
	g.tap.setTarget(2400.f, true);
	int n = 0;
	while (g.tap.moving() && n < 100000) {
		g.sine(440.0);
		n++;
	}
	CHECK(std::abs(n - 1920) <= 2, "a 40 ms fade takes %d samples (want 1920)", n);
}

// A crossfade never adds level, and a change of a few samples does not
// fade at all (the two reads are nearly the same signal): it glides.
void crossfade_adds_no_level() {
	Rig g;
	g.tap.snap(4800.f);
	for (int i = 0; i < 20000; i++)
		g.sine(440.0);
	// Back and forth between delays at many phase differences.
	float peak = 0.f;
	for (int k = 0; k < 40; k++) {
		g.tap.setTarget(k % 2 ? 4800.f : 4800.f + 109.f * (1 + k % 5), true);
		for (int i = 0; i < 2400; i++)
			peak = std::max(peak, std::fabs(g.sine(440.0)));
	}
	CHECK(peak <= 1.001f, "crossfading between two reads of a unit sine peaks at %.4f (want no more than 1)", peak);
	Rig h;
	h.tap.snap(4800.f);
	for (int i = 0; i < 6000; i++)
		h.sine(440.0);
	h.tap.setTarget(4801.f, true);
	CHECK(!h.tap.fading() && h.tap.moving(), "a one-sample change glides, with no fade");
	int n = 0;
	while (h.tap.moving() && n++ < 200000)
		h.sine(440.0);
	CHECK(h.tap.delay() == 4801.f && !h.tap.fading(), "and lands (%.4f)", h.tap.delay());
	h.tap.setTarget(4811.f, true);
	CHECK(h.tap.fading(), "a ten-sample change fades");
}

void latest_target_wins() {
	Rig g;
	g.tap.snap(4800.f);
	for (int i = 0; i < 6000; i++)
		g.sine(440.0);
	g.tap.setTarget(6000.f, true);
	for (int i = 0; i < 300; i++)
		g.sine(440.0);
	g.tap.setTarget(7000.f, true);
	for (int i = 0; i < 300; i++)
		g.sine(440.0);
	g.tap.setTarget(8000.f, true);
	float prev = g.sine(440.0), jump = 0.f;
	int n = 0;
	while (g.tap.moving() && n < 100000) {
		const float y = g.sine(440.0);
		jump = std::max(jump, std::fabs(y - prev));
		prev = y;
		n++;
	}
	CHECK(g.tap.delay() == 8000.f, "three targets inside a fade end at the last (%.1f)", g.tap.delay());
	CHECK(jump <= 1.5f * (float) (TWO_PI * 440.0 / FS), "queued fades step %.4f a sample", jump);
}

// Noise in the line, then frozen: the loop repeats exactly and is as long
// as the delay was.
void freeze_repeats() {
	Rig g;
	rka::hadal::Random rng;
	rng.seed(7);
	g.tap.snap(2000.f);
	for (int i = 0; i < 12000; i++) {
		g.line.write(rng.uniform());
		g.tap.process(g.line, 0.f);
	}
	g.tap.freeze(0.01f);
	CHECK(g.tap.frozen(), "freeze() freezes");
	const int L = 2000;
	std::vector<float> first(L);
	for (int i = 0; i < L; i++)
		first[i] = g.tap.process(g.line, 0.f);
	double worst = 0;
	for (int k = 0; k < 50; k++)
		for (int i = 0; i < L; i++)
			worst = std::max(worst, (double) std::fabs(g.tap.process(g.line, 0.f) - first[i]));
	CHECK(worst < 1e-6, "a frozen loop of %d samples must repeat exactly (off by %g)", L, worst);
	// It is the audio that was in the line: most of the loop equals a
	// plain read of it.
	int same = 0;
	for (int i = 0; i < L; i++)
		for (int d = 1; d < 2010 && d < L + 10; d++)
			if (std::fabs(first[i] - g.line.readInt(d)) < 1e-6f) {
				same++;
				break;
			}
	CHECK(same > L * 0.6, "the frozen loop must be the line's own audio (%d of %d samples found)", same, L);
}

// A loop longer than the delay (the engine's read is a little short of the
// echo time): it is as long as asked, repeats exactly, and still carries
// straight on from the last read.
void freeze_longer_loop() {
	Rig g;
	g.tap.snap(1500.f);
	float last = 0.f;
	for (int i = 0; i < 12000; i++)
		last = g.sine(37.0);
	(void) last;
	g.tap.freeze(0.01f, 2000.f);
	const float first0 = g.tap.process(g.line, 0.f);
	const double next = std::sin(TWO_PI * 37.0 * (double) (g.n - 1500) / FS);
	CHECK(std::fabs(first0 - next) < 1e-4, "a longer loop carries on from the read: %.6f, want %.6f", first0, next);
	const int L = 2000;
	std::vector<float> first(L);
	first[0] = first0;
	for (int i = 1; i < L; i++)
		first[i] = g.tap.process(g.line, 0.f);
	double worst = 0;
	for (int k = 0; k < 20; k++)
		for (int i = 0; i < L; i++)
			worst = std::max(worst, (double) std::fabs(g.tap.process(g.line, 0.f) - first[i]));
	CHECK(worst < 1e-6, "a loop of 2000 from a read at 1500 repeats every 2000 samples (off by %g)", worst);
	// And not every 1500.
	double diff = 0;
	for (int i = 0; i + 1500 < L; i++)
		diff = std::max(diff, (double) std::fabs(first[i] - first[i + 1500]));
	CHECK(diff > 0.1, "the loop is the asked length, not the delay's");
}

// The join: no step beyond the signal's ordinary ones, for long and very
// short loops, and no read outside the line.
void freeze_join() {
	const int loops[4] = {4800, 480, 96, 16};
	for (int L : loops) {
		Rig g(8192);
		g.tap.snap((float) L);
		// A slow sine whose period does not divide the loop: a bad join
		// is a visible step.
		const double hz = 37.0;
		float last = 0.f;
		for (int i = 0; i < 20000; i++)
			last = g.sine(hz);
		g.tap.freeze(0.01f);
		const float slope = (float) (TWO_PI * hz / FS);
		float prev = g.tap.process(g.line, 0.f), jump = 0.f;
		// Freezing itself is seamless: the loop carries straight on.
		// (the sample after the last one read, not a repeat of it).
		const double next = std::sin(TWO_PI * hz * (double) (g.n - L) / FS);
		CHECK(std::fabs(prev - next) < 1e-4 && std::fabs(prev - last) <= 1.5f * slope,
		      "freezing a loop of %d: first sample %.6f, want %.6f (last live %.6f)", L, prev, next, last);
		bool finite = true;
		for (int i = 0; i < 20 * L + 200; i++) {
			const float y = g.tap.process(g.line, 0.f);
			finite = finite && y == y && std::fabs(y) <= 1.001f;
			jump = std::max(jump, std::fabs(y - prev));
			prev = y;
		}
		const int join = std::min(480, L / 4);
		// Across the join the output moves from the loop's end to its
		// start: at most the whole gap spread over the join, plus the slope.
		const float allowed = 2.f / (float) join + 2.f * slope;
		std::printf("  frozen loop of %d: largest step %.4f (allowed %.4f)\n", L, jump, allowed);
		CHECK(finite, "a frozen loop of %d must stay finite and inside the signal", L);
		CHECK(jump <= allowed, "a frozen loop of %d steps %.4f at its join (allowed %.4f)", L, jump, allowed);
	}
	// Releasing fades back to the live read, no step.
	Rig g;
	g.tap.snap(4800.f);
	// 37 Hz does not fit the loop, so the loop and the live read differ.
	for (int i = 0; i < 20000; i++)
		g.sine(37.0);
	g.tap.freeze(0.01f);
	float prev = 0.f;
	for (int i = 0; i < 7000; i++)
		prev = g.tap.process(g.line, 0.f);
	g.tap.release();
	CHECK(!g.tap.frozen(), "release() releases");
	float jump = 0.f;
	for (int i = 0; i < 4000; i++) {
		const float y = g.sine(37.0);
		jump = std::max(jump, std::fabs(y - prev));
		prev = y;
	}
	CHECK(jump <= 3.f * (float) (TWO_PI * 37.0 / FS), "leaving freeze steps %.4f a sample", jump);
	CHECK(!g.tap.moving() && g.tap.delay() == 4800.f, "after release the tap is back at its delay (%.1f)", g.tap.delay());
}

// A moving `mod` changes the frozen loop's speed: a slope of -1 % a sample
// plays it 1 % fast.
void freeze_mod() {
	for (double rate : {-0.01, 0.01}) {
		Rig g;
		g.tap.snap(4800.f);
		for (int i = 0; i < 20000; i++)
			g.sine(480.0);   // 48 cycles in the loop exactly
		g.tap.freeze(0.01f);
		std::vector<float> x(48000);
		for (size_t i = 0; i < x.size(); i++)
			x[i] = g.tap.process(g.line, (float) (rate * (double) i));
		const double hz = crossingsHz(x);
		CHECK(std::fabs(hz / (480.0 * (1.0 - rate)) - 1.0) < 0.004, "mod slope %+.2f plays the loop at %.1f Hz (want %.1f)",
		      rate, hz, 480.0 * (1.0 - rate));
	}
}

void bounds() {
	Rig g(8192);
	rka::hadal::Random rng;
	rng.seed(3);
	g.tap.snap(100.f);
	const float nan = std::nanf("");
	const float targets[6] = {0.f, nan, 1e9f, -5.f, 1e30f, 4000.f};
	bool ok = true;
	for (int i = 0; i < 400000; i++) {
		g.line.write(rng.uniform());
		if (i % 7 == 0)
			g.tap.setTarget(targets[(i / 7) % 6] + (i % 3 == 0 ? rng.uniform() * 3000.f : 0.f), (i / 5000) % 2 == 0);
		if (i == 150000)
			g.tap.freeze(0.01f);
		if (i == 200000)
			g.tap.release();
		const float y = g.tap.process(g.line, i % 11 == 0 ? nan : rng.uniform() * 50.f);
		ok = ok && y == y && std::fabs(y) <= 2.f;
		ok = ok && g.tap.delay() >= 1.f && g.tap.delay() <= 8188.f;
	}
	CHECK(ok, "hostile targets and mods must leave the tap finite and inside its range (delay %g)", g.tap.delay());
	// A target that jumps every sample still obeys the glide's two octaves.
	Rig h;
	h.tap.snap(4800.f);
	for (int i = 0; i < 20000; i++)
		h.sine(440.0);
	double worstUp = 0, worstDown = 0;
	float prev = h.tap.delay();
	for (int i = 0; i < 48000; i++) {
		h.tap.setTarget(i % 2 ? 20000.f : 10.f, false);
		h.sine(440.0);
		const float d = h.tap.delay();
		worstUp = std::max(worstUp, (double) (d - prev));
		worstDown = std::max(worstDown, (double) (prev - d));
		prev = d;
	}
	CHECK(worstUp <= 0.7501 && worstDown <= 3.0001, "the delay moves at most +0.75 / -3 a sample (%g / %g)", worstUp,
	      worstDown);
}

} // namespace

// A big change bends for at most the glide's limit, then crossfades the
// rest of the way: 300 ms to 10 s at two octaves down would take 13 s of
// bass. A turn that bends for less than the limit is untouched: no fade.
void bend_is_capped() {
	for (int dir = 0; dir < 2; dir++) {
		Rig g(490000);
		g.tap.setGlideLimit(1.f);
		const float from = dir ? 480000.f : 14400.f, to = dir ? 14400.f : 480000.f;
		g.tap.snap(from);
		for (int i = 0; i < 2000; i++)
			g.sine(440.0);
		g.tap.setTarget(to, false);
		long n = 0;
		bool faded = false;
		while (g.tap.moving() && n < 30L * 48000) {
			g.sine(440.0);
			faded = faded || g.tap.fading();
			n++;
		}
		std::printf("  bend %.0f -> %.0f samples with a 1 s limit: lands after %.2f s, crossfaded %d\n", from, to, n / FS,
		            (int) faded);
		CHECK(faded && g.tap.delay() == to, "a big change crossfades to its target (%.0f, faded %d)", g.tap.delay(), (int) faded);
		CHECK(n < (long) (1.1f * FS), "a big change lands within the limit and a fade (%.2f s)", n / FS);
	}
	// 4800 -> 9600 bends for about a quarter of a second: no fade.
	Rig g;
	g.tap.setGlideLimit(1.f);
	g.tap.snap(4800.f);
	for (int i = 0; i < 2000; i++)
		g.sine(440.0);
	g.tap.setTarget(9600.f, false);
	bool faded = false;
	for (int i = 0; i < 5 * 48000; i++) {
		g.sine(440.0);
		faded = faded || g.tap.fading();
	}
	CHECK(!faded && g.tap.delay() == 9600.f, "a medium change glides as before (%.3f, faded %d)", g.tap.delay(), (int) faded);
}

int main() {
	bend_is_capped();
	steady();
	glide_bends_and_lands();
	crossfade_no_bend();
	crossfade_adds_no_level();
	latest_target_wins();
	freeze_repeats();
	freeze_longer_loop();
	freeze_join();
	freeze_mod();
	bounds();
	return rkatest::finish("wake test_tap");
}

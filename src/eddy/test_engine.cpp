// Eddy's engine (src/eddy/EddyEngine.hpp) against the spec's Testing section:
// the exact loop, length, speed, the window, Smear, Dub, Wear, Play and EOC,
// Restore, the view, Mix, the limit and clicks. Built with the plugin's
// floating-point flags. Tapes are short (0.05 to 1 s) so the 1000-pass
// tests stay quick.
#include "eddy/test_rig.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

using namespace rig;
using rka::eddy::Window;

// Records n frames of in(i) from a Rec press, presses Rec again at frame n,
// and feeds `after` more samples of in() (the tail is taken from these).
// Returns the sample count at the press that ended it.
long record(Rig& g, long n, long after, const std::function<void(long, float&, float&)>& in) {
	float l = 0, r = 0;
	g.press(&F::recPress);
	long end = 0;
	for (long i = 0; i < n + after; i++) {
		if (i == n) {
			g.press(&F::recPress);
			end = g.t;
		}
		in(i, l, r);
		g.step(l, r);
	}
	return end;
}

std::vector<float> noiseVec(size_t n, float volts, uint32_t seed) {
	rka::hadal::Random rnd;
	rnd.seed(seed);
	std::vector<float> x(n);
	for (float& v : x)
		v = volts * rnd.uniform();
	return x;
}

// Rising edges of EOC over n samples, as sample counts.
std::vector<long> eocEdges(Rig& g, long n) {
	std::vector<long> at;
	bool was = g.r.eoc;
	for (long i = 0; i < n; i++) {
		g.step();
		if (g.r.eoc && !was)
			at.push_back(g.t);
		was = g.r.eoc;
	}
	return at;
}

void exact_loop() {
	Rig g;
	g.f.inRPatched = true;
	const long N = 4800, M = 480;   // 0.1 s; m = F = 10 ms
	const std::vector<float> x0 = noiseVec(N + M, 5.f, 11), x1 = noiseVec(N + M, 5.f, 12);
	const long end = record(g, N, M, [&](long i, float& l, float& r) {
		l = i < N + M ? x0[(size_t) i] : 0.f;
		r = i < N + M ? x1[(size_t) i] : 0.f;
	});
	CHECK(!g.r.recording && g.r.playing, "the recording ended and plays");
	// The voice started at the end press reading frame 0, so sample
	// end + j N + k plays frame k.
	for (int pass : {1, 1000}) {
		if (g.t < end + (long) (pass - 1) * N)
			g.run(end + (long) (pass - 1) * N - g.t);
		long bad = 0, first = -1;
		while (g.t < end + (long) pass * N) {
			const long k = g.t - end - (long) (pass - 1) * N;
			g.step();
			if (k > M && (g.r.l != x0[(size_t) k] || g.r.r != x1[(size_t) k])) {
				if (first < 0)
					first = k;
				bad++;
			}
		}
		CHECK(bad == 0, "pass %d: %ld samples outside the join differ from the recorded input (first at frame %ld)", pass, bad,
		      first);
	}
}

void length() {
	for (float fs : {44100.f, 48000.f, 96000.f}) {
		Rig g(fs);
		const long N = std::lround(0.0731 * fs) + 7;
		const std::vector<float> x = noiseVec((size_t) N + 2000, 3.f, 5);
		record(g, N, 2000, [&](long i, float& l, float& r) { l = r = x[(size_t) i]; });
		CHECK(std::lround(g.r.seconds * fs) == N, "%.0f Hz: a recording of %ld frames gives a tape of %.1f", fs, N,
		      (double) g.r.seconds * fs);
		const std::vector<long> at = eocEdges(g, 6 * N);
		bool ok = at.size() >= 5;
		for (size_t i = 1; i < at.size(); i++)
			ok = ok && at[i] - at[i - 1] == N;
		CHECK(ok, "%.0f Hz: %zu turns, each %ld samples (first gap %ld)", fs, at.size(), N, at.size() > 1 ? at[1] - at[0] : -1L);
	}
}

// Left: sin + half the second harmonic, rising a third of each cycle and
// falling two; right: the plain sine, for the pitch.
TapeSet* asymmetric(Rig& g) {
	const double fs = g.fs;
	return g.loaded(48000, 480, [=](size_t k, float& l, float& r) {
		const double th = 2 * PI * 100.0 * (double) k / fs;
		l = (float) (2.0 * (std::sin(th) + 0.5 * std::sin(2 * th)));
		r = (float) (2.0 * std::sin(th));
	});
}

void speed() {
	// Speed x pitch, and the phase's direction.
	for (float knob : {0.5f, -0.5f, 1.f, -1.f, 2.f, -2.f}) {
		Rig g;
		g.f.params[F::SPEED] = knob;
		g.use(asymmetric(g));
		g.press(&F::playPress);
		g.run(4800);
		std::vector<float> y(48000), a(48000);
		long rising = 0;
		for (size_t i = 0; i < y.size(); i++) {
			g.step();
			y[i] = g.r.r;
			a[i] = g.r.l;
			rising += i > 0 && a[i] > a[i - 1];
		}
		const double hz = crossingsHz(y, g.fs), want = 100.0 * std::fabs(knob);
		CHECK(std::fabs(hz / want - 1.0) < 1e-3, "Speed %+.1f plays the 100 Hz tape at %.3f Hz (want %.1f)", knob, hz, want);
		const double up = (double) rising / (double) y.size();
		CHECK(knob > 0 ? up < 0.4 : up > 0.6, "Speed %+.1f: rising %.2f of the time (forwards 1/3, backwards 2/3)", knob, up);
	}
	// V/OCT: an octave a volt, within 1 cent, over +-2 V.
	for (float volts : {-2.f, -1.5f, -1.f, -0.5f, 0.f, 0.5f, 1.f, 1.5f, 2.f}) {
		Rig g;
		g.f.voct = volts;
		g.use(asymmetric(g));
		g.press(&F::playPress);
		g.run(4800);
		std::vector<float> y(96000);
		for (float& v : y) {
			g.step();
			v = g.r.r;
		}
		const double want = 100.0 * std::pow(2.0, volts);
		const double cents = 1200.0 * std::log2(crossingsHz(y, g.fs) / want);
		CHECK(std::fabs(cents) < 1.0, "V/OCT %+.1f V is %+.3f cents off", volts, cents);
	}
	// Stopped: silent, no DC (from the start, and after gliding there).
	for (int glided = 0; glided < 2; glided++) {
		Rig g;
		g.f.params[F::SPEED] = glided ? 1.f : 0.f;
		g.use(asymmetric(g));
		g.press(&F::playPress);
		g.run(4800);
		g.f.params[F::SPEED] = 0.f;
		g.run(48000);
		double peak = 0, sum = 0;
		for (int i = 0; i < 48000; i++) {
			g.step();
			peak = std::max(peak, (double) std::fabs(g.r.l) + std::fabs(g.r.r));
			sum += g.r.l;
		}
		CHECK(peak == 0.0 && sum == 0.0, "Speed 0%s: peak %g, mean %g (want silence)", glided ? " (glided)" : "", peak, sum / 48000);
	}
	// A knob step glides; a V/OCT step lands within 5 ms.
	{
		Rig g;
		g.use(asymmetric(g));
		g.f.playTrig = 10.f;
		g.run(2400);
		g.f.params[F::SPEED] = 2.f;
		const double early = g.speedOver(240);
		g.f.playTrig = 0.f;
		g.run(48000);
		g.f.playTrig = 10.f;   // back to the start, away from a join
		g.run(1000);
		const double late = g.speedOver(480);
		CHECK(early > 1.0 && early < 1.2, "5 ms after a knob step from 1 to 2 the speed is %.4f (gliding)", early);
		CHECK(std::fabs(late / 2.0 - 1.0) < 1e-3, "a second later it is %.5f", late);
	}
	{
		Rig g;
		g.use(asymmetric(g));
		g.f.playTrig = 10.f;
		g.run(2400);
		g.f.voct = 1.f;
		g.run(240);
		const double s = g.speedOver(48);
		const double cents = 1200.0 * std::log2(s / 2.0);
		CHECK(std::fabs(cents) < 1.0, "5 ms after a 1 V step the speed is %.5f (%+.2f cents)", s, cents);
	}
}

// Both sides code the place: (sin, cos) of a turn of the tape.
TapeSet* placeCoded(Rig& g, size_t n, size_t tail = 0) {
	return g.loaded(n, tail, [=](size_t k, float& l, float& r) {
		l = (float) (4.0 * std::sin(2 * PI * (double) k / (double) n));
		r = (float) (4.0 * std::cos(2 * PI * (double) k / (double) n));
	});
}
double place(const EddyResult& r, double n) {
	double p = std::atan2((double) r.l, (double) r.r) / (2 * PI) * n;
	return p < 0 ? p + n : p;
}
// Distance from p forward to q round a tape of n.
double ahead(double p, double q, double n) {
	double d = q - p;
	return d < 0 ? d + n : d;
}

void window() {
	const double n = 48000;
	struct Case {
		float start;
		double frac;
	} cases[] = {{0.25f, 0.3}, {0.8f, 0.4}};
	for (const Case& c : cases) {
		Rig g;
		g.use(placeCoded(g, (size_t) n));
		g.f.params[F::START] = c.start;
		g.f.params[F::LENGTH] = lengthKnob(c.frac * n, n, g.fs);
		const Window w = rka::eddy::window(c.start, 0.f, g.f.params[F::LENGTH], 0.f, n, 0.0, g.fs);
		const bool wraps = w.start + w.length > n;
		const double turn = w.length;   // the tape beyond the end holds the whole of m
		g.press(&F::playPress);
		g.run(2400);
		long outside = 0, samples = 0, firstWrong = 0;
		bool sawEnd = false, sawBeginning = false, eocWas = g.r.eoc;
		std::vector<long> eoc;
		long since = 100000;
		for (long i = 0; i < 4 * (long) turn; i++) {
			g.step();
			if (g.r.eoc && !eocWas) {
				eoc.push_back(g.t);
				since = 0;
			}
			eocWas = g.r.eoc;
			since++;
			if (g.e.activeVoices() != 1)
				continue;
			const double p = place(g.r, n);
			samples++;
			if (ahead(w.start - 1.0, p, n) > w.length + 1.0)
				outside++;
			// Just after the join's fade the head is m frames past Start.
			if (since == (long) std::ceil(w.minFade) + 2 && std::fabs(ahead(w.start, p, n) - (since - 1)) > 1.5)
				firstWrong++;
			sawEnd = sawEnd || p > 0.9 * n;
			sawBeginning = sawBeginning || p < 0.1 * n;
		}
		CHECK(samples > 1000 && outside == 0, "Start %.2f, Length %.2f: %ld of %ld places read outside the window", c.start, c.frac,
		      outside, samples);
		CHECK(firstWrong == 0 && eoc.size() >= 3, "Start %.2f: each turn starts at Start (%ld wrong, %zu turns)", c.start,
		      firstWrong, eoc.size());
		bool spaced = eoc.size() >= 3;
		for (size_t i = 1; i < eoc.size(); i++)
			spaced = spaced && std::fabs((double) (eoc[i] - eoc[i - 1]) - turn) <= 1.0;
		CHECK(spaced, "Start %.2f, Length %.2f: turns of %ld samples (want %.1f)", c.start, c.frac,
		      eoc.size() > 1 ? eoc[1] - eoc[0] : -1L, turn);
		if (wraps)
			CHECK(sawEnd && sawBeginning, "a window past the end wraps: both the end and the beginning are read");
	}
}

// Samples a turn during which two voices sound.
double overlapSamples(Rig& g, long turns, long turn) {
	g.run(turn / 2);
	long two = 0;
	for (long i = 0; i < turns * turn; i++) {
		g.step();
		two += g.e.activeVoices() == 2;
	}
	return (double) two / (double) turns;
}

void smear() {
	// Smear 0: the join is m (F on a whole recording, L / 4 in a short window).
	{
		Rig g;
		g.use(g.loaded(24000, 480, [](size_t k, float& l, float& r) { l = r = k < 24000 ? 1.f : -1.f; }));
		g.press(&F::playPress);
		const double two = overlapSamples(g, 4, 24000);
		CHECK(std::fabs(two - 480.0) <= 1.0, "Smear 0, a whole tape: the join lasts %.1f samples (want m = 480)", two);
	}
	{
		Rig g;
		g.use(g.sine(1.0, 100.0, 2.f));
		g.f.params[F::START] = 0.25f;
		g.f.params[F::LENGTH] = lengthKnob(960, 48000, g.fs);
		const Window w = rka::eddy::window(0.25f, 0.f, g.f.params[F::LENGTH], 0.f, 48000, 0.0, g.fs);
		g.press(&F::playPress);
		const double two = overlapSamples(g, 8, (long) std::lround(w.length));
		CHECK(std::fabs(two - w.minFade) <= 1.0 && std::fabs(w.minFade - w.length / 4) < 1e-9,
		      "Smear 0, a 20 ms window: the join lasts %.1f samples (want L / 4 = %.1f)", two, w.minFade);
	}
	// Above 0: noise keeps its RMS through the overlap; the turn is the law's.
	for (float s : {0.5f, 1.f}) {
		Rig g;
		TapeSet* set = g.noise(1.0, 3.f, 77);
		g.use(set);
		g.f.params[F::SMEAR] = s;
		const Window w = rka::eddy::window(0.f, 0.f, 1.f, s, 48000, 0.0, g.fs);
		g.press(&F::playPress);
		g.run(4800);
		const double ref = rms(set->tape.ch[0], 48000);
		const int B = 2400;
		double worst = 0;
		std::vector<float> blk(B);
		for (int b = 0; b < 40; b++) {
			for (int i = 0; i < B; i++) {
				g.step();
				blk[(size_t) i] = g.r.l;
			}
			worst = std::max(worst, std::fabs(db(rms(blk.data(), B) / ref)));
		}
		CHECK(worst < 0.5, "Smear %.1f: the level wanders %.2f dB through the overlap (want under 0.5)", s, worst);
		const std::vector<long> at = eocEdges(g, 4 * (long) w.turn);
		bool ok = at.size() >= 3;
		for (size_t i = 1; i < at.size(); i++)
			ok = ok && std::fabs((double) (at[i] - at[i - 1]) - w.turn) <= 1.0;
		CHECK(ok, "Smear %.1f: turns of %ld samples (the law: %.1f)", s, at.size() > 1 ? at[1] - at[0] : -1L, w.turn);
	}
	// A whole 5 minute tape (at 8 kHz) at Smear 1: the overlap is 1.2 M
	// frames, the fades move under 1e-6 a frame, and the level holds.
	{
		Rig g(8000.f);
		TapeSet* set = g.noise(300.0, 3.f, 78);
		g.f.params[F::SMEAR] = 1.f;
		g.use(set);
		g.press(&F::playPress);
		const double ref = rms(set->tape.ch[0], set->tape.length);
		const long B = 8000;
		std::vector<float> blk((size_t) B);
		double worst = 0;
		int most = 0;
		for (long b = 0; b < 1300000 / B * 2; b++) {
			for (long i = 0; i < B; i++) {
				g.step();
				blk[(size_t) i] = g.r.l;
				most = std::max(most, g.e.activeVoices());
			}
			if (b > 0)
				worst = std::max(worst, std::fabs(db(rms(blk.data(), (size_t) B) / ref)));
		}
		CHECK(worst < 0.5 && most == 2, "Smear 1 on a 5 minute tape: the level wanders %.2f dB over two turns, %d voices", worst, most);
	}
}

// Least squares: x[k] ~ a sin + b cos at each of two frequencies (cycles a
// frame); the two amplitudes.
void fitTwo(const float* x, size_t n, long k0, double f1, double f2, double& amp1, double& amp2) {
	double A[4][5] = {};
	for (size_t i = 0; i < n; i++) {
		const double k = (double) (k0 + (long) i);
		const double b[4] = {std::sin(2 * PI * f1 * k), std::cos(2 * PI * f1 * k), std::sin(2 * PI * f2 * k),
		                     std::cos(2 * PI * f2 * k)};
		for (int r = 0; r < 4; r++) {
			for (int c = 0; c < 4; c++)
				A[r][c] += b[r] * b[c];
			A[r][4] += b[r] * x[i];
		}
	}
	for (int p = 0; p < 4; p++)
		for (int r = p + 1; r < 4; r++) {
			const double q = A[r][p] / A[p][p];
			for (int c = p; c < 5; c++)
				A[r][c] -= q * A[p][c];
		}
	double s[4];
	for (int r = 3; r >= 0; r--) {
		double v = A[r][4];
		for (int c = r + 1; c < 4; c++)
			v -= A[r][c] * s[c];
		s[r] = v / A[r][r];
	}
	amp1 = std::hypot(s[0], s[1]);
	amp2 = std::hypot(s[2], s[3]);
}

void dub_levels() {
	for (float d : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		Rig g;
		TapeSet* set = g.sine(1.0, 100.0, 2.f);
		g.use(set);
		const Snapshot before(set->tape);
		g.f.params[F::DUB_LEVEL] = d;
		g.press(&F::playPress);
		g.run(4800);
		const long k0 = (long) std::lround(g.r.head * 48000.0);
		g.press(&F::dubPress);
		for (int i = 0; i < 24000; i++)
			g.step(2.f * (float) std::sin(2 * PI * 1000.0 * i / 48000.0));
		g.press(&F::dubPress);
		g.run(2000);
		CHECK(!g.r.dubbing, "dub off");
		if (d == 0.f) {
			CHECK(before.equals(set->tape), "Dub Level 0: the tape is unchanged to the bit");
			continue;
		}
		// Frames written once at full gain: well inside [k0 + F, k0 + 24000).
		const long a = k0 + 700, n = 22000;
		double oldA, newA;
		fitTwo(set->tape.ch[0] + a, (size_t) n, a, 100.0 / 48000.0, 1000.0 / 48000.0, oldA, newA);
		float add, keep;
		rka::eddy::dubGains(d, add, keep);
		CHECK(std::fabs(db(newA / 2.0 / add)) < 0.2, "Dub Level %.2f: the new layer is %+.2f dB re %.3f", d, db(newA / 2.0 / add),
		      add);
		if (d < 1.f)
			CHECK(std::fabs(db(oldA / 2.0 / keep)) < 0.2, "Dub Level %.2f: the old is %+.2f dB re %.4f", d, db(oldA / 2.0 / keep),
			      keep);
		else
			CHECK(oldA / 2.0 < 1e-3, "Dub Level 1: the old is gone (%.2e of it left)", oldA / 2.0);
	}
}

void dub_at_speed() {
	for (float knob : {0.5f, -1.f}) {
		Rig g;
		g.f.params[F::SPEED] = knob;
		g.use(g.loaded(48000, 0, [](size_t, float& l, float& r) { l = r = 0.f; }));
		g.f.params[F::DUB_LEVEL] = 1.f;
		g.press(&F::playPress);
		g.run(4800);
		const double v = std::fabs((double) rka::eddy::speedKnob(knob));
		const Window w = rka::eddy::window(0.f, 0.f, 1.f, 0.f, 48000, 0.0, g.fs);
		const long T = std::lround(w.turn / v);   // samples a turn
		const long D = (long) (0.8 * T);
		std::vector<float> in((size_t) D), out((size_t) D);
		std::vector<int> single((size_t) D);
		g.press(&F::dubPress);
		for (long i = 0; i < D; i++) {
			in[(size_t) i] = 2.f * (float) std::sin(2 * PI * 1000.0 * i / 48000.0 + 0.3);
			g.step(in[(size_t) i]);
		}
		g.press(&F::dubPress);
		g.run(T - D);
		for (long i = 0; i < D; i++) {
			g.step();
			out[(size_t) i] = g.r.l;
			single[(size_t) i] = g.e.activeVoices() == 1;
		}
		double err = 0, sig = 0;
		for (long i = 1000; i < D - 1000; i++) {
			if (!single[(size_t) i])
				continue;
			err += std::pow((double) out[(size_t) i] - in[(size_t) i], 2);
			sig += std::pow((double) in[(size_t) i], 2);
		}
		CHECK(sig > 0 && db(std::sqrt(err / sig)) < -40.0, "dubbed at Speed %+.1f and played back: the error is %.1f dB", knob,
		      db(std::sqrt(err / (sig > 0 ? sig : 1))));
	}
}

void dub_bounded() {
	// 1000 passes of +-10 V noise (the same loop of it each pass, with an
	// offset or without), dubbed at full level: the tape stays under its
	// bound and no DC builds. At noon the bound holds too, and the DC is what
	// this loop leaves (its saturation after the DC blocker, and the join's
	// fade frames, which converge slowly at their own weight): settled by
	// 1000 passes (the same at 2000) and under 20 mV.
	for (float d : {1.f, 0.5f})
		for (float offset : {0.f, 1.f}) {
			Rig g;
			TapeSet* set = g.loaded(2400, 0, [](size_t, float& l, float& r) { l = r = 0.f; });
			g.use(set);
			g.f.params[F::DUB_LEVEL] = d;
			g.f.inRPatched = true;
			g.press(&F::playPress);
			const Window w = rka::eddy::window(0.f, 0.f, 1.f, 0.f, 2400, 0.0, g.fs);
			const long T = std::lround(w.turn);
			const std::vector<float> n0 = noiseVec((size_t) T, 10.f, 3), n1 = noiseVec((size_t) T, 10.f, 4);
			auto measure = [&](float& peak, double& dc) {
				peak = 0;
				double sum = 0;
				for (int c = 0; c < 2; c++)
					for (size_t k = 0; k < 2400; k++) {
						peak = std::max(peak, std::fabs(set->tape.ch[c][k]));
						sum += set->tape.ch[c][k];
					}
				dc = sum / 4800.0;
			};
			g.press(&F::dubPress);
			float peak1000 = 0.f, peak = 0.f;
			double dc1000 = 0.0, dc = 0.0;
			const int passes = d == 1.f ? 1000 : 2000;
			for (int pass = 0; pass < passes; pass++) {
				for (long i = 0; i < T; i++)
					g.step(n0[(size_t) i] + offset, n1[(size_t) i] + offset);
				if (pass == 999)
					measure(peak1000, dc1000);
			}
			measure(peak, dc);
			CHECK(peak <= 10.f && peak1000 <= 10.f, "Dub Level %.1f, offset %.0f V: the tape peaks at %.4f V (bound 10)", d, offset, peak);
			if (d == 1.f)
				CHECK(std::fabs(dc) < 1e-3, "full-level dubs, offset %.0f V: DC on the tape %.3f mV (want under 1)", offset, dc * 1e3);
			else   // The spec: "Repeated full-level dubs ... no DC builds"; noon is held to not building.
				CHECK(std::fabs(dc) < 2e-2 && std::fabs(dc - dc1000) < 5e-4,
				      "Dub Level %.1f, offset %.0f V: DC on the tape %.3f mV after 2000 passes, %.3f after 1000 (want under 20, not building)",
				      d, offset, dc * 1e3, dc1000 * 1e3);
		}
}

uint64_t checksum(const Tape& t) {
	uint64_t h = 1469598103934665603ull;
	for (int c = 0; c < 2; c++) {
		const unsigned char* b = (const unsigned char*) t.ch[c] - Tape::GUARD * sizeof(float);
		const size_t n = (t.capacity() + t.tailCapacity() + 2 * Tape::GUARD) * sizeof(float);
		for (size_t i = 0; i < n; i++)
			h = (h ^ b[i]) * 1099511628211ull;
	}
	return h;
}

void wear_zero() {
	Rig g(8000.f);   // a small allocation to checksum
	TapeSet* set = g.noise(0.05, 4.f, 9, 80);
	g.use(set);
	const uint64_t h0 = checksum(set->tape), o0 = checksum(set->original);
	g.press(&F::playPress);
	g.run(1000 * 400);
	CHECK(checksum(set->tape) == h0 && checksum(set->original) == o0, "Wear 0, Dub off: 1000 passes leave the tape's memory unchanged");
	CHECK(g.r.playing && g.e.activeVoices() >= 1, "it played");
}

void wear_window() {
	// Both directions: going backwards the last move of a turn ends a frame
	// below Start, and only the window's clip keeps the write off it.
	for (float knob : {1.f, -1.f})
		for (float start : {0.3f, 0.8f}) {
			Rig g;
			TapeSet* set = g.noise(0.2, 3.f, 21, 480);
			const double n = 9600;
			g.f.params[F::SPEED] = knob;
			g.f.params[F::START] = start;
			g.f.params[F::LENGTH] = lengthKnob(0.4 * n, n, g.fs);
			g.f.params[F::WEAR] = 1.f;
			g.use(set);
			const Snapshot before(set->tape);
			const Window w = rka::eddy::window(start, 0.f, g.f.params[F::LENGTH], 0.f, n, 0.0, g.fs);
			g.press(&F::playPress);
			g.run(50 * (long) w.length);
			// What the voices pass: the window and the join's read-on, m past
			// its end (the tape there or the tail) going forwards, m below
			// Start (and the frame Start - 1) going backwards; round the tape
			// if the window wraps. Beyond both nothing changes.
			const bool wraps = w.start + w.length > n;
			const double beyond = wraps ? n - w.length : n + 480 - (w.start + w.length), below = wraps ? n - w.length : w.start - 1;
			const double t = std::min(w.minFade, beyond), tb = std::min(w.minFade, below);
			const double first = knob > 0 ? std::floor(w.start) : std::floor(w.start - 1.0 - tb);
			const double last = knob > 0 ? std::ceil(w.start + w.length + t) : std::ceil(w.start + w.length);
			long changedOut = 0, changedIn = 0, in = 0;
			for (size_t k = 0; k < (size_t) n + 480; k++) {
				const double kk = (double) k;
				const bool inside = wraps ? k < (size_t) n && ahead(first, kk, n) < last - first : kk >= first && kk < last;
				const bool changed = set->tape.ch[0][k] != before.ch[0][k] || set->tape.ch[1][k] != before.ch[1][k];
				if (inside) {
					in++;
					changedIn += changed;
				}
				else
					changedOut += changed;
			}
			CHECK(changedOut == 0, "Wear 1, Speed %+.0f, Start %.1f: %ld frames beyond the window and its read-on changed", knob,
			      start, changedOut);
			CHECK(changedIn > in * 9 / 10, "Wear 1, Speed %+.0f, Start %.1f: %ld of %ld frames in the window worn", knob, start,
			      changedIn, in);
		}
	// Dropouts take both sides together, through the engine's own write path.
	{
		Rig g;
		TapeSet* set = g.sine(0.2, 100.0, 2.f);
		g.use(set);
		g.f.params[F::WEAR] = 1.f;
		g.press(&F::playPress);
		g.run(60 * 9600);
		float worstDiff = 0;
		double lo = 1e9, hi = 0;
		for (size_t b = 0; b + 240 <= 9600; b += 240) {
			lo = std::min(lo, rms(set->tape.ch[0] + b, 240));
			hi = std::max(hi, rms(set->tape.ch[0] + b, 240));
			for (size_t k = b; k < b + 240; k++)
				worstDiff = std::max(worstDiff, std::fabs(set->tape.ch[0][k] - set->tape.ch[1][k]));
		}
		CHECK(lo < 0.85 * hi, "Wear 1 for 12 s of tape: a dropout has marked it (quietest 5 ms at %.2f of the loudest)", lo / hi);
		CHECK(worstDiff < 0.05f, "both sides dip together: they differ by at most %.3f V", worstDiff);
	}
}

void play_trigger() {
	const double n = 48000;
	for (float knob : {1.f, -1.f}) {
		Rig g;
		g.f.params[F::SPEED] = knob;
		g.use(placeCoded(g, (size_t) n));
		g.f.params[F::START] = 0.3f;
		g.f.params[F::LENGTH] = lengthKnob(0.4 * n, n, g.fs);
		const Window w = rka::eddy::window(0.3f, 0.f, g.f.params[F::LENGTH], 0.f, n, 0.0, g.fs);
		const double from = knob > 0 ? w.start : w.start + w.length - 1.0;
		for (int round = 0; round < 2; round++) {   // from stopped, then a retrigger while playing
			g.run(round ? 7000 : 100);
			g.f.playTrig = 10.f;
			g.step();
			g.f.playTrig = 0.f;
			CHECK(g.r.playing, "a PLAY trigger turns Play on");
			const double h = g.r.head * n;
			CHECK(std::fabs(h - from) < 1.5, "Speed %+.0f, %s: the head goes to %.1f (want %.1f)", knob, round ? "playing" : "stopped",
			      h, from);
			g.run(480);
			const double p = place(g.r, n), want = from + knob * 481.0;
			CHECK(std::fabs(p - want) < 2.0, "Speed %+.0f, %s: F after the trigger it reads %.1f (want %.1f)", knob,
			      round ? "playing" : "stopped", p, want);
		}
	}
}

void one_shot() {
	Rig g;
	g.use(g.sine(0.5, 100.0, 2.f, 0.0, 480));
	g.f.loop = false;
	for (int shot = 0; shot < 2; shot++) {
		g.f.playTrig = 10.f;
		g.step();
		g.f.playTrig = 0.f;
		long edges = 0, playingAt = -1;
		bool was = g.r.eoc;
		double late = 0;
		for (long i = 0; i < 36000; i++) {
			g.step();
			edges += g.r.eoc && !was;
			was = g.r.eoc;
			if (g.r.playing)
				playingAt = i;
			if (i > 24000 + 960)
				late = std::max(late, (double) std::fabs(g.r.l));
		}
		CHECK(edges == 1, "Loop off, shot %d: %ld EOCs (want one)", shot, edges);
		CHECK(!g.r.playing && std::labs(playingAt - 23999) <= 2, "Loop off, shot %d: Play goes off at the window's end (%ld)", shot,
		      playingAt);
		CHECK(late == 0.0, "Loop off, shot %d: silent after the end (%.3g)", shot, late);
	}
}

void eoc() {
	for (float knob : {1.f, -1.f}) {
		Rig g;
		g.f.params[F::SPEED] = knob;
		g.use(g.sine(0.25, 100.0, 2.f, 0.0, 480));
		g.press(&F::playPress);
		g.run(100);
		long width = 0, maxWidth = 0, edges = 0;
		std::vector<long> at;
		bool was = false;
		for (long i = 0; i < 5 * 12000; i++) {
			g.step();
			if (g.r.eoc) {
				width++;
				if (!was)
					at.push_back(g.t);
			}
			else {
				if (was)
					maxWidth = std::max(maxWidth, width);
				width = 0;
			}
			was = g.r.eoc;
		}
		edges = (long) at.size();
		const Window w = rka::eddy::window(0.f, 0.f, 1.f, 0.f, 12000, knob > 0 ? 480.0 : 0.0, g.fs);
		bool spaced = edges >= 4;
		for (size_t i = 1; i < at.size(); i++)
			spaced = spaced && std::fabs((double) (at[i] - at[i - 1]) - w.turn) <= 1.0;
		CHECK(spaced, "Speed %+.0f: EOC once a turn (%ld in 5 turns, first gap %ld, want %.0f)", knob, edges,
		      at.size() > 1 ? at[1] - at[0] : -1L, w.turn);
		CHECK(maxWidth == 48, "Speed %+.0f: EOC is %ld samples wide (want 48)", knob, maxWidth);
	}
}

void restore() {
	Rig g;
	g.copySamples = 64;   // the copy takes its time, a slice a sample
	TapeSet* set = g.noise(0.05, 3.f, 31, 480);
	g.use(set);
	const Snapshot first(set->original);
	g.f.params[F::WEAR] = 1.f;
	g.press(&F::playPress);
	g.run(4800);
	g.press(&F::dubPress);
	for (int i = 0; i < 4800; i++)
		g.step(2.f * (float) std::sin(2 * PI * 300.0 * i / 48000.0));
	g.press(&F::dubPress);
	g.run(1000 * 2400);   // a turn is the whole 2400: the tail holds m
	while (std::fabs(g.r.head - 0.5) > 0.01)   // Restore with the head mid-tape
		g.step();
	CHECK(!first.equals(set->tape), "dubbed and worn, the tape differs from the original");
	CHECK(first.equals(set->original), "the original never changed");
	const double h0 = g.r.head * 2400;
	long done = -1;
	g.onCopyDone = [&](EddyEngine::Copy c) {
		if (c == EddyEngine::ORIGINAL_TO_TAPE && done < 0) {
			done = g.t;
			CHECK(sameTape(set->tape, set->original), "once the copy is done the tape equals the original to the bit");
		}
	};
	const long t0 = g.t;
	g.press(&F::restorePress);
	g.run(5000);
	CHECK(done > 0 && done - t0 < 4800, "Restore: the copy was asked for and done (%ld samples)", done - t0);
	CHECK(g.tapeMatchedAtDone, "nothing wrote the tape while it was copied");
	CHECK(first.equals(set->original), "the original is unchanged by Restore");
	// The head kept its place: it moved on by the time passed, round the
	// turn of 2400.
	const double h1 = g.r.head * 2400;
	const double moved = std::fmod(h1 - h0 + 4 * 2400.0, 2400.0), want = std::fmod(5000.0, 2400.0);
	CHECK(std::fabs(moved - want) < 2.0, "the head kept its place (moved %.1f, want %.1f)", moved, want);
	// Triggered at audio rate for a second, it stays so.
	g.f.params[F::WEAR] = 0.f;
	g.copySamples = 0;
	for (int i = 0; i < 48000; i++) {
		g.f.restoreTrig = (i / 4) % 2 ? 10.f : 0.f;
		g.step();
	}
	g.f.restoreTrig = 0.f;
	g.run(4800);
	CHECK(first.equals(set->tape) && first.equals(set->original), "after a second of Restore at audio rate both are the original");
}

void make_original() {
	Rig g;
	TapeSet* set = g.noise(0.05, 3.f, 41, 480);
	g.use(set);
	const Snapshot first(set->original);
	g.f.params[F::WEAR] = 1.f;
	g.press(&F::playPress);
	g.run(200 * 1920);
	g.e.requestCopy(EddyEngine::TAPE_TO_ORIGINAL);
	g.run(2000);
	const Snapshot worn(set->original);
	CHECK(!first.equals(set->original), "Make this the original: the original is now the worn tape");
	g.run(200 * 1920);
	g.f.params[F::WEAR] = 0.f;   // and its write fades out (F) before Restore
	g.run(960);
	g.press(&F::restorePress);
	g.run(4800);
	CHECK(worn.equals(set->tape) && !first.equals(set->tape), "then Restore returns the worn tape, not the first take");
}

void view() {
	const double n = 48000;
	{
		Rig g;
		g.f.params[F::START] = 0.25f;
		g.f.params[F::LENGTH] = lengthKnob(0.5 * n, n, g.fs);
		g.f.params[F::SMEAR] = 0.4f;
		g.use(g.sine(1.0, 100.0, 2.f));
		const Window w = rka::eddy::window(0.25f, 0.f, g.f.params[F::LENGTH], 0.4f, n, 0.0, g.fs);
		g.press(&F::playPress);
		g.run(9000);
		const double h = g.r.head;
		CHECK(std::fabs(g.r.winStart - 0.25) < 1e-6 && std::fabs(g.r.winLength - w.length / n) < 1e-6 &&
		          std::fabs(g.r.overlap - w.overlap / n) < 1e-6 && std::fabs(g.r.seconds - 1.0) < 1e-6,
		      "a window in the middle: start %.4f, length %.4f, overlap %.4f, %.3f s", g.r.winStart, g.r.winLength, g.r.overlap,
		      g.r.seconds);
		CHECK(std::fabs(h - (0.25 + (9000 - 1) / n)) < 2 / n, "the head at %.5f (want %.5f)", h, 0.25 + 8999 / n);
	}
	{
		Rig g;
		g.f.params[F::START] = 0.8f;
		g.f.params[F::LENGTH] = lengthKnob(0.5 * n, n, g.fs);
		g.use(g.sine(1.0, 100.0, 2.f));
		g.press(&F::playPress);
		g.run(14400);
		CHECK(std::fabs(g.r.winStart - 0.8) < 1e-6 && g.r.winStart + g.r.winLength > 1.f, "a window that wraps: start %.3f, length %.3f",
		      g.r.winStart, g.r.winLength);
		CHECK(std::fabs(g.r.head - std::fmod(0.8 + 14399 / n, 1.0)) < 2 / n, "past the end the head reads from the beginning (%.4f)",
		      g.r.head);
	}
	{
		Rig g;
		g.press(&F::recPress);
		g.run(4800);
		const double c = 300.0 * 48000;
		CHECK(g.r.recording && std::fabs(g.r.head - 4800 / c) < 1e-9 && g.r.winStart == 0.f &&
		          std::fabs(g.r.winLength - 4800 / c) < 1e-9 && std::fabs(g.r.seconds - 0.1) < 1e-6,
		      "while recording: the head at %.3g and the tape %.3g of the capacity, %.3f s", g.r.head, g.r.winLength, g.r.seconds);
	}
}

void mix_law() {
	struct Fit {
		double tape, input;
	};
	auto measure = [](float mix, int mode) {
		Rig g;
		TapeSet* set = g.sine(1.0, 100.0, 2.f);
		g.use(set);
		g.f.params[F::MIX] = mix;
		g.f.params[F::DUB_LEVEL] = 0.f;   // a dub that writes nothing
		g.press(&F::playPress);
		if (mode == 1)
			g.press(&F::dubPress);
		if (mode == 2)
			g.press(&F::recPress);
		std::vector<float> y(24000);
		for (int i = 0; i < 26400; i++) {
			const float in = 2.f * (float) std::sin(2 * PI * 1000.0 * i / 48000.0);
			g.step(in);
			if (i >= 2400)
				y[(size_t) (i - 2400)] = g.r.l;
		}
		Fit f;
		// The tape: 100 Hz in the tape's frames, which run with the samples.
		fitTwo(y.data(), y.size(), 2400, 100.0 / 48000.0, 1000.0 / 48000.0, f.tape, f.input);
		f.tape /= 2.0;
		f.input /= 2.0;
		return f;
	};
	for (float mix : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
		const Fit f = measure(mix, 0);
		const double wet = std::sin(PI / 2 * mix), dry = std::cos(PI / 2 * mix);
		CHECK(std::fabs(f.tape - wet) < 1e-3 && std::fabs(f.input - dry) < 1e-3, "Mix %.2f: tape %.4f, input %.4f (want %.4f, %.4f)",
		      mix, f.tape, f.input, wet, dry);
	}
	for (int mode : {1, 2}) {
		const Fit f = measure(1.f, mode);
		CHECK(std::fabs(f.input - 1.0) < 1e-3, "%s at Mix 1: the input is heard at %.4f (want unity)", mode == 1 ? "dubbing" : "recording",
		      f.input);
	}
	// Mix 1 is the tape alone, to the bit.
	{
		Rig g;
		TapeSet* set = g.sine(1.0, 100.0, 2.f);
		g.use(set);
		g.f.playTrig = 10.f;
		g.run(1000);
		bool exact = true;
		for (int i = 0; i < 4000; i++) {
			g.step(3.f);
			const size_t k = (size_t) (1000 + i);
			exact = exact && g.r.l == set->tape.ch[0][k] && g.r.r == set->tape.ch[1][k];
		}
		CHECK(exact, "Mix 1: the output is the tape, to the bit");
	}
}

void limit() {
	Rig g(48000.f, 1, false);
	g.f.params[F::MIX] = 0.f;
	g.f.inRPatched = true;
	g.run(16);
	bool identity = true, bounded = true, monotonic = true;
	float last = -1e9f;
	for (int i = 0; i <= 4000; i++) {
		const float x = -20.f + 40.f * i / 4000.f;
		g.step(x, -x);
		if (std::fabs(x) <= 8.f)
			identity = identity && g.r.l == x && g.r.r == -x;
		bounded = bounded && std::fabs(g.r.l) <= 10.f && std::fabs(g.r.r) <= 10.f;
		monotonic = monotonic && g.r.l >= last;
		last = g.r.l;
	}
	CHECK(identity, "the output is the input to the bit below 8 V");
	CHECK(bounded && monotonic, "and never over 10 V, rising with it");
	g.step(NAN, INFINITY);
	CHECK(g.r.l == 0.f && g.r.r == 0.f, "NaN and infinite inputs read as 0 V");
}

// The largest sample-to-sample step of a tape round its turn (frames
// [0, n) then the tail).
float tapeStep(const Tape& t) {
	float worst = 0;
	for (int c = 0; c < 2; c++)
		for (size_t k = 1; k < t.length + t.tail; k++)
			worst = std::max(worst, std::fabs(t.ch[c][k] - t.ch[c][k - 1]));
	return worst;
}

void no_clicks() {
	Rig g;
	const double w = 2 * PI * 100.0 / 48000.0;
	const float bound = 1.5f * 5.f * (float) w;   // 1.5 x the 5 V sine's largest step
	long t = 0;
	float worst = 0, prev = 0;
	const char* where = "start";
	const char* worstAt = "";
	float input = 5.f;
	double inPhase = 0.0;
	auto tick = [&](long n, const char* what) {
		where = what;
		for (long i = 0; i < n; i++) {
			g.step(input * (float) std::sin(w * (double) t + inPhase));
			t++;
			const float d = std::fabs(g.r.l - prev);
			if (d > worst) {
				worst = d;
				worstAt = where;
			}
			prev = g.r.l;
		}
	};
	tick(1000, "start");
	g.press(&F::recPress);   // Rec in: on an empty tape
	tick(48000, "recording");
	g.press(&F::recPress);   // Rec out: 1 s, 100 cycles
	tick(2400, "rec out");
	input = 2.f;
	inPhase = PI / 2;        // a dub at right angles to the tape
	tick(48000 * 2, "the join");
	g.press(&F::dubPress);
	tick(24000, "dub in");
	g.press(&F::dubPress);
	tick(4800, "dub out");
	TapeSet* live = g.e.live();
	const float written = tapeStep(live->tape);
	tick(48000, "after the dub");
	g.press(&F::restorePress);
	tick(9600, "restore");
	g.f.playTrig = 10.f;
	tick(4800, "retrigger");
	g.f.playTrig = 0.f;
	tick(10000, "playing");
	g.press(&F::playPress);
	tick(4800, "stop");
	g.press(&F::playPress);
	tick(9600, "play");
	for (int i = 0; i < 24000; i++) {
		g.f.params[F::START] = 0.3f * i / 24000.f;
		tick(1, "Start by knob");
	}
	for (int i = 0; i < 24000; i++) {
		g.f.params[F::LENGTH] = 1.f - 0.3f * i / 24000.f;
		tick(1, "Length by knob");
	}
	g.f.startCv = 1.f;
	tick(9600, "Start by CV");
	g.f.startCv = 0.f;
	tick(9600, "Start by CV");
	g.f.params[F::START] = 0.f;
	g.f.params[F::LENGTH] = 1.f;
	tick(9600, "knobs back");
	CHECK(g.e.offer(g.sine(1.0, 100.0, 5.f, 2.0, 480)), "a new tape is offered");
	tick(9600, "a new tape");
	g.press(&F::recPress);   // Rec in over a playing tape
	tick(24000, "rec in");
	g.press(&F::recPress);
	tick(9600, "rec out");
	g.e.clear();
	tick(9600, "clear");
	CHECK(worst <= bound, "the largest step at the output is %.4f V, at %s (bound %.4f)", worst, worstAt, bound);
	CHECK(written <= bound, "the largest step written on the tape by the dub is %.4f V (bound %.4f)", written, bound);
	CHECK(g.r.empty && g.r.l == 0.f, "cleared: empty and silent");
}

// The join after a dub or wear has crossed it (Smear 0 and 1, forwards and
// backwards, a whole tape with its tail and a short window inside a longer
// tape): no step at the output over the bound, and none on the tape across
// the window's end and the join's read-on.
void no_clicks_join() {
	const double w = 2 * PI * 100.0 / 48000.0;
	const float bound = 1.5f * 5.f * (float) w;
	for (int shortWindow = 0; shortWindow < 2; shortWindow++)
		for (float smearK : {0.f, 1.f})
			for (float knob : {1.f, -1.f}) {
				Rig g;
				const double n = 24000;
				g.f.params[F::SPEED] = knob;
				g.f.params[F::SMEAR] = smearK;
				if (shortWindow) {
					g.f.params[F::START] = 0.25f;
					g.f.params[F::LENGTH] = lengthKnob(14400, n, g.fs);
				}
				TapeSet* set = g.sine(0.5, 100.0, 5.f, 0.0, 480);
				g.use(set);
				const Window win = rka::eddy::window(g.f.params[F::START], 0.f, g.f.params[F::LENGTH], smearK, n, 0.0, g.fs);
				// The turn: the tape beyond the window's end holds the whole of m
				// (the tail, or tape), and so does the tape before Start going
				// backwards, except from frame 0 of the whole tape.
				const double tf = win.minFade, tb = shortWindow ? win.minFade : 0.0;
				const long T = std::lround(win.length - (win.overlap - (knob > 0 ? tf : tb)));
				long t = 0;
				float worst = 0, prev = 0, input = 0.f;
				const char* where = "";
				const char* worstAt = "";
				auto tick = [&](long k, const char* what) {
					where = what;
					for (long i = 0; i < k; i++) {
						g.step(input * (float) std::sin(w * (double) t + PI / 2));
						t++;
						const float d = std::fabs(g.r.l - prev);
						if (t > 1000 && d > worst) {
							worst = d;
							worstAt = where;
						}
						prev = g.r.l;
					}
				};
				g.press(&F::playPress);
				tick(T * 7 / 10, "playing");
				input = 1.f;
				g.press(&F::dubPress);   // a dub across the join
				tick(T * 6 / 10, "dubbing across the join");
				g.press(&F::dubPress);
				tick(960, "dub out");
				input = 0.f;
				tick(T * 12 / 10, "the dub through the join");
				const float dubbed = tapeStep(set->tape);
				g.f.params[F::WEAR] = 1.f;
				tick(30 * T, "Wear 1, 30 passes");
				const float worn = tapeStep(set->tape);
				const char* name = shortWindow ? "a short window" : "a whole tape";
				CHECK(worst <= bound, "%s, Smear %.0f, Speed %+.0f: the largest step at the output is %.4f V, at %s (bound %.4f)", name,
				      smearK, knob, worst, worstAt, bound);
				CHECK(dubbed <= bound && worn <= bound,
				      "%s, Smear %.0f, Speed %+.0f: the largest step on the tape is %.4f V after the dub, %.4f worn (bound %.4f)", name,
				      smearK, knob, dubbed, worn, bound);
			}
}

// A dub made across the join is heard at its own level through the join on
// the next turn.
void dub_through_join() {
	for (float smearK : {0.f, 1.f}) {
		Rig g;
		g.f.params[F::SMEAR] = smearK;
		g.f.params[F::DUB_LEVEL] = 1.f;
		TapeSet* set = g.loaded(24000, 480, [](size_t, float& l, float& r) { l = r = 0.f; });
		g.use(set);
		const Window win = rka::eddy::window(0.f, 0.f, 1.f, smearK, 24000, 480.0, g.fs);
		const long T = std::lround(win.turn);
		g.press(&F::playPress);
		// After the first join: the first turn's head came in by F, not by a
		// join's fade, so it wrote its frames at full weight.
		g.run(T + T / 2);
		g.press(&F::dubPress);
		for (long i = 0; i < T; i++)
			g.step(2.f * (float) std::sin(2 * PI * 1000.0 * (double) i / 48000.0));
		g.press(&F::dubPress);
		double lo = 1e9, hi = -1e9;
		std::vector<float> blk(48);
		for (long b = 0; b < T / 48; b++) {
			for (int i = 0; i < 48; i++) {
				g.step();
				blk[(size_t) i] = g.r.l;
			}
			if (b * 48 < 1000 || b * 48 > T - 1000)
				continue;   // the dub's own fades in and out
			const double level = db(rms(blk.data(), 48) / std::sqrt(2.0));
			lo = std::min(lo, level);
			hi = std::max(hi, level);
		}
		CHECK(lo > -0.5 && hi < 0.5, "Smear %.0f: a dub across the join is heard next turn at %+.2f to %+.2f dB of its own level", smearK,
		      lo, hi);
	}
}

} // namespace

int main() {
	exact_loop();
	length();
	speed();
	window();
	smear();
	dub_levels();
	dub_at_speed();
	dub_bounded();
	wear_zero();
	wear_window();
	play_trigger();
	one_shot();
	eoc();
	restore();
	make_original();
	view();
	mix_law();
	limit();
	no_clicks();
	no_clicks_join();
	dub_through_join();
	return rkatest::finish("eddy test_engine");
}

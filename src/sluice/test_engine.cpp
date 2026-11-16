// The Sluice engine (SluiceEngine.hpp): the Lin and Exp curves, Level plus
// CV, the rope (inputs normal downward), the mix downward, polyphony, the
// light and the Level smoother. Every expected value is worked out here from
// the spec's formulas with libm (Lin: in x p; Exp: in x 10^(3 (p - 1)) x
// min(1, p / 0.05)), never with sluiceCurve. Built with the plugin's
// floating-point flags (tests.mk). Run with `make test`.
#include "SluiceEngine.hpp"
#include "testing.hpp"
#include <algorithm>
#include <cmath>

using F = rka::SluiceFrame;

namespace {

const float FS = 48000.f;
const int ROWS = 4;

// The spec's curves, in double through libm.
double refGain(double p, bool exp) {
	if (!exp)
		return p;
	return std::pow(10.0, 3.0 * (p - 1.0)) * std::min(1.0, p / 0.05);
}

double clamp01(double p) {
	return p < 0.0 ? 0.0 : p > 1.0 ? 1.0 : p;
}

bool near(double a, double b, double tol) {
	return std::fabs(a - b) <= tol;
}

bool nearRel(double a, double b, double rel) {
	return std::fabs(a - b) <= rel * std::fabs(b);
}

// A whole patch as plain arrays; frame() points the ports into it, so the
// Patch must outlive the frame.
struct Patch {
	float in[ROWS][16] = {};
	int inCh[ROWS] = {};
	float cv[ROWS][16] = {};
	int cvCh[ROWS] = {};
	bool outConn[ROWS] = {};
	float level[ROWS] = {};
	bool exp = false;

	void setIn(int r, std::initializer_list<float> v) {
		int c = 0;
		for (float x : v)
			in[r][c++] = x;
		inCh[r] = c;
	}
	void setCv(int r, std::initializer_list<float> v) {
		int c = 0;
		for (float x : v)
			cv[r][c++] = x;
		cvCh[r] = c;
	}
	void allConnected(bool on) {
		for (bool& b : outConn)
			b = on;
	}
	void allLevels(float l) {
		for (float& x : level)
			x = l;
	}

	F frame() const {
		F f;
		for (int r = 0; r < ROWS; r++) {
			f.params[F::LEVEL_A + r] = level[r];
			f.in[r].v = in[r];
			f.in[r].channels = inCh[r];
			f.cv[r].v = cv[r];
			f.cv[r].channels = cvCh[r];
			f.outConnected[r] = outConn[r];
		}
		f.params[F::CURVE] = exp ? 1.f : 0.f;
		return f;
	}
};

// Runs n samples of a still patch; r holds the last.
void run(rka::SluiceEngine& e, const Patch& p, int n, rka::SluiceResult& r) {
	const F f = p.frame();
	for (int i = 0; i < n; i++)
		e.process(f, FS, r);
}

const int SETTLE = 64;

void testCurveEndpoints() {
	for (int e = 0; e < 2; e++) {
		CHECK(rka::sluiceCurve(0.f, e) == 0.f, "curve(0, exp=%d) = %g", e, rka::sluiceCurve(0.f, e));
		CHECK(rka::sluiceCurve(1.f, e) == 1.f, "curve(1, exp=%d) = %.9g", e, rka::sluiceCurve(1.f, e));
	}
	CHECK(rka::sluiceCurve(0.5f, false) == 0.5f, "Lin curve(0.5) = %g", rka::sluiceCurve(0.5f, false));
}

void testExpCurveMatchesDbTaper() {
	double worst = 0.0;
	for (int i = 0; i <= 100000; i++) {
		const float p = float(0.05 + 0.95 * i / 100000.0);
		const double ref = refGain(p, true);
		const double err = std::fabs(rka::sluiceCurve(p, true) - ref) / ref;
		worst = std::max(worst, err);
	}
	CHECK(worst <= 2e-6, "Exp over [0.05, 1]: worst relative error %g", worst);
	int bad = 0;
	for (int i = 0; i <= 100000; i++) {
		const float p = float(0.05 * i / 100000.0);
		const double ref = refGain(p, true);
		if (!nearRel(rka::sluiceCurve(p, true), ref, 2e-6))
			bad++;
	}
	CHECK(bad == 0, "Exp over [0, 0.05] (the ramp): %d points beyond 2e-6 relative", bad);
}

void testExpCurveMonotonicAndContinuous() {
	float prev = rka::sluiceCurve(0.f, true);
	float maxStep = 0.f;
	int falls = 0;
	for (int i = 1; i <= 1000000; i++) {
		const float p = float(i / 1000000.0);
		const float g = rka::sluiceCurve(p, true);
		if (!(g >= prev))
			falls++;
		maxStep = std::max(maxStep, g - prev);
		prev = g;
	}
	CHECK(falls == 0, "Exp curve falls %d times", falls);
	CHECK(maxStep < 1e-4f, "Exp curve's largest step %g", maxStep);
}

void testLinExactOnDc() {
	Patch p;
	p.setIn(0, {3.f});
	p.level[0] = 0.5f;
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.out[0][0] == 1.5f, "3 V at Level 0.5 Lin gives %.9g", r.out[0][0]);
	CHECK(r.channels[0] == 1, "channels %d", r.channels[0]);
}

void testCvAddsToLevel() {
	const float in = 2.5f;
	struct Case { float level, cv; double gain; };
	const Case cases[] = {{0.5f, 5.f, 1.0}, {0.5f, -5.f, 0.0}, {0.5f, 2.f, 0.7}, {0.f, 10.f, 1.0}};
	for (const Case& k : cases) {
		Patch p;
		p.setIn(0, {in});
		p.setCv(0, {k.cv});
		p.level[0] = k.level;
		p.outConn[0] = true;
		rka::SluiceEngine e;
		rka::SluiceResult r;
		run(e, p, SETTLE, r);
		CHECK(near(r.out[0][0], in * k.gain, 1e-6), "Level %g, CV %g V: %.9g, want %.9g", k.level, k.cv,
		      r.out[0][0], in * k.gain);
	}
}

void testUnpatchedCvIsStill() {
	Patch p;
	p.setIn(0, {1.f});
	p.allLevels(0.5f);
	p.allConnected(true);
	p.cvCh[0] = p.cvCh[2] = p.cvCh[3] = 1;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	F f = p.frame();
	float first = 0.f;
	int moved = 0;
	float aMin = 1e9f, aMax = -1e9f;
	for (int i = 0; i < 9600; i++) {  // a whole 5 Hz cycle
		const float lfo = 5.f * float(std::sin(2.0 * M_PI * 5.0 * i / FS));
		p.cv[0][0] = p.cv[2][0] = p.cv[3][0] = lfo;
		e.process(f, FS, r);
		if (i == 0)
			first = r.out[1][0];
		moved += r.out[1][0] != first;
		aMin = std::min(aMin, r.out[0][0]);
		aMax = std::max(aMax, r.out[0][0]);
	}
	CHECK(moved == 0, "B, with no CV, moved on %d samples", moved);
	CHECK(first == 0.5f, "B = %.9g, want 0.5", first);
	// The CV on A really moves A, so B's stillness means something.
	// A's input is 1 V, so its output is its gain: it reaches both 0 and full.
	CHECK(aMin == 0.f, "A's lowest gain %.9g, want exactly 0", aMin);
	CHECK(aMax == 1.f, "A's highest gain %.9g, want exactly 1", aMax);
}

void testRopeCarriesSignalAndChannels() {
	Patch p;
	p.setIn(0, {1.f, -2.f, 3.f});
	p.allLevels(1.f);
	p.allConnected(true);
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	for (int row = 1; row < ROWS; row++) {
		CHECK(r.channels[row] == 3, "row %d: %d channels", row, r.channels[row]);
		for (int c = 0; c < 3; c++)
			CHECK(r.out[row][c] == p.in[0][c], "row %d voice %d: %g, want %g", row, c, r.out[row][c], p.in[0][c]);
	}
}

void testRopeRestartsAtPatchedInput() {
	Patch p;
	p.setIn(0, {1.f});
	p.setIn(2, {3.f});
	p.allLevels(1.f);
	p.allConnected(true);
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.out[1][0] == 1.f, "B = %g, want A's 1 V", r.out[1][0]);
	CHECK(r.out[3][0] == 3.f, "D = %g, want C's 3 V", r.out[3][0]);
}

void testRowAUnpatchedIsSilence() {
	for (int conn = 0; conn < 2; conn++) {
		Patch p;
		p.allLevels(1.f);
		p.allConnected(conn == 1);
		rka::SluiceEngine e;
		rka::SluiceResult r;
		run(e, p, SETTLE, r);
		for (int row = 0; row < ROWS; row++) {
			CHECK(r.channels[row] == 1, "connected=%d row %d: %d channels", conn, row, r.channels[row]);
			CHECK(r.out[row][0] == 0.f, "connected=%d row %d: %g V", conn, row, r.out[row][0]);
		}
	}
}

void testMixAllPatterns() {
	// Different Levels, so the carry going down (which has already been through
	// its own row's gain) is told apart from one scaled by the receiving row's.
	const float inputs[ROWS] = {0.5f, 1.f, 2.f, 4.f};
	const float levels[ROWS] = {0.3f, 0.5f, 0.7f, 0.9f};
	for (int pattern = 0; pattern < 16; pattern++) {
		Patch p;
		for (int row = 0; row < ROWS; row++) {
			p.setIn(row, {inputs[row]});
			p.outConn[row] = (pattern >> row) & 1;
			p.level[row] = levels[row];
		}
		rka::SluiceEngine e;
		rka::SluiceResult r;
		run(e, p, SETTLE, r);
		// A connected output carries its own row and every unconnected row
		// directly above it, each as its own input x its own Level.
		for (int row = 0; row < ROWS; row++) {
			if (!p.outConn[row])
				continue;
			double want = double(inputs[row]) * levels[row];
			for (int up = row - 1; up >= 0 && !p.outConn[up]; up--)
				want += double(inputs[up]) * levels[up];
			CHECK(near(r.out[row][0], want, 1e-6), "pattern %d row %d: %.9g, want %.9g", pattern, row, r.out[row][0],
			      want);
		}
	}
}

// The rope hands down the row above's input, before that row's gain.
void testRopeBelowFullOpen() {
	Patch p;
	p.setIn(0, {2.f});
	const float levels[ROWS] = {0.5f, 0.25f, 0.75f, 0.9f};
	for (int row = 0; row < ROWS; row++)
		p.level[row] = levels[row];
	p.allConnected(true);
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	for (int row = 0; row < ROWS; row++)
		CHECK(near(r.out[row][0], 2.0 * levels[row], 1e-6), "row %d: %.9g, want 2 V x %g", row, r.out[row][0], levels[row]);
	// And mixed down onto D alone: four copies of 2 V, each at its own Level.
	p.allConnected(false);
	p.outConn[3] = true;
	rka::SluiceEngine e2;
	run(e2, p, SETTLE, r);
	const double want = 2.0 * (0.5 + 0.25 + 0.75 + 0.9);
	CHECK(near(r.out[3][0], want, 1e-6), "D = %.9g, want %.9g", r.out[3][0], want);
}

void testRopeCopiesAddUp() {
	Patch p;
	p.setIn(0, {0.75f});
	p.allLevels(1.f);
	p.outConn[3] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.out[3][0] == 3.f, "D = %.9g, want 4 x 0.75 V", r.out[3][0]);
}

void testMonoRowSpreadsIntoPoly() {
	Patch p;
	p.setIn(1, {1.f});
	p.setIn(2, {0.5f, -0.5f});
	p.allLevels(1.f);
	p.outConn[2] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.channels[2] == 2, "C: %d channels", r.channels[2]);
	CHECK(r.out[2][0] == 1.5f && r.out[2][1] == 0.5f, "C = (%g, %g), want (1.5, 0.5)", r.out[2][0], r.out[2][1]);
}

// The mirror case: a mono row whose carry, from above, is wider.
void testMonoRowSpreadsIntoWiderCarry() {
	Patch p;
	p.setIn(0, {0.5f, -0.5f});
	p.setIn(1, {1.f});
	p.allLevels(1.f);
	p.outConn[1] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.channels[1] == 2, "B: %d channels", r.channels[1]);
	CHECK(r.out[1][0] == 1.5f && r.out[1][1] == 0.5f, "B = (%g, %g), want (1.5, 0.5)", r.out[1][0], r.out[1][1]);
}

void testPolyRowsMixVoiceByVoice() {
	Patch p;
	p.setIn(1, {0.5f, 0.25f});
	p.setIn(2, {1.f, 2.f, 3.f, 4.f});
	p.allLevels(1.f);
	p.outConn[2] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	const float want[4] = {1.5f, 2.25f, 3.f, 4.f};
	CHECK(r.channels[2] == 4, "C: %d channels", r.channels[2]);
	for (int c = 0; c < 4; c++)
		CHECK(r.out[2][c] == want[c], "C voice %d: %g, want %g", c + 1, r.out[2][c], want[c]);
}

// A 4-channel input on A at Level 0.5 Lin, with the CV given; each voice
// against in x clamp(0.5 + cv(c) / 10).
void checkCvVoices(const char* name, std::initializer_list<float> cv, const double* cvPerVoice) {
	Patch p;
	p.setIn(0, {1.f, 2.f, -3.f, 4.f});
	p.setCv(0, cv);
	p.level[0] = 0.5f;
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.channels[0] == 4, "%s: %d channels", name, r.channels[0]);
	for (int c = 0; c < 4; c++) {
		const double want = p.in[0][c] * clamp01(0.5 + cvPerVoice[c] / 10.0);
		CHECK(near(r.out[0][c], want, 1e-6), "%s voice %d: %.9g, want %.9g", name, c + 1, r.out[0][c], want);
	}
}

void testMonoCvDrivesEveryVoice() {
	const double v[4] = {2, 2, 2, 2};
	checkCvVoices("mono CV", {2.f}, v);
}

void testPolyCvMapsVoiceByVoice() {
	const double v[4] = {-5, 0, 2, 5};
	checkCvVoices("poly CV", {-5.f, 0.f, 2.f, 5.f}, v);
}

void testShortCvGivesZeroBeyond() {
	const double v[4] = {2, 5, 0, 0};
	checkCvVoices("2-channel CV", {2.f, 5.f}, v);
}

void testPolyCvMonoInputStaysMono() {
	Patch p;
	p.setIn(0, {2.f});
	p.setCv(0, {2.f, 5.f, -5.f, 0.f});
	p.level[0] = 0.5f;
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.channels[0] == 1, "%d channels", r.channels[0]);
	CHECK(near(r.out[0][0], 2.0 * 0.7, 1e-6), "%.9g, want %.9g", r.out[0][0], 2.0 * 0.7);
}

void testSixteenVoices() {
	for (int exp = 0; exp < 2; exp++) {
		Patch p;
		p.exp = exp;
		for (int row = 0; row < ROWS; row++) {
			p.inCh[row] = p.cvCh[row] = 16;
			p.level[row] = 0.2f + 0.2f * row;
			p.outConn[row] = true;
			for (int c = 0; c < 16; c++) {
				p.in[row][c] = float(c + 1 + 16 * row) / 8.f * (c % 2 ? -1.f : 1.f);
				p.cv[row][c] = -8.f + float(c);
			}
		}
		rka::SluiceEngine e;
		rka::SluiceResult r;
		run(e, p, SETTLE, r);
		int bad = 0;
		for (int row = 0; row < ROWS; row++) {
			CHECK(r.channels[row] == 16, "row %d: %d channels", row, r.channels[row]);
			for (int c = 0; c < 16; c++) {
				const double pos = clamp01(double(p.level[row]) + p.cv[row][c] / 10.0);
				const double want = p.in[row][c] * refGain(pos, exp);
				if (!near(r.out[row][c], want, 1e-6 + 2e-6 * std::fabs(want))) {
					bad++;
					std::printf("  exp=%d row %d voice %d: %.9g, want %.9g\n", exp, row, c + 1, r.out[row][c], want);
				}
			}
		}
		CHECK(bad == 0, "exp=%d: %d voices wrong", exp, bad);
	}
}

void testLightIsMaxP() {
	Patch p;
	p.exp = true;
	p.setIn(0, {1.f, 1.f});
	p.setCv(0, {0.f, 2.f});
	p.level[0] = 0.5f;
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(near(r.light[0], 0.7, 1e-6), "light %.9g, want p = 0.7", r.light[0]);
}

void testLevelSmoothing() {
	Patch p;
	p.setIn(0, {1.f});
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.out[0][0] == 0.f, "Level 0 gives %g", r.out[0][0]);
	p.level[0] = 1.f;
	const F f = p.frame();
	e.process(f, FS, r);
	CHECK(r.out[0][0] < 0.01f, "first sample after the step: %g", r.out[0][0]);
	CHECK(r.out[0][0] > 0.f, "first sample after the step did not move: %g", r.out[0][0]);
	const int n50 = int(0.050f * FS), n200 = int(0.200f * FS);
	for (int i = 1; i < n200; i++) {
		e.process(f, FS, r);
		if (i == n50 - 1)
			CHECK(std::fabs(r.out[0][0] - 1.f) < 0.01f, "at 50 ms: %.9g", r.out[0][0]);
	}
	CHECK(r.out[0][0] == 1.f, "at 200 ms: %.9g, not exactly 1", r.out[0][0]);
}

void testCvNotSmoothed() {
	Patch p;
	p.setIn(0, {2.f});
	p.setCv(0, {-5.f});
	p.level[0] = 0.5f;
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.out[0][0] == 0.f, "CV -5 V: %g", r.out[0][0]);
	p.cv[0][0] = 5.f;
	run(e, p, 1, r);
	CHECK(r.out[0][0] == 2.f, "CV +5 V, same sample: %g, want 2", r.out[0][0]);
}

void testStartsAtSavedLevel() {
	Patch p;
	p.setIn(0, {2.5f});
	p.level[0] = 0.8f;
	p.outConn[0] = true;
	const double want = 2.5 * 0.8;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, 1, r);
	CHECK(near(r.out[0][0], want, 1e-6), "fresh engine's first sample: %.9g, want %.9g", r.out[0][0], want);

	// After reset(), from a different settled Level.
	Patch q = p;
	q.level[0] = 0.1f;
	run(e, q, 4800, r);
	e.reset();
	run(e, p, 1, r);
	CHECK(near(r.out[0][0], want, 1e-6), "first sample after reset(): %.9g, want %.9g", r.out[0][0], want);
}

void testCurveSwitchImmediate() {
	Patch p;
	p.setIn(0, {1.f});
	p.level[0] = 0.5f;
	p.outConn[0] = true;
	rka::SluiceEngine e;
	rka::SluiceResult r;
	run(e, p, SETTLE, r);
	CHECK(r.out[0][0] == 0.5f, "Lin: %g", r.out[0][0]);
	p.exp = true;
	run(e, p, 1, r);
	const double want = refGain(0.5, true);
	CHECK(nearRel(r.out[0][0], want, 2e-6), "Exp on the next sample: %.9g, want %.9g", r.out[0][0], want);
	p.exp = false;
	run(e, p, 1, r);
	CHECK(r.out[0][0] == 0.5f, "back to Lin on the next sample: %g", r.out[0][0]);
}

} // namespace

int main() {
	testCurveEndpoints();
	testExpCurveMatchesDbTaper();
	testExpCurveMonotonicAndContinuous();
	testLinExactOnDc();
	testCvAddsToLevel();
	testUnpatchedCvIsStill();
	testRopeCarriesSignalAndChannels();
	testRopeRestartsAtPatchedInput();
	testRowAUnpatchedIsSilence();
	testMixAllPatterns();
	testRopeBelowFullOpen();
	testRopeCopiesAddUp();
	testMonoRowSpreadsIntoPoly();
	testMonoRowSpreadsIntoWiderCarry();
	testPolyRowsMixVoiceByVoice();
	testMonoCvDrivesEveryVoice();
	testPolyCvMapsVoiceByVoice();
	testShortCvGivesZeroBeyond();
	testPolyCvMonoInputStaysMono();
	testSixteenVoices();
	testLightIsMaxP();
	testLevelSmoothing();
	testCvNotSmoothed();
	testStartsAtSavedLevel();
	testCurveSwitchImmediate();
	return rkatest::finish("sluice test_engine");
}

// FathomEngine's outputs as the module writes them, in volts: control steps
// must not step L and R (issue #87), and L and R stay within the documented
// +/-5 V, held and after CV steps (issue #88). Run with `make test`.
#include "FathomEngine.hpp"
#include "testing.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>

using F = rka::FathomFrame;

namespace {

const float FS = 48000.f;

// The module's defaults (Fathom.cpp's configParam values).
void defaults(F& f) {
	float* p = f.params;
	p[F::PITCH] = 0.f;
	p[F::SUB] = 0.5f;
	p[F::DETUNE] = 0.25f;
	p[F::SPREAD] = 0.6f;
	p[F::DRIFT] = 0.25f;
	p[F::CUTOFF] = std::log2(1200.f / 20.f);
	p[F::DETUNE_ATT] = p[F::DRIFT_ATT] = p[F::CUTOFF_ATT] = 0.f;
	p[F::RES] = 0.2f;
	p[F::DRIVE] = 0.25f;
	p[F::RES_ATT] = p[F::DRIVE_ATT] = 0.f;
	for (float& v : f.inputs)
		v = 0.f;
	for (bool& c : f.outputConnected)
		c = true;
	f.subOctave = 0;
	f.filterType = 0;
	f.subThroughFilter = false;
}

// Issue #87's measurement: the defaults for 3 s, the largest
// sample-to-sample step of L and R over the next 1 s (steady), then `change`
// in one sample and the largest step over the 100 ms after it. The change
// must step the output no more than the drone itself does.
template <typename Change>
void checkStep(const char* name, bool startThrough, Change change) {
	rka::FathomEngine e;
	F f;
	defaults(f);
	f.subThroughFilter = startThrough;
	float out[F::OUTPUTS_LEN], prev[2] = {0.f, 0.f};
	float steady = 0.f, during = 0.f;
	const long start = long(4 * FS), end = start + long(0.1 * FS);
	for (long i = 0; i < end; i++) {
		if (i == start)
			change(f);
		e.process(f, FS, out);
		for (int c = 0; c < 2; c++) {
			const float jump = std::fabs(out[c] - prev[c]);
			prev[c] = out[c];
			if (i >= long(3 * FS) && i < start)
				steady = std::fmax(steady, jump);
			else if (i >= start)
				during = std::fmax(during, jump);
		}
	}
	CHECK(steady > 0.01f, "%s: no signal (steady step %f V)", name, steady);
	CHECK(during < 1.5f * steady, "%s stepped L/R by %.3f V (steady %.3f V, %.1fx)", name, during, steady, during / steady);
	std::printf("%-30s largest step %.3f V, steady %.3f V (%.2fx)\n", name, during, steady, during / steady);
}

void testSubAndRoutingAreSmoothed() {
	checkStep("SUB 0.5 to 0", false, [](F& f) { f.params[F::SUB] = 0.f; });
	checkStep("SUB 0.5 to 1", false, [](F& f) { f.params[F::SUB] = 1.f; });
	checkStep("Sub through filter off to on", false, [](F& f) { f.subThroughFilter = true; });
	checkStep("Sub through filter on to off", true, [](F& f) { f.subThroughFilter = false; });
}

struct Lcg {
	uint32_t s;
	explicit Lcg(uint32_t seed) : s(seed) {}
	float uniform() {  // [0, 1)
		s = s * 1664525u + 1013904223u;
		return float(s >> 8) / 16777216.f;
	}
};

// Largest |L| or |R| over `seconds` after the first `settle`.
template <typename Change>
float peak(F& f, float fs, double settle, double seconds, Change change) {
	rka::FathomEngine e;
	float out[F::OUTPUTS_LEN], pk = 0.f;
	for (long i = 0; i < long((settle + seconds) * fs); i++) {
		change(f, i);
		e.process(f, fs, out);
		if (i >= long(settle * fs))
			pk = std::fmax(pk, std::fmax(std::fabs(out[F::LEFT]), std::fabs(out[F::RIGHT])));
	}
	return pk;
}

// The settings issue #88 found peaking at 5.28 V held: SUB, DETUNE and
// DRIVE up, the clip's asymmetric output pushed past 5 V by the DC blocker.
void testHeldPeak() {
	for (int type : {0, 1, 3}) {
		F f;
		defaults(f);
		f.filterType = type;
		f.params[F::RES] = 0.5f;
		f.params[F::DRIVE] = f.params[F::SUB] = f.params[F::DETUNE] = 1.f;
		f.params[F::PITCH] = -12.f;
		f.params[F::CUTOFF] = std::log2(10200.f / 20.f);
		const float pk = peak(f, FS, 5.0, 10.0, [](F&, long) {});
		CHECK(pk <= 5.f, "held, filter type %d: L/R peak %.3f V, over 5 V", type, pk);
		std::printf("held, filter type %d: peak %.3f V\n", type, pk);
	}
}

// Issue #88's fuzz, shortened: every knob, attenuverter and menu setting
// random, every CV redrawn from +/-10 V (V/OCT +/-2 V) every 0.25 s, at 48
// and 44.1 kHz. Steps of the DC blocker's input reached 7.2 V.
void testCvStepPeak() {
	Lcg rng(777);
	float worst = 0.f;
	int worstTrial = -1;
	for (int t = 0; t < 100; t++) {
		const float fs = t % 2 ? 44100.f : 48000.f;
		F f;
		defaults(f);
		for (float& p : f.params)
			p = rng.uniform();
		f.params[F::PITCH] = -24.f + 48.f * rng.uniform();
		f.params[F::CUTOFF] = 10.f * rng.uniform();
		f.filterType = int(4.f * rng.uniform());
		f.subOctave = rng.uniform() < 0.5f ? 0 : 1;
		f.subThroughFilter = rng.uniform() < 0.5f;
		const long every = long(fs / 4);
		const float pk = peak(f, fs, 0.0, 3.0, [&](F& g, long i) {
			if (i % every == 0)
				for (int k = 0; k < F::INPUTS_LEN; k++)
					g.inputs[k] = (k == F::VOCT ? 0.2f : 1.f) * (-10.f + 20.f * rng.uniform());
		});
		if (pk > worst) {
			worst = pk;
			worstTrial = t;
		}
	}
	CHECK(worst <= 5.f, "random CV steps: L/R peak %.3f V (trial %d), over 5 V", worst, worstTrial);
	std::printf("random CV steps: peak %.3f V (trial %d)\n", worst, worstTrial);
}

// The limit is the identity below the knee, so it leaves the normal sound
// alone; above, it rises monotonically to at most 1 and is continuous at the
// knee; NaN passes through, as it did before it.
void testOutputLimit() {
	const auto outputLimit = rka::OutputStage::limit;
	const float OUT_KNEE = rka::OutputStage::KNEE;
	bool identity = true, monotonic = true, bounded = true;
	float prev = 0.f;
	for (int i = 0; i <= 200000; i++) {
		const float x = i * 1e-4f - 10.f;
		const float y = outputLimit(x);
		if (std::fabs(x) <= OUT_KNEE)
			identity = identity && y == x;
		if (i > 0)
			monotonic = monotonic && y >= prev;
		bounded = bounded && std::fabs(y) <= 1.f;
		prev = y;
	}
	CHECK(identity, "the output limit changes the signal below the knee");
	CHECK(monotonic, "the output limit is not monotonic");
	CHECK(bounded, "the output limit passes +/-1");
	const float above = outputLimit(std::nextafter(OUT_KNEE, 2.f));
	CHECK(above - OUT_KNEE < 1e-6f, "the output limit jumps at the knee: %g", above - OUT_KNEE);
	CHECK(outputLimit(1e30f) <= 1.f && outputLimit(-1e30f) >= -1.f, "the output limit passes +/-1 far out");
	CHECK(std::isnan(outputLimit(std::nanf(""))), "the output limit eats NaN");
}

// At the defaults the output stays below the knee once started, so the
// limit leaves it bit for bit as it was before issue #88. (A cold start's
// first 20 ms rise to 4.80 V, through the DC blocker settling, and lose up
// to 0.5 mV there.)
void testDefaultsBelowKnee() {
	F f;
	defaults(f);
	const float pk = peak(f, FS, 1.0, 10.0, [](F&, long) {});
	CHECK(pk < 5.f * rka::OutputStage::KNEE, "defaults peak %.3f V, into the output limit's knee (%.2f V)", pk, 5.f * rka::OutputStage::KNEE);
	std::printf("defaults: peak %.3f V, knee %.2f V\n", pk, 5.f * rka::OutputStage::KNEE);
}

} // namespace

int main() {
	testOutputLimit();
	testDefaultsBelowKnee();
	testSubAndRoutingAreSmoothed();
	testHeldPeak();
	testCvStepPeak();
	return rkatest::finish("test_output");
}

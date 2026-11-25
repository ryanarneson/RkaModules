// FathomEngine's outputs as the module writes them, in volts: control steps
// must not step L and R (issue #87). Run with `make test`.
#include "FathomEngine.hpp"
#include "testing.hpp"
#include <cmath>
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

} // namespace

int main() {
	testSubAndRoutingAreSmoothed();
	return rkatest::finish("test_output");
}

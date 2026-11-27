// Doldrums' aliasing (docs/DSP-ALIASING.md; TESTING.md rule 16; the design's
// "Aliasing"), measured with rkatest::AliasMeter (src/testing_alias.hpp) at
// 44.1 and 48 kHz. The method has no generated discontinuities and no
// variable-rate read: grains are taken at whole samples at the tape's own
// rate. What it does make is a skirt of noise round a held tone, as wide as
// the grain's windows make it; what Shift adds is a move of the loudnesses
// along the bins, with what would pass the top of the band dropped; and the
// one nonlinearity is the soft bound at the output (SoftBound.hpp, the
// identity up to 8 V). So the test holds a sine at 0.1, 0.25 and 0.4 of the
// rate, at each grain size, with Shift at 0, +12 and +24, and bounds
// everything outside the tone's skirt; a tone shifted past the top must be
// gone, with nothing folded back. Twice: at 1 V, where the soft bound never
// acts (the method alone), and at full scale, 5 V, where it does (a held
// sine comes out as a narrow band of noise, whose peaks pass 8 V). `test_alias
// --table` prints every measurement instead of checking it.
#include "doldrums/DoldrumsEngine.hpp"
#include "testing.hpp"
#include "testing_alias.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using namespace rka::doldrums;
using rkatest::AliasMeter;
typedef DoldrumsFrame F;

bool table = false;
const double kPi = 3.14159265358979323846;
const double RATES[2] = {44100.0, 48000.0};
const double TONES[3] = {0.1, 0.25, 0.4};
const float SHIFTS[3] = {0.f, 12.f, 24.f};
const size_t N = AliasMeter::MAX_N;   // samples a meter reads
const int WINDOWS = 4;                // meters in a row; the worst counts

// The skirt. A held tone's loudness lies in the Hann lobe of the grain's
// transform (+-2 of the grain's bins, rate / grain Hz each); the sine window
// that lays each random-phase grain down spreads it, falling 12 dB an
// octave (measured 2026-11-26 at 1 V, the strongest component outside the
// skirt, worst case: -43 dB at 8 grain bins, -54 at 16, -64 at 32, -75 at
// 64, -87 at 128). Shift up by r spreads it r-fold (Tables.hpp: a gather).
// The skirt is pinned at 32 of the grain's bins either side of the tone,
// times r: 172 Hz at Short and 43 Hz at Long at 44.1 kHz, unshifted.
const double SKIRT_BINS = 32.0;

double skirtHz(double fs, int size, float semis) {
	return SKIRT_BINS * fs / grainFrames(GrainSize(size), float(fs)) * std::max(1.0, double(shiftRatio(semis)));
}

struct Held {
	double landsHz;    // where the tone should be heard
	double otherDb;    // the strongest component outside the skirt, dB re the tone put in (the worst window)
	double otherHz;    // and where
	double levelDb;    // the output's RMS, dB re the input's
};

// A sine of `amp` volts at `ratio` of the rate recorded for a second, then
// frozen at Position fully right (nothing to travel through: held). Shift
// `semis` from the start, Mix 1 (the wet sound alone). Measured from three
// grains after the freeze, WINDOWS meters in a row.
Held held(double fs, int size, double ratio, float semis, double amp) {
	std::unique_ptr<DoldrumsEngine> e(new DoldrumsEngine);
	CHECK(e->allocate(float(fs)), "allocate(%g)", fs);
	e->setSampleRate(float(fs));
	e->seed(7);
	F f;
	f.grainSize = size;
	f.params[F::MIX] = 1.f;        // all wet: the wet sound is what is measured (the default moved to 0.5 at 2.0.49)
	f.params[F::POSITION] = 1.f;   // held at the end: Position at now, as these tests were written (the default moved to 0.9 at 2.0.46; its own effect is test_engine's default_position_freezes_a_stretch)
	f.params[F::SHIFT] = semis;
	DoldrumsResult r;
	const double hz = ratio * fs;
	const int grain = grainFrames(GrainSize(size), float(fs));
	const long record = long(fs), settle = 3L * grain + e->latencyFrames();
	const long total = record + settle + long(WINDOWS * N);
	std::vector<float> out;
	out.reserve(size_t(WINDOWS * N));
	for (long t = 0; t < total; t++) {
		f.inL = float(amp * std::sin(2.0 * kPi * hz * double(t) / fs));
		if (t == record) f.params[F::FREEZE] = 1.f;
		e->process(f, r);
		if (t >= record + settle) out.push_back(r.outL);
	}
	Held h;
	h.landsHz = hz * double(shiftRatio(semis));
	std::vector<double> want;
	if (h.landsHz <= fs / 2.0) want.push_back(h.landsHz);
	h.otherDb = -999.0;
	h.otherHz = 0.0;
	for (int w = 0; w < WINDOWS; w++) {
		const AliasMeter m(out.data() + size_t(w) * N, N, fs);
		const AliasMeter::Other o = m.strongestOther(want, skirtHz(fs, size, semis));
		const double d = AliasMeter::db(o.amp / amp);
		if (d > h.otherDb) {
			h.otherDb = d;
			h.otherHz = o.hz;
		}
	}
	double sq = 0.0;
	for (float x : out) sq += double(x) * x;
	h.levelDb = 10.0 * std::log10(std::max(1e-30, sq / double(out.size())) / (amp * amp / 2.0));
	return h;
}

const char* sizeName(int s) { return s == 0 ? "Short" : s == 1 ? "Medium" : "Long"; }

// ---- The bounds (measured 2026-11-26, 54 cases at each level).

// The method alone (1 V, the soft bound idle): everything outside the skirt
// at least 60 dB under the tone (DSP-ALIASING.md: 60 to 70 is clean).
// Measured: worst -64.1 dB (48 kHz, Long, 0.1 of the rate, Shift 0: the
// skirt's own tail); with Shift +12 or +24 -78 dB or less; a tone shifted
// past the top -143 dB or less (nothing folded back).
const double METHOD_DB = -60.0;
// Full scale (5 V): the soft bound's products, pinned at the measured worst
// plus 3 dB. Measured: worst -37.0 dB (44.1 kHz, Medium, 0.1 of the rate,
// Shift +24, the tone at 0.4 of the rate: its third harmonic, 1.2 of the
// rate, folded to 0.2); unshifted tones at 0.4 of the rate -40.1 to -58.2,
// tones at 0.25 of the rate -66 dB or less.
const double FULL_SCALE_DB = -34.0;
// A tone that lands inside the band keeps its level (measured within
// 0.8 dB; a held sine wavers by up to about 2 dB over 10 s at Long: test_grain).
const double LEVEL_TOL_DB = 3.0;
// A tone that lands past the top is gone: what is left of it, re the input
// (measured -130 dB or less).
const double GONE_DB = -60.0;

void held_tones(double amp, double bound, const char* what) {
	for (double fs : RATES)
		for (int size = 0; size < 3; size++)
			for (double ratio : TONES)
				for (float semis : SHIFTS) {
					const Held h = held(fs, size, ratio, semis, amp);
					const double nyq = fs / 2.0, skirt = skirtHz(fs, size, semis);
					// Within a skirt of Nyquist the tone is part in, part out: its
					// level is not checked, only what lies outside the skirt.
					const bool in = h.landsHz + skirt < nyq, out = h.landsHz - skirt > nyq;
					if (table) {
						std::printf("%.0f V fs %5.0f %-6s tone %.2f shift %+3.0f: lands %.3f x Nyquist  other %7.1f dB at %7.0f Hz  level %8.2f dB%s\n",
						            amp, fs, sizeName(size), ratio, semis, h.landsHz / nyq, h.otherDb, h.otherHz, h.levelDb,
						            in ? "" : out ? "  (past the top)" : "  (at the top)");
						continue;
					}
					CHECK(h.otherDb <= bound,
					      "%s: fs %.0f, %s, a %.0f V tone at %.2f of the rate, Shift %+.0f (lands at %.3f x Nyquist): the strongest component outside the skirt is %.1f dB re the tone at %.0f Hz (bound %.1f)",
					      what, fs, sizeName(size), amp, ratio, semis, h.landsHz / nyq, h.otherDb, h.otherHz, bound);
					if (in)
						CHECK(std::fabs(h.levelDb) <= LEVEL_TOL_DB,
						      "%s: fs %.0f, %s, tone %.2f, Shift %+.0f: the held tone's level is %.2f dB re the input's", what, fs,
						      sizeName(size), ratio, semis, h.levelDb);
					if (out)
						CHECK(h.levelDb <= GONE_DB,
						      "%s: fs %.0f, %s, tone %.2f, Shift %+.0f: shifted past the top, %.1f dB of it is left (bound %.1f)", what,
						      fs, sizeName(size), ratio, semis, h.levelDb, GONE_DB);
				}
}

} // namespace

int main(int argc, char** argv) {
	table = argc > 1 && std::strcmp(argv[1], "--table") == 0;
	held_tones(1.0, METHOD_DB, "the method (1 V)");
	held_tones(5.0, FULL_SCALE_DB, "full scale (5 V)");
	return table ? 0 : rkatest::finish("doldrums/test_alias");
}

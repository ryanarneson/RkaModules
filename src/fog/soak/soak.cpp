// Fog soak: it never runs away. For every mode, at v = 1 (T60 80 s) and three
// corners of Size, Damp and Drift (all minimum; all maximum; Size maximum
// with Damp minimum and Drift maximum): 10 minutes of white noise at +/-5 V,
// then 3 minutes of silence. Mix is 1 (the wet alone), so every sample of L
// and R must be a number within the limiter's bound (10.5 V). In the silence
// the level must fall by at least the T60 law: one-second RMS windows every
// 20 s, on L and R each, are under that side's level at the start of the
// silence times 10^(-3 t / (1.15 T60)) (the spec's +/-15 % on T60, the
// slower end), floored where a float stops resolving it. Not part of `make test` (it takes minutes): `make fog-soak`.
#include "FogEngine.hpp"
#include "Voicing.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

using rka::FogEngine;
using rka::FogFrame;
using rka::FogResult;

namespace {

uint32_t rng = 7u;
float uniform() {
	rng = rng * 1664525u + 1013904223u;
	return (float) (rng >> 8) / 8388608.f - 1.f;
}

struct Corner {
	const char* name;
	float size, damp, drift;
};

} // namespace

int main() {
	const float FS = 48000.f;
	const Corner corners[3] = {
		{"min", 0.f, 0.f, 0.f},
		{"max", 1.f, 1.f, 1.f},
		{"size max, damp min, drift max", 1.f, 0.f, 1.f},
	};
	const double t60 = 0.4 * std::pow(200.0, 1.0); // v = 1
	const int noiseN = (int) (600 * FS), silenceN = (int) (180 * FS), win = (int) FS;
	int failures = 0;
	const auto t0 = std::chrono::steady_clock::now();
	static FogEngine eng; // large: keep it off the stack
	for (int mode = 0; mode < rka::fog::MODES_LEN; mode++) {
		for (const Corner& c : corners) {
			float vL = 0.f;
			FogFrame f;
			f.inL.v = &vL;
			f.inL.channels = 1;
			f.params[FogFrame::VISIBILITY] = 1.f;
			f.params[FogFrame::SIZE] = c.size;
			f.params[FogFrame::DAMP] = c.damp;
			f.params[FogFrame::DRIFT] = c.drift;
			f.params[FogFrame::BLOOM] = 0.f;
			f.params[FogFrame::MIX] = 1.f;
			f.mode = mode;
			eng.setSampleRate(FS);
			eng.reset();
			double peak = 0, startRms[2] = {0, 0}, worstRatio = 0;
			bool ok = true;
			double acc[2] = {0, 0};
			int accN = 0;
			for (long i = 0; i < noiseN + silenceN; i++) {
				const bool noise = i < noiseN;
				vL = noise ? 5.f * uniform() : 0.f;
				FogResult o;
				eng.process(f, o);
				// Each channel on its own, NaN-safe (fmax would drop a NaN, and
				// x == x stays a NaN test under -funsafe-math-optimizations).
				const float out[2] = {o.outL, o.outR};
				for (int ch = 0; ch < 2; ch++) {
					const double x = out[ch];
					const double a = std::fabs(x);
					if (!(x == x) || !(a <= 10.5)) {
						if (ok)
							std::printf("FAIL mode %d %s: |out %c| %g at sample %ld\n", mode, c.name, "LR"[ch], a, i);
						ok = false;
					}
					else
						peak = a > peak ? a : peak;
					acc[ch] += x * x;
				}
				accN++;
				if (!noise) {
					const long s = i - noiseN;
					// A 1 s window at the start, then one every 20 s.
					if ((s + 1) % win == 0 && accN >= win && ((s + 1) / win == 1 || ((s + 1) / win) % 20 == 0)) {
						const double t = (double) (s + 1) / FS - 0.5; // the window's centre
						for (int ch = 0; ch < 2; ch++) {
							const double r = std::sqrt(acc[ch] / accN);
							if ((s + 1) / win == 1) {
								startRms[ch] = r;
								continue;
							}
							const double allowed = startRms[ch] * std::pow(10.0, -3.0 * (t - 0.5) / (1.15 * t60));
							// Below 1e-9 V the float state has no meaning. Written so
							// a NaN fails.
							if (!(r <= allowed || r <= 1e-9)) {
								std::printf("FAIL mode %d %s: RMS %c %g V at %.0f s into silence, law allows %g\n", mode,
								            c.name, "LR"[ch], r, t, allowed);
								ok = false;
							}
							const double ratio = allowed > 0 ? r / allowed : 0;
							worstRatio = ratio > worstRatio ? ratio : worstRatio;
						}
					}
					if ((s + 1) % win == 0) {
						acc[0] = acc[1] = 0;
						accN = 0;
					}
				}
				else if ((i + 1) % win == 0) {
					acc[0] = acc[1] = 0;
					accN = 0;
				}
			}
			if (!ok)
				failures++;
			std::printf("mode %d, %-30s peak %.3f V, RMS at silence start %.3f / %.3f V (L / R), worst RMS/law %.3g  %s\n", mode, c.name, peak, startRms[0], startRms[1], worstRatio, ok ? "ok" : "FAIL");
			std::fflush(stdout);
		}
	}
	const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("fog soak: %s in %.0f s\n", failures ? "FAILED" : "PASS", secs);
	return failures ? 1 : 0;
}

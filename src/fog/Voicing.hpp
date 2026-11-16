#pragma once

// Fog's modes: each is a voicing of the same network (spec, "Modes").
// Only HAAR is tuned so far; FRET, SMOKE and ICE copy it until their own
// voicings are set (plan Task 7). Rack-free, C++11.
#include "FastMath.hpp"

namespace rka {
namespace fog {

enum Mode { HAAR, FRET, SMOKE, ICE, MODES_LEN };

struct Voicing {
	int lines;          // 8 or 16
	float baseMs[16];   // line lengths at Size noon (scale 1)
	int diffuseStages;  // input diffuser stages, up to 4
	float diffuseMs[4]; // each stage's span
	float dampBias;     // built-in darkening (0 = HAAR)
	float bloomBias;    // slower build-up (0 = HAAR)
	float loopDiffuse;  // diffusion inside the loop (0 = none; SMOKE)
	float shimmer;      // octave-up amount in the loop (0 = none; ICE)
	float brightness;   // brighter damping (0 = HAAR)
	float wetMakeup;    // scale on the engine's wet make-up (1 = HAAR)
};

// HAAR: 16 lines, long and spread over a little under 1.5 octaves
// (53-156 ms at Size noon, mean 101 ms), chosen to share no common factor
// in samples, and four diffusion stages. The base lengths span 0.33x-3x
// with Size.
#define RKA_FOG_HAAR                                                                              \
	{                                                                                              \
		16, {53.1f, 58.7f, 64.3f, 70.9f, 76.3f, 83.1f, 88.9f, 95.3f, 102.7f, 109.1f, 116.9f, 123.7f, \
		     131.3f, 139.7f, 147.1f, 155.9f},                                                      \
		    4, {13.f, 29.f, 47.f, 71.f}, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f                               \
	}

inline const Voicing& voicing(Mode m) {
	// Constant-initialised: read-only data, no guard, no writable global.
	static const Voicing table[MODES_LEN] = {RKA_FOG_HAAR, RKA_FOG_HAAR, RKA_FOG_HAAR, RKA_FOG_HAAR};
	return table[(m >= HAAR && m < MODES_LEN) ? m : HAAR];
}

#undef RKA_FOG_HAAR

// The spec's decay law: T60 = 0.4 s x 200^v (v in [0, 1]).
inline float fogT60(float v) {
	return 0.4f * fastExp2(v * 7.64385618977472f); // log2(200)
}

} // namespace fog
} // namespace rka

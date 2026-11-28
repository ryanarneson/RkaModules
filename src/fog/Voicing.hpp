#pragma once

// Fog's modes: each is a voicing of the same network (spec, "Modes").
// Rack-free, C++11.
#include "FastMath.hpp"

namespace rka {
namespace fog {

enum Mode { HAAR, FRET, SMOKE, ICE, MODES_LEN };

// The feed's tone stage (the engine, before Bloom): none, an RBJ peaking
// filter, or an RBJ high shelf.
enum ToneKind { TONE_NONE, TONE_PEAK, TONE_HIGH_SHELF };

struct Voicing {
	int lines;          // 8 or 16
	float baseMs[16];   // line lengths at Size noon (scale 1)
	int diffuseStages;  // input diffuser stages, up to 4
	float diffuseMs[4]; // each stage's span
	float dampBias;     // built-in darkening (0 = HAAR): Damp d acts as bias + (1 - bias) d
	float bloomBias;    // slower build-up (0 = HAAR): Bloom's time constant x (1 + bias)
	float loopDiffuse;  // in-loop allpass coefficient (0 = none; SMOKE)
	float loopMs[8];    // each in-loop allpass's delay (lines 0-7)
	float shimmer;      // octave-up in the loop (0 = none; ICE): what the shifter's
	                    // crossfade takes from the note a pass, as a fraction of the
	                    // loop's own loss a pass (Network.hpp)
	float shimmerOut;   // the feed-forward octave's level on the wet (0 = none; ICE),
	                    // added as shimmerOut x the octave of each wet side
	float brightness;   // lighter damping (0 = HAAR): Damp d acts as (1 - brightness) d
	int toneKind;       // the feed's tone stage
	float toneHz, toneDb, toneQ;
	float early;        // early reflections (0 = none; SMOKE): the diffuser's own
	                    // build-up added to the wet ahead of the lines' first
	                    // echoes, at this level against them (Network.hpp)
	float wetMakeup;    // the wet's make-up gain at short decays, and
	float wetBuild;     // how it grows with the loop gain: wetMakeup x (1 +
	                    // wetBuild g^2) levels full wet to the dry, so every
	                    // mode follows the same level law (Network.hpp, "Wet
	                    // level"; test_engine's wet_level_law). Both are fitted
	                    // to measurement by `make fog-calibrate`: run it after
	                    // changing anything above.
};

inline const Voicing& voicing(Mode m) {
	// Constant-initialised: read-only data, no guard, no writable global.
	static const Voicing table[MODES_LEN] = {
		// HAAR: 16 lines, long and spread over a little under 1.5 octaves
		// (53-156 ms at Size noon, mean 101 ms), chosen to share no common
		// factor in samples, and four diffusion stages. Signed off by ear.
		{16,
		 {53.1f, 58.7f, 64.3f, 70.9f, 76.3f, 83.1f, 88.9f, 95.3f, 102.7f, 109.1f, 116.9f, 123.7f, 131.3f, 139.7f, 147.1f, 155.9f},
		 4, {13.f, 29.f, 47.f, 71.f},
		 0.f, 0.f,
		 0.f, {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
		 0.f, 0.f, 0.f,
		 TONE_NONE, 0.f, 0.f, 0.f,
		 0.4f,
		 1.001f, 0.425f},
		// FRET (LV-426): dark and slow to build. 16 lines about 20 % longer
		// than HAAR's (61-177 ms, mean 119 ms), the four diffusion stages
		// longer still (17-89 ms), so the onset smears and the space rises
		// behind the sound. Damp acts from a third of the way up (noon is
		// HAAR's 0.67: highs die 3x faster), Bloom's swell takes twice as
		// long, and a broad +4 dB at 300 Hz on the feed gives the low-mid
		// weight.
		{16,
		 {61.3f, 67.9f, 74.7f, 81.1f, 88.3f, 95.9f, 103.1f, 110.9f, 118.7f, 126.1f, 134.3f, 142.9f, 151.1f, 159.7f, 168.1f, 176.9f},
		 4, {17.f, 37.f, 59.f, 89.f},
		 0.35f, 1.f,
		 0.f, {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
		 0.f, 0.f, 0.f,
		 TONE_PEAK, 300.f, 4.f, 0.7f,
		 1.f,
		 0.966f, 0.696f},
		// SMOKE (Greyhole, Supermassive's echo modes): 8 lines clustered within
		// +/-10 % of 153 ms, so each round of the loop comes back as one
		// repeat (about 170 ms apart at noon, 0.5 s at Size 1). Only two short
		// input stages, so the first repeat is distinct; an allpass after each
		// line's read (coefficient 0.6, 6-22 ms) blurs every later repeat
		// further into the wash.
		{8,
		 {137.9f, 142.3f, 146.9f, 151.3f, 155.9f, 160.1f, 164.7f, 169.3f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
		 2, {3.f, 7.f, 0.f, 0.f},
		 0.f, 0.f,
		 0.6f, {6.1f, 8.3f, 10.7f, 12.9f, 15.1f, 17.3f, 19.7f, 21.9f},
		 0.f, 0.f, 0.f,
		 TONE_NONE, 0.f, 0.f, 0.f,
		 0.f,
		 1.148f, 0.244f},
		// ICE: sparse and glassy. 16 lines a little shorter than HAAR's
		// (47-162 ms), only two short input stages (sparser onset), half
		// HAAR's damping at every Damp, +3 dB above 3 kHz on the feed, and a
		// +12-semitone shimmer in two parts (Network.hpp). In the loop, a
		// small cascade (shimmer 0.16): ICE's T60 is about 13 % shorter than
		// Visibility's at every setting, and this octave fades as the tail
		// lengthens (-15 dB under a held note at noon, -34 dB at DENSE). On
		// the wet, a fixed feed-forward octave (shimmerOut 0.35), shifted
		// once and never fed back: together they put the octave 11-14 dB
		// under a held 440 Hz note at Visibility 0.5 to 1.
		{16,
		 {47.3f, 53.9f, 61.1f, 67.7f, 74.9f, 82.3f, 89.9f, 97.1f, 104.9f, 112.3f, 120.7f, 128.9f, 137.3f, 145.1f, 153.7f, 161.9f},
		 2, {9.f, 23.f, 0.f, 0.f},
		 0.f, 0.f,
		 0.f, {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
		 0.16f, 0.35f, 0.5f,
		 TONE_HIGH_SHELF, 3000.f, 3.f, 0.7071f,
		 0.4f,
		 0.724f, 0.241f},
	};
	return table[(m >= HAAR && m < MODES_LEN) ? m : HAAR];
}

// The spec's decay law: T60 = 0.4 s x 200^v (v in [0, 1]).
inline float fogT60(float v) {
	return 0.4f * fastExp2(v * 7.64385618977472f); // log2(200)
}

} // namespace fog
} // namespace rka

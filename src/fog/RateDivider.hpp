#pragma once

// Runs a stereo inner process at the host rate divided by 1, 2, 4 or 8, so
// the inner rate never exceeds 96 kHz. Each halving is a polyphase halfband
// (fathom/Halfband.hpp) going in and coming out. The inner process runs once
// per `factor` host samples; output is one inner block late (latency).
#include "fathom/Halfband.hpp"

namespace rka {

struct RateDivider {
	static const int MAX_LEVELS = 3;
	int fac = 1;
	int levels = 0;
	int phase = 0;
	// Per channel (0 = L, 1 = R) and level.
	Downsampler2x down[2][MAX_LEVELS];
	Upsampler2x up[2][MAX_LEVELS];
	float pend[2][MAX_LEVELS][2] = {};
	int pendN[MAX_LEVELS] = {};
	float outBuf[2][8] = {};
	float hostRate = 48000.f;

	void setHostRate(float hz) {
		hostRate = hz;
		int f = 1, l = 0;
		while (l < MAX_LEVELS && hz / (float) f > 96000.f) {
			f *= 2;
			l++;
		}
		fac = f;
		levels = l;
		reset();
	}

	int factor() const {
		return fac;
	}

	float innerRate() const {
		return hostRate / (float) fac;
	}

	void reset() {
		phase = 0;
		for (int c = 0; c < 2; c++) {
			for (int l = 0; l < MAX_LEVELS; l++) {
				down[c][l].reset();
				up[c][l].reset();
				pend[c][l][0] = pend[c][l][1] = 0.f;
			}
			for (int i = 0; i < 8; i++)
				outBuf[c][i] = 0.f;
		}
		for (int l = 0; l < MAX_LEVELS; l++)
			pendN[l] = 0;
	}

	template <class F>
	void process(const float inL, const float inR, float& outL, float& outR, F inner) {
		if (fac == 1) {
			inner(inL, inR, outL, outR);
			return;
		}
		outL = outBuf[0][phase];
		outR = outBuf[1][phase];
		// Push the input down the halfband chain; a level emits one sample
		// every second input.
		float l = inL, r = inR;
		bool have = true;
		for (int lev = 0; lev < levels && have; lev++) {
			pend[0][lev][pendN[lev]] = l;
			pend[1][lev][pendN[lev]] = r;
			if (++pendN[lev] < 2) {
				have = false;
				break;
			}
			pendN[lev] = 0;
			l = down[0][lev].process(pend[0][lev]);
			r = down[1][lev].process(pend[1][lev]);
		}
		if (have) {
			float yl, yr;
			inner(l, r, yl, yr);
			// Expand back up the chain, innermost level first.
			float a[2][8], b[2][8];
			a[0][0] = yl;
			a[1][0] = yr;
			int n = 1;
			for (int lev = levels - 1; lev >= 0; lev--) {
				for (int c = 0; c < 2; c++) {
					for (int i = 0; i < n; i++)
						up[c][lev].process(a[c][i], &b[c][2 * i]);
				}
				n *= 2;
				for (int c = 0; c < 2; c++)
					for (int i = 0; i < n; i++)
						a[c][i] = b[c][i];
			}
			for (int c = 0; c < 2; c++)
				for (int i = 0; i < 8 && i < n; i++)
					outBuf[c][i] = a[c][i];
		}
		phase = (phase + 1) % fac;
	}
};

} // namespace rka

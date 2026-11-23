// Spindrift's panel behaviour (Controls.hpp), ported from Beads' cv_reader.h
// and ui.cc, which the vendored firmware does not include.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "testing.hpp"
#include "Controls.hpp"

using namespace spindrift;

namespace {

void attenuverter_matches_cvreader() {
	for (int i = 0; i <= 100; i++) {
		float k = i / 100.f;
		// cv_reader.h:110-113, written out independently.
		float v = k - 0.5f;
		float expected = (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
		CHECK(attenuverterLaw(k) == expected, "k=%g: %g vs %g", k, attenuverterLaw(k), expected);
	}
	CHECK(attenuverterLaw(0.5f) == 0.f, "centre");
	CHECK(attenuverterLaw(0.52f) == 0.f, "dead zone");
}

void detune_snaps() {
	CHECK(detuneLaw(0.5f) == 0.f, "centre");
	CHECK(detuneLaw(1.f) == 24.f, "top");
	CHECK(detuneLaw(0.f) == -24.f, "bottom");
	const float snaps[7] = {0, 1, 3, 4, 7, 12, 24};
	for (int k = 0; k < 7; k++) {
		for (float f : {0.05f, 0.25f}) { // within the first third: a plateau
			float knob = 0.5f + 0.5f * (k + f) / 6.f;
			CHECK(detuneLaw(knob) == snaps[k], "k=%d f=%g: %g", k, f, detuneLaw(knob));
			CHECK(detuneLaw(1.f - knob) == -snaps[k], "neg k=%d f=%g", k, f);
		}
	}
}

void seed_latched_at_reset() {
	Buttons b;
	b.reset();
	CHECK(b.seedLatched, "latched");
	CHECK(!b.seedHeld && !b.frozenButton, "idle");
	CHECK(b.quality == DIGITAL && b.pending == DIGITAL, "digital");
	CHECK(!b.autoGainRequest, "no auto gain");
}

void seed_press_unlatches() {
	Buttons b;
	b.reset();
	b.seedDown();
	CHECK(!b.seedLatched && b.seedHeld, "pressed");
	b.seedUp(100);
	CHECK(!b.seedLatched && !b.seedHeld, "short release stays unlatched");
}

void seed_hold_4s_latches() {
	Buttons b;
	b.reset();
	b.seedDown();
	for (int i = 0; i < 15999; i++) b.tick();
	CHECK(!b.seedLatched, "not yet at 15999");
	b.tick();
	CHECK(b.seedLatched, "latched at 16000");
	b.seedUp(16000);
	CHECK(b.seedLatched, "release keeps it");
	// release-only path (no ticks): heldTicks decides
	Buttons c;
	c.reset();
	c.seedDown();
	c.seedUp(16000);
	CHECK(c.seedLatched, "latched by held time");
}

void seed_plus_freeze_latches() {
	Buttons b;
	b.reset();
	b.seedDown();
	b.freezePress();
	CHECK(b.seedLatched, "latched");
	CHECK(!b.frozenButton, "freeze not toggled");
	b.seedUp(500);
	CHECK(b.seedLatched, "release ignored");
	b.freezePress();
	CHECK(b.frozenButton, "freeze alone toggles");
	b.freezePress();
	CHECK(!b.frozenButton, "and back");
}

void quality_steps_digital_vintage_clean_dirty() {
	Buttons b;
	b.reset();
	const Quality order[] = {VINTAGE, CLEAN_TAPE, DIRTY_TAPE, DIGITAL};
	for (Quality q : order) {
		b.qualityDown();
		b.qualityUp(100);
		CHECK(b.quality == q && b.pending == q, "expected %d got %d", (int) q, (int) b.quality);
	}
	CHECK(!b.autoGainRequest, "no auto gain");
}

void quality_long_press_sets_auto_gain_not_step() {
	Buttons b;
	b.reset();
	b.qualityDown();
	b.qualityUp(5999);
	CHECK(b.quality == VINTAGE && !b.autoGainRequest, "5999 ticks steps");
	b.qualityDown();
	b.qualityUp(6000);
	CHECK(b.autoGainRequest, "6000 ticks requests");
	CHECK(b.quality == VINTAGE && b.pending == VINTAGE, "no step");
}

void quality_while_frozen_is_pending_last_wins() {
	Buttons b;
	b.reset();
	b.freezePress();
	CHECK(b.frozenButton, "frozen");
	for (int i = 0; i < 3; i++) {
		b.qualityDown();
		b.qualityUp(100);
	}
	CHECK(b.quality == DIGITAL, "quality unchanged");
	CHECK(b.pending == DIRTY_TAPE, "pending is the third: %d", (int) b.pending);
	CHECK(b.hasPending(), "has pending");
	b.applyPending();
	CHECK(b.quality == DIRTY_TAPE && !b.hasPending(), "applied");
}

void pot_filter_and_light() {
	PotFilter f;
	f.init(0.05f, 0.f);
	float v = f.process(1.f);
	CHECK(v == 0.05f, "one pole: %g", v);
	PotFilter h;
	h.init(1.f, 0.1f);
	CHECK(h.process(0.05f) == 0.f, "inside hysteresis holds");
	CHECK(std::fabs(h.process(0.5f) - 0.4f) < 1e-6f, "outside follows at threshold");
	Buttons b;
	b.reset();
	CHECK(b.seedLight(0) == 64.f / 255.f, "latched floor");
	CHECK(b.seedLight(1024) > 0.9f, "latched peak");
	b.seedDown();
	CHECK(b.seedLight(0) == 1.f, "held lit");
	b.seedUp(10);
	CHECK(b.seedLight(0) == 0.f, "unlatched dark");
}

} // namespace

int main() {
	attenuverter_matches_cvreader();
	detune_snaps();
	seed_latched_at_reset();
	seed_press_unlatches();
	seed_hold_4s_latches();
	seed_plus_freeze_latches();
	quality_steps_digital_vintage_clean_dirty();
	quality_long_press_sets_auto_gain_not_step();
	quality_while_frozen_is_pending_last_wins();
	pot_filter_and_light();
	return rkatest::finish("test_controls");
}

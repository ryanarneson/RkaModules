# Fathom Phase 2 (Ladder Filter) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Fathom's placeholder 12 dB low-pass with a compensated, 2x-oversampled Moog-style ladder (RES, DRIVE, four types, sub routing), lay the filter section out flat under the voice orbit, and shrink the plugin's attenuverters to a 4.6 mm Nightglow trim.

**Architecture:** Two new Rack-free headers in `src/fathom/`: `Halfband.hpp` (Fundamental's polyphase 2x pair) and `Ladder.hpp` (Fundamental's TPT ladder with Mystran linearisation, plus the Xpander mixing table and the `1 + k` low-pass compensation). `DroneVoice.hpp`'s `OutputStage` becomes the filter stage: knob-to-coefficient mapping, smoothing, per-side ladder, sub insert before or after, makeup, clip, DC block. The module gains four params and two inputs (appended), two menu items, and a flat layout from `gen_panels.py`, which also generates the trim SVGs.

**Tech Stack:** C++11 plugin code on Rack SDK 2.6.6 (`RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64` in every shell), C++17 offline tests via `src/testing.hpp` (`CHECK`, `rkatest::finish`), Python 3 panel generator, Inkscape for `make panels`.

**Spec:** `docs/specs/2026-10-24-fathom-filter-design.md` (phase 1 context: `docs/specs/2026-10-24-fathom-design.md`).

## Global Constraints

- DSP headers are Rack-free and tested offline; `Fathom.cpp` only maps params, ports and the menu onto them. (CLAUDE.md)
- Never hand-edit `res/`, `res-src/`, `src/<module>/*Layout.hpp`, `src/RkaTheme.hpp`; `make panels` is the single source. (CLAUDE.md)
- Existing param and input ids keep their order; new ones are appended to the end of their enums so phase 1 patches load. (Spec, Controls)
- Audio ±5 V; CV: RES/DRIVE `knob + atten·(cv/10)·(max−min)` clamped; CUTOFF `octaves = knob + atten·cv`. (Spec)
- Ladder credits: VCV Fundamental (GPL-3.0-or-later) for structure, halfband coefficients and rational approximations; Mystran for the linearisation; Zavalishin; Huovilainen; Oberheim Xpander table via Electric Druid. (Spec)
- Before calling a task done: `make test`; before the branch is done: `make`, `make smoke`, screenshots looked at. (CLAUDE.md)
- Panel: 10 HP, voice orbit untouched, positions from the spec's table. Trim 4.6 mm, MidKnob = Rogan1PSWhite. (Spec)
- Commit per task with the attribution trailer `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`.

## Review Focus

Inputs the spec implies but no task's tests fully exercise. Each has a test pinned to its owning task below.

1. Cutoff CV driving the ladder past Nyquist/2 at 44.1 kHz (CUTOFF CV +5 V on an open knob): `setCoefficients` must clamp at `0.45·fs` and the output stay finite. (Task 2, `testCutoffClamp`)
2. Switching type while resonating (menu change mid-sound): no NaN, no stuck state, the new type's response appears within 50 ms. (Task 2, `testTypeSwitchMidSound`)
3. Sample-rate change at runtime (Rack engine rate changed from 48 k to 96 k): coefficients recomputed, no burst. (Task 3, `testSampleRateChangeNoBurst`)
4. A patch saved with phase 1 loads with the new params at their defaults and the new inputs disconnected, so it sounds like a 20 % RES, unity-drive LP24 version of itself. (Task 5, manual check in Rack with the phase 1 smoke patch; ids verified by enum order in `Fathom.cpp`.)
5. Silence in, RES at 100 %: the ladder must start oscillating from nothing (the `1e-6` noise term), and at RES 0 it must not hum (noise inaudible). (Task 2, `testSelfOscillation` and `testNoiseFloorAtZeroRes`)

---

### Task 1: Halfband 2x pair

**Files:**
- Create: `src/fathom/Halfband.hpp`
- Test: `src/fathom/test_halfband.cpp`

**Interfaces:**
- Produces: `rka::Upsampler2x { void reset(); void process(float in, float out[2]); }`, `rka::Downsampler2x { void reset(); float process(const float in[2]); }`, both in `namespace rka`, header credits Fundamental.

- [ ] **Step 1: Write the failing tests**

```cpp
// src/fathom/test_halfband.cpp
#include "Halfband.hpp"
#include "testing.hpp"
#include <cmath>
using namespace rka;

// A 1 kHz sine up and straight back down comes out at unity (within 0.1 dB), after settling.
static void testRoundTripIsUnity() {
	Upsampler2x up; Downsampler2x down;
	const float fs = 48000.f;
	double sumIn = 0, sumOut = 0;
	for (int i = 0; i < 8192; i++) {
		const float x = std::sin(2.f * float(M_PI) * 1000.f * i / fs);
		float os[2]; up.process(x, os);
		const float y = down.process(os);
		if (i >= 4096) { sumIn += double(x) * x; sumOut += double(y) * y; }
	}
	const double dB = 10.0 * std::log10(sumOut / sumIn);
	CHECK(std::fabs(dB) < 0.1, "round trip %f dB", dB);
}

// Content near the oversampled Nyquist injected between the halves is removed on the way down:
// alternate +1/-1 at the oversampled rate (24 kHz at 2x48k) comes out below -60 dB.
static void testDownsamplerRejectsNyquist() {
	Downsampler2x down;
	double sum = 0; int n = 0;
	for (int i = 0; i < 8192; i++) {
		const float os[2] = {1.f, -1.f};
		const float y = down.process(os);
		if (i >= 4096) { sum += double(y) * y; n++; }
	}
	const double dB = 10.0 * std::log10(sum / n);
	CHECK(dB < -60.0, "alternating input leaks at %f dB", dB);
}

int main() {
	testRoundTripIsUnity();
	testDownsamplerRejectsNyquist();
	return rkatest::finish("test_halfband");
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `export RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64 && make build/test/fathom/test_halfband 2>&1 | tail -3`
Expected: compile error, `Halfband.hpp` not found.

- [ ] **Step 3: Implement `src/fathom/Halfband.hpp`**

Port `allpassCascadeProcess`, `HALFBAND_2X_COEFFICIENTS_A/B`, `Upsampler2x`, `Downsampler2x` from `~/ws/plugins/Fundamental/src/VCF.cpp` (lines 62–130) as plain `float` structs (no template, no simd) in `namespace rka`. Header comment: the halfband description from Fundamental's comment, and a credit line "Adapted from VCV Fundamental's VCF.cpp (GPL-3.0-or-later)".

- [ ] **Step 4: Run the tests**

Run: `make build/test/fathom/test_halfband && ./build/test/fathom/test_halfband`
Expected: `test_halfband: all checks passed`

- [ ] **Step 5: Commit**

```bash
git add src/fathom/Halfband.hpp src/fathom/test_halfband.cpp
git commit -m "Fathom: 2x halfband pair from Fundamental"
```

---

### Task 2: The ladder core

**Files:**
- Create: `src/fathom/Ladder.hpp`
- Test: `src/fathom/test_ladder.cpp`

**Interfaces:**
- Consumes: Task 1's `Upsampler2x`, `Downsampler2x`.
- Produces, all in `namespace rka`:
  - `float tanhXdX(float x)` ((4,6) rational `tanh(x)/x`, valid on [−4, 4]) and `float tan34(float x)` ((3,4) rational tan), from Fundamental.
  - `enum class LadderType { LP24, LP12, BP, HP };`
  - `static const float LADDER_K_MAX = 4.1f;` `static const float BASS_COMP = 1.f;` `static const float LADDER_NOISE = 1e-6f;`
  - `struct Ladder { void reset(); void setCoefficients(float cutoffHz, float k, LadderType type, float sampleRate); float process(float x); float core(float x); }` where `process` takes one base-rate sample (oversamples inside) and `core` runs one oversampled-rate sample (what `process` calls twice). `setCoefficients` clamps `cutoffHz` to `[1, 0.45·sampleRate]`, stores `g = tan34(π·fc/(2·sampleRate))`, `k`, `type`, and `comp = (type is LP24 or LP12) ? 1 + BASS_COMP·k : 1`. `k` is used as given (the knob mapping lives in Task 3).

- [ ] **Step 1: Write the failing tests**

`src/fathom/test_ladder.cpp`, with a helper `gainDb(type, sineHz, fc, k, fs, amplitude = 1)` in the `test_svf.cpp` style (8192 samples, measure the second half, RMS out over RMS in through `process`), and `peakAfter(ladder, seconds)` that returns the output peak over the last 0.5 s. Tests, each a function, with these assertions:

```cpp
static void testLp24Shape() {           // k = 0, amplitude 0.5
	CHECK(gainDb(LP24, 100.f, 1000.f, 0.f, 48000.f, 0.5f) > -0.5, ...);   // a decade below: passes
	CHECK(gainDb(LP24, 4000.f, 1000.f, 0.f, 48000.f, 0.5f) < -40.0, ...); // two octaves above
}
static void testLp12Shape() {
	const double above = gainDb(LP12, 4000.f, 1000.f, 0.f, 48000.f, 0.5f);
	CHECK(above < -20.0 && above > -30.0, ...);
}
static void testSteadyLowEnd() {        // LP24 and LP12: 40 Hz at fc 1 kHz, RES 90% (k = 4.1*0.81), amplitude 0.5
	for each of LP24, LP12: CHECK(fabs(gainDb(t, 40, 1000, 3.321f, 48000, 0.5f) - gainDb(t, 40, 1000, 0.f, 48000, 0.5f)) < 1.5, ...);
	// HP, fc 100 Hz, 5 kHz sine: same within 1.5 dB (no comp there)
}
static void testHpAndBp() {
	CHECK(fabs(gainDb(HP, 5000, 100, 0, 48000, 0.5f)) < 1.0, ...);
	CHECK(gainDb(HP, 20, 100, 0, 48000, 0.5f) < -40.0, ...);
	const double at = gainDb(BP, 1000, 1000, 0, 48000, 0.5f);
	CHECK(at - gainDb(BP, 100, 1000, 0, 48000, 0.5f) > 20.0 && at - gainDb(BP, 10000, 1000, 0, 48000, 0.5f) > 20.0, ...);
}
static void testSelfOscillation() {     // no input, k = LADDER_K_MAX, fc 220 Hz
	// run 1 s at 48k, then measure 1 s: peak in [0.05, 0.5]; frequency from rising zero crossings within 5% of 220
	// run the same at 96k: frequency within 0.5% of the 48k one
	// run at 48k with fc 440: frequency within 0.5% of twice the 220 one
}
static void testNoiseFloorAtZeroRes() { // no input, k = 0, fc 1 kHz: peak after 1 s below 1e-4
}
static void testResonanceTuningSmallSignal() { // k for RES 95% = 4.1*0.9025, fc 1 kHz, amplitude 0.001:
	// gainDb at 900, 950, 1000, 1050, 1100 Hz; the max must be at 950, 1000 or 1050 (within 5%)
}
static void testDriveAddsHarmonics() {  // 100 Hz sine, fc 10 kHz, k = 0; amplitude 8 (DRIVE 100%) vs 0.5 (DRIVE 0)
	// Goertzel at 300 Hz relative to 100 Hz, in dB: the 8x case is at least 20 dB higher
	// and the output peak at amplitude 8 is <= 2.0
}
static void testStabilityUnderSteps() { // 5 s, ±1 square at 110 Hz in, k = LADDER_K_MAX, amplitude 8,
	// cutoff alternates 20 Hz / 20 kHz every 100 ms: every sample finite and |y| < 2.5. Then the same with k alternating 0 / K_MAX at fc 1 kHz.
}
static void testCutoffClamp() {         // fs 44100, setCoefficients(30000, K_MAX, LP24): 1 s of ±1 square in, finite, |y| < 2.5
}
static void testTypeSwitchMidSound() {  // k = K_MAX, fc 500 Hz, LP24 with a 500 Hz sine 0.5 s, then setCoefficients(..., HP, ...)
	// over the next 50 ms every sample finite; after 50 ms the 500 Hz gain matches a fresh HP ladder's within 1 dB
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `make build/test/fathom/test_ladder 2>&1 | tail -3`
Expected: compile error, `Ladder.hpp` not found.

- [ ] **Step 3: Implement `src/fathom/Ladder.hpp`**

Port `tan_3_4` and `tanhXdX_4_6` (Fundamental `VCF.cpp` lines 24–59) as `tan34` and `tanhXdX`. `Ladder::core(x)` is Fundamental's loop body (lines 194–236) with these changes: `u0 = comp * x - k * yPrev[3] + LADDER_NOISE * white()` where `white()` is a splitmix64 step (as `DriftWalk` in `Drift.hpp`, seed 7) mapped to [−1, 1]; after the four stages, mix the output by `type` from `[A·u0Clamped, B·y0, C·y1, D·y2, E·y3]` with the spec's table (LP24 `0,0,0,0,1`; LP12 `0,0,1,0,0`; BP `0,0,4,−8,4`; HP `1,−4,6,−4,1`), then `v * tanhXdX(0.5f * v)`. `process(x)`: `up.process(x, os); os[0] = core(os[0]); os[1] = core(os[1]); return down.process(os);`. `reset()` zeroes states and the resamplers. Clamp `u0` to ±4 before the tanh, as Fundamental does. Header comment carries the credits from Global Constraints.

- [ ] **Step 4: Run the tests**

Run: `make build/test/fathom/test_ladder && ./build/test/fathom/test_ladder`
Expected: `test_ladder: all checks passed`. If `testResonanceTuningSmallSignal` or `testSelfOscillation` miss, the spec's reviewer numbers (peak ≈ 0.1 at k 4.1; small-signal peak −4 %) say the bound is the problem, not the code: ledger a ruling and widen within the spec's stated tolerances, not beyond.

- [ ] **Step 5: Commit**

```bash
git add src/fathom/Ladder.hpp src/fathom/test_ladder.cpp
git commit -m "Fathom: compensated 2x-oversampled ladder with LP24/LP12/BP/HP"
```

---

### Task 3: The filter stage in DroneVoice.hpp

**Files:**
- Modify: `src/fathom/DroneVoice.hpp` (the `OutputStage` struct and `DroneVoice::Out`)
- Test: `src/fathom/test_voice.cpp`

**Interfaces:**
- Consumes: Task 2's `Ladder`, `LadderType`, `LADDER_K_MAX`.
- Produces:
  - `DroneVoice::Out { float left; float right; float subMix; float sub; }`: `left`/`right` are now the **body only**; `subMix = SUB · SUB_GAIN · drift.subGain · sub` is returned and no longer added to `left`/`right`.
  - `struct OutputStage { struct Params { float cutoffHz; float resonance; float drive; LadderType type; bool subThroughFilter; }; void reset(); void process(const Params& p, float subMix, float sampleRate, float& left, float& right); }`.
  - Constants: `static const float DRIVE_OCTAVES = 4.f;` (gain `2^(4·drive − 1)`), `static const float MAKEUP_EXP = 0.25f;`, `static const float RES_SLEW_SEC = 0.02f;`, `CLIP_DRIVE` retuned.

- [ ] **Step 1: Update and add tests in `test_voice.cpp`**

Change `run()` callers that read the sub in the mix: `testSubOutIgnoresSubLevel` now checks `quiet.subMix == 0` and `loud.subMix` RMS ≈ `0.6 · M_SQRT1_2`; `testDefaultLevel` and `testSpreadKeepsMonoSum` use `left + subMix`. `testModuleLevelAtDefaults` and `testCutoffIsSmoothed` call the new `OutputStage::process(Params{1200.f, 0.2f, 0.25f, LadderType::LP24, false}, o.subMix, FS, l, r)`. `testOutputStage` becomes three tests against the new stage with a sine in and `subMix = 0`:

```cpp
static void testStageClosedCutoffAttenuates();   // Params cutoff 20 Hz, res 0, drive 0.25: 100 Hz sine down > 20 dB (as before)
static void testStageBlocksDc();                 // as before
static void testDriveMakeup();                   // 100 Hz sine, cutoff 20 kHz, res 0: RMS at drive 1.0 within 2 dB of RMS at drive 0.25; drive 0 within 1 dB of 0.25
static void testSubRouting();                    // subMix = 0.5*sin(65 Hz), left = right = 0, cutoff 20 Hz:
                                                 //   subThroughFilter false: output RMS within 1 dB of the same with cutoff 20 kHz
                                                 //   subThroughFilter true: output RMS at least 15 dB below the open case
static void testResAndDriveAreSmoothed();        // the testCutoffIsSmoothed pattern with resonance stepping 0 -> 1 and, separately, drive 0 -> 1
static void testSampleRateChangeNoBurst();       // 1 s at 48k with a 100 Hz sine, then continue at 96k: no sample in the first 10 ms after the switch exceeds 1.5x the steady peak
```

- [ ] **Step 2: Run to verify they fail**

Run: `make build/test/fathom/test_voice 2>&1 | tail -3`
Expected: compile errors (`subMix`, `Params`, `LadderType` unknown).

- [ ] **Step 3: Implement**

In `DroneVoice::process`, stop adding `subMix` to `l`/`r`; return it in `Out::subMix`. Replace `OutputStage`: members `Ladder left, right; DcBlock dcLeft, dcRight;` smoothed `cutoffOct`, `k`, `driveGain` (20 ms one-poles, once per `BLOCK`, in `process` when `block == 0`): target `k = LADDER_K_MAX · res²`, target `driveGain = exp2(DRIVE_OCTAVES · drive − 1)`; on a block boundary call `setCoefficients(exp2(cutoffOct), k, p.type, sampleRate)` on both ladders. Per sample: `in = side + (p.subThroughFilter ? subMix : 0)`, `v = ladder.process(driveGain * in) * makeup`, `makeup = driveGain < 1 ? 1/driveGain : pow(driveGain, −MAKEUP_EXP)` (computed per block), `v += p.subThroughFilter ? 0 : subMix`, then `dc.process(tanh(CLIP_DRIVE · v), dcCoef)`. Reset both ladders on a sample-rate change (the `lastRate` pattern). Retune `CLIP_DRIVE` until `testModuleLevelAtDefaults` passes (start at 2.2; the ladder's 24 dB slope takes more level than the SVF did, so expect it to go up).

- [ ] **Step 4: Run the whole suite**

Run: `make test 2>&1 | tail -12`
Expected: every binary prints `all checks passed`.

- [ ] **Step 5: Commit**

```bash
git add src/fathom/DroneVoice.hpp src/fathom/test_voice.cpp
git commit -m "Fathom: ladder output stage with sub routing, drive makeup, smoothing"
```

---

### Task 4: Widgets and panel

**Files:**
- Modify: `src/widgets.hpp` (MidKnob, Trim)
- Modify: `tools/gen_panels.py` (Fathom layout, tick helper, trim SVGs)
- Generated: `res/components/Trim.svg`, `res/components/Trim_bg.svg`, `res-src/fathom/*`, `res/fathom/*`, `src/fathom/FathomLayout.hpp`

**Interfaces:**
- Produces: `rka::MidKnob` (Rogan1PSWhite); `rka::Trim` as an `app::SvgKnob` loading `res/components/Trim.svg` over `Trim_bg.svg`, `minAngle = −0.75·M_PI`, `maxAngle = 0.75·M_PI`. `FathomLayout` constants `PARAM_RES`, `PARAM_DRIVE`, `PARAM_RES_ATT`, `PARAM_DRIVE_ATT`, `INPUT_RES_CV`, `INPUT_DRIVE_CV` (and the existing names, with `PARAM_CUTOFF`, `PARAM_CUTOFF_ATT`, `INPUT_CUTOFF_CV` moved).

- [ ] **Step 1: Write the generator check**

Add to `tools/gen_panels.py` a `main()`-level assertion that runs after generation: for Fathom, every pair of components is at least `r_a + r_b + 0.3` mm apart, using `RADII = {"big": 8.8, "mid": 6.7, "small": 4.32, "trim": 2.3, "jack": 4.0}` and a per-component size map in the Fathom spec (`sizes={"PITCH": "big", "CUTOFF": "mid", ...}`, default `"small"` for params, `"jack"` for ports). Run `python3 tools/gen_panels.py` before changing the layout: it must pass on the phase 1 layout (sanity), then fail on an intentionally overlapping test edit, then pass again (prove the check bites).

- [ ] **Step 2: Lay out the filter section**

In `PANELS["Fathom"]`: remove the cutoff ring and the old CUTOFF/CUTOFF_ATT/CUTOFF_CV entries; add components at the spec's positions (RES 9.5/76, CUTOFF 25.4/76, DRIVE 41.3/76; RES_ATT/CUTOFF_ATT/DRIVE_ATT at y 86.5; RES_CV/CUTOFF_CV/DRIVE_CV at y 94.0 with "CV" labels, size 1.5, hint, gap −6.2). Labels: RES/DRIVE above at 69.5 (size 1.8), CUTOFF above at 67.3 (size 2.2, ink); `BIG_KNOBS` loses `("Fathom", "CUTOFF")`. Add the voice line `(25.4, 56.9, 25.4, 68.9, "cyan", False)`. Replace the hard-coded band ticks with a `ticks=[(from_name, to_name, accent)]` entry rendered by a new `tick()` helper that draws a dotted line from the edge of `from` to the edge of `to` along their centre line, 0.3 mm clear of each, using `RADII`: ticks `("RES", "RES_ATT", "cyan")`, `("CUTOFF", "CUTOFF_ATT", "cyan")`, `("DRIVE", "DRIVE_ATT", "cyan")`, `("DETUNE_ATT", "DETUNE_CV", "violet")`, `("DRIFT_ATT", "DRIFT_CV", "violet")`. Output card and band otherwise unchanged. Sway's spec is untouched and its generated files must stay byte-identical (`git diff --stat res-src/sway src/sway/SwayLayout.hpp` empty).

- [ ] **Step 3: Generate the trim SVGs**

Add `trim_svgs(root)` to `gen_panels.py`, called from `main()`: writes `res/components/Trim_bg.svg` (4.6 × 4.6 mm, viewBox `0 0 4.6 4.6`, a circle r 2.3 at the centre filled with `THEMES[""]["card"]`, stroked 0.25 with `THEMES[""]["hint"]`) and `res/components/Trim.svg` (same size, a single line from (2.3, 1.9) to (2.3, 0.35), stroke `THEMES[""]["ink"]`, width 0.5, round caps). No text, so no Inkscape pass.

- [ ] **Step 4: Widgets**

```cpp
struct MidKnob : componentlibrary::Rogan1PSWhite {};
struct Trim : app::SvgKnob {
	Trim() {
		minAngle = -0.75f * float(M_PI);
		maxAngle = 0.75f * float(M_PI);
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trim.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/components/Trim_bg.svg")));
	}
};
```
(`pluginInstance` is declared in `plugin.hpp`; include it, or forward-declare `extern Plugin* pluginInstance;` as `plugin.hpp` does.)

- [ ] **Step 5: Regenerate and build**

Run: `python3 tools/gen_panels.py && make panels 2>&1 | tail -2 && make 2>&1 | grep -i "warning\|error" ; echo build-done`
Expected: `generated Sway`, `generated Fathom`, no warnings, `build-done`. `Fathom.cpp` still compiles against the old names because `PARAM_CUTOFF` etc. still exist; the new constants are unused until Task 5.

- [ ] **Step 6: Commit**

```bash
git add tools/gen_panels.py src/widgets.hpp res/components res-src/fathom res/fathom src/fathom/FathomLayout.hpp
git commit -m "Fathom: flat filter section, 4.6 mm trim, mid knob"
```

---

### Task 5: Module wiring

**Files:**
- Modify: `src/fathom/Fathom.cpp`

**Interfaces:**
- Consumes: Task 3's `OutputStage::Params`, `LadderType`; Task 4's layout constants and widgets.

- [ ] **Step 1: Params, inputs, menu**

Append `RES_PARAM, DRIVE_PARAM, RES_ATT_PARAM, DRIVE_ATT_PARAM` to `ParamId` before `PARAMS_LEN`, and `RES_INPUT, DRIVE_INPUT` to `InputId` before `INPUTS_LEN`. `configParam(RES_PARAM, 0, 1, 0.2, "Resonance", "%", 0, 100)`, `configParam(DRIVE_PARAM, 0, 1, 0.25, "Drive", "%", 0, 100)`, attenuverters as the existing ones, `configInput` "Resonance CV", "Drive CV". Members `int filterType = 0;` (index into `LadderType`), `bool subThroughFilter = false;` saved as `"filterType"` (clamped 0–3) and `"subThroughFilter"` in JSON, reset in `onReset` (also `stage.reset()`). Menu: `createIndexPtrSubmenuItem("Filter type", {"Low-pass 24 dB", "Low-pass 12 dB", "Band-pass", "High-pass"}, &module->filterType)` and `createBoolPtrMenuItem("Sub through filter", "", &module->subThroughFilter)`.

- [ ] **Step 2: Process**

```cpp
const float res = clamp(modulated(RES_PARAM, RES_INPUT, RES_ATT_PARAM), 0.f, 1.f);
const float drive = clamp(modulated(DRIVE_PARAM, DRIVE_INPUT, DRIVE_ATT_PARAM), 0.f, 1.f);
rka::OutputStage::Params p{cutoffHz, res, drive, rka::LadderType(filterType), subThroughFilter};
float l = o.left, r = o.right;
stage.process(p, o.subMix, args.sampleRate, l, r);
```

- [ ] **Step 3: Widget**

Place `rka::MidKnob` at `PARAM_CUTOFF`, `rka::SmallKnob` at `PARAM_RES`/`PARAM_DRIVE`, `rka::Trim` at the three `_ATT`s, ports at `INPUT_RES_CV`/`INPUT_CUTOFF_CV`/`INPUT_DRIVE_CV`.

- [ ] **Step 4: Build, smoke, look**

Run: `make 2>&1 | grep -i "warning\|error"; make smoke 2>&1 | tail -3`
Expected: no warnings; smoke prints ok for Sway and Fathom. Open `build/screenshots/Fathom.png` and `Sway.png` with the Read tool: trims are the small dark discs with a bright pointer, the three filter columns line up, the "CV" hints clear the output card, nothing overlaps. If the hints touch the card, move the card to y 102 in `gen_panels.py`, regenerate, rebuild.

- [ ] **Step 5: Commit**

```bash
git add src/fathom/Fathom.cpp tools/gen_panels.py res-src/fathom res/fathom src/fathom/FathomLayout.hpp
git commit -m "Fathom: RES, DRIVE, filter type and sub routing on the module"
```

---

### Task 6: Docs, install

**Files:**
- Modify: `docs/fathom/README.md`, `docs/fathom/Fathom.png`, `README.md`, `CHANGELOG.md`, `CLAUDE.md`

- [ ] **Step 1: Manual**

Rewrite the "Cutoff" section of `docs/fathom/README.md` as "Filter": CUTOFF (pole frequency of a four-pole transistor ladder, 20 Hz–20 kHz), RES (to self-oscillation at the top, a quiet sine that tracks CUTOFF; the low end is held steady as resonance rises in the low-pass types), DRIVE (gain into the ladder's saturators; growl with RES), the three CV jacks with attenuverters (RES and DRIVE as DETUNE; CUTOFF 1 V/oct at full), menu items "Filter type" and "Sub through filter". Credits: add Fundamental (ladder structure, halfband), Mystran, Zavalishin, Huovilainen, Electric Druid's Xpander table. Copy `build/screenshots/Fathom.png` to `docs/fathom/Fathom.png`.

- [ ] **Step 2: README, CHANGELOG, CLAUDE.md**

README: Fathom row says "ladder filter" instead of "low-pass"; the `src/fathom/` layout block lists `Ladder.hpp, Halfband.hpp`; the widgets line mentions `rka::MidKnob` and that `rka::Trim` is the plugin's own 4.6 mm trim from `res/components/`. CHANGELOG, Fathom entry: replace the CUTOFF bullet with the ladder (RES, DRIVE, four types, sub routing, 2x oversampled, steady low end) and add a line that attenuverters are now the small Nightglow trim on every module. CLAUDE.md: in the look bullet, note `res/components/` is generated by `gen_panels.py` too.

- [ ] **Step 3: Verify and install**

Run: `make test 2>&1 | grep -c "all checks passed"; make 2>&1 | grep -ci warning; make install 2>&1 | tail -1`
Expected: `11` (ten phase 1 binaries plus `test_halfband` and `test_ladder` make twelve; adjust the count to what `ls src/*/test_*.cpp | wc -l` says), `0`, install done.

- [ ] **Step 4: Commit**

```bash
git add docs/fathom README.md CHANGELOG.md CLAUDE.md
git commit -m "Fathom: filter manual, changelog, readme"
```

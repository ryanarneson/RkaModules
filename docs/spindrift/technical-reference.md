# Spindrift 2.0.39: Technical Reference for QA

Module: **Spindrift** (Mutable Instruments Beads, vendored, in the RKA panel), plugin `rka` (slug `rka`, module slug `Spindrift`), VCV Rack 2 (minRackVersion 2.5.0). 20 HP (panel 101.6 mm wide).
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Spindrift's files in this commit are byte-identical to build 2.0.32 (commit `d376792`, merged to main); see Build history. Everything here was taken from a `git archive` of `2e6e960`.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04 (a tar archive, zstd-compressed). Its `plugin.json` (version 2.0.39) and `res/spindrift/Spindrift.svg` are byte-identical to the commit's, and it carries `tools/fonts/D-DINCondensed.ttf`, which the window's lettering uses. Spindrift has no factory presets (`presets/` holds Fog's only).
Document date: 2026-11-24.
Scope: the host and the engine: the panel mapping, the rate conversion at the edges, the quality modes, the mutes, the saved state, the window's data, CPU, and the tests that tie the vendored code to upstream. Beads itself (what Time, Size, Density and the rest do to grains) is not re-documented: that is `docs/spindrift/README.md` and Beads' own manual. Section 2.3 is the one table of Beads' controls, as this module maps them.

## Build history

Spindrift was built over test builds 2.0.29 to 2.0.32 on 2026-11-22 (`CHANGELOG.md`, `git log -- src/spindrift docs/spindrift`):

| Build | Commit | What changed in Spindrift |
|---|---|---|
| 2.0.29 | `d5b250f` | First test build: the whole module (vendored Beads with marked changes, engine, controls, window, saved buffer, bench, manual). |
| 2.0.30 | `fb7d677` | The window's top-right caption stays still: the word sits where the mode's widest number puts it, the number has a fixed slot, and the count shown is the peak over each 0.2 s (`0f05333`). |
| 2.0.31 | `79cc075` | Manual and release builds (`902480b`). |
| 2.0.32 | `d376792` | Final review fixes: a buffer frozen in the synth is no longer kept with the patch (`cd0c14a`); a filesystem failure while saving warns and the save goes on (`6a9c9ad`); `test_engine` states `mixParameter`'s constants on their own (`9fa8fce`); `test_save_race` holds on a quiet or one-core machine (`38c6dd1`); the vendored Beads is included as a system header, which fixed GCC's `-Werror` CI build (`de7085b`); the manual says a frozen buffer is kept across a sample-rate change (`d93fd95`). |
| 2.0.33 to 2.0.39 | | Nothing in Spindrift. `git diff d376792 2e6e960` is empty for `src/spindrift`, `res/spindrift`, `docs/spindrift`, the spec, `tools/gen_spindrift_dials.py`, `src/Overview.hpp`, `src/testing*.hpp` and `src/bench`. The shared `src/widgets.hpp` gained Neap and Spring widgets; its `GrainWindow` is unchanged. `tools/gen_panels.py` changed for other modules. |

So the listening guide `spindrift-2.0.32-listening.html` describes the code that 2.0.39 ships.

---

## 0. How to read this document

- Every number here comes from one of these sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/spindrift/*.hpp`, `Spindrift.cpp`, `tests.mk`, the Makefile, and the Rack SDK 2.6.6 headers), cited as file:line where useful.
  - **[test]**: asserted or printed by the module's automated tests, which I built from the 2.0.39 tree and ran (section 13). The tests print only "all checks passed" unless run with `--table`, so a [test] figure is an asserted limit or a `--table` line.
  - **[meas]**: a measurement program I wrote against the Rack-free engine (`spindrift::SpindriftEngine`, the code the module runs), compiled with `-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd` and the vendored Beads object built as the plugin builds it. Unless stated, 48 kHz, grain mode, Digital, Fixed gain, defaults. Programs are listed at the end. **The plugin itself compiles `Spindrift.cpp` with `-fno-unsafe-math-optimizations` after the fast-math flag** (Makefile, `SPINDRIFT_PLUGIN_FLAGS`), so its arithmetic is strict; CPU figures are given for both builds (section 10).
- **[issue #N]** points to an open GitHub issue in `ryanarneson/RkaModules` (section 12). None is about Spindrift itself.
- **[unverified]**: anything I could not check, mostly Rack host behaviour. I ran on an Apple M-series Mac; the shipped package is Windows x64.
- "Beads units" are the firmware's: 1.0 is full scale. The module maps ±10 V to ±1.0.
- Knob values are in [0, 1]; "noon" is 0.5. Voltages are Rack volts.
- "Rack rate" is the engine's sample rate; "Beads rate" is the codec rate the vendored code runs at (24, 32 or 48 kHz).

---

## 1. What Spindrift is

Spindrift is a 20 HP granular processor: Mutable Instruments Beads' own DSP (Emilie Gillet, MIT licence, `LICENSE-beads.md`), copied into `src/spindrift/beads/` and `src/spindrift/stmlib/` with every local change marked `// SPINDRIFT:` and listed in `src/spindrift/CHANGES.md`, behind a Rack-free engine, in RKA's panel look, with a window that shows the buffer, the record head and every grain. The design (spec `docs/specs/2026-11-22-spindrift-design.md`) is "Approach A": bring Beads' code in as it is, so that at the same settings and input the output is Beads' output.

Design goals that QA can check:

- **Beads to the bit.** At Rack 48 kHz in Digital, where nothing is converted, the engine's output equals the untouched upstream code's to float tolerance; the vendored code equals upstream bit for bit in every mode (section 13.2).
- Every Beads control and hidden setting is reachable: the panel has Beads' controls, the menu has what Beads hides behind button combinations (input gain, gate output, synth policy, keep the frozen buffer).
- Beads' four qualities, its delay mode, reverb and wavetable synth all run; a quality change clears the buffer and mutes 250 ms, as on Beads.
- Beads' aliasing is kept; Spindrift may not alias more than Beads does (within 3 dB, section 13.4). The only filters added are the converters at the edges.
- No allocation on the audio thread; instances independent; any sample rate; a frozen buffer survives saving the patch.

Non-goals (do not file these as defects): improving Beads' sound (its aliasing, quantisation, limiter and reverb are kept); polyphony (audio cables are summed, CVs read channel 1 [issue #64 is the general polyphony question]); loading or saving audio files; Beads' calibration, factory test and bootloader; Beads' monophonic-grains and buffer-trim settings (compiled out of shipping Beads, left out here).

Anti-goals: a granular processor that is "inspired by" Beads and sounds different; Beads' button combos reproduced as combos (they are menu items); a window that costs more than the rest of the patch.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Beads' own behaviour is the reference for Beads' controls; this table is about the host. The differences are small: the spec and the manual are closely in step with the code.

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| Dry level at Dry/Wet 0 | manual (Inputs and outputs): "Fully dry the input comes out at 0.75 of its level, as on Beads"; spec (Levels): ±5 V comes out at ±3.75 V | exactly 0.750 in Digital and Clean Tape up to about 5 V peak, 0.747 to 0.750 in Vintage Digital. Clean Tape's soft clip lowers it from about 6.8 V peak. **Dirty Tape is lower and level dependent**: 0.741 at 0.5 V peak, 0.686 at 5 V, 0.573 at 10 V (mu-law and an always-on soft clip) [meas, section 6.5]. The manual names those mechanisms but not the level. |
| Memory | manual: "about 850 kB per Spindrift" | Beads' two buffers are 851,968 bytes. The instance's heap is 909,424 bytes at Rack 48 kHz and **1,345,520 bytes at 44.1 kHz**, because the 44.1 kHz converter tables are 423.5 kB against 4.4 kB at 48 kHz [meas, section 5.5]. |
| Converter pass band | manual (Quality): the converters "pass the band up to 0.41 of the lower rate (to about ... 19.7 kHz in Digital and Clean Tape)" | 19.7 kHz is 0.41 × 48 kHz and holds when Rack's rate is 96 kHz or more. At Rack 48 kHz, Digital and Clean Tape have no converter at all (bit-exact path). At Rack 44.1 kHz the lower rate is Rack's, so the band edge is 18.1 kHz [code: `EdgeRate.hpp:77-79`; meas]. Vintage Digital (13.1 kHz) and Dirty Tape (9.8 kHz) are as stated at every Rack rate. |
| CV smoothing | manual (Controls): "a jump in a knob or a CV takes a few milliseconds to land" | knobs: one-pole, 5 ms time constant (22 ms to 99 %). CVs differ: Time, Size, Shape 0.5 ms; Feedback, Dry/Wet, Reverb 2.5 ms; **Density and Pitch CV are not smoothed** and land at the next 0.25 ms block [code: `SpindriftEngine.hpp:704-711`, section 2.4]. |
| CV range | spec (Controls): CV scaling "every other CV ±5 V for ±1 of the knob's travel" | Time, Size and Shape read up to ±8 V (±1.6 of travel) before clipping, as the manual says; the spec is silent on the clip. |
| Latency | spec: "one block of latency" | one block (0.25 ms) only at Rack 48 kHz in Digital or Clean Tape. Elsewhere the converters add: 1.76 ms at 96 kHz Digital, 1.92 ms at 44.1 kHz, 2.5 ms Vintage and 3.3 ms Dirty Tape at the usual rates [meas, section 5.2]. The manual's figures (0.25, 1.9, 2.5, 3.3 ms) are right. |
| Aliasing bound | spec: "plus a stated margin" | the margin is 3.0 dB over Beads' own figure, at 44.1 and 48 kHz. Worst measured excess is +0.84 dB (manual: "worst measured 0.8 dB more") [test; meas]. |
| Vendor test, Dirty Tape | `test_vendor.cpp:38-40` says its rate table is "Beads' own rate per media (shipping firmware)" | the table has Dirty Tape at 48 kHz (12-frame blocks); the shipping firmware runs Dirty Tape at 24 kHz (6-frame blocks; `ENABLE_HARDWARE_24K` is defined, `processor.h:46`). The vendored code also matches upstream bit for bit at 24 kHz [meas, section 13.2]. A comment error, with a coverage consequence (section 14). |
| Short triggers | manual: a pulse shorter than one Beads sample "can be missed" | confirmed. Pulses of at least one Beads sample (21 µs Digital, 42 µs Dirty Tape) are always seen; 1 ms is always seen at every rate. Shorter ones are seen with the fraction of phases the table in section 8.3 gives [meas]. |
| Initialize | manual: knobs, Seed latched, Digital, Freeze off, menu to defaults | as stated. It does not reset the engine: the buffer, the grains and the mode go on, and the buffer clears only if the quality changes (section 3.3). |
| Randomize | manual: "moves the knobs only" | the three buttons are not randomised (`configButton` sets `randomizeEnabled = false`, Rack SDK 2.6.6 `engine/Module.hpp:169`). Whether Rack's Randomize command then leaves the menu alone is [unverified]; the module overrides nothing. |

Checked and found to agree with the manual: the buffer lengths (4.0, 8.0, 10.67, 16.0 s stereo; doubled mono, section 6.1); the 250 ms mute; the 10 s synth timer (10.0002 s measured); the SEED delay of 119, 79 and 59 frames; the 4 s and 1.5 s hold times; the pot laws; the gate output at 0 and 10 V; the test builds' CPU figures (section 10).

---

## 2. Panel, controls and ports

### 2.1 Layout

20 HP. Positions in mm from `SpindriftLayout.hpp` (generated by `tools/gen_panels.py`) [code]:

| Control | Centre (x, y) mm |
|---|---|
| Window | 91.6 × 14.0 box at (5.0, 19.5) |
| Time / Size / Shape / Pitch knobs | 7.8 / 23.5 / 78.1 / 93.8, y 48.0 |
| Density (the hero knob) | 50.8, 53.0 |
| Attenuverters (Time, Size, Shape, Pitch) | same x as their knobs, y 61.0 |
| CV jacks: Time, Size, Shape, Pitch (V/OCT) | same x, y 73.0 |
| Density CV | 50.8, 73.0 |
| Freeze button / Feedback / Dry/Wet / Reverb / Seed button | 7.8 / 29.3 / 50.8 / 72.3 / 93.8, y 90.0 |
| IN L / IN R | 7.8, 102.0 and 7.8, 116.0 |
| Feedback CV / Dry/Wet CV / Reverb CV | 29.3 / 50.8 / 72.3, y 102.0 |
| FREEZE gate / QUALITY button / SEED trigger | 29.3 / 50.8 / 72.3, y 116.0 |
| OUT L / OUT R | 93.8, 102.0 and 93.8, 116.0 |

Two screws, at (93.98, 2.54) and (7.62, 125.96). The panel has a light and a dark SVG (`Spindrift.svg`, `Spindrift-dark.svg`); the manual's dial diagrams are dark only.

### 2.2 Parameter, input, output and light ids

The ids are the enums in `Spindrift.cpp:35-49`; the comment says "Never renumber (saved patches refer to them)". **No `static_assert` or test pins them** (Fog's are pinned); a reordering would pass the suite [section 14].

| Id | Param | Rack label | Display | Range | Default | Control |
|---|---|---|---|---|---|---|
| 0 | TIME | Time | % (knob × 100) | 0..1 | 0.5 | limb knob |
| 1 | SIZE | Size | % | 0..1 | 0.5 | limb knob |
| 2 | DENSITY | Density | % | 0..1 | **0.65** | hero knob |
| 3 | SHAPE | Shape | % | 0..1 | 0.5 | limb knob |
| 4 | PITCH | Pitch | semitones, from Beads' detune law, 3 decimals | 0..1 | 0.5 | limb knob |
| 5 | TIME_ATT | Time attenuverter | % shown −100..+100 (knob × 200 − 100) | 0..1 | 0.5 | attenuverter |
| 6 | SIZE_ATT | Size attenuverter | same | 0..1 | 0.5 | attenuverter |
| 7 | SHAPE_ATT | Shape attenuverter | same | 0..1 | 0.5 | attenuverter |
| 8 | PITCH_ATT | Pitch attenuverter | same | 0..1 | 0.5 | attenuverter |
| 9 | FEEDBACK | Feedback | % | 0..1 | 0 | limb knob |
| 10 | DRYWET | Dry/Wet | % | 0..1 | 0.5 | limb knob |
| 11 | REVERB | Reverb | % | 0..1 | 0 | limb knob |
| 12 | FREEZE | Freeze | button | 0/1 | 0 | lit cap |
| 13 | SEED | Seed | button | 0/1 | 0 | lit cap |
| 14 | QUALITY | Quality | button | 0/1 | 0 | lit cap |

Density 0.65 and Dry/Wet 0.5 are "ruling R11": a new module makes a cloud at once (Density is on the random side, clear of the dead zone at 0.48 to 0.52).

| Id | Input | | Id | Output |
|---|---|---|---|---|
| 0 | Time CV | | 0 | L (gate output mode: the mono mix) |
| 1 | Size CV | | 1 | R (gate output mode: a 0/10 V gate per grain) |
| 2 | Density CV | | | |
| 3 | Shape CV | | | |
| 4 | Pitch V/OCT | | | |
| 5 | Feedback CV | | | |
| 6 | Dry/Wet CV | | | |
| 7 | Reverb CV | | | |
| 8 | IN L | | | |
| 9 | IN R | | | |
| 10 | FREEZE gate | | | |
| 11 | SEED trigger | | | |

Lights: 0 FREEZE (lit while frozen, by the button or the gate), 1 SEED (Beads' LED: breathing while latched, lit while held or when a SEED trigger fired), 2 QUALITY (lit during the 250 ms mute after a change). The lights are updated every 32 samples (`LIGHT_SAMPLES`). The Rack bypass routes IN L to L and IN R to R (`configBypass`).

### 2.3 Beads' controls as the module maps them

The module hands Beads the same 0..1 values Beads' own `CvReader` gives it; what Beads then does is in the manual. This table is only the host's mapping [code: `SpindriftEngine.hpp:455-469, 690-745`, `Controls.hpp`].

| Control | Knob to Beads | Smoothing (block = 0.25 ms) | CV jack, scale and clip | What Beads does with it (one line; see the manual) |
|---|---|---|---|---|
| Time | linear 0..1 | one-pole 0.05 per block (τ 5 ms), then a hysteresis of 0.001 | ±5 V is ±1 of travel; read to ±8 V (±1.6) | where in the buffer a grain starts; delay time in delay mode |
| Size | linear | 0.05 | as Time | grain length and direction; past 0.99 of travel, delay mode |
| Shape | linear | 0.05 | as Time | grain envelope |
| Pitch | Beads' detune law, ±24 semitones, plateaus on 0, 1, 3, 4, 7, 12, 24 | 0.05, after the law | 12 semitones per volt, read to ±5 V (±60 semitones), no smoothing | grain transposition; the synth's note |
| Density | linear | 0.05 | ±5 V is ±1 of travel, no smoothing; only when patched | grain rate; regular left of noon, random right, dead zone 0.48 to 0.52 |
| Time, Size, Shape, Pitch attenuverters | Beads' attenuverter law (below), −1..+1 | 0.05 | none | unpatched: randomness; patched: CV amount |
| Feedback | linear, then `×1.005` (0.5 % headroom) | 0.05 | ±5 V is ±1, one-pole 0.1 (τ 2.5 ms); added as `cv − 0.002` when patched; held to [0, 1] | output fed back to the buffer; the synth's bank |
| Dry/Wet | as Feedback | 0.05 | as Feedback | input against grains; synth voice against its cloud |
| Reverb | as Feedback | 0.05 | as Feedback | Beads' reverb amount and time |

The attenuverter law (`attenuverterLaw`, `Controls.hpp:13`): with v = knob − 0.5, amount = (2/0.47) · v · max(|v| − 0.03, 0). It is 0 within 0.03 of noon, and ±1 at the ends [code; tested against `cv_reader.h` in `test_controls`]. By tooltip value [code arithmetic]:

| Tooltip | −100..+100 % at | 0, ±3, ±6 | ±10 | ±25 | ±50 | ±72 | ±90 | ±100 |
|---|---|---|---|---|---|---|---|---|
| Amount | | 0 | 0.004 | 0.050 | 0.234 | 0.506 | 0.804 | 1.000 |

The Pitch knob (`detuneLaw`, `Controls.hpp:21`): a flat on each of 0, 1, 3, 4, 7, 12 and 24 semitones, 0.0278 of travel wide (the first third of each sixth of the half travel), smoothstep between them. Flats start at knob 0.5 (0), 0.5833 (1), 0.6667 (3), 0.75 (4), 0.8333 (7), 0.9167 (12) and 1.0 (24, the end stop only), and mirror below noon. Knob 0.25 reads −4, 0.1 reads −10.92, 0.9 reads +10.92. A typed tooltip value sets the knob to the middle of the flat for that many semitones (a bisection, `PitchQuantity::setDisplayValue`, clamped to ±24) [code].

CV channel handling [code]: a CV jack counts as patched when it has a cable (`isConnected`); only channel 1 of a polyphonic cable is read (`getVoltage`); NaN reads 0 V; ±infinity is held to the jack's clip (±8 V or ±5 V). An unpatched jack contributes nothing, and for Time, Size, Shape and Pitch it turns the attenuverter into the randomness control. The three mix CVs act only when patched, at amount 1: Beads has one assignable CV and an Assign button, Spindrift has a jack for each.

### 2.4 Time response of the controls

A knob move or a CV step reaches Beads once a block (0.25 ms, 4000 a second at every Rack rate and every quality), at the Rack sample that completes the block. Then:

| Path | One-pole coefficient per block | 63 % | 99 % |
|---|---|---|---|
| Every knob and attenuverter | 0.05 | 5 ms | 22 ms |
| Time, Size, Shape CV | 0.5 | 0.5 ms | 1.7 ms |
| Feedback, Dry/Wet, Reverb CV | 0.1 | 2.5 ms | 10.9 ms |
| Density CV, Pitch CV | 1 (none) | next block | next block |

[code, arithmetic]. These add to Beads' own smoothing of the parameters inside a block. A zipper-free sweep of a knob is expected; a step on Density's or Pitch's CV is a step (Beads' own hardware does the same).

### 2.5 Buttons

[code: `Controls.hpp:62-151`, `Spindrift.cpp:345-368`] The caps are momentary params; the module detects edges on the audio thread and counts hold lengths in the engine's 4 kHz ticks (so a hold is exact at every Rack rate).

| Button | Press | Release | Hold |
|---|---|---|---|
| Freeze | toggles the freeze (the gate input ORs with it); **if Seed is held, it latches Seed instead and does not toggle** | nothing | nothing |
| Seed | unlatches Seed, starts a hold count | if not already latched by the hold: stops "held" | **4 s** (16,000 ticks) latches Seed while still held; the release after that is ignored |
| Quality | nothing | a hold under 1.5 s (6,000 ticks) steps the quality: Digital, Vintage Digital, Clean Tape, Dirty Tape, Digital (`(q + 3) % 4` from Beads' enum order 3, 2, 1, 0); **1.5 s or more does not step**: it sets the input gain to Automatic and restarts its learning (`autoGainRequest`) | |

Seed is latched at start, and after Initialize. Frozen, a Quality press changes only the `pending` choice, which the window shows ("DIGITAL → DIRTY TAPE"); several presses move the pending choice on, and the last one wins (section 6.3).

### 2.6 Audio inputs and outputs, and the levels

[code: `SpindriftEngine.hpp:242-267, 450-454, 597-622`; `Spindrift.cpp:338-341`]

- **IN L, IN R.** Each sums all channels of its cable (`getVoltageSum`). NaN reads 0. The sum is held to ±10 V and multiplied by 0.1: **±10 V is Beads' full scale**. After the input converter the signal is clipped again to ±1 (the converter's ringing cannot pass the ADC's limit). A jack with no cable reads 0 and makes Beads mono: if either input is unpatched, Beads runs mono and the patched side feeds both (`GainControl`'s normalisation). The buffer then holds twice as long.
- **Outputs.** Beads' output is held to [−1, 0.999999] (so +9.99999 V at most before conversion), converted back to Rack's rate and multiplied by 10. Fully dry (Dry/Wet 0) a 5 V sine leaves at 3.75 V peak in Digital [test: `levels`; meas]. The output converter can overshoot slightly: the hostile test's largest peak is **10.134 V** at Rack 44.1 kHz (a delay-mode Feedback +12 V segment), inside its 10.5 V bound [test, `--table`].
- **Gate output mode** (menu): L is Beads' mono mix; R is a gate, 10 V while Beads' right channel is below −0.4 (Beads gives −0.8 during a grain) and 0 V otherwise. It is decided from the Beads-rate sample nearest the output kernel's centre, not filtered, so it does not ring. Both levels and nothing else are asserted [test: `levels`, `matchesRefAtNativeRate`].
- **TRIG (SEED) and GATE (FREEZE).** A Schmitt trigger: high from 1 V, low at 0.1 V or under, per input; NaN keeps the state. Read per Beads frame (section 8).
- Polyphony: audio is summed, CVs and gates read channel 1.

### 2.7 Input gain

[code: `GainControl.hpp`; menu in section 3.1] Beads' gain stage sits between the input converter and the processor. Default **Fixed**: the manual path at gain 1, so ±10 V is full scale and the stage hard-clips at ±1 (Digital and Vintage), with Beads' soft clip crossfaded in for the tape qualities: always in Dirty Tape, and in Clean Tape by a "dynamic" amount that rises when the input's slow peak follower passes 0.675 (6.75 V) and is at most 0.5 in Fixed [code arithmetic: `2 · (gain / optimalGain − 0.75)`, clamped, with `optimalGain = min(0.9 / min(agcPeak, 0.9), 40)`]. **Automatic** is Beads' own: it ignores the first second, learns the next five, and settles where peaks reach 90 % of full scale (gain up to 40, 32 dB). **Manual** takes the slider, 1 to 40 times (0 to 32.04 dB). Unpatching both inputs restarts the learning. `GainControl` differs from Beads' only at its edges, listed in its header comment (float input, the gain as a number, `manual_gain_` starting at 1 so that Fixed is 1 from the first sample; upstream's global starts at 0 and fades in).

---

## 3. The right-click menu and saved state

### 3.1 Menu

[code: `Spindrift.cpp:557-578`] Items follow Rack's usual context menu, after a separator:

| Item | Choices (menu text) | Default | Field | Notes |
|---|---|---|---|---|
| Synth mode | Automatic (as Beads), Always, Never | Automatic | `synth` 0, 1, 2 | section 7.1 |
| Input gain | Fixed (±10 V is full scale), Automatic (as Beads), Manual | Fixed | `gain` 0, 1, 2 | section 2.7 |
| Manual gain | slider, 0 to 32.04 dB, shown to 3 decimals, 200 px wide | 0 dB | `manualGain` 1..40 (linear) | **moving it chooses Manual**; double-click to the default 0 dB also chooses Manual |
| Gate output (R: a gate per grain; L: the mono mix) | checkbox | off | `gateOutput` | section 2.6 |
| Keep the frozen buffer with the patch | checkbox | on | `keepFrozen` | section 3.4 |

There are no other items. Menu changes land at the next block: the settings are relaxed atomics, written by the UI thread and read by the engine once a block; the Quality long press writes `gain` from the engine's thread. [test: `test_threads` `menu()` under ThreadSanitizer.]

### 3.2 Saved JSON

[code: `SavedState.hpp`, `Spindrift.cpp:253-312`] `dataToJson` writes nine keys. The knobs are saved by Rack as ordinary params (ids 0 to 14, the buttons too).

| Key | Kind | Default | Values | On load |
|---|---|---|---|---|
| `version` | integer | 1 | 1 | **absent, not an integer, or not 1: every field takes its default** |
| `quality` | integer | 3 | 0 Dirty Tape, 1 Clean Tape, 2 Vintage Digital, 3 Digital (Beads' Media order) | outside 0..3 or not an integer: 3 |
| `seedLatched` | boolean | true | | not a boolean: true |
| `frozen` | boolean | false | a frozen buffer is in `buffer.bin` | not a boolean: false |
| `synth` | integer | 0 | 0 Automatic, 1 Always, 2 Never | outside 0..2: 0 |
| `gain` | integer | 0 | 0 Fixed, 1 Automatic, 2 Manual | outside 0..2: 0 |
| `manualGain` | **real** | 1.0 | 1..40 | **an integer, or outside 1..40: 1.0** |
| `gateOutput` | boolean | false | | not a boolean: false |
| `keepFrozen` | boolean | true | | not a boolean: true |

A default patch's `"data"` is `{"version": 1, "quality": 3, "seedLatched": true, "frozen": false, "synth": 0, "gain": 0, "manualGain": 1.0, "gateOutput": false, "keepFrozen": true}` (the order Jansson writes them in is [unverified]; the values are [meas]: `meas/misc`).

Decode rules [code; test: `test_buffer` `saved_state_*`]: a field of the wrong kind or out of range takes its default alone, and the others load. Unlike Fog's issue #12 there is no narrowing: each integer is compared as a double against its range, so `"quality": 4294967297` is rejected (default), not wrapped. A real where an integer is expected (`"quality": 2.0`) is rejected too. The golden map is in `test_buffer` (version 1; "never regenerate").

On a running module (a preset, an undo) `dataFromJson` sets the settings, the quality and Seed's latch, and leaves the buffer alone. Only a patch's load (before `onAdd`) restores a buffer, and only if `frozen` and `keepFrozen` are both true.

### 3.3 Initialize, Randomize, presets, bypass

- **Initialize** (`onReset`, `Spindrift.cpp:189`): Rack resets the 15 params to the defaults of section 2.2; the module sets the buttons to Seed latched, Digital, not frozen, no hold in progress, and the menu to its defaults (Automatic, Fixed, 0 dB, gate output off, keep on). **It does not reset the engine**: grains go on, the buffer is kept unless the quality changes (then Beads clears it and mutes 250 ms), and the mode (grain, delay, synth) stays. It releases a restored freeze. Rack's Initialize on a module that is Dirty Tape therefore sounds as a quality change.
- **Randomize**: moves the knobs (ids 0 to 11); the buttons are excluded by Rack's `configButton`. The menu is not touched by the module [unverified in Rack].
- **Presets**: none ship. A user preset loads params and the nine keys onto a running module (above).
- **Bypass**: IN L to L, IN R to R (Rack copies each input to its output). `process()` is not called while bypassed, so the buffer stops recording and the grains stop; on release the engine continues from where it was [unverified in a live Rack; the converters hold their history across the gap]. The IN R to IN L normalling that Beads has is not applied in bypass.

### 3.4 The frozen buffer kept with the patch (`buffer.bin`)

[code: `BufferFile.hpp`, `BufferSave.hpp`, `Spindrift.cpp:197-251`]

- **When it is written.** Rack calls `onSave`. If the ring has been frozen for a whole block (by the button or the gate, outside the synth) and "Keep the frozen buffer" is on, `buffer.bin` is written to the patch storage folder, **synchronously**. If it was already written for the same frozen image (`ringSeq` unchanged and the file still there), it is not written again. In any other case an existing `buffer.bin` is removed. Rack's autosave does not call this path [per the manual; unverified]. A frozen buffer saved while in the synth is not kept (Beads backs up only outside the synth, beads.cc:403-404): the file is removed and the patch says not frozen.
- **Format** (little-endian): 20-byte header then 768,016 bytes, 768,036 bytes in all.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `"SPDR"` (0x52445053) |
| 4 | 4 | version 1 |
| 8 | 1 | quality 0..3 (Beads' Media) |
| 9 | 1 | mono 0 or 1 |
| 10 | 2 | reserved 0 |
| 12 | 4 | write position, less than the ring's frame count |
| 16 | 4 | payload length, 768,016 |

  The payload is Beads' delay memory in Beads' own format: the 768,000 bytes Beads' backup saves, plus 16. Beads' 12-bit ring ends at byte 768,002, and every delay line keeps a copy of its first 4 frames after its end for Hermite reads at the wrap; 768,016 holds all of it in every format, so a reload is the ring to the bit. Ring frames, stereo: Dirty Tape 384,000, Clean Tape 256,000, Vintage Digital 256,000, Digital 192,000; mono doubles them [meas: `meas/misc`].
- **Loading.** On a patch load, `dataFromJson` records the wish (`frozen` and `keepFrozen`), and `onAdd` reads the file before the first sample. The file must have the magic, version 1, a quality 0..3 that equals the patch's `quality`, mono 0 or 1, the exact payload length, no extra bytes, and a write position inside the ring that quality and mono make. Anything else loads nothing: the buffer is empty and unfrozen, and Rack's log says why (`WARN`). On success Beads is configured for the stored quality and mono, the bytes copied in, the write pointer set, **Freeze is latched on** (as Beads does after restoring its backup), and the window's waveform is rescanned. A stored mono or quality that disagrees with the patch's jacks is held back like any change while frozen.
- **The save against the running engine** is a seqlock on the ring (`ringSeq`, odd while the ring may be written). A copy taken while the freeze ends is discarded and retried up to three times, with a warning if it never holds still [test: `test_save_race`].
- A filesystem call that throws (Rack's `system::` calls do) logs a warning and never fails the patch's save [test: `save_fs_throws`].

---

## 4. Signal flow

### 4.1 Block diagram

```
 IN L ─ sum poly ─ NaN→0 ─ clamp ±10 V ─ ×0.1 ─┐   (unpatched jack: 0, and "mono")
 IN R ─ sum poly ─ NaN→0 ─ clamp ±10 V ─ ×0.1 ─┤
                                               ▼
              EDGE CONVERTER IN (Rack rate → Beads rate 24/32/48 kHz; none if equal)
                                               │ clamp ±1 (the ADC's full scale)
                                               ▼
   in_[] fills a BLOCK of 12, 8 or 6 frames (Beads rate / 4000)
   SEED jack ─ Schmitt 1 V/0.1 V ─ per frame ─► 256-frame delay, Read(block × 10) ─► GateFlags
   FREEZE jack ─ Schmitt ─ per frame ─┐
   Freeze button ─────────────────────┴─ OR ─► freeze[] per frame
                                               │
   once per block (4 kHz): ────────────────────┤
     Buttons.tick, long-press request, menu settings read
     readControls: pots (one-pole 0.05), CV channels (clip, scale, one-pole), Pitch/attenuverter laws
     GainControl.Process (mono normalisation; Fixed / Automatic / Manual; soft clip)
                                               ▼
        muted (transition_ > 0)?  yes: zeros, count down 1,000 blocks
                                  no:  Processor::Process  (grain or delay mode)   ← vendored Beads
                                       or Synth::Render     (synth mode, 48 kHz)
                                               ▼
        clamp [−1, 0.999999]; any |sample| > 0.01 resets the synth timer
        monitorStateChange: pending quality, mono/stereo, synth policy → Configure + mute
        publishView (GrainView) · overview (waveform) · ring stability for the save
                                               ▼
              EDGE CONVERTER OUT (Beads rate → Rack rate)   gate mode: R = nearest < −0.4 ? 10 V : 0
                                               ▼  ×10 V
              FIFO of 1,024 frames (prefilled with one block of silence) ─► OUT L, OUT R
```

Two things are not in Beads: the converters and everything between the jacks and Beads' `Process`. The vendored code never sees Rack's rate, a NaN, or a voltage over the ADC's limit.

### 4.2 Block constants

[code: `SpindriftEngine.hpp:119-136`, `Controls.hpp:69-72`, `EdgeRate.hpp`]

| Constant | Value |
|---|---|
| Control blocks per second (every quality, every Rack rate) | 4,000 (0.25 ms) |
| Frames per block | 12 at 48 kHz (Digital, Clean Tape, synth), 8 at 32 kHz (Vintage), 6 at 24 kHz (Dirty Tape); `kMaxBlock` = 12 |
| Mute after a reconfiguration | 1,000 blocks (250 ms) |
| Synth timer | 40,000 blocks (10 s) |
| SEED delay | `Read(block × 10)` on a 256-frame line: 119, 79, 59 frames [meas] |
| Seed hold to latch | 16,000 ticks (4 s) |
| Quality long press | 6,000 ticks (1.5 s) |
| Output FIFO | 1,024 frames |
| Gate thresholds | high at 1 V, low at 0.1 V |
| Input full scale | ±10 V = ±1 (Beads) |
| Output clamp | [−1, 0.999999] before ×10 |
| Window grains and taps | 32 each (`GrainView::MAX`) |
| Waveform columns | 512; the engine rescans 8 ring frames per Beads frame it runs (`kOverviewBudget`) |
| Ring memory | 786,432 bytes (three 262,144-byte banks); the saved part 768,000 + 16 |
| Reverb memory | 32,768 16-bit words (65,536 bytes), shared by the processor and the synth |

### 4.3 Memory

Per instance, measured as `operator new` bytes during construction, `allocate()` and `setSampleRate()` [meas]:

| Rack rate | Heap, bytes | of which converter tables |
|---|---|---|
| 44.1 kHz | 1,345,520 | about 423.5 kB |
| 48 kHz | 909,424 | 4.4 kB |
| 96 kHz | 928,464 | 8.4 kB |
| 192 kHz | 947,568 | 15.9 kB |
| 384 kHz | 983,520 | 31.0 kB |
| 768 kHz | 1,055,424 | 61.2 kB |

The rest is the 851,968 bytes of Beads' buffers plus the engine's objects (`beads::Processor` 26,640 bytes, `Synth` 4,416, the engine 15,400, the converters' histories). All of it is allocated off the audio thread (the constructor and `setSampleRate`); `process()` allocates nothing [test: `noAllocation`, 10 s through every mode, quality and gain at 44.1, 48 and 96 kHz; `test_hostile`]. Rack-rate tables 88.2, 176.4, 352.8 and 705.6 kHz are 415 to 431 kB, like 44.1 kHz, because their ratios to 48 kHz and 32 kHz need hundreds of phases.

---

## 5. Rate conversion at the edges

### 5.1 What the converters are

[code: `EdgeRate.hpp`] Beads' code runs at 24, 32 or 48 kHz. Rack's rate is whatever the user set. `EdgeRate` holds, for each of Beads' three rates, one `Resampler` from Rack's rate to Beads' and one back. Each is a **polyphase windowed-sinc FIR (Kaiser window)**, stereo, in float:

| Design constant | Value |
|---|---|
| Pass band | up to 0.41 of the lower of the two rates |
| Stop band | from 0.5 of the lower rate (the lower Nyquist) |
| Stop-band attenuation (design) | 100 dB (Kaiser beta = 0.1102 × (100 − 8.7) = 10.06) |
| Length rule | taps W = ceil(D / transition width), D = (100 − 7.95) / 14.36 = 6.41, W made even |
| Cutoff | midway between pass and stop |
| Equal rates | **bypass**: the signal passes untouched, no filter, no delay (so 48 kHz Digital and Clean Tape are bit-exact) |
| Phases | the table holds every phase the rate ratio visits (L = out / gcd(in, out)) when L is at most 1,024: no interpolation. Otherwise P = min(1,024, max(16, 2048 × cutoff + 1)) phases, linearly interpolated between two rows (odd rates only) |

Taps and phases for the usual Rack rates [meas, `meas/edge`]; "in" is Rack to Beads (taps used once per Beads frame), "out" is Beads to Rack (once per Rack sample):

| Rack rate | Beads 48 kHz: in taps / phases, out taps / phases | Beads 32 kHz | Beads 24 kHz |
|---|---|---|---|
| 44.1 kHz | 72 / 160, 78 / 147 | 100 / 320, 72 / 441 | 132 / 80, 72 / 147 |
| 48 kHz | bypass | 108 / 2, 72 / 3 | 144 / 1, 72 / 2 |
| 96 kHz | 144 / 1, 72 / 2 | 214 / 1, 72 / 3 | 286 / 1, 72 / 4 |
| 192 kHz | 286 / 1, 72 / 4 | 428 / 1, 72 / 6 | 570 / 1, 72 / 8 |
| 384 kHz | 570 / 1, 72 / 8 | 856 / 1, 72 / 12 | 1,140 / 1, 72 / 16 |
| 768 kHz | 1,140 / 1, 72 / 16 | 1,710 / 1, 72 / 24 | 2,280 / 1, 72 / 32 |

The engine's Rack rate is clamped for the tables: NaN, zero or negative becomes 48 kHz; below 1 kHz becomes 1 kHz; above 768 kHz becomes 768 kHz; otherwise rounded to an integer [code: `clampRate`]. A repeat of the same rate rebuilds the tables anyway.

### 5.2 Latency

`latency()` is the delay from an input to its output at Dry/Wet 0, in Rack samples: the FIFO's silence (one block at equal rates, else `ceil(block × rack / beads) + 1` samples), plus half of the input kernel, plus half of the output kernel scaled to Rack samples. Excludes anything Beads itself delays (Dirty Tape's wow and flutter; the reverb). [meas: `engine.latency()`; for Digital, Clean Tape and Vintage the 1 kHz phase delay of the whole engine agrees with it to 0.01 sample at every rate listed.]

| Rack rate | Digital and Clean Tape | Vintage Digital | Dirty Tape |
|---|---|---|---|
| 44.1 kHz | 84.83 smp, 1.924 ms | 112.61 smp, 2.554 ms | 145.15 smp, 3.291 ms |
| 48 kHz | **12.00 smp, 0.250 ms** | 121.00 smp, 2.521 ms | 157.00 smp, 3.271 ms |
| 88.2 kHz | 156.15 smp, 1.770 ms | 222.23 smp, 2.520 ms | 287.30 smp, 3.257 ms |
| 96 kHz | 169.00 smp, 1.760 ms | 240.00 smp, 2.500 ms | 312.00 smp, 3.250 ms |
| 176.4 kHz | 309.30 smp, 1.753 ms | 441.45 smp, 2.503 ms | 572.60 smp, 3.246 ms |
| 192 kHz | 336.00 smp, 1.750 ms | 479.00 smp, 2.495 ms | 622.00 smp, 3.240 ms |
| 384 kHz | 670.00 smp, 1.745 ms | 957.00 smp, 2.492 ms | 1,243.00 smp, 3.237 ms |
| 768 kHz | 1,339.00 smp, 1.743 ms | 1,912.00 smp, 2.490 ms | 2,485.00 smp, 3.236 ms |

The test checks that the sine's lag matches `latency()` to 0.02 sample (Digital, Clean Tape, Vintage; 0.1 sample for Dirty Tape, whose wow and flutter moves its delay) at 44.1, 48 and 96 kHz [test: `rates`, `passband`]. The latency is on both the dry and the wet path, so Dry/Wet does not comb; it matters when a patch also takes the dry signal around Spindrift by another route.

### 5.3 Response

[meas, `meas/edge`; test: `edgeRateAlone` and the round trip in `test_alias`]

- One direction, tones at 0.10, 0.30, 0.40 and 0.41 of the lower rate: gain 0.000 dB (within 0.001 dB) at every pair of 44.1, 48, 96 and 192 kHz against 24, 32 and 48 kHz.
- At 0.45 of the lower rate: **−3.9 dB** each way (−3.907 typical; −3.89 to −3.93), so −7.8 dB there on the round trip. This is the transition band. At Rack 44.1 kHz in Digital or Clean Tape it puts the roll-off at 18.1 kHz (−3.9 dB at 19.8 kHz); at 48 kHz in those two qualities there is none.
- The round trip (Rack to Beads to Rack) at 0.38 of the lower rate: level within 0.0001 dB, other components at or below −114.7 dB re the tone [test: bound −90 dB, level within 0.01 dB].
- Tones between the two Nyquists and above: every other component is at or below **−106.7 dB** re the tone, in either direction [test: bound −90 dB; `--table` worst −106.77 dB, 44.1 to 48 kHz out at 23.8 kHz].
- Whole engine, 1 kHz sine at Dry/Wet 0 against Beads' own output at its rate: Dirty Tape +0.027 dB (48 kHz) and +0.042 dB (44.1 kHz); Vintage within 0.005 dB [test bound 0.1 dB, `--table`].

### 5.4 FIFO, restarts and underruns

The converters and the FIFO between Beads' blocks and Rack's samples restart from silence whenever the Beads rate changes or the engine is reconfigured (`select()` clears both histories and refills the FIFO with silence). A `process()` call during which a restart happened returns zero for that sample, so the latency stays what `latency()` says. A mute follows every restart except a rate change while frozen. The FIFO never ran dry or over in any test or measurement: `underruns()` is 0 at 44.1, 48 and 96 kHz in every quality [test: `rates`, `noAllocation`, `test_alias`, `test_hostile`] and at 13 rates from 1 kHz (the clamp) to 768 kHz, including 22,050, 44,117, 47,999, 48,001 and 100,000 Hz, which use interpolated phases [meas: `meas/odd`].

### 5.5 Set-up cost

`setRackRate` builds all six tables on whichever thread calls it: the constructor and Rack's `onSampleRateChange`, under Rack's exclusive engine lock, never during `process()`. Measured on the M-series Mac, `setSampleRate()` takes **2.42 ms at 44.1 kHz** (the sinc tables, with libm), 0.03 ms at 48 kHz, 0.06 ms at 96 kHz, 0.33 ms at 768 kHz; `allocate()` takes 0.07 to 0.15 ms [meas]. Memory by rate: section 4.3.

### 5.6 What a rate change does

[code: `setSampleRate`; test: `rates`; meas]

| State at the change | Result |
|---|---|
| Unfrozen, grains or delay | Beads is reconfigured (the buffer is cleared), the converters restart, **250 ms mute**. Measured at 48 → 44.1 kHz: silent from the first sample for 230.0 ms, then sound. One mute, buffer cleared (the old grains' RMS is 1e-3 V or under afterwards; an engine that stayed keeps 0.2 V or more) [test] |
| Frozen (button or gate) | **No clear, no mute**: Beads' buffer is in Beads' rate, not Rack's. The converters restart, so there is a short gap while the FIFO's silence and the kernels fill (the latency of section 5.2). After 48 → 44.1 → 48 kHz inside one block boundary the output is sample for sample the same as an engine that never changed (after one block) [test]. Frozen through 48 → 44.1 kHz the grains go on within 0.5 dB of an engine that stayed [test] |
| Synth | restarts the codec at 48 kHz, 250 ms mute |
| Before the first sample (not booted) | tables only |

How Rack orders the rate-change event against `process()` is [unverified].

---

## 6. Quality modes

### 6.1 The four qualities

The Quality button steps Digital, Vintage Digital, Clean Tape, Dirty Tape, and round (section 2.5). The saved `quality` is Beads' enum (Dirty Tape 0 to Digital 3). Beads sets the rate, bit depth and processing; the engine sets the block, the converters and the gain stage. [code: `processor.h:78-94`, `BufferFile.hpp:56-61`, `SpindriftEngine.hpp:308-316, 491-496`; the manual for Beads' side]

| | Digital | Vintage Digital | Clean Tape | Dirty Tape |
|---|---|---|---|---|
| Press order from the start | 1st (the start) | 2nd | 3rd | 4th |
| Saved `quality` | 3 | 2 | 1 | 0 |
| Beads rate | 48 kHz | 32 kHz | 48 kHz, decimated 2:1 inside (buffer at 24 kHz) | 24 kHz (the dry path too) |
| Frames per block | 12 | 8 | 12 | 6 |
| Buffer samples | 16-bit | 12-bit (the dry input also quantised to 12 bits) | 12-bit | 8-bit mu-law, with wow and flutter on the whole output |
| Ring frames, stereo (mono: ×2) | 192,000 | 256,000 | 256,000 | 384,000 |
| Buffer length, stereo | 4.000 s | 8.000 s | 10.667 s | 16.000 s |
| Buffer length, mono | 8.000 s | 16.000 s | 21.333 s | 32.000 s |
| Input stage (Fixed gain) | hard clip ±10 V | hard clip | clip, plus a soft clip from about 6.75 V, at most half crossfaded | clip, plus a soft clip always on |
| Synth resolution | 16-bit | 7-bit | 16-bit voice, 6-bit grains | 4-bit |
| Converters at Rack 48 kHz | none (bit-exact) | 48→32 and 32→48 | none | 48→24 and 24→48 |
| Converter pass edge at Rack 44.1 kHz | 18.1 kHz | 13.1 kHz | 18.1 kHz | 9.8 kHz |
| Converter pass edge at Rack 48 kHz | no filter | 13.1 kHz | no filter | 9.8 kHz |
| Converter pass edge at Rack 96 kHz and over | 19.7 kHz | 13.1 kHz | 19.7 kHz | 9.8 kHz |

The buffer lengths are asserted two ways: `bufferSeconds()` against the formula to 1e-4, and the record head's lap time against the formula to 0.2 % [test: `monoWhenEitherUnpatched`]. All eight values printed by `meas/misc` agree. The synth's resolution row is Beads' [manual]; not separately verified.

### 6.2 What reconfigures, and the mute

A reconfiguration is Beads' `Configure(quality, mono)`, which clears the buffer, followed by a codec restart (section 5.4) and a **250 ms mute** (1,000 blocks of zero output; the Quality cap is lit meanwhile). Nothing is crossfaded. [code: `startCodec`, `monitorStateChange`; meas at Rack 48 kHz, 5 V sine, Dry/Wet 0]

| Cause | Measured silence | Buffer |
|---|---|---|
| Quality button, short press | first zero 11 samples after the press; 250.3 ms | cleared |
| Three presses before the next block (ends on Dirty Tape) | 252.4 ms: one reconfiguration, to the last choice | cleared |
| IN R unpatched (stereo to mono) | 250.3 ms | cleared |
| Rack rate change, unfrozen | 230.0 ms | cleared |
| Enter the synth (10 s timer) | 250.0 ms | the processor's buffer is left as it is |
| Leave the synth (patch an input, or menu Never) | 250 ms [test: `synthAfter10s`, 0.001 s] | cleared (reconfigured) |
| Menu: Always, on a running module | mute [test] | |
| Engine start, or the first block | no mute | |

During a mute the engine feeds the gain stage a "not active" flag and Beads is not run; the window draws no grains. A press during a mute applies after it.

### 6.3 Frozen: the change waits

While the engine is frozen (any frame in the block frozen, by the button or the gate), `monitorStateChange` returns before anything is applied, and the 10 s synth timer is held off. A Quality press in that time changes only `pending` (shown in the window as "DIGITAL → DIRTY TAPE"); further presses step the pending choice (last wins). The same holds for a mono/stereo change and a synth switch. At the first unfrozen, unmuted block the pending quality is applied together with whatever mono and synth change is due: one reconfiguration, one mute. [test: `deferredUntilUnfreeze`, by button and by gate: no mute while frozen, still Digital stereo, `pending` Dirty Tape, the frozen buffer still plays at 0.2 V RMS or more; on release one mute, Dirty Tape, mono, 24 kHz; `test_controls` for the pending logic.] A saved patch stores the **current** quality, not the pending one [code].

### 6.4 Mono and stereo

Beads runs mono when either audio input is unpatched; the patched side feeds both (L copied to R, or R copied to L) [test: `monoWhenEitherUnpatched`]. Plugging or unplugging one input while the other stays patched is a mono/stereo change: a clear and a mute, once per change, none while frozen [test: `plugUnplugStorm`, 60 s, toggling every 3 s]. With neither input patched nothing changes, and after 10 s the synth may start (section 7.1). The mono buffer holds twice as long (section 6.1). The grains are still panned in stereo. There is no level compensation for mono.

### 6.5 Levels per quality

[meas: `meas/engine`, Rack 48 kHz, 1 kHz sine into both inputs, Dry/Wet 0, grain mode, Fixed gain; 1 s settle, peak over the second second]

| Input peak | Digital out peak | Vintage | Clean Tape | Dirty Tape |
|---|---|---|---|---|
| 0.5 V | 0.375 V | 0.373 | 0.375 | 0.371 |
| 2 V | 1.500 | 1.500 | 1.500 | 1.466 |
| 5 V | 3.750 | 3.748 | 3.750 | 3.428 |
| 8 V | 6.000 | 5.998 | 6.000 | 4.969 |
| 10 V | 7.500 | 7.498 | 7.500 | 5.734 |
| 15 V | 7.500 (clipped at ±10 V in) | 7.548 | 7.500 | 5.761 |

Digital's ratio is 0.750 at every level up to 10 V. Clean Tape's peak is 0.750 too, but its RMS falls under the Digital figure once the soft clip acts: at 8 V peak 4.088 V RMS against 4.243 V (−0.32 dB), at 10 V 4.786 V against 5.303 V (−0.89 dB). Dirty Tape's ratio falls from 0.741 (0.5 V) to 0.573 (10 V). Vintage's 0.5 V ratio is 0.747 (the 12-bit input quantiser). The 15 V Vintage peak of 7.548 V is the output converter overshooting by 0.6 %.

---

## 7. Modes: grain, delay, synth

### 7.1 Synth mode: policy, timer, mutes

[code: `monitorStateChange`; test: `synthAfter10s`, `synthPolicyAlwaysNever`; meas]

| Policy (menu) | Behaviour |
|---|---|
| **Automatic (as Beads)**, the default | Grain mode at start, with no mute. With **neither input patched**, unfrozen and unmuted, a timer starts at the first such block and counts 40,000 blocks; at the end the engine enters the synth (`startCodec`: 250 ms mute, converters at 48 kHz). The timer resets to the start whenever an input is patched, the engine is frozen or muted, or **any output sample in a block exceeds 0.01 of full scale (0.1 V)**: a sounding tail, a feedback loop or reverb keeps the synth away. Patching either input leaves the synth at the next block (one mute, the buffer cleared). |
| **Always** | Synth from the first sample, with no mute, whatever is patched. On a running module the menu switch enters it with a 250 ms mute. |
| **Never** | Grain mode always. Switched on in the synth, it leaves with a mute. |

Measured at Rack 48, 44.1 and 96 kHz with nothing patched: the synth starts at **10.0002 s** and unmutes at 10.2502 s (mute 250.0 ms) [meas; test: at or over 10.0 s and under 10.001 s]. The mute outputs exact zeros [test]. The synth sounds after the mute (RMS over 0.5 V at its default) [test]. In the synth the Quality button changes the resolution live, with no mute; Freeze holds the voice. A synth-mode buffer is not kept with the patch (section 3.4).

### 7.2 Delay mode

Size past 0.99 of travel (back to grains below 0.98): Beads waits 1.5 s with the knob there (`kDelayModeTransitionTime`, `processor.cc:37`), fading the grains out over the second half, then fades the delay in over 0.75 s (`kDelayModeFadeTime`). The engine plays no part: no mute, no reconfiguration, the buffer is the same one. Only the knob does this; the Size CV never enters delay mode [manual]. The window changes to "DELAY" and shows the taps. The bench and the equivalence tests cover it (sections 10 and 13).

---

## 8. Triggers and timing

### 8.1 The SEED delay

Beads reads the SEED input 2.5 ms late so that CVs that move with a trigger have settled. The engine reproduces `event_delay.Read(size × 10)` on a 256-frame line: **119 frames at 48 kHz (2.479 ms), 79 at 32 kHz (2.469 ms), 59 at 24 kHz (2.458 ms)** from the frame the pulse arrives [meas; the manual says 119, 79 and 59]. It is asserted against the upstream code to the bit; a reference given the line one frame shorter or longer, or no delay, differs [test: `seedDelay`]. With nothing in SEED the flags are low and the delay line is not fed.

### 8.2 FREEZE

The button and the gate OR per frame. `frozen()` (the FREEZE light) is true if any frame of the block was frozen; the ring counts as stable (for the save) only when **every** frame was [code].

### 8.3 How short a pulse is seen

TRIG and GATE are sampled once per Beads frame. A pulse that falls between two Beads frames is not seen. [meas: Seed unlatched, Density 0.5, a SEED pulse of N Rack samples at 40 phases; "seen" = the Seed light's flash; `meas/trig`]

| Rack rate | Digital (frames 20.8 µs): shortest always seen | Dirty Tape (frames 41.7 µs): shortest always seen |
|---|---|---|
| 44.1 kHz | 1 sample (23 µs): 40 of 40 | 2 samples (45 µs); 1 sample 22 of 40 |
| 48 kHz | 1 sample (21 µs): 40 of 40 | 2 samples (42 µs); 1 sample 21 of 40 |
| 96 kHz | 2 samples (21 µs); 1 sample 21 of 40 | 5 samples (52 µs); 2 samples 21 of 40; 1 sample 14 of 40 |
| 192 kHz | 4 samples (21 µs); 2 samples 21 of 40; 1 sample 14 of 40 | 10 samples (52 µs); 4 samples 20 of 40; 1 sample 7 of 40 |
| 384 kHz | 8 samples (21 µs); 4 samples 20 of 40; 1 sample 7 of 40 | 19 samples (49 µs); 8 samples 20 of 40; 1 sample 3 of 40 |

A 1 ms pulse was seen 40 of 40 at every rate and quality. Rack's usual 1 ms triggers are safe; a one-sample trigger at 96 kHz or more is not.

### 8.4 Holds

Seed's 4 s and Quality's 1.5 s are counted in the engine's 4 kHz ticks, so they are exact whatever Rack's rate and the quality. The Seed light's breathing is Beads' (a triangle of period 2.048 s, scaled 3/4 plus 64 of 255), counting ticks as milliseconds [code: `Controls.hpp:139`].

---

## 9. The window

[code: `GrainView.hpp`, `widgets.hpp` `GrainWindow`; spec "The window"; test: `test_view`, `test_threads`]

- **Data path.** Once a block the engine publishes into a `GrainView` (relaxed atomics): up to 32 grains (position, span, pan, envelope), up to 32 taps (position, gain), the record head, mode, quality, mono, buffer seconds, the synth bank and wave, frozen, and any pending quality. The widget copies it every frame. The waveform is an `rka::Overview` of 512 columns over the ring in Beads' own stored format (quantised, mu-law, after feedback), numbered in write order so the head moves across a fixed picture; the engine updates it from 8 ring frames per Beads frame it runs and rescans a restored buffer in 0.5 to 4 s. No mouse actions.
- **Captions.** Top left: quality, STEREO or MONO, and the length, "DIGITAL · STEREO · 4.0 S"; a quality waiting for the freeze "DIGITAL → DIRTY TAPE"; in the synth, the quality alone. Top right: "GRAINS" with a number, "DELAY", or "SYNTH · BANK" with 1 to 12. The word stays still; the number has a fixed slot (widest "32" or "12"); the grain count shown is the most that sounded in each 0.2 s (`CountHold`). In a mute no grains are drawn.
- **Without a module** (the browser, screenshots) the widget draws a stored picture: a phrase, the head at 0.64, twelve grains.
- The font is D-DIN Condensed, loaded at draw time from `tools/fonts/` in the package.
- The window's frame cost in Rack is [unverified]; the engine's share is inside the bench.

---

## 10. CPU

### 10.1 The bench

`make bench` runs six cases per module, at 1, 4 and 16 instances, 48 kHz, declared in `src/spindrift/bench.inc` [code]:

- **idle**: nothing patched, defaults (Density 0.65 and Dry/Wet 0.5, so grains run on silence), synth set to Never.
- **outputs**: Digital, Density 0.85, Size 0.95 (30 to 32 of Beads' 32 grains sounding), ±5 V noise into both inputs, Dry/Wet 0.5.
- **busy**: outputs, plus Feedback 0.5, Reverb 0.6 and a moving CV into all eight CV jacks.
- **dirty**: outputs, in Dirty Tape (through the converters).
- **delay**: Size fully clockwise, Density 0.8, Feedback and Reverb 0.5.
- **synth**: the menu's Always, Density 0.85, Reverb 0.5.

The libm check (`make libm-calls`) lists all six as knobs-still but busy. Budgets are 1.5 times the most the Mac measured over three runs at 1, 4 or 16 instances (2026-11-22), rounded up to 0.05; the other-machine budgets are 3 times the Mac's and provisional. The bench fails if a case exceeds its budget at 1 or 16 instances, or if 16 instances cost more than 1.5 times one plus 0.01 %. Units: % of one core per instance.

| Case | Budget, Apple arm64 | Budget, other (provisional) | Mine, 1 instance, runs 1 / 2 / 3 [meas] | Mine, 16 instances, run 3 | TESTING.md and manual (1 instance) |
|---|---|---|---|---|---|
| idle | 0.4 | 1.2 | 0.272 / 0.279 / 0.246 | 0.242 | 0.245; manual 0.25 |
| outputs | 2.05 | 6.15 | 1.507 / 1.527 / 1.401 | 0.854 | 1.347; manual 1.35 |
| busy | 2.1 | 6.3 | 1.558 / 1.613 / 1.467 | 0.987 | 1.385; manual 1.39 |
| dirty | 0.95 | 2.85 | 0.689 / 0.725 / 0.642 | 0.481 | 0.620; manual 0.62 |
| delay | 1.15 | 3.45 | 0.809 / 0.863 / 0.758 | 0.571 | 0.736; manual 0.74 |
| synth | 0.35 | 1.05 | 0.262 / 0.264 / 0.242 | 0.244 | 0.230; manual 0.23 |

Three consecutive `build/bench/bench` runs on an Apple M-series Mac while other agents were building (load average 4 to 8). Every Spindrift case was within budget in all three. Run 1 printed "bench: FAILED" because of Eddy's busy case (1.731 % at 16 instances, over its budget), not Spindrift's. Per-instance cost falls as instances are added (outputs 1.507, 1.351, 0.964 at 1, 4, 16 in run 1), never grows. Run 3, the quietest, is within 0 to 6 % of the developer's figures; runs 1 and 2, under more load, are 12 to 17 % over them. That is the machine, not a regression.

### 10.2 CPU against Rack's rate

[meas: `meas/engine`, one instance, Digital or Dirty Tape, Density 0.85, Size 0.95, ±5 V noise into both inputs, Dry/Wet 0.5; best of three one-second runs, load average about 4.] Rack's rate matters, because the input kernel has up to 2,280 taps and the output kernel runs once per Rack sample. The bench runs at 48 kHz only.

| Rack rate | Digital, fast-math (the brief's flags) | Digital, **plugin's flags** (strict) | Dirty Tape, fast-math | Dirty Tape, plugin's flags |
|---|---|---|---|---|
| 44.1 kHz | 1.06 % | 1.27 % | 0.55 % | 0.70 % |
| 48 kHz | 0.99 % | 1.08 % | 0.56 % | 0.71 % |
| 96 kHz | 1.17 % | 1.48 % | 0.66 % | 1.04 % |
| 192 kHz | 1.30 % | 2.03 % | 0.78 % | 1.73 % |
| 384 kHz | 1.71 % | 3.55 % | 1.16 % | 2.89 % |
| 768 kHz | 2.31 % | 5.91 % | 1.75 % | 5.41 % |

The plugin's Makefile appends `-fno-unsafe-math-optimizations` to `Spindrift.cpp`'s flags (`SPINDRIFT_PLUGIN_FLAGS`), so the converters' sums are not reassociated or vectorised as the bench's fast-math build does (`tests.mk` says the bench times a fast-math build of the engine's glue; Beads' own object is built strict in both). The second and fourth columns are therefore the closer figure for what ships: **at 192 kHz and over the converters cost more than the grains**, and one instance takes about 6 % of a core at 768 kHz. A patch with several Spindrifts at a high rate needs checking on the target machine. The Windows build uses the same flags; its CPU is not measured here [unverified]. My 48 kHz loop reads lower than the bench's `outputs` (0.99 to 1.08 against 1.40 to 1.53); I did not find why [unverified].

The engine's per-sample code calls no libm (the only `std::sin` is in the table build, `EdgeRate.hpp:102`, off the audio thread) [code]. The libm check itself is Linux only [issue #36], so I did not run it, and Beads' own object was not checked here.

---

## 11. Sample rates and robustness

### 11.1 Sample rates

- Any Rack rate is accepted; the tables are built for it (section 5.1 for the clamps). Tested: 44.1, 48 and 96 kHz in every quality and mode (test), the full range in my loop, and odd rates (section 5.4).
- The engine holds one rate; a change is a restart (section 5.6).
- At each rate the 1 kHz sine at Dry/Wet 0 leaves at 0.75 of its level, within 0.1 dB of Beads' own, in Digital, Vintage and Clean Tape; Dirty Tape within 0.05 dB of Beads' own at its own rate (section 5.3) [test: `rates`, `passband`].

### 11.2 Hostile input

[test: `test_hostile`, `--table`] A ±12 V sine, +12 V and −12 V held, NaN, +infinity and −infinity on each of the 12 input jacks in turn (IN L, IN R, the eight CVs, FREEZE, SEED), 1 s each, in grains, delay and synth, in every quality, at Rack 44.1 kHz (so every quality goes through the converters): 864 segments. In all of them the output is finite and within ±10.5 V (the worst peak is **10.134 V**, delay mode, Digital, Feedback +12 V; the converter overshoot, section 2.6). One second later (six seconds in delay mode, whose limiter releases slowly) the output's RMS on the same sane input is within **−0.83 to +0.52 dB** of a fresh engine's, over 144 recoveries; the bound is 1 dB. Nothing allocates. The guards sit in the engine, before Beads: audio is held to ±10 V and NaN read as 0 V, CVs held to their range and NaN read as 0 V, gates keep their state through NaN. The vendored code is never given a NaN.

### 11.3 No allocation, no global state, threads

- **No allocation** in `process()`: counted per thread across 10 s of every mode, quality and gain at 44.1, 48 and 96 kHz [test: `noAllocation`], and in the hostile run.
- **No writable global state**: Beads' only mutable global (stmlib's `Random` state) lives in the instance's `BeadsContext`, reached through one thread-local pointer (`spindrift::current()`) that `BeadsScope` sets; `make spindrift-globals` allows that one symbol and nothing else, and `make engine-globals` covers the rest [test, both exit 0]. `Random` asserts that a context is current, so a call into Beads outside a scope fails the tests instead of dereferencing null.
- **Instances are independent**: the module seeds each from `random::u32()`. Two engines with different seeds make different clouds (more than 90 % of sounding samples differ); two with the same seed, run interleaved, make identical output [test: `test_vendor`].
- **Threads**: the window's reads under ThreadSanitizer (4.0 s of engine, 191,275 copies, no race, every copy drawable); the menu's writes (2,394,194, each seen) and 31 Quality long presses from the engine's thread; the save against the running engine (a deliberate, detected race on the ring's bytes, under ASan and UBSan): 137,844 samples, 448 stable rings, 1,844 images returned, 7 discarded as written meanwhile, 764,057 refused as unfrozen; every image returned equals the ring as it stood at its sequence number [test].

---

## 12. Open issues

### 12.1 Filed issues

`gh issue list --state all --limit 200` (repo `ryanarneson/RkaModules`) has **no issue about Spindrift**; the search "spindrift OR Beads" returns only #61. Issues that touch it:

| # | Title | State | QA impact on Spindrift |
|---|---|---|---|
| 36 | Process: libm-calls check is skipped on macOS | open | Spindrift's libm check ran only on Linux CI; I could not run it |
| 64 | Polyphony: modules should be polyphonic where it makes sense | open | Not applicable by the spec (Beads is one stereo instrument): audio summed, CVs and gates channel 1 |
| 67 | Aliasing: measure every module that makes or bends sound | open | Spindrift already has its aliasing test (section 13.3); its table predates it and does not list Spindrift |
| 61 | DelayLine: fractional reads lose precision at long delays (before Eddy) | open | Says it "matters for Eddy and Spindrift". It does not: Spindrift uses Beads' own delay line, not `src/DelayLine.hpp` (no include of it in `src/spindrift/`) [code] |

### 12.2 Found while preparing this document (not filed)

For the developer to file or dismiss. Each is cited above.

1. **`test_vendor`'s Dirty Tape rate.** Its table (`test_vendor.cpp:40`) runs Dirty Tape at 48 kHz in 12-frame blocks, with a comment calling it the shipping firmware; the shipping rate is 24 kHz in 6-frame blocks. The bit-exact comparison still passes at 24 kHz (I ran a copy with the table changed) [meas]. The engine's Dirty Tape path is covered against Beads only by `test_alias` (aliasing and level), not bit for bit (section 14).
2. **The bench and the plugin are built with different float flags** for the engine's glue (section 10.2). At Rack 48 kHz the difference is about 9 %; at 192 kHz and over it is 1.5 to 2.6 times, and the bench runs only at 48 kHz.
3. **Manual: memory.** "About 850 kB" is Beads' buffers; the instance is 0.89 MB at 48 kHz and 1.31 MB at 44.1 kHz.
4. **Manual and listening guide: "fully dry comes out at 0.75".** Not in Dirty Tape (0.74 down to 0.57 as the level rises), and not in Clean Tape's RMS above about 6.8 V peak.
5. **Manual: converter band.** 19.7 kHz for Digital and Clean Tape holds at Rack 96 kHz and over; at 44.1 kHz it is 18.1 kHz, and at 48 kHz there is no filter.
6. **Manual: "a CV takes a few milliseconds to land".** Density and Pitch CV are not smoothed.
7. **No test pins the param, input, output and light ids** (TESTING.md rule 5 says existing ids never change number; Fog pins its with `static_assert`).
8. **The engine against the reference is bit-compared only at 48 kHz Digital**, where nothing is converted. The converters' own tests and the aliasing comparison cover the other paths.

### 12.3 Known limits that are not defects

- Beads' aliasing is kept on purpose and can be large: the synth at +60 semitones (4.19 kHz) has an alias at −20.4 dB re full scale, at +72 (8.37 kHz) −25.1 dB (Beads' own figure, matched by Spindrift to 0.3 dB); grains at +24 semitones alias at −46 to −64 dB [test: `--table`]. Do not file these.
- A quality change, a mono/stereo change, a rate change and the synth switch each clear the buffer and mute 250 ms. Beads does the same; nothing is crossfaded.
- Dirty Tape is quieter than 0.75 and hissy (Beads' own).
- A SEED or GATE pulse shorter than one Beads frame can be missed (section 8.3).
- The output may exceed ±10 V by about 1.3 % at 44.1 kHz, by the converter's overshoot, with a hard clip before it.

---

## 13. What the automated suite already covers

All of the tests below passed on the 2.0.39 tree in my run. Build: `make -f tests.mk` (the repo's own makefile for the offline tests) with GCC-style flags as in `tests.mk`: `-fwrapv -fno-strict-aliasing -fno-unsafe-math-optimizations -funsigned-char -DTEST`, `-O1 -g`, AddressSanitizer and UBSanitizer (`test_threads` ThreadSanitizer) on Apple clang, C++17. These are the plugin's arithmetic flags, so the tests see the plugin's floating point. Run times with all nine in parallel on a loaded machine: `test_controls` under 1 s, `test_view` 1 s, `test_buffer` 1 s, `test_save_race` 4 s, `test_threads` 4 s, `test_engine` 7 s, `test_alias` 13 s, `test_vendor` 26 s, `test_hostile` 28 s. The tests print "all checks passed" only; they print no counts.

Also run [test]: `tools/check_spindrift_vendor.sh` ("51 vendored files match upstream but for marked changes; the reference is upstream's"), `make spindrift-globals`, `make engine-globals` and `make bench-modules` ("15 module declaration(s) complete") passed; `make libm-calls` skipped (needs GNU ld; Linux and CI). I did not run `make test` as a whole, `make`, or `make smoke`.

### 13.1 The files

| Test | What it covers |
|---|---|
| `test_controls` | the attenuverter law against `cv_reader.h`; the detune law's plateaus; Seed latched at reset, a press unlatches, a 4 s hold and Seed + Freeze latch; Quality steps Digital, Vintage, Clean, Dirty; the long press sets automatic gain and does not step; a choice made frozen is pending and the last wins; the pot filter and the Seed light |
| `test_engine` | the engine against the reference (13.2); the SEED delay; `mixParameter`'s constants; the CV scaling; levels (+10 V and −5 V DC give 0.75 and −0.375 of 10 V; a 5 V sine leaves at 3.75 V; +15 V and −20 V clip at ±10 V in; gate output 0 and 10 V only); synth after 10 s (and not at add; not at 9.9 s; the 250 ms mutes both ways with exact zeros; a sounding output holds it off); the menu policies; mono when either input is unpatched, and buffer lengths per quality; a quality change clears and mutes; a change made while frozen waits (button and gate); a plug-unplug storm; the rate set (1 kHz level within 0.1 dB, delay equals `latency()` to 0.02 sample, a rate change frozen and not); no allocation |
| `test_vendor` | the vendored Beads against upstream bit for bit (13.2) |
| `test_alias` | aliasing against Beads' own, the converters alone, the 1 kHz passband and delay (13.3) |
| `test_hostile` | section 11.2 |
| `test_buffer` | the golden version 1 `buffer.bin` loads to the bit; a saved frozen engine's image through a real file into a new engine, which then plays what Beads' own restore plays, for 2 s; files that do not fit (bad magic, version, quality, mono, length, write position, a quality other than the patch's, truncated, long, missing) load as an empty unfrozen buffer; frozen in the synth is not kept; filesystem calls throwing; the JSON: golden defaults, every field round-tripped away from its default, the golden version 1 map, every way a field can be wrong |
| `test_save_race` | section 11.3 (skipped, saying so, on one hardware thread) |
| `test_threads` | section 11.3, under ThreadSanitizer |
| `test_view` | the captions, the geometry of grain dashes and tap ticks, the synth's wave, the waveform, the mode caption's fixed slot, the 0.2 s count hold |

### 13.2 Vendor equivalence

This is the test that says "sounds like Beads". Four layers [test unless marked]:

1. **The files are upstream's** (`tools/check_spindrift_vendor.sh`). Upstream is `pichenettes/eurorack` at `f4cfa76` with `stmlib` at `e3bd7c9`, read from the git objects of `~/ws/plugins/eurorack`. Every difference between `src/spindrift/{beads,stmlib}/` and upstream must be on a line carrying `SPINDRIFT:` (or in a file listed under "Files rewritten" in `CHANGES.md`: none yet); `testdata/ref/` must equal upstream byte for byte. 51 vendored files, 51 reference files. Without the checkout (CI) the check says "skipped".
2. **The vendored code is bit-identical to the reference** (`test_vendor`, `matchesRefEveryMode`). The reference is the untouched upstream copy compiled under other namespaces (`-Dbeads=beadsref -Dstmlib=stmlibref`, `RefBeads.cpp`), with upstream's one global `Random` seeded 0x5eed and each comparison run alone. The vendored side has its own context and the same seed. Both are driven the way `beads.cc` drives them, block by block, with the same input, parameters, Seed flags and freeze; every output float is compared with `==`. For each of Dirty Tape, Clean Tape, Vintage Digital and Digital, stereo and mono: twelve 3 s segments (grains at Density 0.2, 0.5 with Seed pulses every 50 ms, 0.8; each again with CVs; Seed held; Seed locked with a 0.125 s clock; frozen halfway; delay mode plain, clocked, frozen halfway; every attenuverter at ±0.6), and each segment must make grains or taps, and the run's RMS exceed 0.01. Then the synth at its four resolutions, 3 s, with the bank swept, a clock, a freeze, a Pitch CV and gate output. 36 s a run, eight runs and four synth runs. **Nothing in the marked changes alters a sample**, which is the claim of `CHANGES.md`. `test_vendor` runs Dirty Tape with 48 kHz timing (12.2 item 1); a copy of it with the shipping 24 kHz timing (6-frame blocks) also matches bit for bit [meas: `meas/vendor24`].
3. **The engine is Beads' output at Rack 48 kHz Digital** (`test_engine`, `matchesRefAtNativeRate`). The reference is driven by the test's own copy of `beads.cc`'s control path (pot laws and filters, the calibrated CV channels, the SEED delay), written from the firmware side by side, one block (12 frames, the reported latency) late. Nine comparisons: grains in stereo with every CV moving, a clock in SEED, a freeze gate and moving knobs; delay mode; mono; gate output; Seed unlatched; the Freeze button; the synth with its bank swept; the SEED delay; and CV scaling (Pitch 1 V is 12 semitones, the others ±5 V is ±1). The tolerance is 1e-6 of full scale (10 µV). **Measured worst differences: 1.5e-8 to 6.0e-8 of full scale (0.15 to 0.6 µV)**, in all nine [meas: a copy of the test printing `c.worst`; the manual says "within a millionth of a volt"]. The comparisons also fail when they should: a reference given the SEED line 0, one frame shorter, or one frame longer differs; a reference at 11 semitones a volt differs.
4. **Two instances are independent** (13.1, section 11.3).

**Mutations shown to fail** [test; I ran each in a throwaway copy of the tree, not the repo]:

| Change | Result |
|---|---|
| in `beads/dsp/grain_envelope.h`, the minimum attack 0.0005 s to 0.0006 s (one constant, 20 %) | `test_vendor` fails 12 of its 12 runs (the eight quality and channel combinations at the first grain segment, for example 1.04 s in Dirty Tape stereo, and the four synth resolutions at 0.5 s); `test_engine` fails 5 of its 9 comparisons (grains 2.25 s, delay mode 1.42 s, mono 2.50 s, synth 0.0027 s, the SEED delay); `test_alias` still passes, as it must: its bound is Beads' own aliasing and the change is too small to move that. The check script would also report the edit as an unmarked difference [by its rule; not run] |
| in `EdgeRate.hpp`, the converters' design attenuation 100 dB to 30 dB | `test_alias` fails 50 checks (the edge-converter bounds at −90 dB, the 1 kHz level and the aliasing against Beads at 44.1 kHz); `test_vendor` and `test_engine` still pass, because the 48 kHz Digital path has no converter |
| from the developer's commit message (`38c6dd1`), not re-run: `test_save_race` without the seqlock's second read reports 207 torn images of 81,673 | |

The spec's named mutation, breaking the edge converters (Rack at 44.1 kHz into Beads' 48 kHz without filtering), is the second row.

### 13.3 Aliasing (`test_alias`; TESTING.md rule 16)

Measured with `rkatest::AliasMeter` at Rack 44.1 and 48 kHz, in each quality, against Beads' own code run at its own rate on the same input and seed: the bound is **Beads' figure plus 3.0 dB**, so the test fails if Spindrift aliases more than Beads, not if Beads aliases. Sounds: a 2.3 kHz, 5 V sine through regular long grains at Pitch 0, +12 and +24 semitones (Time 0.05, Size 0.7, Density 0.15, Dry/Wet 1); the wavetable voice at 130.8 Hz, +24, +48, +60 and +72 semitones in each resolution (Dry/Wet 0); each converter pair alone (absolute bound −90 dB; passes with 0.38 of the lower rate through, and stops tones between the Nyquists); the round trip (level within 0.01 dB); and the 1 kHz passband and delay (section 5.2). Excess over Beads [test `--table`]: worst **+0.84 dB** (grains at +12, Dirty Tape, 44.1 kHz) and +0.76 dB (synth, Dirty Tape, 44.1 kHz); 0.00 dB at 48 kHz in Digital and Clean Tape (bypass); most others within ±0.3 dB. Guards: each harmonic owns 50 Hz either side plus 1 % of the tone for the grains (Dirty Tape's wow and flutter spreads its sidebands), 20 Hz for the synth.

---

## 14. What the automated suite does not cover

- **Anything in Rack itself**: the panel, tooltips, the right-click menu and its slider, `buffer.bin` through Rack's patch storage (its folder, `createPatchStorageDirectory`, the order of `onSave` and `dataToJson`, autosave), loading a patch, Initialize, Randomize, bypass, polyphonic cables, and the order of Rack's sample-rate event against `process()`.
- **The window as drawn**: `test_view` tests the geometry and captions Rack-free; the drawing (NanoVG, the font, the glass, the preview with no module) is not tested.
- **Windows and Linux**: the suite and the bench ran on the Mac for me; the package is Windows x64. CPU on x86-64 or Windows; the libm check on a Mac (issue #36).
- **The engine's Dirty Tape, Vintage and Clean Tape glue against the reference bit for bit.** `test_engine`'s bit comparison runs at Rack 48 kHz Digital only. `test_vendor` compares Beads itself in every quality but runs Dirty Tape at the wrong block size (12.2 item 1; it also passes at 24 kHz).
- **The ids.** No test pins the param, input, output and light numbers (12.2 item 7).
- **Listening**: whether it sounds like Beads on real material, the reverb, the window's usefulness, the level next to other modules, the verdicts in the listening guide.
- **Non-default knobs** beyond those in the segments (Beads' own behaviour is held by the bit comparison where it is exercised, not elsewhere).
- **Rates above 96 kHz** in the engine tests (only my loop covers them); a test of the clamp values.
- **A patch from another version**: only version 1 exists; a newer version loads as defaults by rule, tested by the golden maps.
- **Long runs**: there is no soak test for Spindrift (Fog has one).

---

## 15. Suggested QA test areas and expected results

Defaults unless stated: Rack 48 kHz, the module just added (all knobs at their defaults), Digital, Seed latched, menu defaults, a stereo source into IN L and IN R, Dry/Wet at its default 0.5, Density 0.65.

**Method notes.**
- Test the dry path with Dry/Wet 0, Feedback 0 and Reverb 0.
- To see a quality or rate change, watch the Quality cap (lit for 250 ms) and the window's top-left caption, and listen for the mute.
- Beads' own sound (what Size 0.2 or Shape 0.9 sounds like) is the manual's and the listening guide's business, not this reference's.
- Latency: loop OUT L back through a cable to a second module, or compare Spindrift's dry output with the source on a scope in Rack's scope module; the expected delays are in section 5.2.

### 15.1 A new module and the saved state

| Area | Procedure | Pass criteria |
|---|---|---|
| Defaults | add Spindrift; tooltips | Time, Size, Shape, Pitch, Feedback... as section 2.2: Density 65 %, Dry/Wet 50 %, Feedback 0, Reverb 0; attenuverters 0 %; Pitch reads 0 semitones; window "DIGITAL · STEREO · 4.0 S" with both inputs patched, "MONO · 8.0 S" with one; Seed cap breathing; no mute at add |
| Initialize | set Dirty Tape, gate output on, manual gain, Seed unlatched, move every knob; Initialize | knobs and menu to the defaults, Seed latched, Digital; Dirty Tape to Digital is a quality change: mute 250 ms and the buffer cleared |
| JSON | save, open the patch file (Rack 2's `.vcv` is a zstd-compressed tar holding `patch.json` and the modules' folders [unverified]) | the nine keys of section 3.2 with the values set; `manualGain` a real |
| Hostile JSON | hand-edit: remove `version`; `"quality": 7`; `"manualGain": 5` (integer); `"synth": "x"` | no version: all defaults; each bad field takes its default alone; no crash |
| Frozen save | play 10 s into IN L and IN R, Freeze, save the patch; look in the patch's storage folder | `buffer.bin`, 768,036 bytes; reopen: sound at once, Freeze lit; the buffer is the same (the grains differ, as every load seeds anew) |
| Not frozen | save unfrozen | no `buffer.bin`; reopen: unfrozen, empty buffer |
| Keep off | menu "Keep the frozen buffer" off, freeze, save | no `buffer.bin`; the patch says `frozen: false` |
| Synth freeze | Synth mode Always, Freeze, save | no `buffer.bin`; reopen unfrozen, empty |
| Bad files | truncate, flip a byte of the magic, change the header's quality, delete `buffer.bin`, each on a frozen patch | empty unfrozen buffer, a line in Rack's log naming why; no crash |
| Preset | load a user preset onto a running module | knobs and menu change; the buffer is kept; a different quality clears it |

### 15.2 Levels and the dry path

| Area | Procedure | Pass criteria |
|---|---|---|
| Unity | 5 V 1 kHz sine, Dry/Wet 0, Digital | out peak 3.75 V; 10 V in gives 7.5 V; 15 V in gives 7.5 V (clipped at ±10 V in) |
| Qualities | same, each quality | Vintage 3.75 V (3.748); Clean Tape 3.75 V at 5 V, RMS falls at 8 V and over; Dirty Tape 3.43 V at 5 V, 5.73 V at 10 V, 0.37 V at 0.5 V |
| Mono | one input | the patched side on both outputs at Dry/Wet 0; the window reads MONO and the length doubles |
| Poly | a 4-channel cable on IN L | the channels are summed (then held to ±10 V) |
| Gate output | menu on, density up, Size 0.5 | R only 0 V or 10 V; L the mono mix |
| Input gain | Fixed, Automatic, Manual 20 dB; a quiet source | Fixed leaves it; Automatic ramps up over about 6 s; Manual follows the slider; the slider and long press change the menu choice |

### 15.3 Rates and latency

| Area | Procedure | Pass criteria |
|---|---|---|
| Latency | Dry/Wet 0; measure the delay at 44.1, 48, 96, 192 kHz in Digital | 1.92 ms, **0.25 ms**, 1.76 ms, 1.75 ms (section 5.2); the same at Dry/Wet 0 for the two paths of a parallel dry leg |
| Passband | 1 kHz at 0.5 V, Dry/Wet 0, each quality, at 44.1 and 96 kHz | 0.75 of the input within 0.1 dB of Digital's (Dirty Tape lower by its own law) |
| Treble | white noise, Digital, Dry/Wet 0, spectrum at 44.1 and 48 kHz | at 44.1 kHz −3.9 dB per direction at 19.8 kHz (about −8 dB overall), steep above; at 48 kHz flat to Nyquist |
| Rate change | change Rack's rate during play, unfrozen | 230 ms of silence, then sound, buffer cleared; no crash; the Quality cap lights |
| Rate change frozen | freeze, change the rate | no mute; the grains continue |
| Odd rates | Rack's sample-rate menu: every offered rate | sound at the right pitch (a 1 kHz tone at 1 kHz), no clicks beyond the restart |
| High rates | 384 and 768 kHz | works; CPU up (section 10.2) |

### 15.4 Qualities

| Area | Procedure | Pass criteria |
|---|---|---|
| Press | Quality press each time | Digital, Vintage Digital, Clean Tape, Dirty Tape, Digital; caption reads the new name and length (4.0, 8.0, 10.7, 16.0 s stereo); a mute of 250 ms with the cap lit; grains from the old buffer do not play |
| Long press | hold 1.5 s | on release the quality does not change; Input gain becomes Automatic |
| Short press | 1.49 s | steps |
| Frozen | Freeze (button), press Quality three times, wait; unfreeze | nothing changes while frozen; the caption shows "DIGITAL → DIRTY TAPE" style; on release one change and one mute; with the gate the same |
| Quality while a mute | press again during the 250 ms | applies after the mute |
| Mono and stereo | pull IN R, plug it back | each is a clear and a mute; the caption changes; no change while frozen |
| Dirty Tape | listen | wow and flutter, hiss, darker; dry path also at 24 kHz and mu-law |

### 15.5 Modes: delay and synth

| Area | Procedure | Pass criteria |
|---|---|---|
| Delay | Size to its end stop, wait 1.5 s | window "DELAY" with ticks; the grains fade out; no mute; back below 0.98: grains |
| Synth timer | add a module, unplug both inputs, count | the synth enters at 10.0 s (±0.1 s), after a 250 ms silent mute; "SYNTH · BANK n"; patch IN L: back to grains after a mute |
| Timer held off | nothing patched, Feedback and Reverb up so the output sounds | stays in grains while the output exceeds 0.1 V; frozen: held off |
| Policy | Always, Never | Always: synth with inputs patched; Never: grains with none after 12 s; the switches mute |
| Synth quality | Quality in the synth | the resolution changes live (4, 7, 16 bit), no mute |

### 15.6 Triggers, Freeze and Seed

| Area | Procedure | Pass criteria |
|---|---|---|
| Seed latch | add; press Seed; hold Seed 4 s; Seed then Freeze | latched breathing at start; a press unlatches (light off, grains only while held or SEED high); the 4 s hold latches while still held; Freeze while holding Seed latches and does not toggle Freeze |
| Dead zone | Density 0.5, Seed unlatched, press Seed or a trigger | one grain per press; latched: none |
| Clock in SEED | latched, a 1 ms clock into SEED | Density divides (left) or thins (right) the clock; delay mode follows the clock |
| Pulse width | single SEED pulses of 1 ms, 100 µs, 25 µs at 48 kHz Digital and 96 kHz | 1 ms always seen; 25 µs at 48 kHz Digital seen; at 96 kHz or Dirty Tape the shortest are missed (section 8.3) |
| SEED delay | a sequencer's gate and Pitch CV at the same instant, Seed unlatched | the grain takes the new pitch (the CV settled 2.5 ms before the grain) |
| Freeze gate | a gate into GATE | frozen while high; the light follows; Freeze button and gate OR |

### 15.7 CV, ranges and smoothing

| Area | Procedure | Pass criteria |
|---|---|---|
| Time, Size, Shape CV | ±5 V with the attenuverter fully clockwise; then ±8 V; then ±10 V | ±5 V sweeps the control's whole travel; the CV is clipped at ±8 V |
| Pitch CV | 1 V steps, attenuverter fully clockwise | 12 semitones a volt (an octave a volt), clipped at ±5 V (±60 semitones); a tuner or a second oscillator |
| Mix CVs | ±5 V on Feedback, Dry/Wet, Reverb | each moves its knob's whole range; unpatched, no effect |
| Density CV | ±5 V | moves the knob over its whole travel; steps are not smoothed |
| Attenuverter | unpatched; then with a cable | unpatched: randomness, 0 at noon (dead zone ±3 %), none under it; patched clockwise: scales; anticlockwise: the CV's positive half adds randomness |
| Poly CV | a 4-channel cable on each CV | only channel 1 acts |
| NaN and ±inf | a source that outputs them | read as 0 V, or the jack's clip; no click, no crash |
| Knob steps | a typed value (a mapping or Rack's value entry) | glides over about 22 ms; no click |
| Pitch tooltip | type 12 and 7 into Pitch | the knob goes to the middle of the 12 and 7 flats; the tooltip reads them |

### 15.8 Window

| Area | Procedure | Pass criteria |
|---|---|---|
| Waveform | play a tone, stop | the record head sweeps left to right wrapping; the waveform fixed on the glass; the newest sound just left of the head |
| Grains | Density and Size up | dashes at their read points; the count (top right) steady, the word still; up to 32 |
| Frozen | Freeze | the head stops, the waveform stays |
| Delay, synth | each | ticks in delay mode; the wave and grains across the bank in the synth |
| Mute | a quality change | no grains drawn for 250 ms |
| Browser | the module browser and a screenshot | the stored picture |

### 15.9 CPU

| Area | Procedure | Pass criteria |
|---|---|---|
| One instance | Digital, 30 to 32 grains, Rack 48 kHz | about 1.0 to 1.5 % of a core (Mac); Windows not measured |
| Idle | nothing patched, defaults | about 0.25 % (Mac) |
| Many | 16 instances | no growth per instance |
| High rate | 192 kHz and 384 kHz, one instance | about 2 % and 3.5 % (section 10.2); compare with 48 kHz |

### 15.10 Robustness

| Area | Procedure | Pass criteria |
|---|---|---|
| Loud and bad input | ±12 V, DC, NaN and ±inf sources into every jack, each mode and quality | output finite, within about ±10.5 V; recovers in 1 s (6 s in delay mode) to within 1 dB |
| Bypass | bypass in play, release | IN L to L and IN R to R while bypassed; recording and grains stop and resume [unverified] |
| Instances | two Spindrifts, same settings and input | different clouds (separate random streams) |
| Long run | 30 min at a heavy setting | no growth in CPU, no crash [no automated soak exists] |

---

## 16. Glossary

| Term | Meaning |
|---|---|
| Beads units | the firmware's scale, 1.0 = full scale = 10 V in Spindrift |
| Beads rate | the codec rate the vendored code runs at: 24, 32 or 48 kHz, by quality |
| Block | Beads' control unit: Beads rate / 4000 frames (12, 8 or 6), 0.25 ms |
| Bypass (converter) | equal rates: the converter passes the signal untouched |
| Codec restart | the engine's reconfiguration: the converters' histories and the FIFO are cleared and the 250 ms mute starts |
| Configure | Beads' call that sets the quality and mono and clears the buffer |
| Dead zone | Density 0.48 to 0.52: no grains of its own; the attenuverter's ±0.03 of noon |
| Edge converter | the polyphase Kaiser-windowed-sinc filters between Rack's rate and the Beads rate |
| FIFO | the 1,024-frame queue between Beads' blocks and Rack's samples |
| Frozen | recording stopped, by the Freeze button or the GATE input; the ring holds still |
| Latched (Seed) | grains come by Density; unlatched they come only while Seed is held or SEED is high |
| Mute | 250 ms of zero output after a reconfiguration; the Quality cap is lit |
| Pending | a quality chosen while frozen, applied at unfreeze |
| Reference | the untouched upstream Beads, built under other namespaces, that the tests compare with |
| Ring | Beads' circular buffer, 768,000 bytes of audio (plus a 16-byte tail kept by `buffer.bin`) |
| Seqlock | the sequence count (`ringSeq`) that lets the save copy the ring and know whether it changed meanwhile |
| Tick | one control block, 0.25 ms; hold times are counted in them |
| Vendored | copied into the repository with its changes marked `// SPINDRIFT:` |

---

### Note on the measurement programs

All in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/spindrift/meas/`, compiled outside the repository against an extracted copy of the 2.0.39 tree (`../tree`) with `-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd` (`build.sh`), and the Beads object built as the plugin builds it (`-fwrapv -fno-strict-aliasing -fno-unsafe-math-optimizations -funsigned-char -DTEST`):

- `edge.cpp`: converter taps, phases and memory per Rack and Beads rate; one-direction gain at fractions of the lower rate; `latency()` and the 1 kHz delay per quality at ten Rack rates.
- `engine.cpp`: heap and set-up time per Rack rate; dry level per quality and input level; mute lengths (quality, mono, rate); the synth's entry time; CPU against Rack rate (also built with the plugin's strict flags as `engine_strict`).
- `misc.cpp`: the SEED delay in frames, the saved defaults, buffer sizes and constants.
- `trig.cpp`: the shortest SEED pulse seen at five Rack rates in Digital and Dirty Tape.
- `odd.cpp`: odd and clamped Rack rates.
- `vendor24.cpp`: a copy of `test_vendor.cpp` with Dirty Tape at its shipping 24 kHz.
- `engine_worst.cpp`: a copy of `test_engine.cpp` that prints the worst difference of each reference comparison.
- Mutations: `../mut/` (a copy of the tree; the two edits are described in 13.2).
- Logs: `../logs/`.

They drive `spindrift::SpindriftEngine` as `Spindrift.cpp` does: one `Knobs` and `Jacks` per Rack sample, the mode and quality set before the first sample. They do not exercise Rack's ports, threading, the window, or the patch storage. Anything marked [meas] should match Rack at the same rate; Rack-side effects are outside them. I did not run `make libm-calls`, `make smoke`, `make` (the plugin) or the whole `make test`.

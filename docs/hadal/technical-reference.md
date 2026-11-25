# Hadal 2.0.39: Technical Reference for QA

Module: **Hadal**, plugin `rka` (slug `rka`, module slug `Hadal`), VCV Rack 2 (minRackVersion 2.5.0). A 10 HP rumble and noise bed with no audio input.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from a copy of that tree (`git archive 2e6e960`).
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. Its `plugin.json` is byte-identical to the commit's, and its `res/hadal/Hadal.svg` and `Hadal-dark.svg` are byte-identical to the commit's. It has no `presets/Hadal` (Hadal has no presets).
Document date: 2026-11-24.

## Build history

Hadal's sources, panel and manual have not changed since build 2.0.33. `git diff 051d217 2e6e960` is empty for `src/hadal`, `docs/hadal`, `res/hadal` and the spec. Builds 2.0.34 to 2.0.39 (Beacon, Neap, Spring) did not touch it. The builds that did:

| Build | Date | Commit | What changed for Hadal |
|---|---|---|---|
| 2.0.3 | 2026-11-16 | `b10a123` | First build for listening: Rumble, Hiss, Crackle, Depth, Drift, CV on all five, the L/R mix. |
| 2.0.4 | 2026-11-16 | `8fa6b67` | Crackle soft-clipped and about 9 dB louder (the shaper, `Crackle::shape`). |
| 2.0.5 | 2026-11-17 | `82ba942` | Final review fixes: the CRACKLE jack carries the mono sum of both sides, a cheap `reset()`, no libm on the audio path. |
| 2.0.18 | 2026-11-20 | `b5a1cf0` | Rumble louder: 3.2 V RMS at full, default 70 %. |
| 2.0.19 | 2026-11-20 | `6ce788b` | Rumble audible at depth: low-pass corner 160 to 70 Hz (was 120 to 40), a soft clip whose drive rises with Depth, 2.5 V RMS at full, peaks inside ±5 V, default 80 %. |
| 2.0.33 | 2026-11-23 | `051d217` | Issues #34 (Drift and Crackle holds kept in seconds across a rate change) and #35 (gain tables built once, at Rack's rate). |

A bench-only commit (`cbcbbb7`, 2026-11-20) moved the CPU budgets to the 2.0.19 soft clip's cost. The spec and the manual were last edited for 2.0.19. Neither says anything about #34 or #35.

---

## 0. How to read this document

- Every number here comes from one of three sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/hadal/*.hpp`, `src/hadal/Hadal.cpp`, `src/FastMath.hpp`).
  - **[test]**: printed or asserted by the module's automated tests. I rebuilt them from the 2.0.39 tree and ran them on an Apple M-series Mac: `test_layers` and `test_engine` with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`), `test_hostile` with AddressSanitizer and UndefinedBehaviorSanitizer at `-O1`. All three passed.
  - **[meas]**: a small measurement program I wrote against the engine (`rka::HadalEngine`, the code the module runs), at 48 kHz unless stated. The programs are listed at the end. Where a figure is arithmetic on a measured one, it says so.
- **[issue #N]** points to a GitHub issue in `ryanarneson/RkaModules` (section 9).
- **[unverified]** marks anything I could not check against code or by measurement (mostly Rack host behaviour: bypass, the look of the panel, tooltips as drawn).
- Voltages are Rack volts. "RMS" is the root mean square over many seconds. "Jack" means one of the three layer outputs (RUMBLE, HISS, CRACKLE); "side" means the left or right part of a layer before the mix; "mix" means L and R.
- The knob value of every panel control is in [0, 1] (attenuverters −1 to +1). "Noon" is 0.5.
- The listening guide `hadal-2.0.39-listening.html` was used as a cross-check only. Where it disagrees with the code, the code wins; the disagreements are in section 9.2.
- Crackle is random, so its RMS and peaks vary from run to run (a 600 s run of one seed gave 0.133 V; the test's 60 s run gave 0.138 V). Figures are for the seed and length named.

---

## 1. What Hadal is

Hadal is a background sound source for dark, space and horror ambient: sub rumble, hiss and crackle. The spec (`docs/specs/2026-11-16-hadal-design.md`) says it will mostly **feed other modules**: rumble into Fog, hiss into Rigging, crackle into a delay, or the whole bed under a piece. The instrument is the hadal zone of the ocean. The main dial, Depth, is lettered in kilometres from 0 to 11. Deeper is darker.

Design goals that QA can check:

- Three noise layers (Rumble, Hiss, Crackle), each with its own level knob and its own mono jack, plus a stereo mix on L and R.
- One gesture (Depth) darkens the whole bed, each layer in its own range, without changing a layer's level.
- Slow, never-repeating movement (Drift). At Drift 0 the bed is static in its statistics.
- All five controls (Depth, Rumble, Hiss, Crackle, Drift) take CV with an attenuverter.
- Predictable levels: Hiss 1.6 V RMS and Rumble 2.5 V RMS at full on their own jacks; Crackle about 0.14 V RMS.
- No clicks or zipper noise when a knob or CV moves or a jack is patched.
- No DC offset and no energy below about 15 Hz (the spec's anti-goals; see 1.1 and 9.2 for Crackle's DC).
- A patched layer jack leaves the L/R mix; with R unpatched, L carries the mono sum.
- With nothing patched, Hadal does no work.

Non-goals (do not file these as defects): an audio input, pitch, gates or rhythm outputs, presets, a polyphonic output (a polyphonic CV uses channel 1) [issue #64 asks whether to revisit polyphony].

Anti-goals from the spec, which QA should listen for: anything that draws attention (pitch, ringing, pulsing, a loop); clicks or zipper noise on a move; headroom wasted on inaudible content (DC, energy under 15 Hz); a layer output that changes level when an unrelated control moves or that fades as Depth turns.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against the code's behaviour; the spec's and the manual's versions are given for context. No GitHub issue tracks any of these yet.

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| Crackle's density under Drift | spec: "about 3 to 25 per second" (a starting figure to tune by ear); manual: "between about 5 and 22 ticks a second (with its usual clustering on top)" | the density setting runs 10 × 2.2^(±Drift): **4.545 to 22.0 per second** at Drift 1 [code]; the largest range reached in 64 hours of simulation was 4.57 to 21.82 [meas]. The manual's range is the code's, rounded. The spec's is not what ships. Realised counts per window are wider (section 2.8) |
| Crackle level in the mix | spec as-built note: "about 0.1 V RMS at full and noon Depth in each side of the mix" | 0.103 V RMS per side before the mix gain; **0.051 V in L and R**, because the mix gain is 0.5 [meas] |
| "L and R are the layers at half their own outputs' level" (manual) | | true for Rumble and Hiss (exactly, to 1e-4 [test]). **Not for Crackle**: its jack is the mono sum (L+R)/√2, 2.2 dB over a side, so its mix side is 8.25 dB under its jack, not 6.02 dB [meas] |
| Crackle ticks never above ±5 V at Drift 0 | spec "no tick ever peaks above ±5 V at full"; manual "at Drift 0 no tick peaks above ±5 V" | true for each side (4.93 V in 2 hours at 0 km). **The CRACKLE jack reaches 6.82 V** at 0 km (7.07 V is the arithmetic ceiling, two sides at 5 V each through (L+R)/√2), 6.46 V at 2.75 km, 5.77 V at 5.5 km, and stays under 5 V from 8.25 km to 11 km [meas] |
| No DC offset | spec anti-goal: "no DC offset"; test plan: "no output has a DC offset" | Rumble and Hiss: none [test]. **Crackle has a DC component**: its ticks are all positive. At full level the CRACKLE jack's mean is +4 mV at 0 km, +12 mV at noon and +29 to +30 mV at 11 km, which is 4 %, 9 % and 21 % of its RMS [meas]. The test checks Rumble and Hiss only (section 10) |
| Mono through L | spec and manual: Hiss and Crackle "at about one side's loudness", Rumble about 2.8 dB louder | Rumble +2.74 dB, Hiss +0.00 dB, **Crackle +2.23 dB** [meas]. The listening guide says "hiss and crackle unchanged": wrong for Crackle |
| Rumble's L/R correlation | spec "at least 0.8"; manual "about 0.9" | +0.896 at 0 km, +0.882 at noon, +0.858 at 11 km (mix, Drift 0) [meas]. 0.9 is the figure before the soft clip; the clip lowers it with Depth |
| Depth holds each layer's level | spec ±1.5 dB; as built, Crackle within 2 dB | Rumble within −0.05 to +0.04 dB, Hiss within ±0.03 dB [test]. Crackle (engine, full level, Drift 0): −1.53 dB at 0 km and +0.54 dB at 11 km re noon [test]. The spec's ±1.5 dB is missed by 0.03 dB at 0 km; its as-built 2 dB holds |
| Crackle's long-term level | spec starting figure 0.5 V RMS; as built about 0.14 V on the jack | jack 0.133 V (one 600 s seed) to 0.138 V (the test), the constant `CRACKLE_FULL_RMS` is 0.138 [code]; each side 0.103 V before the mix |
| Drift's pace | spec "roughly 10 to 40 seconds"; manual "a new destination every 8 to 30 seconds, reached by a slow glide" | each target is held 8 to 30 s and followed through two glides, 6 s then 3 s [code]; a wander crosses its mean about every 42.9 s [test] |
| Smoothing | spec "30 to 40 ms one-poles, or per-sample interpolation"; manual "about 30 ms" | 30 ms one-pole (time constant) on the Depth, Drift and the three level gains [code]. Patching and R's mono switch are 10 ms linear ramps, not smoothed CV |
| Level knobs' tooltip | spec: audio taper "square of the knob position" | the tooltip shows the knob position as a percentage (0.5 reads "50%"), not the level: 50 % is 12.04 dB down [code] [unverified as drawn] |
| Hiss never brighter than 16 kHz | spec | the low-pass corner is capped at 16 kHz and at 0.45 × the sample rate [code] |
| Hadal's libm use | spec: "no libm per sample" | none per sample in the audio path; `std::sqrt` runs in `Crackle::update` every 32 samples and `std::exp`/`std::tan`/`std::pow` run on a sample-rate change [code]. `make libm-calls` is Linux-only and was not run here [issue #36] |
| Bypass | spec and manual: "Bypass silences every output" | no bypass routes are configured, so Rack has nothing to copy [code]; what Rack puts on the outputs under bypass is [unverified] |
| Presets | spec: none in the first release | none ship [code] |
| Unused constant | | `DRIFT_DENSITY_RATIO = 2.2` (`HadalEngine.hpp:83`) is declared but not used; the density law uses the literal `1.1375035f` (log2 2.2) at `HadalEngine.hpp:181`. Harmless; they agree |

---

## 2. Panel, controls and ports

### 2.1 Layout

10 HP (panel from `tools/gen_panels.py`; do not hand-edit). Top to bottom: title HADAL, subtitle RUMBLE & NOISE; the Depth hero dial with an ivory face (ticks every half kilometre, long ticks every 2 km, numerals 0, 2, 4, 6, 8 and 11, "KM" under the centre); the brass limb with Rumble, Hiss, Crackle and Drift curving below it; a row of five attenuverters; a row of five CV jacks; then the output tiles on the right. Positions in mm from `HadalLayout.hpp` [code]:

| Control | Centre (x, y) mm |
|---|---|
| Depth knob | 25.4, 32.0 |
| Rumble / Hiss / Crackle / Drift knobs | 5.9, 54.8 / 15.65, 60.37 / 35.15, 60.37 / 44.9, 54.8 |
| Attenuverters, left to right: Rumble, Hiss, Depth, Crackle, Drift | x 5.9 / 15.65 / 25.4 / 35.15 / 44.9, y 72.5 |
| CV jacks, same column order | same x, y 83.0 |
| RUMBLE / HISS / CRACKLE outputs | 25.4 / 35.15 / 44.9, y 102.0 |
| L / R outputs | 35.15 / 44.9, y 116.0 |
| Screws | 43.18, 2.54 (top right) and 7.62, 125.96 (bottom left) |

The middle column is Depth's: its CV jack and attenuverter sit directly under the dial, and the trace runs up into it. Depth's CV jack is therefore the middle one, not the first. There are no lights. How the panel looks (the numerals, the traces, the tiles) is not checked by any test: a visual check is [unverified here].

### 2.2 Parameter, input and output ids

`static_assert`s in `Hadal.cpp` pin Depth, Drift, the first and last attenuverter, the first and last output and the counts. A mis-numbered middle id, or any input id, would not be caught (the CV jacks are read as `DEPTH_CV_INPUT + i`).

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 | Depth | 0..1 | 0.5 | "Depth", unit " km", ×11: shows 0 to 11 km, default 5.5 km |
| 1 | Rumble | 0..1 | 0.8 | "Rumble", unit "%", ×100: default 80 % |
| 2 | Hiss | 0..1 | 0.3 | "Hiss", "%": default 30 % |
| 3 | Crackle | 0..1 | 0 | "Crackle", "%": default 0 % |
| 4 | Drift | 0..1 | 0.25 | "Drift", "%": default 25 % |
| 5..9 | Depth, Rumble, Hiss, Crackle, Drift CV amount (attenuverters) | −1..+1 | 0 | "Depth CV amount" and so on, "%", ×100 (−100 to +100) |

| Id | Input | | Id | Output (hover text) |
|---|---|---|---|---|
| 0..4 | Depth CV, Rumble CV, Hiss CV, Crackle CV, Drift CV | | 0 | "Rumble (patching it removes Rumble from the L/R mix)" |
| | | | 1 | "Hiss (patching it removes Hiss from the L/R mix)" |
| | | | 2 | "Crackle (patching it removes Crackle from the L/R mix)" |
| | | | 3 | "Left (the mono sum when R is unpatched)" |
| | | | 4 | "Right" |

None of the params snaps. None is excluded from Rack's Randomize [code: no `randomizeEnabled` calls], so Randomize moves all ten, attenuverters included [unverified in Rack].

### 2.3 The CV law (all five controls)

[code, `HadalEngine::process`, `HadalEngine.hpp:122`]

```
control = clamp( knob + (cv / 10) × att , 0, 1 )
```

- `cv` is **channel 1 only** of the jack (`HadalPort::first()`). Other channels of a polyphonic cable are ignored. An unpatched jack reads 0 V [code; test: `poly_cv_uses_channel_1`, `cv_law`].
- With the attenuverter at +1, +5 V adds 0.5. From noon, −5 V to +5 V sweeps the whole range; from 0 it takes +10 V. Attenuverter at 0 ignores CV [test: `cv_law`, all five controls, att −1, 0, +1].
- NaN and ±infinity (and any |x| ≥ 1e30) on a CV, a knob or an attenuverter read as 0 [code: `hadalFinite`; test: `nan_cv_safe`].
- There is no voltage limit on the jack; the sum is clamped.
- Smoothing, applied after the clamp, one pole per sample with a 30 ms time constant (`smooth_ = 1 − e^(−1/(0.03 fs))`, 6.94e-4 at 48 kHz) [code]:
  - Depth, Drift and the three level gains (the gain is the square of the control; the smoothing acts on the squared value).
  - On the first sample after construction, a sample-rate change or a reset, the smoothed values snap to their targets (no glide up from 0).
- Smoothing is not applied to the jack-patched mix weights or the mono switch; those are 10 ms linear ramps (section 6.2).
- Rack's tooltips show the knob position, not the CV-modified value.

### 2.4 Depth

[code, `NoiseLayer.hpp:184`, `Crackle.hpp:83`]

Depth moves each layer's corner on a log scale: the low-pass corner for Rumble and Hiss, the tick's time constant for Crackle. With `d` the smoothed Depth (0 to 1):

| Layer | Corner at d | At 0 km | At 11 km | Other Depth effects |
|---|---|---|---|---|
| Rumble | low-pass (4th order) 160 Hz × (70/160)^d | 160 Hz | 70 Hz | clip drive 0.66 × (2/0.66)^d |
| Hiss | low-pass (4th order) 12 kHz × (1500/12000)^d | 12 kHz | 1.5 kHz | none |
| Crackle | tick time constant τ = 0.06 ms × (2/0.06)^d | 0.06 ms | 2 ms | none |

Values across the dial [code arithmetic from the formulas above]:

| d (km) | Rumble corner | Rumble clip drive | Hiss corner | Crackle τ | Crackle −3 dB point of one tick |
|---|---|---|---|---|---|
| 0 (0 km) | 160 Hz | 0.66 | 12.0 kHz | 0.060 ms | about 1.7 kHz |
| 0.25 (2.75 km) | 130 Hz | 0.87 | 7.1 kHz | 0.144 ms | about 710 Hz |
| 0.5 (5.5 km) | 106 Hz | 1.15 | 4.2 kHz | 0.346 ms | about 300 Hz |
| 0.75 (8.25 km) | 86 Hz | 1.52 | 2.5 kHz | 0.83 ms | about 120 Hz |
| 1 (11 km) | 70 Hz | 2.00 | 1.5 kHz | 2.0 ms | about 51 Hz |

Fixed parts of each layer: Rumble has a 20 Hz 4th-order high-pass; Hiss has a 250 Hz 2nd-order high-pass. The Hiss corner is limited to min(16 kHz, 0.45 × sample rate) [code: `ShapedNoise::update`].

Measured spectral centroids [test, `test_layers`, 48 kHz, Drift not applicable]:

| Depth | Rumble centroid | Hiss centroid |
|---|---|---|
| 0 | 98.4 Hz | 6278 Hz |
| 0.25 | 83.6 Hz | 3901 Hz |
| 0.5 | 71.8 Hz | 2412 Hz |
| 0.75 | 62.4 Hz | 1503 Hz |
| 1 | 55.0 Hz | 955 Hz |

Crackle at 0, 0.5 and 1: spectral centroid 1788 Hz, 292 Hz, 53 Hz; tick length 0.44 ms, 2.58 ms, 14.37 ms [test].

Level stays put as Depth moves. Each layer's output gain comes from a table of the filters' noise power across the corner range (65 entries over the Depth range plus an octave either side for Drift), so the RMS is 1.0 in layer units at every corner. Measured RMS re 1.0 at the five depths: Rumble −0.05 to +0.04 dB, Hiss −0.03 to +0.03 dB [test]. For Crackle see 1.1.

How Depth is applied: the smoothed Depth goes into the layers every 32 samples (`ctl_`, `HadalEngine.hpp:177`), so corners move in steps of 0.67 ms at 48 kHz (42 µs at 768 kHz) on top of the 30 ms smoothing. The filters are TPT state-variable sections, whose coefficients can change at any time without a click.

Rumble's clip: after the filters, each side goes through tanh(drive × x) and a stored norm to bring it back to unit RMS. The peak is therefore bounded: 1.993 × RMS at 0 km, 1.499 at noon, 1.255 at 11 km (stated, and measured within 0.4 % of it [test]). At full (2.5 V) that is 4.96 V, 3.75 V and 3.14 V peak at Drift 0 [meas, test]. About 31.5 % of Rumble's energy at 11 km is above 60 Hz and 7.1 % above 100 Hz; 1.85 % is below 15 Hz [test].

### 2.5 Rumble, Hiss, Crackle: the level knobs

[code, `HadalEngine.hpp:76`, `128`, `196`]

```
gain_k = control_k²                  (audio taper; smoothed 30 ms)
jack/side amplitude = layer unit signal × scale_k × gain_k × Drift level wander
scale: Rumble 2.5 V, Hiss 1.6 V, Crackle 5 V (per shaped unit)
```

| Knob | Gain | Rumble RMS | Hiss RMS |
|---|---|---|---|
| 0 | 0 (silent; 0.0 V) | 0 | 0 |
| 0.1 | 0.01 (−40 dB) | 0.025 V | 0.016 V |
| 0.25 | 0.0625 (−24.1 dB) | 0.156 V | 0.100 V |
| 0.5 | 0.25 (−12.04 dB) | 0.625 V | 0.400 V |
| 0.8 | 0.64 (−3.9 dB) | 1.60 V | 1.02 V |
| 1 | 1 | 2.50 V | 1.60 V |

[code arithmetic.] Measured at full, Drift 0, 48 kHz, jacks [meas, test]: Rumble 2.498 to 2.503 V RMS across Depth; Hiss 1.600 to 1.601 V; Crackle 0.111 V at 0 km, 0.133 V at noon, 0.142 V at 11 km (the same seed, 300 s). Knob 0.5 gives −12.04 dB within 0.5 dB [test: `audio_taper`]; knob 0 is silent (under 1e-6 V RMS).

At the defaults (Rumble 0.8, Hiss 0.3, Crackle 0, Drift 0.25, Depth noon) with all jacks patched, 120 s [meas]: RUMBLE 1.579 V RMS (peak 2.61 V); HISS 0.149 V RMS (peak 0.71 V). With nothing on the jacks and L/R patched: L 0.793 V RMS (peak 1.52 V), R 0.835 V RMS (peak 1.60 V). The soft limit acted on 0 samples [meas; test `headroom`].

The knob sets the level at the jack and in the mix alike (the mix then applies its own fixed gain, section 6.2). The Crackle knob sets the ticks' size, not their number.

### 2.6 Crackle: what it makes

[code, `Crackle.hpp:122`]

- **Times.** Each sample draws a tick with probability `rate / fs`, where `rate = density × (0.15 + 1.7 × mod)`. `mod` is a value in [0, 1] that glides (0.2 s time constant) towards a new random target every 0.25 to 1 s. So the instantaneous rate runs from 0.15× to 1.85× the density, averaging 1×. This is the "loose clustering". The gaps between ticks have a coefficient of variation of 1.37 (a plain Poisson process gives 1.0; the test wants at least 0.7) [test].
- **Sizes.** u² for u uniform in [0, 1): mostly small, occasionally large, bounded by 1. Half the ticks are under a quarter of the largest size; 10 % are under 1 % of it (u < 0.1), which sit under a scope's noise. The share of ticks with size above s is 1 − √s: a counter set at 5 % of the largest size sees 78 % of the ticks [code arithmetic]. The engine's tick count includes all of them.
- **Position.** Each tick has one random pan in [0, 1), constant-power: left gain cos(π/2 × pan), right gain sin(π/2 × pan). 25 % or more land in each outer third [test: `crackle_spread`].
- **Shape.** Each side runs the impulse through a critically damped two-pole low-pass (response t·e^(−t/τ)), a positive-going pulse. Every tick of size 1 carries the same energy whatever τ and the sample rate, so Depth does not change the long-term level; per side the long-term RMS is 1.0 layer unit at density 10 per second.
- **Soft limit and shaper.** The layer soft-limits above 112 (0.8 × `PEAK` 140) towards 140 (`softLimit`). The engine then applies `Crackle::shape`: tanh(3x/140) / tanh(3), so a unit sample of 140 maps to 1.0, small and mid ticks come up about 3× and the largest are rounded. The result × 5 V is the side's voltage; no side can exceed ±5 V at Drift 0 and level 1. The shaper puts the long-term level about +9 dB over plain scaling (+7.5 at 0 km, +9.5 at 11 km).
- **Peaks and DC.** All ticks are positive, so the signal has a mean. Measured at the CRACKLE jack, full, Drift 0, 7200 s each depth [meas]:

| Depth | Jack peak | Side peak (mix side ÷ 0.5) | Jack samples over 5 V (in 7200 s) | Jack mean | Jack RMS |
|---|---|---|---|---|---|
| 0 km | 6.82 V | 4.93 V | 28,597 | +4.2 mV | 0.109 V |
| 2.75 km | 6.46 V | 4.79 V | 1,267 | +7.2 mV | 0.122 V |
| 5.5 km | 5.77 V | 4.40 V | 65 | +11.6 mV | 0.130 V |
| 8.25 km | 4.54 V | 3.59 V | 0 | +18.3 mV | 0.135 V |
| 11 km | 3.29 V | 2.68 V | 0 | +28.6 mV | 0.139 V |

  With Drift on, the level wander lifts these. 2-hour jack and side peaks at 0 km: Drift 0.25: 6.86 V and 5.39 V; Drift 1: 8.76 V and 7.42 V [meas]. The arithmetic ceiling is 5 V × 10^(4·Drift/20) per side: 5.61 V at Drift 0.25, 7.92 V at Drift 1. At Drift 1 the jack goes past the 8 V knee (8.76 V measured at the jack, so after the limiter has started to bend it).
- **Crackle's DC offset.** At full level the mean is 4 to 21 % of the RMS, rising with Depth, because a deeper tick is longer and holds more area (area = 2√τ in layer units). In the mix (jack unpatched) the mean per side is +1.6 mV at 0 km, +4.2 mV at noon and +10.5 mV at 11 km [meas]. It scales with the Crackle level (a square law in the knob). It is small against a mix, and it passes straight into anything DC-coupled that follows. It is not in the spec's or the manual's description of Crackle (no issue is filed).
- Seeds: ticks are drawn from the layer's own PCG32 stream, so they are reproducible per seed and differ between modules.

### 2.7 Drift

[code, `Drift.hpp`, `HadalEngine.hpp:167`]

**Wander.** Ten independent smooth random values, each in (−1, 1), advanced every 32 samples and linearly interpolated between steps:

| Wander | Use |
|---|---|
| 0..5 | level, per layer (Rumble, Hiss, Crackle) per side (L, R) |
| 6..8 | colour, per layer (the same on both sides) |
| 9 | Crackle's density |

Each draws a random target uniform in [−1, 1), holds it for 8 to 30 s (uniform), and follows it through two one-pole glides of 6 s then 3 s. It never reaches a target exactly, so |W| stays under 1 (a 3000 s trace reached −0.97 to +0.98 [test]). The wander is filtered noise, not an LFO. Tests: its largest step is 0.00015 per ms; its mean-crossing interval is 42.9 s; its autocorrelation at 60 to 300 s is up to 0.233 (the bound is 0.3); two seeds correlate under 0.2 [test].

Effects, with `D` the smoothed Drift control (30 ms one-pole) [code]:

```
level, layer k, side s:  dB = 4 × D × W[2k+s]        (±4 dB at D = 1)
colour, layer k:         octaves = (1/3) × D × W[6+k] (added to the low-pass corner; Crackle's τ is divided by 2^octaves)
Crackle density:         10 × 2^(D × W[9] × log2 2.2)  per second
```

At Drift 0 the wander values still move but have no effect: the level gain is exactly 1, the colour is 0 and the density is exactly 10 per second. Measured on Hiss over 60 s: level spread 0.26 dB and centroid 2396 to 2418 Hz [test].

At Drift 1, measured over 300 s on Hiss: level −3.27 to +3.00 dB around its mean; colour −0.37 to +0.21 octave around its mean [test]. From the density run, W's distribution at Drift 1 is: 90 % of the time within ±0.77, 98 % within ±0.90, so a level excursion past ±3 dB is rare [meas, derived].

The default Drift is 0.25: ±1 dB level, ±1/12 octave colour, density 8.2 to 12.2 per second.

On the CRACKLE jack the two sides keep their own level wanders (independent), so the sum moves by up to ±4 dB × D.

The pace has no control. After a sample-rate change the pending holds are rescaled to the same seconds (since 2.0.33) [test: `wander_hold_survives_rate_change`, `crackle_hold_survives_rate_change`].

### 2.8 Crackle density against Drift: the range, measured

The question was whether Drift's density range is 3 to 25 (spec), about 5 to 22 (manual) or about 4.5 to 22 (a reading of the code). The code's range is right.

[code] `crackle_.setDensity(10 × 2^(D × W × 1.1375035))` at `HadalEngine.hpp:181`, every 32 samples, with D in [0, 1] and W in (−1, 1). So the density runs from 10 / 2.2 = **4.545** to 10 × 2.2 = **22.0** ticks per second at Drift 1, never reaching either end exactly. At Drift 0.25 it is 8.21 to 12.18; at 0.5, 6.74 to 14.83. `Crackle::setDensity` clamps to 0 to 50, which never acts. Drift's CV cannot push it further: the control is clamped to 1.

[meas, `density`] Crackle's density, read from the engine every 1/3 s, after a 30 s settle; 64 seeds of 3600 s at Drift 1, 32 of each at 0.25 and 0.5, 16 at 0:

| Drift | Density setting: min / max | p5 / p50 / p95 | Mean tick rate over all time |
|---|---|---|---|
| 0 | 10.000 / 10.000 | 10 / 10 / 10 | 10.008 /s |
| 0.25 | 8.223 / 12.153 | 8.60 / 9.97 / 11.62 | 10.037 /s |
| 0.5 | 6.762 / 14.770 | 7.39 / 9.93 / 13.50 | 10.147 /s |
| 1 | **4.568 / 21.817** | 5.46 / 9.87 / 18.18 | 10.629 /s |

At Drift 1 the density sat below 5 per second for 1.55 % of the time and above 22 per second for none. It is skewed low because the law is exponential: the median is just under 10, and 90 % of the time it is between 5.5 and 18.2. The mean tick rate is above 10 at high Drift (10.63 at Drift 1) because 2.2^W has a mean over 1.

The numbers a listener or a counter sees are wider than the density setting, because ticks are random and clustered (the instantaneous rate is 0.15× to 1.85× the density). Realised ticks per second at Drift 1, counted by the engine (every tick, whatever its size), windows of 1, 10 and 60 s [meas]:

| Window | min | p1 | p5 | p50 | p95 | p99 | max | Share below 3/s | Share below 5/s | Share above 22/s | Share above 25/s |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 s | 0 | 1 | 2 | 9 | 23 | 30 | 53 | 5.08 % | 15.43 % | 5.45 % | 2.87 % |
| 10 s | 2.50 | 4.10 | 5.10 | 9.90 | 18.70 | 22.10 | 27.80 | 0.05 % | 4.25 % | 1.02 % | 0.15 % |
| 60 s | 4.55 | 5.83 | 6.67 | 10.28 | 15.63 | 17.82 | 20.75 | 0.00 % | 0.16 % | 0.00 % | 0.00 % |

At Drift 0 (no Drift effect, only the clustering) the same windows are: 1 s: 0 to 34 with p1 1 and p99 22; 10 s: 4.40 to 16.1 with p1 6.4 and p99 13.8; 60 s: 7.60 to 11.92 [meas, 16 seeds of 3600 s].

What to state to QA: **at Drift 1 the average tick rate wanders between 4.5 and 22 per second (a one-minute count reads 4.6 to 20.8; a ten-second count 2.5 to 27.8).** The spec's 3 and 25 appear only as 10-second extremes (0.05 % of windows under 3, 0.15 % over 25) and as one-second counts (5 % and 3 %); they are not the density's range. The manual's "about 5 and 22" is the density's range with 4.545 rounded to 5, and it says "with its usual clustering on top", which is correct. The engine test's own bound (15 to 300 ticks per 10 s) is looser than either (section 10); its measured run was 49 to 240 per 10 s over 30 windows [test: `drift_full_moves_within_ranges`].

When counting with a scope or a counter module, remember that a tick's visibility depends on its size (u²): a threshold at 5 % of the largest tick sees 78 % of them (2.6).

---

## 3. The right-click menu and saved state

[code: `Hadal.cpp`; unchanged since 2.0.19]

- **There is no right-click menu beyond Rack's own** (no `appendContextMenu`). The manual says the same.
- **There are no saved JSON keys.** `Hadal.cpp` has no `dataToJson` or `dataFromJson`, so a saved module's `"data"` object is absent. Rack stores the ten params (ids 0 to 9) in the patch.
- **The seed is not saved.** Each module draws a seed from `random::u32()` when it is constructed. A reloaded patch, a duplicate and a pasted module each get a new seed, so the same patch never sounds the same twice. The statistics (levels, corners, density) are the same; the noise samples and the Drift wander start differently [code; manual: "Every Hadal has its own random seed"].
- **Initialize** (right-click, "Initialize"): Rack resets the ten params to their defaults (Depth 0.5, Rumble 0.8, Hiss 0.3, Crackle 0, Drift 0.25, attenuverters 0), and `onReset()` calls `engine.reset()`: it reseeds every random stream from the module's own seed, clears the Rumble and Hiss filters and snaps the smoothed values. After a reset the output equals a fresh engine's, bit for bit [test: `reset_matches_fresh`]. It does not rebuild the gain tables (0.000 ms against 6.45 ms for a rate change [test: `reset_is_cheap`]).
- **Bypass**: no bypass routes (`Hadal.cpp:66`). Hadal has no input, so there is nothing to pass through. Whether Rack leaves the outputs at 0 V under bypass is [unverified in a live Rack]. `process()` is not called while bypassed, so the wanders and crackle stream freeze and resume where they stopped [unverified].
- Randomize, copy and paste, and preset menus are Rack's. There are no factory presets.

---

## 4. Signal flow

### 4.1 Block diagram

```
 CONTROLS (HadalEngine.hpp:122-152)
   knob + CV(channel 1)/10 × att, clamp 0..1, NaN/inf → 0
   Depth, Drift ─► 30 ms one-pole ─► depthS, driftS
   Rumble, Hiss, Crackle ─► square ─► 30 ms one-pole ─► gain_k
   patched[k] (jack k) ─► w_k  (1 → 0 when patched, 10 ms linear ramp)
   patched[R]          ─► mono (0 → 1 when R unpatched, 10 ms linear ramp)

 WANDERS ×10 (Drift.hpp), every sample, steps every 32 samples, interpolated
   level_k,s = 4 dB × driftS × W    colour_k = 1/3 oct × driftS × W    density = 10 × 2^(driftS × W × 1.1375)

 every 32 samples:  rumble.setDepthColour, hiss.setDepthColour, crackle.setDepthColour, crackle.setDensity

 RUMBLE  uniform noise: shared c (0.9487) + own a/b (0.3162) per side
           ─► HP 20 Hz (4th, 2 SVF) ─► LP 160..70 Hz (4th) × table gain ─► tanh(drive·x) × norm        L, R
 HISS    independent uniform noise per side
           ─► HP 250 Hz (2nd, 1 SVF) ─► LP 12k..1.5 kHz (4th) × table gain                              L, R
 CRACKLE Poisson ticks, rate = density × (0.15 + 1.7·mod); size u²; one constant-power pan per tick
           ─► critically damped 2-pole (τ 0.06..2 ms) ─► softLimit(112..140) ─► shape tanh(3x/140)/tanh 3   L, R

 (a layer is run only if its jack is patched, or L or R is patched)

 per layer k, side s:   v[k][s] = side × scale_k (2.5 / 1.6 / 5 V) × gain_k × dB→gain(level_k,s)
 jacks:  RUMBLE = v[0][L]      HISS = v[1][L]      CRACKLE = (v[2][L] + v[2][R]) / √2
 mix:    mixL = Σ_k 0.5 × w_k × v[k][L]            mixR = Σ_k 0.5 × w_k × v[k][R]
 L out = mono × (mixL + mixR)/√2 + (1 − mono) × mixL         R out = mixR

 every output ─► SOFT LIMIT (identity to ±8 V, then 8 + 2·tanh((|x|−8)/2): ceiling 10 V) ─► jack
```

With nothing patched, `process()` returns zeros without running any layer or wander (the smoothed controls and ramps still advance).

### 4.2 Block constants

**Input sanitising** [code]: `hadalFinite` on every param and CV (NaN, ±inf and |x| ≥ 1e30 → 0).

**Random source** [code, `Random.hpp`]: PCG32 per user (Rumble, Hiss, Crackle, ten wanders), all seeded from the instance seed: `seed × 2654435761 + 1` (Rumble), `+2` (Hiss), `+3` (Crackle), `+11 + i` (wander i). No global state [engine-globals check passed].

**Filters** [code, `NoiseLayer.hpp:36`]: Simper TPT state-variable sections in double precision (so 20 Hz stays exact at 768 kHz). 4th-order Butterworth is two sections with Q 0.5411961 and 1.3065630. The high-pass corner is limited to 0.2 × the sample rate (only matters below 100 Hz).

**Gain tables** [code, `ShapedNoise::setRate`]: 65 entries per layer, each the reciprocal of the square root of the noise variance through the filters at that low-pass corner (numerical integral over 6000 points). Built on construction and on every sample-rate change (about 6.5 ms on the Mac [test]). Lookup is linear in log2(corner).

**Rumble clip tables** [code]: 33 entries of drive and norm (the tanh's RMS for a unit Gaussian input). The filtered noise is only approximately Gaussian, measured peaks reach 99.7 % or more of the stated value and never pass it [test].

**Fixed constants** [code, `HadalEngine.hpp:76`]: `FULL_V` 1.6, `RUMBLE_V` 2.5, `CRACKLE_GAIN` 5, `CRACKLE_FULL_RMS` 0.138 (test reference only), `MIX_GAIN` 0.5, `KNEE` 8, `CEIL` 10, `DRIFT_LEVEL_DB` 4, `DRIFT_COLOUR_OCT` 1/3, `DENSITY` 10.

---

## 5. The three layers

### 5.1 Rumble

- **Should sound**: a low roar at 0 km, a deep growl at 11 km, never a note. Soft-clipped, so a speaker without a sub still gets overtones at depth.
- **Stereo**: both sides share 90 % of their noise power before the filters and clip. Correlation +0.896 / +0.882 / +0.858 at 0 / 5.5 / 11 km [meas]; the spec's floor is 0.8 [test: `rumble_correlated_hiss_not`].
- **Spectrum** (section 2.4): 1.85 % of the energy under 15 Hz at 11 km (bound 5 %); 31.5 % over 60 Hz and 7.1 % over 100 Hz at 11 km (bounds 25 % to ∞ and 3 % to 20 %) [test].
- **Mean**: −0.0015 V at 0 km, −0.0030 V at noon, −0.0055 V at 11 km, on 2.5 V RMS over 300 s (0.06 to 0.22 % of RMS, the chance wander of a 20 to 70 Hz band) [meas]; the test bound is 0.5 % of RMS.
- **Pitch**: no spectral peak more than 12 dB over its ±1/3-octave neighbourhood at depth 0, 0.5 and 1 [test: `no_pitch`]. No autocorrelation over 0.1 between 250 ms and 10 s [test: `no_loop`].

### 5.2 Hiss

- **Should sound**: bright air at 0 km down to a dull muffled roar at 11 km. High-passed at 250 Hz, so it has no rumble of its own.
- **Stereo**: independent noise per side. Correlation +0.001, mono-sum loss 3.01 dB at every Depth [meas]; the test wants |corr| under 0.1.
- **Peaks**: a noise with a high crest factor. At full and Drift 0, the HISS jack peaks at 6.34 V (0 km), 7.47 V (noon) and 8.20 V (11 km) in 120 s [meas]; the last is over the 8 V knee, so the limiter acts at full Hiss and 11 km even at Drift 0 (section 6.5).
- **Mean**: +0.00000 V (to 5 decimals) [meas].

### 5.3 Crackle

Sections 2.6 and 2.8. A summary of what QA can see:

- Short, decaying positive clicks, about 10 a second on average at Drift 0, in loose clusters. Bright and short at 0 km (0.44 ms long), dark and long at 11 km (14.4 ms) [test].
- Level is held by energy, so a deep thud is longer and peaks lower than a shallow tick.
- The ticks' times show no rhythm: the tick train's autocorrelation is under 0.1 between 50 ms and 10 s [test: `crackle_no_rhythm`].
- Ticks come out at 635 per 60 s in the test (600 expected, bound ±15 %) [test].
- It is silent at Crackle 0 (the default). The listening guide asks whether it should start with a little; that is a taste question.
- Crackle's DC (section 2.6).

### 5.4 Which layers run

[code, `HadalEngine.hpp:154`] A layer is computed only if its jack is patched or L or R is patched. A layer with its jack patched is still computed, but it leaves the mix. With nothing patched nothing runs and every output is exactly 0 V [test: `nothing_patched_no_work`]. When a layer resumes after a pause its filters continue from their last state (noise, so no click).

---

## 6. Outputs, the mix and headroom

### 6.1 The three layer jacks

- **RUMBLE** and **HISS** carry the layer's **left side** only, at the knob's level (Drift's level wander for the left side applies).
- **CRACKLE** carries (left + right)/√2 of the shaped, scaled sides, so a hard-panned tick is only 3 dB down and no tick goes missing [code; manual].
- Each jack passes through the soft limit (8 V knee, 10 V ceiling).
- Patching a layer jack removes that layer from L and R (6.2). The hover text says so on each jack; the panel does not [code; not seen drawn].

### 6.2 The L/R mix

[code, `HadalEngine.hpp:197`]

- Each layer reaches L and R at `MIX_GAIN` 0.5 × its weight `w_k`. Weights are 1 when the layer's jack is unpatched and 0 when patched. They move linearly: a full 0 to 1 takes 10 ms (`fadeStep_ = 1/(0.01 fs)` per sample). Patching or unpatching therefore fades over about 10 ms and does not click [test: `patch_unpatch_no_click`: step at the switch within 2× the steady maximum step + 0.05 V].
- The change in L/R when a jack is patched is exactly that layer's contribution: unpatching restores it to within 1e-4 V [test: `patched_layer_leaves_mix`].
- The mix's ratio to the jack is the fixed 0.5 for Rumble and Hiss (within 1e-4 of scale) and 0.5 for Crackle against the mono sum of its two mix sides [test: `mix_ratio`].
- For Crackle the mix side RMS is 0.0514 V (L) and 0.0517 V (R) at full and noon Depth, against a jack of 0.1329 V: 8.25 dB under the jack [meas, 600 s].

### 6.3 Mono through L

[code, `HadalEngine.hpp:209`] With R unpatched, L carries (mixL + mixR)/√2. The switch is a 10 ms linear crossfade, driven by whether the R jack is connected [test: `mono_normal_switch_no_click`]. With R patched, L is the left mix.

Measured, one layer in the mix at level 1, Drift 0, noon: L with R unpatched against L with R patched [meas, 30 s; 600 s for Crackle]:

| Layer | Change | L stereo → L mono (V RMS) |
|---|---|---|
| Rumble | **+2.74 dB** | 1.2509 → 1.7156 |
| Hiss | +0.00 dB | 0.8008 → 0.8008 |
| Crackle | **+2.23 dB** | 0.0514 → 0.0665 |

The Rumble figure is the correlation's: (1 + ρ) = 1.9, +2.79 dB. Crackle's is E[(gl+gr)²]/2 = 0.82 against 0.5 per side, +2.1 dB, since each tick appears on both sides with a pan-dependent split [code arithmetic].

### 6.4 Stereo image of the mix

Mix of one layer at a time, Drift 0, 60 s (Crackle 300 s) [meas]:

| Layer | Depth | L/R correlation | Mono-sum loss | L, R RMS (V, level 1) |
|---|---|---|---|---|
| Rumble | 0 km | +0.896 | 0.23 dB | 1.2490, 1.2522 |
| Rumble | 5.5 km | +0.882 | 0.26 dB | 1.2484, 1.2522 |
| Rumble | 11 km | +0.858 | 0.32 dB | 1.2506, 1.2514 |
| Hiss | 0, 5.5, 11 km | +0.001 | 3.01 dB | 0.800, 0.800 |
| Crackle | 0 km | +0.727 | 0.64 dB | 0.0424, 0.0423 |
| Crackle | 5.5 km | +0.663 | 0.80 dB | 0.0515, 0.0514 |
| Crackle | 11 km | +0.644 | 0.82 dB | 0.0553, 0.0550 |
| Default bed (Rumble 0.8, Hiss 0.3, Drift 0.25) | 5.5 km | +0.873 | 0.29 dB | |

"Mono-sum loss" = 10·log10(mean channel power / power of (L+R)/2): 0 dB for identical channels, 3 dB for uncorrelated. The mix of all three layers at full has a loss of at most 3 dB [test: `stereo_mix`, Rumble + Hiss].

Crackle's correlation is positive (0.64 to 0.73) because a tick appears on both sides at once; the test checks pan spread, not correlation.

### 6.5 Headroom and the soft limit

[code, `limit()`, `HadalEngine.hpp:246`] Every output (3 jacks, L, R) passes through: identity for |x| ≤ 8 V, then 8 + 2·tanh((|x| − 8)/2). The ceiling is 10 V and is approached only asymptotically; |x| = 10 V needs an infinite input.

The mix at full, all three layers in it, 120 s, peak (share of samples over 8 V) [meas]:

| Drift | Depth | L peak | R peak |
|---|---|---|---|
| 0 | 0 km / 5.5 / 11 | 5.86 / 5.35 / 5.42 V | 6.01 / 5.68 / 5.72 V |
| 0.25 | 0 / 5.5 / 11 | 6.05 / 5.53 / 5.73 V | 6.37 / 5.40 / 5.93 V |
| 1 | 0 / 5.5 / 11 | 6.74 / 6.31 / 7.04 V | 6.86 / 6.85 / 6.60 V |

The mix never reached 8 V (0.000 % in every case); the test bound is 10 V [test: `headroom`].

Layer jacks at full, 120 s, peak and share over 8 V [meas]:

| Drift | Depth | RUMBLE | HISS | CRACKLE |
|---|---|---|---|---|
| 0 | 0 km | 4.96 V | 6.34 V | 6.30 V |
| 0 | 5.5 km | 3.75 V | 7.47 V | 3.74 V |
| 0 | 11 km | 3.14 V | 8.20 V | 2.35 V |
| 0.25 | 0 km | 5.34 V | 6.52 V | 6.37 V |
| 0.25 | 5.5 km | 4.07 V | 7.89 V | 4.93 V |
| 0.25 | 11 km | 3.41 V | 8.74 V | 2.76 V |
| 1 | 0 km | 6.86 V | 8.30 V | 7.08 V |
| 1 | 5.5 km | 5.24 V | 9.43 V (0.006 %) | 4.22 V |
| 1 | 11 km | 4.41 V | 9.90 V (0.013 %) | 2.73 V |

Figures are at the jack, after the limiter. At full level the HISS jack peaks over the 8 V knee at 11 km (8.20 V at Drift 0) and at 0 km at Drift 1 (8.30 V); the limiter bends those peaks, and above 8 V they are rare (0.006 % to 0.013 % of samples at Drift 1). The manual says as much. At the defaults the limiter never acts (0 samples in 120 s with all three in the mix [meas]; test `headroom`: 60 s). Rumble at Drift 0 stays under 5 V as the spec asks (4.96 V at 0 km).

---

## 7. CPU

[meas, `make bench` equivalent: `build/bench/bench`, built with the plugin's flags, Apple M-series Mac, 48 kHz, one core, per instance, run with the machine otherwise quiet. The bench passed all its budgets.]

| Case | Instances | ns per sample per instance | % of a core | Budget (Apple arm64) |
|---|---|---|---|---|
| idle (nothing patched) | 1 | 6.5 | 0.031 | 0.10 |
| idle | 4 | 6.2 | 0.030 | |
| idle | 16 | 6.5 | 0.031 | |
| outputs (all patched, defaults) | 1 | 48.4 | 0.232 | 0.45 |
| outputs | 4 | 48.8 | 0.234 | |
| outputs | 16 | 47.6 | 0.229 | |
| busy (all full, Drift 1, five CVs moving) | 1 | 50.9 | 0.244 | 0.45 |
| busy | 4 | 52.1 | 0.250 | |
| busy | 16 | 51.5 | 0.247 | |

- Sixteen instances cost 0.98 to 1.01 times one alone per instance. The non-Mac budgets are three times the Mac's and provisional (not yet measured on x86-64) [TESTING.md].
- `TESTING.md` records 0.029 / 0.201 / 0.214 (idle / outputs / busy). The new run is 7 to 15 % higher; it is within budget and within normal machine noise, but QA should compare their own machine's figures with each other, not with these.
- Cost with one layer's jack patched is lower, since only needed layers run [code]. I did not measure the saving [unverified].
- A sample-rate change builds both gain tables: about 6.5 ms on the Mac [test: `reset_is_cheap`, `construction_builds_tables_once`]. It runs under Rack's engine lock. On Windows it may be longer [unverified]; the change is a one-off.
- Initialize costs 0.000 ms [test].
- Windows: no automated budget. Record Rack's CPU meter per instance for idle (no cables), outputs (all patched) and busy (CV on everything, Drift full) at 48 kHz.
- `make libm-calls` is Linux-only (issue #36). It was not run. On the Mac, the bench's knobs-still cases are not a libm test.
- `make engine-globals` passed (no writable global state in the engine).

---

## 8. Sample rates and robustness

### 8.1 Sample rates

- Usable rates are finite and in (0, 768 000] Hz. Anything else (0, negative, NaN, infinity, 1e9) is ignored and the last good rate kept [code: `usableRate`; test: `bad_rate_ignored`, `constructed_at_rate`]. The constructor takes Rack's rate so the tables are built once (#35).
- Corners are set relative to the rate. The Hiss corner is capped at min(16 kHz, 0.45 fs); the high-pass corners are capped at 0.2 fs.
- Levels at the jacks across rates (full, noon Depth, Drift 0; 30 s for Rumble and Hiss, 200 to 600 s for Crackle) [meas]:

| Rate | Rumble (V RMS) | Hiss (V RMS) | Crackle (V RMS) |
|---|---|---|---|
| 44.1 kHz | 2.498 | 1.602 | 0.1324 |
| 48 kHz | 2.502 | 1.602 | 0.1329 |
| 88.2 kHz | 2.508 | 1.601 | 0.1329 |
| 96 kHz | 2.506 | 1.600 | 0.1323 |
| 176.4 kHz | 2.507 | 1.603 | 0.1309 |
| 192 kHz | 2.506 | 1.602 | 0.1290 |
| 352.8 kHz | 2.498 | 1.603 | 0.1344 |
| 384 kHz | 2.507 | 1.602 | 0.1340 |
| 705.6 kHz | 2.509 | 1.602 | 0.1310 |
| 768 kHz | 2.511 | 1.602 | 0.1332 |

  Rumble and Hiss are within 0.5 % across rates; Crackle's spread (0.129 to 0.134 V) is its random variation. Spectral centroids at 44.1, 96, 192 and 768 kHz are within 3.0 % (Rumble) and 1.7 % (Hiss) of the 48 kHz values [test: `stable_at_all_rates`, bound 10 %].
- A sample-rate change clears the filters, snaps the smoothed values and rescales the pending Drift and Crackle holds (#34). No burst beyond the cleared start.

### 8.2 Hostile input and numerical safety

[test: `test_hostile`, built with ASan and UBSan at `-O1`, 0.7 s]

- Rate changes 44.1 → 768 → 48 kHz with every output finite and within ±10 V; the Hiss centroid and level after them within 5 % and 1 dB of a fresh engine's.
- Rates 1, 10, 1000, 8000, 44100, 192000 and 768000 Hz at Drift 1: all outputs finite and within ±10 V.
- No allocation in `process()` over 200,000 samples with Drift 1, all CVs at ±4 V square, the patch states changing [test: `no_allocation_in_process`, counting `operator new`].
- Seeded fuzz: 10^6 samples (seed 2026), params, attenuverters (−1 to +1) and patch states re-drawn every 4096 samples, CV channels 0 to 2, CVs ±12 V, a rate change every 65,536 samples: finite and within ±10 V.
- Denormals: Crackle's state decays to exactly 0 after 2,000,000 silent samples.
- NaN, +inf and −inf on every CV with attenuverters at 1: outputs finite and bounded [test: `nan_cv_safe`].
- Square CV at 50 Hz on each of the five CVs: the output's largest step is within 2 × the steady step + 0.05 V [test: `square_cv_no_zipper`].

Not covered: NaN or infinity on a knob (the code reads it as 0, but no test sets it), a seed sweep (one seed, 2026), and a rate above 768 kHz in Rack (Rack's own limit applies).

---

## 9. Open issues and findings

### 9.1 GitHub issues

No open issue is about Hadal alone. Do not re-file these.

| Issue | State | What it says |
|---|---|---|
| #64 Polyphony: modules should be polyphonic where it makes sense | open, tech debt | lists Hadal as "a source with no voices; CVs use channel 1". The issue's own view: stereo effects, a mixer and a noise source "probably stay as they are; confirm". The listening guide asks the same (a poly output?) |
| #67 Aliasing: measure every module that makes or bends sound | open, test, tech debt | lists Hadal with Squall, Sluice, Pan as "noise, steps and gains ... not looked at; expected to need a note, not a fix". Hadal has tanh clips (Rumble's drive up to 2, Crackle's shaper, the output limit) and no aliasing test or note in `TESTING.md` rule 16's table |
| #36 Process: libm-calls check is skipped on macOS | open | applies to every module, Hadal included |
| #34 Hadal: Drift and Crackle holds not rescaled on a rate change | closed in 2.0.33 | explains why holds are rescaled in `setRate` |
| #35 Hadal: gain tables built twice | closed in 2.0.33 | explains why the constructor takes the rate |

### 9.2 Found while preparing this document (not yet filed)

Each of these is a difference between the code and the spec, the manual or the listening guide. None has a GitHub issue.

1. **Crackle has a DC offset** (section 2.6, 1.1). Its jack's mean is +4 mV at 0 km and +29 mV at 11 km at full level. The spec lists "no DC offset" as an anti-goal and its test plan says "no output has a DC offset". The test checks Rumble and Hiss only. It may be acceptable (small beside a mix, and Fog has a Low cut), but it is undocumented.
2. **The CRACKLE jack's peak passes ±5 V at Drift 0**: 6.82 V at 0 km in two hours, 7.07 V as the ceiling (section 2.6). Per side it holds at 4.93 V. The manual's "at Drift 0 no tick peaks above ±5 V" is true of the sides, not the jack. `test_engine` checks the jack's peak only at noon (3.74 V).
3. **Crackle in the mono L is +2.2 dB, not "one side's loudness"** (section 6.3). The listening guide's "hiss and crackle unchanged" is wrong for Crackle.
4. **Crackle's mix side is 8.25 dB under its jack**, not 6 dB (the manual's "half their own outputs' level"), and 0.051 V per side at full, not the spec's "about 0.1 V in each side of the mix".
5. **Crackle's density range**: the spec's 3 to 25 is not what ships; the manual's 5 to 22 is the code's 4.545 to 22.0, rounded (section 2.8).
6. The level knobs' tooltips show the knob percentage, not the level: a knob at 50 % is 12 dB down [code].
7. `DRIFT_DENSITY_RATIO` is unused (the literal `1.1375035f` stands in for it) [code]. The header comment of `HadalEngine.hpp` (lines 9 and 10) lost words: "the layers that are needed: a layer's output is patched, or L or R is" is cut short. Neither affects behaviour.
8. Rumble's L/R correlation is 0.858 at 11 km: the manual says "about 0.9".

### 9.3 Known limits that are not defects

- No input, no pitch, no presets (spec non-goals).
- The pace of Drift is fixed (8 to 30 s holds, 6 s and 3 s glides).
- The three level knobs read as percentages of knob position, with a squared level law (spec).
- Hadal does not repeat: every load, duplicate and paste gets a new seed.
- At full Hiss, deep Depth and high Drift, the soft limit acts on peaks (section 6.5).

### 9.4 Listening guide (cross-check)

`hadal-2.0.39-listening.html` was read as a cross-check. It matches the code except:

- "MONO THROUGH L ... hiss and crackle unchanged": Crackle is +2.2 dB (above). Rumble "about 3 dB louder": +2.74 dB, fine.
- "CRACKLE LEVEL: ... Drift can lift the odd tick to about 5.3 V at the default Drift, 7.6 V at full": the 2-hour side peaks are 5.39 V and 7.42 V at 0 km [meas], so the figures hold for a side; the jack's peaks at 0 km are 6.86 V and 8.76 V (limited above 8 V). The guide does not say which output.
- It gives no density range for Crackle under Drift; the range in section 2.8 is the figure to test against.

---

## 10. What the automated suite already covers

Three test programs, all passed: `test_layers` (25 tests), `test_engine` (22 tests) and `test_hostile` (5 tests, ASan and UBSan). I ran them from the 2.0.39 tree. I did not run the mutation checks that `TESTING.md` rule 1 asks for (the review records them), and I did not run `make` (the plugin build), `make smoke` or the Windows build.

Also run and passed: `bench` (all declarations and budgets), `bench --check`, `make engine-globals`, `make panels-test` (115 tests). `make libm-calls` is Linux-only and was skipped.

### 10.1 test_layers (the layers and the wander)

| Test | What it checks |
|---|---|
| `rumble_centroid_falls_with_depth`, `hiss_centroid_falls_with_depth` | centroid falls at each of five depths; below a ceiling at 11 km and above a floor at 0 km |
| `layer_level_flat_across_depth` | Rumble and Hiss RMS within ±1.5 dB of 1.0 across Depth, both sides (measured within ±0.05 dB) |
| `rumble_little_below_15hz` | under 5 % of the energy below 15 Hz at 11 km (1.85 %) |
| `rumble_audible_when_deep` | at 11 km at least 25 % of the energy over 60 Hz and 3 % to 20 % over 100 Hz |
| `rumble_peaks_bounded` | peak ≤ the stated peak and ≤ 2.0 × RMS; within 90 % of it is reached |
| `no_dc` | mean under 5e-3 of RMS on Rumble and Hiss at 0 and 11 km (**not Crackle**) |
| `rumble_correlated_hiss_not` | Rumble corr ≥ 0.8; Hiss |corr| < 0.1 |
| `no_pitch` | no spectral peak over 12 dB above its neighbourhood (Rumble, Hiss) |
| `no_loop` | autocorrelation under 0.1 from 250 ms to 10 s (Rumble, Hiss) |
| `same_seed_repeats_other_differs` | same seed bit-identical; different seeds corr < 0.1 |
| `stable_at_all_rates` | finite; centroid within 10 % and level within 1.5 dB of 48 kHz, at 44.1, 96, 192 and 768 kHz |
| `crackle_rate` | 600 ± 15 % ticks in 60 s at density 10; gap CV ≥ 0.7 (measured 635, 1.37) |
| `crackle_level_flat_across_depth` | layer RMS within ±1.5 dB (measured +0.32 to +0.46 dB re 1.0) |
| `crackle_peaks_bounded` | raw layer peak ≤ 140 at five depths, at 48 kHz and 768 kHz, density 25 (before the shaper and the jack sum) |
| `crackle_darker_and_longer_with_depth` | centroid falls and tick length rises with Depth |
| `crackle_sizes_skewed` | median size under 0.4 × the 95th percentile |
| `crackle_spread` | 25 % or more of ticks in each outer third of the pan |
| `crackle_no_rhythm` | tick-train autocorrelation under 0.1 from 50 ms to 10 s |
| `wander_bounded_and_smooth`, `wander_slow`, `wander_not_periodic`, `wander_seeds_independent` | range within ±1 and over 1 wide; step under 0.01 per ms; mean-crossing 5 to 60 s; autocorrelation under 0.3; seeds corr under 0.2 |
| `wander_hold_survives_rate_change`, `crackle_hold_survives_rate_change` | holds last the same seconds after 768 → 48 kHz (#34), five seeds each |

### 10.2 test_engine (the full engine)

| Test | What it checks |
|---|---|
| `full_levels` | Rumble 2.5 V ±1 dB and peak ≤ 5 V at 0, 0.5, 1 Depth; Hiss 1.6 V ±1 dB; Crackle 0.138 V ±1 dB, ≥ 0.085 V, peak ≤ 5 V at noon (60 s); Crackle within 2 dB of noon at 0 and 1 |
| `audio_taper` | knob 0.5 is −12.04 ± 0.5 dB; knob 0 silent |
| `mix_ratio` | each layer reaches L at the fixed 0.5 ratio (Crackle through (L+R)/√2) |
| `cv_law`, `poly_cv_uses_channel_1` | the law on all five controls at att −1, 0, +1; channel 1 only |
| `patched_layer_leaves_mix`, `patch_unpatch_no_click` | L silent when Rumble patched; returns exactly; step at the switch ≤ 2 × steady + 0.05 V |
| `mono_normal`, `mono_normal_switch_no_click` | L = (L+R)/√2 when R unpatched; within 1 dB of one side on Hiss; no click |
| `nothing_patched_no_work` | zero work with nothing patched; work starts on patching |
| `square_cv_no_zipper` | 50 Hz square CV on each input; finite, ≤ 10 V, step ≤ 2 × steady + 0.05 V |
| `drift_zero_static` | at Drift 0, level spread under 0.5 dB and centroid within 3 % over 60 s |
| `drift_full_moves_within_ranges` | at Drift 1 over 300 s: level within ±5 dB and ≥ 2 dB wide; colour within ±0.43 oct and ≥ 0.1 oct wide; Crackle 15 to 300 ticks per 10 s (measured 49 to 240) |
| `headroom` | all at full, 60 s, three depths, Drift 1: mix within 10 V; at the defaults the limiter never acts |
| `stereo_mix` | mono-sum loss ≤ 3 dB; Rumble corr ≥ 0.8 |
| `seeds_differ_and_repeat`, `reset_matches_fresh` | one seed repeats; reset equals a fresh engine, bit for bit |
| `reset_is_cheap`, `constructed_at_rate`, `construction_builds_tables_once` | reset under 1/20 of a rate change; constructor rate honoured and bad ones fall back to 48 kHz; construction under 1.5 × one rate change |
| `bad_rate_ignored`, `nan_cv_safe` | unusable rates ignored; NaN and ±inf CV safe |

### 10.3 test_hostile

See section 8.2.

### 10.4 Not covered by the automated suite

- **Crackle's DC** and the **CRACKLE jack's peak** (section 9.2).
- **Aliasing** (rule 16): no test, no table note [issue #67].
- Crackle's density **range under Drift** is bounded loosely (15 to 300 per 10 s); no test pins 4.5 to 22.
- Crackle's level at Drift above 0 and the density's mean rise with Drift.
- The mix-side level of Crackle, the jack-to-side relation, mono-through-L for Crackle and Rumble in numbers (the test bounds Hiss only).
- Hiss's jack against the 8 V knee at full; Rumble and Crackle jack peaks at Drift above 0.
- Stereo image of Crackle.
- Rumble's correlation under Drift (the sides' level wanders are independent).
- Tooltips, hover text, the panel's look, the Dark theme, bypass, Initialize in Rack, Randomize, copy and paste of a module, the Rack module browser. All are Rack-side [unverified here].
- Windows build.
- Level and spectrum of any layer at Drift 1 over long runs beyond the 300 s test.

---

## 11. Suggested QA test areas and expected results

### 11.1 Per control

| Procedure | Pass criteria |
|---|---|
| Rumble knob 0 → 1 with a meter on the RUMBLE jack, Depth noon, Drift 0 | 0 V at 0; 0.625 V RMS at 0.5; 1.6 V at 0.8; 2.5 V at 1. Square law; no steps while turning |
| Hiss knob 0 → 1, HISS jack | 0; 0.40 V at 0.5; 1.02 V at 0.8; 1.6 V at 1 |
| Crackle knob 0 → 1, CRACKLE jack | silent at 0; ticks grow in size, not number; long-term RMS about 0.133 to 0.139 V at 1 and noon Depth; about 0.034 V at 0.5 (square law, 1/4) |
| Depth 0 → 1 on each jack, constant level knob | each layer darkens steadily; Rumble and Hiss RMS constant within about 0.1 dB; Crackle within 2 dB; no step while turning |
| Depth as a tooltip | 0 to 11 km, 5.5 km at noon |
| Drift 0 → 1 | at 0, nothing moves (level steady within 0.5 dB over 60 s); at 1, level wanders ±4 dB and colour ±1/3 octave over tens of seconds; no pulse or loop |
| Each attenuverter at 0, +1, −1 with ±5 V from a slow LFO | at 0 no effect; at +1 from noon ±5 V sweeps the whole range; at −1 inverts |
| Each CV at 50 Hz square, ±5 V, attenuverter +1 | no clicks: steps in the output under about 2 × the steady step; the response follows slowly (30 ms) |
| Polyphonic cable into each CV | channel 1 only |

### 11.2 Crackle density and level

| Procedure | Pass criteria |
|---|---|
| Crackle full, Drift 0, a counter or scope on the CRACKLE jack, 10 min | about 10 ticks a second on average (a count over 60 s reads 7.6 to 11.9); clusters but no rhythm; a count over 1 s varies from 0 to about 34 |
| Same at Drift 1, 60 s counts over an hour | between about 4.6 and 20.8 (p1 5.8, p99 17.8); a 10 s count 2.5 to 27.8; the mean over an hour about 10 to 11 |
| Same at the default Drift 0.25, 60 s counts | 7.3 to 13.9 |
| Count with a threshold | ticks under about 1 % of the largest size (10 % of ticks) are invisible; at a threshold of 5 % of the largest tick, expect about 78 % of the engine's count |
| Crackle at 0 km, full, Drift 0, DC-coupled meter on the jack | mean about +4 mV (4 % of its RMS) [known: section 9.2]; at 11 km about +29 mV |
| Crackle at 0 km, full, Drift 0, scope on the jack, 1 hour | peaks to about 6.8 V (known: the jack is the sum of both sides); each L/R side stays under 2.5 V (4.93 V times the 0.5 mix gain) |
| Crackle at 11 km | soft, long thuds about 14 ms long, centroid about 53 Hz; peaks at the jack about 3.3 V |

### 11.3 Mix, patching and mono

| Procedure | Pass criteria |
|---|---|
| Patch RUMBLE, HISS and CRACKLE in turn with L/R monitored | that layer fades out of L/R over about 10 ms with no click; unpatching fades it back; the other layers do not change |
| Patch L only | L carries the mono sum, (L+R)/√2; Hiss as loud as one side; Rumble +2.7 dB; Crackle +2.2 dB |
| Patch R while L is playing | L crossfades to the left mix over 10 ms; no click |
| Mono-sum the mix | Rumble loses under 0.4 dB (corr 0.86 to 0.90); Hiss loses 3.0 dB; default bed 0.29 dB |
| Rumble only in the mix | L/R correlation 0.85 to 0.90 at every Depth |
| Nothing patched | CPU at the idle level; outputs 0 V |

### 11.4 Levels and headroom

| Procedure | Pass criteria |
|---|---|
| Defaults, L and R patched | L about 0.79 V RMS, R about 0.84 V RMS, peaks under 2 V; the limiter never acts |
| All layers at full, Drift 1, three depths, L/R patched, 2 min | mix peaks under about 7.1 V; never above 10 V |
| Hiss at full, Depth 11 km, HISS jack | RMS 1.6 V; peaks to about 8.2 V at Drift 0 (the soft limit acts on occasional peaks) |
| Rumble at full, Drift 0 | peaks 4.96 V at 0 km, 3.75 V at noon, 3.14 V at 11 km |

### 11.5 Sample rates

| Procedure | Pass criteria |
|---|---|
| 44.1, 48, 88.2, 96, 176.4, 192, 352.8, 384, 705.6, 768 kHz, Rumble and Hiss at full | RMS 2.50 V and 1.60 V within 1 %; centroids within 10 % of 48 kHz (measured within 3 %); no silence, noise bursts or crash |
| Change the rate while playing | no loud burst; the bed restarts at once; levels and corners as at 48 kHz |
| Crackle at 768 kHz | ticks still about 10 a second; level as at 48 kHz within a few percent (varies with the seed) |

### 11.6 Patch, preset and Rack behaviour

| Procedure | Pass criteria |
|---|---|
| Save and reload a patch | all ten params restore; the sound has the same character but different noise (new seed) [expected] |
| Duplicate the module | the copy has the params; its noise differs from the original |
| Initialize | params return to defaults (Depth 5.5 km, Rumble 80 %, Hiss 30 %, Crackle 0 %, Drift 25 %, attenuverters 0); the noise restarts from its seed [unverified in Rack: the audible restart] |
| Bypass | outputs silent [unverified in Rack]; on release the bed resumes |
| Randomize | all ten params move; no crash (Rack's behaviour) |
| Tooltips | Depth shows km; level knobs show the knob percentage; each jack's hover text carries the "patching it removes ... from the L/R mix" line; L's says "the mono sum when R is unpatched" [unverified as drawn] |
| Panel look | numerals 0, 2, 4, 6, 8, 11 on the Depth face; ticks every half kilometre; the middle column belongs to Depth; both themes [unverified here] |

### 11.7 Reference patches (from the manual)

| Patch | Expected |
|---|---|
| L, R into Fog, Visibility long, some Bloom | a deep, moving space; no clicks when Depth moves |
| HISS into a resonant filter swept by Sway | wind or surf; HISS leaves the L/R mix while patched |
| CRACKLE into a dark delay with feedback, or Fog in SMOKE | each tick leaves a trail; on its own the jack is sparse and quiet (0.13 V RMS), so turn Crackle up |
| Slow Sway into Depth CV, small attenuverter | the bed sinks and rises over minutes |

### 11.8 CPU

| Procedure | Pass criteria |
|---|---|
| one instance, nothing patched | about 0.03 % of a core (Mac bench); the lowest reading in the meter |
| one instance, all patched | about 0.23 % (budget 0.45) |
| 16 instances | per-instance cost within 1 to 1.5 × one alone |
| Windows | record the meter per instance for idle, outputs and busy; no automated budget |

---

## 12. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a −1..+1 scale on a CV input; negative inverts |
| Audio taper | the level knob's square law: gain = knob² |
| Colour | Drift's movement of a layer's corner, in octaves (up to ±1/3) |
| Crackle density | the mean tick rate of the Crackle layer (10 per second at Drift 0); Drift moves it between 4.545 and 22.0 |
| Depth | the main dial, 0 to 11 km; moves each layer's corner on a log scale |
| Drift | slow random wander of level, colour and density |
| DC offset | a nonzero mean; Crackle's ticks are all positive, so its signal has one |
| Jack | one of the three layer outputs RUMBLE, HISS, CRACKLE |
| Knee | the level (8 V) above which the soft limit bends the signal; the ceiling is 10 V |
| Layer | Rumble, Hiss or Crackle |
| Mix | the L and R outputs: the layers still in it, at 0.5 × their level |
| Mix weight | a layer's 1 or 0 multiplier in the mix (0 when its jack is patched), ramped over 10 ms |
| Mono sum | (L + R)/√2, what L carries when R is unpatched, and what the CRACKLE jack carries |
| Mono-sum loss | how much quieter (L+R)/2 is than the mean channel level; 0 dB for identical channels, 3 dB for uncorrelated |
| One-pole | a first-order smoother; after one time constant it has covered 63 % of a step |
| PCG32 | the small random-number generator behind each layer and wander |
| Shaper | `Crackle::shape`, the soft clip tanh(3x/140)/tanh(3) that brings Crackle's small ticks up |
| Side | a layer's left or right signal before the mix |
| Soft limit | identity to ±8 V, a tanh bend above it, ceiling 10 V |
| TPT | topology-preserving transform; the filter form Hadal's sections use |
| Wander | one of ten smooth random values (8 to 30 s holds, 6 s and 3 s glides) behind Drift |

---

### Note on the measurement programs

All are outside the repository, in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/hadal/meas/`, compiled against the extracted 2.0.39 tree with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`, C++17):

- `density.cpp`: Crackle's density setting (read from the engine every 1/3 s; it opens `private` for that one read) and the realised tick counts in 1, 10 and 60 s windows, by Drift. Used with Drift 0, 0.25, 0.5, 1, 16 to 64 seeds of 3600 s.
- `levels.cpp`: levels, DC, mono through L, Crackle's jack against its mix side, defaults, headroom by Drift and Depth, stereo, and levels across ten sample rates.
- `cpeak.cpp`: Crackle's jack and side peaks, over 7200 s per depth, at Drift 0, 0.25 and 1.
- The test logs are in `.../hadal/logs/`.

They drive `rka::HadalEngine` as `Hadal.cpp` does: params at the values named, one CV unpatched, the jacks patched as named, noon Depth unless stated. They do not exercise Rack's ports, threading, the panel or the tooltips. Anything marked [meas] should match Rack at 48 kHz, but Rack-side effects (event order on a patch load or a rate change, bypass, Randomize) are outside them. I did not run `make libm-calls` (Linux only), `make`, `make smoke` or the mutation checks.

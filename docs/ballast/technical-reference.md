# Ballast and Pan 2.0.39: Technical Reference for QA

Modules: **Ballast** (12 HP four-channel stereo mixer) and **Pan** (6 HP pan expander for Ballast), plugin `rka` (slug `rka`, module slugs `Ballast` and `Pan`), VCV Rack 2 (minRackVersion 2.5.0). One document covers both, because Pan only works inside Ballast's `process()`.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). The commit only adds the Neap "Add Spring expander" menu item. The Ballast and Pan sources, manuals and specs are as of commit `115e04c` (2026-11-23, test build 2.0.33), the last commit to touch them. Everything here was taken from the `2e6e960` tree.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It holds `rka/plugin.json` (version 2.0.39), `rka/plugin.dll`, and the panels `rka/res/ballast/Ballast.svg`, `Ballast-dark.svg`, `rka/res/pan/Pan.svg` and `Pan-dark.svg`. There are no factory presets for either module.
Document date: 2026-11-24.

Specs: `docs/specs/2026-11-17-ballast-design.md`, `docs/specs/2026-11-17-pan-design.md`. Manuals: `docs/ballast/README.md`, `docs/pan/README.md`. Sources: `src/ballast/` and `src/pan/`.

## Build history

Ballast and Pan were built on 2026-11-17 and have had no change to sound or panel since 2.0.11 (2026-11-18). CHANGELOG.md "Test builds", with the commits behind each [code: `git log` of `src/ballast`, `src/pan`, `docs/ballast`, `docs/pan`]:

| Build | Date | What changed for these modules |
|---|---|---|
| 2.0.6 | 2026-11-17 | Ballast, first build for listening: four stereo channels, VCA level CV, Master, CHAIN, DC blocker, CLIP, expander hook (commits `bd07bd4` panel, `b99559d` engine, `5eaeec0` DC blocker/limit/CLIP, `84af83a` hook tests, `0a2f38f` hostile tests, `b28a023` module) |
| 2.0.7 | 2026-11-17 | Ballast final review: bypassed expanders skipped, NaN stopped at the mix, cheap chain walk (`b446aba`) |
| 2.0.8 | 2026-11-17 | Pan, first build (panel `21c6d90`, core `2641e30`, module and "Add Pan expander" `ac21f9f`). Ballast: expander `postLevel` takes a source type, **either input alone is now a centred mono source** (2.0.7 played IN R alone on the right only), one expander of each kind, `linked()`, DC blocker fades on (#38), a rate change keeps the state (#39), #40 fixes (`748121f`, `d261b9e`) |
| 2.0.9 | 2026-11-17 | Pan: the Pan law menu; **No boost is the default**, so a hard pan cannot drive Ballast into its limit (`7fe80da`). The 2.0.8 build had only the +3 dB law |
| 2.0.10 | 2026-11-17 | Pan and Ballast: "newly attached" is decided on Ballast's thread through `attached()`. The first build inferred it from a counter both threads wrote; with several engine threads Pan restarted from centre every few samples (crackle) (`d464389`) |
| 2.0.11 | 2026-11-18 | Ballast: the chain walk is capped at 32 steps (a looped chain cannot hang). Pan: LINK dark when bypassed (`2fda360`) |
| 2.0.33 | 2026-11-23 | Tests and TESTING.md only (`115e04c`, closes #43): `rate_change_keeps_state` now changes 48 to 96 kHz; new `mono_no_boost_dip_under_cv` |
| 2.0.34 to 2.0.39 | 2026-11-23/24 | No change to Ballast or Pan. 2.0.38 and 2.0.39 add Neap and its Spring expander, which is **not** a Ballast expander (section 5.2) |

A patch saved in test build 2.0.8 (Pan with only the constant-loudness behaviour) has no `panLaw` key, so it loads in 2.0.39 with the No boost law and sounds different at the sides (section 6.8).

---

## 0. How to read this document

- Every number comes from one of three sources, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources, cited as file:line.
  - **[test]**: asserted or printed by the module's automated tests, which I ran. I built them from the 2.0.39 tree with the plugin's flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; the two hostile tests with ASan and UBSan at `-O1`), on an Apple M-series Mac, with `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64`. All five test programs passed: `ballast/test_engine`, `ballast/test_expander`, `ballast/test_hostile`, `pan/test_core`, `pan/test_hostile`. `bench --check`, the engine-globals check and compiling `Ballast.cpp` and `Pan.cpp` against the SDK (objects only, not linked or loaded) also passed. `make libm-calls` is skipped on macOS [issue #36], so the no-libm claim is from reading the code and was not run.
  - **[meas]**: a program I wrote against the Rack-free headers (`BallastEngine`, `BallastChain.hpp`, `PanCore`), the code the modules run. Programs are in the scratchpad `meas/` folder; the list is at the end. The machine was busy (load average about 8), so timings are upper figures.
- **[issue #N]** is a GitHub issue in the repository (section 9).
- **[unverified]** marks anything I could not check: Rack host behaviour (the engine lock, expander events at patch load, bypass of outputs without routes, tooltips and the light's look) and anything needing a live Rack. **No part of this document was run inside Rack.**
- Voltages are Rack volts. "dB" is an amplitude ratio unless stated. "Noon" means knob 0.5. The knob value of every panel control is in [0, 1] (the attenuverters in [−1, 1]).
- The listening guide `/Volumes/share1/VCVRACK/ballast-2.0.39-listening.html` was used as a cross-check only. Where it disagrees with the code, the code wins (section 1.2).

---

## 1. What Ballast and Pan are

### 1.1 Intent

**Ballast** (spec "Intent"): a four-channel stereo mixer, so a patch of RKA modules needs no other maker's mixer. Four stereo sources in, each with a Level knob and a level CV, summed to one stereo pair through a Master. It is the last stage before the audio interface, Fog or a limiter. The user's inspiration is Venom's mixer family and its expanders. Ballast ships with an expander connection designed in, and Pan is the first module on it.

**Pan** (spec "Intent"): a pan knob, a pan CV and its attenuverter for each of Ballast's four channels, placed against Ballast's right side. All pan functionality lives here (the user's ruling: Ballast has no pan of its own).

Goals QA can check:

- Four stereo channels, each with Level and a level CV on the VCA law: the CV can never push a channel above its knob.
- Master with its own CV; CHAIN inputs for joining two Ballasts.
- A clean, safe output: DC blocked, never above 10 V, CLIP light on the soft limit.
- Either input of a channel alone is a centred mono source.
- No click or zipper from a knob; CV is unsmoothed (envelopes stay sharp, audio-rate CV works).
- With no expander attached, Ballast's sound and CPU are those of a Ballast without the connection.
- Pan at centre changes nothing, bit for bit, in every mode and law.
- Pan pans mono sources (one input patched) and moves stereo sources as an image (true pan) or balances them.
- One Pan acts per Ballast; two never stack.
- "Add Pan expander" in Ballast's menu places a Pan against its right side.

Non-goals (do not file as defects): pan in Ballast itself; mute, solo, fades, sends and offsets (later expanders); polyphonic mixing (polyphonic cables are summed); attenuverters on Ballast's CVs; a choice of mono pan laws beyond the two Pan offers; panning CHAIN.

Anti-goals (a failure even if it works): a CV that raises a channel above its knob; clicks or zipper from a knob; any cost from the expander connection when none is attached; attaching Pan at centre changing the sound; two Pans stacking.

### 1.2 Where the spec, the manual and the shipped code differ

The code is what ships. Test against the code. The spec and manual versions are given for context. Rows 1 and 2 were the two items the listening-guide pass flagged; both are confirmed.

| # | Topic | Spec / manual says | 2.0.39 does |
|---|---|---|---|
| 1 | **Ballast's menu** | Manual `docs/ballast/README.md:23`: "The right-click menu has one setting: the DC blocker." | **Two items**: the DC blocker checkbox and "Add Pan expander" (`Ballast.cpp:222`, `:232`). The same manual says so at line 93 and `docs/pan/README.md:12` too, so the manual contradicts itself. Line 23 is stale |
| 2 | **Pan's knobs on patch load** | Pan spec `:145-146`: smoothing "snapping on load". Spec `:182-183`: the smoothing "starts from centre when Pan is first called after not being linked". Pan manual: silent on load | **Glides in from centre on load** (`PanCore::first_` starts true, `PanCore.hpp:133`). The glide is a one-pole with a **10 ms time constant**: 63 % of the way at 10.0 ms, 90 % at 23.0 ms, 99 % at 46.1 ms, exactly on the knob at 85.2 ms [meas, 48 kHz; the same at 96 kHz]. "About 10 ms" in the manual and the guide is the time constant, not the duration. The spec contradicts itself [issue #42] |
| 3 | Mono source | Ballast spec `:120-122`: mono = IN L patched and IN R not; "IN R alone is stereo"; "with only IN R patched, the channel is R only" | Either jack alone is a centred mono source; each is normalled to the other (`BallastEngine.hpp:139-142`). Changed in 2.0.8; the Pan spec and both manuals say so, the Ballast spec was not updated |
| 4 | Expander source flag | Ballast spec: `postLevel(ch, l, r, bool mono)` | `postLevel(ch, l, r, BallastSource src)`, `STEREO` or `MONO`; an unpatched channel reads `STEREO` (`BallastExpander.hpp:43`, `BallastEngine.hpp:148`) |
| 5 | Maximum expanders | Spec and manuals: silent | At most **8** expanders are called per sample (`Ballast.cpp:72`); at most **32** modules are looked at per walk (`BallastChain.hpp:15`); one expander of each nonzero kind. Pan's kind is 1, so **one Pan acts** (section 5.3) |
| 6 | Smoothing "about 10 ms" (Ballast knobs and Pan knobs) | Both manuals, both specs | One-pole, time constant 10 ms: the Level position is 99 % there at about 46 ms; the gain through the curve is 63 % at 11.8 ms and 99 % at 48.6 ms for a 0.5 to 0.75 step [meas] |
| 7 | DC blocker fade "about 50 ms" | Manual, spec | 1/e time constant 53.0 ms; 10 % left at 122 ms; 1 % left at 244 ms [meas]. Corner 3 Hz (spec: "about 5 Hz or below") |
| 8 | Level curve | Spec "2 x p^2.41" | exponent ln 2 / ln (4/3) = 2.4094208 so 0.75 is exactly unity (spec records the change) |
| 9 | "No work with no output patched" | Spec: "no work"; manual: "almost no work (the knobs' smoothing still runs)" | The manual is right. The engine's header comment (`BallastEngine.hpp:17-18`) still says "nothing is computed" |
| 10 | Bypass | Spec: "Bypass silences both outputs (no bypass routes)" | No `configBypass` call (`Ballast.cpp:99`), so no routes. Whether Rack zeroes the outputs of a bypassed module with no routes is [unverified]. A bypassed Ballast does not link its expanders (section 5.7) |
| 11 | Pan laws | Pan spec's "pan laws" text gives equal-power gains with +3 dB at the sides, then an "As built" note adding the Pan law menu | **No boost is the default**: mono gains are the equal-power gains each held to 1; true pan folds with k = 1/2. Constant loudness is the spec's first law (section 6.5) |
| 12 | Pan menu order | Spec lists *Stereo sources* first, then the law as built | **Pan law** first, then **Stereo sources** (`Pan.cpp:147-152`) |
| 13 | LINK "within the last 50 ms" | Spec: a counter Ballast writes and Pan reads | The counter is Pan's own `process()` calls since Ballast last linked it (`Pan.cpp:45`, `:112-116`); the threshold is `0.05 × sample rate` calls, so 2,400 at 48 kHz. That is 50 ms only because Pan's `process()` runs once per sample |
| 14 | Pan CPU | Manual: "about 0.1 % of a CPU core" | Pan's share of the pair is about 0.12 to 0.14 % of a core at 48 kHz on this busy machine; TESTING.md records 0.09. **The bench does not include the chain walk**, which costs about as much again [meas, stand-in] (section 8.1) |
| 15 | Pan `stereoMode` and `panLaw` on load | Spec silent | Out-of-range or wrong-type values load as 0 (the default), not clamped; a missing key also loads as 0 (`Pan.cpp:80-86`). Compare Fog, where a missing key leaves the setting alone |
| 16 | Add Pan expander greyed | Spec: "when an unbypassed Pan is already in Ballast's chain" | As specified (`Ballast.cpp:225-232`). Decided when the menu opens |
| 17 | libm check | Spec: run on Linux before an engine task closes | Skipped on this Mac (issue #36); not run for this document |

#### The listening guide against the code

The guide agrees with the code except for these points:

- **Item 2 ("both read 0.0")**: the Level and Master tooltips read **+0.0 dB** at unity, because the format is `%+.1f` (`Ballast.cpp:29`) [meas: the gain at 0.75 is exactly 1.000000 and the string is "+0.0"]. The unit " dB" follows [unverified in Rack's tooltip]. Fully left reads "-inf dB".
- **"Glides in from centre over 10 ms"** (verdict row "Glide on load", patch 6, items "Unlinking test", "Chaining, bypass and unlinking" and "With the patch"): the 10 ms is a time constant. The glide is audibly complete at about 46 ms (99 %) and lands exactly at 85 ms (row 2 above).
- **"Fully right: ±5 V sweeps hard left to hard right from the knob"** (Pan controls): true only with the knob at noon. The CV adds to the knob and the sum is clamped, so with the knob at 0.75, +2.5 V is already hard right [meas]. The Pan manual's "from centre" is right.

Everything else I checked in the guide (menu texts and order, defaults, the greyed item, LINK behaviour with no outputs patched, the bypass order of Pans, the law figures, the 150 ms CLIP hold) matches the code.

---

## 2. Ballast: panel, controls and ports

### 2.1 Layout

12 HP (60.96 mm). Drawn as a clinometer. Positions in mm from `BallastLayout.hpp` [code]:

| Control | Centre (x, y) mm |
|---|---|
| Level 1 / 2 / 3 / 4 | 7.6, 52.0 / 17.1, 66.0 / 26.6, 52.0 / 36.1, 66.0 (staggered) |
| Master | 50.35, 34.0 |
| Level CV 1 to 4 | x 7.6 / 17.1 / 26.6 / 36.1, y 83.0 |
| IN L 1 to 4 | same x, y 102.0 |
| IN R 1 to 4 | same x, y 116.0 |
| Master CV | 45.6, 83.0 |
| CLIP light | 55.1, 83.0 |
| CHAIN L, CHAIN R | 45.6, 102.0 and 55.1, 102.0 |
| OUT L, OUT R | 45.6, 116.0 and 55.1, 116.0 |
| Screws | 53.34, 2.54 and 7.62, 125.96 |

### 2.2 Parameter, input, output and light ids

Ids follow the enums in `Ballast.cpp:35-69` and must never change number. Only the engine's own ids (LEVEL_1, MASTER, PARAMS_LEN) are pinned by a `static_assert` (`Ballast.cpp:135`); the rest are not [code].

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 to 3 | Level 1 to 4 | 0..1 | 0.75 | "Level n", dB, `%+.1f`, "-inf" at 0 |
| 4 | Master | 0..1 | 0.75 | "Master", dB |

| Id | Input | | Id | Output |
|---|---|---|---|---|
| 0, 1 | IN 1 L, IN 1 R ("Channel 1 right (normalled to left)") | | 0 | L ("Left (the mono sum when R is unpatched)") |
| 2, 3 / 4, 5 / 6, 7 | IN 2, 3, 4 L and R | | 1 | R |
| 8 to 11 | Level CV 1 to 4 ("0 V silent, 10 V opens to the knob") | | | |
| 12 | Master CV | | **Light** | |
| 13 | CHAIN L ("added before Master") | | 0 | CLIP ("the output is in the soft limit, above 8 V") |
| 14 | CHAIN R ("normalled to chain left") | | | |

The tooltip text says "normalled to left" for IN R only. IN L is also normalled to IN R (section 2.5); the tooltip does not say so.

### 2.3 Level and Master: the curve

[code, `BallastEngine::curveExact`, `curve`]

```
gain = 2 × p^2.4094208        (p = knob 0..1; 0 at p = 0)
```

- Silence fully left, **unity (0 dB) at 0.75** (the long tick), +6.02 dB fully right [test: `curve_points`, 0.75 within 0.01 dB of unity, 1.0 within 0.01 dB of +6.02].
- Gain and tooltip by knob position [meas]:

| Knob | Gain | dB (tooltip shows) |
|---|---|---|
| 0 | 0 | -inf |
| 0.10 | 0.00779 | -42.2 |
| 0.25 | 0.0709 | -23.0 |
| 0.375 | 0.188 | -14.5 |
| 0.50 | 0.3765 | -8.5 |
| 0.625 | 0.644 | -3.8 |
| 0.75 | 1.0000 | +0.0 |
| 0.875 | 1.450 | +3.2 |
| 1.0 | 2.0000 | +6.0 |

- Audio uses a 256-interval table with linear interpolation, built at construction. The table is within 0.05 dB of the exact curve for gains above −60 dB [test: `curve_points`]; the worst point is 0.12 dB at knob 0.021 (gain −73 dB), and from knob 0.1 up it is within 0.0054 dB [meas]. The curve never falls as the knob rises [test].
- Typing a dB value into the tooltip inverts the curve (`BallastLevelQuantity::setDisplayValue`, `Ballast.cpp:18-26`); a non-finite value sets 0. Not covered by a test [unverified in Rack].
- Master has the same curve, range and default. It scales the whole mix, CHAIN and any `intoMix` included.

**Smoothing.** The knob position (not the gain) goes through a one-pole: coefficient `1 − e^(−1/(0.01 × rate))` (0.002081 at 48 kHz), time constant 10 ms at every rate [code: `BallastEngine::setSampleRate`; meas: the same 63 % and 99 % times at 44.1, 48, 96 and 192 kHz]. For a knob step 0.5 to 0.75 the output gain is 63 % there at 11.8 ms and 99 % at 48.6 ms [meas]. The first sample after construction or `reset()` snaps to the knob, so a patch loads without a fade-in [test: `knob_smoothing`, first sample within 0.2 % of the knob's gain]. A Level step to 0 reaches −60 dB within 100 ms with no sample step over 5 % of the input [test].

### 2.4 Level CV and Master CV: the VCA law

[code, `BallastEngine::vca`]

```
gain = knob gain × clamp(CV / 10 V, 0, 1)        (CV unpatched: 1)
```

[meas] one channel, knob at unity:

| CV | Output |
|---|---|
| −3 V, 0 V | 0 (silence) |
| 2.5 V | 0.25 (−12.04 dB) |
| 5 V | 0.5 (−6.02 dB) |
| 7.5 V | 0.75 (−2.50 dB) |
| 10 V, 12 V, 50 V | 1.0 (the knob's level) |
| unpatched | 1.0 |

- **Never above the knob** [test: `vca_law`, `master_cv_law`].
- A polyphonic CV uses its first channel only [test: `poly_cv_uses_channel_1`; meas: 5 V on channel 1 and 10 V on channel 2 gives 0.5].
- **CV is not smoothed.** A 0 to 10 V step reaches the output on the next sample [test: `cv_not_smoothed`; meas 1.00000]. Audio-rate CV is amplitude modulation.
- A bipolar ±5 V LFO leaves the channel silent for its negative half and opens to half level at the top (the manual's tremolo note).
- NaN and ±inf read 0 V. Any |x| of 1e30 or more also reads 0 (`ballastFinite`, `BallastEngine.hpp:31`).

### 2.5 Inputs: normals, mono and stereo

[code, `BallastEngine::process`; test: `normals`, `normals_both_ways`, `poly_inputs_summed`, `source_type`]

- A channel's IN L and IN R are each **summed over all their channels** (up to 16) to one signal per side.
- **Either input alone is a centred mono source**: with only IN L patched, l = r = IN L; with only IN R patched, l = r = IN R. With both patched, l = IN L and r = IN R.
- Source type given to expanders: `MONO` when exactly one of the two is patched; `STEREO` when both are, **or when neither is** [test: `source_type`, all four combinations].
- A channel with neither input patched costs nothing, unless an expander is attached: then its hooks run on zero signals (`BallastEngine.hpp:137`).
- A polyphonic cable on an input is summed, so a 16-channel ±5 V cable can present ±80 V before Level.

### 2.6 CHAIN

[code; test: `normals`, `mix_sum`]

- CHAIN L and CHAIN R are summed (all channels) and added to the mix at unity, **after the channels and the expanders' `postLevel`, before Master and before `intoMix`**.
- CHAIN R is normalled to CHAIN L: with CHAIN R unpatched, the right side gets CHAIN L as well.
- CHAIN is not a channel: no Level, no CV, and **no expander acts on it** (Pan cannot pan it).
- Eight channels from two Ballasts: the first one's L and R into the second one's CHAIN L and R; the second one's Master then sets all eight.

CHAIN is a signal connection. It is a different thing from the physical **expander chain** of section 5.

### 2.7 Outputs and the mono out

[code; test: `mono_normal`, `mono_normal_switch_no_click`]

- L and R carry one channel each.
- **L alone is a mono out**: with R unpatched, L = (mixL + mixR) / √2, taken after Master and before the DC blocker. With R alone patched, R carries just the right side.
- The switch between the two is a **linear ramp of 10 ms** (step 1/(0.01 × rate) per sample, 480 samples at 48 kHz), both when R is patched and when it is unpatched [code]. A 1 V DC case reaches within 1 % of the new level after 9.8 ms [meas]. The step it makes is under twice the steady step plus 0.01 V [test].
- If neither output is patched, the engine does no mixing and writes zeros; the knobs' smoothing and the mono ramp still run (section 1.2, row 9) [test: `nothing_patched_no_work`].

### 2.8 The output stage: DC blocker, soft limit, CLIP

Per side, after Master and the mono sum [code, `BallastEngine::process`, `dcBlock`, `limit`].

**DC blocker.** One-pole high-pass `y = a (y1 + x − x1)`, `a = e^(−2π × 3 / rate)` (0.99961 at 48 kHz). On by default; the menu checkbox is `dcBlock`.

| Frequency | Response [meas, 48 kHz, on against off] |
|---|---|
| 3 Hz | −3.01 dB [test: within 0.5 dB of −3] |
| 5 Hz | −1.34 dB |
| 10 Hz | −0.38 dB |
| 20 Hz | −0.10 dB |
| 30 Hz | −0.045 dB [test: under 0.1 dB; printed `-0.045`] |
| 50 Hz | −0.017 dB |

- 1 V DC reads under 1 mV after 1 s with the blocker on [test: `dc_blocker`]. Off, DC passes exactly [test: `dc_blocker_off`].
- **Turning it on** with a DC offset present fades the offset: with 2 V present the first sample reads 1.9992 V and the largest sample step is 0.0008 V; the offset falls to 1/e at 53.0 ms, 10 % at 122 ms and 1 % at 244 ms [meas; test: `dc_on_no_step`, no step over 0.01 V and under 1 mV after 1 s] (#38, closed). While off, the state follows the input so this works.
- **Turning it off** with an offset present **steps the output by the whole offset in one sample**: 2 V DC gives a 1.9997 V step [meas] [issue #44, open]. On audio it is a click; on CV it may be what you want.
- **Initialize** and a first start zero the state (`reset()`), so an offset can step in.
- A sample-rate change recomputes the coefficients only; the state and the knob glide carry on [test: `rate_change_keeps_state`, 48 to 96 kHz mid-glide: no step, and the glide moves about half as far per sample after the change] (#39, closed).
- Denormals: the output state flushes below 1e-20 [code]; after a loud run and 2,000,000 samples of silence the state is 0 or a normal number [test: `denormals`].

**Soft limit** (Ballast's own copy of Hadal's curve):

```
|x| ≤ 8 V :  y = x
|x| > 8 V :  y = sign(x) × (8 + 2 × tanh((|x| − 8) / 2))     → approaches 10 V
```

[meas, DC blocker off] 7.9 V reads 7.9, 8.0 reads 8.0, 8.01 reads 8.01, 9 V reads 8.924, 10 V reads 9.523, 12 V reads 9.928, 20 V reads 9.99998, 100 V and 1e5 V read 10.000. 7.9 V passes untouched and 8.001 V is continuous [test: `limit_curve`]. A hot mix of every input at ±10 V, all knobs at +6 dB, is finite and within 10 V and lights CLIP [test: `hot_inputs_bounded`]. A 5 V, 440 Hz sine at defaults is never limited [test: `defaults_never_limit`].

Four channels at unity can sum to 20 V on peaks (more with CHAIN and Master at +6 dB); there is no headroom control other than the knobs. **Master is not capped.**

**CLIP light.**

- The engine's flag is set on any sample where either side, after the DC blocker and before the limit, is beyond ±8 V, and held for `int(0.15 × rate + 0.5)` samples, **at least 1** [code]. At 48 kHz: 7,200 samples [test: `clip_hold`, 7,200 ± 1]; 6,615 at 44.1 kHz, 14,400 at 96 kHz, 115,200 at 768 kHz [meas]. At 1 Hz and 3 Hz it lights for 1 sample [test: `clip_hold_min`; meas].
- The lamp is driven with `setBrightnessSmooth(…, sampleTime)` and Rack's default lambda of 30 (`Ballast.cpp:169`; `include/engine/Light.hpp:27`), so it rises and falls with a time constant of about 33 ms on top of the 150 ms hold [code]. How it looks is [unverified].
- It is not a level meter: it reports the soft limit, not the output level.

---

## 3. Ballast: the right-click menu and saved state

[code, `Ballast.cpp:117-127`, `:217-233`]

| Item | Choices | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| DC blocker (3 Hz high-pass on the outputs) | checkbox | on | `"dcBlock"` | JSON boolean | on unless the value is exactly `false` |
| Add Pan expander | action | enabled | none | none | not saved |

The menu, below Rack's standard items, is: a separator, then the DC blocker checkbox, then "Add Pan expander". There is no separator between those two. The "Add Pan expander" item is **greyed out while an unbypassed Pan is in Ballast's expander chain** (section 5.8).

A saved module's `"data"` object is `{"dcBlock": true}`. The five knobs are saved by Rack as ordinary params (ids 0 to 4).

JSON edge cases [code]:

- A missing `dcBlock`, a non-boolean (`0`, `1`, `"false"`, a real) or `null` loads as **on** (#40, closed). Only `false` loads as off.
- Which expanders are beside Ballast is not saved by Ballast: it is rebuilt from the rack's module positions. Whether Rack reports the neighbours to `onExpanderChange` after a patch load is [unverified] (section 5.2).

**Initialize** (`onReset`, `Ballast.cpp:112-115`): sets the DC blocker on and calls `engine.reset()` (zeroes the DC blocker state, clears CLIP, snaps the knob smoothing to the knobs). Rack resets the knobs to 0.75. Because the smoothing snaps and the DC state is zeroed, Initialize during sound can step the output [code]. It does not touch the expander link state. Rack's default `onReset` calls the legacy `onReset()` after resetting params (SDK `Module.hpp:432`, `:459`) [unverified in Rack]. There is no `onRandomize` override, so Randomize randomizes the five knobs [unverified].

**Bypass**: no `configBypass`; see section 1.2, row 10 and section 5.7.

---

## 4. Ballast: signal flow

### 4.1 Block diagram

```
 for channel c = 1..4  (skipped if neither IN L nor IN R is patched and no expander is attached)

 IN L ──sum poly──┐   l = IN L if patched else IN R
 IN R ──sum poly──┴─► r = IN R if patched else IN L           (either alone: centred mono)
                       │
                       ▼   expanders: preLevel(c, l, r)       (chain order; none ships)
                       ▼   × g                                g = curve(smoothed knob) × vca(CV)
                       ▼   expanders: postLevel(c, l, r, MONO | STEREO)     (Pan acts here)
                       ▼
                    mixL += l ; mixR += r              (after all four channels)

 CHAIN L ──sum poly──► mixL += chainL
 CHAIN R ──sum poly──► mixR += chainR   (unpatched: mixR += chainL)

 expanders: intoMix(mixL, mixR)                         (none ships)
 NaN/inf from an expander stops here: mix = finite(mix) (1e30 and over, or NaN: 0)
 × Master = curve(smoothed knob) × vca(Master CV)
 L = mono·(mixL+mixR)/√2 + (1−mono)·mixL   (mono = 1 when R unpatched; 10 ms linear ramp)
 R = mixR
 DC blocker per side (3 Hz, if on)   ──►  CLIP flag if |side| > 8 V (held 150 ms)
 soft limit per side (knee 8 V, ceiling 10 V)  ──►  OUT L, OUT R
```

The expander hooks lie on the same sample as the signal: there is no delay [test: `post_level_same_sample`, a ×0.5 postLevel changes the output on that sample].

### 4.2 Block constants

| Block | Constant [code] |
|---|---|
| Curve | 2 p^2.4094208, 256-interval table |
| Knob smoothing | one-pole on the position, τ 10 ms |
| VCA | clamp(CV/10, 0, 1); unpatched 1; unsmoothed |
| Inputs | sum of up to 16 channels; |x| ≥ 1e30 or NaN reads 0 |
| Mono ramp | linear, 10 ms |
| DC blocker | 3 Hz one-pole high-pass |
| CLIP | knee 8 V, hold 0.15 s (min 1 sample) |
| Limit | knee 8 V, ceiling 10 V, `fastTanh` |
| Rate limits | 0 < rate ≤ 768 kHz; anything else is ignored and the last good rate kept |
| Latency | none; no delay lines; no allocation and no libm per sample (`std::pow` and `std::exp` run only in the constructor and on a rate change) |

---

## 5. The expander protocol

Sources: `src/ballast/BallastExpander.hpp` (the interface), `BallastChain.hpp` (the walk and the linker), `Ballast.cpp:71-165` (the use), `BallastEngine.hpp:116-185` (the calls).

### 5.1 The contract

An expander is a module that implements `rka::BallastExpander`. Ballast calls it, **in chain order, on the same sample**, through these methods. Every method defaults to doing nothing.

| Method | When | Purpose |
|---|---|---|
| `kind()` | during the walk, once per module per sample | A kind number. Nonzero (1 to 31): Ballast keeps only the first unbypassed expander of that kind. 0 (the default): any number may chain. Pan is kind 1 |
| `attached()` | once, on the first sample the expander is in the chain after not being in it | The expander restarts its smoothing. Comes from Ballast's thread |
| `linked()` | **every sample**, for each kept expander, before any other call, whether or not an output is patched | Read the expander's controls, advance its smoothing, know it is linked |
| `preLevel(ch, l, r)` | per channel, before Level and CV | May change a channel's input (a future Offset). Pan does not use it |
| `postLevel(ch, l, r, src)` | per channel, after Level and CV | Change the channel's stereo pair. `src` is `MONO` (exactly one input patched; l and r both hold that signal) or `STEREO`. **Pan acts here** |
| `intoMix(l, r)` | after CHAIN, before Master | Add to the mix (a future send return). Pan does not use it |

`ch` runs 0 to 3. The calls do not allocate or block. Each channel runs `preLevel` for every expander, then Level and CV, then `postLevel` for every expander, before the next channel (`BallastEngine.hpp:143-150`).

Within one sample the order is: `attached()` where it applies, then `linked()` for every kept expander (all of them, in chain order), then the engine's per-channel calls, then `intoMix` [code: `BallastLinker::link`, then `BallastEngine::process`].

`linked()` runs even when neither Ballast output is patched. `preLevel`, `postLevel` and `intoMix` do not (the engine returns first), so Pan keeps smoothing and LINK stays lit with no outputs patched, but nothing is panned [code; test: `linked_every_walk`; listening guide agrees].

With **no expander attached, Ballast's output is bit-identical** to one without the connection: an engine run with no chain, an empty chain and a unity expander agree to the bit over 10 s of noise, with the DC blocker on and off [test: `no_expander_bit_identical`]. Attaching and detaching an expander for one sample leaves the later output matching a fresh engine (bit for bit with the blocker off; within 1e-5 V after 1 s with it on, since the blocker keeps the one changed sample) [test: `expander_attach_detach`]. A NaN from an expander is stopped at the mix and the outputs recover [test: `expander_nan_recovers`].

### 5.2 Chaining: which modules count

[code, `collectBallastExpanders`, `BallastChain.hpp:17-40`]

The walk starts at **Ballast's right-hand neighbour** (`rightExpander.module`, which Rack sets for modules placed edge to edge in a row) and goes right, one module at a time, through each module's own `rightExpander.module`. For each module `m`:

1. If `m` is not a `BallastExpander` (a `dynamic_cast` fails), **the walk ends**. That is the end of the chain, even if more expanders follow.
2. If `m` is bypassed, it is **skipped**: not called, does not claim its kind, and **does not end the chain**.
3. If `m`'s kind is nonzero (1 to 31) and an earlier unbypassed module already had that kind, `m` is **skipped** (not called, does not end the chain).
4. Otherwise `m` is kept.

Consequences:

- Expanders must touch, in an unbroken row, to Ballast's right. A gap, a module of another family, or a different row ends the chain.
- Order is physical, left to right. The hooks run in that order. (The only shipped expander, Pan, does not depend on order.)
- **Only Pan is a Ballast expander.** The only classes using `BallastExpander` are `PanCore` and the test stand-ins [code: grep of `src/`]. **Neap's Spring expander (2.0.38 and later) is not one**: it belongs to Neap's own interface (`NeapExpander.hpp`). A Spring placed against Ballast's right does nothing and **ends the chain**; a Pan behind it is not reached.
- A Ballast to the right of a Ballast is not an expander and ends the chain. "Ballast, Pan, Ballast, Pan" works: each Pan serves the Ballast on its left (the walk from the first Ballast stops at the second).
- A Pan on Ballast's **left**, or on the far side of a stranger, does nothing and its LINK stays dark.
- Walk cost: the walk is skipped entirely when the immediate right neighbour is not an expander. `expanderRight` is kept by `onExpanderChange` for side 1 (`Ballast.cpp:107-110`), by a `dynamic_cast` on the neighbour. Otherwise it runs on every sample, with one `dynamic_cast` per module walked (section 8.1).
- Whether Rack calls `onExpanderChange` for every neighbour after a **patch load** is [unverified]. If it did not, `expanderRight` would stay false and Pan would not link until the rack is changed. The listening guide's last group ("Pan is still beside Ballast") tests this.
- A bypassed first neighbour still sets `expanderRight` (it is an expander); the walk then skips it.

The test stand-in covers: bypassed skipped and a stranger ending the chain (`walk_skips_bypassed_and_stops_at_strangers`), one of each kind (`one_of_each_kind`), a looped chain (`walk_ends_on_a_loop`: two Pans pointing at each other keep 1, two bypassed ones keep 0 and the walk ends) [test].

### 5.3 The maximum number of expanders

Four limits act at once [code]:

| Limit | Value | Where | What it does |
|---|---|---|---|
| Called per sample | **8** | `Ballast::MAX_EXPANDERS`, `Ballast.cpp:72` | The walk stops after 8 kept expanders. "A longer chain is cut here" |
| Modules looked at per walk | **32** | `MAX_STEPS`, `BallastChain.hpp:15` | Counts every module visited, **including bypassed and duplicate ones**, so a looped chain ends. An expander after 32 steps is never reached |
| Per nonzero kind | **1** (kinds 1 to 31) | `BallastChain.hpp:32-36` | The first unbypassed one of each kind. Kind 0, and any kind of 32 or more (or negative), are not limited by this rule |
| Linker memory | 16 | `BallastLinker::MAX`, `BallastChain.hpp:48` | Never reached, because the walk keeps at most 8 |

[meas, walk over stand-in nodes]:

- 12 kind-0 expanders with max 8: 8 kept; with max 16: 12 kept.
- 40 Pans (kind 1): **1** kept.
- 31 bypassed Pans, then live Pans: 1 kept (the live one is the 32nd step). **32 bypassed Pans, then live Pans: 0 kept** (the step cap).
- 40 expanders of kind 31: 1 kept. 40 expanders of kind 40: 8 kept (kind 40 is treated as repeatable).
- expander, stranger, expander: 1 kept.

So, today: **at most one Pan acts per Ballast**, and the chain can hold more Pans in the row, but they do nothing and their LINK is dark. The 8 limit has no effect until an expander of kind 0 ships. The 8 and 32 limits are not on the panel, in either manual, or in the spec. A test pins `max` (`walk_skips_bypassed_and_stops_at_strangers`: max 1 keeps 1) but the figure 8 and the 32-step cap's value are not asserted by any test [code].

### 5.4 One of a kind: the second Pan

- A second Pan behind the first is **not called** and **does not end the chain**: its LINK stays dark [test: `one_of_each_kind`].
- If the first Pan is **bypassed**, it does not claim the kind, so the second acts and its LINK lights. If the first is later un-bypassed, it takes over again (it is first in chain order).
- The second Pan, taking over, **attaches** (section 5.6): it glides in from centre while the first's removal was a step to centre. So bypassing the first Pan of a pair is a step to centre followed by a glide to the second's positions [code; meas of the same sequence with one Pan: section 5.6].
- "Add Pan expander" is greyed while an unbypassed Pan is in the chain; with the Pan bypassed it is enabled, and a new Pan placed at Ballast's side then acts ahead of the bypassed one.

### 5.5 Bypass

| What is bypassed | Effect |
|---|---|
| **Pan** | Ballast's walk skips it; Ballast sounds as without it. The position is a **step back to centre** in one sample. Pan's `processBypass` sets LINK to 0 at once (`Pan.cpp:107-110`). Un-bypassing attaches it again: **a glide in from centre** |
| **Ballast** | Rack does not call `process()`. No links happen, so Pan's LINK goes dark within 50 ms. Both outputs go silent only if Rack zeroes outputs that have no bypass route [unverified; section 1.2, row 10]. Un-bypassing: Pan's position was in the previous chain (`last_` is untouched), so it is **not** "newly attached" and **does not glide**; it resumes from where it was [meas: position 1.00000 on the first sample after Ballast is released] |
| A second Pan | Not called either way (section 5.4) |
| Both | Both are skipped; the same as Pan alone |

### 5.6 Linking and unlinking

Events, and what each does, for one Pan with a knob turned (say hard right) on a mono source [code; the meas figures are the Rack-free path through `BallastLinker` and `BallastEngine`, 48 kHz, 1 V DC mono, Level unity, DC blocker off]:

| Event | Pan's position | Sound | LINK |
|---|---|---|---|
| Patch loads, Pan beside Ballast | first `linked()` restarts from centre (`first_`) and glides to the knob | L starts at 0.998 and falls as R holds 1.000; a 10 ms time constant | lights at the first link |
| "Add Pan expander" or place Pan against Ballast | `attached()`, then glide from centre | as above | lights |
| Pan dragged away (a gap), or any stranger put between, or a different row | removed from the chain; state frozen | **step back to centre in one sample**: (L 0, R 1) becomes (1, 1) in No boost; (0, 1.414) becomes (1, 1) in Constant loudness [meas]; stereo true pan from (0, 0.25) to (1, −0.5) for L = 1, R = −0.5 [meas] | dark 50 ms later (2,400 samples at 48 kHz) |
| Put back | `attached()`, glide from centre | glide, no click | lights |
| Pan bypassed / un-bypassed | as removed / as attached | step, then glide | dark at once / lights |
| Ballast bypassed / released | no links; frozen / resumes with no restart | silent (see above) / resumes | dark within 50 ms / lights |
| Rate change | coefficients only | no step | no change |
| Knob turned while linked | one-pole to the knob | 63 % in 10.0 ms, 99 % in 46.1 ms [meas] | |

- **The glide is 63 % in 10.0 ms, 90 % in 23.0 ms, 99 % in 46.1 ms and exact at 85.2 ms** (the last 1e-4 is snapped, because a float one-pole stalls about 3e-5 short) [meas; code `PanCore.hpp:67-71`]. The first output sample is at position 0.5010 (48 kHz) or 0.5005 (96 kHz).
- The glide restarts from **0.5, regardless of CV**; the CV is added after the smoothing and is not smoothed (`PanCore.hpp:72`).
- There is **no test of the load behaviour**. `attach_glides_from_centre` calls `attached()` on a settled Pan; the first-link path (`first_`) is exercised only because every test's `settle()` starts from a fresh Pan. Neither snap-on-load nor glide-on-load is asserted [code; issue #42].
- A removed Pan is a **step**, by design, and the manual says so ("as unpatching a cable would be"). The spec's reason: a fade would need a crossfade in Ballast.
- **LINK** (`Pan.cpp:40-47`, `:100-117`): a counter `idle_` that Ballast's thread sets to 0 in `linked()` and Pan's `process()` increments by one per call (an atomic, capped at 2^20). LINK is lit while the count is below `0.05 × sample rate` (2,205 at 44.1 kHz, 2,400 at 48 kHz, 4,800 at 96 kHz, 38,400 at 768 kHz). It is a hard 0 or 1 (`setBrightness`), and starts dark (`idle_` starts at 2^20).
- The counter works only as long as Pan's `process()` runs once per sample (it does in Rack's normal use). A bypassed Pan's `processBypass` sets LINK to 0 each sample.

### 5.7 The linker: what "newly attached" means

[code, `BallastLinker::link`, `BallastChain.hpp:46-68`; test: `attached_on_change`]

Ballast keeps last sample's kept expanders as bare addresses (compared, never dereferenced). Each sample, `attached()` is called on every kept expander that was not in last sample's list, then `linked()` on every one, then the list is replaced. Tested sequences: first link gives 1 attach; 1,000 more links add none; a second expander joining adds one for it only; one that leaves and returns gets a second; an empty chain then one expander gives a second attach.

Also: a brand-new Pan restarts from centre on its first `linked()` regardless (`first_`), so a new object at a reused address is still handled.

The 2.0.8 and 2.0.9 builds decided this from a counter that both threads wrote. With several engine threads the two raced and lost updates, so Pan restarted from centre every few samples (crackle). `attached()` was added in 2.0.10 for that reason (section 5.9).

### 5.8 "Add Pan expander" (Ballast's menu)

[code, `Ballast.cpp:203-233`; spec "Add Pan expander in Ballast's menu"]

- **Label**: "Add Pan expander". **Position**: second item of Ballast's own menu section (after the DC blocker). Built when the menu opens.
- **Enabled** unless an unbypassed `PanCore` is in the chain. The check repeats the walk from `rightExpander.module` (cap 8) and tests `kind() == PanCore::KIND` (1). A Pan that is bypassed, or beyond a stranger, or to the left, does not grey the item. It does not update while the menu is open.
- **Action** (`addExpander(modelPan)`): creates a new Pan with default params (not a copy); adds it to the engine (`APP->engine->addModule`); creates its widget; **places it at Ballast's right edge** (`box.pos.x + box.size.x`, the same y) with `setModulePosForce`, which pushes whatever is there to the right; adds it to the rack; and pushes one `history::ModuleAdd` named "create Pan". So it is **one undo step**. It is modelled on Venom's `addExpander`.
- The new Pan is adjacent, so Rack links it and Ballast's `onExpanderChange` fires; on its first sample `first_` and `attached()` start it from centre (a no-op with every knob at noon).
- **Undo** removes the Pan but **does not move back the modules that were pushed along** [issue #42; Venom does the same]. Redo adds it again.
- If a Spring or any other module is on Ballast's right, the Pan is inserted between them and the other module is pushed; the chain then reads Ballast, Pan, other.
- Not covered by an automated test (it needs `APP`) and **not run in Rack**; the listening guide has it as an "untested" item [unverified].

### 5.9 Threads

[code; Rack's own threading is [unverified]]

Rack may run Ballast's `process()` and Pan's `process()` on different engine threads, at the same time. The design keeps the audible work on Ballast's thread:

| State | Written by | Read by | Protection |
|---|---|---|---|
| Pan's params and CV voltages | the UI thread, or another module's thread (cables) | Ballast's thread, in `linked()` (`Pan::readFrame`) | none: a plain float read, benign by design [unverified under TSan] |
| Pan's smoothing (`knobS_`), `pos_`, `first_`, `restart_`, laws | Ballast's thread only (`linked`, `attached`) | Ballast's thread (`postLevel`) | single thread |
| `idle_` (LINK counter) | Ballast's thread stores 0; Pan's thread adds 1 | both | `std::atomic<int>`, relaxed. A lost update only shifts LINK by a sample |
| Pan's LINK light | Pan's `process()` / `processBypass` only | Rack | single thread |
| `stereoMode`, `panLaw` | UI thread (menu, load) | Ballast's thread | plain `int`: a benign race. A change takes effect on a sample boundary or one sample late |
| Ballast's `dcBlock` | UI thread (menu pointer) | Ballast's thread | plain `bool` |
| `expanderRight` | `onExpanderChange` | Ballast's `process()` | Rack's engine lock [unverified] |
| `BallastLinker::last_` | Ballast's thread | Ballast's thread | single thread; addresses only |
| The kept-chain array | Ballast's `process()` (a stack array, rebuilt every sample) | the same call | **no pointer is cached across samples** |

- Adding or removing a module takes Rack's engine write lock, so the neighbours cannot change under a running `process()`, and Pan cannot be destroyed while Ballast calls it [unverified: this is the design's stated premise (spec, `Ballast.cpp:153-155`), not something I could check without Rack's source].
- Pan's `process()` only drives LINK. The hook calls never run from Pan's `process()`.
- **Known gap [issue #42]**: while modules are being dragged, Rack's expander links can briefly leave two Ballasts each seeing the same Pan. Both then call `linked()` and `postLevel()` for one block and Pan's smoothing advances twice. At worst a short glitch while dragging. Not guarded or documented in `BallastExpander.hpp`.
- **No automated thread test exists** for Ballast and Pan. TESTING.md's table says "Not needed" for Ballast and "Not needed (one atomic counter)" for Pan. The 2.0.10 fix is covered only by `attached_on_change`, which proves the linker's logic, not the race. The crackle regression can only be seen in Rack with more than one engine thread [unverified; section 11.4].

---

## 6. Pan

### 6.1 Panel

6 HP (30.48 mm). Four rows centred on the panel, one per Ballast channel; the **PAN** knob, its **attenuverter**, then the **CV** jack, traced right to left; "CV" over the jack column; the **LINK** light low and centred; the short foot mark "R K A". Positions in mm from `PanLayout.hpp` [code]:

| Control | Centre (x, y) mm |
|---|---|
| Pan 1 to 4 knobs | 6.4, y 46 / 64 / 82 / 100 |
| Attenuverters 1 to 4 | 15.24, same y |
| CV jacks 1 to 4 | 24.08, same y |
| LINK light | 15.24, 112.0 |
| Screws | 22.86, 2.54 and 7.62, 125.96 |

### 6.2 Parameter, input and light ids

[code, `Pan.cpp:14-38`, `:50-61`]

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 to 3 | Pan 1 to 4 | 0..1 | 0.5 | "Pan n", %, shown −100 to +100 (`value × 200 − 100`) |
| 4 to 7 | Pan n CV amount (attenuverters) | −1..+1 | 0 | "Pan n CV amount", %, −100 to +100 |

| Id | Input | | Output / light |
|---|---|---|---|
| 0 to 3 | Pan 1 to 4 CV ("+-5 V sweeps the width at full amount") | | no outputs |
| | | | light 0: LINK ("Linked to a Ballast") |

Pan is silent on its own: it has no audio ports. Its `process()` only sets LINK.

### 6.3 Position and the CV law

[code, `PanCore::linked`, `PanCore.hpp:59-77`; test: `cv_law`, `poly_cv_uses_channel_1`; meas]

```
position = clamp( smoothed knob + (CV / 10 V) × attenuverter , 0, 1 )     (0.5 = centre)
p = 2 × position − 1                                                       (−1 hard left, 0 centre, +1 hard right)
```

[meas]:

| Knob | Attenuverter | CV | p |
|---|---|---|---|
| 0.5 | +1 | +5 V / −5 V / +2.5 V | +1 / −1 / +0.5 |
| 0.5 | −1 | +5 V | −1 (inverted) |
| 0.5 | 0 | +5 V | 0 (CV ignored) |
| 0.5 | +0.5 | +5 V | +0.5 |
| 0.75 | +1 | +5 V | +1 (clamped; +2.5 V already reaches it) |
| 0.5 | +1 | +50 V | +1 |
| 0 | +1 | +10 V | +1 |

- At full attenuverter, ±5 V from a **centred** knob sweeps the whole width. From any other knob position the sum clamps earlier.
- The CV is **not smoothed**: a 0 to 5 V step lands on the same sample [test: `knob_smoothing`; meas p = +1.0000], so audio-rate auto-pan works. Only the knob is smoothed (section 6.4).
- A polyphonic CV uses its first channel only [test]. NaN and ±inf on a CV read 0 V [meas: position stays 0.500; test: `nan_cv_safe`]. The attenuverter and the knob are also sanitised by `ballastFinite`.
- An unpatched CV jack reads 0 V, so the attenuverter does nothing.
- The Pan manual's "attenuverter defaults to 0" is the module's default: a patched CV does nothing until it is turned up.

### 6.4 Knob smoothing

[code, `PanCore.hpp:53-57`, `:63-74`; test: `knob_smoothing`]

- One-pole on the knob, `1 − e^(−1/(0.01 × rate))`, time constant 10 ms, coefficient set by `setSampleRate` (anything not in (0, 768 kHz] is ignored [test: `bad_rate_ignored`]).
- A full-scale knob step: 63 % at 10.0 ms, 90 % at 23.0 ms, 99 % at 46.1 ms [meas]. The test requires a settle within 1 % in under 50 ms and no step over 5 % of the step in one sample.
- Within 1e-4 of the knob the smoothed value snaps to it.

### 6.5 The pan laws

[code, `PanCore::postLevel`, `PanCore.hpp:79-110`; test: `mono_law`, `true_pan`, `no_boost`, `balance`, `mono_equal_power_under_cv`, `mono_no_boost_dip_under_cv`]

At **p = 0 the function returns at once**, so a centred Pan is the identity, bit for bit (random stereo and mono input on all four channels, 100,000 samples, both Stereo-source modes and both laws: bit-identical to no Pan [meas; test: `centre_is_identity`]).

**Mono sources** (`MONO`; l == r): θ = (p + 1) π / 4; gL = √2 cos θ, gR = √2 sin θ. **Law 0 (No boost, the default)** holds each gain to 1. **Law 1 (Constant loudness)** leaves them.

Gains on a mono 1 V signal [meas]:

| Knob | p | Law 0: L, R | Law 0: power re centre | Law 1: L, R | Law 1: power |
|---|---|---|---|---|---|
| 0 | −1 | 1.0000, 0.0000 | −3.01 dB | 1.4142, 0.0000 | 0 dB |
| 0.25 | −0.5 | 1.0000, 0.5412 | −1.89 dB | 1.3066, 0.5412 | 0 dB |
| 0.5 | 0 | 1.0000, 1.0000 | 0 | 1.0000, 1.0000 | 0 |
| 0.75 | +0.5 | 0.5412, 1.0000 | −1.89 dB | 0.5412, 1.3066 | 0 dB |
| 1 | +1 | 0.0000, 1.0000 | −3.01 dB | 0.0000, 1.4142 | 0 dB |

So No boost: the side it is on stays at the level it had at centre, the other side fades, and the **total power dips 3.01 dB at the extremes** (an LFO auto-pan dips at each extreme). Constant loudness: +3.01 dB on its side hard over.

**Stereo sources, true pan** (`stereoMode` 0, the default), for p ≥ 0, t = p π / 2, s = sin t, c = cos t:

```
L' = L × c
R' = R × (1 − (1 − k) s) + L × k × s            k = 1/2 (law 0)  or  1/√2 (law 1)
```

mirrored for p < 0. Hard right folds the left side into the right: (L + R)/2 under No boost, (L + R)/√2 under Constant loudness.

**Stereo sources, balance** (`stereoMode` 1): p ≥ 0: L' = L cos t, R' = R; mirrored. Hard right silences L. Never boosts, under either law.

Stereo results [meas] (inputs (1, 0) and (0, 1) shown as `(L', R')`):

| Mode and law | Knob 0.5 | Knob 1.0 (hard right) | Knob 0.75 |
|---|---|---|---|
| True pan, No boost | (1, 0) and (0, 1) | (0, 0.5) and (0, 0.5) | (0.707, 0.354) and (0, 0.646) |
| True pan, Constant | same | (0, 0.707) and (0, 0.707) | (0.707, 0.5) and (0, 0.793) |
| Balance, either law | same | (0, 0) and (0, 1) | (0.707, 0) and (0, 1) |

- **No boost** never leaves a sample louder than the louder of the channel's two sides went in, at any pan, mono or stereo, true pan or balance [test: `no_boost`]. The default no-boost law's mono power dips about 3 dB at the sides under a 1 kHz pan CV and never rises above centre [test: `mono_no_boost_dip_under_cv`]; constant loudness holds equal power under the same CV [test: `mono_equal_power_under_cv`].
- True pan keeps L + R content (no loss at hard right) [test: `true_pan`]. A near-mono stereo source in true pan with Constant loudness gains up to 3 dB hard over.
- Computed with `fastSin2pi`; no libm per sample [code].
- Pan acts on the **post-Level** pair. A Level CV still scales before Pan; Master and the DC blocker and limit come after. A hard pan on a hot mix can still drive the limit if the mix is hot (Constant loudness adds up to +3 dB per channel).

### 6.6 Menu and saved state

[code, `Pan.cpp:68-86`, `:142-153`]

| Item | Choices (menu text) | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| Pan law | "No boost (panning never raises a level)", "Constant loudness (up to +3 dB at the sides)" | No boost | `"panLaw"` | integer 0 or 1 | 1 only when the value is the integer 1; anything else, or missing, is 0 |
| Stereo sources | "True pan (move the whole image)", "Balance" | True pan | `"stereoMode"` | integer 0 or 1 | 1 only when the value is the integer 1; anything else, or missing, is 0 |

A saved module's `"data"` object is `{"stereoMode": 0, "panLaw": 0}`. The eight knobs and attenuverters are saved by Rack as params (ids 0 to 7). Out-of-range values are not clamped: `"stereoMode": 2` loads as True pan, and a real such as `1.0` loads as 0 (Jansson's `json_is_integer` is false for reals) [code]. **Initialize** (`onReset`) sets both to 0 and Rack resets the knobs; it does not restart the smoothing, so the knobs glide to centre over about 46 ms [code]. Menu changes take effect on the next sample and are not smoothed: switching law or mode during sound can step the pan.

### 6.7 What the spec and manual promise at attach and removal

- Attaching with knobs at noon changes nothing.
- Attaching a panned Pan glides in from centre (section 5.6); the measured glide figures are in that section.
- Removing or bypassing a panned Pan is a step to centre (section 5.6). The size of the step is the whole difference between the panned and the centred gain: up to 1.0 of the input on the near-side output for a hard-panned mono source.

### 6.8 Signal flow

```
 Ballast, per channel c, per sample:
   ... × Level × CV  →  (l, r) , src = MONO | STEREO
                         │
   Pan (if kept):        ▼
     linked()  once per sample, before:   knobS += (knob − knobS) × smooth      (τ 10 ms; restart at 0.5 on attach or first link)
                                          position = clamp(knobS + CV/10 × att, 0, 1)
     postLevel(c):   p = 2·position − 1 ;  p == 0 → unchanged
                     MONO   → gL, gR (law 0: min(1, ·); law 1: ·)
                     STEREO → true pan (k by law) or balance (mode)
                         │
                         ▼
   (l, r)  →  mix  →  CHAIN  →  Master  →  mono sum  →  DC blocker  →  limit
```

---

## 7. Sample rates and robustness

### 7.1 Sample rates

- Both modules accept 0 < rate ≤ 768,000 Hz. Zero, negative, NaN, infinite and 1e9 are ignored and the last good rate kept [test: `bad_rate_ignored` in both hostile tests]. Rates from 1 Hz to 768 kHz run without non-finite output or output over 10 V [test: `extreme_rates`].
- All time figures (10 ms smoothing, 10 ms mono ramp, 3 Hz corner, 150 ms CLIP, 50 ms LINK) scale with the rate [code; meas at 44.1 to 768 kHz for CLIP and LINK; the smoothing times at 44.1 to 192 kHz].
- A rate change keeps the DC blocker state and the knob glide and only recomputes coefficients [test: `rate_change_keeps_state`; `rate_change_recovers`].

### 7.2 Hostile input

- NaN and ±inf on every input, CV, CHAIN and Pan CV read 0 [test: `nan_inputs_safe`, `nan_cv_safe`].
- Fuzz, 10^6 samples each, fixed seed: Ballast's knobs (−0.1 to 1.1), CVs (±12 V), polyphony counts (0 to 16), patch states, rates; Pan's knobs, attenuverters, CV, channel counts, Stereo-source mode (including the invalid 2), random `attached()` and rates. Outputs stay finite and within ±10 V [test: `seeded_fuzz` in both]. **Pan's fuzz never sets `panLaw`**, so Constant loudness is not fuzzed (the laws are covered by `test_core` instead) [code].
- No allocation in `process()`: zero allocations over 200,000 samples with outputs toggling, hostile values and a Ballast and Pan pair [test: `no_allocation_in_process`, `no_allocation`].
- No writable global state: the engine-globals check passed. Each instance owns all its state.
- Denormals: section 2.8.

### 7.3 What is not covered

- Aliasing: the soft limit and audio-rate level CV (amplitude modulation) are not measured [issue #67: Ballast "low priority", Pan "not looked at; expected to need a note, not a fix"].
- Polyphony: both modules sum polyphonic cables; whether they should stay that way is open [issue #64].

---

## 8. CPU

### 8.1 Bench figures and budgets

`make bench`: CPU share of one instance at 1, 4 and 16 instances, Apple M-series, 48 kHz, plugin flags. Budgets are about twice the measurement; elsewhere three times (TESTING.md). I ran the bench with the machine busy (load average about 8); figures are the range of three 4-second runs of each case at one instance, and the 16-instance figures from the full run.

| Case | Budget (% of a core) | TESTING.md measured | This run, 1 instance | This run, 16 instances |
|---|---|---|---|---|
| Ballast idle (nothing patched) | 0.05 | 0.017 | 0.018 to 0.019 | 0.016 |
| Ballast outputs (all inputs, both outputs) | 0.25 | 0.105 | 0.098 to 0.105 | 0.138 |
| Ballast busy (plus every CV moving) | 0.3 | 0.128 | 0.112 to 0.128 | 0.132 |
| Pan with Ballast, idle | 0.1 | 0.034 | 0.041 to 0.045 | 0.048 |
| Pan with Ballast, outputs (channels 1 and 3 stereo, 2 and 4 mono; knobs off centre) | 0.4 | 0.187 | 0.220 to 0.236 | 0.302 |
| Pan with Ballast, busy (every Pan CV moving at full attenuverter) | 0.45 | 0.211 | 0.245 to 0.270 | 0.325 |

The Ballast and Pan cases are within budget [meas: the full `make bench` run; I did not record its final verdict line for the other modules]. Per-instance cost does not grow with the count (16 against 1: Ballast ×0.88, ×1.36, ×1.07; Pan ×1.04, ×0.97, ×0.99; the outputs case at 16 is the noisiest on a busy machine).

Pan's own share is the pair minus Ballast alone: about 0.12 to 0.14 % of a core on outputs (the manual says "about 0.1 %").

**The bench does not include the chain walk.** Its Pan case calls `core.linked()` and `engine.process(…, chain, 1)` directly (`src/pan/bench.inc`), without the `dynamic_cast` walk Ballast does in Rack. A stand-in with a polymorphic base and a `rightExpander` pointer costs, per sample [meas, stand-in; Rack's `Module` is larger and Windows' RTTI may differ, so treat as an order of magnitude]:

| Walk | ns per sample | % of a core at 48 kHz |
|---|---|---|
| Ballast, Pan, nothing | 19 to 20 | 0.09 to 0.10 |
| Ballast, Pan, another module | 32 | 0.15 to 0.16 |
| Ballast, Pan, Pan, another module | 54 to 56 | 0.26 to 0.27 |
| Right neighbour is not an expander | skipped (`expanderRight` false) | 0 |

So with a Pan attached the walk can cost about as much as Pan's own DSP. This is [unverified in Rack].

### 8.2 CPU rules

- With no expander, the walk is skipped and Ballast's output and CPU are those of a Ballast without the connection [test: `no_expander_bit_identical`; bench].
- Pan runs inside Ballast's `process()`, so Pan's cost moves with Ballast's thread.
- No libm per sample (code review): `std::pow`, `std::exp` run in the constructor, on a rate change and in `curveExact` (tests and the tooltip). The libm-calls check could not run on macOS.

---

## 9. Open issues

### 9.1 Open issues about these modules: do not re-file

| Issue | Summary |
|---|---|
| #42 Pan: small behaviour gaps carried from the build | (1) A panned channel glides in from centre on load (about 10 ms time constant); the spec says it should snap on load and glide only on attach. (2) Two Ballasts can call one Pan for a block while modules are dragged; the smoothing advances twice. Document it in `BallastExpander.hpp` or guard it. (3) Undoing "Add Pan expander" leaves the pushed neighbours where they are (Venom does the same) |
| #44 Ballast: turning the DC blocker off with an offset present steps the output | 2 V of DC gives a 2 V step in one sample [meas 1.9997 V]. Decide whether to fade it or document it |
| #36 Process: libm-calls check is skipped on macOS | I could not run the libm check for these engines |
| #67 Aliasing: measure every module | Ballast's soft limit "low priority"; Pan "not looked at" |
| #64 Polyphony | Ballast and Pan sum polyphonic cables; "probably stay as they are; confirm" |

### 9.2 Closed issues that explain present behaviour

- #38 turning the DC blocker on with DC present stepped the output; now it fades (53 ms time constant).
- #39 a rate change cleared the DC blocker and snapped the knob smoothing; now the state carries on.
- #40 the expander's mono flag could not tell IN R alone from stereo; a non-boolean `dcBlock` loaded as off; CLIP hold could round to 0; the manual overstated "no work". All fixed in 2.0.8.
- #43 test and doc gaps (the 48 to 48 kHz rate test, `panLaw` missing from TESTING.md, the no-boost dip test). Fixed in 2.0.33.

### 9.3 Found while preparing this document (not filed; for QA to decide)

1. **Ballast manual line 23**: "The right-click menu has one setting: the DC blocker." Stale; the menu has two items (section 1.2, row 1).
2. **"About 10 ms"** in both manuals and the guide is a time constant. The glide and the knob smoothing take about 46 ms to reach 99 % (section 1.2, rows 2 and 6).
3. **The Level tooltip at unity is "+0.0 dB"**, not "0.0" as the listening guide says.
4. **Ballast spec** still gives the old mono rule (section 1.2, row 3).
5. **The bench omits the chain walk**; Pan's real CPU in Rack is likely higher than the bench says (section 8.1).
6. **Pan's hostile fuzz never sets `panLaw`**.
7. **Maximum expanders (8 called, 32 modules walked) is in no document.**
8. **A patch saved in 2.0.8 loads with a different pan law** (it has no `panLaw` key; section 6.6).
9. **Unbypassing Ballast does not glide Pan** (it was never "detached"), while bypassing and unbypassing Pan does.
10. **`BallastEngine.hpp:17-18`** says "nothing is computed" with no outputs patched; the smoothing and mono ramp still run.

None of these is an audible defect on its own.

### 9.4 Known limits that are not defects

- A removed or bypassed Pan is a step to centre.
- Pan's law menu changes are unsmoothed.
- No Ballast pan; CHAIN is not panned.
- The default No boost law dips a mono source's power about 3 dB at the sides.
- Initialize on Ballast snaps the knob smoothing and zeroes the DC state.

---

## 10. What the automated suite covers

All run and passed. They are Rack-free; no test loads Rack, the menus, the panel, the cables, or the plugin.

### 10.1 `ballast/test_engine` (22 tests)

`curve_points` (0, unity, +6.02, monotone, table within 0.05 dB for gain over 1e-3); `vca_law`, `master_cv_law` (unpatched = knob, 0 V and below silent, 5 V −6 dB, 10 V and 12 V the knob, never above); `cv_not_smoothed`; `poly_cv_uses_channel_1`; `normals`, `normals_both_ways`, `poly_inputs_summed`, `mix_sum` (channels, CHAIN, CHAIN R normal, Master); `mono_normal`, `mono_normal_switch_no_click` (both directions); `knob_smoothing` (no fade-in on load, no step, under 100 ms to −60 dB); `nothing_patched_no_work`; `dc_blocker` (1 V DC gone, 30 Hz −0.045 dB, 3 Hz −3.01 dB), `dc_blocker_off`; `limit_curve`, `hot_inputs_bounded`, `clip_hold` (7,200 ± 1), `defaults_never_limit`; `dc_on_no_step` (#38); `rate_change_keeps_state` (#39, 48 to 96 kHz); `clip_hold_min` (#40).

### 10.2 `ballast/test_expander` (the hook, 13 tests; two of them run with the DC blocker on and off)

`post_level_same_sample`, `pre_level_before_level`, `into_mix_before_master`, `source_type` (all four patch combinations), `chain_order` (×2 then +1 gives 3), `no_expander_bit_identical`, `expander_attach_detach`, `expander_nan_recovers`, `walk_skips_bypassed_and_stops_at_strangers` (also max), `walk_ends_on_a_loop`, `one_of_each_kind`, `linked_every_walk`, `attached_on_change`.

### 10.3 `ballast/test_hostile` (ASan and UBSan, `-O1`)

`nan_inputs_safe`, `bad_rate_ignored`, `rate_change_recovers`, `extreme_rates` (1 Hz to 768 kHz), `no_allocation_in_process`, `seeded_fuzz` (10^6 samples), `denormals`.

### 10.4 `pan/test_core`

`centre_is_identity` (bit-exact, both modes and laws), `mono_law`, `mono_equal_power_under_cv`, `mono_no_boost_dip_under_cv`, `true_pan` (both laws), `no_boost`, `balance`, `cv_law`, `knob_smoothing` (and CV unsmoothed), `attach_glides_from_centre`, `no_restart_without_attach` (48,000 links, never dips), `poly_cv_uses_channel_1`.

### 10.5 `pan/test_hostile` (ASan and UBSan, `-O1`)

`nan_cv_safe`, `bad_rate_ignored`, `no_allocation` (a Ballast and Pan pair, 200,000 samples), `seeded_fuzz` (10^6 samples; `panLaw` never set).

### 10.6 Also run

`bench --check` (15 module declarations complete), the bench, `engine-globals`. Not run: `libm-calls` (macOS), `make smoke`, `make` (linked plugin), TSan (none exists for these modules).

### 10.7 Not covered by the automated suite

- Anything in `Ballast.cpp` and `Pan.cpp`: saved JSON (no golden image: TESTING.md says "No (params and `dcBlock`)" and "No (params, `stereoMode` and `panLaw`)"), the menus, the greying, `addExpander`, undo, the tooltips (`BallastLevelQuantity`), the light, `onExpanderChange`, bypass, and `process()`'s mapping of ports.
- Two real threads: neither the 2.0.10 crackle fix nor the two-Ballasts-one-Pan case.
- The glide on **patch load** (no assertion for either snap or glide; section 5.6).
- The chain walk's `MAX_EXPANDERS` of 8 and the 32-step value (only `max` and a loop are tested).
- Cross-family neighbours (Spring) in a live rack.
- The bench's omission of the walk (section 8.1).
- Aliasing of the limiter and of audio-rate CV (#67).
- Constant loudness under fuzz.
- Rack behaviour: the engine lock, patch-load expander events, bypass of outputs with no routes, the lamp's look. All [unverified].

---

## 11. Suggested QA test areas and expected results

Numbers are for Rack at 48 kHz unless stated.

### 11.1 Ballast per control

| Test | Expected |
|---|---|
| Tone into IN 1 L, Level 1 to fully left, to 0.75, to fully right | silence ("-inf dB"); the same level as the input ("+0.0 dB"); +6.02 dB |
| Level at noon, at 0.25 | tooltip −8.5 dB and −23.0 dB; measured −8.49 and −22.99 |
| Turn a Level or Master fast during a tone | no click; 63 % in 12 ms, 99 % in 49 ms |
| CV at 0, 5, 10 V, −5 V, 15 V | silence; −6.02 dB; the knob's level; silence; the knob's level |
| Unpatched CV | the knob alone |
| Poly CV (5 V on channel 1, 10 V on channel 2) | 5 V's gain |
| Envelope into CV | sharp swells (not smoothed) |
| Bipolar ±5 V LFO into CV | silent for the negative half; half level at the peak |
| Master and Master CV | the same laws on the whole mix, CHAIN included |

### 11.2 Inputs and outputs

| Test | Expected |
|---|---|
| IN L alone, IN R alone | each centred on L and R (equal) |
| Both patched, different signals | left stays left, right stays right |
| Poly cable | all channels summed (a 16-channel ±5 V cable can present ±80 V) |
| CHAIN L alone, then L and R | centred on both; stereo. Added before Master |
| Two Ballasts through CHAIN | eight channels; the second's Master sets all |
| Patch L only with stereo content | (L + R)/√2 on L |
| Patch then unpatch R | 10 ms crossfade, no click |
| Hot mix (all channels +6 dB, Master up) | peaks round off, never above 10.000 V, CLIP lights and holds about 150 ms (plus the lamp's 33 ms smoothing) |

### 11.3 DC blocker

| Test | Expected |
|---|---|
| 3 V DC into a channel, blocker off, then tick it on | the 3 V fades; 1/e at 53 ms, 1 % left at 244 ms; no step |
| 30 Hz drone, blocker on and off | −0.045 dB |
| Offset present, blocker off | step of the whole offset, one sample [issue #44] |
| Saved with the patch | yes (`"dcBlock"`) |

### 11.4 Expanders and Pan

| Test | Expected |
|---|---|
| Right-click Ballast, "Add Pan expander" | a Pan against Ballast's right edge; neighbours pushed; LINK lit; one undo step; the item greyed afterwards |
| Undo, redo | Pan goes and returns; pushed neighbours stay pushed [issue #42] |
| Place Pan from the browser touching Ballast | LINK lit |
| Gap between Ballast and Pan; a module in between; Pan on the left | LINK dark; no panning |
| A Neap Spring beside Ballast | ends the chain; a Pan behind it is dark |
| Ballast outputs unpatched | Pan's LINK still lit; nothing audible |
| Every Pan knob at noon, both modes, both laws | sound identical to no Pan (bit-identical offline) |
| Mono source, knob hard left and right, No boost | all on one side at the same level as at centre; power −3 dB at the sides |
| Same, Constant loudness | +3 dB on its side |
| Stereo source (both inputs), True pan hard right | left folds into right: (L + R)/2 in No boost, (L + R)/√2 in Constant loudness |
| Stereo source, Balance hard right | left silent, right unchanged |
| Pan CV, ±5 V, attenuverter +100 % | full sweep from a centred knob; −100 % inverts; 0 ignores the CV |
| Audio-rate CV | works (unsmoothed) |
| Add a second Pan beside the first | does nothing; LINK dark |
| Bypass the first of two | the second acts, lights, glides in; the first's LINK dark |
| Bypass the only Pan with channels panned | a step to centre; LINK dark at once. Un-bypass: glide from centre, 99 % in 46 ms |
| Drag Pan away and back | LINK dark within 50 ms; back: glides in, no click |
| Bypass and release Ballast | LINK dark within 50 ms; Pan resumes without a glide |
| Two Ballasts, each with a Pan | each Pan serves only the Ballast on its left |
| Save a patch with panned knobs, quit, reopen | Pan beside Ballast; LINK lit; **glide from centre on load, 99 % in 46 ms** [issue #42]; knobs, menu choices and the DC blocker restored |
| Patch with Pan saved in test build 2.0.8 | loads as No boost (sounds different at the sides) |
| Engine threads 1, 2, 4, 8 (Rack's Engine menu) with a hard-panned Pan on a steady tone | no crackle at any thread count (the 2.0.10 regression) |
| Drag modules around Ballast and Pan while sound runs | no crash; a short glitch at worst [issue #42] |

### 11.5 Menus

- Ballast: DC blocker checkbox, then "Add Pan expander" (greyed with a working Pan).
- Pan: submenu "Pan law" ("No boost…", "Constant loudness…"), submenu "Stereo sources" ("True pan…", "Balance").
- Initialize Ballast: knobs 0.75, DC blocker on. Initialize Pan: knobs noon, attenuverters 0, True pan, No boost.

### 11.6 Sample rates

44.1, 48, 96, 192 kHz and above: no click on a rate change; smoothing and the DC blocker keep their state; CLIP hold stays 150 ms; LINK goes dark 50 ms after the link stops.

### 11.7 CPU

One Ballast and one Pan, all channels patched: about 0.2 to 0.3 % of a core (bench), plus the walk (up to about 0.1 to 0.15 % [meas, stand-in]). 16 pairs should cost 16 times one. Compare against Rack's own meter [unverified].

---

## 12. Glossary

- **Attached**: the first sample an expander is in Ballast's kept chain after not being in it. Pan restarts its smoothing at centre.
- **Balance**: a stereo source's far side is faded; the near side is untouched.
- **Chain (expander)**: modules touching in a row to Ballast's right, walked on every sample.
- **CHAIN (jacks)**: CHAIN L and R, a signal input added to the mix before Master.
- **Constant loudness**: Pan law 1, equal power; up to +3 dB on the side a source is panned to.
- **Kind**: an integer an expander returns. The first unbypassed expander of each nonzero kind acts.
- **Linked**: Ballast has called `linked()` on the expander this sample. Pan's LINK is lit for 50 ms after the last one.
- **MONO / STEREO**: the source type given to `postLevel`. MONO means exactly one of IN L and IN R is patched.
- **No boost**: Pan law 0 (default); no sample leaves louder than the louder side went in.
- **Noon**: knob 0.5. For Pan, centre.
- **Time constant**: the time a one-pole takes to cover 63 % of a step. 99 % takes 4.6 of them.
- **True pan**: Pan's default for stereo sources; the whole image moves and nothing is lost.
- **VCA law**: gain = knob × clamp(CV/10 V, 0, 1).

### Note on the measurement programs

All in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/ballast/meas/`, built with `c++ -std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd` against the tree in `../tree` (`git archive 2e6e960`):

- `ballast_meas.cpp`: the curve and tooltip values, the knob smoothing, the VCA law, the DC blocker (frequency response, on and off steps and fade times), the limiter, the CLIP hold across rates, the mono crossfade, and the expander walk limits (8, 32, kinds).
- `pan_meas.cpp`: the glide on load and on knob moves (through `BallastLinker` and `BallastEngine` as `Ballast.cpp` drives them), the removal step, un-bypass, Ballast bypass, the law tables, bit-identity at centre, the CV law and the LINK window.
- `walk_cost.cpp`: the chain walk's cost with a stand-in for `rack::Module`.
- `tbl.cpp`: the curve table's worst error.

The test binaries were built from the repo's `tests.mk` rules; the `make bench` and `bench ballast|pan <case> 1 4` figures are from `build/bench/bench` in the same tree.

# Sluice 2.0.39: Technical Reference for QA

Module: **Sluice**, plugin `rka` (slug `rka`, module slug `Sluice`), VCV Rack 2 (minRackVersion 2.5.0).
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree (`git archive 2e6e960`).
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. I unpacked it: its `plugin.json` says 2.0.39, and its `res/sluice/Sluice.svg`, `Sluice-dark.svg` and `plugin.json` are byte-identical to the commit's. Its `plugin.dll` contains Sluice's strings (28 matches). I could not run the DLL; the code below was checked on macOS from the Rack-free engine.
Document date: 2026-11-24.

## Build history

Sluice was built on 2026-11-15, before the first numbered test build, and has not changed since.

| Commit | Date | What |
|---|---|---|
| `05f0b03` | 2026-11-15 | design spec |
| `7c8eb38`, `57be077` | 2026-11-15 | panel, a rope primitive in the generator; outputs moved onto their rows so the traces run level |
| `7917835` | 2026-11-15 | engine: rope, mix downward, polyphony, Exp curve |
| `d501761` | 2026-11-15 | hostile-input and allocation tests under sanitizers; bench budgets |
| `ba4c70d` | 2026-11-15 | the module, registered |
| `8b4bec6` | 2026-11-15 | manual, changelog, roadmap |
| `60a54ad` | 2026-11-15 | engine ignores an unusable sample rate in the Level smoother (keeps the last good rate, 48 kHz before any) |
| `efff9a4` | 2026-11-15 | the whole-branch review's test gaps closed (fuzz against a model, edge sample rates, extreme values) |

- `git diff da9cccc 2e6e960 -- src/sluice res/sluice res-src/sluice docs/sluice` is empty, where `da9cccc` is build 2.0.1 (2026-11-16, the first test build). So **no test build from 2.0.1 to 2.0.39 changed Sluice's code, panel or manual.** No line of CHANGELOG.md's "Test builds" list mentions it. Its only changelog entry is under "2.0.0 (unreleased)".
- Since `efff9a4` only `src/FastMath.hpp` (shared) changed: a new `fastLog2` (for Squall). Sluice uses `fastExp2` and `fastDbToGain`, which are unchanged.
- This is the first QA reference for Sluice. The listening guide `sluice-2.0.39-listening.html` is the only earlier QA document.

---

## 0. How to read this document

- Every number here comes from one of these sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/sluice/SluiceEngine.hpp`, `Sluice.cpp`, `SluiceLayout.hpp`, `src/FastMath.hpp`, `tools/gen_panels.py`), cited as file:line.
  - **[test]**: asserted by the module's automated tests, which I rebuilt from the 2.0.39 tree and ran (section 9).
  - **[meas]**: a measurement program I wrote against the Rack-free engine (`rka::SluiceEngine`, the code the module runs), at 48 kHz unless stated. It is `meas/probe.cpp` (section 12, note on the measurement programs). Built with the plugin's flags: `-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`, on an Apple M-series Mac.
  - **[issue #N]**: an open GitHub issue in `ryanarneson/RkaModules`. There is none about Sluice itself (section 8).
  - **[unverified]**: anything I could not check, mostly Rack host behaviour (the panel, tooltips, menu, bypass, Initialize, light drawing).
- Voltages are Rack volts. A "gain" is output voltage over input voltage. "p" is the gain position, `clamp(Level + CV/10 V, 0, 1)`.
- The knob value of a Level is in [0, 1]; "noon" is 0.5.
- Where the spec, the manual or the user's listening guide disagrees with the code, the code is what ships (section 1.1).

---

## 1. What Sluice is

Sluice is a 10 HP quad VCA in the Instrument look: four rows, A to D from the top, each with an input, a Level dial, a CV input, a light and an output, plus one Lin/Exp switch for all rows. Spec: `docs/specs/2026-11-15-sluice-design.md`. Manual: `docs/sluice/README.md`. Tags: VCA, Mixer, Polyphonic, Utility.

Design intent (spec): the user makes dark, space and horror ambient. Until Sluice the collection had no way for a CV to control loudness. It is also meant to work with CV passing through it, for three cases: modulation depth under CV, gating a modulation with an envelope, and staggered layers from one LFO fed down the rope.

Goals QA can check:

- Four DC-coupled VCAs, exact enough on CV to be used as a modulation depth control: with Lin, gain is exactly `clamp(Level + CV/10, 0, 1)`.
- An LFO on a row's CV makes the row breathe around the Level, bipolar or unipolar. With Level at noon a ±5 V LFO sweeps closed to fully open; at Level 0 a 0–10 V envelope works as a classic VCA.
- Four rows mix to one cable without a mixer (unpatched outputs mix downward). Unpatched inputs copy the row above (the rope).
- Polyphonic throughout, up to 16 voices; a two-voice cable is a stereo signal.
- A new Sluice is silent: every Level defaults to 0 %.
- No zipper or click on turning Level (10 ms smoothing); an envelope on CV is not softened.

Anti-goals (a failure even if it works): a row that moves when nothing is patched to its CV; clicks or zipper on Level; an envelope on CV softened on its way through; hidden state that changes what a row does without a sign on the panel.

Non-goals (do not file these as defects): stereo features (no L/R pairs, no link between rows); AC coupling or a DC blocker (an input offset stays an offset); per-row Lin/Exp; gain above unity, clipping or saturation; CV attenuverters; scaling the mix down; normalling row A to a constant voltage; a CV that follows the rope. The 4× summing of rope copies is intended and documented.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against it; the other versions are given for context.

| Topic | Spec / manual / listening guide says | 2.0.39 does |
|---|---|---|
| What the light shows | **Spec, "Behaviour", Light bullet** (line 101): "glows with that row's gain". Spec "Decided by the designer" (lines 158–163), the manual (lines 43–45), the panel generator's comment and the listening guide: Level plus CV before the curve, highest voice | **p, before the curve** (`SluiceEngine.hpp:53`, `:156`, `:166`). The spec contradicts itself; its later section and the manual agree with the code. At Exp, Level 50 % the light is at 0.500 while the gain is 0.0316 (−30.0 dB) [meas]. Not a bug; the spec's Behaviour bullet is stale |
| Smoothing time | spec "a one-pole of about 10 ms"; manual "(about 10 ms)"; spec test list "settles to within 1 % in a stated time", the manual states none | 10 ms is the **time constant**. p follows a Level step to 63.2 % at 10.0 ms, within 1 % at 46.1 ms and exactly on target at 138.2 ms, at 44.1–192 kHz [meas]. In Exp the audio gain takes 58–65 ms to come within 1 % on a rising step (section 2.3) |
| Hostile values | spec: NaN and ±inf "replaced with 0 where they come in, so they cannot spread to other rows" | true for NaN and ±inf. **A huge finite value is not stopped**: 3e38 V on A with every Level at 1, rope, only D patched, gives **inf** at D and at C [meas]. Four rope copies overflow above FLT_MAX/4 = 8.5e37 V [code arithmetic]. The tests use 1e30 (section 7.2). Not reachable with real patches |
| Channel limit | spec "up to 16" | the engine trusts the channel count Rack gives (at most 16). There is no clamp in the engine; a count above 16 would write past the voice arrays [code: `SluiceEngine.hpp:155–164`]. Rack never gives more |
| Initialize | spec "Saved state: params only" | Initialize resets the engine, so the Level smoother **snaps** to the reset Level on the next sample; there is no fade-out from an open Level (section 3) |
| Tooltip names | spec: "The exact names are left to the builder" | Level "Level A".."Level D" (unit %); inputs "A".."D" and "A level CV"; outputs "A".."D"; lights "A open".."D open"; the switch "Response" (Linear, Exponential) (section 2.2). The light's name "A open" reads like the gain; it is p |
| Exp curve at the bottom | spec "reaches true silence at p = 0" | exactly 0 at p = 0. Just above 0 it is not silent: −79.7 dB at p = 0.005, −73.4 dB at 0.01 [meas] (the ramp is linear in gain, so it is a quiet, not a silent, region) |
| 4× sum | spec and manual: +12 dB | **4.000000 (+12.041 dB)**, in Lin and in Exp, for 1 V into A with all Levels 1 and only D patched [meas] |
| Listening guide, patch 3 | "Set all four Levels to about 25 % first: the four copies add up" | At Exp, 25 % is −45.0 dB per row, so four full-open copies sum to about −33 dB. The 4× warning matters near full open. Unity for four equal copies is 80 % per row (−12 dB each) [meas, section 2.5] |
| Listening guide, patch 7 | "see voices 5 to 8 close, since a short CV gives them 0 V" | voices past the CV's channel count get 0 V of CV, so they sit at **Level**, and close only if Level is 0 (the default) |
| Light in a quiet or unpatched row | not stated | the light follows Level plus CV whether or not a signal or any cable is present (row A with nothing patched still lights at its Level) [meas] |

---

## 2. Panel, controls and ports

### 2.1 Layout

10 HP (50.8 mm wide, 128.5 mm tall). Title SLUICE, subtitle QUAD VCA. The brass rope hangs from an anchor ring at (6.4, 21.0) mm down through the four inputs; five rings struck from the same point at radii 30, 50, 70, 90, 110 mm. Each row is a straight line, joined by a trace: input, Level, CV jack, light, output on an ivory tile lettered A to D. Positions in mm from `SluiceLayout.hpp` [code, generated by `tools/gen_panels.py`]:

| Control | Centre (x, y) mm |
|---|---|
| Lin/Exp switch | 40.0, 12.0 (lettering "EXP" at y 6.9, "LIN" at y 19.6) |
| Level A / B / C / D | 19.0, at y 33.0 / 55.5 / 78.0 / 100.5 |
| Input A–D | 6.4, same four y |
| CV A–D | 31.5, same four y |
| Output A–D | 43.6, same four y |
| Light A–D | 37.4, at y 29.6 / 52.1 / 74.6 / 97.1 |
| Screws | 43.18, 2.54 and 7.62, 125.96 |

The Level dial sits on a small enamel face (6.6 mm radius) with 10 divisions and no numerals; the ticks at 0, 5 and 10 are heavier. One division is 1 V of CV. The pointer starts fully left. The row pitch is 22.5 mm. Widgets: `rka::FaceKnob` (Level), `rka::Toggle2` (switch), `rka::BrassPort` (all ports), `rka::JewelLight` (cyan, with Rack's own halo, so the user's light-bloom setting applies) [code: `Sluice.cpp:101–123`, `widgets.hpp:485–517`]. How the panel looks in Rack, both themes, and whether the lever up means Exp are [unverified]; `make smoke` was not run.

### 2.2 Parameter, input, output and light ids

Pinned by `static_assert` in `Sluice.cpp:69–79`; they must never change number (saved patches depend on them).

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 | Level A | 0..1 | 0 | "Level A", unit %, shown 0..100 (display multiplier 100) |
| 1 | Level B | 0..1 | 0 | "Level B" |
| 2 | Level C | 0..1 | 0 | "Level C" |
| 3 | Level D | 0..1 | 0 | "Level D" |
| 4 | Response (Lin/Exp) | 0..1 switch | **1 (Exponential)** | "Response", labels "Linear", "Exponential" |

| Id | Input | Tooltip name | Description line | | Id | Output | Description line |
|---|---|---|---|---|---|---|---|
| 0..3 | IN A..D | "A".."D" | A: "Silence when unpatched"; B–D: "Normalled from the row above when unpatched" | | 0 | OUT A | "Mixes into the row below when unpatched" |
| 4..7 | CV A..D | "A level CV".."D level CV" | none | | 1, 2 | OUT B, C | same |
| | | | | | 3 | OUT D | "Final mix" |

Lights 0..3: "A open".."D open" (`Sluice.cpp:58`). `configBypass(IN x, OUT x)` for each row (section 3). The Level params are continuous (no snap); the switch is a two-position switch.

### 2.3 Level and its smoother

[code: `SluiceEngine.hpp:129–145`, `:114–120`]

- Level is the knob value in [0, 1], unit %, shown 0–100. **Default 0 %: a new Sluice is silent.** A NaN or infinite Level reads as 0 [test: `testNanDoesNotTravel`].
- Level is smoothed; the CV, the input and the Lin/Exp switch are not.
- The smoother is a one-pole of 10 ms time constant, in the p domain (before the curve and before CV is added): coefficient `1 − e^(−1/(0.010 × fs))`, worked out when the sample rate changes (computed with `fastExp2`, no libm).
- It is held as the target plus the offset from it. The offset decays geometrically and is set to exactly 0 once |offset| < 1e-6, so a still knob gives an exact gain (a plain float one-pole would stall a few ulps short; the code comment at lines 134–137 records that `-funsafe-math-optimizations` would reassociate otherwise). The offset is updated for a moved knob only when the target changes.
- **The first sample after construction or after Initialize snaps to the target**, so a patch does not fade in when it loads [test: `testStartsAtSavedLevel`].
- Measured step response of p (the light follows it) for Level 0 → 1 [meas]:

| Host rate | 63.2 % at | 99 % at | exactly 1 at |
|---|---|---|---|
| 44.1 kHz | 10.02 ms | 46.05 ms | 138.16 ms |
| 48 kHz | 10.02 ms | 46.06 ms | 138.17 ms |
| 96 kHz | 10.01 ms | 46.06 ms | 138.17 ms |
| 192 kHz | 10.00 ms | 46.05 ms | 138.16 ms |

- Audio gain after a Level step, 1 V DC in, 48 kHz [meas]. "Within 1 %" is of the final gain; for a step down to 0 the criterion is |out| ≤ 1e-4 (−80 dB re 1 V), which is much stricter:

| Step | Within 1 % (or 1e-4) at | Largest change in one sample |
|---|---|---|
| Exp 0 → 0.5 (0 → −30 dB) | 58.4 ms | 0.00004 |
| Exp 0 → 1 | 65.3 ms | 0.00077 |
| Exp 0.5 → 1 | 58.4 ms | 0.00077 |
| Exp 0.5 → 0 | 46.4 ms | 0.00023 |
| Exp 1 → 0 | 53.3 ms | **0.0143** |
| Lin 0 → 0.5 | 46.1 ms | 0.00104 |
| Lin 0 → 1 | 46.1 ms | 0.00208 |
| Lin 0.5 → 1 | 39.1 ms | 0.00104 |
| Lin 0.5 → 0 | 85.2 ms | 0.00104 |
| Lin 1 → 0 | 92.1 ms | 0.00208 |

  The largest single-sample change is 1.4 % of full scale (Exp 1 → 0, at the start of the fall). Because the smoother acts on p, not on gain, an Exp fade is a smooth dB-domain fall; there is no audible step. The final values are exact (0 and 1 reached exactly) [meas].
- A Level moved while a row is bypassed: the smoother is not stepped while `process()` is not called, so on un-bypass it glides from the old position over about 46 ms [unverified; follows from the code and Rack not calling `process()` in bypass].
- Rack's per-sample sample-rate change: the coefficient is recomputed whenever the rate passed to `process()` differs from the last one, so the smoothing time stays 10 ms [test: `testSmoothingFollowsSampleRate`].

### 2.4 The CV law

[code: `SluiceEngine.hpp:152–156`]

```
p(voice c) = clamp( Level_smoothed + CV(c) / 10 V , 0 , 1 )      per voice
gain       = curve(p)
```

- 10 V of CV covers the whole Level travel. One dial division is 1 V. With Level at noon, a ±5 V LFO sweeps exactly 0 to 1 in p; with Level 0, a 0–10 V envelope does the same. Where Level plus CV leaves [0, 1] the swing flattens at that end [test: `testCvAddsToLevel`; meas: ±5 V sine at noon gives min 0, max 1 in Lin and in Exp; a 0–10 V sine at Level 0 gives min 0, max 1].
- **An unpatched CV is 0 V, and CV never follows the rope** [test: `testUnpatchedCvIsStill`: a row with no CV does not move while row A's CV moves].
- **CV is not smoothed.** A CV step reaches the output on the same sample [test: `testCvNotSmoothed`; meas: Lin, Level 0, 10 V in, CV 0 → 5 V gives 5.000000 on the first sample].
- **Polyphonic CV**: voice c of the row uses CV channel c. A mono CV (1 channel) drives every voice. A CV with fewer channels than the input (more than 1) gives **0 V** to the voices beyond its count. A CV with more channels than the row's input is ignored beyond the row's voice count: a mono input with a 16-channel CV stays a mono row driven by CV channel 1 only [test: `testPolyCvMonoInputStaysMono`, `testShortCvGivesZeroBeyond`].
- NaN or ±inf on a CV channel reads as 0 V [test].
- No limit on the CV voltage; ±1e30 V clamps p to 0 or 1 [test: `testExtremeValues`].
- Volts of CV needed on top of Level, for common Exp gains (from `p = Level + CV/10`) [meas]:

| Exp gain | p | CV from Level 0 | CV from Level 0.5 |
|---|---|---|---|
| −60 dB | 0.0500 | 0.50 V | −4.50 V |
| −50 dB | 0.1667 | 1.67 V | −3.33 V |
| −40 dB | 0.3333 | 3.33 V | −1.67 V |
| −30 dB | 0.5000 | 5.00 V | 0 V |
| −20 dB | 0.6667 | 6.67 V | +1.67 V |
| −12 dB | 0.8000 | 8.00 V | +3.00 V |
| −6 dB | 0.9000 | 9.00 V | +4.00 V |
| 0 dB | 1.0000 | 10.00 V | +5.00 V |

### 2.5 The Lin/Exp response

[code: `SluiceEngine.hpp:57–65`, `:121–125`]

```
Lin:  gain = p
Exp:  gain = fastDbToGain(60 × (p − 1)) × ramp,   ramp = p < 0.05 ? 20 p : 1
```

- One switch for the whole module. The switch value > 0.5 is Exp. Default Exp. Applied at once: a jump in level when it is flicked is accepted (spec). The cached gains are discarded when the switch changes, so the next sample is already the new curve [test: `testCurveSwitchImmediate`].
- Exp is linear in dB over 60 dB: p = 0.5 is −30 dB, 0 dB at p = 1, times a linear ramp to zero over the bottom 5 %. Gain 0 at p = 0, 1 at p = 1. It is continuous: the largest step across a 1e-6 grid is under 1e-4 [test: `testExpCurveMonotonicAndContinuous`; meas: 4.8e-9 across p = 0.05] and monotonic (0 falls in 1,000,000 steps) [test].
- Accuracy: within 2e-6 relative of the libm formula `10^(3(p−1)) × min(1, p/0.05)` over [0.05, 1], and at all 100,001 points of [0, 0.05] [test: `testExpCurveMatchesDbTaper`].
- The Exp curve is evaluated only when a voice's p changes (`knownP` cache); with a moving CV it is evaluated every sample, which is the bench's busy path.
- Measured gain against p [meas]:

| p | Exp gain | dB | Lin gain |
|---|---|---|---|
| 0 | 0 | −inf | 0 |
| 0.005 | 1.035e-4 | −79.7 | 0.005 |
| 0.01 | 2.143e-4 | −73.4 | 0.01 |
| 0.025 | 5.943e-4 | −64.5 | 0.025 |
| 0.04 | 1.0546e-3 | −59.5 | 0.04 |
| 0.05 | 1.4125e-3 | −57.0 | 0.05 |
| 0.10 | 1.9953e-3 | −54.0 | 0.10 |
| 0.20 | 3.981e-3 | −48.0 | 0.20 |
| 0.25 | 5.623e-3 | −45.0 | 0.25 |
| 0.30 | 7.943e-3 | −42.0 | 0.30 |
| 0.40 | 0.015849 | −36.0 | 0.40 |
| 0.50 | 0.031623 | −30.0 | 0.50 |
| 0.60 | 0.063096 | −24.0 | 0.60 |
| 0.75 | 0.177828 | −15.0 | 0.75 |
| 0.90 | 0.501187 | −6.0 | 0.90 |
| 0.95 | 0.707946 | −3.0 | 0.95 |
| 1 | 1 | 0 | 1 |

  At p = 0.05 the ramp ends and the taper gives −57.0 dB (not −60 dB); below that the gain falls linearly to 0.

### 2.6 The light

[code: `SluiceEngine.hpp:154–166`, `Sluice.cpp:95`]

- Each row's light brightness is **the highest p among the row's voices, before the curve**, so it reads the same in Lin and Exp and a polyphonic row never looks shut while one voice is open. The voice count is the row's (its input's after the rope), not the CV's.
- It is p after the Level smoother, so it follows a Level change with the same 10 ms one-pole, and follows CV on the same sample. It does not depend on the input signal, on a cable being plugged in, or on the output. Row A with nothing patched still lights at its Level [meas].
- `setBrightness(p)` is a direct value, not Rack's smoothed brightness; the light's colour is the cyan SIGNAL token with the halo from Rack's light widget [code: `widgets.hpp:491–517`]. An audio-rate CV therefore shows as the instant value at each drawn frame, a flicker or an average; not checked [unverified].
- Measured [meas]: Exp Level 0.10 / 0.25 / 0.50 / 0.75 / 1 gives light 0.10 / 0.25 / 0.50 / 0.75 / 1 and gains −54 / −45 / −30 / −15 / 0 dB. Level 0.5 with CV −5 V gives light 0; +2 V gives 0.700; +5 V or more gives 1.000. Four voices, Level 0.2, CV 0, 0, 0, +5 V gives light 0.700 (the highest voice).
- Brightness 0.5 is a half-open row in both curves; in Exp that is only −30 dB. Do not read the light as loudness.

### 2.7 Audio inputs and outputs

- Inputs are read per channel with Rack's `getPolyVoltage` rule: a mono cable drives every channel; channels beyond the count read 0. NaN or ±inf read 0 per channel (`SluicePort::poly`, `SluiceEngine.hpp:25–36`).
- No clamp on the audio path; nothing scales the output. Gain never exceeds 1 and the mix is a plain sum, so an output can reach 4 × the input in an all-open rope patch (section 5.2).
- Outputs: each row's output has the channel count of its row's mix (section 5.3); `setChannels` and `writeVoltages` are called for all four outputs each sample, patched or not (`Sluice.cpp:92–96`).
- DC-coupled: nothing filters the signal; an input offset passes unchanged times the gain. At Level 0 in Exp, 0.1 V DC in gives exactly 0 [meas].

---

## 3. The right-click menu and saved state

There is no module menu: no `appendContextMenu`, no `dataToJson` or `dataFromJson` [code: `Sluice.cpp`]. Only Rack's standard module menu (plugin and module name, Initialize, Randomize, Duplicate, Disconnect cables, Bypass, Delete) applies.

- **Saved state: params only.** Five params (ids 0..4, section 2.2). There are no custom JSON keys, so no JSON edge cases and no golden saved-state image (the spec's decision). The engine's smoother and curve cache are not saved.
- **Loading a patch**: the engine's first `process()` snaps the smoother to the loaded Level (`fresh` is true from construction), provided the params are loaded before the first sample. Whether Rack loads params before it first calls `process()` is [unverified]; the offline test covers the engine's side only.
- **Initialize** (`onReset`, `Sluice.cpp:64–66`) calls `engine.reset()`: the next `process()` snaps the smoother to the target and forgets the curve cache. The module overrides the deprecated no-argument `onReset()`; The SDK header says Rack's default `onReset(e)` resets the params to their defaults (Level 0 %, Response Exponential); that it also calls the deprecated `onReset()`, and in what order, is my recollection of Rack's source and is [unverified] (the SDK ships no `.cpp`).
  - Consequence: Initialize with an open row cuts the row to Level 0 in one sample, with no fade.
- **Randomize**: Rack's default; it randomises the four Levels and the switch. The Levels then glide in 46 ms (the smoother is not snapped by Randomize) [unverified in Rack].
- **Bypass** [code: `configBypass`]: IN A → OUT A, IN B → OUT B, IN C → OUT C, IN D → OUT D, each row on its own. **No rope, no mix, no Level, no CV** are applied while bypassed. `process()` is not called while bypassed, so the engine's state holds until it is released [unverified in a live Rack]. What Rack does to an output whose input is unpatched, and to a polyphonic input, is [unverified].

---

## 4. Signal flow

### 4.1 Block diagram

Per sample, in `SluiceEngine::process` (`SluiceEngine.hpp:110–191`). Four rows, r = A..D:

```
 CONTROLS      Level r ──finite──► ONE-POLE 10 ms (p domain) ──► level_r
                                                                    │
 THE ROPE      IN r patched? ── yes ─► src = IN r                   │
 (rows above)  (channels > 0)   no  ─► src = the row above's src    │
                                      (row A: no src, silence)      │
                                                                    ▼
 per voice c   x_c = src.poly(c)                       p_c = clamp(level_r + CV r .poly(c) × 0.1, 0, 1)
 (voices = src channels, or 1 if no src)                     │
                                                             ├──► light r = max_c p_c
                                                             ▼
                                              gain_c = curve(p_c)   (cached while p_c is unchanged)
                                                             │
                                  own_r[c] = x_c × gain_c    ◄─┘     (n_r = voices)

 THE MIX       carry (from the row above, 0 channels at the top)
 DOWNWARD      width = max(n_r, carry channels)
               out_r[c] = (n_r == 1 ? own_r[0] : own_r[c], 0 beyond n_r)
                        + (carry channels == 1 ? carry[0] : carry[c], 0 beyond)
               OUT r patched?  yes ─► carry cleared (the chain stops here)
                               no  ─► carry = out_r (same width)
```

The wide boxes in order: Level smoothing; the rope; CV add and clamp; the curve; the gain multiply; the mix. There is no smoothing on CV or on the curve, no clipping, no filtering and no state besides the Level smoother, the sample-rate coefficient and the curve cache.

### 4.2 Block constants

| Constant | Value | Source |
|---|---|---|
| Rows | 4 (A..D) | `SluiceFrame::ROWS` |
| Max channels | 16 (`PORT_MAX_CHANNELS`, static-asserted) | `Sluice.cpp:78–79` |
| CV scale | 0.1 per volt (10 V across the travel) | `SluiceEngine.hpp:156` |
| Level smoother | one-pole, 10 ms; snap when \|offset\| < 1e-6 | `:119`, `:141` |
| Exp depth, ramp | 60 dB, bottom 5 % (ramp slope 20) | `:61–64` |
| Curve switch threshold | `params[CURVE] > 0.5` | `:121` |
| Unusable sample rate | NaN, ±inf, ≤ 0 → last good rate, 48 kHz before any | `:114–115` |
| Finite filter | `x >= -FLT_MAX && x <= FLT_MAX ? x : 0` (NaN, ±inf → 0) | `:21–23` |
| Light | highest p of the row's voices | `:154–166` |
| Per-sample libm | none (`fastExp2`, `fastDbToGain`, comparisons only) | `FastMath.hpp`; `bench.inc` libm cases |

Memory: the engine holds 4 × 16 floats each for `knownP`, `knownGain` and `own`, 16 for `carry`, and a few scalars; no allocation anywhere in `process()` [test: `testNoAllocationInProcess`, 0 allocations in 48,000 samples with moving CV, Levels, switch and patching].

---

## 5. The rope, the mix downward and polyphony

### 5.1 The rope (inputs normal downward)

[code: `SluiceEngine.hpp:147–150`; test: `testRopeCarriesSignalAndChannels`, `testRopeRestartsAtPatchedInput`, `testRowAUnpatchedIsSilence`]

- An unpatched input takes the **input** of the row above, before that row's gain, with its channel count. A patched input starts the rope again from there.
- The rope follows through several unpatched rows: with only A patched, B, C and D all see A. With A and C patched, B sees A; D sees C.
- **Row A unpatched is silence**: 1 channel at 0 V (even with its CV patched, and at any Level) [test; meas].
- An input counts as patched if its channel count is above 0. A patched input at 0 V is patched.
- The rope never carries CV.

### 5.2 The mix downward (outputs normal downward)

[code: `SluiceEngine.hpp:170–190`; test: `testMixAllPatterns` (all 16 patterns of patched outputs)]

- An unpatched output adds its row's signal (its own VCA output plus anything already carried to it) into the row below's output. A patched output carries its own row and whatever was carried down into it, and **stops the carry**.
- A patched output D with A to C unpatched carries all four rows. All outputs patched: four separate VCAs. A and C patched: A carries A; C carries B and C; D carries D, and D is heard only if D itself is patched.
- Rows below the lowest patched output go nowhere. If only B is patched, C and D's signals are not heard from this module (they sit on unpatched outputs).
- **Copies add up.** One signal into A, only D patched, every Level at 1, comes out **4.000000 (+12.041 dB)**, in Lin and in Exp; A, B, C read 1, 2, 3 V on their (unpatched) outputs [meas; test: `testRopeCopiesAddUp`, `testRopeBelowFullOpen`]. At Exp, Level 0.5 each, the same patch gives 0.12649 V per volt in (−17.96 dB; four copies of −30 dB sum to −18 dB) [meas]. Nothing scales the mix down.

### 5.3 Polyphony

[code: `SluiceEngine.hpp:150–151`, `:170–190`]

- A row has as many voices as its input after the rope (1 to 16). With no input at all the row has 1 voice at 0 V.
- **Mixed width**: an output's channel count is the widest of its own row and the carry. A **mono row spreads into every voice** of a wider one; **polyphonic rows mix voice by voice**: a 2-voice row mixed into a 4-voice row adds into voices 1 and 2 only [test: `testMonoRowSpreadsIntoPoly`, `testMonoRowSpreadsIntoWiderCarry`, `testPolyRowsMixVoiceByVoice`]. Measured: A mono 1 V, B two voices (0, 2 V), all Levels 1 Lin, B patched: B has 2 channels, 1.000 and 3.000 V [meas].
- Channel counts of the four outputs with nothing patched, B patched input 3 channels, C and D: C unpatched (rope: B's 3 channels), D input 2 channels: 1, 3, 3, 3 [meas]. D's voice 3 holds only the carry.
- The cost of the spread (intended): a mono row mixed into a 4-voice chord is in every voice, so if the chord is later summed to mono, the mono row is 4× louder.
- 16 voices: a 16-voice input with a 16-channel CV ramp −5 V to +10 V at Level 0.5 Lin gives gains 0.0 to 1.0 in steps of 0.1 and 1.0 above; the light reads 1.000 [meas; test: `testSixteenVoices`].
- One CV cable on a polyphonic input moves every voice together; the output's light follows the highest.

---

## 6. CPU

### 6.1 Bench cases and budgets

Declared in `src/sluice/bench.inc` [code]; each at 48 kHz:

- **idle**: no cables, Levels 0.
- **outputs**: every output patched, no inputs.
- **busy**: 16 voices on every row from the bench's CV generator, a moving 16-channel CV on every row, Levels 0.5, Exp, all four outputs patched. The curve is computed for every voice every sample.
- **mixDown**: as busy, but only D patched, so A to C mix down into D.

| Case | Budget, Apple arm64 | Budget, other machines (provisional) | TESTING.md (written with the module) | Measured by me, 1 / 4 / 16 instances [meas] |
|---|---|---|---|---|
| idle | 0.2 | 0.6 | 0.077 | 0.077 / 0.090 / 0.090 (second run) |
| outputs | 0.15 | 0.45 | 0.066 | 0.076 / 0.094 / 0.086 |
| busy | 2.15 | 6.45 | 1.07 | 1.26 / 1.48 / 1.45 |
| mixDown | 2.25 | 6.75 | 1.11 | 1.32 / 1.51 / 1.48 |

- Units: % of one core per instance. The first run (idle 0.077, busy 1.258 at 1 instance, 1.299 at 16) was at machine load average about 6.8; the second (the values shown for 4 and 16, and 1.458 busy at 1) at about 10.8, with other builds running. The Mac was therefore loaded, and the busy figures are 18–36 % above TESTING.md's 1.07; they are well inside the budgets. Per 16-voice row the cost is about 65–75 ns per sample (busy x1: 262–304 ns for four rows) [meas].
- The bench fails if a case is over its budget at 1 or 16 instances, or if the 16-instance cost exceeds 1.5 × the single cost plus 0.01 %. Busy at 16 against busy at 1 is 1.03 (first run) to 0.99 (second) [meas].
- The x86-64 budgets are 3 × the Mac's and **have not been measured on native x86-64 hardware**. **Windows CPU figures are not covered by any automated budget.**
- I ran only the four Sluice cases (`bench sluice <case> <n> <seconds>`), not the whole `make bench`, which times 15 modules.
- The libm check (`make libm-calls`) requires zero libm calls per sample in Sluice's idle, outputs and busy cases. It runs on Linux only [issue #36]; **I did not run it** (macOS). By reading the code the engine's per-sample path has no libm call: it uses `fastExp2` (in `sluiceCurve`, and once per sample-rate change for the coefficient) and comparisons.
- `make engine-globals` (no writable global state in any declared engine) ran on the whole bench object and passed (exit 0).

### 6.2 Cost drivers

- The curve is recomputed per voice only when that voice's p changes. With a still knob and still CV the per-sample cost is the multiply and the mix. With a moving CV on every voice it is `fastExp2` per voice per sample (bench busy).
- 64 voices (4 rows × 16) is the worst case. A row with no input costs one voice.

---

## 7. Sample rates and robustness

### 7.1 Sample rates

[code: `SluiceEngine.hpp:111–120`]

- The only rate-dependent state is the smoother's coefficient. A rate NaN, infinite or ≤ 0 is ignored: the last good rate is kept, or 48 kHz before any. Any positive finite rate is accepted, with no upper limit.
- The coefficient is recomputed when the rate changes between calls, so the smoothing is 10 ms at any rate. Measured time to 63.2 % at 44.1, 48, 96, 192 kHz: 10.0 ms in all [meas] (section 2.3).
- Test: 1 Hz, 768 kHz, 0 Hz, −48 kHz, −1 Hz: outputs finite, and the engine is back to exact gain at 48 kHz afterwards [test: `testEdgeSampleRates`]. 48 → 96 and 96 → 48 kHz in the middle of a Level step: within 1 % of target at 50 ms of wall time [test: `testSmoothingFollowsSampleRate`]. Measured with a rate of 0 Hz passed from the first call: treated as 48 kHz; after 50 ms of a Lin Level 0 → 1 step the output is 0.99326 [meas].
- The CV is not band-limited and not oversampled. At audio rate it is a multiplier (ring modulation); aliasing at audio-rate CV or audio-rate input has not been measured [issue #67 lists Sluice as "not looked at; expected to need a note, not a fix"]. A 440 Hz ±5 V sine times a 110 Hz ±5 V CV at Level 0.5 Lin gave peak 4.815 V and mean 0.00000 V (DC-free) [meas].

### 7.2 Hostile input and numerical safety

- **NaN, ±infinity** on an input, a CV or a Level, on row A (the first of the rope): outputs stay finite, and D reads exactly what the clean rows sum to (the bad value reads as 0 where it enters) [test: `testNanDoesNotTravel`, 9 cases, 2,000 samples each].
- **1e30 V** on every input channel and ±1e30 V on Level and CV: outputs finite, light inside [0, 1], every output within 4e30 V (the sum of four inputs), and a unit input stays within [0, 1] [test: `testExtremeValues`].
- **A huge finite input is not stopped.** 3e38 V into A, rope, all Levels 1 Lin, only D patched: C and D read **inf** [meas]. The overflow starts at about 8.5e37 V per row (FLT_MAX / 4) and needs a source that large; no RKA module produces one. The inf is not replaced downstream. It cannot turn into a NaN inside Sluice (the signs of the carry and the row would have to differ and both overflow) [code analysis].
- **Denormals**: the only state is the Level smoother, which snaps to exact values below 1e-6, so none builds up. Input denormals pass through the multiply unchanged [code].
- **No allocation** in `process()` [test].
- **No writable global state** (`make engine-globals`, passed) [test].
- **Seeded fuzz**: 600 trials against an independent model in double precision (the rope, the p clamp, the curve, the carry stop and the mono spread, held as 16 dense voltages), with Levels below 0 and above 1 (−0.3 and 1.4), input and CV channel counts 0 to 16, patched or not, ±10 V inputs, ±8 V CVs, both curves. Outputs agree within 1e-5 + 1e-5·|out|, channel counts exactly, light within 1e-6 [test: `testFuzzAgainstReference`].
- The hostile test is built with ASan and UBSan at `-O1` (`tests.mk:86–88`); the other test with the plugin's float flags at `-O3`.

---

## 8. Open issues

**There is no open or closed GitHub issue about Sluice.** `gh issue list --state all --limit 200` (54 issues, #8 to #77) has no title naming Sluice. Do not file duplicates of the following, which mention it only as one module of several:

| # | Title | Sluice impact |
|---|---|---|
| 64 | Polyphony: modules should be polyphonic where it makes sense | its table lists Sluice as already polyphonic (16 voices per row; a mono row spreads into wider rows); no work for Sluice |
| 67 | Aliasing: measure every module that makes or bends sound, and fix what fails | lists "Hadal, Squall, Sluice, Pan: ... Sluice at audio-rate CV (ring modulation): not looked at; expected to need a note, not a fix". No aliasing test exists for Sluice |
| 36 | Process: libm-calls check is skipped on macOS | I could not run Sluice's libm cases on the Mac (section 6.1) |

### 8.1 Found while preparing this document (not filed)

These are the developer's call whether to file; I have not filed anything.

1. **The spec contradicts itself on the light** (section 1.1): the Behaviour bullet says gain, the "Decided by the designer" section and the code say p. The manual is right. Documentation only.
2. **A huge finite input overflows to inf** (section 7.2). The spec's "hostile values" promise covers NaN and ±inf only; the tests use 1e30. Not reachable in a real patch.
3. **The engine has no channel-count clamp** (section 1.1). Rack never exceeds 16.
4. **Listening-guide patch 3 gives a Level ("about 25 %") that is −45 dB per row at Exp**, and patch 7's "voices 5 to 8 close" is true only at Level 0 (section 1.1).
5. **Initialize is an abrupt cut** from an open Level because the smoother snaps (section 3). By design in the engine; the spec's "starts at the saved value" was written for patch load.

### 8.2 Known limits that are not defects

- At Exp, the bottom of the dial is crowded: p = 0.1 is already −54 dB, and 0.05 is −57 dB, so a very quiet layer takes a Level in the first 5 %. The spec and the listening guide leave the 60 dB depth and 5 % ramp "to be set by ear".
- The Level and the smoother are in p, so a Level step in Exp is not a one-pole on the gain (it takes 58–65 ms to come within 1 % going up, section 2.3).
- A row whose Level is 0 and whose CV is 0 outputs exactly 0, even for a loud input, in both curves [meas].

---

## 9. What the automated suite already covers

I rebuilt both test programs from the 2.0.39 tree with the plugin's flags and ran them on an Apple M-series Mac (macOS, Apple clang):

- `build/test/sluice/test_engine`: built with `-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Wall -Wextra`. Output: `sluice test_engine: all checks passed`.
- `build/test/sluice/test_hostile`: built with `-std=c++17 -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all`. Output: `sluice test_hostile: all checks passed`.
- `make -f tests.mk engine-globals`: exit 0. `bench --check`: "15 module declaration(s) complete".
- Both tests print nothing but their result line; the figures I cite as [test] are the tests' assertions in the source (tolerances), not printed values.

### 9.1 test_engine

Every expected value is worked out in the test from the spec's formulas in double through libm (`refGain`), never with `sluiceCurve`.

| Test | Guarantees | Tolerance |
|---|---|---|
| testCurveEndpoints | curve(0) = 0, curve(1) = 1 in Lin and Exp; Lin(0.5) = 0.5 | exact |
| testExpCurveMatchesDbTaper | Exp against the dB formula over [0.05, 1] (100,001 points) and the ramp over [0, 0.05] (100,001 points) | 2e-6 relative |
| testExpCurveMonotonicAndContinuous | no fall over 1,000,000 steps; largest step | 0 falls; < 1e-4 |
| testLinExactOnDc | 3 V DC at Level 0.5 Lin comes out 1.5 V exactly; 1 channel | exact |
| testCvAddsToLevel | the output matches `in × clamp(level + cv/10, 0, 1)` across Level and CV values | 1e-6 |
| testUnpatchedCvIsStill | row B with no CV does not move while A's CV sweeps; A's lowest gain exactly 0 and highest exactly 1 | exact |
| testRopeCarriesSignalAndChannels | a 3-channel input reaches all four rows with its channels | exact |
| testRopeRestartsAtPatchedInput | a patched input breaks the rope | exact |
| testRowAUnpatchedIsSilence | row A with nothing patched: 1 channel, 0 V, patched or not | exact |
| testMixAllPatterns | all 16 patterns of patched outputs; each patched output carries exactly the rows the rule says | 1e-6 |
| testRopeBelowFullOpen, testRopeCopiesAddUp | rope copies at partial Levels; 4 × 0.75 V | 1e-6; exact |
| testMonoRowSpreadsIntoPoly, ...IntoWiderCarry, testPolyRowsMixVoiceByVoice | spread and voice-by-voice mixing | exact |
| testMonoCvDrivesEveryVoice, testPolyCvMapsVoiceByVoice, testShortCvGivesZeroBeyond, testPolyCvMonoInputStaysMono | CV poly rules | 1e-6 |
| testSixteenVoices | 16 voices, 16-channel CV, both curves, signed input | 1e-6 + 2e-6·|out| |
| testLightIsMaxP | the light is the highest p (0.7 for p = 0.5 and 0.7) | 1e-6 |
| testLevelSmoothing | no step on a Level step; first sample < 0.01 and > 0; within 1 % at 50 ms; exactly 1 at 200 ms | as stated |
| testCvNotSmoothed | a CV step passes on the same sample (0 then 2) | exact |
| testStartsAtSavedLevel | a fresh engine and the first sample after `reset()` start at the Level | 1e-6 |
| testCurveSwitchImmediate | Lin 0.5, Exp on the next sample, Lin on the next | exact; 2e-6 relative |

### 9.2 test_hostile (built with ASan and UBSan)

| Test | Guarantees | Tolerance |
|---|---|---|
| testNanDoesNotTravel | NaN, +inf, −inf in row A's input, CV and Level: outputs finite; D equals the clean sum | 1e-5 |
| testSmoothingFollowsSampleRate | 48 → 96 and 96 → 48 kHz during a Level step; within 1 % at 50 ms ± 1 ms | 1 % |
| testFuzzAgainstReference | 600 seeded random patches against an independent double-precision model, light and channel counts included | 1e-5 + 1e-5·|out| |
| testEdgeSampleRates | 1, 768000, 0, −48000, −1 Hz, both curves: finite; exact gain afterwards | exact; 1e-5 |
| testNoAllocationInProcess | moving inputs, CVs, Levels, switch and patching, 48,000 samples | 0 allocations |
| testExtremeValues | 1e30 on inputs (both signs, both curves); ±1e30 Level and CV: finite, light in [0, 1], within 4e30 | as stated |

The spec's testing section also asks for: exactness on CV (done), a ±5 V sine reaching both 0 and full (done: `testUnpatchedCvIsStill`), a new Sluice silent at its defaults (the bench Instance and `testRowAUnpatchedIsSilence`; there is no test that builds a module with the Rack defaults), `make test`, `make`, `make smoke`, and a look at the screenshot. I ran none of `make`, `make smoke`, or the screenshot.

### 9.3 Not covered by the automated suite

- Anything in Rack itself: the panel, the knob and switch, the tooltips and their units, the lights (both themes, the halo, dim room), save and load, Initialize and Randomize, bypass, the order of events when a patch loads, polyphonic cables as Rack delivers them, and Rack's CPU meter. The listening guide marks the lights, panel, tooltips and switch as "untested".
- That the module's default params really are Level 0 and Exponential in Rack (the bench `Instance` sets them by hand).
- That Rack's `Output::writeVoltages` and `setChannels` give the channel counts the engine's `res.channels` asks for; `Sluice.cpp` has no test.
- Windows and Linux: the suite and bench were run on the build machine. The libm check runs only on Linux.
- CPU on x86-64 or Windows.
- Aliasing at audio-rate CV or audio-rate Level [issue #67].
- Huge finite values above about 8.5e37 V (section 7.2).
- Listening: whether the Exp curve, the 60 dB depth, the 5 % ramp, the closed default, the 10 ms smoothing and the 10 V CV scale feel right. The listening guide asks these as "first guesses, to be set by ear".

---

## 10. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, Level A to D at 0, Response Exponential, nothing patched. Method: use a DC source or a steady sine into the input and a voltmeter or scope on the output. "10 V in" means a 10 V DC source.

### 10.1 Per control

| Control | Procedure | Expected |
|---|---|---|
| Defaults | add a new Sluice; patch 5 V DC into A, A out to a meter | out 0 V; light A off; switch on Exp; all Levels at the leftmost tick |
| Level, Lin | 10 V DC into A, switch Lin, A out patched; Levels 0.1, 0.25, 0.5, 0.75, 1 | 1.000, 2.500, 5.000, 7.500, 10.000 V (exact) |
| Level, Exp | same, switch Exp | 0.01995, 0.05623, 0.31623, 1.7783, 10.000 V; Level 0.5 is −30.0 dB; Level 0 is exactly 0 V |
| Level tooltip | hover a Level | "Level A", reads 0 to 100 % |
| Level smoothing | step Level A 0 → 1 (type 100 in the tooltip); watch a scope on A out with DC in | no step; 63 % at 10 ms, 99 % at 46 ms in p; in Exp the gain is within 1 % at about 65 ms |
| CV, Lin | Level 0, A out, 10 V in; 0–10 V ramp on A CV | out equals CV exactly (0 to 10 V) |
| CV, Lin, bipolar | Level 0.5, ±5 V sine on CV | gain 0 to 1: out between 0 and 10 V, flat at the ends if the CV exceeds ±5 V |
| CV, clamp | Level 0.5, CV +20 V and −20 V | gain 1 and 0 (flat); no overshoot |
| CV not smoothed | square wave on CV at Lin | the output square follows on the same sample; edges as sharp as the source |
| CV unpatched | patch CV into A only; leave B's CV unpatched, B open | B does not move |
| Response switch | flick Lin to Exp at Level 0.5, 10 V in | 5.000 V to 0.3162 V at once (no fade) |
| Switch | check the lettering EXP and LIN, and which lever position is Exp | Exp is the default; the lever position for Exp is [unverified] |

### 10.2 The rope

| Procedure | Expected |
|---|---|
| nothing in A; Levels 1, all outputs patched | all four outputs 0 V, 1 channel |
| 1 V DC into A only, all Levels 1, all outputs patched | all four outputs 1.000 V |
| as above, then patch 2 V into C | A, B 1 V; C, D 2 V |
| CV in A only, Lin | only A moves; B, C, D stay at their Levels |
| 3-voice cable into A, B and C unpatched | B and C outputs are 3 channels |

### 10.3 The mix downward

| Patched outputs | Procedure | Expected |
|---|---|---|
| D only | 1 V in A, Levels 1 | D = 4.000 V (+12.04 dB); in Exp the same |
| D only | 1 V in A, Exp, Levels 0.5 each | 0.1265 V (−17.96 dB) |
| one at a time | 1 V in A, Levels 1 Lin | only A patched: A = 1 V; only B: 2 V; only C: 3 V; only D: 4 V (rows below the patched one are not heard) |
| A and C | different DC in each of A to D, Lin, Levels 1 | A = a; C = b + c; D = d |
| all four | 1 V in A | each 1.000 V (the rope), separate |
| mono A, 2-voice B | A 1 V mono, B 0 and 2 V on two channels, Levels 1 Lin, only B patched | B has 2 channels: 1.0 and 3.0 V |
| 2-voice B into 4-voice C | | C voices 1, 2 sum; voices 3, 4 only C |
| chord to mono | mono row into a 4-voice row, then to a mono sum | mono row 4× louder (intended) |

### 10.4 Polyphony

| Procedure | Expected |
|---|---|
| 8-voice input, mono CV | every voice moves together; 8 output channels |
| 8-voice input, 4-channel CV | voices 1 to 4 follow CV; voices 5 to 8 sit at **Level**, not closed (unless Level is 0) |
| 16-voice input, 16-channel CV ramp | voices follow, flat beyond ±; 16 channels |
| mono input, 8-channel CV | still 1 channel, driven by CV channel 1 |
| light with 4 voices, CVs 0, 0, 0, +5 V at Level 0.2 | light at 0.7 |

### 10.5 The light

| Procedure | Expected |
|---|---|
| Level sweep, Exp and Lin | light brightness equals the Level in both; at Exp noon the light is half but the gain is −30 dB |
| light with nothing patched at row A | still follows Level |
| CV adds | Level 0.5, CV +2 V: light 0.7; ±5 V LFO: light swings 0 to 1 |
| Level step | the light rises with a 10 ms time constant |
| dark theme and light theme | the light shows in both [unverified] |

### 10.6 Saved patches, Initialize and bypass

| Procedure | Expected |
|---|---|
| set Levels and the switch, save, close, load | same knobs; no fade-in on load (first sample at the saved Level) [unverified in Rack] |
| Initialize with Level A at 1 and a signal through | Level back to 0 % and Exp; the output cuts at once (smoother snaps) |
| Randomize | Levels and switch change; Levels glide, no click |
| Bypass | each input goes straight to its output; no Level, CV, rope or mix; with an unpatched input check the output; polyphonic input stays polyphonic [unverified] |
| tooltips on ports | A: "Silence when unpatched"; B to D: "Normalled from the row above when unpatched"; outputs A to C: "Mixes into the row below when unpatched"; D: "Final mix"; lights "A open".. |

### 10.7 Audio-rate CV and DC

| Procedure | Expected |
|---|---|
| 440 Hz ±5 V sine in, 110 Hz ±5 V on CV, Level 0.5 Lin | ring modulation; peak about 4.8 V; mean 0 V; aliasing not measured [issue #67] |
| 0.1 V DC in, Level 0 or Exp CV at 0 | exactly 0 V out; no DC leak |
| 0.1 V DC in, Level 1 | 0.1 V (DC-coupled); an offset stays an offset |
| envelope on CV into a CV-controlled modulation | the shape is unchanged (no smoothing), only scaled |

### 10.8 Sample rates (44.1 to 768 kHz) and hostile values

| Procedure | Expected |
|---|---|
| change the rate in Rack's audio settings with a Level step in progress | still 10 ms time constant after the change |
| NaN or inf from a source into a row's input, CV, or Level | the row reads 0; other rows are unaffected; outputs finite |
| huge finite values (above 8.5e37 V) | overflow to inf is possible with the rope and the mix (section 7.2) |

### 10.9 CPU

| Procedure | Pass criteria |
|---|---|
| one idle instance | Mac bench about 0.08 % of a core (budget 0.2) |
| one busy instance (16 voices on each row, moving CV, Exp) | Mac bench about 1.1 to 1.5 % of a core under load (budget 2.15); 16 instances within 1.5 × the single cost |
| mixDown (only D patched) | about 1.3 to 1.5 % (budget 2.25) |
| Windows | no automated budget; record the Rack meter readings per instance for idle, 1 voice and 16 voices at 48 kHz |

---

## 11. Glossary

| Term | Meaning |
|---|---|
| Bypass | Rack's module bypass: each input is copied to its output (`configBypass`); the rope, mix and Level do not apply |
| Carry | the signal passed from an unpatched output into the row below's output |
| Copies add up | the rope plus the mix downward sum several copies of one signal; four fully open rows give 4× (+12.04 dB) |
| Curve | the gain law selected by the switch: Lin (gain = p) or Exp (60 dB taper with a ramp to zero) |
| DC-coupled | nothing filters the signal; a CV comes through exactly; an input offset stays |
| Exp | the decibel taper: gain = 10^(3 (p − 1)) × min(1, p / 0.05) |
| Level | the dial of a row, 0 to 1 (0–100 %); smoothed with a 10 ms one-pole |
| One-pole | a first-order smoother; after one time constant it has covered 63 % of a step, after about 4.6 time constants 99 % |
| p | the gain position, `clamp(Level + CV/10 V, 0, 1)` per voice |
| Rope | the normalling of an unpatched input to the input of the row above |
| Spread | a mono row copied into every voice of a wider row when mixed |
| Voice | one channel of a polyphonic cable |

---

### Note on the measurement programs

- My probe is one C++ file, `meas/probe.cpp`, kept outside the repository at `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/sluice/meas/probe.cpp`. It was compiled against the extracted 2.0.39 tree (`sluice/tree`, from `git archive 2e6e960`) with `-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Itree/src`.
- It drives `rka::SluiceEngine` as `Sluice.cpp` does: a `SluiceFrame` of params, port voltages and channel counts per sample. It prints the curve table, the light against Level and CV, the smoother's step response at four sample rates, the gain after Level steps, the CV step, the 4× sum, the spread, the overflow case, the channel counts and the audio-rate CV.
- It does not exercise Rack's ports, threading or GUI. Anything marked [meas] should match Rack at 48 kHz, but Rack-side effects (the light's drawing, the order of events on a patch load, Initialize, bypass, tooltips) are outside it.
- CPU figures are `build/bench/bench sluice <case> <n> <seconds>` from the bench built with `make -f tests.mk build/bench/bench`, with the plugin's `BENCH_FLAGS` (`-O3 -funsafe-math-optimizations -fno-omit-frame-pointer -g -march=armv8-a+fp+simd`), 5 s (first run) and 8 s (second run) per case, on a machine that was running other work (load average 6.8 to 10.8).
- I did not run `make libm-calls` (Linux only), `make`, `make smoke`, or the full `make test` and `make bench` for the other modules. I did not look at `build/screenshots/Sluice.png`.

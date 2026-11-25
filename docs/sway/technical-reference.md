# Sway 2.0.39: Technical Reference for QA

Module: **Sway**, plugin `rka` (slug `rka`, module slug `Sway`), VCV Rack 2 (minRackVersion 2.5.0). A 12 HP polyphonic LFO.
Build: **2.0.39 = commit `2e6e960`** (full hash `2e6e9609f8c509432d690ab4715c5b00be450a38`, 2026-11-24 20:03, plugin.json `"version": "2.0.39"`), branch `neap`, not yet merged to main. Everything here was taken from that tree (`git archive 2e6e960`).
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It is a Zstandard-compressed tar. Its `plugin.json` has version 2.0.39, and its `res/sway/Sway.svg` and `Sway-dark.svg` are byte-identical to the commit's. Sway has no factory presets (`presets/` holds Fog's only).
Document date: 2026-11-24.

## Build history

Sway is listed in `CHANGELOG.md` under **2.0.0 (unreleased)** only. No test build from 2.0.1 to 2.0.39 lists a Sway change.

- **No file of Sway's has changed since 2026-11-15** (`git log` for `src/sway`, `docs/sway`, `res/sway`, `res-src/sway`: last commit `b888141`, "Bench, libm and globals checks discover modules from src/*/bench.inc"). The last change to Sway's own sound or behaviour is `1e06618` (2026-11-15, "per-sample rate work only on change, and unpatched outputs skipped"). The test builds are version labels on later work for other modules.
- Shared files that changed after that and that Sway uses:
  - `src/widgets.hpp`: the 1-SHOT button (`CapLatch`) now keeps its light reference in a shared `CapLightRef` (refactor for Spindrift's buttons); `Toggle2` (BI/UNI) is unchanged; `Toggle3` and the Eddy and Spindrift windows were added. [code, `git diff b888141 2e6e960`]
  - `src/FastMath.hpp`: `fastLog2` added (Squall's). The functions Sway calls (`fastSin2pi`, `fastExp2D`) are unchanged.
  - `res/components/`: `Toggle3_*` files added.
- **Neap includes three of Sway's headers** (`sway/Phasor.hpp`, `sway/RandomGen.hpp`, `sway/Triggers.hpp`, in `src/neap/Cycles.hpp` and `NeapEngine.hpp`). A change to those files moves Neap too.
- History of the module before that, from `git log` (all 2026-10-23 unless stated): `4ee67fe` phase kept in double (fixes the stall below 2⁻¹⁰ Hz); `a3cfb9d` RandomGen; `5507d6a` ClockSync; `861d805` Shaper; `263f4c4` the 18 HP panel, clock sync, spread, shaping, random, ranges, hold and one-shot; `5cabd23` a parked one-shot rests at cycle end, and inactive channels stay in step; `b891887` STEP and RAND merged into one RAND, EOC added; 2026-11-15: `e2764fd` the Instrument look at 12 HP with a rate light, `a46828c` the Rack-free `SwayEngine` and `make bench` (issue #8), `1e06618` the CPU work.

---

## 0. How to read this document

- Every number comes from one of these, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/sway/*.hpp`, `src/sway/Sway.cpp`, `src/FastMath.hpp`, `src/widgets.hpp`, `tools/gen_panels.py`), cited as file:line or by function.
  - **[test]**: printed or asserted by Sway's automated tests, which I rebuilt from the 2.0.39 tree and ran (section 12).
  - **[meas]**: a measurement program I wrote against the engine (`rka::SwayEngine`, the code `Sway.cpp` calls each sample) and ran. It is `meas.cpp` (section 14); its raw output is `meas.out` next to it. Unless stated: 48 kHz, one instance, knobs at their defaults, nothing patched, every output marked patched.
- **[issue #N]** is an open GitHub issue in the repository (section 11). **[unverified]** marks anything I could not check, mostly Rack host behaviour. The Windows package was not run.
- Voltages are Rack volts. "Noon" is knob 0.5 (for RATE, 0; for SMOOTH, 0). A "cycle" is one pass of one channel's own phase from 0 to 1.
- The user's listening guide `/Volumes/share1/VCVRACK/sway-2.0.39-listening.html` was used as a cross-check, not as a source. Where it disagrees with the code, section 11.2 says so.

---

## 1. What Sway is

Sway is a polyphonic low-frequency oscillator, 12 HP, drawn as a pendulum on a pivot at the top right (spec `docs/specs/2026-10-23-sway-expansion-design.md`; manual `docs/sway/README.md`). Per channel (1 to 16) it gives seven outputs:

- **SIN, TRI, SAW, SQR**: the four classic waves; SQR has a pulse width.
- **MORPH**: one shaped wave, from sine through triangle and saw to square, bent by SKEW and rounded or folded by SMOOTH.
- **RAND**: a new random value each cycle, stepped, or gliding in as SMOOTH turns left.
- **EOC**: a 10 V, 1 ms trigger at the end of each cycle.

Other features: three ranges (Slow, Normal, Audio), exponential FM, CLOCK sync with ratios ÷16 to ×16, RESET, HOLD, PHASE and SPREAD (phase offset per channel), BI/UNI, a 1-SHOT mode that turns it into an envelope, a rate light, and CV with an attenuverter on FM, PW, PHASE, SKEW, SMOOTH and MORPH.

Design goals QA can check:

- The phase is a `double`, so slow rates are exact: the old float phase ran fast at 2⁻⁷ Hz and froze below 2⁻¹⁰ Hz (spec, "Bug: phase stalls at slow rates").
- SIN and TRI start at 0 V rising and stay in phase with MORPH at its defaults.
- Every channel keeps running whether or not it is read, so a channel added later is already in step with the others.
- The per-sample cost is small and does not depend on CHANNELS (TESTING.md rule 10; section 9).
- No libm call per sample with the knobs still (`make libm-calls`; Linux only, not run here).

Non-goals (spec "Out of scope"): an on-panel display (users patch VCV's Scope), CV on SPREAD or on the clock ratio (FM does move the ratio), and an expander.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. The spec is dated 2026-10-23 and its panel section is superseded by the 12 HP Instrument panel; its "Amendment" section (STEP and RAND merged, EOC added) is current. No issue tracks these differences; the rows marked "not filed" are candidates (section 11.2).

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| Panel width | spec: 18 HP, layout B, a STEP output; manual and CHANGELOG: 12 HP | 12 HP (60.96 × 128.5 mm), seven outputs, no STEP. The spec's panel table and the mockup are history |
| **PHASE shifts every output** | manual: "**PHASE**: shifts every output by 0–360°". Spec: the read phase is `phasor + PHASE + SPREAD·c/N`, and RAND "advanced on the Phasor's wrap (not the read phase), so SPREAD and PHASE don't change when values are drawn" | PHASE and SPREAD shift SIN, TRI, SAW, SQR and MORPH only. **RAND and EOC read the channel's own cycle and do not move**, with PHASE on the knob, on its CV, or with SPREAD [meas, section 5.2]. Every channel's EOC fires at the same instant at any SPREAD. The manual's sentence is wrong for RAND and EOC; the spec's sentence supports the code. The manual's rate-light line already says PHASE does not move the light |
| **RAND seeds** | spec: "Each channel has its own generator (seeded per channel)"; manual: "Each channel has its own random values" | the seed is the channel number plus 1 (`Voice.hpp:62`), fixed. Every Sway plays the same sequence on a given channel, every patch load restarts it, and it is never saved [meas, section 6.2]. Channels differ from each other |
| Audio range top | spec "~8 Hz … 8.4 kHz"; manual "about 8 Hz to 8 kHz"; menu text "Audio (8 Hz–8 kHz)" | 8.176 Hz to 8,372 Hz (C4 × 2^±5) [meas] |
| Band-limiting | manual: "SAW, SQR and MORPH's square are band-limited in this range"; spec: SAW and SQR, and MORPH's square part | polyBLEP is applied only in the Audio range (`dt` is 0 in Slow and Normal). MORPH's **saw** part is also band-limited. In Normal range with FM, frequency reaches 16.4 kHz and SAW and SQR are then naive and alias [code `SwayEngine.hpp`, `dt`; issue #67 for the unmeasured aliasing] |
| CV law | spec: `knob + atten · (cv / 10 V) · (max − min)`; "±5 V at full attenuverter sweeps the whole range". Manual: "±5 V at full sweeps the whole knob range" | the formula is exactly the spec's. ±5 V covers the whole range **only from the knob's centre** (PW at noon, SKEW at noon, SMOOTH at 0). MORPH's default is 0, so +5 V reaches only 0.5; use 0–10 V. PHASE ±5 V is ±½ cycle, which wraps, so it does cover a cycle [meas, section 2.3] |
| EOC on a clock restart | manual: "including … a clock restart"; spec: "a clock restart **past mid-cycle**" | only when the phase is above 0.5 at the restart (`Voice.hpp:44`). An early clock edge restarts the cycle with no EOC |
| 1-SHOT | manual: "RESET runs a single cycle, then the outputs rest at the cycle's end until the next RESET". Spec adds: "Before the first RESET the LFO is parked" | as written, and also: switching 1-SHOT on at any time parks the channels at once (the outputs jump to the cycle's end); a patch saved with it on loads parked; switching it off while parked fires one EOC and starts free-running [meas, section 8]. The manual does not say what the rest levels are (section 8) |
| Clock before lock | manual: "once two clock edges have arrived" | two **counted** rising edges. A clock already high at the module's first sample is not an edge (`Triggers.hpp`: an uninitialised trigger settles without firing) [code; measured as lock at the third pulse when the first was high from sample 0] |
| Clock stops | not stated | the LFO keeps the last frequency indefinitely; there is no timeout. Unplugging the cable returns to RATE [meas, section 7] |
| ×1 at "centre" | manual: "(centre is ×1; the tooltip shows the ratio)" | true: RATE 0 = ×1. The default RATE is +1, which is **×2** when a clock is patched (`ratioIndex`) |
| SMOOTH folds | manual and spec: "up to three folds" | the fold is `fold(y · (1 + 3·SMOOTH))`, a gain of 4 at +1. On a sine that is 10 direction reversals per cycle (2 plain, 8 from folds at ±1 and ±3) [meas, section 3.5]. "Three" is the 3 in the gain |
| RATE tooltip | manual: "The tooltip shows Hz, or the cycle time in Slow" | true, but from 1,000 Hz up the text is in exponent form: "1.48e+03 Hz" and "8.37e+03 Hz" (`"%.3g Hz"`) [meas, transcription of `Sway.cpp` `RateQuantity`] (section 2.4). Not filed |
| Rate light | spec: silent (no light; "dropped" display); manual and `RateLight.hpp` describe it | as the manual says. It lights from 0 to 10 Hz as a blink and above 10 Hz at 0.5; exactly 10.00 Hz still blinks [meas] |
| Typed values | spec silent | RATE has no typed-value parser: typing a number in the tooltip sets the knob value (−7..7), not Hz [code: no `setDisplayValueString` override; [unverified] in Rack] |
| Polyphonic CV | spec: "Clock is polyphonic via getPolyVoltage(c): a mono clock drives every channel"; manual: "a mono cable drives every channel" | true for every input: each channel reads its own CV channel, and a mono cable feeds all [meas, section 9]. Unlike Fog, CV is not channel-1-only |
| Version | spec: "Sway is unreleased … The version stays 2.0.0" | CHANGELOG still has Sway under "2.0.0 (unreleased)"; the plugin is 2.0.39 |

---

## 2. Panel, controls and ports

### 2.1 Layout

12 HP, 60.96 × 128.5 mm, light and dark panels (`res/sway/Sway.svg`, `Sway-dark.svg`). Positions in mm from `SwayLayout.hpp` [code]:

| Control | Centre (x, y) mm | Control | Centre (x, y) mm |
|---|---|---|---|
| RATE (hero knob) | 21.0, 34.0 | CLOCK / RESET / HOLD jacks | 6.98 / 16.38 / 25.78, 102.0 |
| Rate light | 21.0, 45.2 | SIN / TRI / SAW jacks | 35.18 / 44.58 / 53.98, 102.0 |
| SPREAD | 46.5, 23.0 | BI/UNI switch | 6.98, 116.0 |
| CHANNELS | 52.0, 41.0 | 1-SHOT button | 16.38, 116.0 |
| FM jack, FM attenuverter | 6.98, 88.0 and 65.04 | SQR / MORPH / RAND / EOC jacks | 25.78 / 35.18 / 44.58 / 53.98, 116.0 |
| PW knob, att, jack | 16.38: 59.0, 70.14, 88.0 | PHASE knob, att, jack | 25.78: 63.06, 73.51, 88.0 |
| SKEW knob, att, jack | 35.18: 65.33, 75.43, 88.0 | SMOOTH knob, att, jack | 44.58: 66.0, 76.0, 88.0 |
| MORPH knob, att, jack | 53.98: 65.13, 75.26, 88.0 | Screws | 53.34, 2.54; 7.62, 125.96; 53.34, 125.96 |

- FM has no knob of its own: its jack and attenuverter sit in the first column and their trace runs up to the RATE dial.
- The five shape knobs sit on an arc; each attenuverter is 5.6 mm; outputs sit on ivory tiles; inputs are bare.
- The RATE dial is numbered 0 to 10 with 50 ticks over 270° (`gen_panels.py` `faces`). The numeral is `(RATE + 7) · 10 / 14`: noon (RATE 0) is 5, the default RATE +1 is 5.71, the ends are 0 and 10 [code arithmetic].
- SPREAD has 20 ticks; CHANNELS one tick per channel (15 intervals, long ticks at 1, 6, 11, 16).

### 2.2 Parameter, input, output and light ids

Pinned by `static_assert` in `Sway.cpp` (`process()`) against the engine's frame; they must never change (saved patches depend on them).

| Id | Param | Range | Default | Snap | Tooltip label and unit |
|---|---|---|---|---|---|
| 0 | RATE | −7..7 | 1 | no | "Rate", custom text (section 2.4) |
| 1 | PW | 0.01..0.99 | 0.5 | no | "Pulse width", % (×100): 1 %..99 % |
| 2 | PHASE | 0..1 | 0 | no | "Phase", ° (×360): 0°..360° |
| 3 | SKEW | 0..1 | 0.5 | no | "Skew (MORPH out)", % |
| 4 | SMOOTH | −1..1 | 0 | no | "Smooth: round MORPH and glide RAND ← → fold MORPH", % (−100..100) |
| 5 | MORPH | 0..1 | 0 | no | "Morph: sin → tri → saw → square", % |
| 6 | SPREAD | 0..1 | 0 | no | "Phase spread across channels", % |
| 7 | CHANNELS | 1..16 | 1 | yes | "Channels" |
| 8 | FM attenuverter | −1..1 | 0 | no | "FM amount (1 V/oct at 100%)", % |
| 9..13 | PW, PHASE, SKEW, SMOOTH, MORPH attenuverters | −1..1 | 0 | no | "PW CV amount", "Phase CV amount", "Skew CV amount", "Smooth CV amount", "Morph CV amount", % |
| 14 | BI/UNI | 0 or 1 | 0 | switch | "Offset": "Bipolar (±5 V)" / "Unipolar (0–10 V)" |
| 15 | 1-SHOT | 0 or 1 | 0 | switch | "One-shot (RESET runs one cycle)": "Off" / "On" |

| Id | Input | | Id | Output | Id | Light |
|---|---|---|---|---|---|---|
| 0 | "FM (exponential)" | | 0 | "Sine" | 0 | "One-shot" (the 1-SHOT cap) |
| 1 | "Pulse width CV" | | 1 | "Triangle" | 1 | "Rate (channel 1)" (single colour, under RATE) |
| 2 | "Phase CV" | | 2 | "Saw" | | |
| 3 | "Skew CV" | | 3 | "Square" | | |
| 4 | "Smooth CV" | | 4 | "Morph (shaped)" | | |
| 5 | "Morph CV" | | 5 | "Random (stepped; SMOOTH left glides)" | | |
| 6 | "Clock" | | 6 | "End of cycle trigger" | | |
| 7 | "Reset" | | | | | |
| 8 | "Hold (freezes while high)" | | | | | |

No control has a smoothing filter: knobs and CV are read every sample, every channel [code]. The only filter is the rate light's fade (section 10).

### 2.3 The CV law

[code `SwayEngine::modulated`, `span`; spec "Controls"]

```
cv        = jack voltage of this channel / 10 × attenuverter
effective = knob + cv × span          span: PW 0.98, SMOOTH 2, PHASE/SKEW/MORPH 1
```

then PW clamps to 0.01..0.99, SKEW and MORPH to 0..1, SMOOTH to −1..1, and PHASE wraps. FM is separate (section 2.4).

- **Poly:** each channel reads its own CV channel; a mono cable feeds every channel (`SwayPort::poly`) [meas: a 4-channel PHASE CV of 0, 2.5, 5, 7.5 V at attenuverter +1 gave SAW −5.0, −2.5, 0.0, +2.5 V on channels 1 to 4].
- **Reach of ±5 V at attenuverter +1** [meas, values before the clamp]:

| Control | From the default knob, +5 V | From the default knob, −5 V | From the knob at its minimum, +5 V |
|---|---|---|---|
| PW (default 0.5) | 0.990 (clamped at 0.99) | 0.010 (clamped at 0.01) | 0.50 |
| PHASE (default 0) | +0.5 cycle | −0.5 cycle (wraps) | +0.5 |
| SKEW (default 0.5) | 1.0 | 0.0 | 0.5 |
| SMOOTH (default 0) | +1.0 | −1.0 | 0.0 |
| MORPH (default 0) | 0.5 | −0.5 (clamped to 0) | 0.5 |

  So the manual's "±5 V at full sweeps the whole knob range" holds from the centre of PW, SKEW and SMOOTH. For MORPH at its default a 0 to +10 V CV is needed to reach 1.
- NaN or infinity on a CV: the result is clamped like any value. NaN goes to the clamp's upper bound for PW, SKEW, SMOOTH and MORPH (`clampF` sends NaN to `hi`), and to a harmless fixed phase for PHASE (`readPhase`). A fuzz with NaN, ±inf and ±1e30 on every input and parameter produced no non-finite output [meas, section 11.1].
- Attenuverter 0 means the CV has no effect; a CV jack that is not patched reads 0 V.

### 2.4 RATE, ranges and FM

[code `Rate.hpp`, `SwayEngine::updateRates`; manual "Rate and timing"]

```
Slow:    exponent = RATE − 7 + FM          Hz = 2^exponent
Normal:  exponent = RATE + FM              (default)
Audio:   exponent = log2(261.6256) + RATE × 5/7 + FM
exponent clamped to [−24, 14]; Hz = min(2^exponent, 0.45 × sample rate)
FM       = FM jack volts × FM attenuverter (1 V/oct at +1; inverted at −1; none at 0)
```

Frequencies [meas, 48 kHz, voice 1]:

| Range | RATE −7 | RATE −3.5 | RATE 0 (noon) | RATE +1 (default) | RATE +3.5 | RATE +7 |
|---|---|---|---|---|---|---|
| Slow | 6.104e-5 Hz (16,384 s = 4 h 33 m) | 6.905e-4 Hz (1,448 s) | 0.0078125 Hz (128 s = 2 m 08 s) | 0.015625 Hz (64 s) | 0.0884 Hz (11.31 s) | 1 Hz |
| Normal | 0.0078125 Hz (128 s) | 0.0884 Hz | 1 Hz | 2 Hz | 11.31 Hz | 128 Hz |
| Audio | 8.1758 Hz | 46.249 Hz | 261.626 Hz (C4) | 429.241 Hz | 1,479.98 Hz | 8,372.02 Hz |

- FM, Normal range, RATE +1 (2 Hz): +1 V at attenuverter +1 gives 4.000 Hz; +2 V 8.000 Hz; −1 V 1.000 Hz; +1 V at −1 gives 1.000 Hz; +1 V at +0.5 gives 2.828 Hz; +5 V 64.0 Hz; +10 V 2,048 Hz; any voltage at attenuverter 0 leaves 2 Hz [meas].
- Clamps [meas]: Normal RATE +7 with FM +10 V (exponent 17) gives 16,384 Hz at 44.1, 48, 96 and 192 kHz, because 2¹⁴ is below 0.45·fs at those rates (19,845 Hz at 44.1 kHz). Slow RATE −7 with FM −10 V (exponent −31, clamped to −24) gives 5.96e-8 Hz, a cycle of 1.68e7 s = 194.2 days. [test: `testClamps`] pins the floor, the 2¹⁴ ceiling and the 0.45·fs cap at 22.05 kHz.
- Accuracy [meas]: Audio RATE 0 for 100 s at 48 kHz counted 26,162.5555 cycles against 26,162.56 expected (−0.17 ppm). Normal RATE +1 for 600 s gave 1,200 wraps. Slow periods at 2⁻⁷ and 2⁻¹⁴ Hz are within 0.1 % [test: `testSlowPeriods`], and the phase still advances at 2⁻²⁴ Hz [test: `testNoStallAtMinRate`].
- **Tooltip text** (transcribed from `RateQuantity`, free-running, no FM in the text) [meas]:

| Range | RATE −7 | −3.5 | 0 | +1 | +3.5 | +7 |
|---|---|---|---|---|---|---|
| Slow | 4h 33m per cycle | 24m 08s per cycle | 2m 08s per cycle | 1m 04s per cycle | 11.3 s per cycle | 1 Hz |
| Normal | 0.00781 Hz | 0.0884 Hz | 1 Hz | 2 Hz | 11.3 Hz | 128 Hz |
| Audio | 8.18 Hz | 46.2 Hz | 262 Hz | 429 Hz | **1.48e+03 Hz** | **8.37e+03 Hz** |

  With a clock locked the tooltip shows the ratio (`×N` or `÷N`) of RATE alone (FM not included in the text). The tooltip unit is empty. In Rack the real strings are [unverified].
- The 16 voices' frequencies are worked out again only when RATE, the range, the sample rate or an FM channel changes (`updateRates`), so a still RATE costs no libm call.
- Phase advance per sample is `freq / sample rate` in double precision (`Phasor::step`); a step larger than one cycle still lands in [0, 1) [test: `testHugeStep`].

### 2.5 PW, PHASE, SKEW, SMOOTH, MORPH

[code `Waves.hpp`, `Shaper.hpp`]

- **PW**: pulse width of SQR and of MORPH's square, 1 % to 99 %. SQR is high for `p < pw`, so it starts high (+5 V) at phase 0 [meas]. Duty within 0.001 of PW [test: `testPulseWidth`].
- **PHASE**: added to the read phase, wrapped. 0.25 reads the wave a quarter cycle ahead: SIN at 5.000 V on the first sample [meas]. It moves SIN, TRI, SAW, SQR and MORPH only (section 5.2).
- **SKEW**: warps the phase so the rise takes SKEW of the cycle, measured from the trough so MORPH stays in phase with SIN and TRI. 0.5 is the identity (error under 1e-6 [test: `testSkewIdentity`]). SKEW is clamped to 0.001..0.999 inside the warp. 0 and 1 give falling and rising ramps from the sine and triangle [test: `testSkewRamps`]. It affects MORPH only.
- **MORPH** crossfades sine, triangle, saw and square across equal thirds: 0, ⅓, ⅔, 1 are the pure shapes within 1e-5 [test: `testPureShapes`]. At its default MORPH 0 the MORPH output is the sine, equal to SIN [meas: 0.001, 3.536, 5.000 V at phases 0, ⅛, ¼ for both].
- **SMOOTH** (−1..1), applied after the crossfade:
  - negative: blends toward `sin(π/2·y)`, a sine rounding of the corners, by `−SMOOTH`;
  - positive: `fold(y × (1 + 3·SMOOTH))`, a triangle fold of the scaled wave back into ±1;
  - 0 is exact identity [test: `testSmoothZeroIdentity`].
  - For RAND, negative SMOOTH is also the glide (section 6.1); positive SMOOTH leaves RAND stepped.
- Folds [meas, MORPH 0 (sine), reversals of direction per cycle]: SMOOTH 0: 2; +0.25: 6; +0.5: 6; +1: 10. The output stays within ±1 (±5 V) at every setting.
- MORPH range [meas]: over a grid of MORPH 0..1 (step 0.05), SKEW 0..1 (0.1), SMOOTH −1..1 (0.25), PW 0.01, 0.5, 0.99 and 2,048 phases, the largest |output| is exactly 1.000000 (5.0000 V). In the Audio range with up to 0.45 cycle per sample, polyBLEP SAW peaks at 0.9976, SQR at 1.0000 and MORPH's square at 1.0000 (×5 V): no overshoot [meas].
- With BI/UNI on UNI, every wave is `5 × (x + 1)`, 0 to 10 V (section 2.7).

### 2.6 SPREAD and CHANNELS

[code `SwayEngine::process`; manual "Polyphony"]

- Output channels `N = max(CHANNELS knob, the channel count of every input jack)`, 1..16. The jacks that count are all nine, including CLOCK, RESET and HOLD [meas: a 3-channel CLOCK gave 3 channels; a 16-channel RESET gave 16; CHANNELS 12 with a 4-channel FM gave 12]. Every output reports N channels; an unpatched output is not worked out and reads 0 V (`outputConnected`).
- Channel `c` (0-based) reads the phase `p = frac(phasor + PHASE + SPREAD × c / N)` [meas, frozen with HOLD just after RESET]: CHANNELS 2 at 100 %: 0, 0.5; CHANNELS 4 at 100 %: 0, 0.25, 0.5, 0.75; at 50 %: 0, 0.125, 0.25, 0.375; CHANNELS 16 at 100 %: 0, 0.0625, 0.125, … 0.9375 (first eight checked).
- SPREAD offsets the **waves only**; it does not offset RAND, EOC or the rate light (section 5.2).
- All 16 channels run every sample whatever N is, so raising CHANNELS or patching a poly cable finds the new channels in step with channel 1 [test: `testLateChannelsFreeRunning`, `testLateChannelsClocked`].
- Changing N changes the spacing at once (`c / N`), so the outputs jump on a change of CHANNELS while SPREAD is above 0. [code]
- Channels 2 and up have their own phasor, clock follower, Schmitt triggers, EOC pulse and random generator (section 9).

### 2.7 BI/UNI and 1-SHOT

- BI/UNI: 0 = bipolar (`5·x`, −5 to +5 V), 1 = unipolar (`5·(x+1)`, 0 to 10 V), for SIN, TRI, SAW, SQR, MORPH and RAND [code `lfoVolts`; test: `testVolts`]. **EOC is always 0 or 10 V** [meas: min 0.0, max 10.0 V with UNI on].
- 1-SHOT: see section 8. The button's light is lit exactly when the param is above 0.5 (`oneShotLight`).

### 2.8 CLOCK, RESET, HOLD

All three are per channel (a mono cable feeds every channel). The Schmitt trigger is `SchmittTrigger` with low threshold 0.1 V and high threshold 1 V (`SwayEngine.hpp`, the same as Rack's) [code].

- **RESET**: a rising edge at or above 1.0 V restarts the cycle at phase 0 and clears the ÷N edge count [meas: 0.50 and 0.99 V ignored; 1.00 and 2.00 V restart]. It works while HOLD is high (phase goes to 0 and stays there) and while parked. It is **not** an end of cycle (no EOC) [test: `testResetIsNotEoc`]. A RESET already high when the module starts is not an edge.
- **HOLD**: high from 1.0 V up, and stays high until the input falls to 0.1 V or less [meas: 0.99 V runs, 1.00 V holds; after 5 V, 0.5 V still holds; 0.1 V releases]. While held the phase does not move, no EOC fires [meas: 0 cycles in 2 s], clock resyncs are suppressed, but the clock follower keeps measuring.
- **CLOCK**: section 7.

### 2.9 Outputs at known phases

[meas, Normal RATE 0 = 1 Hz, PW 0.5, defaults; the phase is the channel's own; RAND is 0 V (5 V in UNI) in the first cycle, section 6]

| Phase | SIN | TRI | SAW | SQR | MORPH (default) | SIN in UNI | SAW in UNI |
|---|---|---|---|---|---|---|---|
| 0 | 0.001 | 0.000 | −5.000 | +5.000 | 0.001 | 5.001 | 0.000 |
| 0.125 | 3.536 | 2.500 | −3.750 | +5.000 | 3.536 | 8.536 | 1.250 |
| 0.25 | 5.000 | 5.000 | −2.500 | +5.000 | 5.000 | 10.000 | 2.500 |
| 0.5 | −0.001 | 0.000 | 0.000 | −5.000 | −0.001 | 4.999 | 5.000 |
| 0.75 | −5.000 | −5.000 | 2.500 | −5.000 | −5.000 | 0.000 | 7.500 |
| 1 (just under) | 0.000 | 0.000 | 5.000 | −5.000 | 0.000 | 5.000 | 10.000 |

SIN and TRI start at 0 and rise together [test: `testSinTriInPhase`]. SAW rises from −5 V and drops at the wrap. The sine uses `fastSin2pi`, within 2.5e-7 of the exact sine [code `FastMath.hpp`].

### 2.10 Rate light

Section 10.

---

## 3. The right-click menu and saved state

[code `Sway.cpp`]

| Item | Choices (menu text) | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| Range | "Slow (4.5 h – 1 s per cycle)", "Normal (0.008–128 Hz)", "Audio (8 Hz–8 kHz)" | Normal | `"range"` | integer 0 (Slow), 1 (Normal), 2 (Audio) | `clamp(int(json_integer_value(r)), 0, 2)`; a missing key leaves the setting as it was |

A saved module's `"data"` is `{"range": 1}`. The 16 params (ids 0..15, including BI/UNI and 1-SHOT) are saved by Rack as ordinary params. **Nothing else is saved: not the phase, not the random state, not the clock lock.** A reloaded patch starts every channel at phase 0, unlocked, with a fresh random sequence.

- **Initialize** (`onReset`): range back to Normal and `engine.reset()`. The params go to their defaults by Rack's default `onReset(const ResetEvent&)`, which calls the legacy `onReset()` that Sway overrides [unverified: Rack's `Module.cpp` is not in the SDK; this is Rack's documented behaviour]. `engine.reset()` sets every voice's phase to 0 and un-parks it, clears every clock follower, and clears the "one-shot was on" memory. **It does not** re-seed or advance the random generators, clear the Schmitt states, cancel a running EOC pulse, or reset the rate-light fade [meas: after an Initialize at cycle 3 the RAND sequence continued with the 4th value, not the 1st; section 6.2].
- **Randomize** is not overridden. Rack's default randomizes every param that allows it, which includes CHANNELS, BI/UNI and 1-SHOT (set with `configParam`/`configSwitch`, not `configButton`), so a Randomize can park the LFO or change the channel count [unverified].
- **JSON edge cases** [code]: the stored integer goes through `int` before the clamp. A hand-edited `"range": 4294967297` narrows to 1 (Normal), not the clamped 2; a JSON real such as `2.0` reads as 0 (Slow) because Jansson's `json_integer_value` returns 0 for reals; a string reads as 0 (Slow). The same pattern was Fog's issue #12 (now closed). Not filed for Sway.
- Bypass: no bypass route is configured (`configBypass` is not called), so a bypassed Sway's outputs follow Rack's default for a module without routes [unverified].

---

## 4. Signal flow

### 4.1 Block diagram

Per channel `c`, every sample, for all 16 channels (`SwayEngine::process`, `Voice::tick`):

```
RATE knob ──┐
FM[c] × att ┴─ rateExponent(range) ── clamp [−24, 14] ── 2^x ── min(·, 0.45 fs) ──► freeHz[c]
                                                                                       │
CLOCK[c] ── Schmitt 0.1/1 V ── ClockSync[c] ── ratio(round(RATE + FM)) / period ──────┤  (locked and cable in)
                                    │ resync?                                          ▼
RESET[c] ── Schmitt ─────────────► reset?  ──► Phasor[c] (double) ── step(freq, dt, hold, oneShot)
HOLD[c]  ── Schmitt ───────────► hold?  ───────►     │ phase (own cycle)              │ wrapped
                                                     │                                ├──► EOC pulse (1 ms, 10 V)
                                                     │                                └──► RandomGen[c].advance()
                                                     ├──► RAND = RandomGen.value(phase, glide)           (no PHASE, no SPREAD)
                                                     ├──► rate light (channel 1 only)                    (no PHASE, no SPREAD)
                                                     ▼
              read phase p = frac(phase + PHASE(+CV) + SPREAD × c / N)
                                                     │
              ┌──────────┬──────────┬────────────────┼───────────────────────────┐
              ▼          ▼          ▼                ▼                           ▼
             SIN        TRI        SAW (BLEP)       SQR(PW) (BLEP)       MORPH = Shaper(p, SKEW, MORPH, SMOOTH, PW)
              └──────────┴──────────┴────────────────┴──── lfoVolts(BI/UNI) ─────┘
```

### 4.2 Block constants

| Constant | Value | Where |
|---|---|---|
| Phase | `double`, in [0, 1) | `Phasor.hpp` |
| Rate exponent clamp | −24 .. 14 | `Rate.hpp` `expToHz` |
| Frequency cap | 0.45 × sample rate (free and clocked) | `clampHz` |
| Audio centre | 261.6256 Hz; RATE × 5/7 octaves | `rateExponent` |
| Schmitt thresholds | low 0.1 V, high 1 V (RESET, HOLD, CLOCK) | `SwayEngine.hpp` |
| EOC pulse | 10 V for `1e-3` s, `PulseGenerator` (high while `remaining > 0`) | `SwayEngine.hpp`, `Triggers.hpp` |
| One-shot rest phase | `1 − 1e-9`; read as 0.99999994 (`BELOW_ONE`) | `Phasor.hpp` |
| Clock ratios | ÷16 ÷12 ÷8 ÷6 ÷4 ÷3 ÷2 ×1 ×2 ×3 ×4 ×6 ×8 ×12 ×16 for indices −7..7 | `ClockSync.hpp` |
| Ratio index | `round(RATE + FM volts × att)`, halves away from zero, clamped −7..7 | `ratioIndex` |
| Random generator | splitmix64; seed `c + 1`; 24-bit values, uniform in [−1, 1] | `RandomGen.hpp`, `Voice.hpp:62` |
| Glide | `max(0, −SMOOTH)` of the cycle, cosine | `RandomGen::value` |
| Rate light blink limit | 10 Hz | `RateLight.hpp` |
| Rate light fade | 30 s⁻¹ (time constant 33 ms), instant on | Rack's `setBrightnessSmooth` default `lambda` |
| Band-limiting | polyBLEP, two-sample, only when the range is Audio | `Waves.hpp`, `SwayEngine.hpp` |

### 4.3 Memory

`sizeof(SwayEngine)` is 1,880 bytes [meas]. No allocation in the engine [code: no `new`, no containers]; the allocation check that other modules have is not built for Sway (section 13).

---

## 5. Cycle, wrap and EOC

### 5.1 The cycle

- `Phasor::step` adds `freq × dt`; when the phase reaches 1 it wraps by `floor` and reports a wrap. With HOLD high, or parked, it does not move.
- `Voice::tick` order: RESET first (phase 0, ÷N count 0), then the clock follower, then the frequency (clocked if locked and the cable is in, else free), then the step, then the clock restart, then the random advance.
- **EOC (`wrapped`)** is true on a sample where a cycle ends: a plain wrap, a one-shot reaching its end, or a clock restart that happens with the phase above 0.5. RESET is not an end [test: `testResetIsNotEoc`, `testEocFreeRunning`, `testEocOneShotOnce`, `testEocOnClockResync`].

### 5.2 PHASE and SPREAD do not move RAND or EOC

[meas, 10 s at 48 kHz, RATE 3 (8 Hz), SMOOTH −0.5, CHANNELS 4. Table: the largest |difference| in volts from the same patch with PHASE 0 and SPREAD 0, channel 1 / channel 4]

| Change | SIN | TRI | SAW | SQR | MORPH | RAND | EOC |
|---|---|---|---|---|---|---|---|
| PHASE knob 0.5 | 10.00 / 10.00 | 10.00 / 10.00 | 5.00 / 5.00 | 10.00 / 10.00 | 10.00 / 10.00 | **0 / 0** | **0 / 0** |
| PHASE CV +5 V at attenuverter +1 (+0.5) | 10.00 / 10.00 | 10.00 / 10.00 | 5.00 / 5.00 | 10.00 / 10.00 | 10.00 / 10.00 | **0 / 0** | **0 / 0** |
| SPREAD 100 % | 0 / 7.07 | 0 / 5.00 | 0 / 7.50 | 0 / 10.00 | 0 / 8.02 | **0 / 0** | **0 / 0** |
| PHASE 0.25 and SPREAD 100 % | 7.07 / 0 | 5.00 / 0 | 7.50 / 0 | 10.00 / 0 | 8.02 / 0 | **0 / 0** | **0 / 0** |

(The last row's channel 1 moves by PHASE and channel 4 by 0.25 + 0.75 = a whole cycle, so it equals the base; the waves change, RAND and EOC do not.)

- With PHASE 0 the first EOC rises at sample 6,000 and SIN first rises through 0 at 6,000. With PHASE 0.5 the first EOC is still at 6,000, but SIN rises through 0 at 3,000. So **RAND steps and EOC fire half a cycle away from the sine's zero crossing**. RAND first leaves 0 V at 6,023 either way [meas].
- Consequence for QA: the manual's "PHASE shifts every output" is wrong for RAND and EOC; the listening guide says the same. Whether that is intended is the user's decision; the spec's wording ("PHASE and SPREAD don't change when values are drawn") supports the code. [not filed]
- Consequence: at SPREAD above 0 all channels' EOC pulses are simultaneous, and the channels' RAND values change at the same instant (each channel has different values, section 6.2).

### 5.3 EOC pulse

[meas, Normal RATE 3, EOC high in samples]

| Sample rate | High for | Duration | Level |
|---|---|---|---|
| 44.1 kHz | 45 samples | 1.0204 ms | 10.0 V |
| 48 kHz | 48 samples | 1.0000 ms | 10.0 V |
| 96 kHz | 97 samples | 1.0104 ms | 10.0 V |
| 192 kHz | 193 samples | 1.0052 ms | 10.0 V |

- The pulse is 1 ms rounded up to whole samples [code `PulseGenerator`]. Another EOC inside a pulse extends it (`trigger` keeps the longer remainder).
- **Fast cycles do not give a trigger.** In the Audio range [meas, 48 kHz]: at 704.2 Hz, EOC is high 70.3 % of the time (704 pulses a second); at 1,155 Hz it is high 99.9 %, and at 1,896 Hz 100.0 %, with no gaps. A cycle shorter than about 1 ms holds EOC high.
- The pulse keeps running while the output is unpatched, so a cable plugged in mid-pulse sees the rest of it [code comment `SwayEngine.hpp`].

---

## 6. RAND

### 6.1 Behaviour

[code `RandomGen.hpp`, `SwayEngine::process`; manual "Shape" and "Outputs"]

- On each wrap (and each clock restart past mid-cycle), `prev = next; next = uniform(−1, 1)`. RAND is `next` once the phase is past `glide`, and before that a cosine from `prev` to `next`: `prev + (next − prev) × (1 − cos(π·t)) / 2`, `t = phase / glide`. `glide = max(0, −SMOOTH)`, with SMOOTH after its CV.
- The phase used is the channel's own cycle phase (not the PHASE or SPREAD read phase).
- Both values start at 0, so **RAND is 0 V (5 V in UNI) for the whole first cycle** after a load or reset [meas: 0.000 V at every sample of cycle 1; the first value arrives at the first wrap].
- Glide shape [meas, RAND over one cycle at eighths, from 0.666 V to 2.458 V]:

| SMOOTH | 0 | 1/8 | 2/8 | 3/8 | 4/8 | 5/8 | 6/8 | 7/8 |
|---|---|---|---|---|---|---|---|---|
| 0 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 |
| −0.25 | 0.666 | 1.562 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 |
| −0.5 | 0.666 | 0.928 | 1.562 | 2.195 | 2.458 | 2.458 | 2.458 | 2.458 |
| −1 | 0.666 | 0.734 | 0.928 | 1.219 | 1.562 | 1.905 | 2.195 | 2.390 |
| +0.5 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 | 2.458 |

  A SMOOTH CV of −5 V at attenuverter +1 (= −1) gives the same glide as the knob [meas]. Positive SMOOTH leaves RAND stepped.
- Free-running, the glide is continuous across the wrap [test: `testSmoothContinuousAcrossWrap`, `testPartialGlideContinuousAcrossWrap`].
- **Steps and jumps** [meas]:
  - RESET mid-cycle at SMOOTH −1: RAND jumped from 3.292 V to 2.458 V in one sample (back to `prev`'s start of the glide).
  - A clock restart before mid-cycle does the same (no `advance`, but the phase restarts, so the glide replays from `prev`). [code]
- Statistics, seed 1, 10⁶ draws, volts (×5) [meas]: mean 0.0062, RMS 2.8877 (uniform ±5 V: 2.8868), min −5.0000, max 5.0000, lag-1 correlation −0.00007. Values are in [−1, 1] [test: `testValuesInRange`, `testNewValueEachAdvance`].

### 6.2 Seeds: every Sway plays the same RAND values

[code `VoiceBank::VoiceBank`: `voices[c].random = RandomGen(c + 1)`; meas]

- Seeds 1 to 16 are fixed constants. RAND values by cycle, volts, RATE 3, SMOOTH 0, after a RESET (cycle 1 is the 0 V first cycle):

| Channel (seed) | cycle 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|
| 1 (1) | 0.000 | 0.666 | 2.458 | 4.710 | −0.556 | −0.557 | 2.629 |
| 2 (2) | 0.000 | 0.912 | 2.491 | 0.956 | 2.654 | −1.884 | −1.534 |
| 3 (3) | 0.000 | −3.865 | 2.003 | 1.130 | −4.271 | −2.836 | 1.362 |
| 4 (4) | 0.000 | −0.685 | 3.924 | 3.591 | −0.082 | −1.054 | 0.867 |

  The first draws of seeds 1 to 16 are 0.67, 0.91, −3.87, −0.69, −1.13, 2.40, −1.10, 1.19, 1.82, −4.67, −1.84, 0.79, 2.69, −0.83, 0.29, −1.33 V.
- A second instance that ran 1.3 s (10 wraps) longer before its RESET produced the same list shifted by 10 cycles (its cycles 1 to 7 equal the first instance's cycles 11 to 17, on all four channels) [meas]. So the sequence depends only on the number of wraps since the patch loaded. **Two Sways with the same wrap count play identical RAND values on the same channel**, and every patch load replays the same sequence from the start.
- **Initialize does not re-seed** [meas: after `engine.reset()` at cycle 3 the sequence went on with the 4th value, 4.710 V, exactly as without it]. A RESET jack also does not restart the sequence.
- Channels differ from each other (distinct seeds), so one Sway's 16 channels are not copies.
- The listening guide's question ("Is that acceptable?") is the user's call. [not filed]

---

## 7. Clock sync

[code `ClockSync.hpp`, `Voice.hpp`; manual "Rate and timing"]

- Patch a clock into CLOCK. The follower counts rising edges (Schmitt 0.1 V / 1 V). The second counted edge sets the period, and from then the LFO runs at `ratio / period`, capped at 0.45·fs.
- **Ratio**: `index = round(RATE + FM volts × att)`, clamped −7..7, table in section 4.2. RATE is not snapped, so the ratio changes at the half-integer knob positions (0.49 → 0, 0.5 → 1, −0.5 → −1, 1.5 → 2, −7.4 → −7, 9 → 7 [meas]). FM moves it one step per volt at attenuverter +1 [meas: RATE 0 with FM 2 V gave ×3, 6.000 Hz from a 2 Hz clock]. The default RATE +1 gives ×2.
- **Restart**: with ratio ×1 or faster, every locked edge restarts the cycle (phase 0). With ÷N, every Nth edge does; the count starts at the first counted edge, and RESET clears it [test: `testDivideFour`, `testResetCount`: ÷4 resyncs on edges 4, 8, 12]. In between, the LFO free-runs at `ratio / period`.
- Locking and following, 120 BPM (2 Hz clock, edges every 24,000 samples at 48 kHz) [meas]:

| RATE | Ratio | Locks at | Frequency |
|---|---|---|---|
| 0 | ×1 | the second counted edge (sample 24,001 for edges at 1, 24,001) | 2.0000 Hz |
| +1 | ×2 | same | 4.0000 Hz |
| +7 | ×16 | same | 32.0000 Hz |
| −3 | ÷4 | same | 0.5000 Hz |
| −7 | ÷16 | same | 0.1250 Hz |

  Before the lock the LFO runs free at RATE's own frequency (for example 128 Hz at RATE +7 in Normal range).
- **A clock already high at the first sample is not counted** [code]: with the first pulse at sample 0, the lock came a period later.
- **Period tracking has no filter** [meas, ×1, edges at 0.5 s, 0.5 s, 0.6 s, 0.5 s, 0.5 s]: the frequency after each edge was 2.0000, 1.6666, 2.0001, 2.0000 Hz. A single late edge changes the frequency for one interval.
- **A stopped clock is not noticed** [meas]: five edges, then 30 s with the cable still in and 0 V: the LFO kept 2.0000 Hz and completed 60 cycles; `clocked` stayed true. Unplugging the cable returned it to RATE's frequency (1.0000 Hz at RATE 0) at once [test: `testClearUnlocks`].
- **EOC with a clock**: a restart gives an EOC only if the phase was above 0.5 [meas: ×1 on a 2 Hz clock gave 39 EOCs in 20 s; ×16 gave 687, of which about 64 came before the lock, while it free-ran at 128 Hz].
- HOLD suppresses restarts (the follower keeps timing). In 1-SHOT, clock edges set the speed but never restart [meas, section 8].
- Poly clock: one follower per channel; a mono clock drives all. The tooltip's `clocked` flag follows channel 1 only [code `SwayEngine.hpp`: `clocked = voices[0].clock.locked()`].

---

## 8. 1-SHOT

[code `Phasor.hpp`, `SwayEngine::process`; meas, Normal RATE 0 = 1 Hz]

- **Parked**: the phase rests at `1 − 1e-9` (read as 0.99999994), so the outputs sit at the end of the cycle: BI: SIN −0.000, TRI −0.000, SAW +5.000, SQR −5.000, MORPH 0.000; UNI: SIN 5, TRI 5, SAW 10, SQR 0 [meas].
- When parked: switching 1-SHOT on (and a patch that loads with it on) parks every channel at once. Before the first RESET the module is parked. A patch loaded with 1-SHOT on read SAW +5.000 V and EOC 0 V from the first sample [meas].
- **RESET** starts one cycle from phase 0 and the cycle ends after `1 / freq` seconds: 47,999 samples (1.0000 s) at 1 Hz free-running [meas]. At the end exactly one EOC fires (1 pulse in 2 s for each of three runs) and the channel parks again [meas]. RESET during a run restarts it (no EOC); RESET while parked starts a run.
- **Clock**: edges never start a one-shot or restart a running one, but they set its speed: with a 4 Hz clock locked, a RESET cycle took 11,999 samples (0.2500 s) [meas].
- **Switching 1-SHOT off while parked fires one EOC on the next sample and starts free-running** from phase ≈ 0 (the parked phase wraps at once) [meas: EOC high at sample 0 after the switch]. [not filed]
- **Switching 1-SHOT on mid-cycle jumps the outputs** to the rest levels in one sample: SAW went from 0.002 V (phase 0.5) to +5.000 V [meas]. [code, by design: "Switching one-shot on parks every channel until the next RESET"]
- **Rest levels and the envelope shapes** [meas, UNI, 1 Hz; volts at phase 0, ⅛, ¼, ½, ¾, then at rest]:

| PHASE | Output | Parked before RESET | 0 | ⅛ | ¼ | ½ | ¾ | Rest |
|---|---|---|---|---|---|---|---|---|
| 0 | SIN | 5.000 | 5.001 | 8.536 | 10.000 | 4.999 | 0.000 | 5.000 |
| 0 | TRI | 5.000 | 5.000 | 7.500 | 10.000 | 5.000 | 0.000 | 5.000 |
| 0 | SAW | 10.000 | 0.000 | 1.250 | 2.500 | 5.000 | 7.500 | 10.000 |
| 0 | SQR | 0.000 | 10.000 | 10.000 | 10.000 | 0.000 | 0.000 | 0.000 |
| 0.75 | TRI | 0.000 | 0.000 | 2.500 | 5.000 | 10.000 | 5.000 | 0.000 |
| 0.75 | SIN | 0.000 | 0.000 | 1.465 | 5.001 | 10.000 | 4.999 | 0.000 |

  (The marks are 0, 6,000, 12,000, 24,000 and 36,000 samples after RESET, which are phases 0, 0.125, 0.25, 0.5 and 0.75.)
  - **With PHASE 0, TRI in UNI is not a 0 to 10 to 0 hump**: it goes 5 → 10 → 0 → 5 V, and rests at 5 V. A hump from 0 V needs PHASE 0.75 (270°): TRI then reads 0 → 10 → 0 and rests at 0 V. The same holds for SIN. SAW is a ramp 0 to 7.5 V (to 10 V at the end) that rests at 10 V. [meas] The listening guide's patch 4 assumes the first (section 11.2).
- **RAND in a one-shot** [meas]:
  - SMOOTH 0: the value held during run k is the value that appeared at the end of run k−1; a new value appears at the end of each run (mid-run 0.000, 0.666, 2.458, 4.710 V with rest values 0.666, 2.458, 4.710, −0.556 V).
  - SMOOTH −1: each RESET jumps RAND back to the previous value (rest 0.666 V → 0.000 V at RESET in run 2) and glides toward the rest value over the run (0.000, 0.097, 0.333, 0.568 V at ¼ steps), which it then **jumps to** (2.458 V) when the run ends.
- Rate light: lit as the phase sits while running; dark when parked (section 10).

---

## 9. Polyphony

[code; manual "Polyphony"; issue #64]

- 16 voices always run; `CHANNELS` and the cables decide how many are written (section 2.6).
- Per channel: phasor, clock follower, RESET, HOLD and CLOCK Schmitt triggers, EOC pulse, random generator, FM value. All CV jacks are read per channel. A mono cable drives every channel; a cable with fewer channels than N reads 0 V on the channels above its count (Rack's rule, `SwayPort::poly`).
- Poly FM: a cable with two or more channels gives each channel its own frequency; a mono or unplugged FM is shared (`sharedFm`) [code].
- **Instances are independent**: one instance's output does not depend on another run in turn with it [test: `testInstancesIndependent`, 0 differences].
- Issue #64 lists Sway as one of two polyphonic modules (the other is Sluice); it needs no change.

---

## 10. Rate light

[code `RateLight.hpp`, `SwayEngine.hpp`; manual "Rate light"; meas]

- It follows channel 1's own phase, rate and hold state. Target brightness [meas]: at 0.01, 1, 9.99 and 10.00 Hz it is 1 in the first half of the cycle (phase below 0.5) and 0 in the second; at 10.01 Hz and above it is 0.5 at any phase; at 0 Hz (HOLD high, or a finished one-shot) it stays at 1 or 0 as the phase left it. NaN or negative rates are 0 [test: `testBadRateIsDark`].
- PHASE and SPREAD do not move it [meas: channel 1 with PHASE 0.5 or CHANNELS 2 / SPREAD 100 % read the same light, 1.0 at phase 0.0017].
- Clocked, it runs at the clocked rate, not the free one [test: `testClockedRateIsTheClockedRate`]. RESET lights it [test: `testResetLightsIt`].
- Fade: Rack's `Light::setBrightnessSmooth(brightness, sampleTime)`: **on is instant, off decays with a 33 ms time constant** (lambda 30 s⁻¹) [code, Rack SDK header]. At 10 Hz the dark half is 50 ms, so the light does not reach 0 between blinks (about 22 % left at the end of the dark half) [code arithmetic: e^(−30 × 0.05)]. How it looks in Rack is [unverified].
- At 0.01 Hz the light is lit for 50 s and dark for 50 s.

---

## 11. Open issues, findings and the listening guide

### 11.1 Filed issues

| Issue | State | Concerns Sway how |
|---|---|---|
| #67 Aliasing: measure every module that makes or bends sound | open | Sway's SAW and SQR edges (polyBLEP in Audio range) have no aliasing measurement; outside Audio the edges are naive even when FM takes the rate to 16.4 kHz. TESTING.md lists Sway under #67 |
| #64 Polyphony: modules should be polyphonic where it makes sense | open | Sway is the reference case (1 to 16 channels with phase spread) |
| #8 Fathom and Sway: measure and reduce CPU use | closed | Explains why the engine is Rack-free, why unpatched outputs are skipped and why the rates are cached; the bench budgets come from it |

No open issue covers any item in 11.2.

### 11.2 Found while preparing this document (not filed)

1. **PHASE and SPREAD do not move RAND or EOC**; the manual says PHASE shifts every output (section 5.2). The listening guide's question is answered by the code: "no". The spec's wording supports the code.
2. **RAND seeds are fixed per channel**; two Sways play the same values, and every patch load restarts the sequence; Initialize does not re-seed (section 6.2). The guide is right, and slightly understates it: not only "started together" but any two Sways with the same wrap count.
3. **RAND and one-shot, RESET and clock restarts step the output** (sections 6.1 and 8). In a one-shot the value you hear during a run is the previous run's; a new value appears at the end. With glide, RESET jumps RAND back to the previous value.
4. **1-SHOT off while parked fires an EOC; on mid-cycle jumps the outputs** (section 8).
5. **UNI TRI and SIN in a one-shot start and rest at 5 V**; only PHASE 0.75 gives a 0 → 10 → 0 hump (section 8). **The listening guide's patch 4 is wrong as written**: "Each Squall gate runs one triangle hump over Sluice" with Sway TRI in UNI and PHASE at 0 gives 5 → 10 → 0 → 5 V and parks at 5 V, which holds Sluice B half open between gates.
6. **RATE tooltip in exponent form at 1,000 Hz and above** ("1.48e+03 Hz", section 2.4).
7. **A stopped clock is never noticed** (section 7).
8. **EOC is not a trigger above about 1 kHz** (section 5.3).
9. **±5 V "sweeps the whole knob range" only from the knob's centre**; MORPH's default is 0 (section 2.3). The guide's "CV SCALE" row repeats the manual.
10. **polyBLEP only in the Audio range**; MORPH's saw is also band-limited, contrary to the manual's wording (section 1.1).
11. **The range JSON key is narrowed to `int` before the clamp**, as Fog's closed issue #12 (section 3).
12. **Randomize can park the LFO or change CHANNELS** [unverified].
13. **Initialize leaves the random state, trigger states and EOC pulse alone** (section 3).
14. **"Three folds"** in the manual and spec: gain 4, folds at ±1 and ±3 (section 2.5).
15. The audio top is 8,372 Hz, not "8 kHz" (the guide says 8.4 kHz, correctly).

### 11.3 The listening guide against the code

Everything else in `sway-2.0.39-listening.html` that I could check agrees with the code: the default 2 Hz; Slow RATE 0 = 2 min 8 s; the three ranges; Audio 8 to 8.4 kHz; RAND uniform in ±5 V; EOC always 10 V and 1 ms; the rate light's 10 Hz switch; SMOOTH's two jobs; the one-shot "clock edges would not restart it"; HOLD freezing the light; "SPREAD 100 % with 4 channels is a quarter cycle apart". The two disagreements are patch 4 (finding 5) and the loose phrase "5 V at full attenuverter sweeps the whole knob range" (finding 9). "Slow breath: Slow, RATE about +1" gives a 64 s cycle, "about a minute", as written.

---

## 12. What the automated suite covers

I built the eight Sway test programs from the 2.0.39 tree with `tests.mk` (`make -f tests.mk MODULES=sway`, `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64`): seven with `-std=c++17 -O2 -Wall -Wextra`, `test_engine` with `-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Wall -Wextra` (the plugin's floating-point flags). No warnings. **All eight passed** on an Apple M-series Mac:

| Program | Prints | Guarantees (tolerance) |
|---|---|---|
| `test_phasor` | all checks passed | frequency by wrap count; periods at 2⁻⁷ and 2⁻¹⁴ Hz within 0.1 %; advances at 2⁻²⁴ Hz; a huge step stays in [0, 1); reset, hold, one-shot park and release; parked read is ~+1 on SAW and low on SQR; `readPhase` wraps; `rateExponent` ends and centres; exponent and 0.45·fs clamps; ±5 V and 0–10 V |
| `test_clock_sync` | all checks passed | unlocked before two edges; ×4 locks to 8.000 Hz (within 1e-3) from 2 Hz and resyncs on every locked edge; ÷4 gives 0.5 Hz and resyncs on edges 4, 8, 12; reset restarts the count; clear unlocks; `ratioIndex` clamps and rounds; a phasor locks to the clock |
| `test_random` | all checks passed | values in [−1, 1] over 10⁵ draws and covering the range; a new value each advance; glide continuous across the wrap (1e-3 for full glide, exact for partial); endpoints; seeds 1 and 2 differ; same seed same sequence; glide 0 is stepped; partial and full glide are cosine |
| `test_waves` | all checks passed | each wave in ±1; triangle step under 5/N; duty within 0.001 of PW; SIN and TRI in phase; polyBLEP is the identity at dt 0; polyBLEP lowers the sum of squared differences against the naive saw and square (a proxy, not a dB bound) |
| `test_shaper` | all checks passed | MORPH 0, ⅓, ⅔, 1 equal the pure waves (1e-5); crossfade midpoint; SKEW 0.5 identity (1e-6), ramps at 0 and 1, peak at 0.2; finite and within ±1.001 across SKEW × MORPH × p; SMOOTH 0 identity; rounding changes the wave by more than 0.05 and stays in range; fold in range and folds; `wrap01` stays below 1 |
| `test_voice` | all checks passed | channels that become active later are in step (free and clocked, 1e-9 and 1e-6 of a cycle); EOC counts: 3 in 3.5 s at 1 Hz, 1 for a one-shot, 8 for ×1 on a 2 Hz clock in 5 s; RESET is not an EOC |
| `test_rate_light` | all checks passed | blink to 10 Hz; steady 0.5 above; 0 Hz holds; bad rates dark; HOLD freezes it; a finished one-shot is dark; clocked rate; RESET lights it |
| `test_engine` | see below | the optimised `SwayEngine` against a frozen copy of the engine it replaced (`testdata/ref/`), over seconds of rendering |

`test_engine` printed [test]:

| Case | Peak difference | EOC samples high |
|---|---|---|
| defaults | −127.3 dBFS | 912 |
| busy (16 channels, poly CV, clock, resets, holds) | −122.4 dBFS | 142,857 |
| audio range, steps | −121.3 dBFS | 317,178 |
| structure changes | −125.2 dBFS | 242,108 |
| sample-rate change | −127.3 dBFS | 143,995 |

The bound is −100 dBFS (50 µV); EOC, channel count, rate light and clocked flag must be equal and unpatched outputs 0 V. It also checks `ratioIndex` against `lround` and NaN FM as no FM, and instance independence.

**What the suite does not prove**: `test_engine` compares the new engine with the old engine's logic, so a behaviour shared by both (PHASE not moving RAND, the fixed seeds, one-shot jumps) is not checked as right or wrong. No test pins PHASE or SPREAD against RAND or EOC.

**Bench** [test, `build/bench/bench sway <case> <count> 10`, % of one core per instance at 48 kHz, Apple M-series Mac, build -O3 with the plugin's flags; the machine was busy with other work (load average 6 to 10), so the first runs read high]:

| Case | Budget (Apple / elsewhere) | 1 instance | 16 instances |
|---|---|---|---|
| idle (no cables) | 0.3 / 0.9 | 0.20 to 0.24 | 0.19 to 0.20 |
| outputs (all outputs patched) | 0.4 / 1.2 | 0.28 to 0.38 | 0.28 to 0.53 (0.35 to 0.36 in repeat runs) |
| busy (16 channels, CV on every input, clock, resets, holds) | 3.0 / 9.0 | 1.79 to 2.47 | 1.71 to 2.44 |

`bench --check` reported 15 module declarations complete. TESTING.md records 0.19 / 0.27 / 1.6 for the same cases.

Not run here: `make libm-calls` and `make engine-globals` (need Linux or GNU ld), `make panels-test`, `make smoke`, `make test` as a whole, the Windows and Linux builds.

### 12.1 Not covered by the automated suite

- Anything in Rack: the panel, tooltips (including the exponent-form text), the menu, save and load through Rack, Initialize and Randomize, bypass, polyphony as Rack delivers it, the rate light and the 1-SHOT light.
- Sway has no sanitizer build, no hostile-input test, no seeded fuzz, no allocation check, no golden saved-state test (TESTING.md "What each module has today": "Partly" for hostile input; "Future work" lists these).
- No aliasing measurement (#67).
- Windows and Linux; CPU on x86-64.
- Listening: whether RAND, MORPH folds and the ranges are right for the music (the guide's "first guesses").

---

## 13. CPU, sample rates and robustness

### 13.1 CPU

Per instance, section 12's table. The cost is **independent of CHANNELS**: 16 voices advance every sample, and an unpatched output is skipped (`outputConnected`). Idle is about 42 ns per sample on the Mac (0.20 % of a core); all outputs patched about 59 to 78 ns; busy about 374 to 515 ns. The per-instance cost at 16 instances equals the cost of one. Windows readings in issue #8 (3 to 4 % per idle instance) predate the 2026-11-15 work; no Windows figure exists for 2.0.39 [unverified].

### 13.2 Sample rates

- The LFO's frequency is in Hz, independent of the sample rate, up to the cap of `0.45 × fs` and the 2¹⁴ Hz exponent cap (section 2.4). The EOC pulse is 1 ms rounded up to whole samples (section 5.3). The rate light and every other time are in seconds or cycles.
- A sample-rate change is handled by `updateRates`, which recomputes frequencies when `sampleRate` changes; `test_engine` "sample-rate change" (48 kHz to 16 kHz and back, at an Audio rate the lower rate clamps) matched the reference.
- The engine ran finite at 8, 44.1, 48, 96, 192 and 768 kHz in the fuzz below. Behaviour of a sample rate of 0, negative or NaN is outside Rack's range and was not examined.

### 13.3 Hostile input

[meas, `meas.cpp` section N] 9,600,000 samples over 60 runs at 8, 44.1, 48, 96, 192 and 768 kHz. Every param (RATE, shape knobs, attenuverters, SPREAD, CHANNELS, BI/UNI, 1-SHOT), every input's voltages and channel counts (0 to 16) and the range were changed every 4,000 samples or 97 samples, with NaN, +inf, −inf, ±1e30 in about 22 % of the draws. **0 non-finite outputs; 0 outputs beyond ±10.5 V; the largest |output| was 10.000 V** (UNI at its top). This is the engine alone, not Rack's port code.

---

## 14. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, every knob at its default (RATE +1 = 2 Hz Normal; PW 50 %, PHASE 0, SKEW 50 %, SMOOTH 0, MORPH 0, SPREAD 0, CHANNELS 1, attenuverters 0), BI, 1-SHOT off, range Normal, nothing patched except the output under test, a scope on it.

### 14.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| RATE, Normal | read the period of SIN at RATE −7, 0, +1, +7 | 128 s, 1 s, 0.5 s, 7.8125 ms (0.0078125, 1, 2, 128 Hz) |
| RATE, Slow | range Slow; RATE −7, 0, +7 | tooltip "4h 33m per cycle", "2m 08s per cycle", "1 Hz"; periods 16,384 s, 128 s, 1 s |
| RATE, Audio | range Audio; RATE 0 with a tuner or spectrum | 261.63 Hz (C4); RATE −7 8.18 Hz; RATE +7 8,372 Hz |
| RATE tooltip | hover at Audio RATE +3.5 and +7 | expected "1.48e+03 Hz" and "8.37e+03 Hz" (finding 6): record the text |
| Dial numerals | RATE at 0, 5, 10 on the dial | 0.0078 Hz, 1 Hz, 128 Hz in Normal (position `(RATE+7)·10/14`) |
| FM | RATE +1, a constant CV into FM at attenuverter +1, −1, +0.5 | +1 V doubles (4 Hz); −1 V halves; attenuverter −1 inverts; +0.5 gives 2.83 Hz at 1 V; attenuverter 0 no effect |
| FM extremes | Normal RATE +7, FM +10 V | 16,384 Hz (not above); Slow RATE −7, FM −10 V: period 194 days (do not wait; read the tooltip) |
| PW | SQR at PW 1 %, 50 %, 99 % | duty 1 %, 50 %, 99 %; SQR starts high |
| PHASE | SIN against TRI and a reference at PHASE 0, 25 %, 50 % | waves lead by 90° and 180°; **RAND and EOC do not move** (finding 1) |
| SKEW | MORPH at SMOOTH 0, MORPH 0.33; SKEW 0, 0.5, 1 | falling ramp, symmetric triangle-like sine, rising ramp; peak stays at the trough-relative point |
| MORPH | sweep 0 to 1 | sine, triangle at ⅓, saw at ⅔, square at 1; the output equals SIN at 0 |
| SMOOTH | MORPH at 0, SMOOTH −1, +0.25, +1 | rounded; 6 and 10 direction reversals per cycle at +0.25 and +1; always within ±5 V |
| SPREAD, CHANNELS | CHANNELS 4, SPREAD 100 %, SIN to a poly scope | four sines a quarter cycle apart; SPREAD 50 %: an eighth; CHANNELS 1 gives a mono output |
| BI/UNI | switch | every wave except EOC moves from ±5 V to 0–10 V; EOC stays 0/10 V |
| Attenuverters | +5 V and −5 V on each CV jack, attenuverter +1 and −1, knob at default | PW 0.99 / 0.01; SKEW 1 / 0; SMOOTH +1 / −1; PHASE ±0.5 cycle; MORPH 0.5 / 0 (section 2.3); attenuverter 0 has no effect |
| CV range | ±10 V, ±50 V on each CV jack | saturates (PW, SKEW, SMOOTH, MORPH); PHASE wraps; no crash, no stuck output |

### 14.2 Waves and outputs

| Area | Procedure | Pass criteria |
|---|---|---|
| Wave start | RESET, then read SIN, TRI, SAW, SQR at 1 Hz | SIN and TRI at 0 V rising; SAW at −5 V rising; SQR at +5 V; table in section 2.9 |
| Levels | all outputs, BI and UNI | within ±5 V; 0 to 10 V; peaks 5.000 V (MORPH too) |
| MORPH = SIN at defaults | both on a scope | identical |
| Audio-range edges | range Audio, SAW and SQR at 261 Hz, spectrum | no aliased lines you can hear; compare with the same SAW in Normal range through FM (no band-limiting there) [aliasing unmeasured, issue #67] |
| EOC | EOC on a scope at 1 Hz | 10 V pulses of 1.00 ms (1.02 ms at 44.1 kHz), one per cycle; fast cycles (> 1 kHz) leave EOC high (section 5.3) |

### 14.3 RAND

| Area | Procedure | Pass criteria |
|---|---|---|
| First cycle | fresh patch, RAND at 8 Hz | RAND sits at 0 V (5 V UNI) for the first cycle, then steps |
| Stepped | SMOOTH 0 and +0.5 | a new value at each wrap (EOC); constant between; values spread over ±5 V |
| Glide | SMOOTH −0.25, −0.5, −1 | glide over the first ¼, ½ and the whole cycle; no step at the wrap |
| Fixed seeds | two Sways, the same channel, load a patch fresh, compare RAND | **identical sequences** (section 6.2): record as the known behaviour; different channels differ |
| Sequence on reload | save and reload; read the first values | the same values again (channel 1: 0, 0.666, 2.458, 4.710, −0.556, −0.557, 2.629 V at RATE 3) |
| Initialize | Initialize mid-run | the sequence continues, no restart |
| PHASE / SPREAD | PHASE 50 %, SPREAD 100 % | RAND and EOC unchanged (finding 1) |
| RESET with glide | SMOOTH −1, RESET mid-cycle | RAND steps back to the previous value (section 6.1) |

### 14.4 Clock, RESET, HOLD

| Area | Procedure | Pass criteria |
|---|---|---|
| Lock | 120 BPM clock into CLOCK, RATE 0 | free-runs until the second edge, then 2.000 Hz, cycle restarts on every edge; tooltip "×1" |
| Default ratio | RATE at the default +1 | ×2 when clocked, 4 Hz |
| Ratios | RATE −7 to +7 in steps; tooltip | ÷16 ÷12 ÷8 ÷6 ÷4 ÷3 ÷2 ×1 ×2 ×3 ×4 ×6 ×8 ×12 ×16; changes at half-integers |
| ÷N | RATE −3 (÷4) | restart every 4th edge; 0.5 Hz; RESET clears the count |
| FM on the ratio | RATE 0, FM +2 V at attenuverter 1 | ×3 |
| Clock stops | stop the clock, cable in | the LFO keeps going at the last tempo (known, section 7); unplug and it returns to RATE |
| Clock jitter | a clock with one late edge | the frequency follows each interval at once |
| RESET | 0.9 V and 1.1 V pulses | 0.9 V ignored; 1.1 V restarts at phase 0; not an EOC; works while HOLD is high |
| HOLD | 0.9 V, 1.1 V, then 0.5 V and 0.05 V | 0.9 V runs; 1.1 V freezes; 0.5 V still frozen; 0.05 V releases; rate light stays put; no EOC while held |
| Poly clock / reset / hold | 4-channel cables with different timing per channel | each channel follows its own; output channel count 4 |

### 14.5 1-SHOT

| Area | Procedure | Pass criteria |
|---|---|---|
| Park | 1-SHOT on | outputs jump to the cycle's end and rest; the cap lights |
| Run | RESET at RATE 0 (1 Hz) | one cycle of 1.000 s, one EOC at its end, then rest |
| Clock | patch a clock, RESET | the cycle lasts one clock period; clock edges never start it |
| Envelope | 1-SHOT, BI/UNI UNI, TRI to a VCA; RESET with gates | with PHASE 0: 5 → 10 → 0 → 5 V (rests at 5 V); with PHASE 0.75: a 0 → 10 → 0 V hump; compare with the guide's patch 4 (finding 5) |
| Off while parked | switch 1-SHOT off at rest | one EOC pulse and free-running again (finding 4) |
| On mid-cycle | switch 1-SHOT on during a cycle | the outputs jump to the rest levels |
| Reload | save with 1-SHOT on, reload | parked at load; the cap lit |

### 14.6 Polyphony

| Area | Procedure | Pass criteria |
|---|---|---|
| Channel count | CHANNELS 1 with a 5-channel cable on any input (including RESET, HOLD, CLOCK) | 5 output channels; CHANNELS 12 with a 4-channel cable: 12 |
| Poly CV | a 4-channel CV into PHASE with attenuverter +1 | each channel moves by its own CV; a mono cable moves all equally |
| Poly FM | a 2-channel FM cable | each channel its own frequency |
| Channel growth | raise CHANNELS from 1 to 8 while running, SPREAD 0 | the new channels are in step with channel 1 |
| SPREAD change of N | SPREAD 100 %, change CHANNELS | the spacing changes at once (outputs jump) |
| Output channels | patch one output only | the other outputs read 0 V |

### 14.7 Rate light and menu

| Area | Procedure | Pass criteria |
|---|---|---|
| Blink | RATE for 0.5, 2, 10 Hz | lit in the first half of the cycle, dark in the second; fades out in about 33 ms; at 10 Hz the dark half is short |
| Steady | above 10 Hz | half brightness, steady |
| Hold | HOLD high | the light stops where it was |
| One-shot | finished one-shot | dark |
| PHASE | PHASE 50 % | the light does not move |
| Range menu | each range, save and reload | restored; tooltip text per range; Initialize returns to Normal |
| Initialize | after changing everything | params default, range Normal, phases 0, clock unlocked; RAND continues (known) |
| Hand-edited JSON | `"range": 5`, `-1`, `"x"`, `2.0` | no crash; `5` clamps to Audio, `-1` to Slow, `"x"` and `2.0` read as Slow (section 3) |

### 14.8 CPU

| Area | Procedure | Pass criteria |
|---|---|---|
| One idle instance | Rack's meter, nothing patched | Mac bench 0.20 % of a core; no Windows figure exists |
| All outputs patched | patch every output | 0.28 to 0.38 % (Mac bench) |
| 16 channels, busy | CHANNELS 16, CV on every input | 1.8 to 2.5 % (Mac bench; budget 3.0) |
| Many instances | 16 instances | per-instance cost the same as one alone |
| CHANNELS | change CHANNELS 1 to 16 with outputs patched | the cost changes only by the per-channel output work, not by a voice count |
| Windows | record the per-instance reading | issue #8 saw 3 to 4 % per idle instance before the CPU work: confirm it is now small |

---

## 15. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a −1..+1 scale on a CV input; negative inverts |
| BLEP, polyBLEP | a short correction at a wave's step that removes most aliasing |
| Channel | one of up to 16 voices; every channel has its own phase, clock follower and random generator |
| Clock restart | the cycle set to phase 0 on a clock edge (every edge for ×N, every Nth for ÷N) |
| EOC | end of cycle: a 10 V, 1 ms trigger when a cycle ends (a wrap, a one-shot finishing, or a clock restart past mid-cycle); not on RESET |
| Fold | the triangle fold of SMOOTH above 0: values past ±1 are reflected back |
| Glide | the cosine move of RAND from the previous to the new value over the first `−SMOOTH` of the cycle |
| Parked | the phase rests at the end of the cycle (one-shot finished or not yet started) |
| Phasor | the phase accumulator, a `double` in [0, 1) |
| Rate light | the light above RATE's name, following channel 1 |
| Read phase | the phase the waves use: the cycle's phase plus PHASE plus SPREAD × c / N; RAND, EOC and the rate light use the cycle's phase itself |
| Rest | the output level of a parked channel |
| Schmitt trigger | a comparator with two thresholds (here 0.1 V and 1 V) |
| Seed | the number that starts a channel's random sequence (channel + 1) |
| Span | the range a CV covers for a control (max − min): 0.98 for PW, 2 for SMOOTH, 1 for the rest |
| Wrap | the phase passing 1 and returning to 0 |

---

### Note on the measurement programs

- `meas.cpp` is one C++ file at `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/sway/meas.cpp`, built outside the repository against the extracted 2.0.39 tree (`techref/sway/tree/`, from `git archive 2e6e960`) with `c++ -std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd -Isrc -Isrc/sway`. Its output is `meas.out` in the same folder. It runs in about 8 s.
- It drives `rka::SwayEngine` through `SwayFrame` as `Sway.cpp` does, one instance, with every output marked patched. It sets the frame's params and inputs directly, so it exercises the engine, not Rack's ports, widgets, tooltips or `Initialize`. The tooltip strings in section 2.4 come from a transcription of `RateQuantity` into the program, not from Rack.
- It has sections A (PHASE and SPREAD against each output), B (RAND sequences, seeds, Initialize, statistics), C (rates, FM, clamps, accuracy, tooltip text), D (the CV law), E (levels at known phases), F (EOC), G (clock), H (RESET and HOLD), I and I2 (one-shot), J (RAND glide and steps), K (MORPH range and folds), L (SPREAD and channel count), M (rate light), N (hostile fuzz).
- The bench runs came from `build/bench/bench` built from `src/bench/bench.cpp` with `tests.mk` in the same copy.
- I did not run `make libm-calls`, `make engine-globals`, `make smoke`, a mutation check of the tests (TESTING.md rule 1), or anything on Windows or Linux.

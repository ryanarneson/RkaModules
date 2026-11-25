# Narrows 2.0.39: Technical Reference for QA

Module: **Narrows**, plugin `rka` (slug `rka`, module slug `Narrows`), VCV Rack 2 (minRackVersion 2.5.0). A 14 HP polyphonic stereo filter.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree (`git archive 2e6e960`). Narrows' own sources are unchanged since build 2.0.24 (section "Build history").
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. Its `plugin.json` (version 2.0.39) and `res/narrows/Narrows.svg` and `Narrows-dark.svg` are byte-identical to the commit's. It holds `plugin.dll` (2,287,104 bytes) and no Narrows presets (the only presets are Fog's).
Document date: 2026-11-24.

## Build history

Narrows has been in three test builds. `git diff 7f03813 2e6e960` is empty for `src/narrows/`, `docs/narrows/`, `res/narrows/` and for the shared headers it uses (`src/Float4.hpp`, `src/FastMath.hpp`, `src/DelayLine.hpp`, `src/fathom/LadderCore.hpp`, `src/fathom/Halfband.hpp`). So 2.0.24 to 2.0.39 differ only in other modules and shared panel code.

| Build | Date | Commit | What changed for Narrows |
|---|---|---|---|
| 2.0.22 | 2026-11-20 | `61e9896` | First build for listening: ladder and state cores, Mode sweep, Drive, self-oscillation, comb, Ping, Spread, polyphony to 16 voices. Also moved Fathom's ladder and halfband into shared templates (Fathom's output unchanged). |
| 2.0.23 | 2026-11-20 | `ea5aac7` | The comb keeps noise at its level (input gain sqrt(1 − f²), `b67e2d5`). Its peaks now reach 15.9 dB over dry, not "about 6 dB" (the user, after listening: "let it go louder"). |
| 2.0.24 | 2026-11-21 | `7f03813` | Final review fixes (`5c4771e`): the core switch keeps the level with sound passing (the kick applies only from Res 0.85); a comb opened by CV from noon starts at pitch (the delay is given its period at noon too). Manual (`44eeb1a`). |
| 2.0.25 to 2.0.39 | 2026-11-21 to 11-24 | | No change to Narrows. Commit `7a2ab51` (2026-11-21) only added Fathom tests (`test_ladder_fast`) and a note in the spec about Fathom's bit-identity. Later builds changed shared panel code (`src/widgets.hpp`, `tools/gen_panels.py`) for Eddy, Spindrift, Beacon, Neap and Spring; whether that moved anything on Narrows' panel was not examined [unverified]. |

---

## 0. How to read this document

- Every number here comes from one of these sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources, cited as file:line (`src/narrows/` unless a path is given).
  - **[test]**: printed or asserted by the module's automated tests. I built the five Narrows test programs from the 2.0.39 tree with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; `test_hostile` with ASan and UBSan at `-O1`) and ran them on an Apple M-series Mac. **All five passed** (section 14). Narrows' tests print very little, so most tolerances below are read from the test source.
  - **[meas]**: a measurement program I wrote against the engine (`rka::NarrowsEngine`, the code the module runs), at 48 kHz unless stated, compiled with the plugin's flags. The programs are in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/narrows/meas/` (list at the end).
- **[issue #N]** points to an open GitHub issue in `ryanarneson/RkaModules` (section 15). No open issue is about Narrows alone.
- **[unverified]** marks anything I could not check against code or by measurement, mostly Rack host behaviour and Windows.
- Voltages are Rack volts. "Noon" is a knob at 0.5. A knob value is in [0, 1] (Comb and FM in [−1, +1]).
- Measurement conventions: the pitch of a tone is the mean period of its rising zero crossings on OUT L; "cents" are 1200·log2(measured / wanted); a gain is the RMS of OUT L over the RMS of the input sine after settling; "state" and "ladder" name the two cores; "Res 1" is the knob fully right.
- The user's listening guide, `narrows-2.0.39-listening.html`, is a cross-check only. Two places where it disagrees with the code are in section 1.2.
- I could not run Rack, the Windows build, `make libm-calls` (Linux only, issue #36) or `make smoke`.

---

## 1. What Narrows is

Narrows is a polyphonic stereo filter for sound that is already in the patch (noise, strings, echoes) or for sound it makes itself: struck by a trigger at PING, or played as a sine voice from V/OCT with Res at the top. The spec is `docs/specs/2026-11-20-narrows-design.md`. The picture is a strait where a channel closes and only part of the water gets through.

Design goals that QA can check:

- Low-pass, band-pass and high-pass on one pair of outputs, with a Mode dial (and CV) that sweeps smoothly between them.
- Two characters on a switch: Fathom's four-pole transistor ladder (24 dB per octave) and a state-variable filter (12 dB per octave).
- Resonance up to self-oscillation, in tune under V/OCT, so the filter is also a sine voice. The two cores start singing at the same place on Res and sing at about the same level.
- A Drive stage; a tuned comb ahead of the filter; PING striking the filter with a short pulse; Spread for width from a mono source.
- A filter pair for every channel of a polyphonic cable, up to 16, at a small cost per voice.
- Anti-goals (spec): no click when Mode, the core switch, Comb or the voice count changes, by hand or CV; no zipper noise on Cutoff; no runaway level with Res, Comb and Drive all at the top; no out-of-tune self-oscillation; the two cores must not sit at different pitch or resonance for the same knobs; no change to Fathom.

Non-goals (do not file these as defects): a notch response or all responses at once; a slope control; a comb with its own tuning or outputs; a fine-tune knob; CV on Drive or Spread; linear FM; separate left and right CVs; a close copy of the I-ō47 or of a Moog filter.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. The spec was written first and carries an "As built (2026-11-21)" note for the first four rows below; its body was not rewritten. The manual (`docs/narrows/README.md`) follows the as-built numbers.

| Topic | Spec says | Manual says | 2.0.39 does |
|---|---|---|---|
| Internal scale | body: "10 V is 1.0 inside"; as-built note: 28 V is 1.0 | silent | 28 V = 1.0 (`NarrowsTuning.hpp:12`) |
| Res law | body: feedback 4.1 × Res², onset near 99 %; note: square law to feedback 4 at 95 %, then a line to 4.6 | "Up to 95 % the peak sharpens. From 95 % up the filter self-oscillates" | feedback 4·(Res/0.95)² to Res 0.95, then a line to 4.6 at Res 1 (`Laws.hpp:52`). Ring holds from Res 0.9494–0.9500 [meas] |
| Self-oscillation level | body: "4 to 9 V"; note: "4 to 5 V" | "about 4 to 5 V at 100 %" | state 4.07 V, ladder 4.82–4.92 V at Res 1 from 30 Hz to 1 kHz; ladder 4.34–4.62 V and state 4.12–4.98 V from 3.5 to 12 kHz [meas]. Test bound is 4 to 9 V. Drive lowers it by up to 15 dB (section 9.3) |
| Comb level | body: "no more than about 6 dB" over dry; note: 16 dB | "to about 16 dB above the dry sound at full" | sqrt((1 + f) / (1 − f)) = 15.9 dB at f = ±0.95 [meas; test `comb_level`] |
| Tuning of the sine | within 5 cents (onset) and 15 cents (to Res 100 %), 30 Hz to 4 kHz, at 44.1, 48 and 96 kHz | within 1 cent as it begins to sing, 7 cents at 100 %, 55 Hz to 3.5 kHz | 55 Hz–3.5 kHz: worst 0.86 and 6.91 cents [test]. 30 Hz–4 kHz: state 8.06 cents at 4.1 kHz at 44.1 kHz (the manual's 7 does not hold at 4 kHz, the spec's 15 does). **Above 4 kHz it drifts**: state up to +19.5 cents, ladder down to −11 cents at Res 1 (section 5.2) |
| Cutoff clamp | 8 Hz to the lower of 20 kHz and 0.45 of the rate | "held between 8 Hz and 20 kHz" | as the spec: 8 Hz to min(20,000 Hz, 0.45 × rate), so 19,845 Hz at 44.1 kHz (`Laws.hpp:41`) |
| Cutoff default | "Default 1 kHz (knob 0.6)" | "It opens at 1 kHz" | knob 0.6 = 1,024 Hz; the tooltip reads "1.02 kHz" |
| Core parameter | note: 1 is Ladder (toggle up, default), 0 State | ladder up | as the spec; the tooltip names 0 "State (12 dB)" and 1 "Ladder (24 dB)" |
| "The two cores are matched" | same pitch, same onset, same level (within 3 dB), unity passband at Res 0 | "at the same settings they sit at the same pitch, begin to self-oscillate at the same place on Res, and ring at about the same level" | true for those four. **Not matched**: the −3 dB corner (state at the cutoff; ladder at 0.435 × the cutoff), the resonant peak (at Res 0.9 the ladder's is 6.6 dB higher and rings 2.7× longer), and the self-oscillation level (ladder +1.6 dB over state at Res 1; +2.3 dB at Res 0.97) [meas] |
| State core's peak | silent | "State: a sharper resonant peak" | at Res 0.9 the state peak is +16.7 dB and its T60 at 440 Hz is 34 ms; the ladder's is +23.3 dB (at 0.972 × the cutoff) and 91 ms [meas]. Not "sharper" by either measure |
| Ping | "about 1 ms long, a smooth single hump, as loud as a 5 V input"; "one edge, one pulse" | "one short pulse (1 ms, 5 V), on both sides" | 1 ms 16p²(1−p)² hump, peak 5 V in, 4.93–4.97 V at the output of an open low-pass [meas]. **An edge arriving while a pulse runs is ignored**, so a 1 kHz gate gives 500 pulses a second (section 7.1). Not in spec or manual |
| Ping as a tuned strike | "with Res up, the filter rings at the cutoff and dies away" | "a tuned ping" (patch 3: Res 90 %) | the ring pitch is the resonance peak, **49 cents flat of the cutoff in the ladder at Res 0.9** (peak at 0.972 × cutoff), 4.6 cents flat in the state core. The pulse's spectrum falls 30 dB between 880 Hz and 1.76 kHz, so rings above about 1.5 kHz are weak (section 7.2) |
| Drive | "a 5 V sine in the passband comes out within 6 dB of its level at Drive 0, across the whole travel" | "the sound gets dirtier but not much louder" | true at 5 V (largest rise +5.3 dB, ladder at Drive 0.7). At full Drive a 2 V sine is up to +8.2 dB and a 0.5 V sine up to +9.1 dB above its Drive 0 level [meas]. A self-oscillating sine falls 15.0 dB at full Drive (section 9.3) |
| Core crossfade | "about 30 ms"; a self-oscillating voice is back within 3 dB inside 100 ms at 100 Hz and above | "a few hundredths of a second, with no click, and a self-oscillating tone carries across" | 30 ms linear (`NarrowsTuning.hpp:80`). Envelope dips 0.3–3.7 dB in the first 100 ms; at 100–200 ms it is within 1.7 dB of the old level (section 5.6). The test asserts only "after > 0.5 × before" (−6 dB) |
| Knob smoothing | "about 5 ms"; jacks not smoothed | silent | 5 ms one-pole on the knobs, on Mode (knob + CV) and on the R-output switch; none on V/OCT, FM, Res CV or Comb CV (section 2.11) |
| Comb CV and clicks | anti-goal: no click when Comb changes "by hand or by CV" | silent | the knob is smoothed; the Comb CV is not. On a 5 V 100 Hz sine (own step 0.065 V a sample) a Comb CV step 0 → +5 V gave a largest step of 0.109 V (state) and 0.061 V (ladder), and 0 → −5 V gave 0.205 V and 0.119 V (section 11) |
| CPU | "about 0.3 % or less for one or two voices in Ladder and about 2 % for 16" | "about 0.35 % ... (0.25 % state) ... about 2.5 % for 16" | 0.37 % (one voice, ladder), 0.27 % (state), 2.4–2.6 % (busy, 16 voices) on a loaded Mac; TESTING.md 0.33 / 0.24 / 2.43 (section 12.2) |
| Tooltip numbers | none | none | Cutoff reads "x.x Hz" below 1 kHz and "x.xx kHz" above; the Mode tooltip names the blend; typed values: Cutoff takes "440", "440 Hz", "2.5 k", "2.5kHz"; Mode takes only "LP", "BP", "HP" (section 2.3) |
| Input limits | "held to ±24 V; NaN and infinities read 0" | "limited to ±24 V" | as stated. Also a CV or audio sample with magnitude 1e6 V or more reads 0, and a knob value of 100 or more (or NaN) reads 0 (`NarrowsEngine.hpp:348-363`) |
| Param and port ids | not pinned | not pinned | a `static_assert` ties the module's `CUTOFF`, `MODE` and `PARAMS_LEN` to the engine's enum (`Narrows.cpp:122`); no assert pins the absolute numbers of the 8 params, 8 inputs and 2 outputs (section 2.2) |

### 1.2 Where the listening guide is wrong or incomplete

The code wins. Where the guide disagrees:

1. **Patch 3, "A tuned ping" ("Each gate rings the filter at the pitch of the step").** At Res 90 % the ladder rings about 49 cents flat of the V/OCT pitch (the state core about 5 cents flat), and the first peak of the ring is 15 to 21 dB lower at 1.76 kHz than at 880 Hz and 25 to 33 dB lower at 3.5 kHz. The ring approaches the pitch as Res approaches 95 %.
2. **"SELF-OSCILLATION ... within 7 cents of pitch."** True from 55 Hz to 3.5 kHz. Above 4 kHz the state core is sharp by 7 to 19.5 cents and the ladder flat by 1 to 11 cents at Res 100 %.
3. The guide's "DRIVE ... taken back at the output" does not say the self-oscillating sine is turned down by up to 15 dB too.

Everything else in the guide that I checked agrees with the code: the Cutoff span, default core Ladder, Drive's 24 dB and 62 % make-up (in octaves), the comb's 16 dB, Spread's half octave each way, the CV scales (Res and Mode 10 V full travel; Comb 5 V each way; FM 1 V per octave at full) and the four-voice poly note for patch 5.

---

## 2. Panel, controls and ports

### 2.1 Layout

14 HP. Positions in mm from `NarrowsLayout.hpp` [code]:

| Control | Centre (x, y) mm |
|---|---|
| Cutoff (hero knob, enamel face 16 to 16K) | 35.56, 37.0 |
| Core switch (LADDER up, STATE down) | 62.5, 37.0 |
| Drive, FM, Res, Comb, Spread knobs | 7.8, 21.7, 35.56, 49.4, 63.3 at y 64.0 |
| V/OCT, FM CV, Res CV, Comb CV, PING jacks | 7.8, 21.7, 35.56, 49.4, 63.3 at y 83.0 |
| IN L, IN R | 7.8, 102.0 and 7.8, 116.0 |
| Mode CV jack; Mode knob (half-turn face LP, BP, HP) | 21.7, 108.0; 42.5, 108.0 |
| OUT L, OUT R (ivory tiles) | 63.3, 102.0 and 63.3, 116.0 |

The Mode knob's travel is ±90° from noon: LP at nine o'clock, BP at noon, HP at three (`Narrows.cpp:160`). There are no lights and no menu.

### 2.2 Parameter, input and output ids

Saved patches use these numbers; they must not change. Unlike Fog's, no `static_assert` pins them: the module's param enum is only tied to the engine's (Cutoff, Mode and the count, `Narrows.cpp:122`), and the input and output ids are not asserted at all. The ids marked * are the two that assert covers.

| Id | Param | Range | Default | Display |
|---|---|---|---|---|
| 0* | Cutoff | 0..1 | 0.6 | Hz or kHz; typed value accepted |
| 1 | Drive | 0..1 | 0 | % (0..100) |
| 2 | FM | −1..+1 | 0 | % (−100..+100) |
| 3 | Res | 0..1 | 0 | % |
| 4 | Comb | −1..+1 | 0 | % (−100..+100) |
| 5 | Spread | 0..1 | 0 | % |
| 6 | Core | 0 or 1 | 1 (Ladder) | switch: 0 "State (12 dB)", 1 "Ladder (24 dB)" |
| 7* | Mode | 0..1 | 0 (LP) | "LP", "BP", "HP" or the blend |

| Id | Input | | Id | Output |
|---|---|---|---|---|
| 0 | V/OCT ("1 V/octave (polyphonic)") | | 0 | L ("Left (alone: both sides, averaged)") |
| 1 | FM ("FM") | | 1 | R ("Right") |
| 2 | Res CV ("Resonance CV") | | | |
| 3 | Comb CV ("Comb CV") | | | |
| 4 | PING ("Ping (a trigger strikes the filter)") | | | |
| 5 | IN L ("Left (alone: both sides; polyphonic)") | | | |
| 6 | IN R ("Right (alone: both sides; polyphonic)") | | | |
| 7 | Mode CV ("Mode CV") | | | |

### 2.3 Tooltips and typed values

[code, `Narrows.cpp:46-101`; `Laws.hpp`]

| Control | Tooltip label | Display |
|---|---|---|
| Cutoff | "Cutoff" | `16 × 2^(10 × knob)` Hz: "%.1f Hz" below 1,000 Hz, "%.2f kHz" from 1,000 Hz. Knob 0 reads "16.0 Hz", noon-default "1.02 kHz", knob 1 "16.38 kHz" |
| Drive | "Drive (into the filter's saturation)" | % |
| FM | "FM (1 V per octave at full)" | % |
| Res | "Resonance (self-oscillates from 95 %)" | % |
| Comb | "Comb (right: peaks at the cutoff; left: between)" | % |
| Spread | "Spread (left cutoff down, right up; half an octave each at full)" | % |
| Core | "Core" | "State (12 dB)" / "Ladder (24 dB)" |
| Mode | "Mode" | "LP", "BP", "HP", or "LP 60 % / BP 40 %" (or "BP 20 % / HP 80 %"); a value within 0.5 % of a point reads as the point |

- **Typed Cutoff**: "440", "440 Hz", " 2.5 k", "2.5kHz", "1K" are accepted [test: `names`]; "", "abc", "-5", "nan", "12 q" are rejected and leave the knob alone. A typed value past the ends is held to the knob's travel (20 kHz types as 16.38 kHz).
- **Typed Mode**: only "lp", "bp", "hp" (any case, trimmed). Numbers do nothing. The tooltip reads the knob alone; it does not show the CV.

### 2.4 Cutoff, V/OCT and FM

[code, `NarrowsEngine.hpp:197-199`, `Laws.hpp:16-47`]

```
octaves above 16 Hz = 10 × Cutoff_sm + V/OCT + FM_cv × FM_sm      (per voice)
hz (side)           = 16 × 2^octaves × Spread factor (left 2^(−S/2), right 2^(+S/2))
cutoff given        = clamp(hz, 8 Hz, min(20,000 Hz, 0.45 × rate))     NaN gives 8 Hz
```

- The knob runs 16 Hz to 16,384 Hz in ten octaves. Measured corner and pitch against the knob are in section 5.
- **V/OCT**: 1 V per octave, per voice, at audio rate, unsmoothed. A one-channel cable applies to every voice.
- **FM**: an attenuverter on the FM jack's volts; fully right is +1 V per octave per volt, fully left −1. Per voice, audio rate, unsmoothed. Default 0, so a new cable does nothing until the knob is turned up.
- Jacks add in octaves: knob +0.1 equals V/OCT +1 V exactly [meas: 0 V output difference on noise]; FM knob 0.5 with 2 V equals V/OCT 1 V exactly.
- The cutoff is worked out every sample with `fastExp2` and the ladder's rational tangent `tan34`; there is no libm per sample [code].
- The comb's delay follows the same per-side cutoff (section 8).

### 2.5 Core: Ladder or State

[code, `NarrowsEngine.hpp:116-181`, `StateCore.hpp`]

- Param 6 is read as ≥ 0.5 = Ladder. Both cores run at twice the host rate through Fathom's halfband pair (stopband 78 dB, passband flat to 1e-7 dB; `src/fathom/Halfband.hpp`). Only the selected core runs, except during the crossfade.
- **Crossfade**: a linear fade over 30 ms (`CORE_FADE_S`): 1,440 samples at 48 kHz. The incoming core is reset to rest. While both run, the incoming core is fed the outgoing core's output on top of its input, by the outgoing core's share of the fade ("the kick"), so a ringing voice is handed over and not left to grow again from noise. The kick scales from 0 at Res 0.85 to full at Res 0.95 (`KICK_RES_FROM`, `RES_ONSET`), so below Res 0.85 the input alone carries the sound. If the switch is flipped back mid-fade both cores are already running and there is no kick.
- The state core is Fathom's `Svf.hpp` maths in the trapezoidal (zero-delay) form, run on `Float4` (`StateCore.hpp:47`). Its damping `k` follows the Res law and it carries two soft clips:
  - Damping: √2 at Res 0 (Butterworth, −3 dB at the cutoff); `√2 (1 − (Res/0.95)²)` up to Res 0.95, where it is 0; then a line to −0.05 at Res 1.
  - Resonance clip: `damp = √2 + (k − √2) × t`, where `t = tanh(x)/x` of 2.6 × the last band-pass output (clamped to ±4 in the argument, floor 0.01). As the band-pass signal grows the damping returns toward √2, which holds the oscillation at a steady level.
  - Input soft clip: the input (plus noise) is clamped to ±4 internal units (±112 V) and passed through `tanh`.
- Both cores add a white noise of 1e-6 internal units per oversampled sample into the loop (`LADDER_NOISE`), so self-oscillation starts from silence. The noise is one stream per voice, shared by its two sides.
- Both cores' outputs go through the ladder's soft limit `2 tanh(v/2)` (±2 internal units = ±56 V) before downsampling; that is a backstop only.

### 2.6 Mode: LP, BP, HP

[code, `Laws.hpp:79-115`, `NarrowsEngine.hpp:214-221`, `322-326`]

```
mode = clamp(knob + CV/10, 0, 1)       smoothed per voice, 5 ms (knob and CV together)
0 to 0.5: bp = 2m, lp = 1 − bp, hp = 0        0.5 to 1: hp = 2m − 1, bp = 1 − hp, lp = 0
```

- State: output = lp·LP + bp·BP + hp·HP of the state core's three outputs, then the soft limit.
- Ladder: the weights slide on the ladder's five points (loop input u and poles y0..y3) between Fathom's rows: LP24 (0, 0, 0, 0, 1), BP (0, 0, 4, −8, 4), HP (1, −4, 6, −4, 1). The passband make-up `1 + k × lp` applies in proportion to the LP weight.
- At a voice's first sample Mode takes its value at once (`modeFresh_`), no slide.
- Measured gains are in section 5.3.

### 2.7 Res

[code, `Laws.hpp:52-59`, `NarrowsEngine.hpp:210-211`]

```
res      = clamp(Res_sm + CV/10, 0, 1)                (CV unsmoothed)
ladder k = 4 (res/0.95)²                for res ≤ 0.95
         = 4 + 0.6 (res − 0.95)/0.05    above
```

| Res | 0 | 0.25 | 0.5 | 0.75 | 0.9 | 0.95 | 0.975 | 1 |
|---|---|---|---|---|---|---|---|---|
| ladder k | 0 | 0.277 | 1.108 | 2.493 | 3.590 | 4.000 | 4.300 | 4.600 |

- The low-pass keeps its passband level as Res rises (Fathom's `comp = 1 + k` in the ladder; the state core's damping law). Measured in section 5.4.
- Above the onset the ladder's pitch would fall as the feedback grows. The engine raises the ladder's `g` by `shift = (0.061921 − 0.024260 e) e × max(0, 1 − 2.3 g)`, `e = k − 4` (`PITCH_FIX_*`, `NarrowsEngine.hpp:259-263`). The measured result is in section 5.2.

### 2.8 Drive

[code, `Laws.hpp:61`, `NarrowsEngine.hpp:138-142`, `242`]

- Gain into the core: `2^(4 × drive)`, 1 to 16 (24 dB). Make-up on the output of both cores: `2^(−0.62 × 4 × drive)`, 1 to 0.179 (−14.95 dB). The knob is smoothed 5 ms; there is no CV.
- The gain sits **after the comb and before the upsampler**; the make-up multiplies everything that leaves the core, including a self-oscillating sine and a ringing ping (section 9.3).

### 2.9 Comb

[code, `Laws.hpp:67`, `Comb.hpp`, `NarrowsEngine.hpp:202-206`]

```
c        = clamp(Comb_sm + CV/5, −1, 1)        (CV unsmoothed)
feedback = ± 0.95 × (1 − (1 − |c|)²)           0 at noon exactly
y[n]     = sqrt(1 − f²) × x[n] + f × y[n − d]  d = rate / cutoff of that side, in samples
```

- Knob to feedback: 0.1 → 0.1805, 0.25 → 0.4156, 0.5 → 0.7125, 0.75 → 0.8906, 0.9 → 0.9405, 1 → 0.95 [code].
- The delay is one period of that side's cutoff, held between 2 samples and the line's length; it slides to a new period with a 3 ms one-pole (`COMB_SLIDE_S`), so a V/OCT jump bends and does not click. 4-point Hermite reads.
- At feedback exactly 0 the input is returned untouched, but still written, so turning the comb in finds the line full. A comb opened from noon by CV starts at pitch [test: `comb_opens_at_pitch`].
- Lines: 32 (16 voices × 2 sides), each sized for one period of 8 Hz at the current rate (power of two ≥ rate/8 + 12 samples). Memory in section 12.4.

### 2.10 Spread, Ping and the inputs

- **Spread** (0..1): left cutoff × 2^(−S/2), right × 2^(+S/2): half an octave each way at full (`SPREAD_OCTAVES` = 1 between sides). At 0 the factors are exactly 1, so with the same signal on both sides L and R are identical to the bit [test: `spread`, 0 differing samples in 48,000 with everything else engaged]. The comb follows each side's cutoff. Knob smoothed 5 ms.
- **PING**: section 7.
- **IN L alone or IN R alone feeds both sides.** With both patched, each side has its own audio.
- **Polyphony and CV channels**: section 10.

### 2.11 Smoothing summary

[code; `smooth_ = 1 − exp(−1/(0.005 × rate))`, a value within 1e-5 of its target lands on it]

| Signal | Smoothing |
|---|---|
| Cutoff, Drive, FM, Res, Comb, Spread knobs | 5 ms one-pole (63 % in 5.5 ms, 99 % in 22.5–23 ms [meas, Cutoff knob 1 kHz → 2 kHz, self-oscillating]) |
| Mode (knob + CV together, per voice) | 5 ms one-pole |
| OUT R patched or not (the L-average switch) | 5 ms one-pole |
| V/OCT, FM jack, Res CV, Comb CV | none (V/OCT +1 V moved the pitch a full octave within one cycle [meas]) |
| Core switch | 30 ms linear crossfade |
| Comb delay | 3 ms one-pole |
| First process after reset, rate change, or a voice joining | knobs (and that voice's Mode) take their values at once |

---

## 3. The right-click menu, saved state, bypass and Initialize

- **There is no menu item and no module JSON.** `Narrows.cpp` defines no `dataToJson` or `dataFromJson` [code]. A saved module is its eight params; patches load identically in any order. TESTING.md records "Golden saved-state image: No (params only)".
- **Initialize** (`onReset`): resets the engine (every voice to rest; combs, filters, halfbands, Ping and noise states cleared; the next frame's knob positions are taken at once). Rack resets the knobs to their defaults (Cutoff 0.6, Core Ladder, the rest 0).
- **Sample-rate change** (`onSampleRateChange`): reallocates all 32 comb lines and resets the engine; the rate is ignored if it is not a positive number. The module also calls `setSampleRate` in its constructor at Rack's current rate (48,000 if there is no engine). Whether Rack calls this on the audio thread or another is [unverified].
- **Bypass** (`processBypass`, `Narrows.cpp:146`): IN L to OUT L and IN R to OUT R, every channel; either input alone goes to both outputs. With neither patched each output is one channel of 0 V. `process()` is not called while bypassed, so the filter state is frozen and resumes when bypass ends [unverified in a live Rack].
- Nothing is saved about voices, tails or the comb: a reloaded patch starts every voice from rest.

---

## 4. Signal flow

### 4.1 Block diagram

Per voice and side; voices go through the cores four sides at a time (`rka::Float4`: pass p holds voices 2p and 2p+1 as lanes L, R, L, R). Lanes never mix, so a voice's output does not depend on its neighbours [test: `voices_independent`].

```
 IN L, IN R ── (either alone feeds both) ── hold to ±24 V, NaN/inf/≥1e6 → 0 ── × 1/28 ─┐
 PING (per voice) ── Schmitt 1 V / 0.1 V ── 1 ms hump × 5 V/28 ───────────────── + ─────┤  (both sides)
                                                                                         ▼
 cutoff per side: 10·Cutoff_sm + V/OCT + FM·FM_sm, ± Spread/2 ─► clamp 8 Hz..min(20k, 0.45 fs) ─► hz
                                                                                         │
                    COMB  y = √(1−f²)·x + f·y[n−d],  d = fs/hz (slid 3 ms)   f from Comb_sm + CV/5
                      (f = 0: input passes untouched, still written)
                                                                                         ▼
                    × DRIVE GAIN 2^(4·Drive_sm)
                                                                                         ▼
                    UP ×2 (halfband pair, 3 allpass sections per branch)
                                                                                         ▼
        ┌── LADDER core: g = tan(π·hz/(2·fs)), k from Res, pitch fix above Res 0.95 ──┐
        │    mix of u, y0..y3 by Mode rows, passband make-up (1 + k·lp), soft limit 2·tanh(v/2)
        │   STATE core: trapezoidal SVF, damping from Res, resonance clip, input tanh      ├─► crossfade
        └── LP·lp + BP·bp + HP·hp, soft limit 2·tanh(v/2) ───────────────────────────────┘   (30 ms linear on the switch)
                                                                                         ▼
                    DOWN ×2 (halfband) ─► × make-up 2^(−2.48·Drive_sm) ─► × 28 (volts)
                                                                                         ▼
                    (R output unpatched: L = (L + R)/2, 5 ms switch)
                                                                                         ▼
                    OUTPUT SOFT LIMIT per side: identity to ±8 V, 8 + 2·tanh((|x| − 8)/2) above (ceiling 10 V)
```

The engine has **no silence sleep**: it runs every sample whether or not anything is patched, so a self-oscillating or pinged filter works with no input. The bench's idle case therefore costs the same as its outputs case (section 12.2).

### 4.2 Block constants

[code, `NarrowsTuning.hpp`]

| Constant | Value |
|---|---|
| Volts per internal unit (`VOLTS`) | 28 (a 5 V signal is 0.18 units) |
| Input hold (`INPUT_LIMIT_V`) | ±24 V |
| Output soft limit knee / ceiling | 8 V / 10 V (`OUT_KNEE_V`, `OUT_CEIL_V`); the curve uses `fastTanh` |
| Cutoff base, span, default | 16 Hz, 10 octaves, knob 0.6 |
| Cutoff floor, ceiling, fraction of rate | 8 Hz, 20,000 Hz, 0.45 |
| Spread at full | 1 octave between the sides |
| Res onset; kick from; ladder k at onset; at top | 0.95; 0.85; 4.0; 4.6 |
| State damping at Res 0; at Res 1; clip scale | √2; −0.05; 2.6 |
| Pitch fix A, B, G | 0.061921, −0.024260, 2.3 |
| Drive span; make-up share | 4 octaves (24 dB); 0.62 of it |
| Comb feedback at the ends; minimum delay; slide | ±0.95; 2 samples; 3 ms |
| Ping length; peak | 1 ms; 5 V |
| Smoothing; core fade | 5 ms; 30 ms |
| Halfband coefficients A | 0.0628224, 0.4243809, 0.7818615 |
| Halfband coefficients B | 0.2238073, 0.6165344, 0.9274736 |
| Ladder noise | 1e-6 units per oversampled sample |
| Voice noise seed | `0x9e3779b9 × (voice + 1)`, an LCG (1664525, 1013904223) |

The halfband's group delay and the comb/filter latency were not measured [unverified].

---

## 5. The filter response, measured

All figures here are from `m_cutoff`, `m_res`, `m_level` and `m_rates` at 48 kHz unless a rate is given. Cutoff 1,024 Hz (knob 0.6) is the default test point.

### 5.1 Corner against the Cutoff knob

Low-pass, Res 0, 0.1 V sine, the −3 dB point found by bisection [meas]:

| Knob | Cutoff (law) | State −3 dB corner (ratio) | Ladder −3 dB corner (ratio) |
|---|---|---|---|
| 0 | 16 Hz | 16.01 Hz (1.001) | 6.95 Hz (0.434) |
| 0.1 | 32 Hz | 32.21 Hz (1.007) | 13.89 Hz (0.434) |
| 0.2 | 64 Hz | 63.72 Hz (0.996) | 27.64 Hz (0.432) |
| 0.3 | 128 Hz | 127.98 Hz (1.000) | 55.37 Hz (0.433) |
| 0.4 | 256 Hz | 255.42 Hz (0.998) | 111.35 Hz (0.435) |
| 0.5 | 512 Hz | 511.38 Hz (0.999) | 222.04 Hz (0.434) |
| 0.6 | 1,024 Hz | 1,022.98 Hz (0.999) | 444.73 Hz (0.434) |
| 0.7 | 2,048 Hz | 2,045.53 Hz (0.999) | 890.40 Hz (0.435) |
| 0.8 | 4,096 Hz | 4,090.20 Hz (0.999) | 1,787.21 Hz (0.436) |
| 0.9 | 8,192 Hz | 8,183.86 Hz (0.999) | 3,626.98 Hz (0.443) |
| 1 | 16,384 Hz | 16,367.85 Hz (0.999) | 7,716.11 Hz (0.471) |

- The state core's −3 dB corner is at the Cutoff (Butterworth). The ladder's is at 0.434–0.436 × the Cutoff from 16 Hz to 4 kHz (four equal poles, each −3 dB at the Cutoff, so the stack is −12.04 dB there), rising to 0.47 at 16.4 kHz. The knob is a pole frequency in the ladder and a corner in the state core. This is why the cores are "matched" in self-oscillation pitch only (section 1.1).
- The corner measurements at 44.1, 96, 192, 384 and 768 kHz (1,024 Hz setting): state 1,022.8 Hz (0.999) at every rate; ladder 444.7 Hz (0.434) from 44.1 to 96 kHz and 444.5 Hz from 192 to 768 kHz [meas].
- Slope [test: `lowpass_24_and_12`]: 24 ± 3 dB per octave (ladder) and 12 ± 3 (state) between 4 and 8 kHz; passband within 0.5 dB.
- The open filter (knob 1, Res 0, low-pass) is not flat to Nyquist. Gain on a 1 V sine [meas]:

| Rate | Core | 0.1 fs | 0.2 fs | 0.3 fs | 0.4 fs | 0.45 fs | 0.49 fs |
|---|---|---|---|---|---|---|---|
| 48 kHz | state | −0.0 dB (4.8 kHz) | −0.4 (9.6 kHz) | −1.9 (14.4 kHz) | −5.1 (19.2 kHz) | −7.2 (21.6 kHz) | −9.8 (23.5 kHz) |
| 48 kHz | ladder | −1.2 | −4.5 | −9.6 | −15.9 | −19.5 | −23.3 |
| 44.1 kHz | state | −0.0 (4.4 kHz) | −0.3 (8.8 kHz) | −1.3 (13.2 kHz) | −3.9 (17.6 kHz) | −5.8 (19.8 kHz) | −8.3 (21.6 kHz) |
| 44.1 kHz | ladder | −1.0 | −3.8 | −8.1 | −13.8 | −17.1 | −20.5 |

### 5.2 Pitch of the self-oscillating filter: knob, V/OCT, FM, Spread, clamps

Method: Res at the onset + 0.006 (0.956) and at 1, knocked once by PING, pitch counted after a settle of 3 s (12 s below 40 Hz) [meas]. "Onset" is Res 0.956.

**Against the Cutoff knob, 48 kHz** (cents from `16 × 2^(10 × knob)`):

| Knob | Want | State, Res 1 | State, onset | Ladder, Res 1 | Ladder, onset |
|---|---|---|---|---|---|
| 0 | 16 Hz | −0.23 | 0.00 | −0.05 | +0.01 |
| 0.1 | 32 Hz | −0.20 | 0.00 | −0.06 | −0.02 |
| 0.2 | 64 Hz | −0.14 | +0.01 | −0.05 | −0.01 |
| 0.3 | 128 Hz | −0.02 | +0.03 | −0.05 | −0.01 |
| 0.4 | 256 Hz | +0.22 | +0.05 | −0.04 | −0.01 |
| 0.5 | 512 Hz | +0.70 | +0.11 | −0.02 | 0.00 |
| 0.6 | 1,024 Hz | +1.66 | +0.23 | 0.00 | +0.01 |
| 0.7 | 2,048 Hz | +3.58 | +0.46 | −0.02 | +0.02 |
| 0.8 | 4,096 Hz | +7.39 | +0.92 | −0.54 | −0.04 |
| 0.9 | 8,192 Hz | +14.49 | +1.76 | −4.50 | −0.59 |
| 0.95 | 11,585 Hz | +18.94 | +2.31 | −11.09 | −1.56 |
| 1 | 16,384 Hz | +19.53 | +2.47 | −9.56 | −1.34 |

- Worst by rate, 16 Hz to 4,096 Hz: 44.1 kHz state +8.06 (Res 1) and +1.00 (onset) cents, ladder −0.72 and −0.06; 48 kHz state +7.39 and +0.92, ladder −0.54 and −0.04; 96 kHz state +3.58 and +0.46, ladder −0.02 and +0.02. Above 4 kHz at 96 kHz: state +14.49 at 16.4 kHz, ladder −4.50.
- At 44.1 kHz above 4 kHz: state +19.78 cents (11.6 kHz), ladder −13.30 cents (11.6 kHz) at Res 1 [meas].
- The tests bound the tuning to 5 cents at the onset and 15 at Res 1 from 30 Hz to 4 kHz [test: `state_tuned`]. Printed: state core alone worst 0.81 (onset) and 7.87 cents (Res 1); engine, 55 Hz to 3.5 kHz at 44.1, 48 and 96 kHz, both cores, worst 0.86 and 6.91 cents [test: `self_oscillation_in_tune`].
- **The sharpening of the state core with cutoff, above 4 kHz, is not bounded by any test or doc.**

**V/OCT**, knob 0.6 (1,024 Hz), Res 1, 48 kHz [meas]:

| V/OCT | Want | State (cents) | Ladder (cents) |
|---|---|---|---|
| −7 V | 8 Hz (clamp) | −0.24 | −0.07 |
| −5 V | 32 Hz | −0.20 | −0.06 |
| −3 V | 128 Hz | −0.02 | −0.05 |
| −1 V | 512 Hz | +0.70 | −0.02 |
| 0 V | 1,024 Hz | +1.66 | 0.00 |
| +1 V | 2,048 Hz | +3.58 | −0.02 |
| +2 V | 4,096 Hz | +7.39 | −0.54 |
| +3 V | 8,192 Hz | +14.49 | −4.50 |
| +4 V | 16,384 Hz | +19.53 | −9.56 |
| +4.3 V and above | 20,000 Hz (clamp) | +13.35 (20,154.8 Hz) | −8.20 (19,905.5 Hz) |

The low clamp (8 Hz) and the high clamp (20 kHz at 48 kHz) both hold. The V/OCT result is the same as the knob's at the same octave count, as the law says.

**FM** (knob 0.6, Res 1, 48 kHz; the cents error equals the V/OCT error at the same final pitch): FM knob +1 with +1 V gives 2,048 Hz (state +3.58, ladder −0.02 cents); +1 with −1 V gives 512 Hz; +0.5 with +2 V gives 2,048 Hz; −1 with +1 V gives 512 Hz; −0.5 with +2 V gives 512 Hz; 0 with +3 V gives 1,024 Hz (no effect). FM +1 with +3 V gives 8,192 Hz. Knob and V/OCT equivalences were exact: 0 V output difference [meas: `m_cv`].

**Spread** (knob 0.6, Res 1, 48 kHz) [meas]:

| Spread | Left want / ladder / state (cents) | Right want / ladder / state (cents) |
|---|---|---|
| 0.25 | 939.0 Hz / 0.00 / +1.50 | 1,116.7 Hz / 0.00 / +1.84 |
| 0.5 | 861.1 Hz / 0.00 / +1.36 | 1,217.8 Hz / +0.01 / +2.02 |
| 1 | 724.1 Hz / −0.01 / +1.10 | 1,448.2 Hz / +0.01 / +2.46 |

**Sample rates** (220 Hz, Res 1 and onset; [meas: `m_rates`]): state −0.23 to +0.19 cents at Res 1 and ≤ 0.05 at the onset from 44.1 to 768 kHz; ladder −0.05 to −0.04 cents and ≤ 0.03. Level in section 5.5.

### 5.3 Mode: gains at the three lettered points and between

Res 0, 0.5 V sine, cutoff 1,024 Hz, gain in dB at 60 Hz | at the cutoff | at 12 kHz [meas]:

| Mode | Name | State | Ladder |
|---|---|---|---|
| 0 | LP | −0.00 \| −3.01 \| −43.7 | −0.06 \| −12.04 \| −87.4 |
| 0.25 | LP 50 % / BP 50 % | −6.0 \| −6.0 \| −27.8 | −6.2 \| −8.5 \| −37.8 |
| 0.5 | BP | −24.7 \| −3.01 \| −21.8 | −37.3 \| −0.00 \| −31.8 |
| 0.75 | BP 50 % / HP 50 % | −30.7 \| −6.0 \| −6.0 | −43.3 \| −8.5 \| −6.4 |
| 1 | HP | −49.3 \| −3.01 \| −0.00 | −89.9 \| −12.04 \| −0.11 |

- The in-between sounds differ between the cores, as the spec says.
- The ladder's low-pass and high-pass are 12 dB down at the cutoff; its band-pass is at unity at the cutoff (Fathom's BP row). The state core is 3 dB down at the cutoff in all three.
- **Mode's CV**: 10 V is the full travel; Mode CV 5 V equals knob 0.5, 10 V equals knob 1, −10 V equals knob 0 (0 V difference on noise, both cores) [meas: `m_cv`]. A step of Mode CV 0 → +10 V or +5 V on a 5 V 100 Hz sine moved the sample step by 1.1–1.2× the sine's own (0.065 V) [meas: `m_clicks`].
- Pure points are Fathom's own: the ladder at LP, BP and HP matches `rka::Ladder` within 0.3 dB at 150 Hz, 1,024 Hz and 6 kHz [test: `ladder_pure_points_are_fathoms`].

### 5.4 Res: the peak, and the passband

Low-pass, cutoff 1,024 Hz, 0.1 V sine (small signal, so the saturation is not engaged), 48 kHz, peak searched in 1/24-octave steps from 0.71 to 1.41 × the cutoff [meas]. † = the peak is at the edge of the grid (0.71 × the cutoff) and not a true peak.

| Res | k (ladder) | Passband at 60 Hz (state / ladder) | State: at cutoff, peak (at) | Ladder: at cutoff, peak (at) |
|---|---|---|---|---|
| 0 | 0 | 0.00 / −0.06 dB | −3.01, −0.98 † | −12.04, −7.04 † |
| 0.25 | 0.277 | 0.00 / −0.01 | −2.39, −0.48 † | −9.30, −4.08 † |
| 0.5 | 1.108 | +0.01 / +0.03 | −0.19, +1.11 † | −2.75, +2.65 † |
| 0.7 | 2.172 | +0.02 / +0.03 | +3.79, +4.27 (0.891 fc) | +4.78, +8.89 (0.841 fc) |
| 0.8 | 2.837 | +0.03 / +0.03 | +7.71, +7.89 (0.944 fc) | +10.34, +13.93 (0.917 fc) |
| 0.9 | 3.590 | +0.03 / +0.03 | +16.70, +16.70 (1.000 fc) | +20.33, +23.33 (0.972 fc) |
| 0.93 | 3.833 | +0.03 / +0.03 | +23.66, +23.66 (1.000 fc) | +25.14, +27.63 (0.972 fc) |
| 0.94 | 3.958 | +0.03 / +0.03 | +28.70, +28.70 (1.000 fc) | +27.27, +29.51 (0.972 fc) |

- **The passband stays at 0 dB** as Res rises, to 0.94, in both cores (within 0.06 dB at 60 Hz) [meas]. The test bound is 1 dB at Res 0.4, 0.7, 0.9 with a 0.5 V sine [test: `passband_holds_with_res`].
- The state core peaks at the cutoff once Res is high; the ladder's peak sits below the cutoff (0.972 × at Res 0.9 to 0.94) because its feedback is still under 4. That is why the ladder rings flat (section 7.2).
- Res with Mode at BP and HP, Res 0.9, 0.1 V sine at 1 kHz: state LP +16.5, BP +16.3, HP +16.1 dB; ladder LP +23.1, BP +22.4, HP +10.0 dB [meas]. The ladder's high-pass peak is 12 dB lower.
- **Res CV**: 10 V is full travel, added to the knob and held to 0..1 (`res + CV/10`). Res CV 5 V equals knob 0.5; 10 V equals knob 1; knob 0.5 + 10 V equals knob 1; knob 1 − 10 V equals knob 0 [meas: 0 V difference]. The CV is not smoothed.

### 5.5 Self-oscillation: onset, level, harmonics, start time

**Onset.** Smallest Res at which a knocked ring still holds after 6 s [meas, bisection, 14 steps]: state 0.9498 (100 Hz), 0.9500 (1 kHz), 0.9500 (4 kHz); ladder 0.9494, 0.9500, 0.9500. Both cores begin at Res 0.95, at the same place, as claimed. [test: `state_self_oscillates_at_the_onset`: ring dies just below, holds just above, in both cores.]

**Level against Res** (peak volts on OUT L, 1 kHz, 48 kHz) [meas]:

| Res | 0.951 | 0.955 | 0.96 | 0.97 | 0.98 | 0.99 | 1 |
|---|---|---|---|---|---|---|---|
| State | 0.574 V | 1.283 | 1.815 | 2.567 | 3.146 | 3.635 | 4.066 |
| Ladder | 0.774 V | 1.709 | 2.388 | 3.288 | 3.924 | 4.421 | 4.822 |

The sine grows from nothing at Res 0.95 to 4.07 V (state) and 4.82 V (ladder) at the top: "about 4 to 5 V", as the manual says. The last 5 % of the knob carries the whole level range.

**Level against pitch, Res 1** (peak V) [meas]:

| Pitch | 30 Hz | 110 Hz | 440 Hz | 880 Hz | 1.76 kHz | 3.52 kHz | 7.04 kHz | 12 kHz |
|---|---|---|---|---|---|---|---|---|
| State | 4.067 | 4.067 | 4.065 | 4.065 | 4.073 | 4.116 | 4.341 | 4.983 |
| Ladder | 4.917 | 4.909 | 4.878 | 4.837 | 4.759 | 4.621 | 4.430 | 4.338 |

**Level against sample rate** (Res 1, 220 Hz): state 4.066–4.067 V at 44.1, 48, 88.2, 96, 192, 384 and 768 kHz; ladder 4.897 V (44.1 kHz) to 4.919 V (768 kHz) [meas]. The cores differ by 1.6 dB at Res 1 (20·log10(4.899/4.066)); at Res 0.97 by 2.3 dB (3.35 V against 2.57 V). The test bound is 3 dB between the cores [test: `self_oscillation_in_tune`] and 4 to 9 V each.

**Harmonics.** The self-oscillation is not a pure sine: the 2nd to 6th harmonics together are −53.5 dB (state) and −50.4 dB (ladder) re the fundamental at Res 1, mostly the 3rd, the same at 30 Hz to 7 kHz (state −53.2 to −53.7, ladder −49.5 to −52.4); they fall to −87 dB (state) and −82 dB (ladder) at Res 0.951, and −58 / −54 dB at Res 0.98 [meas]. At 12 kHz the 2nd harmonic is above Nyquist (none listed).

**Start time from silence, with no knock** (time to half the final peak; the engine's 1e-6 noise starts it) [meas]:

| | 55 Hz | 220 Hz | 1 kHz | 4 kHz |
|---|---|---|---|---|
| State, Res 1 | 1.56 s | 0.37 s | 0.08 s | 0.02 s |
| Ladder, Res 1 | 1.27 s | 0.29 s | 0.07 s | 0.02 s |
| State, Res 0.96 | 6.84 s | 1.76 s | 0.35 s | 0.09 s |
| Ladder, Res 0.96 | 5.74 s | 1.25 s | 0.28 s | 0.07 s |

With a PING knock first, half level is reached in 0.01–0.02 s at 220 Hz to 4 kHz and in 0.036 s (state, Res 0.96) to 0.29 s (ladder, Res 0.96) at 55 Hz [meas]. The manual's "takes a moment to start, longer at low pitches ... a trigger at PING starts it at once" holds.

**Idle noise with nothing patched**: Res 0 to 0.94, any cutoff: RMS ≤ 4.8e-5 V (−86 dBV; state, 8 kHz, Res 0.94), mostly under 1e-5 V; DC ≤ 2e-8 V. At Res 0.95 exactly the filter is at the edge of singing: noise RMS reads up to 2.3 mV (state, 8.2 kHz) and 0.4 mV (ladder) over seconds 2 to 6 [meas: `m_floor`]; a ring knocked into it holds (onset above).

### 5.6 The core switch on a self-oscillating voice

[meas: `m_core`; test: `core_switch_no_click_no_gap`, `core_switch_holds_level`] Res 1, no input, after a PING knock and 4–8 s of settling; the switch flipped, OUT L envelope:

| Switch | Pitch | Lowest envelope in the first 100 ms | 100–200 ms | After 0.5 s | Largest sample step |
|---|---|---|---|---|---|
| state → ladder | 55 Hz | −1.0 dB | +2.2 dB | +1.6 dB | 0.046 V |
| state → ladder | 100 Hz | −2.8 | +1.7 | +1.6 | 0.082 |
| state → ladder | 220 Hz | −2.3 | +1.6 | +1.6 | 0.163 |
| state → ladder | 1 kHz | −3.0 | +1.5 | +1.5 | 0.669 |
| state → ladder | 4 kHz | −3.7 | +0.9 | +0.9 | 2.417 |
| ladder → state | 55 Hz | +0.5 | −0.2 | −1.6 | 0.057 |
| ladder → state | 100 Hz | −0.8 | −1.2 | −1.6 | 0.101 |
| ladder → state | 220 Hz | −1.5 | −1.6 | −1.6 | 0.206 |
| ladder → state | 1 kHz | −1.5 | −1.5 | −1.5 | 0.885 |
| ladder → state | 4 kHz | −0.9 | −0.9 | −0.9 | 3.522 |

- Within the spec's 3 dB at 100–200 ms from 100 Hz up; the dip in the first 100 ms reaches −3.0 dB (1 kHz) and −3.7 dB (4 kHz) from state to ladder. The final level differs from the old one by the cores' level difference.
- The sample steps at 1 kHz and 4 kHz are the oscillating sine's own steps (a 4 V sine at 4 kHz steps up to 2.1 V a sample); the test bound is 0.5 V at 220 Hz [test].
- With audio through (5 V 100 Hz sine) the largest step in the 200 ms after a core flip is 0.065–0.066 V, the sine's own [meas: `m_clicks`].
- Flipping the switch every 3 samples with Res 1 and ±5 V noise keeps |out| ≤ 10 V and finite [test]; flipping every sample to every tenth for 300,000 samples is safe [test: `switches_at_audio_rate`].

---

## 6. Level

### 6.1 Passband and unity

Res 0, low-pass: passband gain 0 dB within 0.1 dB in both cores (state −0.00, ladder −0.06 dB at 60 Hz) [meas]. A 1 V and a 2 V sine come out in proportion to 0.05 dB [test: `output_limit`]. Peak gain at the resonance is in section 5.4. The mono sum (R output unpatched): OUT L = (L + R)/2 to 1e-5 V [test: `normalling`].

### 6.2 The output limit and the worst case

[code, `NarrowsEngine.hpp:410-417`] Straight to ±8 V; above it `8 + 2·tanh((|x| − 8)/2)`, which approaches ±10 V and never reaches it (the ceiling reads 10.000 V in float for a huge overload).

Worst case [meas: `m_level`, 30 runs per core]: Res 1, Drive 1, Comb ±1, five cutoffs (0.2, 0.4, 0.6, 0.8, 0.95 knob), inputs of ±10 V noise, ±24 V noise and a 10 V sine at the cutoff, 10 s each: peak **9.54 V (state)** and **8.62 V (ladder)**; mean DC over the last second 0.004 V and 0.006 V. A 10 V sine at 500 Hz through the state core at Res 0.9, Drive 0.25 reaches 9.995 V [meas; the test asserts > 9 V as proof the limit works].
Tests: three-minute runs per core at the top never exceed 10 V and keep DC under 0.05 V [test: `minutes_at_the_top`, `bounded_everything_up`: 20 s of ±10 V noise, 20 s of 10 V DC, 20 s of silence, comb ±1]; the output is silent (< 0.01 V) 5 s after Res and Comb come down.

### 6.3 Comb level

[meas: `m_level`, the comb alone, period 100 samples; test `comb_level`] Noise comes out at its level (−0.14 to +0.04 dB) at every setting. The peaks stand above dry by sqrt((1 + |f|)/(1 − |f|)):

| Comb knob | ±0.25 | ±0.5 | ±0.75 | ±1 |
|---|---|---|---|---|
| Feedback | 0.4156 | 0.7125 | 0.8906 | 0.95 |
| Noise level | −0.01 / 0.00 dB | −0.01 / −0.02 | +0.03 / −0.10 | +0.04 / −0.14 |
| Peak over dry (right and left) | +3.84 dB | +7.75 | +12.38 | +15.91 |

A steady note on a peak is 16 dB louder at full Comb and can reach the output limit; the manual says so.

---

## 7. Ping

### 7.1 The pulse and the trigger

[code, `Ping.hpp`; meas: `m_ping`; test: `ping_one_per_edge`]

- A rising edge through **1 V** fires; the gate re-arms when it falls to **0.1 V** or below (`high` flag: a gate that stays between 0.1 V and 1 V does not re-arm). 0.5 V and 0.99 V do not fire; 1.00 V does. After 5 V → 0.5 V → 5 V no new pulse; after 5 V → 0.05 V → 5 V one new pulse [meas]. NaN holds the state.
- The pulse is `16 p² (1 − p)²` over 1 ms (zero and flat at both ends, 1 at the middle): peak 1.0 (scaled to 5 V), area 2.667 V·ms. Length: 48 samples at 48 kHz (1.000 ms), 44 at 44.1 (0.998 ms), 95 at 96 kHz (0.990 ms), 192 at 192 kHz, 768 at 768 kHz [meas]. Never negative, no step at the ends [test].
- **An edge that arrives while a pulse is running does not start a new one.** Pulses per second from a square gate, 48 kHz [meas]: 1, 10, 100, 500, 900 Hz give the same number; 1,000 Hz gives 500; 1,100 Hz gives 550; 2,000 Hz gives 667; 5,000 Hz gives 834. Gates above about 900 Hz therefore drop pulses. A gate held high gives one pulse [test].
- The pulse is added to **both sides** of the voice at 5 V, ahead of the comb and Drive (Drive gains it up to 24 dB). Through an open low-pass (knob 1, Res 0) the output peak is **4.93–4.97 V** at 44.1, 48, 96 and 192 kHz, as loud as a 5 V input [meas].
- A one-channel PING cable strikes every voice; a polyphonic one strikes each voice from its own channel. PING's channel count also counts toward the voice count [test: `ping_rings`, `cables_and_voices`].
- It needs no audio patched. With Mode at BP or HP and Res 0 the pulse's low-frequency energy is removed: peak 0.13 V (state BP), 0.012 V (state HP), 0.04 V (ladder BP), 0.002 V (ladder HP) [meas]; set Res or Comb up to hear it.

### 7.2 What the ping sounds like: ring pitch, level and decay

The pulse is 1 ms long, so its spectrum rolls off. Level re DC [meas, 192 kHz computation of the same pulse]:

| Hz | 100 | 440 | 880 | 1,760 | 3,520 | 7,040 | 14,080 |
|---|---|---|---|---|---|---|---|
| dB | −0.06 | −1.20 | −5.08 | −34.12 | −39.49 | −68.89 | −85.28 |

Pinged low-pass, Res 0.9, cutoff set by the knob, first peak on OUT L within 50 ms [meas]:

| Cutoff | 55 Hz | 110 Hz | 220 Hz | 440 Hz | 880 Hz | 1,760 Hz | 3,520 Hz | 7,040 Hz |
|---|---|---|---|---|---|---|---|---|
| State: first peak | 0.818 V | 1.620 | 2.702 | 4.036 | 3.547 | 0.315 | 0.076 | 0.003 |
| Ladder: first peak | 0.669 V | 1.328 | 2.587 | 4.042 | 4.508 | 0.842 | 0.254 | 0.022 |

- **Rings above about 1.5 kHz are weak**: against 880 Hz the first peak is 21 dB lower at 1.76 kHz and 33 dB lower at 3.5 kHz (state), and 15 and 25 dB lower (ladder), by the pulse's spectrum. The same applies to the comb's pluck: the comb (full right, band-pass) rings at the cutoff at 0.19–0.52 V from 55 to 880 Hz and at 0.007 V (state) and 0.011 V (ladder) at 1.76 kHz [meas: `m_comb`].
- **Ring pitch**: the ladder's ring at Res 0.9 is **47.5 to 52.6 cents flat** at 55 Hz to 3.5 kHz, 65 cents flat at 7 kHz; the state core's is 4.5–4.9 cents flat at 55–220 Hz. This is the response peak (section 5.4: 0.972 × the cutoff for the ladder at Res 0.9 to 0.94; 0.917 at Res 0.8; 0.841 at Res 0.7). At Res 0.94 a ring at 440 Hz measured −19 cents (ladder); the ring is on pitch from Res 0.95. The pitch tests allow 60 cents [test: `ping_rings`].
- **Ring decay**: time for the ring envelope to fall 60 dB from its peak [meas]:

| 440 Hz, LP | Res 0.7 | Res 0.8 | Res 0.9 | Res 0.94 |
|---|---|---|---|---|
| State | 9 ms | 14 ms | 34 ms | 154 ms |
| Ladder | 20 ms | 32 ms | 91 ms | 418 ms |

At Res 0.9 the ring time scales as 1/pitch: state 16/f seconds (0.29 s at 55 Hz, 0.145 s at 110, 0.073 s at 220, 0.017 s at 880), ladder 42/f seconds (0.76 s, 0.37, 0.19, 0.045). At Res 0 the ring is gone in 5 ms; at Res 0.5 in 7 ms (state) to 11 ms (ladder). The times are read from one-period envelope windows, so they are good to about one period. BP and HP rings fall at the same rate as LP's in the ladder (95 ms to −60 dB at 440 Hz, Res 0.9) and in the state core (34 ms) [meas].
- A knock on a self-oscillating voice brings it to half its final level in 10 to 20 ms at 220 Hz to 4 kHz (section 5.5); it holds 1.813 V (state) and 2.414 V (ladder) at Res 0.96 and 4.065 V and 4.878 V at Res 1 at 440 Hz.
- The ping is knocked ahead of Drive: ladder Res 0.9 first peak at 440 Hz is 4.04 V at Drive 0, 3.77 at 0.25, 2.88 at 0.5, 2.01 at 0.75, 1.36 at 1; state 4.04, 4.17, 3.62, 2.76, 1.93 V [meas].

### 7.3 The comb pluck

Ladder core, band-pass at the cutoff, Res 0, 330 Hz, PING [meas, 10 ms envelope]:

| Comb knob | Feedback | First peak | −20 dB | −40 dB | Ring pitch |
|---|---|---|---|---|---|
| +0.5 | +0.712 | 1.347 V | 0.03 s | 0.05 s | (decays too fast to count) |
| +0.75 | +0.891 | 1.057 V | 0.06 s | 0.13 s | 332.8 Hz |
| +0.9 | +0.940 | 0.829 V | 0.12 s | 0.23 s | 330.0 Hz |
| +1 | +0.950 | 0.768 V | 0.14 s | 0.28 s | 330.0 Hz |
| −0.5 | −0.712 | 1.088 V | 0.03 s | 0.05 s | (too fast) |
| −0.9 | −0.940 | 0.672 V | 0.12 s | 0.24 s | 496.2 Hz |
| −1 | −0.950 | 0.623 V | 0.14 s | 0.28 s | 496.2 Hz |

Left of noon the ring is on the odd multiples of half the cutoff (165 and 495 Hz); through a band-pass at 330 Hz it is the 495 Hz one [test: `comb_in_the_engine`].

**Comb pitch against cutoff** (pluck, Comb full right, band-pass, Res 0; ring pitch 20 ms to 220 ms after the knock) [meas: `m_comb`]: 55 to 880 Hz within 0.3 cents at 44.1, 48 and 96 kHz in both cores; 20 Hz −1.5 cents (state) and +7.6 cents (ladder). From 1.76 kHz the ring is too weak to count (above).

---

## 8. Comb, in the engine

[code, `Comb.hpp`; test: `test_parts`, `comb_in_the_engine`]

- The input gain `sqrt(1 − f²)` is recomputed only when the feedback moves. A NaN or a value under 1e-18 in the loop is flushed to 0. Reads before the first written sample after a reset return silence (the `written` counter), so a voice joining finds an empty line at no cost.
- Peaks: at feedback +f, at the cutoff frequency and its harmonics; at −f, halfway between (odd multiples of half the cutoff), an octave lower for the lowest peak. Positive peak found at 480 Hz ± 4.8 Hz for a 100-sample period, negative at 240 Hz ± 2.4 [test: `comb_peaks`].
- Bound: feedback ±0.95 means peak gain at most sqrt(1.95/0.05) = 6.24 (15.9 dB); with ±2.4 V noise and then 2.4 V DC at feedback ±0.95 the peak stays under 2.4 × 6.3 V, and exact silence returns afterwards [test: `comb_bounded`].
- A jump of the period (a stepped V/OCT or a Spread move) slides: with a 0.5 V 2 kHz sine at feedback 0.8 and the period jumping from 100 to 61 samples, the largest step in the next 200 samples is under 0.35 V (the sine's own steps before are over 0.01 V) and the period reaches 50 within 0.01 sample after a slide [test: `comb_slides`].
- Delay range: 2 samples to the line length; at 8 Hz the period is rate/8 within 1 sample at 44.1, 48, 192 and 768 kHz [test: `comb_range`; also NaN, infinity, 0, 1, −5 and 1e9 samples asked for stay finite].
- Related: `DelayLine::read` loses fractional precision at long delays [issue #61]. The comb reads up to 6,000 samples (125 ms at 8 Hz, 48 kHz); the effect on Narrows was not measured [unverified].

---

## 9. Drive

### 9.1 Gain in the passband

[meas: `m_level`] A 100 Hz sine in the passband (cutoff 1,024 Hz), Res 0, gain re the input sine (dB) and total harmonic distortion:

| Drive | State 5 V | State 2 V | State 0.5 V | Ladder 5 V | Ladder 2 V | Ladder 0.5 V |
|---|---|---|---|---|---|---|
| 0 | −0.09 (0.3 %) | −0.01 | 0.00 | −0.19 (0.3 %) | −0.17 | −0.16 |
| 0.2 | +1.58 (1.0 %) | +1.79 | +1.83 | +1.60 (0.9 %) | +1.65 | +1.66 |
| 0.4 | +2.93 (2.8 %) | +3.53 | +3.65 | +3.29 (2.8 %) | +3.46 | +3.49 |
| 0.6 | +3.57 (7.1 %) | +5.12 | +5.47 | +4.68 (8.4 %) | +5.23 | +5.32 |
| 0.8 | +3.01 (14.9 %) | +6.28 | +7.25 | +5.06 (19.8 %) | +6.85 | +7.14 |
| 1 | +1.25 (24.4 %) | +6.55 | +8.93 | +3.56 (27.0 %) | +8.02 | +8.93 |

- At Drive 0 the filter is clean at 5 V: gain within 0.2 dB of the small-signal gain and 0.3 % distortion (limits 1 dB and 3 %) [test: `drive`, `state_clean_at_five_volts`].
- The 5 V sine stays within 6 dB of its Drive 0 level: the largest rise over the Drive 0 level is +3.66 dB (state, Drive 0.6) and +5.27 dB (ladder, Drive 0.7). **At 2 V the rise at full Drive is +6.6 dB (state) and +8.2 dB (ladder); at 0.5 V it is +8.9 and +9.1 dB.** The spec's 6 dB is stated for 5 V.
- At full Drive distortion is 24.4 % (state) and 27.0 % (ladder) of the 5 V sine; the test requires > 15 %.

### 9.2 Drive and clicks

A Drive knob step 0 → 1 on the 5 V 100 Hz sine moved the sample step to 0.182 V (state, 2.8× the sine's own) and 0.370 V (ladder, 5.8×) over the 5 ms smoothing [meas: `m_clicks`]. It is a gain move through a saturating stage, not a click.

### 9.3 Drive and self-oscillation, ping

The make-up multiplies the output of the core, so a self-oscillating sine, which does not pass through the Drive gain, falls with Drive [meas: `m_drive_sine`], 440 Hz, Res 1:

| Drive | 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| Make-up | 1.000 | 0.651 | 0.423 | 0.275 | 0.179 |
| State sine | 4.065 V | 2.645 | 1.721 | 1.120 | 0.729 |
| Ladder sine | 4.878 V | 3.174 | 2.065 | 1.344 | 0.874 |

Full Drive lowers a sine voice by 15.0 dB (ladder) and 14.9 dB (state). The manual's patch "A sine voice. Res 100 %" does not use Drive.

---

## 10. Voices and polyphony

[code, `NarrowsEngine.hpp:103-113`, `186-225`, `356-363`; test: `voices_independent`, `cables_and_voices`, `voice_count_change_unbroken`, `channel_churn`]

- **Voices** = the largest channel count of IN L, IN R, V/OCT and PING, at least 1 and at most 16. The channel counts of FM, Res CV, Comb CV and Mode CV do not add voices: a CV with more channels than voices is ignored past the last voice.
- **A one-channel cable feeds every voice; a polyphonic cable gives each voice its own channel; a voice past a polyphonic cable's last channel reads 0 V.** For audio that is silence to that voice. Channel c of IN L pairs with channel c of IN R, V/OCT and each CV.
- With nothing patched at all there is one voice; the outputs are one channel each (the filter can self-oscillate or be pinged).
- **Outputs**: one channel per voice on both L and R (the module sets the channels on both even if R is not patched). With R unpatched, L is `(L + R)/2` of each voice.
- A voice added starts from rest (filters, halfbands, comb, Ping and Mode state; its noise stream is reseeded when the engine resets, not when the voice joins); a voice removed stops. A voice's output is the same alone as among 16 [test: 0 of 48,000 samples differ, with different neighbours]. Voices 0 and 1 are unbroken when the count drops 4 → 2 → 4; a returning voice starts from rest (under 1e-3 V with silence in) [test].
- The count of voices is read every sample; a test with 0..17 channels on every port changing every sample for 200,000 samples reported the right count each time and no bad output [test: `channel_churn`].
- Passes of two voices: 1 or 2 voices are one pass, 3 or 4 two, and so on. Cost against the number of voices (ladder, ±5 V noise, Res 0.5, 48 kHz, best of three 4 s runs, loaded Mac) [meas: `m_voices`]:

| Voices | 1 | 2 | 3 | 4 | 5 | 8 | 9 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|
| Ladder, % of a core | 0.408 | 0.410 | 0.529 | 0.587 | 0.752 | 1.022 | 1.236 | 2.066 | 2.083 |
| State, % of a core | 0.290 | 0.297 | 0.397 | 0.453 | 0.579 | 0.836 | 0.954 | 1.530 | 1.549 |

One and two voices cost the same, as the spec says. An odd voice's partner lanes carry silence through an open, quiet filter [code, `NarrowsEngine.hpp:226-235`].

---

## 11. Steps and clicks

[meas: `m_clicks`] A 5 V 100 Hz sine, cutoff 1 kHz, one control changed at once; the largest sample-to-sample step in the next 200 ms against the 0.064–0.065 V step of the sine itself:

| Change | State | Ladder |
|---|---|---|
| Comb CV 0 → +5 V | 0.109 V (1.7×) | 0.061 V (0.9×) |
| Comb CV 0 → −5 V | 0.205 V (3.1×) | 0.119 V (1.9×) |
| Comb CV 0 → +1 V | 0.082 V (1.3×) | 0.081 V (1.3×) |
| Comb knob 0 → 1 | 0.065 V (1.0×) | 0.061 V (1.0×) |
| Res CV 0 → +9 V (Res 0.9) | 0.139 V (2.1×) | 0.223 V (3.5×) |
| Res knob 0 → 0.9 | 0.067 V (1.0×) | 0.069 V (1.1×) |
| Res CV 0 → +10 V (starts oscillating) | 0.592 V (9.0×) | 0.693 V (10.8×) |
| V/OCT 0 → +1 V | 0.131 V (2.0×) | 0.127 V (2.0×) |
| V/OCT 0 → −1 V | 0.065 V (1.0×) | 0.061 V (0.9×) |
| V/OCT 0 → +3 V | 0.396 V (6.1×) | 0.495 V (7.7×) |
| FM knob 1, FM CV 0 → +2 V | 0.235 V (3.6×) | 0.261 V (4.1×) |
| Cutoff knob 0.6 → 0.9 | 0.071 V (1.1×) | 0.077 V (1.2×) |
| Drive knob 0 → 1 | 0.182 V (2.8×) | 0.370 V (5.8×) |
| Spread knob 0 → 1 | 0.065 V (1.0×) | 0.063 V (1.0×) |
| Mode CV 0 → +10 V | 0.077 V (1.2×) | 0.079 V (1.2×) |
| Mode CV 0 → +5 V | 0.071 V (1.1×) | 0.069 V (1.1×) |
| Core switch | 0.065 V (1.0×) | 0.066 V (1.0×) |

- The smoothed knobs, Mode and the core switch move by no more than the sine's own step. Stepped jacks (V/OCT, FM, Res CV, Comb CV) are by design unsmoothed (spec), and move more, most of all when they open a resonance (Res CV to 10 V) or jump the cutoff three octaves (V/OCT +3 V: 6 to 8×).
- The rows with a Res step include the filter's own ring.

---

## 12. CPU, sample rates and robustness

### 12.1 No silence sleep

The engine does not sleep. With nothing patched it still runs one voice; the idle and outputs bench cases are one stereo ladder voice at the module's defaults [code: `bench.inc`].

### 12.2 Bench figures and budgets

`bench` cases [code, `bench.inc`], each at 48 kHz: **idle** (defaults, nothing patched), **outputs** (one stereo voice through the ladder, ±5 V noise into both inputs, Res 0.5), **state** (the same through the state core), **busy** (16 voices, ladder, Res 0.9, Drive 0.5, FM 0.3, Comb 0.5, Spread 0.5, V/OCT, FM, Mode and Comb CVs moving on every channel).

| Case | Budget Apple arm64 | Budget other machines (provisional) | TESTING.md measured | Mine, 1 instance / 16 instances (best of 3, 4 s) |
|---|---|---|---|---|
| idle | 0.7 | 2.1 | 0.33 | 0.374 / 0.374 |
| outputs | 0.7 | 2.1 | 0.33 | 0.373 / 0.382 |
| state | 0.5 | 1.5 | 0.24 | 0.269 / 0.269 |
| busy | 4.9 | 14.7 | 2.43 (at 16) | 2.435 / 2.510 |

- Units: % of one core per instance. My run: Apple M-series Mac, load average 6–7 from other work, so my figures are 3 to 13 % above TESTING.md's. In the full `make bench` run (done at the same time as my other programs, so slower still) Narrows read idle 0.437, outputs 0.449, state 0.326, busy 3.226 at one instance and passed every budget and the 16 vs 1 check (×1.00 to ×1.05). That whole run failed other modules' budgets (Eddy busy, Fog quiet at 16), which I put down to the load [unverified].
- The spec's expectations (about 0.3 % for one or two voices, about 2 % for 16) are a little under what the Mac now measures; the budgets (2× the Mac's, rounded) are met by a wide margin. **Windows CPU is covered by no automated budget** and the x86-64 budgets (3× the Mac's) are provisional.

Per-sample cost against the sample rate (3 s of ±5 V noise, % of one core) [meas: `m_rates`, loaded Mac]:

| Rate | Ladder, 1 voice | Ladder, 16 voices (busy-style) | State, 1 voice | State, 16 voices |
|---|---|---|---|---|
| 44.1 kHz | 0.351 | 2.277 | 0.252 | 1.882 |
| 48 kHz | 0.381 | 2.466 | 0.270 | 2.025 |
| 96 kHz | 0.761 | 5.002 | 0.549 | 4.108 |
| 192 kHz | 1.550 | 10.144 | 1.091 | 8.308 |
| 384 kHz | 3.169 | 20.201 | 2.182 | 16.501 |
| 768 kHz | 6.110 | 41.432 | 4.484 | 34.030 |

**The cost is proportional to the sample rate**: unlike Fog, Narrows has no rate divider and the cores run at twice the host rate. Sixteen voices at 192 kHz cost 10 % of a core on the Mac.

### 12.3 Sample-rate handling

[code, `NarrowsEngine.hpp:68-75`, `328-339`; test: `extreme_rates`]

- Accepted: any finite rate with fs > 0; no upper limit. A zero, negative or NaN rate is ignored (the engine keeps its last rate). The hostile test runs 1, 10, 1,000, 8,000, 44,100, 192,000 and 768,000 Hz in both cores with 3 voices of full-range noise, full-range FM and V/OCT, PING at audio rate, the cutoff knob swinging between its ends and a rate doubling mid-run: every output finite and within 10 V.
- A rate change reallocates the 32 comb lines and resets the engine (all voices to rest, tails gone).
- At 44.1 kHz the cutoff ceiling is 19,845 Hz (0.45 × 44,100) and at 48 kHz 20,000 Hz.
- Response, pitch and level at 44.1 to 768 kHz in section 5; aliasing in section 13.

### 12.4 Memory

32 comb lines, each a power of two of at least rate/8 + 12 floats [meas by arithmetic of `DelayLine::allocate`]: **1.00 MB at 44.1 and 48 kHz, 2.00 MB at 88.2 and 96 kHz, 4.00 MB at 192 kHz, 8.00 MB at 384 kHz, 16.00 MB at 768 kHz.** The engine object is 5,568 bytes besides these [meas: `sizeof`]. All buffers are allocated only in `setSampleRate()`, never in `process()` [test: `no_allocation_in_process`, 0 allocations over 100,000 samples with the voice count, core, Comb and a reset changing].

### 12.5 Hostile input and numerical safety

- **NaN, ±infinity, 1e30, −1e30 and 1e-40** on every port and knob at once (16 channels, 60,000 samples per core, random values on a quarter of the samples): every output channel finite and within ±10 V; with clean settings and silence after, the output falls below 0.01 V [test: `hostile_values`].
- Every port reads NaN and ±infinity as 0 V, bit for bit the same output as a 0 V port, on all eight ports [test: `bad_volts_read_zero`]. An input of ±1000 V equals ±24 V.
- A NaN that reached a pass would reset that pass's four lanes (two voices) [code, `NarrowsEngine.hpp:296-302`]; it cannot come from the ports.
- **Denormals**: the comb flushes below 1e-18; the ladder, state core and halfbands have no flush [code]. Three minutes at the top (noise, DC, silence) show silence taking under 3 × the time of noise [test: `minutes_at_the_top`], a proxy for no subnormal slowdown on arm64 (which does not flush by default). On x86-64 Windows, Rack sets flush-to-zero on its engine threads [unverified].
- **Fuzz**: twelve seeds, 40,000 samples each, random knobs, CVs (±12 V), channel counts (0–16), rates (8,000, 44,100, 48,000, 96,000, 384,000 Hz), resets, rate changes and R-output patching: no bad sample [test: `fuzz`].
- **Sanitizers**: `test_hostile` built with ASan and UBSan passed. The other four test programs were built with the plugin's flags and no sanitizers.
- **No writable global state** (`make engine-globals`): ran, passed [test]. **`make bench-modules`** (`bench --check`): 15 module declarations complete [test]. `make libm-calls` is Linux only: not run [issue #36].

---

## 13. Aliasing

[meas: `m_alias`, using `rkatest::AliasMeter` from `src/testing_alias.hpp`] TESTING.md rule 16 and `docs/DSP-ALIASING.md` ask each module to measure aliasing against a bound in dB; **Narrows has no such test and no bound [issue #67]**. These are the first numbers.

Where aliasing can come from, by the three kinds in `docs/DSP-ALIASING.md` [code]:

1. Generated discontinuities: the PING pulse is a smooth hump, zero and flat at both ends; no oscillator edges. Nothing to alias.
2. Nonlinearities: the soft clips (the ladder's four stage tanh, the input tanh, the state core's input tanh and resonance clip, the 2·tanh soft limit) all run **inside the 2× oversampled loop**. Drive's gain sits before the upsampler and is linear; the output soft limit (8 V knee) acts after the downsampler on the base-rate signal and only on peaks above 8 V. Oversampling is 2×, the minimum "for soft saturation" in the doc; DSP-ALIASING says 4× for hard drive.
3. Variable-rate reads: the comb's delay is read with a Hermite interpolator and its period moves through a 3 ms one-pole (about 53 Hz bandwidth), so audio-rate V/OCT or FM reaches the comb only as slow movement; the filter's cutoff is recomputed every sample and is not band-limited.

**A. A tone through the open low-pass** (cutoff knob 1.0, Res 0), strongest component that is not a harmonic of the tone, 48 kHz, dB re the output fundamental, 65,536-sample window after a 1 s settle [meas]:

| Core, level, Drive | 4.8 kHz | 9.6 kHz | 14.4 kHz | 19.2 kHz |
|---|---|---|---|---|
| State, 5 V, 0 | −106.6 | −126.4 | −111.9 | −96.4 |
| State, 10 V, 0 | −45.9 | −48.2 | −88.2 | −72.7 |
| State, 5 V, 0.5 | −61.3 | −103.6 | −65.6 | −50.1 |
| State, 5 V, 1 | −33.0 | −41.0 | −29.9 | −17.6 |
| State, 10 V, 1 | −28.2 | −26.7 | −20.0 | −11.6 |
| Ladder, 5 V, 0 | −92.9 | −133.6 | −114.9 | −85.6 |
| Ladder, 10 V, 0 | −70.3 | −127.3 | −93.5 | −61.9 |
| Ladder, 5 V, 0.5 | −50.1 | −112.2 | −71.3 | −39.1 |
| Ladder, 5 V, 1 | −33.6 | −41.9 | −22.5 | −5.9 |
| Ladder, 10 V, 1 | −32.8 | −27.0 | −12.3 | +0.5 |

- The tone frequencies are `fs × (0.1, 0.2, 0.3, 0.4) + 13.7 Hz` (4,814, 9,614, 14,414, 19,214 Hz). Re a 5 V input tone the ladder's 19.2 kHz rows are 15 dB lower (the filter has already attenuated that tone): −101.6, −51.6 and −20.8 dB at Drive 0, 0.5 and 1 (5 V).
- The strongest components are folds of the 5th to 7th harmonic: at 4.8 kHz the 5th lands at 23.9 kHz; at 19.2 kHz the 5th harmonic is 96,070 Hz, 70 Hz above the oversampled rate, and lands at 69 Hz; at 14.4 kHz the 7th is 100,898 Hz and lands at 4.9 kHz. They are made inside the 2× loop and cannot be removed by the downsampler.
- **At Drive 0 and 5 V (an ordinary signal) every alias is at least 85 dB under the fundamental.** At 10 V (state core's input tanh) the worst is −45.8 dB. At Drive 0.5 it is −39 dB at worst (ladder, 19.2 kHz) and −50 to −61 dB at 4.8 kHz. **At Drive 1 it is −33 dB at 4.8 kHz and −18 to −6 dB at 19 kHz**, above the 50 dB target in `docs/DSP-ALIASING.md` for a tone at 0.1 fs or higher.
- At 44.1 kHz the same table moves by under 2.1 dB (Drive 0.5 and 1, 5 V) and under 5 dB at Drive 0 and 10 V.

**B. The self-oscillating sine** (Res 1, no input; its own harmonics expected), strongest other component, dB re the sine [meas]:

| Core | ~1 kHz | ~2.4 kHz | ~4.8 kHz | ~9.6 kHz | ~14.4 kHz | ~16.5 kHz |
|---|---|---|---|---|---|---|
| State, 48 kHz | −111.1 | −104.9 | −102.5 | −94.0 | −71.4 | −52.5 |
| Ladder, 48 kHz | −117.4 | −114.3 | −113.0 | −87.2 | −77.4 | −41.3 |
| State, 44.1 kHz | −113.0 | −105.7 | −98.6 | −93.4 | −71.4 | −55.9 |
| Ladder, 44.1 kHz | −131.2 | −114.9 | −111.3 | −87.2 | −77.4 | −51.2 |

The sine voice is clean to about 10 kHz; the worst figures are for sines at 14 to 16.5 kHz, where the 2nd harmonic is above Nyquist.

**C. Audio-rate FM on V/OCT** (a 497 Hz sine of ±0.25 V on a 2 kHz self-oscillation): components off the sideband comb were 55–57 dB under the strongest sideband (48 kHz, both cores). This is not a clean alias figure (the sine's own harmonics and intermodulation are in it); at ±1 V the carrier's spectrum was too wide to place. FM is not band-limited by design (spec: the cutoff is worked out every sample).

---

## 14. What the automated suite covers

All five test programs passed on the 2.0.39 sources in my run. They are Rack-free and drive `rka::NarrowsEngine` (or its parts) directly at 48 kHz unless stated. Times: laws 0.2 s, parts 0.2 s, state 0.6 s, engine 2.5 s, hostile 3.9 s. TESTING.md requires each test to be shown to fail by a mutation; I did not repeat that.

### 14.1 test_laws (control laws)

| Test | Guarantees | Tolerance |
|---|---|---|
| cutoff_law | 16 Hz at 0, 1,024 Hz at 0.6, 16,384 Hz at 1; knob ↔ Hz round trip; `hzFromOctaves` | 1e-4 relative; 1e-5 |
| octaves_law | knob alone; V/OCT 1 V per octave; FM volts × knob; Spread ± half an octave | exact |
| clamp_law | floor 8 Hz, ceiling 20 kHz, 0.45 × rate at 44.1 kHz; NaN and infinities; a 10 Hz rate | exact |
| amounts | ladder feedback 0, 4 at 0.95, 4.6 at 1; Drive 16× at full; Comb ±0.95, 0 at noon, odd, monotonic, 0.5 → 0.6..0.8 | 1e-6 |
| mode_laws | pure points; blends; the five ladder weights at the three rows and halfway; LP weight | exact |
| names | tooltip names; typed Hz parse and rejects | exact |

### 14.2 test_parts (comb and ping)

| Test | Guarantees | Tolerance |
|---|---|---|
| comb_exact_at_zero | feedback 0 returns the input to the bit; the line is still written | exact; 1e-4 |
| comb_reset_forgets | a reset costs nothing and silence in gives silence out | exact |
| comb_fresh_equals_run_in | a reset comb equals one run on silence | 1e-4 |
| comb_peaks | peaks at the period's harmonics (+) and halfway (−), 20 dB over the troughs; fractional period | within 1 % |
| comb_level | noise within 0.5 dB; peaks sqrt((1+f)/(1−f)) | 0.5 dB |
| comb_bounded | ±2.4 V noise then 2.4 V DC at feedback ±0.95: peak under 2.4 × 6.3; returns to exact silence | exact |
| comb_slides | a 2 kHz 0.5 V sine, period 100 → 61: largest step < 0.35 V; the period arrives | 0.01 sample |
| comb_range | NaN, inf, 0, 1, −5, 1e9 asked for stay finite; floor 2 samples; 8 Hz at 44.1, 48, 192, 768 kHz | 1 sample |
| ping_one_per_edge | one pulse per edge, a held gate one pulse, peak 1 ± 0.02, length 1 ms ± 0.2 ms at 44.1–768 kHz, no step at the ends, 1 V fires, 0.1 V re-arms, audio-rate gates bounded, an edge inside a pulse does not restart it | as listed |

### 14.3 test_state (the state core, run at 2× in the halfband pair)

| Test | Guarantees | Tolerance |
|---|---|---|
| state_lowpass_shape, state_highpass_mirrors | passband, −3 dB corner, 12 dB per octave | 0.5 dB, 1 dB, 1.5 dB |
| state_bandpass_peaks_at_cutoff | peak at 1 kHz; 6 dB per octave skirts | ±20 Hz; 1 and 1.5 dB |
| state_passband_holds_with_res | passband at Res 0.4, 0.7, 0.9 | 1 dB |
| state_res_law | gain at the cutoff at half the onset follows the damping law | 0.5 dB |
| state_self_oscillates_at_the_onset | sings from silence at Res 1; dies just below the onset, holds just above; the ladder's onset is in the same place | peak limits |
| state_tuned | onset within 5 cents and Res 1 within 15 cents, 30 Hz to 4 kHz, 44.1/48/96 kHz | printed worst 0.81 and 7.87 cents |
| state_level_matches_ladder | 110, 440 and 1,760 Hz at Res 1: within 3 dB of the ladder; each 4 to 9 V | |
| state_clean_at_five_volts | a 5 V sine within 1 dB of small-signal, distortion < 3 %; a 3.0-unit (84 V) sine held back by more than 6 dB | |
| state_four_wide_equals_one_wide | each `Float4` lane equals the one-float core | 1e-5 (worst 0) |
| state_bounded | Res 1, 4-unit noise plus DC for 5 s stays under 2.6 units (73 V) | |

### 14.4 test_engine (the full engine)

| Test | Guarantees | Tolerance |
|---|---|---|
| lowpass_24_and_12 | passband, 24 / 12 dB per octave | 0.5 dB; ±3 dB |
| passband_holds_with_res | Res 0.4, 0.7, 0.9, 0.5 V sine at 60 Hz | 1 dB |
| ladder_pure_points_are_fathoms | LP, BP, HP equal Fathom's `Ladder` | 0.3 dB |
| state_pure_points | BP peak 15 dB over three octaves away; HP passband and stop | 0.7 dB; −30 dB |
| mode_continuous | Mode CV step, both cores | step < 0.15 V; became BP |
| self_oscillation_in_tune | 3 rates × 2 cores × V/OCT −2..+4 V, onset and Res 1; peak 4 to 9 V; cores within 3 dB | 5 / 15 cents; printed 0.86 / 6.91 cents, 4.07 V state, 4.90 V ladder |
| voct_at_audio_rate | 200 Hz sine on V/OCT swings the period | ratio > 1.6 |
| spread | Spread 0: L = R to the bit with everything engaged; Spread 1: −0.5 and +0.5 octave | 0 differing; 6 cents |
| core_switch_no_click_no_gap | step < 0.5 V with audio; self-oscillation within 6 dB 100 ms after the flip; flipped every 3 samples bounded | as listed |
| core_switch_holds_level | the fade's peak ≤ 1.12 × steady, sine and DC, both ways | 1.12 |
| comb_opens_at_pitch | a comb opened by CV equals one open all along | 1e-3 × peak |
| drive | Drive 0 clean (1 dB, 3 %); within 6 dB of Drive 0 for 5 V across the travel; dirty at full (> 15 %) | |
| ping_rings | rings at the cutoff (within 60 cents), silent before and after; polyphonic and one-channel PING | 0.01 V |
| comb_in_the_engine | plucked comb rings at the cutoff (25 cents); 20× louder after 50 ms than without; negative on 165 or 495 Hz; Comb CV +5 V equals the knob | |
| voices_independent | voice 5 the same with different neighbours, 16 voices | 0 differing |
| cables_and_voices | mono audio into 4 V/OCT voices; 2-channel audio with 4 voices; mono and polyphonic Res CV | |
| voice_count_change_unbroken | count 4 → 2 → 4 | 0 differing; 1e-3 V |
| normalling | IN R alone feeds both; L alone is the average | 1e-5 V |
| bad_volts_read_zero | NaN/inf on each of 8 ports equals 0 V; 1,000 V equals 24 V | 0 differing |
| bounded_everything_up | Res, Drive, Comb at top, 3 × 20 s; peak ≤ 10 V; DC < 0.05 V; silent after | |
| output_limit | peaks ≤ 10 V; the limit is at work (> 9 V, state); straight below the knee | 0.05 dB |
| reset_and_rate_change_rest | reset and a rate change silence the voice | 1e-3 V |

### 14.5 test_hostile (ASan and UBSan, `-O1`)

`hostile_values`, `extreme_rates`, `channel_churn`, `minutes_at_the_top`, `switches_at_audio_rate`, `fuzz` (twelve seeds), `no_allocation_in_process`: described in section 12.

### 14.6 Not covered by the automated suite

- Anything in Rack itself: the panel, the tooltips and typed values, the Mode knob's half turn, saving and loading through Rack, bypass, Rack's channel-count behaviour and the order of events on a rate change, Rack's CPU meter. TESTING.md's smoke screenshot is "Yes"; I did not run `make smoke`.
- **Aliasing** [issue #67] and the harmonic content of the sine.
- Windows and Linux builds, CPU on x86-64 or Windows, `libm-calls` on macOS [issue #36].
- Tuning above 4 kHz, the pitch of a ping or a pluck, the ring's decay, the ping's spectrum, the dropped ping edges, the Drive level at under 5 V, the self-oscillation level against Drive.
- Sample rates other than 44.1, 48 and 96 kHz for tuning and the corner.
- Listening: whether the in-between Modes, the comb curve and the Drive range sound right (the spec's "Listening stop" and the listening guide).
- Golden saved-state image: none (params only).

---

## 15. Open issues

### 15.1 Filed issues that touch Narrows

| # | Title | QA impact |
|---|---|---|
| 67 | Aliasing: measure every module that makes or bends sound, and fix what fails | Names Narrows ("Drive before the upsampler and the comb read are unchecked"). Section 13 has the first measurements; add the data to the issue. |
| 64 | Polyphony: modules should be polyphonic where it makes sense | Lists Narrows as "planned"; it shipped polyphonic. Wording out of date. |
| 61 | DelayLine: fractional reads lose precision at long delays | The comb reads that line up to 125 ms (8 Hz); effect on Narrows not measured. |
| 36 | Process: libm-calls check is skipped on macOS | Narrows' no-libm-per-sample check was not run here. |

### 15.2 Found while preparing this document (not filed)

None of these has an issue yet. They are listed for the owner to file or rule on.

1. **The tuned ping is flat in the ladder below Res 95 %** (49 cents at Res 0.9; 19 cents at 0.94). The test tolerates 60 cents. The manual's patch 3 sets Res 90 %.
2. **Rings above about 1.5 kHz are weak** because the 1 ms pulse has a spectral null near 1.8 kHz (section 7.2); the manual does not say so.
3. **A gate above about 900 Hz on PING drops pulses** (an edge during a running pulse is ignored); 1 kHz gives 500 pulses a second.
4. **Drive lowers a self-oscillating sine by up to 15 dB** (and the first peak of a Res 0.9 ping by up to 9.5 dB, ladder); the manual's "turned back down to match" is true for passing audio only.
5. **Pitch above 4 kHz drifts** (state sharp to +19.5 cents, ladder flat to −11 cents at Res 1, 48 kHz); the tests and spec stop at 4 kHz.
6. **Manual wording**: "State: a sharper resonant peak" is not what the engine does (the ladder peaks higher and rings longer); "matched" does not say the −3 dB corner differs (state at the cutoff, ladder at 0.435 ×); the 0.45 × rate clamp is not mentioned; the cutoff default is 1,024 Hz, not 1 kHz.
7. **Spec body not updated** for the as-built values (Res 4.1 r² and 99 %, 10 V scale, 6 dB comb, 4–9 V sine); only a note at the top.
8. **Drive's 6 dB promise is for 5 V**; at 0.5 V full Drive is 8.9 to 9.1 dB louder than Drive 0.
9. The module's param enum is tied to the engine's by a `static_assert` on Cutoff, Mode and the count; nothing pins the absolute id numbers of the params, inputs and outputs (18 ids), unlike Fog's. Saved patches depend on the enum order staying put.
10. **Aliasing at Drive 1**: aliases are 33 dB under a 4.8 kHz 5 V tone (−18 to −6 dB at 19 kHz), against a 50 dB target. Not new to #67; the numbers are.

### 15.3 Known limits that are not defects

- The ladder's low-pass is 12 dB down at the cutoff; the state core's is 3 dB down (section 5.1).
- Stepping V/OCT, FM, Res CV or Comb CV is not smoothed (section 11).
- Every voice runs always, and the filter costs the same with nothing patched.
- From silence a low sine takes seconds to start without a PING (section 5.5).

---

## 16. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, Cutoff 0.6, all other knobs at their defaults (Core Ladder, Mode LP, Res 0, Drive 0, Comb noon, Spread 0, FM 0), nothing patched but the test signal.

**Method notes.**
- A scope or a tuner at OUT L, and a sine or white noise from a signal source. For pitch, use a long ring (Res ≥ 0.95) or a sustained self-oscillation; read the frequency counter or a spectrum.
- A ping rings for tens of milliseconds at Res under 0.9; look for the first 100 ms on a scope.
- Set Core to compare the two; remember the ladder's −3 dB corner is at 0.435 × the Cutoff.

### 16.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Cutoff tooltip | hover or type at knob 0, 0.6, 1 | reads 16.0 Hz, 1.02 kHz, 16.38 kHz; typed "440", "2.5 k", "1K" accepted; "abc", "-5" rejected |
| Cutoff corner | 0.1 V sine sweep through the low-pass, State; Ladder | State −3 dB at the Cutoff (±1 %); Ladder −3 dB at 0.434 × Cutoff from 32 Hz to 4 kHz |
| Cutoff pitch | Res 1 (or 0.96 plus a PING), both cores, V/OCT 0 V, knob 0.1 to 0.8 | within 1 cent (ladder) and 7.4 cents (state, 4 kHz) of `16 × 2^(10 × knob)` |
| V/OCT | Res 1, V/OCT −3 to +3 V in 1 V steps | each volt an octave; state +14.5 cents and ladder −4.5 cents at +3 V from 1,024 Hz |
| V/OCT clamps | −7 V, +5 V from 1,024 Hz | 8 Hz and 20,000 Hz (at 48 kHz) |
| FM | Res 1, FM knob 1, 1 V and −1 V | one octave up and down; FM knob 0 does nothing |
| Spread | Res 1, Spread 0, 0.5, 1 | OUT L down and OUT R up by the same ratio: 0.5 octave each way at 1; at 0 L and R identical |
| Mode | white noise in, Mode 0, 0.25, 0.5, 0.75, 1 | LP, LP/BP, BP, BP/HP, HP; the tooltip names the blend; Mode CV 5 V equals the knob at 0.5 |
| Res | white noise, Res 0 to 0.94 | the peak rises; passband gain stays within 0.1 dB; no self-oscillation below 0.95 |
| Res onset | knocked ring held 6 s | holds from Res 0.95 in both cores; dies just below |
| Self-oscillation level | Res 1 | 4.07 V (State), 4.8–4.9 V (Ladder) peak at 30 Hz to 1 kHz |
| Self-oscillation start | Res 1, nothing patched, 55 Hz and 1 kHz | starts in about 1.5 s and 0.08 s without PING; at once with PING |
| Drive | 5 V 100 Hz sine in the passband, Drive 0 to 1 | clean at 0 (0.3 % distortion); rises to 24–27 % distortion at 1; level within 6 dB of Drive 0 at 5 V; +8 to +9 dB on a 0.5 V sine at 1 |
| Comb | white noise, Comb 0, +0.5, +1, −1 | noon: bit-exact path; noise level unchanged at the other settings; a sustained tone on a peak up to +15.9 dB |
| Comb pitch | PING with Comb +1, band-pass, Res 0 | rings at the Cutoff (±0.3 cents) from 55 to 880 Hz; at 330 Hz it lasts 0.28 s to −40 dB |
| Comb CV | Comb CV +2.5 V equals the knob at +0.5; −5 V equals −1 | identical output |
| Core switch | flip while a tone passes, and while self-oscillating at 220 Hz | no click; the self-oscillation level dips under 3 dB and settles within 100 ms at the other core's level |
| Ping | gate to PING, Res 0.95 to 1, 100 Hz to 1.5 kHz | the filter sings at each gate; at 5 V amplitude through an open filter 4.9 V |
| Ping threshold | gate 0.9 V and 1.1 V; 0.5 V back to 5 V | 0.9 V no pulse; 1.1 V one pulse; no re-arm until the gate falls to 0.1 V |
| Ping rate | square gate at 100, 500, 1,000, 2,000 Hz | pulses per second 100, 500, 500, 667 |
| Ping with BP or HP | Res 0, Mode BP or HP | almost silent (0.13 V and 0.012 V state; 0.04 V and 0.002 V ladder) |
| IN L alone, IN R alone | each alone | both outputs carry it; with IN L alone and Spread 0 L = R exactly |
| L alone | nothing in OUT R | OUT L carries (L + R)/2 of each voice; patching R switches within 5 ms |
| Initialize | after changing all knobs | knobs at defaults; every voice at rest |

### 16.2 Per core

| Core | Listen for | Measurable checks |
|---|---|---|
| Ladder | round, saturates when driven | −12 dB at the Cutoff, 24 dB per octave; at Res 0.9 +23 dB peak at 0.97 × Cutoff, 91 ms ring at 440 Hz; sine 4.8–4.9 V |
| State | gentler slope, more open | −3 dB at the Cutoff, 12 dB per octave; at Res 0.9 +16.7 dB peak at the Cutoff, 34 ms ring at 440 Hz; sine 4.07 V |
| Both | pitch of the sine voice, no click on the switch | within 1.6 dB in level; at 1 kHz 0.0 cents (ladder) and +1.7 cents (state) |

### 16.3 Polyphony

| Procedure | Pass criteria |
|---|---|
| IN L mono, V/OCT 4 channels | four voices; each at its own cutoff; outputs 4 channels |
| IN L 2 channels, V/OCT 4 channels | voices 3 and 4 silent |
| PING 3 channels, nothing else | three voices from PING alone; each rings from its own gate |
| 16 channels on V/OCT | 16 voices; CPU about 2.1 % ladder, 1.5 % state per instance on the Mac at 48 kHz |
| Res CV 1 channel, 4 voices | the same Res on all four; a 4-channel Res CV sets each |
| Add and remove a channel while others ring | the others are unbroken; a returning voice starts silent |
| Mono CV beyond the voices | channels past the last voice ignored |

### 16.4 Sample rates (44.1 kHz to 768 kHz)

| Procedure | Pass criteria |
|---|---|
| run at 44.1, 48, 88.2, 96, 192, 384, 768 kHz | no crash, no silence where there should be sound; at 1,024 Hz: State corner 1,022.8 Hz; Ladder 444.5–444.7 Hz; sine at 220 Hz within 0.25 cents; level State 4.066–4.067 V, Ladder 4.90–4.92 V |
| change rate while a ring sounds | the tail is cleared (expected); nothing loud or stuck afterwards |
| CPU against rate | proportional: 48 → 96 kHz doubles; 16 voices at 192 kHz about 10 % of a Mac core |
| cutoff ceiling | 19,845 Hz at 44.1 kHz; 20,000 Hz from 44.44 kHz |
| comb memory | 1 MB at 48 kHz up to 16 MB at 768 kHz |

### 16.5 Aliasing

| Procedure | Pass criteria |
|---|---|
| 5 V sine from 4.8 to 19 kHz, open low-pass, Drive 0, spectrum analyser | every component that is not a harmonic at least 85 dB under the tone |
| the same with Drive 1 | up to −33 dB at 4.8 kHz and −6 dB at 19 kHz: expected, report on #67 |
| sine voice at Res 1 from 1 to 10 kHz | harmonics (3rd about −50 to −54 dB) only; others under −87 dB |

### 16.6 Bypass, saving and robustness

| Procedure | Pass criteria |
|---|---|
| bypass with IN L only | OUT L and OUT R carry IN L; polyphonic input passes polyphonic [unverified in Rack] |
| save a patch with every knob moved; reopen | all eight params restored; filters start from rest |
| duplicate the module | copy carries all knobs; the instances are independent |
| NaN or huge CV | the output stays within ±10 V |
| 3 minutes of Res 1, Drive 1, Comb 1 and loud noise | output ≤ 10 V (measured peak 9.54 V state, 8.62 V ladder); no DC; silence 10 s after Res and Comb come down |
| CPU meter, one instance | about 0.4 % of a Mac core at the defaults, even with nothing patched |
| Windows | no automated budget; record the Rack meter per instance for idle, one voice and 16 voices at 48 kHz |

---

## 17. Glossary

| Term | Meaning |
|---|---|
| Alias | a component that appears at a frequency that is not a harmonic of the tone, from content above Nyquist folding back |
| Band-pass (BP) | the response that passes a band at the cutoff |
| Comb | a short delay fed back on itself, giving a row of evenly spaced peaks |
| Core | the filter on the switch: Ladder or State |
| Cutoff | for the State core the −3 dB corner; for the Ladder the frequency of each of its four poles |
| Drive | gain into the core's saturation, with a make-up gain after it |
| Float4 | four floats side by side; one pass holds two voices as L, R, L, R |
| Halfband | the filter pair that takes the signal up to twice the rate and back down |
| Kick | feeding the outgoing core's output into the incoming core during the switch's fade, so a ringing voice carries over |
| Ladder | Fathom's four-pole transistor ladder, 24 dB per octave |
| Lane | one of the four floats in a `Float4` |
| Low-pass (LP) / High-pass (HP) | passes below / above the cutoff |
| Make-up | the gain after the core that takes back part of Drive's gain |
| Mode | the LP, BP, HP dial and its CV |
| Onset | the Res setting (0.95) at which self-oscillation begins |
| Ping | a 1 ms, 5 V hump struck into both sides of a voice by a rising edge at PING |
| Res | resonance; ladder feedback k = 4 at the onset |
| Spread | moves the left cutoff down and the right cutoff up |
| State | the second core, a 12 dB per octave state-variable filter |
| Voice | one filter pair (left and right) for one channel of a polyphonic cable |

---

### Note on the measurement programs

- All are single C++ files compiled outside the repository, against the extracted 2.0.39 tree (commit `2e6e960`) and its Rack-free headers, with `-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`. `rig.hpp` is the shared harness (an engine with the module's defaults, one voice, both outputs patched, a sine-gain function, a zero-crossing pitch meter). They are in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/narrows/meas/`:
  - `m_cutoff.cpp` (corner against knob; self-oscillation pitch against knob, V/OCT, FM, Spread and the clamps);
  - `m_res.cpp` (Res response, onset, sine level and harmonics, start times);
  - `m_ping.cpp` (the pulse, thresholds, edge rate, ring decay, comb pluck);
  - `m_level.cpp` (Mode gains, Drive, worst-case peaks, comb gain, ping spectrum);
  - `m_alias.cpp` (aliasing, with `AliasMeter`);
  - `m_rates.cpp` (corner, pitch, level, memory and CPU against sample rate);
  - `m_clicks.cpp`, `m_cv.cpp`, `m_core.cpp`, `m_glide.cpp`, `m_comb.cpp`, `m_drive_sine.cpp`, `m_floor.cpp`, `m_voices.cpp` (steps, CV scales, the core switch, knob smoothing, comb pitch, Drive and the sine, idle noise, cost against voices).
  - Their printed output is saved as `out_*.txt` beside them. `build.sh <name>` compiles one.
- They drive the engine as `Narrows.cpp` does (one frame of knob positions and port voltages per sample). They do not exercise Rack's ports, threading, GUI, tooltips or the bypass path. Anything marked [meas] should match Rack at the stated rate; Rack-side effects are outside them.
- The test programs were built with `make -f tests.mk MODULES=narrows TEST_WERROR=-Werror` and run from a copy of the tree (`git archive 2e6e960`), not from the repository.
- I did not run Rack, the Windows plugin, `make libm-calls` or `make smoke`.

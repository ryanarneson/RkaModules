# Fog 2.0.2: Technical Reference for QA

> **Note (2026-11-24):** this reference was written for Fog 2.0.2 and has not been redone for 2.0.39. Fog's code has changed since (fixes for issues #11 and #12, a soak check for #16, and the panel's bottom row); check figures against the code and the manual. The manual's 2.0.39 corrections (Bloom, Size) are in `docs/fog/README.md`.


Module: **Fog**, plugin `rka` (slug `rka`, module slug `Fog`), VCV Rack 2 (minRackVersion 2.5.0).
Build: **2.0.2 = commit `122d85b`** of the RkaModules repository (plugin.json `"version": "2.0.2"`). The merged main branch, commit `7b43182` (pull request #28), has a tree identical to `122d85b` (`git diff 122d85b 7b43182` is empty). Everything here was taken from that tree.
Package on the share: `rka-2.0.2-win-x64.vcvplugin`, 460,707 bytes, 2026-11-16 14:38. It contains `plugin.json` with version 2.0.2 and the four factory presets in `presets/Fog/`, byte-identical to the commit's.
Document date: 2026-11-16. Replaces `Fog-2.0.1-Technical-Reference.md` for testing 2.0.2; that file is left as it was.

## What changed since 2.0.1

The full account, with the reasons, before and after figures and what to retest, is in **`Fog-Changes-2.0.1-to-2.0.2.md`** (same folder). In short:

1. **Low-frequency stereo fixed.** Below 300 Hz both outputs now come from one shared tap through an 80° phase pair. A mono bass tone gives an L/R correlation of about +0.16 to +0.30 and a mono-sum loss of about 2 dB in every mode (2.0.1: −0.89 and 12.5 dB at worst). Sections 2.10 and 4, and issue #19.
2. **The wet level now scales with decay time.** Full wet is matched to dry at noon Visibility and is 10·log10(T60 / 5.66 s) dB elsewhere: −11.5 dB fully left, +11.5 dB fully right, the same at any Size. Loud material from about MIST upwards is held to about the noon level by the leveller. Section 6.1.
3. **Drift, Visibility and Damp steps no longer click.** Drift depth goes through a 40 ms smoother, and Visibility and Damp through a 30 ms smoother. The make-up takes Visibility from the same smoother, so a jump neither steps nor swells the wet. Sections 2.4, 2.6, 2.8 and 6.1.
4. **The leveller's slow attack is 10 ms (was 50 ms).** The hotter onsets at long Visibility would otherwise put more than 1 % of samples over the knee at Mix noon. Section 6.2.
5. **Four factory presets and the manual now ship.** Section 3.1. The manual is `docs/fog/README.md`.
6. **Known issues are now GitHub issues #11–#27.** Section 9. Do not re-file them.

Unchanged: the panel, ports, param ids, menu, JSON keys (`Fog.cpp` is byte-identical to 2.0.1), Bloom, the diffuser, the rate divider, ICE's shifter, the mode switch, the sleep logic and the limiter. Saved patches load identically in both directions.

---

## 0. How to read this document

- Every number here comes from one of three sources, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.2 sources (`src/fog/*.hpp`, `src/DelayLine.hpp`, `src/FastMath.hpp`, `src/fog/Fog.cpp`).
  - **[test]**: printed by the automated tests. I rebuilt them from the 2.0.2 sources with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; the hostile test with ASan and UBSan at `-O1`) and ran them on an Apple M-series Mac. All six test programs, the soak and the bench cases passed.
  - **[meas]**: a measurement program I wrote against the 2.0.2 engine (`rka::FogEngine`, the code the module runs), at 48 kHz unless stated. The same program was compiled against the 2.0.1 sources for the before and after figures in the changes document.
- Numbers from the QA testbed are marked **[testbed]**:
  - `/Volumes/share1/VCVRACK/fog-lv426-handoff/FINDINGS.md` measured 2.0.0 in Rack on Windows.
  - `/Volumes/share1/VCVRACK/fog-testbed/runs-2.0.1/RESULTS.md` measured 2.0.1 headless in Rack on macOS.
  - **The 2.0.2 testbed run is complete:** `/Volumes/share1/VCVRACK/fog-testbed/runs-2.0.2/RESULTS.md`. Against ValhallaRoom LV-426 on the QA patch, every setting now gives L/R sustain correlation +0.16, mono-sum loss 2.4 dB and R−L 0.0 dB (2.0.1: −0.70/−0.89, 7.3/12.5 dB, +4.2 dB), with 0 % of samples at the ceiling and decay within 5 % of Visibility. Level falls 2.9 dB from 6.32 s to 2.15 s (LV-426: 4.8 dB; the leveller holds the loud drone down at 6.32 s). Attack to −3 dB at Bloom 50 % is 1.8 s at 6.32 s and 3.1 s at 2.15 s (LV-426 0.65 s; #24). Engine-derived predictions elsewhere in this document are superseded by that file where they differ.
- **[issue #N]** points to an open GitHub issue in `ryanarneson/RkaModules` (section 9).
- **[unverified]** marks anything I could not check against code or by measurement (mostly Rack host behaviour).
- Voltages are Rack volts. "dB re dry" is RMS of the output against RMS of the input.
- The knob value of every panel control is in [0, 1]; "noon" is 0.5.

---

## 1. What Fog is

Fog is a 10 HP stereo reverb for very large, dark, slowly building spaces. The design intent (spec `docs/specs/2026-11-15-fog-design.md`) is the character of Valhalla Supermassive's large dark modes and of the LV-426 mode of Valhalla Room, built from published reverb designs. Valhalla's algorithms are not published, so Fog copies nothing; it aims at the character only.

The test of success in the spec is a held drone (Fathom) swallowed by its own wash, with a tail that lasts seconds to minutes after the note stops.

Design goals that QA can check:

- The decay time is set in seconds (Visibility) and holds at any Size.
- The tail always decays, even fully right. There is no freeze and no infinite mode.
- Four modes (HAAR, FRET, SMOKE, ICE) are voicings of one engine, and every knob keeps its direction in every mode.
- Stereo throughout; a mono source fills both sides. Since 2.0.2, the low end (below about 300 Hz) is mono-compatible.
- The wet level follows the decay time, as a room's does (user's ruling 2026-11-16, "go with scaling", as in ValhallaRoom), matched to dry at noon.
- No metallic ring on long tails, no level that builds without limit under a sustained drone, and no click or burst on a mode switch, on patching, on a sample-rate change or on a control step (the spec's anti-goals).

Non-goals (do not file these as defects): realistic rooms, a plate, freeze, choosing the mode from the panel or by CV, and CV on Visibility.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against the code's behaviour; the spec's and the manual's versions are given for context. Issue #20 tracks the documentation inconsistencies.

| Topic | Spec / manual says | 2.0.2 does |
|---|---|---|
| Size range | spec "roughly 0.3× to 3×"; manual "about 0.3x to 3x" | exactly 1/3× to 3× (3^(2·Size−1)) |
| Size slew | manual: "slewed (about 100 ms)" | a 30 ms one-pole that settles in about 100 ms [issue #20] |
| CV law | spec and manual: "±5 V at full attenuverter sweeps the knob's whole range" | adds cv/10 × att to the knob, then clamps; ±5 V covers the whole range only from noon |
| Polyphonic CV | spec silent; manual: "uses its first channel only" | channel 1 only, as the manual says [issue #26 asks whether to sum instead] |
| Bloom fully left | spec "under 50 ms" to −3 dB; manual "about under 50 ms" | 12.3 ms (24.6 ms in FRET) |
| Bloom whole-output attack | manual: "roughly 1.2 to 1.7 s at noon" | 1.25–1.34 s in HAAR, SMOKE and ICE, 2.19 s in FRET, at T60 6.32 s [meas] (section 2.7) |
| Mode switch | "fades out over about 50 ms", then a clean start | 50 ms out, clear, 50 ms in; Bloom also restarts from zero, so the wet re-swells at Bloom's rate |
| Sleep hold | spec "0.25 s or twice the longest line" (also TESTING.md); manual "about 1.4 s (at 48 kHz, in every mode)" | twice the longest ring = 1.402 s at every supported rate, not only 48 kHz [meas]. The spec has no Sleep section [issue #20] |
| Drones over the 9 V knee | spec: at most 0.2 % at Mix 1, 0.63 % at Mix noon | measured 0.18 % and 0.627 % [test]; the test's limit is 1 % for both |
| ICE T60 | spec and manual "about 13 % short" | 10.2–14.1 % short at FOG and DENSE [test] |
| ICE octave | spec "a faint octave-up shimmer"; manual "about 11 to 13.5 dB under the note" | 9.6–14.2 dB under the note [test] [issue #20 for "faint"] |
| Short sounds against Visibility | spec and manual: "a short sound (a ping, a pluck) is quieter at DENSE than at CLEAR" | **now the reverse**: a 50 ms burst's loudest 50 ms of wet is about 11 dB louder at DENSE than at CLEAR (section 6.1). The sentence describes 2.0.1. Filed as #29 |
| libm | none in `process()` | none per sample; a mode switch (and reset) computes coefficients with libm once, on the audio thread [issue #14] |
| Factory presets | Becalmed, Nebula, Harbour, Whiteout, "tuned by ear" | shipped with values chosen from the spec, not yet tuned by ear [issue #23] |

---

## 2. Panel, controls and ports

The panel, the ports and the parameter set are unchanged from 2.0.1 (`Fog.cpp` and `FogLayout.hpp` are byte-identical).

### 2.1 Layout

10 HP. Top to bottom: title; Size, Damp, Bloom, Drift and Mix on an arc; under each its attenuverter and then its CV jack; the large Visibility knob; the bottom row with IN L, IN R (left) and OUT L, OUT R (right). Positions in mm from `FogLayout.hpp` [code]:

| Control | Centre (x, y) mm |
|---|---|
| Visibility | 25.4, 82.0 |
| Size / Damp / Bloom / Drift / Mix knobs | 5.9 / 15.65 / 25.4 / 35.15 / 44.9, at y 33.79 / 30.92 / 30.0 / 30.92 / 33.79 |
| Attenuverters | same x, y 44.3 |
| CV jacks | same x, y 54.3 |
| IN L, IN R | 5.9, 115.0 and 15.65, 115.0 |
| OUT L, OUT R | 35.15, 115.0 and 44.9, 115.0 |

The panel generator's preview (the manual's `Fog.png`) now shows Visibility at noon, like every other knob (`tools/gen_panels.py`). It was drawn at its first word in 2.0.1. The module's real default has always been noon.

### 2.2 Parameter, input and output ids

These are pinned by `static_assert` in `Fog.cpp` and must never change number (saved patches depend on them).

| Id | Param | Range | Default | Tooltip unit |
|---|---|---|---|---|
| 0 | Visibility | 0..1 | 0.5 | seconds, shown as 0.4 × 200^v |
| 1 | Size | 0..1 | 0.5 | % (0..100) |
| 2 | Damp | 0..1 | 0.5 | % |
| 3 | Bloom | 0..1 | 0.5 | % |
| 4 | Drift | 0..1 | 0.5 | % |
| 5 | Mix | 0..1 | 0.5 | % |
| 6..10 | Size, Damp, Bloom, Drift, Mix CV amount (attenuverters) | −1..+1 | 0 | % (−100..+100) |

| Id | Input | | Id | Output |
|---|---|---|---|---|
| 0 | IN L ("Left") | | 0 | OUT L |
| 1 | IN R ("Right (normalled to left)") | | 1 | OUT R |
| 2..6 | Size CV, Damp CV, Bloom CV, Drift CV, Mix CV | | | |

There are no lights.

### 2.3 The CV law (Size, Damp, Bloom, Drift, Mix)

[code, `FogEngine::knob`]

```
effective = clamp( knob + (cv / 10) × att , 0, 1 )
```

- **Polyphonic CV uses channel 1 only.** `cv` is the first channel of the CV jack (`FogPort::first()`). The other channels of a polyphonic cable on a CV jack are ignored. An unpatched CV jack reads 0 V. The audio inputs are different: they sum all channels (section 2.10). This is documented in the manual since 2.0.2. Whether polyphonic CV should be summed is an open decision [issue #26]; until then channel-1-only is the intended behaviour.
- With the attenuverter at +1, +5 V adds 0.5 and −5 V subtracts 0.5. So from noon, −5 V..+5 V sweeps the whole range. From 0, +10 V is needed to reach 1.
- There is no voltage limit on the jack itself; the sum is clamped to [0, 1], so ±50 V simply saturates [test: `cv_law`, +50 V clamps to 1].
- NaN or ±infinity on a CV jack or a parameter reads as 0 [code: `fogFinite`].
- Visibility has no CV jack (spec non-goal).
- Dry kill overrides the Mix knob and the Mix CV: Mix is forced to 1 [code; test: `dry_kill_overrides_cv`].
- **New in 2.0.2: Damp and Drift CV pass through smoothers** (sections 2.6 and 2.8). The CV is still read every sample, but Damp reaches the filters through a 30 ms one-pole (corner about 5.3 Hz) and Drift's depth through a 40 ms one-pole (corner about 4.0 Hz). Fast Damp or Drift CV is therefore filtered, and a stepped one moves smoothly. Size keeps its 30 ms slew, Mix its 5 ms ramp, and Bloom is unsmoothed (it only shapes the feed's gain, which is itself a slow follower).

### 2.4 Visibility: the decay time

[code, `fogT60`, `Network::stepDecay`]

```
T60 = 0.4 s × 200^v        (v = Visibility knob, 0..1)
```

- Fully left 0.4 s; noon 5.66 s; fully right 80 s.
- T60 is the time for the low-frequency tail (250 Hz band) to fall by 60 dB. Damp shortens the highs only.
- Each delay line's feedback gain is worked out from T60 and that line's current length, so the decay time does not depend on Size.
- **Smoothing (new in 2.0.2).**
  - The network uses a smoothed Visibility, `visSm`. It follows the knob through a one-pole of 30 ms time constant, taken as one step every 32 inner-rate samples (the gains' refresh): coefficient 32 / (0.03 × inner rate + 32). It snaps to the knob once within 1e-4.
  - The new T60 holds within about 100 ms of a jump.
  - On reset, a clear, a mode switch and a wake, `visSm` snaps to the knob (no glide from an old value).
  - The make-up takes the same `visSm` (section 6.1), so the network's level and the make-up move together.
- Visibility's level effect: full wet now rises with Visibility (section 6.1). Turning Visibility therefore changes loudness as well as decay.

Dial words (the spec places each word at the knob position whose time it names; the face has no numerals):

| Word | v on the dial | T60 at that v [code] | Measured T60, HAAR, Size noon, 250 Hz [test] | Full-wet level re dry on quiet material [code] |
|---|---|---|---|---|
| CLEAR | 0.042 | 0.50 s | 0.50 s (+1.0 %) | −10.5 dB |
| HAZE | 0.304 | 2.00 s | 2.01 s (+0.4 %) | −4.5 dB |
| MIST | 0.477 | 5.01 s | 4.96 s (−1.0 %) | −0.5 dB |
| FOG | 0.642 | 12.0 s | 12.00 s (−0.1 %) | +3.3 dB |
| THICK | 0.815 | 30.0 s | 30.02 s (0.0 %) | +7.3 dB |
| DENSE | 0.975 | 70.1 s | 70.05 s (0.0 %) | +10.9 dB |

### 2.5 Size

[code, `Network::setTargets`, `updateLengths`]

```
scale = 3^(2 × Size − 1)        (1/3 at 0, 1 at noon, 3 at 1)
line length = base length × scale
```

- Size scales every FDN line length together. It does **not** scale the input diffuser or SMOKE's in-loop allpasses.
- The scale is slewed by a one-pole with a 30 ms time constant (about 100 ms to settle) [code]. Moving Size therefore bends the pitch of the tail (a "swoop"), as a delay changing length does. This is the spec's intent, and fast Size CV is a pitch effect.
- A Size 0.5 → 1 jump gives an output step of 0.13–0.33 V on a 1 V 110 Hz sine (0.14–0.29 V in 2.0.1) [meas]. This is the intended swoop, not a click.
- Size does not change T60 [test: `t60_holds_across_size`, within 0.4 % at Size 0.25, 0.5, 0.75; `modes_t60` within 3.0 % at Size 0 and 1 for HAAR, FRET, SMOKE].
- Size does not change the level: Size 1 against Size 0 is within 0.28 dB in HAAR and 0.89 dB in any mode at Visibility 0, noon and 1 [test: `wet_level_law`].
- Line-length ranges per mode are in section 5.

### 2.6 Damp

[code, `Network::setTargets`, `updateGains`, `stepDecay`]

```
d      = dampBias + (1 − dampBias) × (1 − brightness) × Damp_sm
ratio  = 2^−(d + 2 d²)          = T60(4 kHz) / T60(250 Hz)
```

- HAAR (bias 0, brightness 0): ratio 1 at Damp 0, 1/2 at noon, 1/8 fully right. Highs decay 2× faster at noon and 8× faster fully right, as the spec asks.
- Implementation: a first-order high shelf in each line's feedback (bilinear, corner 2 kHz, unity at DC).
  - Its high-frequency gain G is solved so that the 4 kHz gain per pass gives the wanted 4 kHz T60.
  - The shelf's small dip at 250 Hz is made up in the line gain, so the 250 Hz T60 stays Visibility's at any Damp and Size.
  - If the wanted cut is deeper than the shelf can reach (G = 0, about −7 dB a pass at 4 kHz), the shelf stops at its floor. This happens only for short T60s with long lines [issue #26].
- **Smoothing (new in 2.0.2).** `Damp_sm` follows the effective Damp (knob plus CV) through the same 30 ms one-pole as Visibility, one step every 32 inner samples, snapping within 1e-4.
  - Why: in 2.0.1 a Damp step changed every line's shelf at once. The step was written into the lines and came back one line length later (50–160 ms) as lone spikes. On a 1 V 110 Hz sine, Damp 0 → 1 gave 0.21–0.44 V spikes in HAAR, FRET and SMOKE against a 0.02–0.05 V baseline [meas, 2.0.1]. The 2.0.1 reference reported "no step" because it looked only at the first 20 ms.
  - 2.0.2: the largest step in the 200 ms after Damp 0 → 1 or 1 → 0 is 0.02–0.07 V, at or under the baseline [meas]. Test: `damp_step_no_click`.
- Measured, HAAR, FOG, Size noon [test: `damp_ratio`]: ratio 0.980 at Damp 0, 0.508 at noon, 0.131 at 1. The 250 Hz T60 holds at Damp 1 (HAZE −1.0 %, MIST at Size 1 −0.2 %).
- Per-mode effective d and ratio [code arithmetic]:

| Damp | HAAR d / ratio | FRET d / ratio | SMOKE | ICE d / ratio |
|---|---|---|---|---|
| 0 | 0 / 1.00 | 0.35 / 0.66 | as HAAR | 0 / 1.00 |
| 0.5 | 0.5 / 0.50 | 0.675 / 0.33 | as HAAR | 0.25 / 0.77 |
| 1 | 1 / 0.125 | 1 / 0.125 | as HAAR | 0.5 / 0.50 |

### 2.7 Bloom

[code, `Bloom.hpp`; unchanged] Bloom shapes the signal fed into the network, not the network itself. The dry path never passes through it.

- A gain rises towards 1 while the input is "present" and falls towards 0 when it is not; the feed is input × gain.
  - Rise: one-pole with time constant τ. Fall: twice as fast (τ/2).
  - "Present": a peak follower of |x| (instant attack, 250 ms release) above **1 mV**.
- τ against the knob:
  - Bloom ≤ 0.5: τ = 10 ms × 2^(2 × Bloom × 6.34376), log-spaced from 10 ms to 0.8122 s.
  - Bloom > 0.5: τ = 0.8122 s × 4^(2 × Bloom − 1), up to 3.2488 s.
  - × (1 + bloomBias): FRET's bias is 1, so its Bloom is twice as slow; the other modes have 0.
- Time to come within 3 dB (gain 0.708) is 1.231 τ:

| Bloom | τ | −3 dB rise, HAAR/SMOKE/ICE | −3 dB rise, FRET |
|---|---|---|---|
| 0 | 10 ms | 12.3 ms [test] | 24.6 ms [test] |
| 0.25 | 90 ms | 0.111 s | 0.222 s |
| 0.5 | 0.812 s | 1.000 s [test] | 2.00 s |
| 0.75 | 1.62 s | 2.00 s | 4.00 s |
| 1 | 3.25 s | 3.98 s [test] | 7.97 s [test] |

- These are the feed's rise times. The whole output's attack is longer, because the network's own build-up adds to it. Whole output, ±1 V noise, T60 6.32 s, Size noon, time to within 3 dB of steady [meas, identical within 0.01 s on 2.0.1]:

| Mode | Bloom 0: first \|out\| > 1 mV | Bloom 0: to −20 dB | Bloom 0: to −3 dB | Bloom noon: to −3 dB |
|---|---|---|---|---|
| HAAR | 66 ms | 0.13 s | 0.30 s | 1.34 s |
| FRET | 77 ms | 0.15 s | 0.32 s | 2.19 s |
| SMOKE | 139 ms | 0.16 s | 0.23 s | 1.27 s |
| ICE | 50 ms | 0.08 s | 0.25 s | 1.25 s |

  The testbed measured FRET's attack at Bloom 50 % as 1.7 s at 6.32 s and 1.2 s at 2.15 s on 2.0.1, against LV-426's 0.65 s [testbed; issue #24]. The onset delay is issue #21.
- Consequences QA will see:
  - **An input that never exceeds 1 mV peak produces no wet at all** [meas on 2.0.1: 0.5 mV and 0.9 mV sines gave wet RMS 0 and the engine stayed asleep; 1.1 mV gave a wet; Bloom is unchanged]. This is a gate by design (−74 dB re 5 V), not a defect.
  - Even at Bloom 0 the gain starts at 0 and needs about 12 ms to open, so a **single-sample impulse goes in at only 1/480 of its height at 48 kHz** (−54 dB) and 1/7680 at 768 kHz [code arithmetic]. Use bursts, not one-sample impulses, for decay measurements (section 11).

### 2.8 Drift

[code, `Network::updateModulation`, `Network::process`]

- Each line's read position swings by a sine of its own rate and phase:
  - Rates: 0.1 Hz (shortest line) to 0.5 Hz (longest), spread log-evenly. **Very slow** multiplies them by 0.2 (0.02–0.1 Hz).
  - Starting phases are golden-ratio steps, so the lines are decorrelated.
- Depth target (either side of the nominal length): Drift² × 8 ms on Slow, Drift² × 24 ms on Very slow, capped per line at 0.45 × the line's length at Size 0, so a read never leaves its line.
- **Smoothing (new in 2.0.2).**
  - The applied depth follows the target through a **40 ms one-pole, per sample**, coefficient 1/(0.04 × inner rate + 1). It runs while any line's depth differs from its target by more than 1e-3 samples.
  - A step in Drift (knob jump, stepped CV) or a switch of the Drift rate menu changes the depth target. The reads move to the new swing over about 40 ms (63 %) to 200 ms (99 %), instead of jumping.
  - The rate itself is not smoothed. The modulation phase is continuous, so a new rate changes the speed of the swing but never moves the read.
  - On reset, clear, mode switch and wake, the depth snaps to its target.
  - Known side effect: in float the one-pole can stop 0.03–0.47 samples short of its target, above the 1e-3 threshold, so the per-line update keeps running every sample after a Drift change. It is inaudible and costs a little CPU while awake [issue #11].
- Measured steps [meas], 1 V 110 Hz sine, Mix 1, other knobs noon, largest sample-to-sample output step in the 200 ms after a single change (baseline: the second before, 0.025–0.054 V):

| Change | 2.0.1, worst mode | 2.0.2, every mode |
|---|---|---|
| Drift noon → 0 | 1.63 V (ICE; HAAR 0.63 V) | ≤ 0.052 V |
| Drift noon → 0.75 | 1.80 V (ICE; HAAR 1.20 V) | ≤ 0.034 V |
| Drift noon → 1 | 3.66 V (HAAR) | ≤ 0.049 V |
| Drift rate Slow → Very slow, Drift noon | 2.39 V (ICE; HAAR 2.20 V) | ≤ 0.046 V |

- Drift 0 is exactly static: two renders of the same input started at different times are bit-identical [test: `drift_zero_is_static`].
- Peak pitch deviation at Drift 1 [test: `drift_depth_per_rate`]: 43.0 cents on Slow, 25.3 cents on Very slow.
- Reads use 4-point Hermite interpolation (`DelayLine::read`).

### 2.9 Mix

[code; unchanged]

```
out = cos(π/2 × m) × dry + sin(π/2 × m) × wet
```

- Equal-power. Mix 0 is bit-exact dry, Mix 1 contains no dry, noon gives 0.7071 × dry + 0.7071 × wet [test: `mix_law`, dry gain error 1.2e-8, mix law within 4.2e-7 V].
- `m` follows the effective Mix (knob, CV and Dry kill) through a **linear ramp of 5 ms** for a full 0→1 move (step 1/(0.005 × rate + 1) per sample), and lands exactly on the target. A Mix CV step or the Dry kill switch therefore does not click [test: `mix_ramp_no_click`, largest step 0.033 V on 5 V DC].
- Measured loudness across Mix, noise, **Visibility noon**: +0.00, −0.00, −0.02, −0.03, −0.04 dB at Mix 0, 0.25, 0.5, 0.75, 1 [test: `level_across_mix`].
- **At other Visibility settings the loudness now changes with Mix**, because full wet is no longer level with the dry (section 6.1). For uncorrelated material the output level is about 10·log10(cos²(π/2·m) + sin²(π/2·m) × 10^(L/10)) dB, where L is the full-wet level of section 6.1. For example, on quiet material at Visibility 1 (L = +11.5 dB), Mix noon reads about +8.8 dB and Mix 1 +11.5 dB; at Visibility 0 (L = −11.5 dB), Mix noon reads about −2.7 dB.

### 2.10 Audio inputs and outputs

- **IN L, IN R.** IN R is normalled to IN L: with nothing in IN R, the right side is fed IN L's signal.
- **Polyphony on the audio inputs: every channel is summed** (`FogPort::sum`, up to 16 channels). The sum goes to both the dry path and the reverb. A 16-channel ±5 V cable can therefore present up to ±80 V as the dry signal, which passes unattenuated at Mix 0. The wet is limited to ±10 V [test: `poly_input_bounded`, peak 10.000 V].
- No voltage clamp on the audio inputs; NaN and ±infinity read 0 per channel and on the sum.
- **OUT L, OUT R**: one channel each.
- The dry path has zero delay at every sample rate [test: `dry_path_zero_delay` at 48, 192 and 384 kHz].
- **Stereo image (new in 2.0.2).** Above about 300 Hz the two outputs come from two orthogonal tap patterns, as before, and are decorrelated. Below about 300 Hz both come from one shared tap, 80° apart in phase (section 4.2, "Low band"). Expected figures:

| Material (mono into IN L, Mix 1) | L/R correlation | Mono-sum loss | R against L |
|---|---|---|---|
| sine 20–200 Hz, Drift noon, any mode, Visibility 0.3 or 0.52 | +0.01 to +0.31 (33 Hz: +0.16; 65 Hz: +0.29 to +0.30) | 1.9–2.4 dB at 33 and 65 Hz; ≤ 4 dB everywhere | within 1.5 dB (measured within 0.1 dB at 33 and 65 Hz) [test] |
| sine 20–140 Hz, Drift 0 | within −0.2 to +0.5 at every point | ≤ 4 dB | within 1.5 dB [test] |
| sine 210–450 Hz (the crossover and above), Drift noon | −0.37 to +0.52 | ≤ 5.0 dB | ≤ 5.1 dB apart [test] |
| broadband noise | −0.03 to +0.00 | 3.0–3.1 dB | within 0.1 dB [test] |
| the testbed's Fathom drone (stereo), Visibility 0.317 and 0.521 | +0.16 to +0.18 | 2.3–2.4 dB | within 0.1 dB [test] |
| 1 kHz sine | −0.05 to +0.08 | 2.7–3.2 dB | up to 2.6 dB (FRET) [meas] |

  "Mono-sum loss" = 10·log10(mean channel power / power of (L+R)/2): 0 dB for identical channels, 3 dB for uncorrelated, large for anti-phase. Per mode, 1 V sine, knobs noon, Low cut Off, correlation over 9–12 s [meas]:

| Mode | 33 Hz, v 0.5 | 33 Hz, v 0.25 | 55 Hz | 110 Hz | 220 Hz | 440 Hz | 1 kHz | white noise |
|---|---|---|---|---|---|---|---|---|
| HAAR | +0.16 (2.4 dB) | +0.16 (2.4 dB) | +0.29 (1.9) | +0.25 (2.0) | +0.17 (2.3) | +0.11 (2.5) | −0.01 (3.0) | −0.00 (3.0) |
| FRET | +0.16 (2.4) | +0.16 (2.4) | +0.28 (1.9) | +0.24 (2.1) | −0.00 (3.0) | +0.07 (2.7) | −0.05 (3.2) | +0.00 (3.0) |
| SMOKE | +0.16 (2.4) | +0.16 (2.4) | +0.28 (1.9) | +0.23 (2.1) | +0.14 (2.4) | +0.25 (2.1) | +0.08 (2.7) | −0.02 (3.1) |
| ICE | +0.16 (2.4) | +0.17 (2.3) | +0.28 (1.9) | +0.25 (2.1) | +0.11 (2.6) | +0.08 (2.7) | +0.08 (2.7) | −0.01 (3.0) |

  The same measurement on 2.0.1 read down to −0.89 (12.5 dB) in FRET at 33 Hz; see the changes document.
- **The cost of the shared low band** [test: `bass_evenness`; issue #19]. Under 300 Hz each side's level from note to note is that of one tap sum, so it is lumpier than in 2.0.1, most of all at Drift 0:

| Bass evenness, 20–100 Hz, summed level of both sides | 2.0.1 | 2.0.2 |
|---|---|---|
| Mean spread, Drift noon | 2.2 dB | 3.2 dB |
| Mean spread, Drift 0 | 3.3 dB | 5.8 dB |
| Worst dip below the power mean, Drift noon | −7.2 dB | −11.5 dB |
| Worst dip, Drift 0 | −10.8 dB | −30.4 dB |

  On the Fathom drone at 2.15 s, SMOKE is about 6 dB quieter than the other modes because its sub lands in such a dip [issue #19]. At Drift 0 between 150 and 300 Hz only 41 % of points meet the stereo targets above (test bound 30 %) [test]; above 300 Hz at Drift 0 the image is the network's own and is not bounded.

---

## 3. The right-click menu

[code; `Fog.cpp` unchanged]

| Item | Choices (menu text) | Default | JSON key | Stored value | On load |
|---|---|---|---|---|---|
| Mode | Haar, Fret, Smoke, Ice | Haar | `"mode"` | integer 0..3 | clamped to 0..3 |
| Drift rate | Slow, Very slow | Slow | `"driftRate"` | integer 0..1 | clamped to 0..1 |
| Low cut | Off, 50 Hz, 100 Hz | 50 Hz | `"lowCut"` | integer 0..2 | clamped to 0..2 |
| Dry kill (wet only) | checkbox | off | `"dryKill"` | JSON boolean | `json_boolean_value` |

A saved module's `"data"` object therefore looks like `{"mode": 0, "driftRate": 0, "lowCut": 1, "dryKill": false}`. The knobs are saved by Rack as ordinary params (ids 0–10).

- **Mode** chooses the voicing (section 5). It is not on the panel and has no CV (spec ruling).
- **Drift rate** chooses the modulation rate and depth range (section 2.8). Since 2.0.2 a switch during play moves the reads smoothly (no click).
- **Low cut** is a 2nd-order Butterworth high-pass at 50 or 100 Hz on the feed into the network, in double precision (transposed direct form II), so 50 Hz stays clean at 768 kHz. The dry signal is untouched [test: `low_cut`, 30 Hz wet −9.4 dB with 50 Hz against Off; the dry is bit-exact with the cut at 100 Hz].
- **Dry kill** forces Mix to 1 over the knob and the Mix CV, for use on a send. It ramps over 5 ms like any Mix change.

JSON edge cases [code]:

- A missing key leaves that setting as it was. On a new module that is the default. Whether Rack resets the module before loading a preset onto an existing instance is [unverified]; if it does not, a preset without `"mode"` would keep the current mode. The factory presets carry all four keys, so they are not affected.
- **The integer is narrowed before it is clamped** [issue #12]. The value goes through `int` first:
  - a hand-edited `"mode": 4294967297` loads as FRET (1), not as the clamped ICE (3);
  - a JSON real such as `2.0` reads as 0, because Jansson's `json_integer_value` returns 0 for reals;
  - a string reads as 0. A non-boolean `"dryKill"` reads as false.
  - Values always end in range, so nothing can be corrupted; only the wrong valid value can load.
- Ordinary out-of-range integers are clamped, not rejected (for example `"mode": 7` loads ICE; `"mode": -1` loads HAAR).
- **Initialize** (`onReset`) sets Mode Haar, Drift rate Slow, Low cut 50 Hz, Dry kill off, and resets the engine. Rack resets the knobs.

**Bypass** [code: `configBypass`]: IN L → OUT L and IN R → OUT R. Rack's bypass copies each input to its output. Consequences:

- **IN R's normalling to IN L does not apply under bypass**, so with only IN L patched, OUT R is silent while bypassed [issue #13]. Do not re-file.
- A polyphonic input passes through as polyphonic [unverified in a live Rack].
- `process()` is not called while bypassed, so the tail freezes and resumes where it stopped when bypass is released [unverified in a live Rack].

### 3.1 Factory presets (new in 2.0.2)

Four presets ship in `presets/Fog/` and appear in the module's right-click Preset menu. Their values were chosen from the spec's descriptions and are **not yet tuned by ear** [issue #23]; report impressions on that issue rather than as defects. Each file sets all 11 params and all four menu keys. Every attenuverter is 0, Drift rate is Slow, Low cut is 50 Hz and Dry kill is off in all four.

Exact values [code: the `.vcvm` files]:

| Preset (file) | Mode | Visibility (id 0) | Size (1) | Damp (2) | Bloom (3) | Drift (4) | Mix (5) |
|---|---|---|---|---|---|---|---|
| Becalmed (`1_Becalmed.vcvm`) | FRET (1) | 0.815 | 0.60 | 0.80 | 0.75 | 0.40 | 0.45 |
| Nebula (`2_Nebula.vcvm`) | HAAR (0) | 0.975 | 0.85 | 0.50 | 0.60 | 0.85 | 0.50 |
| Harbour (`3_Harbour.vcvm`) | HAAR (0) | 0.304 | 0.15 | 0.55 | 0.20 | 0.40 | 0.30 |
| Whiteout (`4_Whiteout.vcvm`) | ICE (3) | 0.85 | 0.70 | 0.35 | 0.85 | 0.50 | 1.00 |

What those values mean [code arithmetic]:

| Preset | T60 | Word | Size scale | 4k/250 T60 ratio | Bloom feed −3 dB | Drift depth (Slow) | Dry / wet gain | Full-wet level, law |
|---|---|---|---|---|---|---|---|---|
| Becalmed | 30.0 s | THICK | 1.25× | 0.19 | 4.00 s (FRET 2×) | 1.28 ms | 0.760 / 0.649 | +7.3 dB |
| Nebula | 70.1 s | DENSE | 2.16× | 0.50 | 1.32 s | 5.78 ms | 0.707 / 0.707 | +10.9 dB |
| Harbour | 2.00 s | HAZE | 0.46× | 0.45 | 71 ms | 1.28 ms | 0.891 / 0.454 | −4.5 dB |
| Whiteout | 36.1 s | between THICK and DENSE | 1.55× | 0.85 (ICE) | 2.64 s | 2.00 ms | 0 / 1 (wet only via Mix 1, not Dry kill) | +8.0 dB |

Measured with uniform noise, 40 s, last two thirds [meas]:

| Preset | ±1 V noise, Mix 1 | ±1 V noise, the preset's Mix | ±5 V noise, Mix 1 | ±5 V noise, the preset's Mix | Peak at ±5 V, Mix 1 |
|---|---|---|---|---|---|
| Becalmed | +5.70 dB | +3.32 dB | −0.08 dB (levelled) | −0.03 dB | 10.00 V, 0.17 % over 9 V |
| Nebula | +10.81 dB | +8.15 dB | −0.03 dB (levelled) | −0.01 dB | 10.00 V, 0.18 % |
| Harbour | −4.78 dB | −0.64 dB | −4.78 dB | −0.64 dB | 9.23 V, 0 % |
| Whiteout | +8.44 dB | +8.44 dB | −0.04 dB (levelled) | −0.04 dB | 10.00 V, 0.18 % |

- Becalmed reads 1.5 dB under the law because its heavy Damp (0.8, in FRET) removes high-frequency energy; the law's ±1.5 dB is specified at Damp noon.
- The files' own header says `"version": "2.0.1"` (the plugin version when they were written). Rack is expected to treat this as informational [unverified in Rack]. The developer's smoke test does not load the presets [issue #18], so QA's load test is the first check in Rack.

---

## 4. Signal flow

### 4.1 Block diagram

```
 IN L ──sum poly──┬───────────────────────────────── dryL ──────────────────────────────┐
 IN R ──sum poly──┤  (IN R unpatched: dryR = dryL)                                      │
                  │                                                                     │
                  └─► LOW CUT ─► TONE ─► BLOOM ─► denormal flush (|x|<1e-20 → 0)         │
                      2nd-order   per-mode  swell                                       │
                      HPF 50/100  biquad    gain                                        │
                      Hz or off   (FRET/ICE)                                            │
                                     │  feed (host rate)                                │
   CONTROLS (at the inner rate):     ▼                                                  │
   Visibility, Damp ─► 30 ms one-pole, a step per 32 samples ─► visSm, dampSm           │
   Drift depth      ─► 40 ms one-pole, per sample                                       │
   Size             ─► 30 ms one-pole on the line scale                                 │
              ┌──────────── RATE DIVIDER (÷1, 2, 4 or 8; halfband down/up) ────────────┐│
              │                                                                        ││
              │  L,R ─► 8 ch (L,R,L,R,…) ─► DIFFUSER (2–4 stages: delays, shuffle,      ││
              │                             polarity, Hadamard/√8)                     ││
              │                                  │ × inGain × inSign[j]                ││
              │                                  ▼                                     ││
              │   ┌──► line j write ◄── fb[j] = v[j−1] − (2/N)Σv   (Householder+shift)  ││
              │   │         │                     ▲   ▲ ICE: crossfade on u axis       ││
              │   │   Hermite read at len_j ± Drift                                     ││
              │   │         │ (SMOKE: allpass k=0.6 after each read)                   ││
              │   │         ├──► taps  L = Σ ±r (row 5)  ─► HPF 300 Hz (4th-order) ─┐  ││
              │   │         │          R = Σ ±r (row 10) ─► HPF 300 Hz (4th-order) ─┤  ││
              │   │         │        LOW = Σ ±r (row 15) ─► LPF 300 Hz (4th-order)  │  ││
              │   │         │                 ├─► phase pair branch A ─► + L        │  ││
              │   │         │                 └─► phase pair branch B ─► + R (80°)  │  ││
              │   │         │          × wetGain √(1 − g_m²)  (from visSm)  ◄───────┘  ││
              │   │         │          (ICE: + 0.35 × octave-up of each side)          ││
              │   │   × g_j (T60 gain, visSm) ─► damp shelf_j (dampSm) ─► v[j]          ││
              │   └──────────────────────────────┘                                     ││
              └────────────────────────────────────────────────────────────────────────┘│
                                     │ wet L, R                                         │
                                     ▼                                                  │
           × WET MAKE-UP = matched(visSm, Size_sm) × mode factor × √(T60(visSm)/5.66 s)  │
             (visSm taken the sample it changes; Size_sm through a 30 ms one-pole)      │
                                     ▼                                                  │
           WET LEVELLER (stereo-linked slow AGC, target 6.5 V, slow attack 10 ms)       │
                                     ▼                                                  │
           LIMITER (identity to ±9 V, tanh knee to ±10 V), each side                    │
                                     ▼                                                  │
           × MODE FADE (50 ms out, switch, 50 ms in)                                    │
                                     ▼                                                  ▼
           OUT = cos(π/2·m)·dry + sin(π/2·m)·wet ,  m = Mix ramped linearly over 5 ms
```

While the engine is asleep (section 7), the boxes from the rate divider to the limiter are skipped and the wet is exactly 0; the low cut, tone, Bloom and mix keep running.

### 4.2 Block constants

**Input sanitising** [code]: each channel and the sum pass through `fogFinite` (NaN and ±inf → 0). The network also zeroes any input with |x| ≥ 1e9.

**Low cut** [code]: Butterworth, Q = 0.7071, f clamped to 0.4 × rate; state flushed below 1e-30.

**Tone** (per mode, RBJ cookbook biquad at the host rate, coefficients worked out only on a mode or rate change) [code]:

| Mode | Tone stage |
|---|---|
| HAAR | none |
| FRET | peaking, 300 Hz, +4 dB, Q 0.7 |
| SMOKE | none |
| ICE | high shelf, 3 kHz, +3 dB, slope parameter 0.7071 |

**Bloom**: section 2.7.

**Rate divider** [code, `RateDivider.hpp`]: the network runs at the host rate divided by 1, 2, 4 or 8, the smallest factor that brings the inner rate to 96 kHz or below. Each halving is a polyphase halfband filter going in and another coming out (`fathom/Halfband.hpp`). The output is one inner block late, so above 96 kHz the wet arrives a few host samples late; the dry does not. A 5 kHz tone passes within 0.1 dB at 44.1–768 kHz [test: `rate_factor`]. The passband edge above 5 kHz was not measured.

| Host rate | Factor | Inner rate |
|---|---|---|
| 44.1, 48, 88.2, 96 kHz | 1 | same |
| 176.4, 192 kHz | 2 | 88.2, 96 kHz |
| 352.8, 384 kHz | 4 | 88.2, 96 kHz |
| 705.6, 768 kHz | 8 | 88.2, 96 kHz |

**Input diffuser** [code, `Diffuser.hpp`]:

- 8 channels; the input is laid out L, R, L, R, … (even channels L, odd R).
- 2 to 4 stages. Each stage has 8 delays drawn by a seeded LCG (seed 0x5eed) within the stage's span: delay_i = span × (i + 0.25 + 0.5u)/8. Then a random channel permutation, random polarity flips, and an orthonormal 8-point Hadamard (scaled by 1/√8).
- Energy-preserving [test: `diffuser_energy`, RMS ratio within 1 %].
- Integer-sample reads. Not scaled by Size.

**FDN** [code, `Network.hpp`]:

- N = 16 lines (HAAR, FRET, ICE) or 8 (SMOKE).
- Feedback matrix: a Householder reflection (I − (2/N)·11ᵀ), then a cyclic shift by one line (output j takes line j−1). Both are orthogonal, so the matrix is lossless. The shift removes the reflection's strong diagonal so energy walks round all lines instead of each line feeding itself as a comb.
- Injection: line j receives `inGain × inSign[j] × diffuser[j mod 8]`, with inGain 0.25 for 16 lines or 0.3536 for 8. inSign is row 3 of the Sylvester Hadamard pattern.
- Per-line gain: g_i = 10^(−3 × L_i / (T60(visSm) × rate)), where L_i is the line's current length plus, in SMOKE, its in-loop allpass delay. Each g_i < 1, so the tail always dies. The shelf's 250 Hz dip is made up in g_i, capped at (1 + g)/2.
- **Gains, damping and the ICE crossfade are recomputed at most once per 32 samples** when the smoothed Visibility, the smoothed Damp or the slewing length changes. A change inside a 32-sample window waits for the window to end.
- Output taps: wet L = Σ tapL_i × r_i, wet R = Σ tapR_i × r_i, with ±1 patterns from rows 5 and 10 of the Sylvester Hadamard matrix. The reads are tapped before the line gain and the shelf.
- **Low band (new in 2.0.2)** [code, `Network::process`, `setLowBand`]:
  - Split: **300 Hz** (`lowSplitHz`), a 4th-order Butterworth made of two biquad sections (Q 0.5412 and 1.3066), bilinear with prewarping, in double precision (transposed direct form II, state flushed below 1e-20).
  - Each side's tap sum (L, R) is high-passed at 300 Hz.
  - A third tap pattern, LOW = Σ tapLow_i × r_i with tapLow from row 15 of the Sylvester Hadamard matrix (row 7 over 8 lines in SMOKE), orthogonal to the L, R and input patterns, is low-passed at 300 Hz by the matching Butterworth. The two bands' powers sum to 1.
  - LOW then feeds a **phase pair**: two chains of two first-order allpasses, poles at 6.965 and 159.461 Hz (branch A, added to L) and 30.966 and 711.28 Hz (branch B, added to R). Their phase difference is 80° ± 7.5° from 8 Hz to 600 Hz (minimax fit). cos 80° = 0.17, which is the expected bass correlation (+0.05 to +0.30).
  - The low band's filter states are cleared on clear, on a mode switch and on every wake (the high-pass can hold energy above the wet it feeds).
  - The T60s are untouched: every tap pattern decays at the network's rate.
  - Why 300 Hz and not 200 Hz (1bbfd08): the high-pass leaks the sides' own random phase for about an octave below the split. At 200 Hz, 96 of 152 points from 105 to 195 Hz missed the targets at Drift 0; at 300 Hz, 22 of 152 [commit].
- Wet normalisation: × wetGain = √(1 − g_m²), where g_m is the loop gain of the mean line length (from visSm), because steady-state power through the loop grows as 1/(1 − g_m²). wetGain is 0.985 at Visibility 0 and 0.132 at 1. The calibration constant `wetCal` is 1; with the low band the network's own wet sits 0.7–1.3 dB under the input across the six words [test: `even_wet_level`], and the engine's make-up levels it.
- Denormals: every filter state and line write is flushed to 0 below 1e-20.

**Make-up, leveller, limiter, fade, mix**: section 6, with the fade in section 5.6.

### 4.3 Memory

All buffers are sized once for the longest line of any mode at Size 1 plus the full Drift swing, at the inner rate. They are allocated only in `setSampleRate()`, never in `process()` [test: `no_allocation_in_process`, 0 allocations over every mode, moving knobs, CVs, menu changes and mode switches].

Delay memory per instance: **3.1 MB at 44.1/48 kHz, 6.2 MB at 88.2 kHz and above** [meas on 2.0.1], since the inner rate is capped at 96 kHz. 2.0.2 adds no delay buffer (the low band is a few dozen doubles of filter state) [code], and the sleep hold is unchanged (section 7.1), which confirms the longest ring did not change.

---

## 5. Modes

### 5.1 Voicing table

[code, `Voicing.hpp`; only a comment changed in 2.0.2] Line lengths are at Size noon; multiply by 1/3 at Size 0 and by 3 at Size 1.

| Parameter | HAAR | FRET | SMOKE | ICE |
|---|---|---|---|---|
| Lines | 16 | 16 | 8 | 16 |
| Base lengths (ms) | 53.1–155.9 (mean 101.1) | 61.3–176.9 (mean 116.3) | 137.9–169.3 (mean 153.6) | 47.3–161.9 (mean 102.4) |
| Range at Size 0 / Size 1 (ms) | 17.7–52.0 / 159.3–467.7 | 20.4–59.0 / 183.9–530.7 | 46.0–56.4 / 413.7–507.9 | 15.8–54.0 / 141.9–485.7 |
| Diffuser stages (spans, ms) | 4 (13, 29, 47, 71) | 4 (17, 37, 59, 89) | 2 (3, 7) | 2 (9, 23) |
| dampBias | 0 | 0.35 | 0 | 0 |
| brightness | 0 | 0 | 0 | 0.5 |
| bloomBias | 0 | 1 (Bloom τ × 2) | 0 | 0 |
| Tone on the feed | none | peak 300 Hz +4 dB Q 0.7 | none | high shelf 3 kHz +3 dB |
| loopDiffuse (in-loop allpass k) | 0 | 0 | 0.6, delays 6.1, 8.3, 10.7, 12.9, 15.1, 17.3, 19.7, 21.9 ms | 0 |
| shimmer (in-loop octave) | 0 | 0 | 0 | 0.16 |
| shimmerOut (feed-forward octave) | 0 | 0 | 0 | 0.35 |
| wetMakeup factor | 1.00 | 1.02 | 0.955 (−0.4 dB) | 0.715 (−2.9 dB) |
| Low-band tap | row 15 | row 15 | row 7 (8 lines) | row 15 |

Full line lists (ms, Size noon):

- HAAR: 53.1, 58.7, 64.3, 70.9, 76.3, 83.1, 88.9, 95.3, 102.7, 109.1, 116.9, 123.7, 131.3, 139.7, 147.1, 155.9.
- FRET: 61.3, 67.9, 74.7, 81.1, 88.3, 95.9, 103.1, 110.9, 118.7, 126.1, 134.3, 142.9, 151.1, 159.7, 168.1, 176.9.
- SMOKE: 137.9, 142.3, 146.9, 151.3, 155.9, 160.1, 164.7, 169.3.
- ICE: 47.3, 53.9, 61.1, 67.7, 74.9, 82.3, 89.9, 97.1, 104.9, 112.3, 120.7, 128.9, 137.3, 145.1, 153.7, 161.9.

The lengths are chosen to share no common factor in samples.

### 5.2 HAAR (default)

- **Emulates** Supermassive's dense, dark modes: a smooth, enormous wash.
- **Why this voicing**: the most lines, deep (4-stage) diffusion and long lines give high echo density quickly with no audible repeats. There is no built-in colour, so the spec's numeric targets for Damp and Bloom are exact in HAAR.
- **Should sound**: a dense, smooth wash that swallows the source, with no distinct echoes, no metallic ring on long tails at Drift noon, and nothing plate-like at noon (a plate sound at noon is a spec failure).
- **Measured**: the 250 Hz T60 is within 1.0 % of Visibility at all six words [test: `t60_matches_visibility`]. At Size 1 an impulse shows 1 envelope peak of 6 dB prominence in 2 s, against SMOKE's 4 [test: `smoke_has_echoes`].

### 5.3 FRET (LV-426)

- **Emulates** Valhalla Room's LV-426 mode: dark and slow to build, with the space rising behind the sound.
- **Why this voicing**:
  - Lines about 20 % longer than HAAR's, and longer diffusion stages (17–89 ms), so the onset smears.
  - dampBias 0.35: Damp acts from a third of the way up, so FRET at Damp noon equals HAAR at Damp 0.675, and FRET at Damp 0 is already HAAR at Damp 0.35 (4 kHz T60 = 0.66 × the 250 Hz T60).
  - bloomBias 1: Bloom's swell takes twice as long.
  - +4 dB at 300 Hz on the feed for low-mid weight.
- **Should sound**: darker than HAAR at the same Damp, warmer in the low mids, and slower to swell in.
- **Measured**:
  - 4 kHz/250 Hz T60 ratio at FOG: FRET 0.666 against HAAR 0.997 at Damp 0, and 0.347 against 0.512 at Damp noon [test: `fret_darker_than_haar`].
  - At T60 6.32 s, Damp 0, Size noon: 2 kHz T60 4.92 s (−22 %) and 4 kHz 4.20 s (−34 %) against 6.32 s at 250 Hz [meas on 2.0.1; the damping path is unchanged]. This is the voicing's dampBias at work [issue #24].

### 5.4 SMOKE (echo-led)

- **Emulates** Supermassive's echo modes and Greyhole: distinct repeats that blur into the wash.
- **Why this voicing**:
  - 8 lines clustered within ±10 % of 153 ms, so each round of the loop comes back as one repeat: about 170 ms apart at Size noon and about 0.47 s at Size 1, counting the allpass delay.
  - Only two short input stages (3 and 7 ms), so the first repeat stays distinct.
  - A Schroeder allpass after each line's read, inside the loop (k = 0.6, 6.1–21.9 ms), blurs every later repeat further. It is lossless, and its delay is counted in the line's T60 gain so the decay time holds.
- **Should sound**: a clear first echo, then repeats that get progressively more diffuse and merge into a wash.
- **Measured**:
  - At Size 1, FOG, Damp noon, Drift 0: 4 repeats in 2 s, at 0.47, 0.92 and 1.39 s for the first three [test: `smoke_has_echoes`].
  - The gap after the second repeat is 33.8 dB deep. The test limit is ≤ 40 dB; without the in-loop diffusion the gaps fall over 100 dB.
  - Bass: SMOKE is the mode most affected by the shared low band's note-to-note unevenness (section 2.10; issue #19).

### 5.5 ICE (shimmer)

- **Emulates** shimmer reverbs: sparse and glassy, with an octave-up halo.
- **Why this voicing**:
  - Lines a little shorter than HAAR's and only two short input stages, for a sparser onset.
  - brightness 0.5 halves the damping at every Damp setting.
  - +3 dB high shelf above 3 kHz on the feed.
  - A +12-semitone shimmer in two parts:
    1. **In-loop crossfade (the "cascade", shimmer 0.16).**
       - The feedback is projected onto a unit vector u (a ±1 Hadamard row over the lines, orthogonal to the Householder's all-ones vector) as m. m is octave-shifted to s, and the u component of the line inputs becomes (1 − a)·m + a·s.
       - s is at each sample a convex combination of two past band-limited samples of m, and the band-limit has gain ≤ 1. So the RMS of the crossfade never exceeds m's, whatever the phase between the octave and the note's harmonics, and no tail can outlast the T60 law.
       - a is set so that the extra loss per pass is 0.16 of the loop's own loss per pass: a = 1 − √(1 − 0.16 × N × (1 − g_m²)), capped at 0.5.
       - Cost: ICE's T60 runs 10–14 % short, and the in-loop octave fades as the tail lengthens.
    2. **Feed-forward octave (shimmerOut 0.35).** Each wet side goes through its own octave shifter once, after the loop and after the low band, and 0.35 × that octave is added to the wet. It is never fed back, so it follows the tail's envelope and stays at a nearly fixed level against the note at every Visibility. It runs at the inner rate, before the make-up.
  - **The shifter** (`Shifter.hpp`, unchanged; three instances: one in the loop and one per wet side):
    - Band-limit: two one-pole high-passes at 80 Hz (keeps the bass out of the octave) and two one-pole low-passes at 5 kHz (−16 dB at a quarter of the rate, to keep the 2× read's aliasing down).
    - A buffer read at twice speed by two taps half a 50 ms grain apart, with triangular windows that sum to 1 and integer reads.
    - Each grain starts at a random extra delay of 0–12 ms (an LCG per shifter, drawn where the tap's window is 0), so the octave's carrier does not cancel at 40n + 20 Hz.
- **Should sound**: a bright, sparse tail with a clear octave above the note, about 10–14 dB under it, at every pitch from 110 Hz to 880 Hz and at every Visibility. The bass (around 30 Hz) should have little octave.
- **Measured**:
  - Octave against the note in the tail, Visibility noon, Damp 0 [test: `ice_octave_every_pitch`]: −14.2 dB at 110 Hz, −11.8 at 147, −11.5 at 196, −10.1 at 220, −9.6 at 260, −10.2 at 300, −10.8 at 349, −11.3 at 440, −10.5 at 523, −11.2 at 659, −11.7 at 880. The median is −11.2 dB and every pitch is within 2.9 dB of it.
  - Against Visibility, 440 Hz [test: `ice_octave_audible_long`]: −11.3 dB at 0.5, −13.3 at 0.75, −13.7 at 1.
  - At Visibility 0 and 0.25 [meas on 2.0.1]: −9.5 and −9.3 dB. While the note is held [meas on 2.0.1]: −13.0 to −14.7 dB at Visibility 0.25–1. Not re-measured on 2.0.2; the test figures above moved by at most 0.5 dB.
  - 30 Hz note: octave −29.1 dB against −11.3 dB at 110–880 Hz [test: `ice_bass_kept_out`].
  - T60 runs short by design: −10.2 to −14.1 % at FOG and DENSE across Size 0–1 [test: `modes_t60`]. Tonal tails at Visibility 1 never decay slower than the law [test: `ice_tonal_decay`, sines 110–3520 Hz, Size 0 and 1, 48 and 96 kHz; slowest −0.757 dB/s against the law's −0.652 dB/s at T60 × 1.15].
  - The crossfade never gains: worst 50 ms output/input RMS 0.836 over 6 frequencies × 24 harmonic phases [test: `shimmer_crossfade_never_gains`].

### 5.6 Mode switching

[code, `FogEngine::process`; unchanged]

1. **On a running module**, a new mode:
   - fades the wet out linearly over 50 ms (2,401 samples at 48 kHz);
   - then, in one sample, clears the rate divider, reconfigures and clears the network (every delay line, the diffuser, the allpasses, the shifters and the low band's filters), snaps the smoothed Drift, Visibility and Damp to their targets, clears Bloom and recomputes the tone stage;
   - then fades the wet in over 50 ms. The new mode starts clean: nothing of the old tail is heard.
   - The dry signal is unaffected throughout.
2. **A further change while fading** waits for the current fade, then the latest mode is taken. Toggling the menu rapidly therefore ends in the last mode chosen [test: `mode_rapid_toggle`, 30 changes in 0.3 s ends in SMOKE with no click].
3. **Bloom restarts from zero at the switch.** After the 50 ms fade-in, the wet re-swells at Bloom's rate. Held noise, HAAR → SMOKE: back within 3 dB of steady 0.28 s after the switch at Bloom 0, and 1.33 s after at Bloom noon [meas on 2.0.1; path unchanged]. Expect about 2 s or more when switching into FRET at Bloom noon. This is expected, not a defect.
4. **A switch while asleep** takes effect silently and the engine stays asleep. The next input wakes it as a fresh engine in the new mode [test: `mode_switch_while_asleep`, output exactly 0 during the switch, then within 0 V of a fresh ICE engine].
5. **Patch load**: the first `process()` takes the saved mode at once, with no fade. A patch saved in ICE runs ICE from sample 0, bit-identical to an engine that was always in ICE [test: `mode_starts_snapped`, difference 0 V].
6. **Initialize** from a non-HAAR mode resets the current mode's network, then fades to HAAR over 100 ms (silently, since the network was just cleared) [code].
7. **Cost**: the switch sample does the clearing and calls libm (tan, exp, pow, cos, sin, sqrt) for the new coefficients [issue #14]. Worst single sample during a HAAR → FRET switch: 17–19 µs at 48 kHz, 28 µs at 96 kHz, 27–29 µs at 192 kHz [meas on 2.0.1]. A 256-sample Rack block lasts 5,333 µs at 48 kHz. 2.0.2 adds the low band's coefficients (a few `tan` calls) to that work; not re-timed.

Click criteria in the tests [test: `mode_switch_click_free`]: during 5 switches on a held 3 V 110 Hz sine, no output step exceeds twice the largest step of the previous 100 ms plus 0.05 V (worst ratio 0.39). After each switch with the input stopped, the old tail is below −60 dB within 100 ms (measured: silent).

---

## 6. Level management

### 6.1 The wet level law (new in 2.0.2)

[code, `FogEngine::wetMakeup`, `FogEngine::process`]

```
matched  = voicingMakeup × (1.125 + v × (0.755 + 0.246 v)) × sizeTerm
sizeTerm = 1 + 0.27 × (0.5 − Size) + 0.11 × (1 − v)³ × max(0, 2·Size − 1)
make-up  = matched × 200^((v − 0.5) / 2)          = matched × √(T60(v) / T60(0.5))
```

- **The law.** At Mix 1, steady noise comes out at **10·log10(T60 / 5.66 s) dB against the input**:
  - 0 dB at noon Visibility (MIST, 5.66 s), as in 2.0.1;
  - −11.5 dB fully left (0.4 s), −5.75 dB at 0.25, +5.75 dB at 0.75, +11.5 dB fully right (80 s);
  - the same at any Size ("Size-neutral"; the sizeTerm keeps the matched part level across Size);
  - the same in every mode, within the voicing factors.
- `matched` is the 2.0.1 make-up, which levels the wet to the dry at every Visibility; the factor 200^((v − 0.5)/2) then tilts it by the T60 ratio. The factor uses `fastExp2` and the rest is a polynomial, so there is no libm.
- **Smoothing and the make-up path.**
  - `v` is the network's smoothed Visibility `visSm` (section 2.4), read the sample it changes, with no smoothing of its own. The network's own wet level √(1 − g_m²) is computed from the same `visSm`, so the total wet gain follows one smooth Visibility.
  - Size reaches the make-up through its own 30 ms one-pole (`makeupCoef` = 1/(0.03 × host rate + 1)), and the make-up is recomputed at most once per 32 samples while that moves.
  - On reset, a wake and a mode switch the make-up snaps to its target.
  - Two earlier forms were tried during development and rejected. A one-pole on the raw knob ramped the make-up linearly while the network followed the smoothed Visibility, giving +10 dB in 5 ms on a 0 → 1 jump. A second one-pole after `visSm` lagged the network, so a 1 → 0 jump swelled the wet 5–8 dB for 50–70 ms [code comment; test: `visibility_no_swell`].
- **Expected full-wet level against dry, by dial word** (Mix 1, Size noon, Damp, Bloom and Drift noon, Low cut 50 Hz). "Quiet material" is ±1 V uniform noise, which the leveller leaves alone. "Loud" is ±5 V uniform noise:

| Word | v | T60 | Law [code] | HAAR make-up [code] | HAAR, ±1 V [meas] | FRET, ±1 V | SMOKE, ±1 V | ICE, ±1 V | HAAR, ±5 V (levelled) [meas] | HAAR ±5 V peak |
|---|---|---|---|---|---|---|---|---|---|---|
| fully left | 0 | 0.40 s | −11.51 dB | 0.299 (−10.5 dB) | −11.51 | −11.11 | −11.83 | −11.70 | −11.51 | 4.44 V |
| CLEAR | 0.042 | 0.50 s | −10.54 | 0.344 (−9.3) | −10.41 | −9.98 | −10.64 | −10.55 | −10.41 | 5.02 V |
| HAZE | 0.304 | 2.00 s | −4.51 | 0.819 (−1.7) | −4.42 | −4.27 | −4.31 | −4.00 | −4.42 | 9.07 V |
| MIST | 0.477 | 5.01 s | −0.53 | 1.450 (+3.2) | −0.53 | −0.74 | −0.50 | −0.11 | −0.54 | 10.00 V |
| noon | 0.5 | 5.66 s | 0.00 | 1.564 (+3.9) | −0.01 | −0.24 | +0.02 | +0.41 | −0.03 | 10.00 V |
| FOG | 0.642 | 12.0 s | +3.27 | 2.493 (+7.9) | +3.26 | +2.97 | +3.28 | +3.60 | −0.02 | 10.00 V |
| THICK | 0.815 | 30.0 s | +7.25 | 4.385 (+12.8) | +7.26 | +7.01 | +7.30 | +7.41 | −0.04 | 10.00 V |
| DENSE | 0.975 | 70.1 s | +10.93 | 7.374 (+17.4) | +10.91 | +10.76 | +10.97 | +10.84 | −0.05 | 10.00 V |
| fully right | 1 | 80 s | +11.51 | 7.995 (+18.1) | +11.46 | +11.33 | +11.53 | +11.36 | −0.05 | 10.00 V |

- **Read the table this way.**
  - On material whose full wet stays under the leveller's 6.5 V threshold (most material at ordinary levels), the level follows the law within about 0.5 dB.
  - On loud material, from about MIST up, the leveller (section 6.2) holds the full wet near the noon level. ±5 V noise reads about 0 dB from noon to fully right, not +3 to +11.5 dB. This is expected.
  - At short settings even ±5 V material follows the law (CLEAR −10.4 dB, peak 5.0 V).
- **Measured against the tolerance** [test: `wet_level_law`, ±1 V noise, 11 Visibility/Size points × 4 modes]:
  - every point within ±1.5 dB of the law; worst 0.54 dB off (FRET, v 1, Size 1);
  - the leveller's gain exactly 1 at every point (so the make-up alone is measured);
  - Size 1 against Size 0 within 0.75 dB in HAAR (measured ≤ 0.28 dB) and ≤ 0.89 dB in every mode;
  - every mode within 1 dB of HAAR at v 0, 0.5 and 1 (measured ≤ 0.41 dB).
- **Against LV-426.** ValhallaRoom LV-426 drops 4.8 dB from a 6.32 s decay to 2.15 s [testbed]; the law gives 4.68 dB. Fog drops 4.58 dB (HAAR), 4.23 (FRET), 4.51 (SMOKE) and 4.58 (ICE) between Visibility 0.521 and 0.317 on noise [test; limit 4.68 ± 1.5 dB]. On the testbed's Fathom drone, which the leveller acts on at 6.32 s, FRET drops 3.7 dB and HAAR only 0.5 dB [test: `fathom_drone` level column]. The 2.0.2 testbed rerun will measure this in Rack.
- **Smoothness** [test: `wet_makeup_smooth`]: on a Visibility 0 → 1 step the make-up (a 7.70 change) moves at most 0.065 in one sample (0.84 % of the step); on a Size 0 → 1 step (0.401), at most 0.0093 (2.3 %). Limit 3 % of the step. Both settle on the target within 1e-3.
- **No swell on a Visibility jump** [test: `visibility_no_swell`, 0.2 V 200 Hz sine, every mode, 0 → 1 held, 1 → 0 held and 1 → 0 in a tail]: the total wet gain never exceeds the larger of its values before and after by more than 1 dB, and the 10 ms envelope never exceeds the larger of its start and end by more than 1 dB. Measured: +0.00 dB worst on both.
- **No step on a Visibility jump** [test: `visibility_step_no_click`, 1 V 200 Hz, Drift 0, every mode, 0 ↔ 1]: the wet's 5 ms RMS envelope moves at most 4 dB from one window to the next (measured 0.84–3.37 dB), and no sample step against its window's RMS exceeds twice the steady ratio plus 0.05. [meas], 1 V 110 Hz, knobs noon, largest sample step in the 200 ms after the jump, every mode: 0.03–0.07 V for 0.5 → 1, 0.05–0.11 V for 1 → 0 and 0.03–0.04 V for 0 → 1 (2.0.1: up to 1.04 V, 8.41 V and 1.32 V). These are the ordinary sample steps of the wet at its new level; on 1 → 0 they are under the steady step before the jump (0.06–0.12 V), and on 0 → 1 they grow with the level, which the law raises by 23 dB.
- **Known limit: a Visibility 0 → 1 jump within about 150 ms of a tail lifts that tail about 6 dB** [issue #22]. The echoes still in the network when the make-up rises by about 11 dB get louder, and the envelope peaks about 6 dB over where it ends. This affects stepped Visibility only (a mapping module, a typed value, a preset load during play), not turning the knob. Not covered by a test.
- **Short sounds now get louder with Visibility.** In 2.0.1 a short sound was quieter at DENSE than at CLEAR, because its energy spread over a longer tail. With the law the steady level rises with T60, and the early response rises with it. A 50 ms ±1 V noise burst, Bloom 0, Mix 1 [meas]:

| Mode | loudest 50 ms of wet re the burst: CLEAR / HAZE / noon / FOG / THICK / DENSE | wet energy in 6 s re the burst: CLEAR / DENSE |
|---|---|---|
| HAAR | −16.0 / −10.7 / −8.3 / −7.1 / −5.9 / −5.0 dB | −12.0 / +8.6 dB |
| FRET | −18.1 / −12.6 / −9.9 / −8.5 / −7.3 / −6.3 dB | −13.3 / +6.8 dB |
| SMOKE | −15.1 / −9.1 / −6.6 / −5.4 / −4.2 / −3.3 dB | −12.2 / +8.8 dB |
| ICE | −16.2 / −10.7 / −8.2 / −7.0 / −5.8 / −4.9 dB | −12.2 / +8.6 dB |

  The same measurement on 2.0.1 gave, in HAAR, −5.4 dB at CLEAR down to −15.9 dB at DENSE. The spec and the manual still describe the 2.0.1 behaviour ("a short sound is quieter at DENSE than at CLEAR"); see section 9.2.

### 6.2 The wet leveller

[code, `FogEngine::level`]

- **Why it exists**: a held tonal drone builds up in the network 4–8 dB over noise, with peaks of 20–50 V at Damp 0 (spec). The user's ruling is that such drones are turned down, not clipped. With the level law, long settings start 3–11.5 dB hotter than in 2.0.1, so the leveller also holds loud material near the noon level.
- **Design**: a slow automatic gain on the wet, after the make-up and before the limiter, with one gain for L and R taken from the louder side.

| Stage | Constant [code] |
|---|---|
| Fast peak follower of max(abs L, abs R) | attack 5 ms, release 300 ms |
| Slow follower of the fast one | **attack 10 ms** (50 ms in 2.0.1), release 3 s |
| Gain | 6.5 V / slow, while slow > 6.5 V; exactly 1 otherwise |
| Feed-absent detector: peak follower of the feed (after Bloom) | attack 5 ms, release 50 ms |
| Hold | while the feed follower < 10 mV (−54 dB re 5 V), the slow follower does not release, so the gain is held |
| Reset | on reset(), a sample-rate change, a mode switch and every wake from sleep: gain 1, followers 0 |

- One-pole coefficients are 1/(t × rate + 1), at the host rate. Followers are flushed below 1e-20. At 384–768 kHz the 3 s release coefficient is about 4e-7 and stops moving within about 0.5 V of its follower, a gain error of about 0.1 dB [issue #15; inaudible].
- **Why 10 ms.** With the law the wet arrives 5.8 dB hotter at Visibility 0.75 and 11.5 dB at 1. With the 50 ms attack, a drone's first seconds put up to 1.23 % of samples over 9 V at Mix noon (HAAR square 220 Hz, v 1: 4.4 % of its first second), over the 1 % bound [test comment; commit e7417bf]. 10 ms brings it back under.
- **The hold**:
  - When a loud drone stops, the gain stays where it was, so the tail decays as it would without the leveller.
  - A stopped ±5 V input counts as absent about 0.3 s later.
  - A new note releases the gain again, at the 3 s rate.
  - The attack is never held: a tail that peaks above the held drone's level is turned down like any other peak. In SMOKE the first repeats after a stop can do this, so SMOKE's gain can step down in the tail, but never up [test: −0.61 dB step].
  - The hold also ends when the engine sleeps.
- **Share over the knee** (the fraction of output samples over the limiter's 9 V knee, where the limiter starts to bend):

| Measurement | 2.0.2 | Limit |
|---|---|---|
| 144 drones (±5 V saw, square, sine at 55/110/220 Hz; every mode; v 0.75 and 1; Size 0.5 and 1; Damp 0), Mix 1 [test: `leveller_turns_drones_down`] | worst 0.18 % (ICE saw 220 Hz, v 1, Size 0.5); peak 10.00 V; 14.4 V before the limiter | ≤ 1 %, ≤ 10 V |
| the same drones at Mix noon [test] | worst 0.627 % (HAAR square 55 Hz, v 1, Size 0.5); peak 10.61 V | ≤ 1 % |
| 288 drones (the above, Damp 0 and 0.5), 10 s, counted from 0.5 s, Mix 1 [meas] | worst 0.088 %, median 0.019 %, none over 1 %; output RMS −7.4 to +0.9 dB re dry | |
| same, Mix noon [meas] | worst 0.314 %, median 0.024 %; peak 10.61 V; output RMS −3.8 to +2.1 dB re dry | |
| ±5 V white noise, Mix 1, knobs noon, every mode, v 0.5 and 1 [meas] | 0.13–0.18 % over 9 V; peak 10.0 V | |
| ±5 V white noise, v 0 [meas] | 0 % (peak 3.8–4.4 V: the law puts the wet 11.5 dB down) | |

- **Measured**:
  - Quiet signals pass untouched: ±1 V sine or noise at Visibility noon, every mode, Size 0.5 and 1, gain exactly 1.000000 [test: `leveller_idle_when_quiet`]. ±1 V noise also passes untouched at every point of the level law, including v 1 [test: `wet_level_law`].
  - ±5 V noise: the gain falls to −0.16 dB at v 0.5, −5.85 dB at 0.75 and −11.69 dB at 1, holding the output at −0.04, −0.02 and −0.03 dB re dry [test: `wet_level_law`].
  - No pumping on a steady drone: Drift 0, settled (20–30 s), the gain moves at most 0.32 dB in any second on the 17 drones it turns down by more than 3 dB (worst HAAR saw 55 Hz v 1, gain −16.4 dB) [test: `leveller_no_pumping`, limit < 1 dB]. None of the 17 is ICE: since the low band, ICE's 55 Hz drones no longer build up on one side.
  - With Drift noon the drone's wet wanders as the moving lines sweep the network's resonances across the harmonics, and the gain follows: up to 5.43 dB in a second at Visibility 1 (SMOKE square 110 Hz) [test; regression guard ≤ 6.5 dB].
  - Tail after the stop, against the same run with the leveller off (limiter off in both): within 0.76 dB from 1 to 13 s, all modes (limit 1.5 dB). A new 1 V note releases a held −18.5 dB gain by +3.1 dB in 6 s [test: `leveller_holds_in_tail`].

### 6.3 The limiter, and the peaks QA should expect

[code, `FogEngine::limit`; unchanged] It acts on the wet only, per side:

```
|x| ≤ 9 V :  y = x
|x| > 9 V :  y = sign(x) × (9 + 1 × tanh(|x| − 9))     → approaches 10 V
```

- With the leveller before it, the limiter catches only transients: the spikes a bright drone's edges make (up to 14.4 V before the limiter [test]), noise's rare peaks and a hot input's onset.
- A large overload reads exactly ±10.000 V in float, which looks like a flat top on a scope.

Expected peak behaviour:

| Situation | Expected |
|---|---|
| Mix 1 or Dry kill, any input | \|out\| ≤ 10.0 V [test: `output_ceiling`, `fuzz`; 80 V saw limited to 10.000 V] |
| ±5 V white noise, Mix 1, knobs noon, Visibility noon or above | peaks reach 10.0 V; 0.13–0.18 % of samples over 9 V [meas] |
| ±5 V white noise, Mix 1, Visibility 0 to HAZE | peaks 3.8–9.1 V; under 0.005 % of samples over 9 V (HAZE), none below it [meas] |
| ±5 V drones, Mix 1 | ≤ 0.18 % over 9 V [test] |
| **Mix noon, ±5 V square drone (worst case)** | **output up to 10.6 V**: a wet spike near 10 V × 0.707 on top of 5 V × 0.707 dry [test: 10.61 V; ≤ 0.63 % of samples over 9 V] |
| Mix < 1 in general | bound is 10.5 V + \|dry\| [test: `fuzz`]; the dry is never limited |
| 16-channel poly input | the dry is the sum of the channels and is not limited |

---

## 7. CPU

### 7.1 The silence sleep

[code; unchanged]

- **QUIET threshold**: 1e-5 V (−114 dB re 5 V), applied to both the feed (after low cut, tone and Bloom) and the wet (after the make-up, before the leveller).
- **Hold**: the engine falls asleep after both have stayed under QUIET for

  ```
  hold = max(0.25 s, 2 × longest ring)
  ```

  where the longest ring is the longest path a sample can circulate before being overwritten (the longest line at Size 1 plus the Drift swing, plus an allpass and two shifter buffers). This is set by the buffers, not the current mode or Size.
- **Measured hold: about 1.4 s at every rate** [meas; test: `sleep_after_hold`, exactly 67,296 samples at 48 kHz]:

| Host rate | Hold (samples) | Hold (s) |
|---|---|---|
| 44.1 kHz | 61,830 | 1.4020 |
| 48 kHz | 67,296 | 1.4020 |
| 88.2 / 96 kHz | 123,622 / 134,554 | 1.4016 |
| 176.4 / 192 kHz | 247,244 / 269,108 | 1.4016 |
| 352.8 / 384 kHz | 494,488 / 538,216 | 1.4016 |
| 705.6 / 768 kHz | 988,976 / 1,076,432 | 1.4016 |

  The 0.25 s floor never applies at these rates. The manual's "about 1.4 s (at 48 kHz, in every mode)" is correct at every rate as well. The hold's divider-factor branch above 96 kHz has no automated test [issue #25].
- **Asleep**:
  - The divider and network are skipped and the wet is exactly 0.
  - Low cut, tone, Bloom, the mode fade and the mix still run, so the dry passes normally.
  - Buffers are not cleared, since their contents are already below QUIET.
- **Wake**:
  - Any feed sample with |x| ≥ QUIET wakes the engine.
  - The controls are applied at once: Size snaps (no swoop from the Size of the last awake sample), the smoothed Drift depth, Visibility and Damp snap to their targets, ICE's shifters restart and the low band's filters are cleared.
  - The output is the same as a fresh engine's, sample for sample, within 2e-5 V in every mode [test: `wake_transparent`, worst 2.3e-6 V], and also after Size was moved while asleep [test: `wake_at_new_size`].
  - reset(), a new rate and a mode change made while awake also wake it.
- **It does not sleep inside an audible tail** [test: `no_sleep_in_tail`]: DENSE at Size 1 after a 1 s burst slept at 165.1 s; CLEAR at Size 1 after one impulse slept at 2.03 s; each at least the hold after the last sample the test counts as audible. The step at the moment of sleeping is 1.7e-7 V [test: `sleep_no_click`].
- **Time to sleep after a 1 s ±5 V noise burst**, Mix 1, knobs noon [meas]:

| | v = 0 (0.4 s) | v = 0.5 (5.66 s) | v = 1 (80 s) |
|---|---|---|---|
| HAAR | 2.2 s | 11.1 s | 144 s |
| FRET | 2.2 s | 10.9 s | 142 s |
| SMOKE | 2.2 s | 10.9 s | 143 s |
| ICE | 2.1 s | 9.9 s | 125 s |

  At Visibility 1 this is about 16 s longer than in 2.0.1 (128 s in HAAR), because the wet now starts 11.5 dB higher and takes longer to fall under QUIET, which is measured after the make-up.
- **What QA should see on Rack's CPU meter**: while audio or a tail is present, the awake cost. Once the input and tail are silent, the meter should fall to the idle level after the times above. Whether a constant input keeps Fog awake depends on Bloom's gate, which sees the signal after the low cut [code; not measured in Rack]:
  - A signal that still exceeds 1 mV after the low cut (a noisy source, say) keeps Bloom open and Fog awake.
  - A DC offset is removed by the low cut, so with the low cut on it does not keep the engine awake.
  - With Low cut Off, a DC offset above 1 mV keeps the engine awake indefinitely. Below 1 mV, Bloom's gate closes and the engine sleeps.
  - The bench's idle cost is 0.06 % of a core (section 7.3). Rack's meter also includes Rack's own per-module overhead, so its numbers will not equal the bench's [unverified in Rack].

### 7.2 The cache-stagger fix

[code, `DelayLine::allocate`; since 2.0.0]

- All the delay buffers are page-aligned powers of two. Starting every write position at 0 made the lines write to the same L1 cache set and evict each other.
- Each buffer starts at stagger × 67 × 16 floats (masked), with a distinct stagger index per line: diffuser 0–31, FDN 32–47, SMOKE allpasses 48–55, ICE shifters 56–58.
- The output is unchanged; only the cache behaviour differs.
- The 59 lines occupy 59 distinct L1 sets, also after a clear [test: `lines_use_distinct_cache_sets`]. The low band adds no lines.

### 7.3 Bench figures and budgets

`bench` cases [code, `bench.inc`, unchanged], each at 48 kHz:

- **idle**: no cables, knobs at defaults (asleep).
- **outputs**: the same, with the outputs patched.
- **quiet**: ±3 mV noise into both inputs, which keeps the engine awake.
- **busy**: stereo ±5 V noise, all five CVs moving at full attenuverter, Visibility 0.8, Drift 0.8, Bloom 0.5, Mix 0.5, HAAR.
- **busyIce**: busy in ICE.

| Case | Budget, Apple arm64 | Budget, other machines (provisional) | TESTING.md (written for 2.0.1) | Measured by me, 2.0.2, 1 / 16 instances [meas] | Same run, 2.0.1, 1 / 16 [meas] |
|---|---|---|---|---|---|
| idle | 0.15 | 0.45 | 0.06 | 0.094 / 0.063 | 0.082 / 0.063 |
| outputs | 0.15 | 0.45 | 0.06 | 0.064 / 0.064 | 0.064 / 0.063 |
| quiet | 0.95 | 2.85 | 0.57–0.66 | 0.665 / 0.747 | 0.601 / 0.687 |
| busy | 1.6 | 4.8 | 0.78 (0.85 at 16) | 0.915 / 0.981 | 0.766 / 0.834 |
| busyIce | 1.65 | 4.95 | 0.83 (0.86 at 16) | 0.936 / 0.974 | 0.810 / 0.843 |

- Units: % of one core per instance. My run: Apple M-series Mac, both builds run back to back, 5 s per case, other work running (load average about 2.2–2.6). The idle single-instance figures (0.082, 0.094) are noise at that level; the 16-instance ones agree at 0.063.
- **Awake cost rose about 10 % (quiet) to 19 % (busy) from 2.0.1** and is within every budget. The likely causes are the low band's double-precision filters per inner sample and the Drift depth smoother, which runs every sample while the bench's Drift CV moves (and, by issue #11, keeps running after any Drift change). That attribution is my reading of the code, not separately profiled [unverified].
- The bench fails if a case exceeds its budget at 1 or 16 instances, or if the 16-instance cost exceeds 1.5 × the single cost plus 0.01 %.
- The spec's target is busy under 3 % of an M-series core.
- The x86-64 budgets are 3 × the Mac's and have not been measured on native x86-64 hardware. **Windows CPU figures are not covered by any automated budget.**

Per-sample cost against sample rate [meas], noise at Mix 0.5, defaults, HAAR, timed in-process:

| Host rate | 2.0.2, % of a core | 2.0.1, same run |
|---|---|---|
| 48 kHz | 0.67 | 0.61 |
| 96 kHz | 1.37 | 1.24 |
| 192 kHz | 1.69 | 1.55 |

Above 96 kHz the network runs at 96 kHz, so the cost grows only by the divider's filters.

The libm check (`make libm-calls`, Linux only) requires zero libm calls per sample in the idle, outputs and busy cases. I did not run it (macOS). The low band's `tan` calls are in `setLowBand()`, which runs on configure and rate changes, not per sample [code]. The only libm on the audio thread is the one-off coefficient work on a mode switch or reset (section 5.6) [issue #14].

---

## 8. Sample-rate handling and robustness

### 8.1 Sample rates

[code, `FogEngine::setSampleRate`; unchanged]

- Accepted: any finite rate with 0 < rate ≤ 768 kHz (the tests run 1 Hz to 768 kHz).
- **Ignored, keeping the last good rate**: non-finite, ≤ 0, or > 768 kHz. Before any good rate the engine runs at 48 kHz, and the module constructs at Rack's current rate.
- A rate change reallocates (off the audio path in Rack, through `onSampleRateChange`), resets the engine and clears the tail, including the low band's filters, whose coefficients are recomputed for the new rate.
- A repeat of the current rate is a no-op.
- Measured T60 with a 0.2 s burst, HAAR, Size noon [meas on 2.0.1; the T60 path is unchanged apart from the Visibility smoother, which snaps on reset]:

| Rate | HAZE (2.00 s) | FOG (12.0 s) |
|---|---|---|
| 44.1 kHz | 2.02 s | 12.34 s |
| 96 kHz | 2.00 s | 12.04 s |
| 192 kHz | 2.04 s | 12.30 s |
| 384 kHz | 1.98 s | 12.30 s |
| 768 kHz | 1.97 s | 12.15 s |

- After a sweep through 1 Hz…768 kHz and back, and after 44.1 → 96 → 48 kHz with a tail ringing, the 48 kHz T60 is 1.992 s against the 1.961 s expected (+1.6 %) [test: `edge_sample_rates`, `rate_change_mid_tail`, limit ±15 %].

### 8.2 Hostile input and numerical safety

- **NaN, ±infinity** on IN L, IN R, any CV, any parameter, or all at once, with mode, Drift rate and Low cut also set out of range: the outputs stay finite, and after 1 s of clean input the reverb works again [test: `nan_inputs`]. Each is replaced by 0 where it enters [code].
- **Huge input**: the network zeroes any sample with |x| ≥ 1e9 V [code]. The wet stays ≤ 10 V [test: 80 V saw]. The dry is passed as is.
- **Out-of-range menu values** in the frame are sanitised: mode outside 0–3 runs HAAR, Low cut outside 1–2 is Off, and any nonzero Drift rate is Very slow [code].
- **Denormals**: every filter state, follower and delay write is flushed to 0 below 1e-20 (the low cut below 1e-30), including the low band's. After 2 s of noise and 120 s of silence, in every mode at v 0 and 1, no output sample and no stored state value is denormal [test: `no_denormals`].
- **No allocation** in `process()` [test]. Only `setSampleRate()` allocates.
- **No writable global state**, so instances are independent (`make engine-globals`; not run by me).
- **Seeded fuzz**: 200 trials, 9.78 million samples, random knobs, attenuverters, modes, menus, 0–16 channels on every input, rates 44.1–192 kHz [test: `fuzz`]. Outputs were finite and within 10.5 V (Mix 1) or 10.5 V + |dry|, with a worst margin of 0.500 V.
- **Soak** (`make fog-soak`, not in `make test`): every mode at v = 1 and three corners of Size, Damp and Drift, 10 minutes of ±5 V noise then 3 minutes of silence, Mix 1. It checks |out| ≤ 10.5 V and that the tail falls at least as fast as the T60 law allows at T60 × 1.15. Result of my run: see the note at the end. The soak's own checks miss a NaN on one channel and measure RMS on L only [issue #16].

---

## 9. Open issues (GitHub #11–#27)

### 9.1 Filed issues: do not re-file

These are the open issues in `ryanarneson/RkaModules` at the time of writing. They replace the 2.0.1 reference's "known open issues" list. **Do not re-file any of them.** When you reproduce one, reference its number in your report (for example "reproduces #22 at 192 kHz"), and add new data to the existing issue rather than opening a new one.

| # | Title | QA impact |
|---|---|---|
| 11 | Fog: Drift depth (and Size make-up) one-pole never settles, so per-line work runs forever | Inaudible. After any Drift change the per-line depth update runs every sample while awake, so awake CPU can read slightly higher after Drift has been moved. |
| 12 | Fog: JSON load narrows integers before clamping (mode, driftRate, lowCut) | Hand-edited JSON with huge integers or reals loads a wrong but valid value (`2.0` → 0; `4294967297` → FRET). Ordinary out-of-range integers still clamp. |
| 13 | Fog: bypass leaves OUT R silent when only IN L is patched | Expected in 2.0.2: bypass with a mono source on IN L gives sound on OUT L only. |
| 14 | Fog: mode change runs libm and clears buffers inside process() | A one-off CPU spike at each mode switch (17–29 µs measured on 2.0.1). Listen and watch the meter, but a single spike is known. |
| 15 | Fog: leveller 3 s release loses precision at 384–768 kHz | About 0.1 dB leveller gain error at those rates; inaudible. |
| 16 | Fog: soak test misses a NaN on one channel and measures RMS on L only | Test-side only. QA's own NaN and level checks should examine both outputs. |
| 17 | Tests: Fog's four slowest suites take ~110 s on M-series; tier them for CI | None for QA. |
| 18 | make smoke does not load the factory presets | The presets have been checked only structurally by the developer. QA's load of each preset in Rack is the first real check (section 11.4). |
| 19 | Fog stereo: Drift 0 bass lumpiness and the 150–300 Hz bounds are regression guards, not targets | Per-side bass level varies more from note to note, most at Drift 0 (spread 5.8 dB, dips to −30 dB). SMOKE's Fathom drone at 2.15 s is about 6 dB quieter. A listening check is wanted: report what you hear on #19. |
| 20 | Fog docs: spec and manual inconsistencies | Manual wording on Size's slew and Bloom; no Sleep section in the spec; ICE called "faint". Section 1.1. |
| 21 | Fog: early reflections per voicing (onset 0.18 s vs LV-426's 0.02 s) | Onset delay unchanged from 2.0.1: first wet 50–139 ms, −20 dB at 0.08–0.16 s at Bloom 0 (section 2.7). Not implemented in 2.0.2. |
| 22 | Fog: Visibility 0→1 within ~150 ms of a tail lifts the tail ~6 dB | A stepped Visibility jump from 0 to 1 just after a note ends makes the remaining tail swell about 6 dB. Knob turns are not affected. |
| 23 | Fog: tune all four modes and the factory presets by ear | Modes and presets are not yet tuned by ear. Report character, loudness and preset impressions on #23, not as defects. |
| 24 | Fog vs LV-426: remaining testbed gaps (attack, treble decay, R−L balance) | Attack at Bloom 50 % (1.7 s / 1.2 s vs 0.65 s), FRET treble decay at Damp 0 (2 kHz/500 Hz 0.83 vs 0.99), and the testbed's R−L imbalance (+4.1 to +4.2 dB) are known. |
| 25 | Fog: test debt carried from the build (deferred minors) | Lists corners with no automated test, among them Damp across a mode change, the sleep hold above 96 kHz, and the Visibility 0 → 1 tail lift. Worth covering by hand (section 11). |
| 26 | Fog: code cleanups and small enhancements carried from the build | Includes the open decision on polyphonic CV (channel 1 only today) and the shelf's 7 dB-per-pass floor. No behaviour change expected in 2.0.2. |
| 27 | QA docs on the share describe 2.0.1; update for 2.0.2 | This document and the changes document. |

### 9.2 Found while preparing this document (now filed)

These were found while this document was written and have since been filed; do not re-file them:

1. **The manual and spec describe 2.0.1's level against Visibility for short sounds.** Both say "a short sound (a ping, a pluck) is quieter at DENSE than at CLEAR". In 2.0.2 the reverse holds: the loudest 50 ms of a burst's wet is about 11 dB louder at DENSE than at CLEAR (section 6.1). Documentation only. Filed as #29.
2. **The factory preset files say `"version": "2.0.1"`.** Harmless in the engine; whether Rack shows or logs anything for it is [unverified]. Filed as #30.
3. **The manual's CV wording** ("at full, ±5 V sweeps the knob's whole range") holds only from noon (section 2.3). The same wording is in the spec. Filed as #31.
4. **Awake CPU is about 10–20 % higher than 2.0.1** (busy 0.77 → 0.92 % of a core, within every budget; cause not profiled). Filed as #32.

### 9.3 Known limits that are not defects

- **ICE (and SMOKE) at CLEAR with Size 1 measure a short decay.** At Size 1 the lines are 140–530 ms long, so a 0.5 s tail is over in one to three passes, a sparse train of reflections that a T30 fit misreads. The test suite excludes this case. [meas on 2.0.1], 0.2 s burst: ICE −14.5 %, SMOKE −34.1 %, HAAR +7.8 %, FRET +17.0 %. Issue #20 records the ruling that the spec should note this.
- **The level now rises with decay time.** It no longer matches LV-426's "level does not scale" finding of the 2.0.0 and 2.0.1 testbeds; that was the intended change.

### 9.4 Testbed status

- The 2.0.1 macOS rerun (`fog-testbed/runs-2.0.1/RESULTS.md`, FRET against LV-426) found, among other things: L/R correlation −0.70 at 6.32 s and −0.89 at 2.15 s, mono-sum loss 7.3 and 12.5 dB; level −9.4 dBFS at 6.32 s and −9.0 at 2.15 s (LV-426 falls 4.8 dB); attack 1.7 s and 1.2 s at Bloom 50 %; onset 0.19 s to −20 dB; peaks −0.4 to −1.5 dBFS with nothing at the ceiling.
- What the engine predicts for the 2.0.2 rerun (FRET, Fathom drone) [test: `fathom_drone`]: correlation about +0.16, mono-sum loss about 2.4 dB, sides level; the level drop from 6.32 s to 2.15 s about 3.7 dB on that drone (4.2 dB on noise). Attack and onset should be unchanged. These are predictions, not testbed results.
- **The 2.0.2 testbed results are in `/Volumes/share1/VCVRACK/fog-testbed/runs-2.0.2/RESULTS.md`.** Use it to judge the testbed items (summary near the top of this document).

---

## 10. What the automated suite already covers

All tests below passed on the 2.0.2 sources in my run. "Network" tests drive `fog::Network` directly (no Bloom, low cut or make-up); "engine" tests drive the full `FogEngine` at 48 kHz unless stated. New or changed tests in 2.0.2 are marked **new** or **changed**. `test_blocks`, `test_network`, `test_modes` and `test_hostile` are byte-identical to 2.0.1; their printed figures moved slightly where the engine changed.

### 10.1 test_blocks (delay line, diffuser, rate divider)

| Test | Guarantees | Tolerance |
|---|---|---|
| delay_integer_exact | integer delays 1–999 return the sample exactly | exact |
| delay_fractional_sine | Hermite read of a 1 kHz sine at d + 0.37 | < 1e-3 |
| delay_clamps | reads below 1, above max, NaN stay finite and clamp | exact |
| delay_power_of_two | buffer is a power of two, ≥ requested | exact |
| diffuser_energy | 4-stage diffuser preserves energy | RMS ratio within 1 % |
| diffuser_decorrelates | an impulse spreads over all 8 channels | each 5–25 % of energy; total 1 ± 0.01 |
| rate_factor | factor 1,1,1,2,4,8 at 44.1–768 kHz; inner calls = n/factor; inner ≤ 96 kHz | 5 kHz passband within 0.1 dB |

### 10.2 test_network (HAAR network)

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| measure self-check | the T60 meter reads synthetic decays (0.5, 12, 70 s) and a two-band decay | < 3 %; two-band 5 % / 10 % |
| lines_use_distinct_cache_sets | 59 lines in 59 distinct L1 sets, also after clear | exact |
| t60_matches_visibility | 250 Hz T60 at the six words | ±15 % (≤ 1.0 %) |
| t60_holds_across_size | FOG at Size 0.25, 0.5, 0.75 | ±15 % (≤ 0.4 %) |
| t60_low_holds_with_damp | 250 Hz T60 at Damp 1 (HAZE; MIST at Size 1) | ±15 % (≤ 1.0 %) |
| damp_ratio | 4k/250 T60 ratio at Damp 0, 0.5, 1 | ≥ 0.8; 0.35–0.65; ≤ 0.18 (0.980, 0.508, 0.131) |
| even_wet_level | pink-noise wet at each word against noon and against input (network only, no make-up) | ±3 dB (0.7–1.3 dB under input) |
| stereo_decorrelated | L-only impulse fills R; correlation | R within 3 dB of L; corr < 0.5 (0.042) |
| always_decays | v 1, Damp 0, Drift 1: 8 × 5 s windows after noise | each lower; last ≥ 20 dB down (28.7) |
| drift_zero_is_static | Drift 0 is time-invariant | bit-identical |
| drift_moves | Drift 1 widens a 1 kHz tone's spectrum | > 2 × Drift 0 and > 2 Hz (26.4 Hz) |
| drift_depth_per_rate | Drift 1 peak pitch deviation; reads stay in the line | 20–100 cents (43.0, 25.3) |

### 10.3 test_engine (full engine)

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| bloom_rise_times | −3 dB rise at Bloom 0, 0.5, 1 | < 50 ms; 0.7–1.3 s; 2.8–5.2 s |
| dry_path_zero_delay | sample 0 of a step is cos(π/4) at 48/192/384 kHz | 1e-6 |
| mix_law | Mix 0 bit-exact dry; Mix 1 no dry; Mix 0.5 = cos·dry + sin·wet | exact; exact; 1e-5 V |
| dry_kill_overrides_cv | Dry kill wins over Mix 0 and −5 V CV | exact |
| cv_law | knob + cv/10 × att, clamped; unplugged CV ignored | exact |
| mono_normals | IN L alone fills both wet sides; dry R = dry L | both > 0.5 V RMS (2.69, 2.73) |
| poly_input_bounded | 16-ch ±5 V squares, v 1, 20 s | ≤ 10.5 V |
| unplug_keeps_tail (**changed**: input clipped at ±0.6 V, as the wet is now 2.6 dB over the dry at FOG) | unplugging IN L keeps the tail, no click | step < 1 V and ≤ 1.1 × plugged slope (0.842 vs 0.856 V) |
| rate_ignored | 0, −48 k, 1e6, 768001, inf, NaN ignored | exact |
| mix_ramp_no_click | Dry kill toggled on 5 V DC | step ≤ 0.05 V (0.033) |
| low_cut | 30 Hz wet with the 50 Hz cut; dry untouched | ≤ −6 dB (−9.4); bit-exact dry |
| **wet_level_law** (**new**, replaces wet_matched_level) | ±1 V noise at Mix 1, 11 points × 4 modes, against 10·log10(T60/5.66 s); leveller idle; Size tilt (HAAR); modes against HAAR; LV-426 drop 6.32 → 2.15 s | ±1.5 dB (worst 0.54); gain exactly 1; ±0.75 dB; ±1 dB; 4.68 ± 1.5 dB (4.23–4.58) |
| wet_makeup_smooth (**changed** bound) | Visibility/Size 0 → 1 step: per-sample make-up move; settles | ≤ 3 % of the step (0.84 %, 2.3 %); within 1e-3 |
| **drift_step_no_click** (**new**) | 1 V 220 Hz, every mode: Drift 0.5 → 0, Slow → Very slow, Very slow → Slow | largest step in the second after ≤ 2 × the second before + 0.05 V |
| **visibility_step_no_click** (**new**) | 1 V 200 Hz, Drift 0, every mode, Visibility 0 → 1 and 1 → 0 | 5 ms envelope ≤ 4 dB per window (≤ 3.37); step/RMS ≤ 2 × steady + 0.05 |
| **visibility_no_swell** (**new**) | 0.2 V 200 Hz, every mode: 0 → 1 held, 1 → 0 held, 1 → 0 in a tail | total wet gain and 10 ms envelope ≤ +1 dB over max(before, after) (+0.00 dB) |
| **damp_step_no_click** (**new**) | 1 V 220 Hz, Drift 0, every mode, Damp 0 → 1 and 1 → 0, held and in a tail | step after ≤ 2 × before + 0.05 V (worst ratio 2.21) |
| level_across_mix | noise loudness at Mix 0–1, Visibility noon | ±2 dB (≤ 0.04) |
| output_ceiling | saw/square/sine/noise at the most resonant setting; 80 V saw | ≤ 10 V; in (8, 10] V |
| leveller_turns_drones_down (**changed**: Mix noon bound 1 %, after a 1.5 % interim) | 144 drones, Mix 1 and Mix 0.5 | ≤ 1 % over 9 V (0.18 %, 0.627 %); peak ≤ 10 V at Mix 1 |
| leveller_no_pumping (**changed**: engaged-drone list re-derived, 17 drones) | steady drones; Drift-noon guard | < 1 dB per s (0.32); ≤ 6.5 dB per s (5.43) |
| leveller_idle_when_quiet | ±1 V signals | gain exactly 1 |
| leveller_holds_in_tail (**changed**: limiter off in both passes; SMOKE gain may step down, never up; "never rises" instead of "constant") | tail against unlevelled, 1–13 s; held gain; release on a new note | ±1.5 dB (0.76); never rises; ≥ +2 dB in 6 s (+3.1) |
| leveller_resets | clean after reset, rate change, mode change, wake | exact |
| sleep_after_hold | sleeps after exactly the hold, ≥ 0.25 s; asleep output 0; sub-QUIET does not wake, 1 V does | exact (67,296 samples) |
| no_sleep_in_tail | DENSE and CLEAR at Size 1 sleep only after the hold past the last audible sample | exact |
| sleep_no_click | step at the sleep transition | < 2e-5 V (1.7e-7) |
| wake_transparent (×4 modes) | output after a wake equals a fresh engine | < 2e-5 V (≤ 2.3e-6) |
| wake_at_new_size (×4 modes) | Size changed while asleep, then wake, equals a settled engine | < 2e-5 V |

### 10.4 test_modes (all four voicings)

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| modes_t60 | 250 Hz T60 at FOG (Size 0, 0.5, 1) and DENSE (Size 0), every mode | ±20 % (HAAR/FRET/SMOKE ≤ 3.0 %; ICE −10.2 to −14.1 %) |
| modes_knob_directions | Damp 0→1 shortens the 4 kHz T60; Bloom 0→1 lengthens the rise | ≤ 70 %; ≥ 3 × |
| fret_darker_than_haar | 4k/250 ratio at Damp 0 and noon | FRET ≤ 0.85 × HAAR (0.666 vs 0.997; 0.347 vs 0.512) |
| smoke_has_echoes | repeats at Size 1; blur between repeats | ≥ 3 peaks of 6 dB in 2 s (4); HAAR fewer (1); gap ≤ 40 dB (33.8) |
| ice_shimmer | 440 Hz in ICE's tail of a 220 Hz note | ≥ +20 dB over HAAR |
| ice_octave_every_pitch | 11 pitches 110–880 Hz | within 6 dB of median (2.9); ≥ 30 dB over HAAR |
| ice_octave_audible_long | 440 Hz note at v 0.5, 0.75, 1 | −13 ± 4 dB; falls ≤ 4 dB; ≥ 20 dB over HAAR |
| shimmer_crossfade_never_gains | 6 tones × 24 harmonic phases, a = 0.5 | 50 ms RMS ratio ≤ 1 (0.836) |
| ice_bass_kept_out | 30 Hz octave against the median | ≥ 8 dB lower (17.8) |
| ice_tonal_decay | sines 110–3520 Hz, v 1, Size 0/1, 48/96 kHz: lines and output | decay ≥ law at T60 × 1.15 |
| mode_switch_click_free | 5 switches on a held sine; old tail after each switch | step ≤ 2 × prior max + 0.05 V (0.39 of it); ≤ −60 dB in 100 ms |
| mode_starts_snapped | first frame's mode taken at once | < 1e-6 V (0) |
| mode_switch_while_asleep | switch while asleep: silent, stays asleep, then equals fresh ICE | 0 V; < 2e-5 V |
| mode_rapid_toggle | 30 changes in 0.3 s | no click; latest mode wins |
| mode_return_starts_clean | ICE → HAAR → ICE replays nothing | exactly 0 |
| all_modes_safe always_decays | noise, 110 Hz saw, 880 Hz sine, v 1, every mode | monotonic; ≥ 20 dB in 40 s |
| all_modes_safe poly_input_bounded | 16-ch squares, every mode | ≤ 10.5 V |

### 10.5 test_hostile (built with ASan and UBSan)

| Test | Guarantees | Tolerance |
|---|---|---|
| nan_inputs | NaN/±inf on each input, CV, param and all at once; recovery | finite; wet energy after recovery |
| edge_sample_rates | 1 Hz…768 kHz finite; bad rates ignored; 768 kHz divides by 8; T60 at 48 kHz after | ±15 % (+1.6 %) |
| rate_change_mid_tail | 44.1 → 96 → 48 kHz over a tail | finite; T60 ±15 % |
| size_cv_sweep_is_safe | ±5 V 10 Hz square on Size CV at full att, Drift 1, both rates | ≤ 10.5 V (10.000) |
| no_allocation_in_process | every mode, moving knobs/CVs/menus, mode changes | 0 allocations |
| no_denormals | 120 s silence after noise, every mode, v 0 and 1 | no value in (0, 1e-30) |
| fuzz | 200 seeded trials, 9.78 M samples | finite; ≤ 10.5 V (+ \|dry\|) |

### 10.6 test_stereo (new in 2.0.2: the low band)

Built with the plugin's flags and part of `make test`. Every run: mono or stereo source at 3 V peak, gated, Mix 1, Low cut Off, other knobs noon unless stated; statistics in double precision. Targets are LV-426's on the same input [testbed]: correlation −0.2 to +0.5 while sustained, mono-sum loss ≤ 4 dB, tail correlation −0.3 to +0.4, mono-input sides within 1.5 dB.

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| mono_targets | every mode, v 0.3 and 0.52; mono 33 Hz sine, 65 Hz sine, noise: sustain and tail | the targets (33 Hz +0.16, 2.3–2.4 dB; 65 Hz +0.29–0.30, 1.9 dB; noise ~0, 3.0–3.1 dB; sides within 0.1 dB) |
| stereo_inputs | every mode, v 0.3 and 0.52; two independent noises; 33 Hz sine with its quadrature on R | sustain and tail targets, mono-sum ≤ 4 dB (noises −0.02 to +0.00; quad +0.16) |
| fathom_drone | the testbed's Fathom drone (FathomEngine, `base.json` settings), gated 10.13 s, v 0.317 and 0.521, every mode | sustain and tail targets (+0.16 to +0.18; 2.3–2.4 dB) |
| sweep | mono sine every 5 Hz from 20 to 100 Hz, then every 10 Hz to 450 Hz, every mode, v 0.3 and 0.52, Drift noon and Drift 0 | Drift noon ≤ 200 Hz: targets at every point (0 of 216 outside); Drift noon 210–450 Hz: corr −0.45 to +0.6, mono ≤ 5.5 dB, sides ≤ 6 dB (−0.37 to +0.52, 5.0, 5.1); Drift 0 ≤ 140 Hz: targets at every point (0 of 168 outside); Drift 0 150–300 Hz: ≥ 30 % meet them (41 %) |
| bass_evenness (in sweep) | note-to-note level of the summed sides, 20–100 Hz | mean spread ≤ 3.6 dB at Drift noon (3.2), ≤ 6.2 dB at Drift 0 (5.8); worst dip above −13 dB (−11.5) and −33 dB (−30.4). A regression guard, not a quality target [issue #19] |

### 10.7 Not covered by the automated suite

- Anything in Rack itself: the panel, the menu, the tooltip, save/load through Rack, bypass, **loading the factory presets** [issue #18], Rack's sample-rate change event order, Rack's CPU meter, and polyphony as Rack delivers it.
- Windows and Linux: the suite and bench run on the build machine. The libm check runs only on Linux.
- CPU on x86-64 or Windows.
- Listening: metallic ring, flutter, the character of each mode, whether HAAR sounds like a plate, the Drift 0 bass evenness [issue #19], and the modes' and presets' tuning [issue #23].
- Onset and attack against a reference [issues #21, #24].
- A Visibility 0 → 1 jump in a tail [issue #22]; Damp across a mode change; the sleep hold above 96 kHz [issue #25].
- No golden saved-state image: the JSON keys are not pinned by a test; the JSON edge cases are not tested [issue #12].

---

## 11. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, all knobs at noon, attenuverters 0, Mode Haar, Drift rate Slow, Low cut 50 Hz, Dry kill off.

**Method notes.**
- For decay measurements, use a burst of at least 0.2 s, not a one-sample impulse. Bloom admits only about 1/480 of a one-sample impulse even at Bloom 0, so the tail starts near the sleep threshold. In my 2.0.1 runs a one-sample 10 V impulse read the HAAR DENSE T60 21 % short at 48 kHz and the MIST T60 47 % short at 768 kHz, purely from that.
- Set Low cut Off for measurements below 100 Hz.
- **For level measurements, state the input level.** On quiet material (wet peaks under about 6.5 V; ±1 V noise is safe at every setting) the full wet follows the law of section 6.1. On loud material (±5 V noise, hot drones) from about MIST up, the leveller holds it near the noon level.
- Measure stereo level per channel and as a mono sum; both now have expected values (section 2.10).
- For control-step tests, look at the 200 ms after the change, not only the first 20 ms: a Damp step in 2.0.1 came back one line length later.

### 11.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Visibility tooltip | hover at each dial word | reads 0.5, 2.0, 5.0, 12, 30, 70 s (± display rounding) at the word positions; 0.4 s fully left, 80 s fully right, 5.66 s at noon |
| Visibility decay | 0.2 s ±5 V noise burst, Mix 1, Bloom 0, Drift 0, Low cut Off; T30 at 250 Hz | within ±15 % of 0.4 × 200^v in HAAR, FRET, SMOKE; ICE 10–15 % short; CLEAR at Size 1 excluded (section 9.3) |
| **Visibility level law** | ±1 V noise, Mix 1; RMS out against in at each word and fully left/right, every mode | within ±1.5 dB of the law column of section 6.1 (−11.5, −10.5, −4.5, −0.5, 0, +3.3, +7.3, +10.9, +11.5 dB); measured within 0.5 dB |
| Visibility level, loud | ±5 V noise, Mix 1, HAAR | follows the law up to HAZE (−11.5 to −4.4 dB); about 0 dB (−0.6 to 0.0) from MIST to fully right; ≤ 0.2 % over 9 V |
| **Visibility jump** | 1 V 110 Hz sine, Mix 1; jump Visibility 0.5 → 1, 1 → 0, 0 → 1 in one step (mapping module or typed value) while the note holds | no click; the level glides over about 100 ms to its new value; no swell above the larger of the start and end levels; no lone sample step out of line with the wet's own steps at the new level (measured ≤ 0.11 V; 2.0.1 up to 8.4 V on 1 → 0) |
| Visibility jump in a tail | as above, but 0 → 1 within 150 ms after the note stops | **known: the tail lifts about 6 dB** [issue #22]; record the figure on #22 |
| Size | FOG; Size 0, noon, 1 | T60 unchanged (±15 %); level unchanged (within 1 dB on ±1 V noise); space grows; moving Size bends pitch and settles in about 100 ms; no clicks on a hand turn |
| Damp | HAAR, FOG; Damp 0, noon, 1 | 4 kHz T60 / 250 Hz T60 ≈ 1, 0.5, 0.125 (test bands 0.8+, 0.35–0.65, ≤ 0.18); 250 Hz T60 unchanged |
| **Damp step** | 1 V 110–220 Hz sine, Mix 1; Damp 0 → 1 and 1 → 0 in one step, held and 0.3 s into a tail; every mode | no click or lone spike in the 200 ms after (2.0.1 gave 0.2–0.4 V spikes about 50–160 ms later); largest step ≤ 2 × the steady one + 0.05 V |
| Bloom | held noise, Mix 1; Bloom 0, noon, 1 | feed rise to −3 dB ≈ 12 ms, 1 s, 4 s (FRET 2×); whole output −3 dB at T60 6.32 s ≈ 0.30, 1.34, 3.8 s in HAAR [meas; 3.8 s from 2.0.1] |
| Bloom gate | 440 Hz sine at 0.9 mV and 1.1 mV peak | no wet at 0.9 mV; a wet at 1.1 mV |
| Drift | Drift 0 vs 1 on a held 1 kHz tone | Drift 0: static, ringing tones allowed; Drift 1: audible chorus-like smear, tens of cents (43 c Slow, 25 c Very slow) |
| **Drift step** | 1 V 110 Hz sine, Mix 1; Drift noon → 0, noon → 1, and the Drift rate menu Slow ↔ Very slow during the note; every mode | no click; the modulation depth changes over about 40–200 ms; largest step ≤ the steady one (measured ≤ 0.052 V against 0.025–0.054 V; 2.0.1 up to 3.7 V) |
| Mix | noise; Mix 0, 0.25, 0.5, 0.75, 1, Visibility noon | Mix 0 bit-exact dry; loudness within ±2 dB across Mix (measured within 0.04 dB) |
| Mix at long Visibility | ±1 V noise at Visibility 1; Mix 0 → 1 | loudness rises with Mix to about +11.5 dB (about +8.8 dB at noon): expected, not a defect |
| Attenuverters | +5 V on each CV at att +1 and −1, knob noon | parameter reaches 1 and 0 respectively; att 0 has no effect |
| CV range | ±10 V, ±50 V, NaN-producing sources | saturates at 0/1; no crash, no noise |
| **Poly on CV** | 4-channel cable on each CV jack, a different voltage per channel | only channel 1 has an effect; channels 2–4 change nothing. Do not file; issue #26 holds the design question |
| IN R normal | IN L only | both outputs carry wet and dry; dry R = dry L exactly |
| Low cut | 30 Hz sine, Mix 1, Off vs 50 Hz vs 100 Hz | wet falls ≥ 6 dB with 50 Hz (measured −9.4 dB); dry unchanged at Mix 0 |
| Dry kill | Mix 0 with Dry kill on | wet only; toggling makes no click (≤ 0.05 V step on 5 V DC) |
| Initialize | after changing all menus | Haar, Slow, 50 Hz, Dry kill off; knobs to noon, attenuverters 0 |

### 11.2 Per mode

| Mode | Listen for | Measurable checks |
|---|---|---|
| HAAR | smooth dense wash, no distinct echoes, not plate-like | T60 within ±15 %; 1 envelope peak at Size 1 |
| FRET | darker and warmer than HAAR at the same Damp; slower swell | 4k/250 T60 ≤ 0.85 × HAAR's (0.67 vs 1.00 at Damp 0); Bloom twice as slow |
| SMOKE | distinct first repeat, then repeats blurring into wash; strongest at Size 1 | ≥ 3 repeats in 2 s at Size 1 (about 0.47 s apart); gaps ≤ 40 dB deep. Listen to bass at Drift 0 [issue #19] |
| ICE | bright, sparse, octave halo at every pitch; little octave on bass | octave 9–14.5 dB under the note in the tail at 110–880 Hz; 30 Hz octave ≥ 8 dB further down; T60 10–15 % short |
| All | no metallic ring or flutter on long tails at Drift noon; no runaway at DENSE; equal loudness between modes | the tail always decays; nothing above 10 V at Mix 1; modes within 1 dB of HAAR on ±1 V noise |

### 11.3 Mode switching

| Procedure | Pass criteria |
|---|---|
| switch mode during a held note and during a tail | no click; wet fades out about 50 ms, silence, new mode fades in about 50 ms; dry uninterrupted; old tail gone within 100 ms |
| switch at Bloom noon with held input | wet returns over about 1.3 s (HAAR/SMOKE/ICE) or about 2+ s (into FRET), not instantly; expected |
| toggle the menu rapidly (several changes per second) | no click; ends in the last mode chosen |
| switch while the module is idle (asleep) | silent; next input plays the new mode normally |
| switch ICE → HAAR → ICE quickly on a ringing ICE tail | nothing of the old ICE tail returns |
| change Damp just before or during a switch [issue #25: no automated test] | no click; the new mode starts at the new Damp |
| CPU meter during switches | a single short spike per switch is known [issue #14]; no sustained rise |

### 11.4 Saved patches and presets

| Procedure | Pass criteria |
|---|---|
| save a patch with each mode, Drift rate, Low cut, Dry kill; reopen | every setting restored; JSON `data` holds `mode` 0–3, `driftRate` 0–1, `lowCut` 0–2, `dryKill` boolean |
| open a patch saved with 2.0.1 in 2.0.2, and the reverse | loads without warnings; same knobs and menus. Sound differs as section "What changed" says (level at long or short Visibility, bass image) |
| reopen a patch saved in ICE while audio plays from the first moment | ICE from the first sample; no fade-in from HAAR |
| hand-edit JSON: missing keys, wrong types, out-of-range values (`mode` 7, −1, "x") | loads without crash; clamped (7 → Ice, −1 → Haar), wrong type → 0/false, missing key → current value. Reals (`2.0` → 0) and huge integers (`4294967297` → Fret) are known [issue #12] |
| save and load a module preset (.vcvm) | knobs and menu restored |
| **load each factory preset** from the module's Preset menu (Becalmed, Nebula, Harbour, Whiteout), on a fresh module and onto a module in another mode, while audio plays | appears in the menu; loads without a Rack warning in the log; sets exactly the values of section 3.1 (mode, all six knobs, attenuverters 0, Drift rate Slow, Low cut 50 Hz, Dry kill off); no click beyond the mode fade [issue #18: first real load test] |
| factory presets by ear | Becalmed: dark, slow FRET swell, about +7 dB of wet on quiet material at its 30 s decay; Nebula: huge HAAR wash, strong Drift; Harbour: short, soft glue at Mix 0.3; Whiteout: wet-only bright ICE wash. Report impressions on #23, not as defects |
| duplicate the module | copy carries all settings; instances independent |

### 11.5 Sample rates (44.1 kHz to 768 kHz)

| Procedure | Pass criteria |
|---|---|
| run at 44.1, 48, 88.2, 96, 176.4, 192, 352.8, 384, 705.6, 768 kHz | no crash, no silence, no noise; T60 at HAZE and FOG within ±15 % (measured within 3 % on 2.0.1) |
| bass stereo at 192 kHz and 768 kHz | 33 Hz mono sine: correlation and mono-sum loss as at 48 kHz (about +0.16, 2.4 dB) [unverified above 48 kHz: the tests run at 48 kHz] |
| change rate while a tail rings | no burst or click beyond the cut; the tail is cleared (expected); correct decay afterwards |
| dry timing above 96 kHz | dry has zero delay; the wet is a few samples later (expected) |
| CPU against rate | roughly doubles from 48 to 96 kHz; grows only modestly above 96 kHz |
| sleep hold | 1.40 s of silence after the tail falls below −114 dB, at every rate (section 7.1) |

### 11.6 Polyphony

| Procedure | Pass criteria |
|---|---|
| poly cable (2–16 ch) into IN L | channels summed; output mono per side; wet ≤ 10 V at Mix 1 |
| 16 ch ±5 V at Mix 0 | the dry is the plain sum (up to ±80 V): expected, not limited |
| poly into IN R only | IN R summed; IN L silent unless patched |
| poly into each CV jack | only channel 1 used (section 2.3) |
| bypass with a poly input [unverified] | passes polyphonic, unsummed |

### 11.7 CV at audio rate

| Procedure | Pass criteria |
|---|---|
| Size CV, audio-rate sine or ±5 V square | pitch/swoop effect (slewed 30 ms); wet ≤ 10 V at Mix 1; no crash |
| Damp CV at audio rate or square | now smoothed (30 ms): the brightness follows only slow movement (corner about 5 Hz); no zipper noise, no clicks on square CV |
| Bloom CV at audio rate | smooth; no clicks |
| Mix CV at audio rate | slew-limited to a 5 ms full-range ramp, so fast square CV becomes a ramped crossfade; no clicks |
| **Drift CV square or stepped** | no clicks (fixed in 2.0.2); the depth follows within about 40–200 ms; fast Drift CV is filtered (corner about 4 Hz) |
| all five CVs at audio rate together (as busy) | CPU about 0.9–1.0 % of an M-series core per instance at 48 kHz; no crash |

### 11.8 Sleep and wake

| Procedure | Pass criteria |
|---|---|
| stop all input | after the tail decays (section 7.1 table; about 2.2 s at Visibility 0, 11 s at noon, 125–144 s at fully right), CPU drops to the idle level |
| play again after sleep | sound starts instantly and identically to a fresh module; no click |
| change Size, Visibility, Damp, Drift or mode while asleep, then play | new settings take effect immediately; no pitch swoop from the old Size, no glide from the old Visibility, Damp or Drift |
| DC offset above 1 mV on the input with Low cut Off | engine stays awake (expected); with Low cut on, it sleeps |
| a 0.5 mV input | no wet and the engine sleeps (Bloom's 1 mV gate) |
| unplug the input during a loud tail | the tail continues and decays; no click |

### 11.9 Mono compatibility (changed in 2.0.2)

| Procedure | Pass criteria |
|---|---|
| sum OUT L + OUT R on broadband material (noise, full mix) | about 3 dB below the per-channel level, as for decorrelated stereo (measured 3.0–3.1 dB) |
| sum on bass sines and drones below about 200 Hz, every mode, Drift noon and Drift 0 (to 140 Hz) | mono-sum loss ≤ 4 dB (measured 1.9–2.4 dB at 33–110 Hz); correlation −0.2 to +0.5 (measured +0.16 to +0.30); **file any anti-phase or loss over 4 dB here** |
| 210–450 Hz sines, Drift noon | loss ≤ 5.5 dB, correlation −0.45 to +0.6, sides ≤ 6 dB apart (the crossover region) |
| the testbed's Fathom drone | correlation about +0.16, loss about 2.4 dB, sides level |
| IN L only, wet both sides | both sides within 1.5 dB on bass and noise (measured within 0.1 dB); on single sines above 300 Hz the balance can differ by up to about 2.6 dB |
| bass level note to note, Drift 0, especially SMOKE | listen for notes that drop out or jump; known lumpiness [issue #19]: report what you hear there |

### 11.10 Loud drones

| Procedure | Pass criteria |
|---|---|
| ±5 V saw/square/sine at 55–220 Hz, Visibility 0.75–1, Size 0.5–1, Damp 0, Mix 1, all modes | ≤ 1 % of samples over 9 V (measured ≤ 0.18 %); no audible clipping; output level about that of the dry (measured −7.4 to +0.9 dB) |
| same at Mix noon | ≤ 1 % over 9 V (measured ≤ 0.63 %); peaks up to 10.6 V are expected |
| a drone's first second at Visibility 1, Mix noon | no audible crunch at the onset (the reason for the 10 ms attack) |
| steady drone, Drift 0, after build-up | level steady (leveller moves < 0.4 dB per s); no pumping |
| dynamic material (drums, plucks) at THICK–DENSE | the 10 ms slow attack reacts faster than in 2.0.1; listen for the wet ducking on single hits |
| drone, then stop | tail decays naturally; does not swell back up |
| new note into a held-down tail | level recovers over several seconds (3 s release) |
| ±1 V material | leveller inactive (gain exactly 1); level follows the law |

### 11.11 CPU

| Procedure | Pass criteria |
|---|---|
| one idle instance | idle-level meter reading after the hold; Mac bench 0.06 % of a core |
| one busy instance, 48 kHz | Mac bench about 0.9 % of a core (budget 1.6 HAAR, 1.65 ICE); spec target < 3 % |
| 16 instances | per-instance cost ≤ 1.5 × one alone |
| ICE vs HAAR | ICE within about 3 % of HAAR (my bench: 0.936 vs 0.915 at 1 instance, 0.974 vs 0.981 at 16) |
| 2.0.2 vs 2.0.1 | awake cost about 10–20 % higher (section 7.3); record it |
| after moving Drift once, then leaving it | awake CPU may stay slightly higher than before the move [issue #11] |
| Windows | no automated budget; record the Rack meter readings per instance for idle, quiet and busy at 48 kHz |

---

## 12. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a −1..+1 scale on a CV input; negative inverts |
| Bloom | Fog's slow swell on the signal fed into the reverb; not applied to the dry |
| Dry kill | menu switch forcing Mix to fully wet |
| Diffuser | a cascade of short delays and orthogonal mixing that smears the input before the FDN, raising echo density |
| Equal-power crossfade | dry × cos, wet × sin, so the summed power of uncorrelated signals stays constant |
| FDN | feedback delay network: several delay lines whose outputs are mixed by a matrix and fed back |
| Feed | the signal into the network after low cut, tone and Bloom |
| Halfband filter | a low-pass at a quarter of the rate used to halve or double the sample rate |
| Hermite read | 4-point cubic interpolation for reading a delay line between samples |
| Hadamard matrix | an orthogonal matrix of ±1 entries; used for mixing and for ±1 tap patterns |
| Householder matrix | I − (2/N)·11ᵀ, an orthogonal reflection used as the FDN's mixing matrix |
| Inner rate | the rate the network runs at: host rate ÷ 1, 2, 4 or 8, at most 96 kHz |
| Level law | full wet against dry = 10·log10(T60 / 5.66 s) dB on steady material below the leveller's threshold |
| Leveller | Fog's slow, stereo-linked automatic gain on the wet, target 6.5 V |
| Limiter knee | the level (9 V) above which the limiter starts to bend the signal; the ceiling is 10 V |
| Low band | below 300 Hz, the shared tap and phase pair that make the bass mono-compatible |
| Make-up | the gain on the wet that sets its level: the matched level times √(T60 / 5.66 s) |
| Mono-sum loss | how much quieter (L+R)/2 is than the mean channel level; 0 dB for identical channels, 3 dB for uncorrelated, large for anti-phase |
| One-pole | a first-order smoother; after one time constant it has covered 63 % of a step, after five about 99 % |
| Phase pair | two allpass chains whose phase difference is nearly constant (here 80°) over a band |
| QUIET | Fog's silence threshold, 1e-5 V |
| Ring | the longest time a sample can circulate in the buffers before being overwritten; the sleep hold is twice it |
| Schroeder integration | backward integration of an impulse response's energy, used to fit a decay slope |
| Share over the knee | the percentage of output samples whose magnitude exceeds the limiter's 9 V knee |
| Shimmer | pitch-shifted (here +12 semitones) energy in a reverb tail |
| T30, T60 | time for a decay to fall 60 dB; T30 fits the −5 to −35 dB part and extrapolates |
| Visibility | Fog's decay-time control |
| visSm | the smoothed Visibility (30 ms) that the network gains and the make-up use |
| Voicing | the parameter set that defines a mode |
| Wet | the reverb's output before it is mixed with the dry |

---

### Note on the soak run

I built and ran `soak.cpp` from the 2.0.2 sources with the plugin's flags. It **passed**: 12 runs (4 modes × 3 corners), each 10 minutes of ±5 V noise and 3 minutes of silence at v = 1, Mix 1.

- The peak was 10.000 V in every run (the limit is 10.5 V).
- The worst ratio of tail RMS to the T60 law's allowance was 0.63. Any value under 1 means the tail decays faster than the law requires.
- The run took 57 s.
- The soak checks |out| with `fmax`, which drops a NaN, and the tail RMS on L only [issue #16].

### Note on the measurement programs

- My probes are one C++ file compiled, outside the repository, against the extracted 2.0.2 sources (commit 7b43182) and, for the before and after figures, the 2.0.1 sources (da9cccc), with the plugin's floating-point flags.
- They drive `rka::FogEngine` as `Fog.cpp` does: one mono signal on IN L, IN R unpatched, the mode set before the first sample. Unless stated, the knobs are at noon and the menus at their defaults.
- My 2.0.1 stereo figures reproduce the 2.0.1 reference's table exactly, which checks the method.
- They do not exercise Rack's ports, threading or GUI. Anything marked [meas] should therefore match Rack at 48 kHz, but Rack-side effects (block size, the order of events on a patch load or a rate change, preset loading) are outside them.
- I did not run `make libm-calls` (Linux only) or `make engine-globals`.

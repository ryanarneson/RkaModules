# Wake 2.0.39: Technical Reference for QA

Module: **Wake**, plugin `rka` (slug `rka`, module slug `Wake`), VCV Rack 2 (minRackVersion 2.5.0).
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`), on branch `neap`, not yet merged to main. Everything here was taken from that tree (`git archive 2e6e960`).
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It is a Zstandard-compressed tar. Its `plugin.json` and `res/wake/Wake.svg` and `Wake-dark.svg` are byte-identical to the commit's. `presets/` holds only Fog's presets: **Wake has no factory presets**. I did not inspect `plugin.dll`.
Document date: 2026-11-24. This is the first technical reference for Wake.

Wake is a 14 HP stereo echo for struck sounds: 10 ms to 10 s, three styles (Tape, Analog, Digital), Ratio between the sides, Ping Pong, Freeze, Diffuse, Drift and a clock input. The clock sync (section 5), the styles (section 6) and Freeze (section 7) are covered in detail.

## Build history

Wake's own test builds (CHANGELOG.md, `git log` for `src/wake`, `docs/wake`, `docs/specs/2026-11-20-wake-design.md`):

| Build | Date | Commit | What changed for Wake |
|---|---|---|---|
| 2.0.20 | 2026-11-20 | `3107e48` | First build for listening: the module, its panel, manual and tests. Shared `src/DelayTap.hpp` (moving read point: glide, crossfade, frozen loop) is new with it. |
| 2.0.21 | 2026-11-20 | `c1f3034` (fixes in `84861a9`) | Final review fixes. Crossfades add no level (a clocked or modulated Digital echo had been able to sustain below 100 % Feedback). A Windows-safe clock counter. Drift's offset no longer jumps on a Time change. Re-freezing inside a fade no longer clicks. An audio-rate Freeze gate has bounded cost. The frozen sound is now the echo that was playing (the loop's colour sits on the way into the line). A paused clock keeps its tempo. Randomize no longer latches Freeze. |
| 2.0.33 | 2026-11-23 | `051d217` (PR #73) | Bypass routes as the working module does and keeps the style light lit (#59, `7107020`). Typing a clock multiple into Time's tooltip sets that step (#60, `1d2c793`). Both issues are closed. The same build changed shared `src/hadal/Drift.hpp` (`Wander::setRate` rescales its hold, #34); Wake calls `setRate` before `seed`, so Wake is unaffected [code]. |
| 2.0.34 to 2.0.39 | 2026-11-23 to 24 | | Nothing in `src/wake`, `docs/wake`, the spec, `DelayTap.hpp`, `DelayLine.hpp` or `FastMath.hpp`. Later modules did change `src/widgets.hpp` (+563 −14 lines since 2.0.21): `CapButton` and `CapLatch`, which Wake uses for Style, Freeze and Ping Pong, gained a light reference for Spindrift. The change is additive; Wake's buttons in Rack were not re-checked [unverified]. |

---

## 0. How to read this document

- Every number comes from one of these sources, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/wake/*.hpp`, `src/wake/Wake.cpp`, `src/DelayTap.hpp`, `src/DelayLine.hpp`, `src/FastMath.hpp`, `src/hadal/Drift.hpp`). Cited as file:line.
  - **[test]**: printed or asserted by Wake's automated tests. I rebuilt them from the 2.0.39 tree with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; `test_hostile` with ASan and UBSan at `-O1`) on an Apple M-series Mac, and ran them. All five test programs passed (section 12).
  - **[meas]**: a short measurement program I wrote against `rka::WakeEngine` (the code the module runs), at 48 kHz unless stated, compiled with the plugin's flags. Programs and outputs are listed at the end. "The probe's floor" below means the tap-delay readout is a float, so the pitch figures taken from it cannot resolve less than 3.4 cents.
  - **[issue #N]**: an open GitHub issue in the repository (section 11).
  - **[unverified]**: anything I could not check, mostly Rack host behaviour. I did not run Rack.
- Voltages are Rack volts. "Noon" is knob 0.5. "T" is the echo time.
- The knob value of every panel control is in [0, 1] except Ratio (0 to 6) and the Time attenuverter (−1 to +1).
- The listening guide `wake-2.0.39-listening.html` was used as a cross-check only. Where it disagrees with the code, the code wins; the disagreements are in section 1.1 and the reply.

---

## 1. What Wake is

Wake sits after a struck sound (Rigging's strikes, a ping, a bell) and gives each hit depth, space and a rhythm of repeats (spec `docs/specs/2026-11-20-wake-design.md`). The user's brief in the spec: "a comprehensive delay. stereo/tap. good functionality", then "it's good to put a delay on there to add some depth and space and a bit of echoing rhythm". It is smaller than the first idea page: the separate taps with outputs, Send and Return, tap tempo and Reverse were left out by design.

Design goals that QA can check:

- Echoes from 10 ms to 10 s, from one repeat (Feedback 0) to repeats that hold for minutes.
- Three styles that differ audibly in the repeats' colour and in what Time does: Tape and Analog bend pitch when Time moves; Digital crossfades with no bend.
- Left and right at related times (Ratio) and Ping Pong.
- Diffuse (repeats blur toward a wash) and Drift (a slow independent waver on each side).
- Freeze: hold what is in the delay and loop it, without fading, dulling or building.
- Echo times that follow a clock.
- CV on Time, Feedback and Mix.

Anti-goals, which the spec states as things that must not happen: a level that runs away with Feedback at its top; clicks or zipper noise when Time, Ratio, Style, Freeze or Ping Pong change (by hand, CV or clock); a frozen loop that fades, dulls or builds; DC at the outputs or building in the loop; left and right cancelling in mono.

Non-goals (do not file these as defects): separate taps with their own levels and outputs, Send and Return, a tap-tempo button, close copies of tape or bucket-brigade units, reverse, ducking, pitch shifting in the loop, echoes over 10 s (that is Eddy), polyphony (a polyphonic audio cable is summed, a CV uses channel 1; issue #64 asks whether this should change) and saving the delay's contents with the patch. The panel has CV only for Time, Feedback and Mix, a clock and a freeze gate: none for Tone, Ratio, Diffuse, Drift, Style or Ping Pong.

### 1.1 Where the spec, the manual, the listening guide and the shipped code differ

The code is what ships. Test against its behaviour. "Manual" is `docs/wake/README.md`; "guide" is the listening list.

| Topic | Spec / manual / guide says | 2.0.39 does |
|---|---|---|
| Freeze release fade | spec Freeze section: "Entering and leaving fade over about 10 ms". The spec's own "As built" paragraph: the release is a 40 ms linear crossfade. Manual and guide: "releasing fades back over a few hundredths of a second". | Entering is seamless to the sample, with no fade. The release is a 40 ms linear crossfade from the loop to the live read (`CROSSFADE_S`, `DelayTap::release`) [code; test: a 40 ms fade is 1920 samples]. The 10 ms figures are the loop's join and the write ramp. |
| Hole after a release | not mentioned | After a release the line holds a short notch where the old end was faded out and writing restarted. It is heard once, T after the release: the one-cycle RMS of a held 220 Hz sine drops to −18.5 dB (Digital), −19.0 dB (Tape), −19.1 dB (Analog) re steady, for about 20 ms [meas] (section 7.5). Not covered by a test. |
| Sample-rate change and Freeze | manual: "A change of sample rate, and Initialize, empty the delay and unfreeze". Guide (Sample rate row): "The delay empties and unfreezes". Spec: only "A sample-rate change clears the delay". | The delay is cleared, but the module **freezes again at once** while the Freeze button is latched or the GATE is high: the first sample after the change is frozen, and the loop is silent [meas] (section 7.7). Initialize does unfreeze, because Rack resets the button. The hostile test releases the latch before it checks, so it does not see this. |
| Clock slowing by more than 4× | manual: "A clock that really slows by more than that is followed one tick late." Spec: an edge more than four periods after the last "is a restart, not a period". | A clock whose new interval is more than 4× the held period is **never followed**: every later edge is again taken as a restart, and Time stays on the old tempo until the clock is unplugged, the rate changes or Initialize runs. 0.25 s → 1 s (4.04×) and 0.25 s → 2 s and 3 s all stayed at 0.250 s through four edges [meas] (section 5.3). A slowing of 3× is followed [test]. Not covered by a test. |
| Noon is ×1 | spec and manual: "Noon is 1" / "Noon is ×1: one echo per clock tick". | ×1 is the knob range [0.5000, 0.6111). **The module's default Time knob, 0.4924 (300 ms), is in the 3/4 step** (0.389 to 0.500). Patch a clock into a fresh Wake without touching Time and the echo is at 3/4 of the clock period (0.375 s on a 0.5 s clock) [meas] (section 5.4). |
| Style change | spec: "A change of style crossfades over a few tens of milliseconds". Manual: "smooth, with no click". | The style's constants (corners, drive, glide) ramp from the old values to the new ones over 30 ms (`STYLE_RAMP_S`), once per 16-sample control tick. Nothing is crossfaded as signal; the "no bend" flag switches at the half-way point (15 ms) [code]. No click: the largest step in 200 ms after a press is at most 2.05× the one before, and the rise tracks the louder style's level [meas]. |
| Style corners | spec: Tape about 6 kHz; Analog about 5 kHz at short times falling to about 1.5 kHz at long ones. | Measured −3 dB corners: Tape 6,432 Hz; Analog 5,473 Hz at 10 ms and 1,674 Hz at 1 s and beyond [test, printed]. Tuning constants are 6,000 / 5,000 / 1,500 Hz [code]. All "by ear" values, not yet settled. |
| Feedback at 100 % and over | spec and manual: "From 100 % up the repeats sustain or grow, into the loop's soft bound." | True for Digital (a 1 V sine at 110 %, T 100 ms, settles at 10.0 V RMS, the output ceiling). Not true for every Tape or Analog setting: each pass loses level that 110 % (+0.83 dB) does not always repay. At 110 %, T 100 ms, 220 Hz, Tone noon: Tape holds 3.7 V RMS; Analog falls to 1.1 V and then rises to 1.6 V; with Tone full left Analog is silent by 15 s [meas] (section 6.6). |
| Diffuse through exactly 0 | manual: "Turning it up from exactly 0 shifts the echo by a few milliseconds as it comes in." | The echo's time moves by the smear's whole latency, **8.92 ms on the left and 9.52 ms on the right**. In Tape and Analog that is a pitch blip, +100 cents (Tape) or +183 cents (Analog) going in and −106 / −204 cents going out, lasting 250 to 374 ms; in Digital it is one 40 ms crossfade [meas] (section 2.9). The manual does not mention the pitch. |
| Drift's pace | spec: "The pace is fixed and slow (seconds), as Fog's and Hadal's Drift." | The wander is Hadal's run 25× faster: a new heading every 0.32 to 1.2 s [code arithmetic: Hadal holds 8 to 30 s, `DRIFT_SPEEDUP` 25]. |
| Drift's depth | spec: "about ±30 cents of pitch" at full. Manual: up to about 30 cents on echoes of a few seconds, about 15 cents at 300 ms, a few cents at 50 ms. | ±2.9, ±15.4 and ±25.9/±27.2 cents at 0.05, 0.3 and 3 s at full Drift [test, printed]. At the default 15 % a frozen loop wanders ±1.7 to ±1.9 cents at T 0.3 s and −3.6 to +2.8 cents at T 2 s [meas]. |
| Randomize | manual: "Randomize does not touch Freeze or Style." Guide (Freeze group): "Freeze and Ping Pong are not touched by it, and the style stays as it was." | Rack's own `Module::onRandomize` skips Style and Freeze and **randomizes Ping Pong**, Time, the Time CV attenuverter, Feedback, Tone, Ratio, Mix, Diffuse and Drift [meas: a harness against libRack 2.6.6]. The guide is wrong about Ping Pong. The delay's contents and the `style` variable are not touched. In a live Rack: [unverified]. |
| Mix fully left | manual: "Fully left is the input exactly." Guide: "the output is exactly the input." | Exact up to 8 V; above it the output's soft limit bends the dry too (8 V knee, 10 V ceiling) [test: `mix_law` checks only up to 8 V]. Bypass is not limited. |
| Style while bypassed | guide: "The style light stays lit." | The light does. A press of STYLE while bypassed does nothing: the trigger runs only in `process()`, so the press is lost [code] (section 3). |
| JSON | spec: "Saved: the knobs, Style, Freeze and Ping Pong." | Rack saves the knobs, Freeze and Ping Pong as params. The module adds one key, `"style"`. A missing, non-integer or out-of-range `"style"` loads as Tape (0); it is not clamped as Fog's menu values are (section 3). |
| Longest echo above 192 kHz | spec: left to the builder. Manual: 10 s up to about 200 kHz, about 5.5 s at 384 kHz and 2.7 s at 768 kHz. | 10 s up to and including 192 kHz; the limit changes at about 209.5 kHz: 5.944 s at 352.8 kHz, 5.461 s at 384 kHz, 2.972 s at 705.6 kHz, 2.731 s at 768 kHz [meas] (section 10.1). The manual's figures are right. |
| Typed Time | manual: "250", "250 ms" and "1.5 s" | As stated; also "2 sec", "10s" (any text starting with s after the number is seconds). A number alone is milliseconds, so "0.25" is a quarter of a millisecond and reads as 10 ms [code, `Wake.cpp:73-90`]. |
| Tooltip near 1 s | spec: "milliseconds or seconds" | Below 1 s it prints `"%.0f ms"`, so 999.6 ms prints as "1000 ms" [code, `Wake.cpp:67`]. |
| CPU | manual: about 0.15 % of a core at 48 kHz | 0.142 to 0.168 % per instance (bench, section 9) |

---

## 2. Panel, controls and ports

### 2.1 Layout

14 HP. Positions in mm from `WakeLayout.hpp` [code]:

| Control | Centre (x, y) mm |
|---|---|
| TIME (hero knob) | 35.56, 37.0 |
| STYLE button | 63.0, 24.0 |
| Style lights TAPE / ANALOG / DIGITAL | 58.0, 33.5 / 39.5 / 45.5 |
| FEEDBACK, TONE, RATIO, MIX knobs | 7.8, 21.7, 49.4, 63.3 at y 64.0 |
| TIME attenuverter | 35.56, 66.0 |
| Jacks, row y 83.0: FEEDBACK CV, CLOCK, TIME CV, GATE (freeze), MIX CV | x 7.8, 21.7, 35.56, 49.4, 63.3 |
| IN L, IN R | 7.8, 102.0 and 7.8, 116.0 |
| DIFFUSE, DRIFT knobs | 21.7, 109.0 and 35.56, 109.0 |
| FREEZE, PING PONG buttons | 49.4, 102.0 and 49.4, 116.0 |
| OUT L, OUT R | 63.3, 102.0 and 63.3, 116.0 |
| Screws | 63.5, 2.54 and 7.62, 125.96 |

The widgets are `HeroKnob` (Time), `Atten`, `LimbKnob`, `CapButton` (Style, momentary; shift-click latches it down), `CapLatch` (Freeze, Ping Pong), `JewelLight` and `BrassPort`.

### 2.2 Parameter, input, output and light ids

Saved patches depend on these numbers; they must never change. Only three are pinned by `static_assert` (`Wake.cpp:176-177`: TIME = frame TIME, DRIFT = frame DRIFT, STYLE = the frame's parameter count, 8), and the frame copies params 0 to 7 in order, so the order of the first eight is pinned in effect.

| Id | Param | Range | Default | Tooltip text (name) and display |
|---|---|---|---|---|
| 0 | Time | 0..1 | 0.4924 (300.1 ms) | "Time"; ms below 1 s, else seconds with 2 decimals; while following a clock, e.g. "x1 clock" |
| 1 | Time attenuverter | −1..+1 | 0 | "Time CV (1 V doubles the time at full)"; −100..+100 % |
| 2 | Feedback | 0..1 | 0.3636 (40.0 %) | "Feedback (over 100 % the repeats grow)"; 0..110 % |
| 3 | Tone | 0..1 | 0.5 | "Tone (left: repeats darken; right: they thin)"; −100..+100 % |
| 4 | Ratio | 0..6, snapped | 3 | "Ratio (the shorter side's time against the other)"; names "L 1:2" to "R 1:2" |
| 5 | Mix | 0..1 | 0.35 | "Mix (input to echoes)"; 0..100 % |
| 6 | Diffuse | 0..1 | 0 | "Diffuse (smears each repeat)"; 0..100 % |
| 7 | Drift | 0..1 | 0.15 | "Drift (slow waver of the echo time)"; 0..100 % |
| 8 | Style | button 0/1 | 0 | "Style (Tape, Analog, Digital)"; momentary, not randomized |
| 9 | Freeze | switch 0/1 | 0 | "Freeze (holds what is in the delay)"; Off / On; not randomized |
| 10 | Ping Pong | switch 0/1 | 0 | "Ping pong (echoes bounce left and right)"; Off / On |

| Id | Input | | Id | Output | | Id | Light |
|---|---|---|---|---|---|---|---|
| 0 | Feedback CV | | 0 | "Left (alone: both sides, averaged)" | | 0 | Tape |
| 1 | "Clock (Time becomes a multiple of it)" | | 1 | "Right" | | 1 | Analog |
| 2 | Time CV | | | | | 2 | Digital |
| 3 | "Freeze gate" | | | | | 3 | "Frozen" (the Freeze cap) |
| 4 | Mix CV | | | | | | |
| 5 | "Left (alone: both sides)" | | | | | | |
| 6 | "Right (alone: both sides)" | | | | | | |

Ratio display names, in order: L 1:2, L 2:3, L 3:4, 1:1, R 3:4, R 2:3, R 1:2 [code, `Timing.hpp:99`]. The exact text Rack prints for the Feedback, Tone, Mix and attenuverter tooltips (digits, spacing) is [unverified].

Typed values [code, `Wake.cpp:73-116`]:

- Time: a number is milliseconds; a number followed by "s" or "S" is seconds ("1.5 s"); a value that is not above 0 is ignored. While a clock is locked, a multiple as the tooltip shows it is accepted ("x2", "1/2", "x1.5 clock", any case) and sets the knob to the middle of that step (step/9). Anything else falls through to the number path.
- Ratio: a name ("L 2:3", "1:1", case aside) or a number 1 to 7. "9" and "0" do nothing.

### 2.3 Control reading, CV laws and smoothing

- **Control rate.** Time, the Time CV and attenuverter, Ratio, Tone, Diffuse, Drift and the Style ramp are read once every 16 samples (`CONTROL_SAMPLES`; 3,000 Hz at 48 kHz) [code, `WakeEngine.hpp:138-140`]. A CV on Time therefore moves the echo in 16-sample steps. The Freeze gate, the clock, the inputs and the output stage run every sample.
- **Smoothing.** Feedback, and the dry and wet gains of Mix, follow their targets through a one-pole of 5 ms (`SMOOTH_S`) and land exactly within 1e-5. Ping Pong, the mono average and the write gain after a release move on a linear ramp of 10 ms (`SWITCH_S`). Time moves by glide or crossfade (section 6). Tone, Diffuse and Drift's depth are unsmoothed in the engine; Diffuse has its own 10 ms fade and gain smoothing (section 2.9); the Tone case is issue #58 (section 2.7).
- **Time CV**: `cv × attenuverter` is added in octaves, clamped to ±12 octaves, to the knob's log time. At attenuverter +1, 1 V doubles the time. Result clamped to 10 ms .. the longest echo (section 10.1). Measured, noon knob (316.2 ms), attenuverter +1: −1 V 158.1 ms, +1 V 632.5 ms, +2 V 1,264.9 ms, +5 V and above 10 s, −5 V and below 10 ms; attenuverter −1 with +2 V gives 79.1 ms; attenuverter +0.5 with +2 V gives 632.5 ms [meas].
- **Feedback CV and Mix CV**: effective = clamp(knob + 0.1 × cv, 0, 1); 10 V is the knob's whole travel, with no attenuverter. Feedback's gain is 1.1 × effective [code, `WakeEngine.hpp:372-375`].
- **CV on a polyphonic cable reads channel 1 only** (`getVoltage()`); audio inputs sum all channels (`getVoltageSum()`) [code; issue #64].
- NaN or ±infinity on any input, CV or param reads as 0 (`finite()`); a clock or gate of NaN is 0 V.
- An unpatched CV jack reads 0 V.

### 2.4 Time

[code, `Timing.hpp:13`] `T = 10 ms × 1000^knob` (log2(1000) = 9.96578). Default knob 0.4924 gives 300.1 ms [test: within 1 %].

| Knob | 0 | 0.1 | 0.25 | 0.4924 | 0.5 | 0.75 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|---|
| Time [meas] | 10.0 ms | 20.0 ms | 56.2 ms | 300.1 ms | 316.2 ms | 1.778 s | 5.012 s | 10.000 s |

- This is the left side's time before Ratio. A NaN knob reads 10 ms.
- With a locked clock, Time is a clock multiple (section 5).
- What happens when Time moves depends on the style (section 6). The echo arrives `T × fs − 1` samples after the hit (the tap reads before the sample is written), and the engine takes the Diffuse latency off that when Diffuse is above 0.
- **Echo time accuracy** [test: `echo_time`]: at 44.1, 48 and 96 kHz, in each style, at 10 ms, 0.3 s, 1 s and 10 s, the echo arrives within 1 sample or 0.1 % (Digital), or 4 samples × fs/48 kHz (Tape, Analog: the filters round the pulse and the peak lands a few samples late), and `seconds()` is within 0.2 % of the request.

### 2.5 Ratio

[code, `Timing.hpp:87`; test: `ratio_times`] Seven steps; the knob snaps.

| Knob | Name | Left | Right |
|---|---|---|---|
| 0 | L 1:2 | ½ T | T |
| 1 | L 2:3 | ⅔ T | T |
| 2 | L 3:4 | ¾ T | T |
| 3 (default) | 1:1 | T | T |
| 4 | R 3:4 | T | ¾ T |
| 5 | R 2:3 | T | ⅔ T |
| 6 | R 1:2 | T | ½ T |

The shorter side stops at 10 ms (`ratioTimes` clamps; it does not double). The unclocked and clocked paths both end here, so a clocked time of 15 ms at "L 1:2" gives a left side of 10 ms, not a grid time. Values outside 0..6 and NaN clamp. Measured at T 200 ms, each step's two echoes land within 1 sample of the table [test].

### 2.6 Feedback

- Gain = 1.1 × clamp(knob + 0.1 × CV). 0 is one echo; the knob's right end is +0.83 dB per pass (110 %); 100 % is knob 0.909.
- The loop is bounded in every style: a soft bound is linear to 12 V and never passes 16 V (`LOOP_KNEE`, `LOOP_CEIL`), a 5 Hz DC block sits in the loop, and the outputs are limited to 10 V (section 8). 4 minutes at 110 % with ±10 V noise, then 10 V DC, then silence, stayed finite and under 16 V in each style, with a mean under 0.5 V [test: `bounded`, `minutes_at_110`].
- **Digital at 50 % with Tone at noon:** successive echoes of a 5 V impulse measure 4.997, 2.497 and 1.248 V, 6.03 and 6.02 dB apart [test: within 0.5 dB of 6.02]. The first echo is within 0.2 dB of the input. 5 V on the Feedback CV at knob 0 gives a loop gain of 0.55 [test: within 0.02].
- **Digital at 100 % is not quite lossless:** the 5 Hz DC block takes a little at 220 Hz, T 100 ms. A 2 V burst measures 1.394 V RMS at 2 s, 1.262 V at 20 s and 0.998 V at 60 s, about −2.9 dB a minute. Tape at 100 % has fallen to 0.024 V RMS by 20 s and is silent by 60 s; Analog is silent by 20 s [meas].
- Each style's loss per pass, and what 110 % does, are in section 6.6.

### 2.7 Tone

[code, `Colour.hpp:50-58`] A one-pole filter in the loop, in front of the style's own filters, so its effect adds up with each repeat. The first echo passes through it once.

- **Noon is flat: the filter is out of the path** for knob values 0.49 to 0.51 (`t < 0.49`: low-pass; `t > 0.51`: high-pass; between: neither).
- Left (knob 0 to 0.49): a low-pass from 20 kHz down to 500 Hz, log-spaced in the knob. Right (0.51 to 1): a high-pass from 20 Hz up to 1 kHz.
- Measured −3 dB corners: 509 Hz at full left, 998 Hz at full right [test, within 15 %]. The corners are monotonic in the knob [test]. Full left leaves 50 Hz within 0.3 dB, full right leaves 12 kHz within 0.8 dB [test].
- The tooltip runs −100 % to +100 %, 0 at noon (`displayMultiplier 200, offset −100`).
- **Issue #58: a jump that takes the low-pass out of the path steps the loop.** When Tone moves, in one control tick, from below 0.49 to 0.49 or above, the low-pass is dropped and `toneLp_ = x` is assigned: the signal switches from the filtered value to the raw one in a single sample (`Colour.hpp:78-79`). The high-pass side drains its state at 0.99 per sample instead (`:131-143`), so it does not step. Measured on a 3 V sine, Digital, T 100 ms, Feedback 0, Mix 1, largest sample step in the echo of the jump [meas]:

| Tone move | 100 Hz (the sine's own largest step 0.039 V) | 440 Hz (own 0.173 V) |
|---|---|---|
| 0 → 0.5 | **0.596 V** (15×) | **1.537 V** (9×) |
| 0 → 0.495 or 0 → 0.52 | 0.596 V | 1.537 V |
| 0 → 1 (low-pass to high-pass) | 0.596 V | 1.537 V |
| 0.25 → 0.5 | 0.112 V | 0.486 V |
| 0.4 → 0.5 | 0.053 V | 0.234 V |
| 0.48 → 0.5 | 0.043 V | 0.189 V |
| 1 → 0.5, 0.75 → 0.5, 0.5 → 0, 0.5 → 1, 1 → 0 | no step (≤ 0.041 V) | no step (≤ 0.180 V) |
| any of the above swept over 100 ms | no step (≤ 0.042 V) | no step (≤ 0.187 V) |

  The issue's title ("straight to noon") is narrower than the case: **any one-tick move from the left half to the right of 0.49** steps, including to the thin (high-pass) side. Sources: a preset or typed value, Randomize (about half of random Tone values land at or above 0.49; Tone is randomized), a stepped mapping module. A hand sweep does not step. The step is written into the line and is heard once, T later, and again at each repeat (reduced by Feedback). Issue #58 proposes a crossfade or a smoothed Tone; not done.

### 2.8 Style (summary; detail in section 6)

Three characters; the button steps Tape → Analog → Digital → Tape; default Tape.

### 2.9 Diffuse

[code, `Smear.hpp`] Four all-pass filters in series on the way into the line, with delays 0.9, 1.7, 2.6 and 3.7 ms on the left and 7 % longer on the right; coefficient `gain = 0.7 × Diffuse` (`MAX_GAIN`).

- **At exactly 0 it is out of the path** and the signal is bit-for-bit unchanged [test: `smear_zero_exact`, also back at 0 after being up]. Any amount above 0 engages it. It still runs fed when out, so it comes in without a step; mix fade and gain smoothing are 10 ms (`SWITCH_S`).
- When engaged, the engine takes the smear's latency off the echo time: 428 samples (8.92 ms) left and 457 samples (9.52 ms) right at 48 kHz [code arithmetic; the left figure measured, section below].
- Spread [test: `smear_spreads`]: 90 % of a repeat's energy falls within 8.9, 12.4 and 18.2 ms at Diffuse 0.1, 0.5 and 1; its centroid is 8.9 ms (equal to the latency); its level within ±2 dB. With Diffuse at 0.3 or 1 the echo's energy is centred within 1 ms of T and within 2 dB of the input [test: `echo_time_diffused`].
- **Crossing exactly 0 moves the echo by the latency.** Tap delay, T 0.3 s, Drift 0 [meas]:

| Move | Tap delay | Tape | Analog | Digital |
|---|---|---|---|---|
| 0 → 0.01 or 0 → 1 | 14,399 → 13,971 samples (−8.92 ms) | +100 cents, beyond 10 cents for 374 ms | +183 cents, 250 ms | one 40 ms crossfade |
| 0.01 or 1 → 0 | 13,971 → 14,399 (+8.92 ms) | −106 cents, 374 ms | −204 cents, 250 ms | one 40 ms crossfade |

  Moving Diffuse between two non-zero values does not move the echo.

### 2.10 Drift

[code, `WakeEngine.hpp:320-370`, `WakeTuning.hpp:49-50`]

- Each side has a random wander (Hadal's, 25× faster: a new heading every 0.32 to 1.2 s), seeded per instance (`random::u32()`) and per side, so two Wakes never drift alike and the sides are independent.
- The wander is a tape speed, ± up to `DRIFT_DEPTH` = 15 cents (2^(15/1200) − 1) × Drift. A delay under a wandering tape differs from the tape's speed by the speed summed over one echo, so the delay offset is that running sum (a ring of doubles of up to 131,076 entries), and the pitch of the echo moves by the difference of two speeds: up to twice 15 cents on a long echo, little on a short one.
- The seed is random for each module instance (`random::u32()` at construction), so a Drift pattern is not repeatable across loads, duplicates or runs, and is not saved.
- At Drift 0 nothing moves in any style (offset exactly 0). The default is 15 %. The offset's slew is limited to 2.2 × the wander's own fastest speed, with a floor, so a Time or Ratio change does not jerk it [code; test: `drift_does_not_jerk`, offset slope ≤ 2.3 × `DRIFT_DEPTH`].
- Measured at full Drift, 120 s [test, printed]: ±2.9 and ±2.9 cents at 0.05 s; ±15.4 and ±15.5 at 0.3 s; ±25.9 and ±27.2 at 3 s; the two sides correlate −0.04, −0.04 and −0.11. Bounds: pitch to ±35 cents, correlation under 0.3 in magnitude.
- Drift keeps acting on a frozen loop (section 7.3).
- A heard check: an echo at 0.3 s arrives at 14,400 samples at Drift 0 and at a different sample (within 300) at Drift 1 [test].

### 2.11 Mix

[code, `WakeEngine.hpp:373-375`] Equal power: dry = cos(π/2 × m), wet = sin(π/2 × m), through `fastSin2pi`. Default m = 0.35 gives dry 0.8526, wet 0.5225 [code arithmetic]. Mix 0 is the input exactly while it stays within 8 V [test: `mix_law`]; at 0.5 the dry and the first echo are 0.7071 of the input each [test: within 1e-3 and 2e-3]. Mix 1 with 10 V on the Mix CV (knob 0) leaves no dry (leak under 1e-4 V) [test].

### 2.12 Ping Pong

[code, `WakeEngine.hpp:180-197`] A latch, default off, moved with a 10 ms ramp (no click).

- Off: each side feeds back into itself; a stereo input stays stereo (the left never reaches the right) [test: right exactly 0 for a left-only source].
- On: the average of the two inputs goes into the left side only; the left side's echo feeds the right at full level; the right side's echo returns to the left through Feedback. One cable in gives an echo left after T and an equal one right after 2T [test: 4,800 and 9,600 samples at T 100 ms, both 5.0 V, nothing else at Feedback 0]. At Feedback 50 % the second pair is 2.5 V and 2.5 V.
- With Ratio off noon the bounce has a long-short rhythm.
- Randomize can set it (section 3.2).

### 2.13 Audio inputs and outputs

- **Either input alone feeds both sides**; with both patched a stereo input stays stereo. A polyphonic cable is summed (`getVoltageSum`, up to 16 channels). The sum passes `finite()`.
- The input into the delay is limited to ±24 V (`INPUT_MAX`). The dry path to the output is not limited by that; the output soft limit applies to the sum.
- With only IN L patched, or only IN R, the result is identical [test: bit-identical outputs].
- **L alone is a mono out**: with OUT R unpatched, OUT L carries the average of the two output signals, ramped over 10 ms when a cable is plugged into R. One cable out at Mix 0 is the input exactly (within 8 V) [test].
- L and R are `limit()`ed: identity to 8 V, then 8 + 2·tanh((|x| − 8)/2), which approaches 10 V and never reaches it [code, `WakeEngine.hpp:268-276`; test: `limit`: peaks over 8 V and at most 10 V at 110 % Feedback with ±10 V noise, in each style].

---

## 3. The right-click menu and saved state

### 3.1 No menu items, one JSON key

Wake adds **no items to the right-click menu**: there is no `appendContextMenu` [code]. Rack's standard items (Initialize, Randomize, Duplicate, Disconnect cables, Bypass, Presets) apply.

Saved: Rack saves all eleven params, so the knobs, the Style button (0 unless latched down), **Freeze** and **Ping Pong** are saved as params. The module adds:

| Key | Values | Default | On load |
|---|---|---|---|
| `"style"` | integer 0 Tape, 1 Analog, 2 Digital | 0 | an integer 0..2 is taken; **anything else (missing key, a real such as `1.0`, a string, an integer outside 0..2) loads as 0, Tape**; it is not clamped (`Wake.cpp:169-173`) |

So a saved module's `"data"` is `{"style": 1}` and nothing else. Not saved: the delay's contents, the clock's tempo, and the gate (a voltage on a jack). The Freeze button is saved.

Style button note [code, `Wake.cpp:179-180`]: the style steps on the rising edge of `params[STYLE] > 0` in `process()`. A shift-click latches the button down and steps the style once; the next click only releases it. If a patch is saved with the Style button latched down, the first `process()` after load sees a rising edge and steps the style once more [code inference; unverified in Rack].

Not saved with the patch: the Style press itself while the module is bypassed (section 3.3).

### 3.2 Initialize and Randomize

- **Initialize** (`onReset`): `style = 0` and `engine.reset()`: the delay and every filter are cleared, the clock follower forgotten, the freeze state and gate cleared, and every control snaps to its value (no glide) [code]. Rack resets the params: Time 0.4924, attenuverter 0, Feedback 40 %, Tone noon, Ratio 1:1, Mix 35 %, Diffuse 0, Drift 15 %, Freeze off, Ping Pong off [unverified in Rack]. After Initialize the delay is empty and unfrozen [test: `rate_change_recovers`, the reset leg].
- **Randomize.** Rack's `Module::onRandomize` calls `randomize()` on every param whose `randomizeEnabled` is true. The configuration in `Wake.cpp` gives [meas: the same `configParam`, `configButton` and `configSwitch` calls, with a recording `ParamQuantity`, against libRack 2.6.6, calling the real `Module::onRandomize`; `m_random.cpp`]:

| Param | Randomized |
|---|---|
| Time, Time attenuverter, Feedback, Tone, Ratio (snapped, 7 values), Mix, Diffuse, Drift | yes |
| **Ping Pong** | **yes** (`configSwitch` leaves `randomizeEnabled` true; Wake.cpp sets it false only for Freeze) |
| **Freeze** | no (`Wake.cpp:136`; fixed in `84861a9`) |
| **Style** | no (`configButton` sets `randomizeEnabled = false`); and the style lives in a member, not a param, so Randomize cannot reach it either |

  Consequences for QA: a Randomize can latch Ping Pong; it can raise the Time attenuverter off 0, so a patched Time CV starts to act; it can move Tone across 0.49, which is the #58 step (section 2.7); it does not freeze the module, does not change the style and does not empty the delay. Whether the live Rack's Randomize behaves as the library does here, and the exact law for the snapped Ratio, is [unverified].

### 3.3 Bypass

[code, `Wake.cpp:217-227`, `WakeEngine.hpp:214-220`; test: `bypass`] Rack's `processBypass` is overridden (since 2.0.33, #59).

- The inputs are routed as when working: either input alone goes to both outputs; with OUT R unpatched, OUT L carries the average of the two. Not limited (it equals the working module at Mix 0 within the 8 V knee).
- **A polyphonic input is summed to mono**, not passed through channel by channel (`getVoltageSum`) [code].
- The engine is not run: the delay is held, and freeze and clock state are left as they were. A frozen module is still frozen when un-bypassed; a tail does not decay while bypassed [code inference; unverified in Rack].
- The Tape, Analog or Digital light stays lit; the Freeze light is dark while bypassed (`setLights(false)`), even if the button is latched.
- **A press of STYLE while bypassed is lost** (the trigger runs in `process()` only).
- NaN on an input passes as 0 [test].

---

## 4. Signal flow

### 4.1 Block diagram

```
 IN L ──sum poly──┐  rawL = IN L, else IN R, else 0   (NaN/inf → 0)
 IN R ──sum poly──┘  rawR = IN R, else rawL
        │                                                                       dry (not limited by 24 V)
        ├─ clamp ±24 V ─► inL, inR ───────────────────────────────────────────────────────────┐
        │                                                                                     │
        │    ┌────────────── per side s (0 = left, 1 = right), per sample ─────────────┐     │
        │    │ y[s] = TAP_s.read(line_s, mod_s)     (glide / crossfade / frozen loop;     │     │
        │    │                                       mod_s = Drift's offset, samples)     │     │
        │    │ xL = inL + pp·(mono − inL) + fb·(y0 + pp·(y1 − y0))     mono = (inL+inR)/2 │     │
        │    │ xR = (1−pp)·(inR + fb·y1) + pp·y0                                          │     │
        │    │ write_s = writeGain × COLOUR_s( SMEAR_s( x_s ) )      (nothing, if frozen) │     │
        │    └────────────────────────────────────────────────────────────────────────────┘     │
        │          COLOUR = Tone LP/HP → style LP → style HP → drive (tanh) → 5 Hz DC block      │
        │                   → bound (12 V knee, 16 V ceiling) → flush < 1e-20, NaN → clear       │
        │                                                                                     │
        │   CONTROLS (every 16 samples): style ramp, Time (knob, CV or clock) → Ratio →        │
        │   seconds_[s] → tap target (glide or crossfade), Tone/Diffuse/Drift, Feedback, Mix  │
        │   CLOCK follower, FREEZE latch/gate: every sample                                    │
        ▼                                                                                     ▼
   outL = rawL·dry + y0·wet ,  outR = rawR·dry + y1·wet    (dry = cos(π/2 m), wet = sin(π/2 m), 5 ms smoothing)
   L out = limit( outL + mono_ramp·(½(outL+outR) − outL) ) ,  R out = limit( outR )   (limit: 8 V knee → 10 V)
```

`pp` is Ping Pong's 10 ms ramp, `fb` the smoothed Feedback gain (1.1 × knob). Frozen, the write is skipped, the Tone, style and smear filters are fed silence, and each tap plays its loop.

### 4.2 Block constants

[code, `WakeTuning.hpp`]

| Constant | Value | Where |
|---|---|---|
| Time range | 10 ms to 10 s, log | `TIME_MIN_S`, `TIME_MAX_S` |
| Buffer cap | 2^21 = 2,097,152 samples a side | `BUFFER_MAX_SAMPLES` |
| Feedback top | 1.1 | `FEEDBACK_MAX` |
| Tone corners | LP 20 kHz → 500 Hz; HP 20 Hz → 1 kHz; flat within 1 % of noon | `TONE_*` |
| Tape | HP 40 Hz, LP 6 kHz (both Time ends), drive 1 (saturates at ±10 V), glide 0.15 s, 2 octaves, glide mode | `STYLES[0]` |
| Analog | HP 80 Hz, LP 5 kHz at 10 ms → 1.5 kHz at 1 s and beyond (log in Time), drive 2 (±5 V), glide 0.08 s, 2 octaves, glide mode | `STYLES[1]` |
| Digital | no filters, no drive, glide 0.15 s (used only for changes under 4 samples), 2 octaves, crossfade mode | `STYLES[2]` |
| Analog high-cut "long" time | 1 s | `HIGH_CUT_LONG_S` |
| Crossfade | 40 ms, linear | `CROSSFADE_S` |
| Style ramp | 30 ms | `STYLE_RAMP_S` |
| Loop bound | knee 12 V, ceiling 16 V (Ballast's curve) | `LOOP_KNEE`, `LOOP_CEIL` |
| Loop DC block | one-pole high-pass, 5 Hz | `LOOP_DC_HZ` |
| Drift | depth 15 cents of speed (0.0087019), speed-up 25 | `DRIFT_DEPTH`, `DRIFT_SPEEDUP` |
| Freeze join, write ramp, switches | 10 ms | `JOIN_S`, `SWITCH_S` |
| Input limit | ±24 V | `INPUT_MAX` |
| Output limit | knee 8 V, ceiling 10 V | `OUT_KNEE`, `OUT_CEIL` |
| Control tick, smoothing | every 16 samples; 5 ms | `CONTROL_SAMPLES`, `SMOOTH_S` |
| Tap glide steps | at most +0.75 / −3 samples of delay per sample (pitch down 2 octaves / up 2 octaves) | `DelayTap::setGlide` [test: `bounds`] |
| Small change | under 4 samples: glide even in Digital | `DelayTap::SMALL_CHANGE` |

Interpolation: 4-point Hermite on a float delay (`DelayLine::read`). Issue #61: at 10 s and 48 kHz that leaves 1/32 sample of resolution, and the read error on a moving 5 kHz sine is −47.5 dB at 100 ms and −42.8 dB at 9.8 s (the issue's figures; I did not re-measure).

### 4.3 Memory

All buffers are allocated in `setSampleRate()` (and the constructor, which allocates twice: once at 48 kHz in the engine's constructor, then at Rack's rate), never in `process()` [test: `no_allocation_in_process`, 0 allocations in `process()` and `reset()`]. Each side's line is a power of two ≥ (longest echo + join + 12). Per instance, delay lines plus the two Drift rings [meas]:

| Rate | Longest echo | Line (floats, each side) | Total |
|---|---|---|---|
| 44.1 kHz | 10.000 s | 524,288 | 4.42 MiB |
| 48 kHz | 10.000 s | 524,288 | 4.46 MiB |
| 88.2 kHz | 10.000 s | 1,048,576 | 8.84 MiB |
| 96 kHz | 10.000 s | 1,048,576 | 8.92 MiB |
| 176.4 kHz | 10.000 s | 2,097,152 | 17.68 MiB |
| 192 kHz | 10.000 s | 2,097,152 | 17.83 MiB |
| 352.8 kHz | 5.944 s | 4,194,304 | 34.00 MiB |
| 384 kHz | 5.461 s | 4,194,304 | 34.00 MiB |
| 705.6 kHz | 2.972 s | 4,194,304 | 34.00 MiB |
| 768 kHz | 2.731 s | 4,194,304 | 34.00 MiB |

Above about 209.5 kHz the 2^21 cap plus the join and guard samples rounds up to 2^22, so half of each line is unused [code].

---

## 5. Clock sync

### 5.1 The edge detector

[code, `Timing.hpp:125-166`; test: `clock_follower`; meas B] A Schmitt trigger on the CLOCK jack, run **every sample** (not at the 16-sample control tick) while a cable is patched:

- A rising edge is a sample at or above **1.0 V** while the detector is low. Measured: pulses of 0.5 V and 0.99 V give no lock; 1.00 V and 1.01 V lock [meas].
- The detector goes low again only at or below **0.1 V**. A high level that falls to 0.2 V and stays there makes no new edge; to 0.05 V it does [meas]. A high level dipping to 0.5 V is not a clock [test].
- A polyphonic clock uses channel 1. NaN and infinity read 0 V.
- An every-other-sample clock gives a period of 2 samples, and no input gives a period under 1 sample [test].
- Pulse width does not matter. The edge is taken at the first sample at or above 1 V.

### 5.2 The period and the lock

- The period is the number of samples between the last two rising edges, an integer count. Its resolution is one sample (20.8 µs at 48 kHz). A 24,000, 24,001 and 24,003 sample clock give Time 0.500000, 0.500021 and 0.500063 s at ×1 [meas].
- **Two edges are needed.** The first edge only starts the count; the second sets the period and locks. Before that, and with CLOCK patched, Time is the knob's own milliseconds [test; meas: 464.3 ms after the first edge, 0.5000 s from sample 24,000, the second edge, on a 0.5 s clock at knob 0.5556]. The tooltip shows milliseconds until then.
- With a locked clock, `Time = fitTime(period × multiple)` for the step the knob and CV choose (section 5.4); then Ratio is applied (section 2.5).
- The counter saturates at 2^30 samples (a 32-bit counter, so Windows' 32-bit `long` is safe) [code].
- CLOCK unpatched: the follower is reset and the knob returns to milliseconds, moving to the new time as any Time change does. **Plugged back in, it needs two fresh edges** [test: `clock_restart`]. A sample-rate change and Initialize also reset the follower; right after a change from 48 to 44.1 kHz Time was the knob's own 464.3 ms with no clock step [meas].

### 5.3 Pauses and slowing: the 4× rule

An edge that arrives more than **4 periods** (strictly more: `count > 4 × period`) after the previous one is "a restart, not a period": the count starts afresh and the held period is not changed. A clock that pauses and starts again therefore keeps its tempo.

Measured, 0.5 s clock locked, one gap of g periods then the clock resumes [meas]:

| Gap | Time after the first resumed edge | After the next edge |
|---|---|---|
| 3.50 periods | 1.750 s (the gap taken as a period) | 0.500 s |
| 3.99 periods | 1.995 s | 0.500 s |
| 4.00 periods | 2.000 s | 0.500 s |
| 4.01 periods | 0.500 s (ignored) | 0.500 s |
| 4.5 and 6 periods | 0.500 s | 0.500 s |

So a drop-out of up to 4 periods swings Time to that long gap for one tick, then back (a one-period excursion); longer ones are ignored entirely.

**A clock that really slows by more than 4× is never followed.** From a locked 0.25 s clock, four edges at 1 s (4.04×), at 2 s and at 3 s intervals left Time at 0.250 s [meas]: each new interval is more than 4× the held period, so each is a restart. The manual says such a clock is "followed one tick late"; the code never takes it. The only ways out are unplugging the clock, a sample-rate change, Initialize, or a slowing in steps of 4× or less (a slowing to a third is followed [test: 24,000 → 72,000 samples; `clock_restart` 0.25 → 0.75 s]). Not covered by a test. Speeding up has no limit.

### 5.4 Steps, the knob and the CV

Ten multiples of the period: 1/4, 1/3, 1/2, 2/3, 3/4, ×1, ×1.5, ×2, ×3, ×4 [test: `multiples`].

```
step = int( clamp(knob + att × cv × 0.1, 0, 1) × 9 + 0.5 )          (att × cv clamped to ±12 first)
```

Each interior step spans 1/9 of the knob; the end steps span 1/18. Measured boundaries on the knob [meas]:

| Step | Name | Knob range | Knob value that "typing" sets (step/9) | Time on a 0.5 s clock |
|---|---|---|---|---|
| 0 | 1/4 | 0 to 0.0556 | 0 | 0.125 s |
| 1 | 1/3 | 0.0556 to 0.1667 | 0.1111 | 0.1667 s |
| 2 | 1/2 | 0.1667 to 0.2778 | 0.2222 | 0.250 s |
| 3 | 2/3 | 0.2778 to 0.3889 | 0.3333 | 0.3333 s |
| 4 | 3/4 | **0.3889 to 0.5000** | 0.4444 | 0.375 s |
| 5 | ×1 | **0.5000 to 0.6112** | 0.5556 | 0.500 s |
| 6 | ×1.5 | 0.6112 to 0.7223 | 0.6667 | 0.750 s |
| 7 | ×2 | 0.7223 to 0.8334 | 0.7778 | 1.000 s |
| 8 | ×3 | 0.8334 to 0.9445 | 0.8889 | 1.500 s |
| 9 | ×4 | 0.9445 to 1 | 1 | 2.000 s |

- **Noon (0.5000 exactly) is the lower edge of ×1.** 0.4990 is 3/4; 0.5000 and 0.5010 are ×1. The tests pin `clockStep(0.5, 0) == 5` [test] and step each knob at step/9, but no test uses the knob's real default. **The default Time knob is 0.4924, which is 3/4**: a fresh Wake with a 0.5 s clock patched and Time untouched gives 0.375 s [meas]. A knob set by eye to "noon" may land on either side.
- The CV is added to the knob before the steps, at 10 V = the whole knob (9 steps) at attenuverter +1. From noon, +1 V stays at ×1 (5.4 → 5), +2 V is ×1.5 (6.3 → 6), −1 V is 3/4, −5 V is 1/4 [test]. A ±6 V ramp at full attenuverter visits all ten steps and never lands between them [test: `clocked`].
- A typed multiple sets the knob to step/9, the middle of the step. With a Time CV patched, the tooltip shows the engine's current step (knob plus CV), so a typed step is offset by the CV.

### 5.5 What the multiples give

- `Time = fitTime(period_seconds × multiple, longest echo)`: while over the longest echo it is halved; while under 10 ms it is doubled. NaN and non-positive read 10 ms [test: `fit`]. Measured on a locked clock (Time in s) [meas]:

| Clock period | 1/4 | ×1 | ×4 |
|---|---|---|---|
| 2 ms | 16 ms (0.5 ms doubled five times) | 16 ms (2 ms doubled three times) | 16 ms (8 ms doubled once) |
| 10 ms | 10 ms | 10 ms | 40 ms |
| 50 ms | 12.5 ms | 50 ms | 200 ms |
| 1 s | 0.25 s | 1 s | 4 s |
| 4 s | 1 s | 4 s | 8 s (16 s halved) |
| 20 s | 5 s | 10 s (20 s halved) | 10 s |
| 40 s | 10 s | 10 s | 10 s |

  (A 4 s clock at ×4 is 8 s [test].) "Doubled" means doubled until it is at least 10 ms, not rounded to the nearest octave.
- The shorter side of a Ratio step is clamped to 10 ms, not doubled (section 2.5).
- At a high sample rate the longest echo is shorter (section 10.1) and the halving uses it.

### 5.6 While locked, frozen, bypassed

- **Frozen:** the clock follower keeps running, Time and Ratio are held, and the new tempo is used when the freeze ends. A 0.5 s clock that becomes 0.25 s while frozen: Time stays 0.500 s while frozen and reads 0.250 s on the first control tick after the release [meas].
- Bypassed: the engine is not run; the follower does not see edges.
- The clock still follows while Time is gliding or crossfading; a Time change that arrives during a Digital crossfade is queued and the latest wins [test: `latest_target_wins`].

### 5.7 Jitter

Every edge sets the period afresh, so a clock with jitter retargets Time on every tick. Measured, 0.5 s clock, ×1, Drift 0, one-sided jitter of 0 to J samples added to each interval, 57 s [meas]:

| J | Tape: largest pitch excursion | Analog: largest pitch excursion | Digital: crossfades started (114 edges) |
|---|---|---|---|
| 0 | 0 | 0 | 0 |
| 1 sample (0.02 ms) | ≤ 3.4 cents (probe's floor) | ≤ 3.4 cents | 0 |
| 3 samples (0.06 ms) | ≤ 3.4 cents | ≤ 3.4 cents | 6 |
| 5 samples (0.10 ms) | ≤ 3.4 cents | 6.8 cents | 33 |
| 24 samples (0.5 ms) | 13.6 cents | 23.8 cents | 101 |
| 48 samples (1 ms) | 23.8 cents | 44.5 cents | 107 |
| 240 samples (5 ms) | 115 cents | 224 cents | 115 |

In Digital, a target 4 samples or more from where the delay is starts a 40 ms linear crossfade (under 4 it glides, bending pitch by a hair; the small steps add up against a delay that is still gliding, which is why 3 samples of jitter already started 6 crossfades). A clock that is not steady to better than a few samples therefore crossfades on every tick; the crossfade can dip and it is audible as a slight breathing on sustained material. A human-played or software clock with 1 ms jitter bends Tape by about a quarter of a semitone on each tick. QA should try a hand-played clock, not only a perfect one. [Whether the actual clock sources in Rack (Squall's Gate, the stock clock) have jitter is [unverified].]

### 5.8 Tooltip and typed values while clocked

While locked the tooltip reads the step, for example "x1 clock", "3/4 clock", "x1.5 clock" [code, `Wake.cpp:62-63`; the same text feeds typed values]. When CLOCK is patched but not locked (fewer than two edges) it shows milliseconds. The tooltip is read from the UI thread without synchronisation [issue #62].

---

## 6. Style

### 6.1 The button and the lights

- STYLE is a momentary `CapButton` (shift-click latches it down). Each rising edge of the param steps `style = (style + 1) % 3`: Tape → Analog → Digital → Tape [code, `Wake.cpp:179-180`]. The three lights show the style at brightness 0 or 1 with no fade [code].
- The style is a member of the module, saved as `"style"`, set to 0 by Initialize, not randomized (section 3.2), lost if pressed while bypassed (section 3.3), and can be pressed while frozen.
- The engine takes the style integer from the frame; out-of-range values clamp to 0..2 inside the engine (−7 and 99 were fed in by the hostile test) [test].
- Default Tape. A new module's Tape light is lit [code].

### 6.2 What a style sets

[code, `WakeTuning.hpp:21-38`]

| | Tape | Analog | Digital |
|---|---|---|---|
| Loop high-pass | 40 Hz | 80 Hz | none |
| Loop low-pass | 6 kHz at every Time | 5 kHz at 10 ms, falling log-wise in Time to 1.5 kHz at 1 s and beyond | none |
| Drive (tanh, unity at small signal) | 1: saturates at ±10 V | 2: saturates at ±5 V | none |
| When Time moves | glide, time constant 0.15 s, within 2 octaves | glide, 0.08 s, within 2 octaves | 40 ms linear crossfade |

- The loop's order in each pass: Tone (low-pass or high-pass, or none) → style low-pass → style high-pass → drive → 5 Hz DC block → the 12/16 V bound [code, `Colour.hpp:72-114`]. Every filter is one pole.
- Analog's low-pass corner depends on each side's own echo time (`seconds_[s]`), so with Ratio off noon the two sides have different brightness.
- A filter that is "none" is out of the path exactly: Digital's loop at Tone noon is within 0.02 dB at 15 kHz, and within 1 dB from 30 Hz to 15 kHz [test: `digital_flat`].

### 6.3 What each style does to one pass

First echo of a 0.5 V sine (Feedback 0, Tone noon, Mix 1, Diffuse 0, Drift 0), level re the input [meas, `m_style.cpp`]. This is also the loss per repeat at small signal.

| Style, Time | 50 Hz | 100 Hz | 200 Hz | 1 kHz | 3 kHz | 6 kHz | 10 kHz | 15 kHz |
|---|---|---|---|---|---|---|---|---|
| Tape, any Time | −2.22 | −0.69 | −0.21 | −0.15 | −0.95 | −2.82 | −5.19 | −7.24 |
| Analog, 30 ms | −5.61 | −2.22 | −0.73 | −0.39 | −2.16 | −5.35 | −8.52 | −10.93 |
| Analog, 100 ms | −5.61 | −2.23 | −0.74 | −0.63 | −3.43 | −7.46 | −10.99 | −13.53 |
| Analog, 300 ms | −5.61 | −2.23 | −0.75 | −1.01 | −4.96 | −9.62 | −13.35 | −15.96 |
| Analog, 1 s and 3 s | −5.62 | −2.24 | −0.79 | −1.68 | −6.99 | −12.13 | −16.00 | −18.67 |
| Digital, any Time | −0.05 | −0.01 | −0.01 | 0.00 | 0.00 | 0.00 | 0.00 to −0.01 | 0.00 to −0.02 |

Printed by the tests [test: `tape_and_analog`]: −3 dB corners of Analog 5,473 Hz at 10 ms and 1,674 Hz at 1 s and at 8 s; Tape 6,432 Hz at both ends. Third harmonic of a ±5 V sine in the loop: Tape −21.2 dB, Analog −13.0 dB.

Drive, first echo of a 1 kHz sine at T 100 ms, level and third harmonic re the fundamental [meas]. Above 8 V the output limit (8 V knee) adds its own compression and Digital's 10 V row is the limiter alone:

| Input | Tape level / 3rd | Analog level / 3rd | Digital level / 3rd |
|---|---|---|---|
| 0.5 V | −0.15 dB / −73.9 dB | −0.63 dB / −62.8 dB | 0.00 dB / −133 dB |
| 2 V | −0.23 / −49.9 | −0.91 / −39.0 | 0.00 / −133 |
| 5 V | −0.65 / −34.4 | −2.22 / −24.5 | 0.00 / −133 |
| 8 V | −1.34 / −27.0 | −4.01 / −18.4 | 0.00 / −133 |
| 10 V | −1.91 / −23.7 | −5.23 / −16.1 | −0.16 / −36.4 (limiter) |
| 20 V | −5.24 / −15.0 | −10.23 / −11.6 | −4.33 / −13.1 (limiter) |

### 6.4 When Time moves

**Tape and Analog (glide).** The delay slides toward its target along an exponential (time constant 0.15 s Tape, 0.08 s Analog) with its slope limited, so pitch stays within two octaves: the delay may change by at most +0.75 samples a sample (pitch down two octaves) and −3 samples a sample (up two octaves) [test: `bounds`]. Doubling Time bends down, halving bends up. The glide lands exactly [test]. The pitch of a sine in the delay, 300 → 600 ms, ran from 110 Hz (the two-octave floor) to 440 Hz and settled at 440 Hz in Tape and Analog, and stayed at 440 Hz in Digital [test, printed].

Pitch and time of the bend, from the tap delay's slope, Drift 0 [meas, `m_style.cpp`]; "to 50 / 10 cents" is the last time the bend exceeded that; "lands" is when the delay stops moving (the pitch is inaudibly small well before):

| Change | Style | Pitch range | Beyond 50 cents until | Beyond 10 cents until | Lands |
|---|---|---|---|---|---|
| 10 → 20 ms | Tape | −119 .. 0 cents | 128 ms | 368 ms | 1.96 s |
| | Analog | −230 .. 0 | 118 ms | 246 ms | 1.05 s |
| 100 → 200 ms | Tape | −1,894 .. 0 | 473 ms | 737 ms | 2.31 s |
| | Analog | −2,400 .. 0 | 315 ms | 455 ms | 1.24 s |
| 300 → 600 ms | Tape | −2,400 .. 0 | 746 ms | 1,037 ms | 2.58 s |
| | Analog | −2,400 .. 0 | 585 ms | 739 ms | 1.51 s |
| 600 → 300 ms | Tape | 0 .. +1,899 | 633 ms | 901 ms | 2.47 s |
| | Analog | 0 .. +2,400 | 390 ms | 533 ms | 1.32 s |
| 1 → 2 s | Tape | −2,400 .. 0 | 1.70 s | 3.30 s | 3.51 s |
| | Analog | −2,400 .. 0 | 1.53 s | 2.33 s | 2.44 s |
| 2 → 1 s | Tape | 0 .. +2,400 | 887 ms | 1,179 ms | 2.72 s |
| | Analog | 0 .. +2,400 | 629 ms | 784 ms | 1.55 s |
| 1 → 5 s | Tape | −2,400 .. 0 | 5.76 s | 7.20 s | 7.51 s |
| | Analog | −2,400 .. 0 | 5.56 s | 6.28 s | 6.44 s |
| 5 → 1 s | Tape | 0 .. +2,400 | 1.89 s | 2.18 s | 3.72 s |
| | Analog | 0 .. +2,400 | 1.63 s | 1.78 s | 2.55 s |

  The two-octave cap (−2,400 / +2,400 cents) is reached when the first step of the exponential exceeds the slope limit: by a **doubling** from a Time above 112 ms in Tape and 60 ms in Analog (the change equals the old Time; limit 0.75 samples a sample), and by a **halving** from a Time above 900 ms in Tape and 480 ms in Analog (the change is half the old Time; limit 3 samples a sample) [code arithmetic, consistent with the table: 100 → 200 ms is −1,894 cents in Tape and the cap in Analog; 600 → 300 ms is +1,899 in Tape and the cap in Analog]. A long Time change takes seconds to settle in either style; Analog settles about 1.4 to 2 times sooner.

**Digital (crossfade).** A change of 4 samples or more starts a 40 ms linear crossfade from the old read to the new one; a change that arrives during a fade is queued and the latest wins; the fade's gains sum to 1, so it never adds level [test: `crossfade_adds_no_level`, a unit sine's crossfade peaks at 1.001 or less]. A 300 → 600 ms step reads 440 Hz throughout, with a step of at most 1.5× the sine's own slope [test: `crossfade_no_bend`]. A change of under 4 samples glides instead (time constant 0.15 s), bending pitch by less than a cent.

In Digital, a Time CV that moves by more than 4 samples between control ticks starts a crossfade every tick; the moving read is a train of 40 ms crossfades. A slow, shallow Time CV (changes under 4 samples a tick) glides. [test: `crossfades_add_no_gain`: Digital at 80 % Feedback under a 0.3 Hz ±0.02 V Time CV decays to under 1 mV RMS within a minute.]

### 6.5 Changing the style

- The press changes `style`; the engine moves the style's constants (filter corners, drive, glide) from the old values to the new in 30 ms, one step per 16-sample tick; the "crossfade or glide" choice flips at 15 ms [code, `blendStyles`; test: `blend` checks the ends are the styles and the halfway point is half]. A corner of "none" blends through the top of the band (20 kHz) and is set to exactly none at the end.
- While the constants ramp, the glide's time constant is recomputed each tick with `exp` and `pow` (libm on the audio thread, for the length of the ramp only: about 90 ticks at 48 kHz) [code, `WakeEngine.hpp:290-295`]. The libm check does not press the button.
- No click: with a 9 V 50 Hz sine and the style pressed during 4 s, the largest step is 3× the sine's slope or less [test: `style_change_ramps`]. Largest sample step in the 200 ms after a press against the 200 ms before, a sine into T 50 ms at 50 % Feedback [meas]:

| Press | 50 Hz, 2 V and 9 V | 440 Hz, 2 V and 9 V |
|---|---|---|
| Tape → Analog | 1.00× | 1.00× |
| Digital → Tape | 1.00× | 1.00× |
| Analog → Digital | 1.91× and 2.05× | 1.08× and 1.19× |

  The Analog → Digital rise is the loop passing more level, not a click: the step before is the quieter style's. All steps are inside the sine's own slope scaled by the level.
- The repeats already in the loop were coloured by the old style and stay so; each later pass takes the new colour. The change is therefore heard as the echoes progressively changing, not as an instant switch.

### 6.6 Feedback in each style: how far 110 % sustains

Per-pass loss is section 6.3; 110 % is +0.83 dB a pass. At small signal the loop gain at 110 % is therefore the table value + 0.83 dB: Tape grows from about 100 Hz to about 2.5 kHz (+0.14 at 100 Hz, +0.62 at 200 Hz, +0.68 at 1 kHz, −0.12 at 3 kHz, −1.39 at 50 Hz); Analog at 1 s and beyond grows only around 200 Hz (+0.04) and falls elsewhere; at 30 ms Analog grows at 200 Hz and 1 kHz (+0.10 and +0.44); Digital grows at every frequency (+0.78 to +0.83). The drive then lowers the gain as the level rises, which is what settles each style.

Measured, 1 V 220 Hz sine for 5 s then silence, 110 % Feedback, T 100 ms, Mix 1, RMS per 5 s [meas, `m_seam.cpp`]:

| | Tone noon | Tone full left |
|---|---|---|
| Tape | 4.30, then 3.70 V, constant | 2.83, 1.54, 0.59, 0.30, 0.20 (at 20 to 25 s), then rising to 1.68 V by 60 s |
| Analog | 2.14, 1.33, 1.09, 1.07, 1.06, 1.16, 1.54, then 1.62 V constant | 2.38, 0.79, 0.02 V, then 0.00 from 15 s |
| Digital | 8.94, then 10.0 V (the output ceiling) constant | 2.76, 6.95, 9.11, then 9.2 V; mean DC at 30 s +0.031 V |

So the manual's "settles into a held, saturated wash" holds for Digital; Tape settles after a dip and a recovery with Tone left; Analog with Tone left does not sustain at all. These are what the tests allow: the loop is bounded, and at 110 % each style "should still be sounding" (above 0.1 V) in the tests' own 5 ms loop [test: `bounded`].

### 6.7 Style and the other sections

- Freeze: a style press while frozen has no effect on the held sound; the style in force at the release decides whether the release re-targets by crossfade (Digital) or glide (section 7.5).
- Diffuse through 0 moves the echo by the smear's latency: a pitch blip in Tape and Analog, a crossfade in Digital (section 2.9).
- Drift and the tap's glide are independent: Drift's offset is added to the delay in every style.
- Clock jitter bends Tape and Analog and crossfades Digital (section 5.7).

---

## 7. Freeze

### 7.1 What freezes

[code, `WakeEngine.hpp:127-166`] Either the **FREEZE button** (the Freeze param above 0.5) or the **GATE jack** freezes; both together is the same as either. The gate is a Schmitt trigger: it goes high at or above **1.0 V**, low at or below **0.1 V** (0.99 V does not freeze; 1.00 V does; a gate that falls to 0.11 V stays frozen, to 0.10 V releases) [meas]. A polyphonic gate uses channel 1; NaN reads 0 V. Both are evaluated every sample.

The light (Freeze cap, light id 3) shows the engine's `frozen` result: lit while frozen by either means. It lights when the freeze takes effect, not when it is asked for.

**A freeze waits for a crossfade to finish.** If either side's tap is mid-crossfade (a Digital Time change, or the release fade of a previous freeze), a request is held until the fade ends: at most 40 ms. Measured request-to-frozen delay after a Digital Time change 250 → 400 ms: with no fade running the freeze takes effect at once; asked in the same instant as the change (before the next control tick starts the fade) it is 1 sample; asked 100 samples after the change, 1,822 samples (38.0 ms); at 480, 1,442 (30.0 ms); at 960, 962 (20.0 ms); at 1,500, 422 (8.8 ms); at 1,900, 22; at 2,000 and later, 1 [meas]. The longest wait seen is 38.0 ms. The wait exists because cutting a fade short would step. The wait applies to freezing, not to releasing.

### 7.2 What freezing does

- Writing stops. Each side plays the **last `round(T_side × fs)` samples** of its line round and round, T_side being that side's echo time at the moment of freezing [code: `tap.freeze(JOIN_S, seconds_[s] × fs, true)`]. Measured with Ratio "R 1:2", T 400 ms: left loop 19,200 samples (0.4000 s), right loop 9,600 samples (0.2000 s), by autocorrelation [meas]. The loop is one echo time, not half of it [test].
- Playback carries on from the last live read with no step: the freeze itself steps at most 1.2× the sine's slope [test: `refreeze_does_not_click`]. The loop's wrap fades from the loop's end into the audio recorded just before its start (a 10 ms join), so the join does not click; a frozen loop of 4,800, 480, 96 and 16 samples steps at most 0.0065, 0.0120, 0.0112 and 0.0123 V against allowances of 0.0139, 0.0264, 0.0930 and 0.5097 [test: `test_tap`, printed].
- The loop is what was heard: the loop's colour and Diffuse sit on the way into the line, so a frozen sound is the echo that was playing, DC-blocked and bounded like the rest. Its level and brightness match the live echo within 0.5 dB in Tape, Analog and Digital (Tone 0.2 and 0.9 too) [test: `frozen_is_what_was_heard`]. 5 V DC in leaves 0.05 V or less in the frozen sound [test].
- **Exact at Drift 0.** A frozen loop repeats unchanged for five minutes (1,000 loops of 0.3 s, noise still arriving at the input, Time, Ratio, Ping Pong, Feedback, Tone, Diffuse and the style all moved during the hold): the largest difference is under 1e-5 V on both sides [test: `freeze`]. The spec asked for 0.5 dB; the test is much tighter.

### 7.3 What is held, and what still acts

| Control while frozen | Effect |
|---|---|
| Time, Time CV, Ratio | **held**: Time and Ratio keep the values from the freeze; the change applies after the release. `seconds()` stays at the frozen value [test: 0.3 s and 0.3 s after Time → 1.7 s and Ratio → "L 1:2"]. |
| Clock | the follower keeps tracking; Time is held; the tempo in force at the release is used (section 5.6) |
| Ping Pong | held (its target is not updated) [test] |
| Feedback, Tone, Diffuse, Style | no effect on the held sound [test: moved during the 5 minute hold]. Style changes the lights and the style ramp, and takes effect after the release. |
| Mix, Mix CV | act on the output as usual |
| Input | still passes to the output through Mix; nothing new is written |
| **Drift** | **still acts**: the loop's playback rate follows Drift's offset slope, so a frozen sound is not static. At the default 15 % a frozen 440 Hz echo wanders −1.7 .. +1.9 cents at T 0.3 s and −3.6 .. +2.8 cents at T 2 s; at Drift 1, −11.4 .. +12.5 cents at 0.3 s and −24.2 .. +18.7 at 2 s [meas, 60 s]. At Drift 0 the loop is exact; with any Drift a noise loop is decorrelated from one pass to the next (a pass differs from the one before by +2.2 dB re the loop at Drift 0.15, +3.0 dB at Drift 1 [meas]). |
| Randomize | cannot freeze or release (Freeze is not randomized) |

### 7.4 Which controls keep running in the background

While frozen, Tone, the style filters and Diffuse are fed silence, so they hold nothing stale when writing starts again (they run on zeros). The write gain is held at 0 and ramps to 1 on release.

### 7.5 Releasing

[code, `WakeEngine.hpp:147-165`, `DelayTap::release`]

1. The tap crossfades from the loop to the live read at the held target, a **40 ms linear crossfade** (`CROSSFADE_S`). In Digital, or if the style says crossfade, the delay is set to the held target at once (no bend); in Tape and Analog the held target is reached by the usual glide.
2. Writing restarts at another place in the sound. To avoid a step in the line, the newest `min(writes since the last release, 480)` samples (10 ms) are faded in place to silence (`DelayLine::fadeOutNewest`), and the write gain ramps 0 → 1 over 10 ms (`SWITCH_S`).
3. Time, Ratio and Ping Pong take their new values at the next control tick; in Digital they start a second crossfade after the release's fade ends.

Measured on a 2 V 220 Hz sine, T 0.2 s and 1.0 s, frozen 2 s then released, one-cycle (218 samples) RMS windows, re the steady wet [meas, `m_seam2.cpp`]:

| Style | Lowest in the first 62 ms | Lowest around T after the release |
|---|---|---|
| Digital | −3.1 dB at 19 ms | **−18.5 dB** at T − 2 ms |
| Tape | −3.1 dB at 18.8 ms | **−19.0 dB** |
| Analog | −3.1 dB at 18.8 ms | **−19.1 dB** |

The −3 dB dip is the linear crossfade at its middle between two signals that are not identical. **The dip near T is the faded-out end of the old line coming round**: a notch about 20 ms wide (from the 10 ms fade-out and the 10 ms write ramp) is heard once, T after every release, and then (reduced by Feedback) again on each repeat. Not in the spec, the manual or the guide, and not covered by a test. QA should listen for it on sustained material; it is the price of a step-free join.

The jump a freeze or a release makes is bounded: freezing, the joins and releasing step at most 4 × (the output's peak) × 2π·71.3/48,000 on a 71.3 Hz sine at Feedback 50 % [test: `freeze`]; re-freezing inside the release fade (gaps of 48, 480 and 2,400 samples) steps at most 4 × 4 V × 2π·220/48,000 = 0.46 V, in Tape and Digital [test: `refreeze_does_not_click`]; freezing 15 ms after a Digital Time change is inside the same bound [test].

### 7.6 The gate at audio rate

A gate that toggles every sample at 768 kHz costs less than 4× the time of no gate plus 0.05 s (the test's bound) and does not mute the delay: an echo still returns at full level afterwards (peak over 4 V from a 5 V pulse) [test: `gate_cost_is_bounded`]. Gates from every other sample to every 2,000th, with an irregular clock of 2 samples to seconds, at 10 V noise and Feedback 90 %, left the output finite, inside 10 V, and Time inside 10 ms .. 10 s [test: `gates_at_audio_rate`].

### 7.7 Rate change, reset and a latched freeze

- `setSampleRate()` and `reset()` clear the line, the filters, the clock and `frozen_`/`gateHigh_`, and **snap every control**. They do not touch the Freeze **button's param**, which Rack keeps, or the gate voltage still on the jack. So with the button latched on or the gate high, **the very next sample freezes again** and the loop is a silent copy of an empty line: after a change from 48 to 44.1 kHz the first sample was frozen and the output stayed 0.0000 two thousand samples later; with the gate high a change to 96 kHz did the same [meas]. The manual and the guide say the rate change "unfreezes". It does so only if the button is off and the gate is low. The hostile test sets `freeze = false` before it checks, so it passes.
- Initialize: `reset()` plus Rack's param reset sets the Freeze button off, so the module unfreezes [test: `rate_change_recovers`: after reset the delay is empty and unfrozen; `engine.reset()` alone with the latch released read `frozen = 0` [meas]].
- Bypass leaves the freeze state as it was and shows the Freeze light dark (section 3.3).
- A latched Freeze with an empty delay holds silence. That is the reason Randomize must not latch it (the 2.0.21 fix).

### 7.8 Saved state

The Freeze button is saved with the patch (a param). The gate's state and the delay's contents are not. A patch saved frozen and loaded runs frozen on an **empty** line: silence until the button is released and the delay refills. [code inference; unverified in Rack.]

---

## 8. Level, bound and stereo

- **No level law.** The echo is the input recorded, coloured once per pass. Digital at Feedback 0 returns 0.00 dB (50 Hz −0.05 dB); Tape −0.15 dB at 1 kHz; Analog −0.4 to −1.7 dB at 1 kHz depending on Time (section 6.3).
- **The loop bound** (12 V knee, 16 V ceiling) acts only above ±12 V inside the loop; "a ±10 V signal passes Digital's loop untouched" [code comment; test: Digital at 10 V has a third harmonic of −80 dB or lower]. Measured with the output limiter in the path the Digital echo of a 10 V sine is 9.81 V with a −36 dB third harmonic: that is the output's 8 V knee, not the loop [meas].
- **Output soft limit:** identity to 8 V; 8 + 2·tanh((|x|−8)/2) beyond; never reaches 10 V. A ±10 V noise at 110 % Feedback in each style peaks between 8 and 10 V [test: `limit`].
- **DC:** the loop's 5 Hz DC block means DC never builds in the loop. 10 V DC in at 110 % Feedback leaves a mean under 0.5 V (the test's bound) at the output; measured: +0.031 V in a held Digital wash [meas]. 10 V DC alone is gone within 2 s (under 5 mV) [test].
- **Stereo.** L-only and R-only inputs give identical outputs [test]. With Ping Pong off and Drift 0.5, the sides do not cancel in mono: the mono sum keeps at least 0.6 of one side's power [test: `stereo`]. With R unpatched L is the average of the sides, to 1e-5 [test].
- **Silence.** Wake has no sleep: the delay runs whenever the module does (`idle` costs the same as `outputs`, section 9). The tail reaches exact silence: with Feedback down, a decaying loop is exactly 0.0 V within 30 s in each style, with no subnormals at the output and no slowdown in the silent tail [test: `minutes_at_110`, `denormals`].

---

## 9. CPU

`bench` cases [code, `bench.inc`], each at 48 kHz:

- **idle**: defaults, nothing patched (the delay runs).
- **outputs**: noise into IN L, both outputs, knobs still.
- **busy**: Analog, Diffuse and Drift full, Ping Pong, Feedback 0.8, noise in both sides, the Time, Feedback and Mix CVs moving, a 5 Hz clock.

| Case | Budget Apple arm64 | Budget other (provisional) | TESTING.md measured | My run, 1 instance | My run, 16 instances |
|---|---|---|---|---|---|
| idle | 0.35 | 1.05 | 0.155 | 0.142 / 0.145 / 0.145 | 0.181 / 0.182 / 0.183 |
| outputs | 0.35 | 1.05 | 0.157 | 0.146 / 0.148 / 0.160 | 0.181 / 0.184 / 0.184 |
| busy | 0.4 | 1.2 | 0.178 (16 instances) | 0.163 / 0.168 / 0.168 | 0.207 / 0.213 / 0.215 |

Units: % of one core per instance; three runs of 5 s each [meas]. The bench passed its checks (`bench --check`: 15 module declarations complete). The x86-64 budgets are 3× the Mac's and unmeasured on native x86-64; **Windows CPU is not covered by any automated budget**. My 16-instance figures are 15 to 20 % above TESTING.md's (idle and outputs 0.181 to 0.184 against 0.155 and 0.157; busy 0.207 to 0.215 against 0.178) and well under the budgets; the Mac was running other work, so read them as the same order, not as a regression.

Per-sample cost against the host rate, in-process, 3 V stereo noise in, Drift 15 %, best of three 10 s runs [meas]. The engine runs at the host rate with no rate divider, so cost rises in proportion:

| Host rate | defaults | Analog, Diffuse, Drift 1, Feedback 0.8, Ping Pong |
|---|---|---|
| 44.1 kHz | 0.090 % | 0.132 % |
| 48 kHz | 0.104 % | 0.145 % |
| 96 kHz | 0.200 % | 0.291 % |
| 192 kHz | 0.405 % | 0.579 % |
| 384 kHz | 0.797 % | 1.125 % |
| 768 kHz | 1.561 % | 2.257 % |

- The libm check (`make libm-calls`, Linux only) requires zero libm calls per sample in idle, outputs and busy. I did not run it (macOS). The libm calls in the engine are the glide's constants while a style change ramps (section 6.5) and the setup in `setSampleRate`, which allocates and is not part of `process()` [code].
- `make engine-globals` ran and passed: no writable global state, so instances are independent.
- A Drift ring renormalisation (one pass over up to 131,076 doubles) runs inside `process()` when the running total passes 1e9: practically unreachable [issue #62].

---

## 10. Sample rates and robustness

### 10.1 Sample rates

[code, `WakeEngine::setSampleRate`; test: `extreme_rates`]

- Accepted: any finite rate with 0 < rate ≤ 768 kHz. The tests run 1 Hz, 10 Hz, 1 kHz, 44.1, 192 and 768 kHz, and bad rates (0, −48 kHz, NaN, infinity, 1e9) are ignored, keeping the last good rate. The module constructs at Rack's rate (or 48 kHz).
- **The longest echo is the line's 2^21 samples** at high rates: `maxSeconds = min(10 s, 2,097,152 / fs)`: 10 s at all rates up to 192 kHz, 9.986 s at 210 kHz, 5.944 s at 352.8 kHz, 5.461 s at 384 kHz, 2.972 s at 705.6 kHz and 2.731 s at 768 kHz [meas; test: 768 kHz is 2,097,152 / 768,000 s within 1e-3]. The tooltip clamps to it; Time and the clocked halving use it.
- **A change of rate clears the delay and every filter, forgets the clock and snaps the controls**; times stay the same in seconds [test: `rate_change_recovers`: rates 48 → 96 → 44.1 → 768 → 8 → 48 kHz, with the freeze button on during alternate legs; after each, silence at the output]. See section 7.7 for the latched freeze.
- The tests at 1 Hz to 768 kHz keep the output finite and inside 10 V at Feedback 100 %, Diffuse 1, Drift 1, Time CV +3 V and a toggling Freeze [test].
- **Not measured:** the echo-time accuracy at 192 kHz and above (the test runs 44.1, 48 and 96 kHz); the clock's period at high rates; the pitch figures of section 6.4 at other rates.

### 10.2 Hostile input and numerical safety

[test: `test_hostile`, ASan and UBSan]

- **NaN, ±infinity, ±3.4e38 and 1e30** on IN L, IN R, any CV, the clock, the freeze gate or any param, in turn, plus the style out of range, for 400,000 samples: output finite and inside 10 V. After NaN on both inputs, an echo still returns (peak over 1 V) [test: `hostile_inputs`].
- **Time CV as ±10 V noise, in each style** (attenuverter ±1, Feedback 90 %): the read point stays inside the line and the output inside 10 V [test: `time_cv_as_noise`].
- **Seeded fuzz**: 6 seeds of 300,000 samples with random params (some past their range), CVs ±12 V, the style, Freeze, Ping Pong, patching and clock toggles, resets and ±10 V noise: finite and inside 10 V every sample [test: `seeded_fuzz`].
- **No allocation** in `process()` or `reset()` [test].
- **Denormals:** an echo's tail through each style at Feedback 0.6, Diffuse 0.5 has no subnormal output value and reaches exactly 0.0 V on both sides within 30 s; the last second is not slower than 4× the first plus 0.05 s [test: `denormals`].
- **Aliasing:** not measured. Issue #67 lists Wake as the likeliest to fail: the drive and the soft bound inside the loop run at the base rate, and the moving read point has no band-limit [issue #67]. The guide asks for a listening check.
- **The 5 minute freeze, 4 minute bounded loop and 10 minute 110 % runs** are in the tests (sections 6 and 7); there is no separate soak target for Wake.

---

## 11. Open issues

### 11.1 Filed issues: do not re-file

Open issues in `ryanarneson/RkaModules` that concern Wake, at the time of writing. When you reproduce one, reference its number and add data to the existing issue.

| # | Title | QA impact |
|---|---|---|
| 58 | Wake: Tone jumping straight to noon switches its filter out in one step | A one-tick move of Tone from below 0.49 to 0.49 or above steps the loop; heard once per echo, then in each repeat. Larger than the title says: also to the thin side and to 1.0 (section 2.7). A 3 V 100 Hz sine steps 0.60 V against its own 0.039 V. By hand, sweeping, it does not step. Sources: a preset, typed value, Randomize. |
| 62 | Wake: internal minors from the final review | (1) The Drift ring is renormalised inside `process()` past 1e9 (practically unreachable). (2) The tooltip reads the engine's clock step and longest time from the UI thread unsynchronised (benign). (3) `no_clicks` uses a 50 Hz sine and a 6× slope allowance (the plan said 100 Hz), and the DC check at 110 % allows 0.5 V. None for QA. |
| 61 | DelayLine: fractional reads lose precision at long delays (before Eddy) | The Hermite read takes a float delay: 1/32 sample at 10 s and 48 kHz. The issue measured −47.5 dB error at 100 ms and −42.8 dB at 9.8 s on a moving 5 kHz sine. Listen to long Time sweeps in Tape and Analog for a faint noise floor on bright material. |
| 64 | Polyphony: modules should be polyphonic where it makes sense | Wake sums polyphonic audio and reads CVs from channel 1. Its row says "probably stay as they are; confirm". Do not file polyphonic CV as a defect. |
| 67 | Aliasing: measure every module that makes or bends sound, and fix what fails | Wake's drive and loop bound and its moving read point are unmeasured. The issue names Wake the likeliest to fail. Listen to loud input at Feedback near 100 % in Tape and Analog. |

Closed and explaining present behaviour: **#59** (bypass routing and the style lights in bypass, fixed in 2.0.33, `7107020`; section 3.3) and **#60** (typed clock multiples, fixed in 2.0.33, `1d2c793`; section 2.2). **#57** is the merged pull request that added Wake.

### 11.2 Found while preparing this document (not yet filed)

These were found against the 2.0.39 code. I did not file them. They are candidates; check `gh issue list` before filing.

1. **A clock that slows by more than 4× is never followed** (section 5.3). Manual says "one tick late". Held at the old tempo until unplug, rate change or Initialize.
2. **The default Time knob is the 3/4 clock step, not ×1** (section 5.4). Spec and manual say noon is ×1. The spec's default is 300 ms (knob 0.4924) and its "noon" is ×1, so the two cannot both hold with an edge at 0.5000; which to change is the developer's call.
3. **A rate change with Freeze latched or the gate high re-freezes at once** (section 7.7). Manual and guide say it unfreezes.
4. **Randomize latches Ping Pong** (section 3.2). The guide's wording says it does not; the manual is silent. A design decision rather than a defect, like Freeze's.
5. **#58 is wider than its title** (section 2.7).
6. **A notch T after every Freeze release** (section 7.5), about −18.5 dB over about 20 ms on a sine. Probably acceptable; record it.
7. **Diffuse crossing 0 bends pitch** by about +1 semitone (Tape) and +1.8 semitones (Analog) as it comes in, and −1 / −2 semitones as it goes out (section 2.9). The manual says only "a few milliseconds".
8. **Style out of range loads as Tape** where Fog clamps (section 3.1). Harmless in practice (the engine writes only 0..2).

---

## 12. What the automated suite already covers

All of these passed on the 2.0.39 tree in my run. Wake has five test programs: `test_tap` (`DelayTap`, the shared moving read point), `test_timing`, `test_loop`, `test_engine` and `test_hostile`. Run times on an M-series Mac: 0.2 s, 0.1 s, 0.4 s, 2.1 s and 8.0 s (ASan and UBSan).

### 12.1 test_tap (DelayTap)

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| steady | a snapped tap reads a ramp 1000.25 samples back | < 1e-3; at rest |
| glide_bends_and_lands | doubling bends down within 2 octaves, never up; lands exactly; halving bends up within 2 octaves | pitch 105..460 Hz; 500..1,800 Hz; lands exactly (printed: 180 .. 440 Hz in 47 blocks) |
| crossfade_no_bend | a crossfaded change holds 440 Hz, steps at most 1.5× the slope, a 40 ms fade is 1,920 samples | within 1 %; ±2 samples |
| crossfade_adds_no_level | a unit sine's crossfade never exceeds it; a one-sample change glides, ten fades | ≤ 1.001 |
| latest_target_wins | targets inside a fade end at the last; queued fades step ≤ 1.5× slope | exact |
| freeze_repeats, freeze_longer_loop, freeze_join | a frozen loop repeats exactly, is the line's own audio, carries on from the last read, joins without a step (loops of 4,800, 480, 96, 16) | < 1e-6; steps 0.0065 / 0.0120 / 0.0112 / 0.0123 V |
| freeze_mod | a mod slope plays the loop at 1 − slope | within 0.4 % |
| bounds | hostile targets and mods stay inside the line; the delay moves ≤ +0.75 / −3 samples a sample | exact |

### 12.2 test_timing

| Test | Guarantees | Tolerance |
|---|---|---|
| time_law | knob 0 = 10 ms, 1 = 10 s, default 0.4924 = 300 ms, 1000^k law and its inverse, NaN reads 10 ms | 1e-4; 1 % |
| multiples, typed_multiples | the ten multiples; noon (0.5) = ×1; CV moves along the steps; typed names round-trip ("x1.5 clock", "1/2", spaces, case); wrong text names none | exact |
| ratios | the seven steps at 600 ms; the 10 ms floor; clamps; names | 1e-6 |
| fit | 45 s → 5.625 s; 1 ms → 16 ms; NaN, 0, negative → 10 ms; infinity → the longest echo | exact |
| clock_follower | not locked until 2 edges, period within 1 sample; a stopped clock keeps its period; a long pause is not a period; slowing to a third is followed; ripple on a high level and pulses under 1 V make no edge; period 2 samples at the fastest; NaN/inf never give a period under a sample | exact |

### 12.3 test_loop (Colour, Smear)

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| digital_flat | Digital's loop within 1 dB, 30 Hz..15 kHz; noon is out of the path (< 0.02 dB at 15 kHz); a ±5 V sine passes within 0.01 dB; third harmonic of ±10 V < −80 dB | |
| tone | each Tone step lowers 4 kHz and 100 Hz; corners 500 Hz and 1 kHz within 15 %; the other end of the band left alone | 509 and 998 Hz |
| tape_and_analog | each style ≥ 3 dB down at 8 kHz and ≥ 1 dB at 20 Hz; Analog's corner at 1 s under half its 10 ms corner; Tape's does not move; Analog grittier than Tape by ≥ 6 dB (third harmonic); Tape passes 0.1 V 1 kHz within 0.5 dB | 5,473 / 1,674 Hz; −21.2 / −13.0 dB |
| blend | a half blend is half; the ends are the styles; blending to "none" opens through the band | exact |
| bounded | 4 minutes at 110 % (noise, 10 V DC, silence) per style: finite, ≤ 16 V; mean DC < 0.5 V; still sounding at 110 % (> 0.1 V); at 50 % 60 dB gone in 30 passes; exact silence; DC alone gone in 2 s (< 5 mV); NaN does not stay | |
| smear_zero_exact, smear_spreads | Diffuse 0 is bit-exact in and back out; more Diffuse spreads further; centroid < 10 ms; level within 2 dB; latency matches the centroid within 0.5 ms; sides differ; engaging does not click | 8.9 / 12.4 / 18.2 ms |

### 12.4 test_engine

| Test | Guarantees | Tolerance (measured) |
|---|---|---|
| echo_time | echo at T within 1 sample or 0.1 % (4 samples × fs/48 kHz in Tape and Analog), at 44.1, 48, 96 kHz, 10 ms, 0.3, 1, 10 s | |
| echo_time_diffused | Diffuse 0.3 and 1: centroid within 48 samples of T, level within 2 dB | |
| ratio_times | the seven steps, within 1 sample | |
| clocked | each knob step on a 0.5 s clock; typed names; CV on the grid and reaches all ten steps; 4 s clock ×4 = 8 s; stopped clock holds; unpatched returns to the knob | 1e-3 |
| feedback_6db | Digital 50 %: successive echoes −6 dB; first echo 0 dB; Feedback CV 5 V gives 0.55 | ±0.5 dB (−6.03, −6.02); ±0.02 |
| time_step_per_style | Time 300 → 600 ms: Tape and Analog bend down within 2 octaves and settle at 440 Hz; Digital stays at 440 ±1 % | printed 110 .. 440 / 440 .. 440 |
| drift | Drift 0 still; full: pitch ≤ 35 cents, > 10 at 0.3 s and 3 s, > 18 at 3 s, < 8 at 50 ms; sides correlate < 0.3; an echo moves | printed ±2.9 / 15.4 / 25.9 cents |
| freeze | the latch and the gate freeze; a 5 minute loop repeats to 1e-5 while everything moves; Time and Ratio held; left and right loops keep their own lengths (4,800 samples); the loop is one echo time; joins and release step ≤ 4× slope | < 1e-5 |
| ping_pong | the bounce at 4,800 and 9,600 samples, equal at 5 V, nothing else at Feedback 0; the second pair 2.5 V; with it off the left stays left (right exactly 0) | ±0.03 |
| no_clicks | 16 events on a 50 Hz sine (Time by knob, CV, Ratio, Style, Freeze, Ping Pong, clock patched and unpatched, Diffuse): no step over 6× the sine's slope, in each style | 0.0528 / 0.0417 / 0.0309 V (allowed 0.129 / 0.099 / 0.185) |
| style_change_ramps | a 9 V sine and three style presses: step ≤ 3× slope | |
| crossfades_add_no_gain | Digital at 80 % under a Time LFO; 90 % on a clock that alternates by one sample; at Feedback 0 no echo above 1.02 V | < 1 mV; < 10 mV and peak < 5 V |
| drift_does_not_jerk | a Time or Ratio change at Drift 1: offset slope ≤ 2.3 × `DRIFT_DEPTH`; step ≤ 6× slope; 3 styles × 4 seeds | |
| refreeze_does_not_click | a gate gap of 48, 480 or 2,400 samples; freeze during a crossfade; the freeze itself ≤ 1.2× slope | |
| frozen_is_what_was_heard | frozen level and brightness within 0.5 dB of the live echo in 4 settings; 5 V DC leaves < 0.05 V | |
| clock_restart | a 0.5 s clock paused 7.3 s keeps 0.5 s; slowing 0.25 → 0.75 s is followed; a replugged clock is not believed on one edge | 0.01 s |
| stereo | L-only = R-only (bit-identical); sides do not cancel (mono ≥ 0.6 of a side); R unpatched = the average | 1e-5 |
| bypass | all eight patch combinations and two inputs match the working module at Mix 0 within the knee; L alone carries the average; NaN passes as 0 | 1e-5 |
| mix_law | Mix 0 is the input exactly (≤ 8 V); half Mix is 0.7071 and 0.7071; 10 V on Mix CV is fully wet | 1e-3 |
| limit | each style at 110 % with ±10 V noise for 60 s: finite, over 8 V and at most 10 V | |

### 12.5 test_hostile (ASan, UBSan)

`extreme_rates`, `rate_change_recovers` (with the latch released before the check, section 7.7), `hostile_inputs`, `gates_at_audio_rate`, `gate_cost_is_bounded`, `time_cv_as_noise`, `minutes_at_110`, `no_allocation_in_process`, `seeded_fuzz` and `denormals`; described in section 10.2.

### 12.6 Not covered by the automated suite

- **Anything in Rack itself:** the panel, the tooltips and typed values (`Wake.cpp`'s quantity classes), the lights, the module browser, save and load through Rack, the Style button's press and latch, Randomize and Initialize as Rack runs them, bypass in Rack, sample-rate change events, polyphony as Rack delivers it, Rack's CPU meter.
- The `"style"` JSON key and its edge cases: no test.
- The clock slowing by more than 4×; the default Time knob in clock mode; jitter.
- A rate change with a latched Freeze or a high gate.
- The Freeze release's notch and the 40 ms fade's depth; the gap between `freeze` and its light.
- Tone's jump (#58); a Diffuse move through 0 and its pitch; the style's effect on a clock-driven Time.
- Pitch of the glide at other sample rates; aliasing [issue #67]; Windows and Linux builds; CPU on x86-64 or Windows; the libm check (Linux only).
- Listening: whether the three styles are far enough apart, the bend's speed, Feedback's top, Diffuse's amount, Drift's depth, the Ratio and clock steps, the defaults (the listening guide's verdict table).

---

## 13. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, a fresh Wake (Time 300 ms, Feedback 40 %, Tone noon, Ratio 1:1, Mix 35 %, Diffuse 0, Drift 15 %, Tape), nothing frozen, Ping Pong off.

**Method notes.**
- Use a short struck sound or a burst, not a steady tone, for echo timing. Set Mix to full wet and Drift 0 for timing and level measurements. Ignore the bend when timing in Tape or Analog.
- The loop colours each repeat, so Tape and Analog lose level per pass (section 6.3); compare Feedback behaviour in Digital first.
- Time CV is read every 16 samples; do not use it for audio rate modulation expectations.

### 13.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Time range | sweep Time with a ping going in, Digital, Feedback 0 | echo from 10 ms at the left to 10 s at the right; tooltip 10 ms, 56 ms at 0.25, 300 ms at default, 316 ms at noon, 1.78 s at 0.75, 10.00 s at the right; ms below 1 s, seconds above |
| Time typed | "250", "250 ms", "1.5 s", "abc", "0.25" | 250 ms, 250 ms, 1.5 s, ignored, 10 ms (a quarter of a millisecond clamps) |
| Time CV | attenuverter 0, then +1, then −1; ±5 V and ±1 V | nothing at 0; +1 V doubles Time at att +1 (316 → 632 ms from noon); −1 inverts; ±5 V covers the range from noon; held 10 ms to 10 s |
| Ratio | the seven steps with a click in | L 1:2 gives a left echo at half and a right at full, to R 1:2 the reverse; the names; typed "R 2:3", "1:1", "5" (R 3:4), "9" ignored |
| Feedback | 0, 40 %, 90 %, 100 %, 110 % in Digital at Tone noon; Feedback CV 5 V on knob 0 | 0 one echo; 50 % falls 6.0 dB per echo (±0.5); 100 % nearly constant (about −2.9 dB per minute at 220 Hz, T 100 ms); 110 % grows to the 10 V ceiling and stays finite; CV 5 V is a gain of 0.55 |
| Tone | noon, full left, full right at Feedback 70 % | noon flat; left darker each repeat to about 509 Hz (−3 dB); right thinner to about 1 kHz; 0 at noon in the tooltip (−100 % .. +100 %) |
| **Tone jump** | a loud low-pitched repeating echo; jump Tone from full left to noon in one move (type 50 %, preset or Randomize) | **known [issue #58]**: a click T later of about 0.6 V on a 3 V 100 Hz sine; also to full right; none from right to noon; none when swept by hand over 100 ms. Record how audible it is on #58 |
| Diffuse | 0, then up, Feedback 60 % | 0: repeats exact; up: each repeat blurs more (90 % of the energy in 8.9 / 12.4 / 18.2 ms at 10 / 50 / 100 %); level within 2 dB; centred on T |
| **Diffuse through 0** | Tape, a held tone into a 300 ms echo; turn Diffuse from 0 to 1 % | the tap's delay shortens by 8.9 ms (the smear's own delay replaces it) and the echo bends up by about a semitone for about a third of a second; Digital crossfades; no click. Between two non-zero values nothing moves |
| Drift | held tone into 0.3 s and 3 s echoes; Drift 0, 15 %, 100 % | 0: static; 15 %: a few cents; 100 %: ±15 cents at 0.3 s, ±26 at 3 s, ±3 at 50 ms; the two sides independent |
| Mix | 0, 35 %, 100 % | 0: the input exactly (within 8 V); 35 %: 0.853 dry, 0.522 wet; 100 %: no dry; equal power across |
| Mix CV, Feedback CV | 10 V on each | the whole knob travel, added |
| Ping Pong | on, one cable in, Feedback 0, then 50 %, Ratio off noon | one echo left at T, one equal right at 2T; at 50 % the second pair is half; long-short rhythm off noon; off: a stereo input stays stereo |
| Inputs | IN L only, IN R only, both, 4-channel poly into IN L; a poly CV | either alone feeds both sides identically; poly audio summed; poly CV uses channel 1 only |
| Outputs | OUT L only patched; then both | L alone is the average of the sides; with R patched, L is the left side; Mix 0 is the input |
| Limit | ±20 V at the input; Feedback 110 % | output soft-limited from 8 V, never over 10 V; no runaway |

### 13.2 Clock

| Procedure | Pass criteria |
|---|---|
| steady 0.5 s clock, Time fully left, then each knob step | 0.125, 0.1667, 0.25, 0.3333, 0.375, 0.5, 0.75, 1.0, 1.5, 2.0 s; tooltip "1/4 clock" to "x4 clock" |
| **default knob with a clock patched** | **expected per the manual: ×1; the code gives 3/4 (0.375 s)** [section 5.4]; ×1 begins at knob 0.5000. Record on the suggested issue |
| two edges to lock | before the second edge Time is the knob's ms; at the second edge it jumps to the clock's time (a normal Time change in the style) |
| unplug, then replug | unplugged: milliseconds at once; replugged: ms until two edges |
| stop the clock | the last tempo holds |
| pause 5 periods then restart | the tempo is unchanged (the gap is ignored); pause 3.5 periods: Time swings to 1.75 s for one tick, then back |
| **slow the clock to more than 4× its period (0.25 s → 1.2 s)** | **expected per the manual: followed one tick late; the code: stays at 0.25 s** [section 5.3]. Unplug and replug recovers it |
| Time CV while clocked | steps along the same grid; never between steps; +2 V at att 1 from ×1 is ×1.5; −5 V is 1/4 |
| a 4 s clock at ×4; a 2 ms clock; a 40 s clock | 8 s; 16 ms (doubled); 10 s |
| a clock with 1 ms jitter in Tape | a few tens of cents of wobble on each tick (24 cents, section 5.7); in Digital a crossfade each tick |
| typed multiples while locked | "x2", "1/2", "x1.5 clock", "X3" set the step (knob = step/9) |
| thresholds | a gate of 0.9 V gives no lock, 1 V does; a pulse train whose low level is 0.2 V does not lock |
| frozen clock | freeze, change the clock tempo, release: the new tempo is used after the release; Time held while frozen |
| rate change while locked | locked state forgotten; two edges to recover |

### 13.3 Style

| Procedure | Pass criteria |
|---|---|
| press STYLE through the three and round | lights: Tape → Analog → Digital → Tape; one press steps once; no click; the change is a 30 ms ramp |
| new module | Tape lit; Initialize returns to Tape |
| shift-click STYLE | latches down, steps once; the next click releases |
| press STYLE while bypassed | nothing: the press is lost |
| save with Analog, reopen; hand-edit `"style"` to 7, −1, 1.0, "x", or delete it | Analog restored; the edited values load as Tape |
| Time 300 → 600 ms, a held 440 Hz tone, each style | Tape: bends down toward 110 Hz and settles by about 2.6 s; Analog: a faster bend, settled by about 1.5 s; Digital: 440 Hz throughout (a 40 ms crossfade) |
| Time 600 → 300 ms | bends up to about +1,900 cents, settled by about 2.5 s (Tape), 1.3 s (Analog) |
| a long jump (1 → 5 s) | Tape: beyond 10 cents for 7.2 s; Analog 6.3 s |
| Feedback near 100 %, a repeating loud note, each style | Tape: soft, a little less top and bottom each pass; Analog: darker (about 5 kHz at short times down to 1.5 kHz at 1 s), grittier, third harmonic −24.5 dB at 5 V; Digital: clean, only Tone changes it |
| Feedback 110 %, T 100 ms, 220 Hz burst, Tone noon / left | Digital: a held wash at 10 V; Tape: about 3.7 V, with a dip and recovery when Tone is left; Analog: about 1.6 V, and with Tone left it dies |
| Analog with Ratio off noon | the shorter side is brighter (its corner follows its own echo time) |
| press STYLE while frozen | no effect on the held sound; lights change; the style applies after the release |

### 13.4 Freeze

| Procedure | Pass criteria |
|---|---|
| button, echo running | each side loops its last echo-time stretch (left 19,200 samples and right 9,600 at T 400 ms, R 1:2); the cap lights; no click at the start; the input still passes through Mix |
| gate into FREEZE | the same as the button; the cap lights; a gate of 0.9 V does not freeze, 1 V does; a gate that falls to 0.11 V stays frozen, to 0.10 V releases |
| both button and gate | frozen while either holds; the cap lights for either |
| hold for minutes at Drift 0 | an exactly repeating loop; level, brightness and spectrum unchanged |
| hold at the default Drift (15 %) | the loop wanders by a few cents and is not an exact repeat |
| move Time, Ratio, Ping Pong, the clock while frozen | nothing changes until the release; then the new values apply |
| move Feedback, Tone, Diffuse, STYLE while frozen | no effect on the held sound |
| release | a 40 ms crossfade back to live; listen for **one notch about 20 ms wide T after the release** (about −18.5 dB on a sine); no click |
| freeze during a Digital Time change | the freeze waits up to 40 ms (38 ms measured) for the fade; no click; the cap lights when it takes effect |
| freeze at audio-rate gate | no crash or click; CPU bounded |
| **change Rack's sample rate with Freeze latched** | **expected per the manual: unfreezes; the code: frozen again at once, the loop silent.** Release the button to recover |
| change the sample rate with Freeze off | the delay empties; times the same in seconds |
| Randomize with Freeze on and off | Freeze unchanged; Style unchanged; **Ping Pong may change**; the Time attenuverter, Tone, Ratio and the rest are re-rolled |
| Initialize while frozen | unfrozen, delay empty, Tape |
| save frozen, reopen | Freeze on; the loop is silent until the delay refills after a release [unverified in Rack] |
| bypass while frozen | the Freeze light goes dark; the freeze state is held; un-bypass resumes frozen |
| 5 V DC in, frozen | under 0.05 V in the frozen sound |

### 13.5 Saved patches and Initialize

| Procedure | Pass criteria |
|---|---|
| set every knob, Style, Freeze and Ping Pong; save; reopen | all restored; the delay starts empty; `"data"` is `{"style": n}` only |
| duplicate the module | settings copied; instances independent (no shared state) |
| Initialize | defaults (Time 300 ms, Feedback 40 %, Mix 35 %, Drift 15 %), Tape, unfrozen, Ping Pong off, delay empty |
| there are no factory presets | the Preset menu offers none for Wake |

### 13.6 Sample rates (44.1 kHz to 768 kHz)

| Procedure | Pass criteria |
|---|---|
| run at 44.1, 48, 88.2, 96, 176.4, 192, 352.8, 384, 705.6, 768 kHz | no crash, no noise; echo time within the table of section 2.4; longest echo 10 s to 192 kHz, 5.94 s at 352.8, 5.46 s at 384, 2.97 s at 705.6, 2.73 s at 768; the tooltip clamps to it |
| memory | about 4.5 MiB at 48 kHz, 17.8 MiB at 192 kHz, 34 MiB at 352.8 kHz and above (section 4.3) |
| CPU against rate | rises in proportion (section 9): 0.10 % at 48 kHz, 0.80 % at 384 kHz, 1.56 % at 768 kHz (defaults) |
| change the rate while a tail rings | the tail is cleared (expected); no burst or click beyond the cut |
| echo timing at 192 kHz and above | within 0.1 % of Time [unverified: the test runs 44.1, 48, 96 kHz] |

### 13.7 Polyphony and CV

| Procedure | Pass criteria |
|---|---|
| poly cable (2 to 16 channels) into IN L or IN R | channels summed; output within 10 V |
| poly cable into each CV and the clock and gate | channel 1 only |
| Time CV at audio rate (a 440 Hz sine, ±5 V, attenuverter 1) | read every 16 samples: a pitch-warble effect, not clean FM; no crash; output ≤ 10 V [test: `time_cv_as_noise`] |
| Feedback CV, Mix CV at audio rate | smoothed over 5 ms; no clicks |
| NaN-producing source on any input | output finite; the loop works again after it |

### 13.8 Bypass

| Procedure | Pass criteria |
|---|---|
| bypass with IN L only | both outputs carry L; with OUT R unpatched, OUT L carries the average |
| stereo in, OUT L only | L carries the average of the two inputs |
| poly input | summed to mono (not passed polyphonic) |
| the style light | stays lit; the Freeze light goes dark |
| un-bypass | the delay is as it was (not cleared) [unverified] |

### 13.9 CPU

| Procedure | Pass criteria |
|---|---|
| one idle instance, 48 kHz | Mac bench 0.14 to 0.15 % of a core; Wake never sleeps, so idle equals outputs |
| a busy instance | Mac bench 0.16 to 0.17 % (budget 0.4); manual: about 0.15 % |
| 16 instances | per instance 0.18 % (idle) to 0.21 % (busy) on the Mac (budget 0.4) |
| Windows | no automated budget; record the Rack meter per instance for idle and busy at 48 kHz |

### 13.10 Listening (from the guide, with the expected facts)

| Item | Expected |
|---|---|
| aliasing on a loud input at Feedback near 100 % in Tape and Analog | unmeasured [issue #67]; say what you hear |
| the three styles far enough apart | first echo's top at 10 kHz: Tape −5.2 dB, Analog −8.5 to −16 dB (by Time), Digital 0 dB; at 50 Hz: −2.2, −5.6, −0.05 dB |
| the pitch bend's speed | Tape time constant 0.15 s, Analog 0.08 s |
| Drift's depth | about ±2 cents at 15 % on a 0.3 s echo (15 % of the ±15.4 cents measured at full) |
| defaults | 300 ms, 40 %, Tone flat, 1:1, 35 %, Diffuse 0, Drift 15 %, Tape |

---

## 14. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a −1..+1 scale on a CV input; negative inverts. Wake's is on Time only. |
| Bound (the loop's) | the soft limit inside the loop: linear to 12 V, never past 16 V |
| Clock follower | the Schmitt trigger and period counter that sets Time from CLOCK |
| Colour | one pass's change to a repeat: Tone, the style's filters and drive, the DC block and the bound |
| Control tick | the 16-sample interval at which Wake reads its knobs and CVs |
| Crossfade | Digital's change of Time, and the release from Freeze: a 40 ms linear fade between two reads that never adds level |
| Diffuse | four all-pass filters that smear each pass; out of the path at exactly 0 |
| Drift | a slow random tape-speed wander on each side |
| Echo time (T) | the left side's delay, set by Time; the right side's is set by Ratio |
| Glide | Tape's and Analog's change of Time: the delay slides, so pitch bends and settles |
| Hermite read | 4-point cubic interpolation for reading a delay line between samples |
| Join | the 10 ms crossfade at a frozen loop's wrap |
| Loop (frozen) | the last echo-time stretch of a side's line, played round and round |
| Notch (release) | the faded-out end of the old line, heard once T after a release |
| Period | the clock's time between the last two rising edges, in samples |
| Ping Pong | the bounce: left in, left to right at full level, right back to left through Feedback |
| Pause rule | an edge more than four periods after the last restarts the count and does not set the period |
| Ratio | the left and right times against each other, seven steps |
| Restart (clock) | an edge taken as the start of a new count, not as a period |
| Schmitt trigger | an edge detector with two thresholds: 1 V up and 0.1 V down for CLOCK and GATE |
| Step (clock) | one of the ten multiples of the clock period |
| Style | Tape, Analog or Digital: the loop's colour and what Time does |
| Tap | a moving read point on a delay line (`DelayTap`) |
| Tone | a one-pole filter in the loop: low-pass left of noon, high-pass right of it, out of the path within 1 % of noon |
| Wake | this module; also a ship's wake: what a passing sound leaves behind |
| Wander | Hadal's random-heading generator, run 25× faster, that Drift uses |

---

### Note on the test runs

I built Wake's five test programs from the 2.0.39 tree (`git archive 2e6e960`, extracted outside the repository) with `make -f tests.mk build/test/wake/test_tap build/test/wake/test_timing build/test/wake/test_loop build/test/wake/test_engine build/test/wake/test_hostile` and ran each: all five printed "all checks passed" (exit 0). I also ran `make -f tests.mk bench-modules` (15 module declarations complete), `engine-globals` (passed), the CPU bench cases for Wake (`build/bench/bench wake idle|outputs|busy 1|16 5`, three times), and `tools/test_gen_panels.py` (115 tests OK). `make libm-calls` is skipped on macOS (needs GNU ld) and was not run. The repository was not edited, built in or committed to.

### Note on the measurement programs

All in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/wake/meas/`, compiled outside the repository by `build.sh` against the extracted 2.0.39 sources with the plugin's flags (`-std=c++17 -O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`); outputs in `../out/`:

- `m_clock.cpp`: clock sync (steps and knob edges, thresholds, lock, period resolution, slowing, pauses, unplug and rate change, a clock held across a freeze, fast and slow clocks) and the memory and longest echo per rate.
- `m_jitter.cpp`: a jittery clock in each style.
- `m_style.cpp`: pitch-bend timelines, one-pass response per style and Time, drive and third harmonic.
- `m_stylestep.cpp`: sample steps after a style press.
- `m_freeze.cpp`: freeze wait during a crossfade, gate thresholds, a rate change with a latch, frozen loop lengths and drift, and the Tone jumps of issue #58.
- `m_seam.cpp`, `m_seam2.cpp`: the release notch and the Feedback 110 % level over time.
- `m_misc.cpp`: Time laws and CV, Feedback 110 % and 100 %, the frozen loop's pitch under Drift, CPU against rate.
- `m_diffuse.cpp`: the shift when Diffuse crosses 0.
- `m_mem.cpp`: buffers and memory per rate.
- `m_random.cpp`: which params Rack's real `Module::onRandomize` reaches (against libRack 2.6.6 in the Rack SDK; each `ParamQuantity` records the call instead of writing a value).

They drive `rka::WakeEngine` as `Wake.cpp` does: `WakeFrame` with the module's defaults, one cable on IN L with IN R unpatched unless stated, OUT R patched, Mix 1 and Drift 0 for timing and level work. They do not exercise Rack's ports, threading, GUI, the tooltips, the Style button's trigger or the JSON. The pitch figures read the tap's delay (`tap(0).delay()`), a float, whose spacing at 24,000 samples is 0.002 samples: pitch excursions of 3.4 cents or less are at the probe's floor. The stated frozen-pitch figures count zero crossings of a sine and are good to about 1 cent. Anything marked [meas] should match Rack at 48 kHz; Rack-side effects (block size, the order of events on a patch load or a rate change) are outside it.

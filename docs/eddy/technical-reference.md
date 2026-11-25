# Eddy 2.0.39: Technical Reference for QA

Module: **Eddy**, plugin `rka` (slug `rka`, module slug `Eddy`), VCV Rack 2 (minRackVersion 2.5.0). A 16 HP stereo tape loop that also plays WAV files.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It is a Zstandard-compressed tar. Its `plugin.json` is byte-identical to the commit's (version 2.0.39, minRackVersion 2.5.0). It contains `res/eddy/Eddy.svg`, `res/eddy/Eddy-dark.svg` and `tools/fonts/D-DINCondensed.ttf` (the window's lettering). It has **no Eddy factory presets** (`presets/` holds only Fog's four).
Document date: 2026-11-24. This is the first technical reference for Eddy.

## Build history

Eddy was built on the branch `eddy` (pull request #68) and shipped in four test builds. From the repository's CHANGELOG.md and `git log` for `src/eddy/`, `src/Tape.hpp`, `src/TapeGesture.hpp`, `src/BandLimitedRead.hpp`, `src/Resample.hpp`, `src/Overview.hpp`, `src/WavFile.*` and `docs/eddy/` (55 commits in all: 54 in the four builds below and one later shared-file commit):

| Build | Date | What changed in Eddy |
|---|---|---|
| 2.0.25 | 2026-11-21 | First listening build: the tape loop, WAV load and save, the waveform window. (21 commits: tape, write head, wear, laws, engine, overview, jobs, module, panel, manual.) |
| 2.0.26 | 2026-11-21 | The window fits the tape while recording (a span of 4 s that doubles). Mix has input and tape both at full at noon, and starts there. |
| 2.0.27 | 2026-11-22 | The tape read is band-limited at every speed (`BandLimitedRead.hpp`; the Hermite read is gone from Eddy's path), with aliasing tests (`test_alias`, `test_band_read`). Dragging in the window sets Start and Length. A quick double tap on REC leaves the tape alone (the button's minimum rose from 50 ms to 0.3 s). Wear's levels were accepted by ear on this build. |
| 2.0.28 | 2026-11-22 | Final review fixes (22 commits). Wear and Dub keep the bass (the 5 Hz DC blocker moved off the tape going round, to what a dub adds; it acts on the tape only while the write bound clips). Restore no longer swells 3 dB (equal-gain fade). A tape loading when Rack's rate changes is loaded again at the new rate, not converted twice (`WorkerBook.hpp`). A stored tape survives rate changes while loading. Saves cannot go quiet. Save file asks before replacing the name it extends with `.wav`. File samples are held to ±100 V. |
| 2.0.29 to 2.0.39 | 2026-11-22 to 2026-11-24 | **No change to Eddy's behaviour.** `git diff 13aabcf 2e6e960` over Eddy's files shows only shared files: `plugin.json`, `src/Overview.hpp` (a `serviceFrom()` was added for Spindrift; `service()` calls it with the same arithmetic), `src/widgets.hpp` (new windows for other modules; `TapeWindow` unchanged) and `tests.mk`. All 15 Eddy test programs pass on the 2.0.39 tree (section 14). |

The user's listening guide, `eddy-2.0.28-listening.html`, is for build 2.0.28. Because Eddy's own files are unchanged since then, it applies to 2.0.39. Where it is wrong or out of date is in section 13.3.

---

## 0. How to read this document

- Every number here comes from one of the sources below, and each is marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/eddy/*.hpp`, `src/eddy/Eddy.cpp`, `src/Tape.hpp`, `src/TapeGesture.hpp`, `src/BandLimitedRead.hpp`, `src/Resample.hpp`, `src/Overview.hpp`, `src/WavFile.hpp`, `src/widgets.hpp`), cited as file:line where useful.
  - **[test]**: printed or asserted by the module's automated tests. I rebuilt all 15 Eddy test programs from the 2.0.39 tree with the flags `tests.mk` gives them (the engine and DSP tests with `-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`; the file, job, book and hostile tests with ASan and UBSan at `-O1`; `test_threads` with ThreadSanitizer) and ran them on an Apple M5 Pro Mac (arm64, Apple clang). All 15 passed. Most tests print only a pass line; `test_alias --table`, `test_band_read --table`, `test_transport`, `test_tape` and `test_select` print measurements, which are quoted.
  - **[meas]**: a small measurement program I wrote and ran against the engine (`rka::EddyEngine` driven through the tests' own `Rig`), or against the file jobs (`TapeJobs.hpp`), at 48 kHz unless stated. The programs are listed in the note at the end.
- **[issue #N]** points to an open GitHub issue in `ryanarneson/RkaModules` (section 13.1).
- **[unverified]**: anything I could not check against the code or by measurement. This is mostly Rack host behaviour (the menu, file dialogs, drag and drop, undo, autosave, patch packing, Windows memory and file semantics). I did not run Rack.
- Voltages are Rack volts. A file's full scale (±1.0) is ±5 V on the tape. "Noon" means knob 0.5 (for Speed, the knob's middle is speed 0).
- "Frame" means one stereo sample of tape. "Turn" means one trip of the head round the window.
- The brief's cross-check: the user's listening guide is not a source. Where it disagrees with the code, the code wins (section 13.3).

---

## 1. What Eddy is

Eddy is a 16 HP stereo tape loop and sampler: one stereo tape of up to 5 minutes, filled by recording or by loading a WAV, played at any speed from twice forwards, through stopped, to twice backwards. The design intent (spec `docs/specs/2026-11-21-eddy-design.md`, manual `docs/eddy/README.md`) is one tape, not two modes: recording and loading put sound on the same tape, and one engine plays, dubs and wears it. The picture is an eddy, water that keeps coming round.

Design goals that QA can check:

- Record a stereo loop of up to 5 minutes and hear it come round with no click at the join. At Speed +1, Wear 0, Smear 0 and Dub off, a loop repeats exactly (a recording of N frames loops every N frames).
- Load a WAV (menu, or drop on the panel) at the right pitch whatever its rate, and save the tape as a 32-bit float stereo WAV.
- Speed from -2 to +2 by knob, CV and V/OCT (to ±4 with V/OCT), pitch following, with a stopped tape giving silence and no DC.
- A window (Start and Length, 10 ms to the whole tape) that can be moved while it plays, with the join always a fade (Smear lengthens it up to half the window).
- Layering by Dub, with one Dub Level knob for both the new layer's level and how much of the old is erased.
- Wear: each pass costs the tape a little (level, highs, saturation, hiss, dropouts); at 0 the tape is not rewritten at all.
- The original is kept beside the tape; Restore puts it back.
- Played as a sample: a PLAY trigger starts from Start; Loop off plays once.
- The tape (and the original) is kept with the patch.
- A window that shows the tape's waveform, the part in play, the head and the tape's length, and that sets Start and Length by dragging.
- Anti-goals: no click at the join, at Rec, Dub, Play, Stop, a retrigger, a move of Start or Length, or when a file arrives; no click or step written onto the tape; no level that builds without limit under repeated dubs; no loop at Wear 0 that fades or drifts; no audio dropout or hung window when a file loads or the patch saves; no patch that reopens with the tape gone.

Non-goals (do not file these as defects): grains, pitch apart from speed, polyphony (one tape, one head; polyphonic audio cables are summed), a clock input, long files (over 5 minutes), several tapes, undo of a single dub, wow and flutter (patch a Sway into Speed CV), random jumps (patch a Squall into Start CV), zooming or scrolling the window, a Wear CV, a Send/Return. Trimming the tape to the window is a future enhancement [issue #66].

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. Test against the code's behaviour; the documents' versions are given for context. I compared `docs/specs/2026-11-21-eddy-design.md`, `docs/eddy/README.md` and the code line by line. The spec carries "as built" notes that already correct its own earlier text; rows here are only where a document and the code still differ, or where an "as built" text and the manual disagree with each other.

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| Start CV range | spec "±5 V moves Start across the whole tape"; manual "the Start CV adds the whole tape for 5 V, and knob plus CV is held to the tape" | start = clamp(knob + CV / 5 V, 0, 1) of the tape. The manual's wording is exact; the spec's "±5 V" holds only from noon: at knob 0, -5 V does nothing; at knob 1, +5 V does nothing [meas, `Laws.hpp:108`] |
| V/OCT landing time | spec "within a few milliseconds"; as built "within 2 ms"; manual "landing within 2 ms" | a 2 ms linear volt ramp that starts at the next control tick (every 16 samples, 0.33 ms at 48 kHz): speed reaches 1.5 after 1.46 ms and 1.99 after 2.27 ms for a 0 to +1 V step [meas] |
| Aliasing bound of the fast read | manual "from ×1.02 to ×4 anything that would fold back is at least 57 dB down"; header comments say 57 dB | measured worst -57.1 dB (speed 3, content landing on Nyquist) [test: `test_band_read --table`], but the tests assert only **-50 dB** (`FAST_ALIAS_DB`, `test_alias.cpp:97`, and `test_band_read.cpp:35`). A drift from -57 to -51 dB would pass the suite |
| Dub away from ×1, "faint images" | manual "faster, faint images are left" | faster than ×1 the input is interpolated linearly between samples. A 5 kHz input dubbed at speed 2 leaves an image at -30.1 / -31.5 dB re 5 V (44.1 / 48 kHz); a 15 kHz input leaves -11.7 / -13.1 dB. Slower (0.5), a 15 kHz input folds at -12.7 / -10.2 dB [test: `test_alias --table`]. "Faint" is true for content below about 5 kHz only. The spec and manual do say the dub is "not band-limited" and the test pins these numbers |
| PLAY trigger while recording | manual "It is ignored while recording" | the trigger is not ignored: it sets Play on (the PLAY light comes on during the recording), and only the jump to Start is skipped. When the recording ends Play comes on anyway, so the sound is the same [meas: PLAY trigger during a recording gives playing = 1, recording = 1] |
| After a rate change | manual "after a rate change the module is silent until the converted tape is ready"; spec "silence, for a rate change" | the **tape** is silent; the input still passes at the Mix setting (noon, the default, passes it at full: 1.000 V in, 1.000 V out; at Mix 1 the output is 0) [meas] |
| Dub at Dub Level 0 | spec "At zero nothing is written"; "While dubbing, the input is heard at the outputs whatever Mix says" | both are true together: with Dub on and Dub Level 0, nothing reaches the tape but the input is still heard at full at Mix 1 (1.000 V out from a silent tape) [meas; `EddyEngine.hpp:383`: the heard-input rule tests `dubOn_`, not the write] |
| Loop length of a loaded file | spec "Length ... a turn is exactly Length long"; manual says it also, with the exception "only where nothing follows the window (the very end of a loaded file)" | consistent, but the consequence is easy to miss: at the defaults (Start 0, Length full, Smear 0) a loaded **file** loops 10 ms shorter than the file (a 1 s file turns every 990 ms, 47,520 samples). A **recording** of N frames loops every N frames exactly (96,000) because it has a recorded tail [meas] |
| DC corners | manual lists corners where DC is left on the tape (heavy lopsided dubbing in time with a short loop; reverse) | the manual **omits case 1 of issue #69**: Wear switched hard between 0 and about 30 % at 2 Hz or faster with lopsided input in time with the loop rebuilds 0.36 to 1.1 V of DC [issue #69, to-do list]. The 1 s-loop figure "under 10 mV" is still being checked there |
| Spec design text | spec "Rec: ... under 50 ms is thrown away" | superseded in the same spec by the as-built note, and by the manual: 0.3 s if the REC button started or ended it, 50 ms if the gate did both [test: `double_tap_*`; meas below] |
| Tape length at high rates | spec first says "5 minutes at any sample rate"; as built "at most 2^24 frames" | the as-built text is right: 5:00 at 44.1 and 48 kHz, 3:10 at 88.2, 2:54 at 96, 1:35 at 176.4, 1:27 at 192, 0:47 at 352.8, 0:43 at 384, 0:23 at 705.6, 0:21 at 768 kHz (section 5) |
| "Initialize" | spec: empties the tape and returns the buttons to defaults | also forgets the file and turns "Keep the tape with the patch" back on (the manual says so) [code, `Eddy.cpp:347`] |
| Randomize | manual "Randomize does not touch Rec, Dub, Play or Loop" | also does not touch Restore (`configButton` turns randomize off [SDK header]). It does randomize Speed, Speed CV, Start, Length, Wear, Smear, Dub Level and Mix, so a random Speed can be backwards |
| Autosave | spec and manual: Rack's autosave does not ask a module to save its storage | [unverified in Rack]; the code writes the stored files only in `onSave` (`Eddy.cpp:365`) |

---

## 2. Panel, controls and ports

### 2.1 Layout

16 HP. Positions in mm from `EddyLayout.hpp` (generated by `tools/gen_panels.py`) [code]:

| Control | Centre (x, y) mm |
|---|---|
| Window (the glass) | 71.28 × 14.0 at (5.0, 19.5), a ruler in tenths under it |
| Speed (hero, face radius 16 mm, lettered -2, -1, 0, +1, +2 at -135°, -67.5°, 0°, +67.5°, +135°) | 40.64, 54.0 |
| Speed attenuverter | 40.64, 78.0 |
| REC, DUB (lit caps) | 7.8, 47.5 and 7.8, 60.5 |
| PLAY, LOOP (lit caps) | 73.48, 47.5 and 73.48, 60.5 |
| START, LENGTH, WEAR, SMEAR | 7.8, 24.22, 57.06, 73.48 at y 76.0 |
| DUB LEVEL, MIX (small, attenuverter size) | 57.06, 90.0 and 73.48, 90.0 |
| RESTORE (cap button) | 57.06, 102.0 |
| Start CV, V/OCT, Speed CV jacks | 7.8, 24.22, 40.64 at y 90.0 |
| IN L, IN R | 7.8, 102.0 and 7.8, 116.0 |
| REC gate, DUB gate | 24.22, 102.0 and 24.22, 116.0 |
| PLAY trigger, EOC | 40.64, 102.0 and 40.64, 116.0 |
| TRIG (Restore's jack) | 57.06, 116.0 |
| L, R outputs (on ivory) | 73.48, 102.0 and 73.48, 116.0 |

Lettering: title EDDY, subtitle TAPE LOOP & SAMPLER; START, LENGTH, WEAR, SMEAR, DUB LEVEL, MIX, REC, DUB, PLAY, LOOP, RESTORE, V/OCT, IN L, IN R, REC, DUB, PLAY, TRIG, EOC, L, R. **The Start CV and Speed CV jacks and the Speed attenuverter have no lettering**: they are joined to START and to the Speed dial by drawn lines. The default panel has Speed at +1 (the knob at +67.5°). Screws at top right and bottom left.

### 2.2 Parameter, input, output and light ids

Saved patches depend on these numbers; they must never change. **They are only partly pinned**: `Eddy.cpp:477` `static_assert`s three (SPEED = the frame's SPEED, MIX = the frame's MIX, REC = the frame's PARAMS_LEN) [code]. The rest rely on enum order.

| Id | Param | Range | Default | Tooltip (name; unit) |
|---|---|---|---|---|
| 0 | SPEED | -2..+2 (knob) | +1 | "Speed (backwards below 0)"; "×"; shown as the law's speed to 3 decimals |
| 1 | SPEED_ATT (Speed CV) | -1..+1 | 0 | "Speed CV"; % (-100..+100) |
| 2 | START | 0..1 | 0 | "Start"; % (0..100) |
| 3 | LENGTH | 0..1 | 1 | "Length"; shown as time (ms below 1 s, else s, two decimals) or % with no tape |
| 4 | WEAR | 0..1 | 0 | "Wear (each pass costs the tape a little)"; % |
| 5 | SMEAR | 0..1 | 0 | "Smear (the overlap at the join)"; % |
| 6 | DUB_LEVEL | 0..1 | 0.5 | "Dub level"; % |
| 7 | MIX | 0..1 | 0.5 | "Mix"; % with the description "0 %: the input alone; 50 %: the input and the tape both at full; 100 %: the tape alone" |
| 8 | REC | switch Off/On | Off | "Rec" |
| 9 | DUB | switch Off/On | Off | "Dub" |
| 10 | PLAY | switch Off/On | Off | "Play" |
| 11 | LOOP | switch Off/On | **On** | "Loop" |
| 12 | RESTORE | button (momentary) | 0 | "Restore (the original back on the tape)" |

REC, DUB, PLAY and LOOP have randomize turned off [code, `Eddy.cpp:285`]. Params 0 to 7 are the engine frame's params in the same order [code, `EddyEngine.hpp:167`].

| Id | Input (tooltip) | | Id | Output (tooltip) | | Id | Light |
|---|---|---|---|---|---|---|---|
| 0 | START_CV ("Start CV (5 V: the whole tape)") | | 0 | EOC ("End of each turn") | | 0 | REC ("Recording") |
| 1 | VOCT ("V/OCT (speed)") | | 1 | L ("Left") | | 1 | DUB ("Dubbing") |
| 2 | SPEED_CV ("Speed CV") | | 2 | R ("Right") | | 2 | PLAY ("Playing") |
| 3 | L ("Left") | | | | | 3 | LOOP ("Loop") |
| 4 | R ("Right (unpatched: the left)") | | | | | | |
| 5 | REC ("Rec gate") | | | | | | |
| 6 | DUB ("Dub gate") | | | | | | |
| 7 | PLAY ("Play trigger (from Start)") | | | | | | |
| 8 | RESTORE ("Restore trigger") | | | | | | |

Note the output order: **EOC is output 0**, L is 1, R is 2. The lights are on the four caps (REC, DUB, PLAY and LOOP): the caps are latching params whose lights are driven by the engine.

### 2.3 CV and input rules

- **Audio inputs sum all channels.** IN L and IN R use `getVoltageSum()` (every channel of a polyphonic cable, up to 16) [code, `Eddy.cpp:488`]. IN R unpatched takes IN L (`inRPatched` false) [meas: L 1.5 V, R 3 V unpatched gives 1.500, 1.500].
- **Every other input uses the first channel only**: Start CV, V/OCT, Speed CV, and also the REC, DUB, PLAY and TRIG gate jacks (`getVoltage()`). The manual says this for the three CVs; it does not say it for the gates and triggers.
- NaN or ±infinity reads as 0 (CVs: `detail::volts`, held to ±100 V; audio: `finite()`) [code, `Laws.hpp:22`, `EddyEngine.hpp:415`].
- **Gate and trigger thresholds**: a Schmitt trigger, high at 1 V and above, low at 0.1 V and below [code, `EddyEngine.hpp:702`]. REC and DUB act on both edges (rise starts, fall ends); PLAY and TRIG act on rising edges. A gate already high when a recording ends, a file loads or the tape is cleared does nothing until it falls and rises again.
- **Control rate**: all knobs and CVs are read every 16 samples (`CONTROL_SAMPLES`, 0.33 ms at 48 kHz) [code, `EddyTuning.hpp:76`]. A CV is therefore sampled at fs/16 (3 kHz at 48 kHz) before any smoothing.
- The input and tape levels use the Mix law (2.9). The audio input is not clamped; the output is held by the soft limit (4.3).

### 2.4 Speed

[code, `Laws.hpp:29-61`; meas: `meas1 laws`, `meas1 glide`]

```
knob       = -2 .. +2 (default +1)
speedKnob  = piecewise linear, flat at -1, 0, +1 (the "flats")
target     = speedKnob + 0.4 × CV volts × attenuverter          (the glide follows this)
speed      = clamp( glided target × 2^(V/OCT volts), -4, +4 )
```

- **The flats.** Within 0.04 knob units either side of 0, +1 and -1 (1 % of the 4-unit travel) the speed is exactly 0, +1 or -1. Between them the law is straight: speed = (|knob| - 0.04) / 0.92 for |knob| between 0.04 and 0.96, and 1 + (|knob| - 1.04) / 0.96 between 1.04 and 2 [meas]:

| Knob | Speed |
|---|---|
| 0, 0.02, 0.04 | 0 |
| 0.0401 | 0.00011 |
| 0.25 | 0.22826 |
| 0.5 | 0.50000 |
| 0.75 | 0.77174 |
| 0.96 to 1.04 | 1 (exact) |
| 1.5 | 1.47917 |
| 2 | 2 |
| negative | the mirror image |

- **Speed CV** adds 0.4 speed per volt at full attenuverter (+1), so ±5 V is ±2 at full; the attenuverter scales it from -1 to +1. The sum is not limited before the V/OCT stage (knob +1 and +5 V at +1 gives a target of +3; +10 V on a stopped knob gives +4) [meas].
- **V/OCT multiplies** by 2^V. Over -2 to +2 V the worst error is 0.0001 cent [meas]. The final speed is held to ±4: 1 × 2^3 and 1 × 2^5 both read 4.0; 1 × 2^-3 reads 0.125; speed 2 at +1 V reads 4.0 [meas]. The glided value is held to ±64 before the multiply [code].
- **Glide.** The knob-plus-CV target follows a one-pole lag of **60 ms** (`SPEED_GLIDE_S`): a 1 to 2 step reaches 63 % at 60 ms, 90 % at 139 ms, 95 % at 180 ms and 99 % at 273 ms [meas]. The V/OCT is not part of the lag; it has its own **2 ms linear ramp in volts** (`VOCT_SMOOTH_S`). The ramp's step is set at a control tick, so a 0 to +1 V step shows speed 1.5 after 1.46 ms and 1.99 after 2.27 ms [meas].
- **Stopped.** The output gain is |speed| / (1/64) up to 1 (`outputGain`): 0.5 at 1/128, 1 from 1/64. Speed 0 gives exactly 0, so a stopped tape gives silence with no DC [test: `speed`, peak 0 and mean 0, also after gliding to 0]. A 1 kHz sine read at speed 1/128 is -6.02 dB, at 1/64 and above within 0.004 dB [test: `test_alias --table`].
- **Writing is off** below |speed| 1/16 and fully on from 1/8, with a linear ramp between (`writeGain`: 0 at 1/16, 0.5 at 3/32, 1 at 1/8) [meas]. Dub and Wear both go through this gain (8.4).
- **Direction**: the sign of the speed. The direction is kept (`dir_`) while the speed is 0, so a window that starts at a stop starts from the right end for the last direction [code, `EddyEngine.hpp:324`].
- **Typed values.** The Speed tooltip accepts a typed speed (`setDisplayValue`): the knob is set to the middle of the flat stretch for exactly 0, ±1, and to the straight line elsewhere; |speed| over 2 is held to 2 [code, `Eddy.cpp:203`].
- **Tooltip**: "Speed (backwards below 0): 1.000 ×" at the default (three decimals).

### 2.5 Start and Length: the window

[code, `Laws.hpp:84-141`; meas: `meas1 laws`]

```
start      = clamp(Start + CV/5 V, 0, 1) × tape frames, held at most one frame inside the tape
length     = 10 ms × (tape / 10 ms)^Length               (Length 0 = 10 ms, 1 = the whole tape)
minFade m  = min(10 ms, length / 4)
overlap o  = max(m, Smear × length / 2)
```

- **Length law** (seconds) [meas]:

| Tape | k = 0 | 0.1 | 0.25 | 0.5 | 0.75 | 0.9 | 1 |
|---|---|---|---|---|---|---|---|
| 300 s | 0.0100 | 0.0280 | 0.1316 | 1.7321 | 22.795 | 107.01 | 300 |
| 60 s | 0.0100 | 0.0239 | 0.0880 | 0.7746 | 6.8173 | 25.138 | 60 |
| 10 s | 0.0100 | 0.0200 | 0.0562 | 0.3162 | 1.7783 | 5.0119 | 10 |
| 0.5 s | 0.0100 | 0.0148 | 0.0266 | 0.0707 | 0.1880 | 0.3381 | 0.5 |

  A tape of 10 ms or less is the whole tape at any Length. The tooltip shows the time (for example "1.73 s", "250 ms"); you can type seconds, or milliseconds with "ms" ("1.5", "250 ms"); with no tape it shows and takes a percentage. The tooltip's number uses `std::pow`; the engine uses `fastExp2`, so they can differ in the last digit [code, `Eddy.cpp:222`].
- **Start**: knob 0..1 of the tape, plus the CV (5 V is the whole tape, so +2.5 V is half). From knob 0.25 on a 10 s tape: +0 V is 2.5 s, +2.5 V is 7.5 s, +5 V is 10 s (held one frame inside), -5 V is 0 s [meas]. The CV is added to the knob and the sum is held to 0..1, so negative CV does nothing from knob 0.
- **A window that runs past the tape's end wraps** to its beginning (`wrap_`) [code, `EddyEngine.hpp:953`].
- **Moving the window** (knob, CV or the mouse) while the tape plays leaves the head alone unless it is now outside the window, in which case the head fades (10 ms, equal power) to the window's start (`controls`, `EddyEngine.hpp:1245`).
- **Smear** (0..1): the fade at the join. At 0 it is the minimum fade m: 10 ms, or a quarter of the window if shorter. Above 0 it is the larger of m and Smear × length / 2, so Smear 1 overlaps half the window. [meas, tape of 300 s]:

| Length | Smear 0 | 0.25 | 0.5 | 1 |
|---|---|---|---|---|
| 0.01 s (k = 0) | 2.50 ms (25 % of the window) | 2.50 | 2.50 | 5.00 ms (50 %) |
| 0.22 s (k = 0.3) | 10.00 ms | 27.55 | 55.09 | 110.18 ms |
| 1.73 s (k = 0.5) | 10.00 ms | 216.5 | 433.0 | 866.0 ms |
| 300 s (k = 1) | 10.00 ms | 37.5 s | 75 s | 150 s |

  The overlap is equal in power, so the level does not dip or rise. **Any overlap beyond the shortest fade comes out of the turn**: each turn starts that much sooner, and EOC follows the turns as heard. Measured turns for a 2 s recording of 96,000 frames: Smear 0 gives 96,000 samples; Smear 0.5 gives 72,480 (= 96,000 - 24,000 + the 480-frame tail); a loaded 1 s file gives 47,520 at Smear 0 (the 480 samples the missing tail would have added), 36,000 at Smear 0.5 and 24,000 at Smear 1 [meas].

### 2.6 Dub Level

[code, `Laws.hpp:143`; meas: `meas1 dub`]

```
add  = min(1, 2 d)        keep = 1 - d²        (d = Dub Level 0..1)
```

One pass of Dub over a 1 s loop of a 1 kHz sine, the old material alone and then a new layer alone on silence [meas, the measured amplitude as a share of the input or old amplitude]:

| Dub Level | Old kept: law (measured) | New layer added: law (measured) |
|---|---|---|
| 0 | 1.0000 (1.0000; nothing is written) | 0 (0) |
| 0.10 | 0.9900 (0.9900) | 0.2000 (0.2001) |
| 0.25 | 0.9375 (0.9375) | 0.5000 (0.5002) |
| **0.5 (default)** | 0.7500 (0.7500; -2.50 dB) | 1.0000 (1.0003) |
| 0.75 | 0.4375 (0.4375; -7.18 dB) | 1.0000 (1.0003) |
| 0.90 | 0.1900 (0.1900) | 1.0000 (1.0003) |
| 1 | 0 (0; it replaces) | 1.0000 (1.0003) |

The slight excess on the new layer (0.03 %) is inside the test's 0.2 dB tolerance; I did not trace its cause. The manual's table (25 % keeps 94 %, adds 50 %; 50 % keeps 75 %, adds 100 %; 75 % keeps 44 %; 100 % replaces) matches.

### 2.7 Wear

See section 8.2. Wear has no CV. The knob is 0..1; every law in section 8.2 is in w or w².

### 2.8 The buttons

- **REC, DUB, PLAY** are latching caps whose state the engine decides: a press that the engine cannot take (Rec while a tape is being converted to a new rate, Dub on an empty tape, Rec with no set at the rate) leaves the cap dark (the module sets the param back) [code, `Eddy.cpp:507`; meas: Rec pressed with no set at the engine's rate gives recording = 0]. **LOOP** is a latch only the user sets; its light follows the param.
- **RESTORE** is a momentary button (`BooleanTrigger` on > 0); **TRIG** is its jack.
- The caps and the jacks act on the same engine edges; either can end what the other started (REC, DUB).

### 2.9 Mix

[code, `EddyEngine.hpp:1238`; meas]

```
input gain = min(1, 2 (1 - m))        tape gain = min(1, 2 m)
```

0 is the input alone, 0.5 (the default) is the input and the tape both at full, 1 is the tape alone. At 0.25 the tape is at 0.5 and the input at 1; at 0.75 the input is at 0.5 and the tape at 1 [meas]. NaN reads as 0.5. Both gains ramp linearly over 10 ms (one fade, F) so a Mix step does not click [test: `mix_smooth`, at most (|in| + |tape|) / 480 per sample]. **At noon both are at full, so the output can be the sum of two ±5 V signals**; the soft limit (4.3) holds it under 10 V.

**While recording or dubbing the input is heard at full whatever Mix says** (`inputLoud`, `EddyEngine.hpp:383`: a recording in its start buffer or on the tape, or Dub on with a usable tape). The recording's tail (up to 10 ms after Rec ends) is not "loud": the input gain ramps back to the Mix setting over 10 ms.

### 2.10 Audio inputs and outputs

- **IN L, IN R**: the dry input has **zero delay**: 1 V in gives 1.000000 V out on the first sample, with no tape, at Mix noon [meas].
- **L, R**: pass `limit()`: exact up to 8 V, a tanh-shaped bound above it, never past 10 V; NaN reads 0; a magnitude under 1e-20 reads 0 so no subnormal leaves [code, `EddyEngine.hpp:419`].
- **EOC**: 10 V for 1 ms at every join, in either direction, and at the end of a one-shot. The width is `floor(fs × 0.001 + 0.5)` samples: 44 at 44.1 kHz (0.998 ms), 48 at 48 kHz, 96 at 96 kHz, 192 at 192 kHz (1.000 ms) [meas]. The EOC stays low while the module is bypassed.
- **Polyphony**: audio inputs are summed; Eddy never outputs polyphony.

---

## 3. The right-click menu and saved state

[code, `Eddy.cpp:1098` (menu), `437` (JSON)]

| Item | What it does |
|---|---|
| Load file | file dialog filtered to WAV (`osdialog`, opens in the last file's folder); loads on the worker thread |
| Save file | file dialog, default name "Eddy.wav"; a name without `.wav` has it added, and if that file exists Rack asks "&lt;name&gt; already exists. Replace it?" (the dialog asked about the name as typed); writes on the worker thread |
| Make this the original | the tape as it is now (worn and dubbed) becomes the original (a worker copy) |
| Clear | empties the tape and the original; Rec, Dub and Play off |
| Keep the tape with the patch | checkbox, default **on** (`keepTape`) |

A WAV dropped on the panel loads the **first** path ending in `.wav` (case-insensitive); other dropped files are ignored [code, `Eddy.cpp:1086`].

### 3.1 Saved JSON

| Key | Type | Default | Meaning | On load |
|---|---|---|---|---|
| `"file"` | string | `""` | the path of the last file loaded; written empty if a recording has stood since (the tape is no longer the file) | a string is taken as is; anything else, or a missing key, gives `""` |
| `"keepTape"` | boolean | true | "Keep the tape with the patch" | a JSON boolean is taken; **anything else, or a missing key, gives true** (Fog's equivalent reads false) |

A saved module's `"data"` is therefore `{"file": "...", "keepTape": true}`. The knobs, the REC, DUB, PLAY and LOOP latches (params 8 to 11) and RESTORE are saved by Rack as ordinary params (ids 0 to 12). The tape and the original are **not** in the JSON: they are three files in the module's folder inside the patch (section 9.4).

- **Rec is never loaded as on**: `dataFromJson` sets the REC param to 0. Play and Dub as saved take effect once the patch's tape is in (the worker applies them when it offers the tape; until then the buttons show the engine). If a patch had no `"data"` object, `dataFromJson` would not run, and a REC param of 1 would load as a press [code; the module always writes `data`, so this is a hand-edited-patch case; unverified in Rack].
- **Preset or undo onto a running module** (`added_`): with Keep on, the tape is left alone (no reload) and only Play and Dub are applied; with Keep off and a different file path, that file loads. This is so an undo does not reload a file over recordings and dubs made since [code, `Eddy.cpp:466`].
- **Initialize** (`onReset`) resets the params to their defaults (Loop on, the rest as in 2.2), empties the tape and the original (a Clear ordered after every job asked for before it), clears the file path and turns Keep back on [code, `Eddy.cpp:347`].
- **Bypass** (`processBypass`): IN L to L, and IN R to R if patched, else IN L to R; EOC low. `process()` is not called while bypassed, so the tape, the head and every fade freeze and resume where they stopped [code; unverified in Rack].
- Menu items that change the tape do so on the worker thread or by an engine request; the UI never writes a tape.
- Saving is in `onSave` (section 9.4).

---

## 4. Signal flow

### 4.1 Block diagram

```
 IN L ──sum all ch──┬─► input (IN R unpatched: R = L) ─────────────────────────────────────────┐
 IN R ──sum all ch──┘                                                                          │
     │ finite(), NaN/inf → 0                                                                   │
     ├─► DC blocker (5 Hz, one-pole) ─► dubIn  (only what a DUB adds; the tape itself is never filtered)
     │                                                                                         │
     │   CONTROLS (every 16 samples): Start, Length, Smear → window;  Wear → wear laws;        │
     │   Dub Level → keep/add;  Mix → dryT/wetT;  Speed knob+CV → target ──► 60 ms one-pole    │
     │   V/OCT → 2 ms linear ramp in volts;   speed = clamp(glide × 2^V, ±4)                  │
     │                                                                                         │
     ├─► RECORD (always speed 1, fs, forwards):  start buffer 0.3 s → original (+ tail ≤ 10 ms)│
     │                                                                                         │
     │   VOICES (up to 4):  BandLimitedRead(tape or original, pos, speed) × gain(env)          │
     │       Σ  ─► × playGain (10 ms) × outputGain(speed) (0 at 0, full by 1/64)               │
     │       │                                                                                 │
     │   each voice moves by speed;  at the window's turn a new voice starts at the            │
     │   window's start (the JOIN: EOC, Smear overlap); loop off: the head fades and stops     │
     │       │                                                                                 │
     │   each voice WRITES what it passed:   old ─► Wear (highs, saturation, level, hiss, dip) │
     │       ─► + dub × ((keep·w + add·dubIn) - w) ─► write bound (soft, 10 V; DC off only     │
     │       while clipping) ─► tape  at gain = voice gain × hold × writeGain(speed) × play     │
     │       × max(Dub fade, Wear fade)                                                        │
     │       │                                                                                 │
     │   OVERVIEW: touched columns rescanned, 32 frames a sample                                │
     ▼                                                                                         │
   out = limit( wet × (tape sum × …) + inGain × input )   inGain = 1 while recording/dubbing   │
        wet, inGain ramp 10 ms toward the Mix law                                             │
        limit: exact to 8 V, tanh to 10 V                                                      ▼
                                                                                  L, R, EOC (10 V, 1 ms)
```

While no usable set exists (before the first tape is made, or after a rate change until the converted tape is offered) the voices and the writes are skipped, the tape side of the output is 0, and the input still passes at the Mix gain (2.10).

### 4.2 Block constants

[code; `EddyTuning.hpp`]

| Constant | Value | File:line |
|---|---|---|
| Fade F (join minimum, Play, Restore, Dub, Mix, holds) | 10 ms | `EddyTuning.hpp:24` |
| Cut (the oldest voice when all four are in use) | 1 ms | `:73` |
| Shortest Length | 10 ms | `:25` |
| Start CV scale | 5 V for the whole tape | `:26` |
| Speed CV | 0.4 per volt | `:13` |
| Speed glide (one-pole) | 60 ms | `:14` |
| V/OCT ramp | 2 ms | `:15` |
| Speed hold | ±4 | `:16` |
| Output fully back / write off / write full | |speed| 1/64 / 1/16 / 1/8 | `:20-21` |
| Flat stretch of the Speed knob | 0.04 each side of -1, 0, +1 | `:11` |
| Control interval | 16 samples | `:76` |
| Voices | 4 | `Voices.hpp:117` |
| Retire slots (sets handed back) | 4 | `EddyEngine.hpp:403` |
| Tape | min(300 s, 2^24 frames) plus a tail of 10 ms | `:50-52` |
| Recording minimums | 0.3 s (button), 50 ms (gate) | `:62-63` |
| Recording start buffer | 0.3 s at 768 kHz, 2 × 230,400 floats = 1.84 MB, in the engine | `:64`; [meas] |
| Start-buffer copy | 64 frames a sample | `:65` |
| Recording window span | 4 s, doubling, held to the capacity | `:70` |
| EOC width | 1 ms | `:72` |
| Overview columns / budget | 512 / 32 frames a sample | `Overview.hpp:47`, `:77` |

### 4.3 The soft bound

One curve serves the output (`limit`), the write bound and Wear's saturation: exactly the identity up to 8 V (bit for bit), then `8 + 2 tanh((|x| - 8) / 2)` [code, `Wear.hpp:32`]. Measured [meas]: 8.5 V gives 8.490, 9 V gives 8.924, 10 V gives 9.523, 12 V gives 9.928, 20 V gives 9.99998, 100 V gives 10.0. At Mix noon an input of 12 V (and any tape) leaves the module at 9.928 V when alone [meas, input 12 V at Mix 0].

---

## 5. The tape, sets and memory

[code, `EddyEngine.hpp:119` `TapeSet`, `Tape.hpp`]

A **TapeSet** is the tape (stereo float frames), the **original** (an untouched copy beside it), their tail frames, and the **overview** (512 columns of peaks that the window draws). The engine owns no tape; sets are made, loaded, converted, copied and freed on the worker thread (section 10).

- **Capacity** is `min(floor(300 s × rate), 2^24 frames)`, plus a tail of `ceil(0.01 s × rate)` frames. Measured by allocating a set [meas]:

| Rate | Capacity (frames) | Length | Tail (frames) | Set (tape + original), MiB |
|---|---|---|---|---|
| 44,100 | 13,230,000 | 5:00 | 441 | 201.9 |
| 48,000 | 14,400,000 | 5:00 | 480 | 219.7 |
| 88,200 | 16,777,216 | 3:10 (190.2 s) | 882 | 256.0 |
| 96,000 | 16,777,216 | 2:54 (174.8 s) | 960 | 256.0 |
| 176,400 | 16,777,216 | 1:35 (95.1 s) | 1,764 | 256.0 |
| 192,000 | 16,777,216 | 1:27 (87.4 s) | 1,920 | 256.0 |
| 352,800 | 16,777,216 | 0:47 | 3,528 | 256.1 |
| 384,000 | 16,777,216 | 0:43 | 3,840 | 256.1 |
| 705,600 | 16,777,216 | 0:23 | 7,056 | 256.1 |
| 768,000 | 16,777,216 | 0:21 | 7,680 | 256.1 |

  `sizeof(TapeSet)` is 4,792 bytes (the overview is inside it). A set's rate is 1 Hz to 4 MHz or the allocation is refused.
- **Memory**: each Eddy sets aside a full pair by `calloc` when the module is added (about 230 MB at 48 kHz: 14.4 M frames × 2 channels × 4 bytes × 2 tapes). The operating system fills it as the tape is filled. While a file or the stored tape loads, or the tape is converted to a new rate, room for a second pair is set aside for a moment (a third, briefly, if the rate changes again before the converted tape plays) [manual; code]. `test_jobs` asserts a same-rate load takes at most one set plus 1 MB of memory, and a load at another rate one set plus the part of the file needed; a refused file allocates under 1 MB [test].
- **Whether the pair counts against the commit limit on Windows** (calloc of 115 MB blocks) and what 16 instances cost are [unverified]: 16 instances is 3.7 GB set aside at 48 kHz.
- The tape is "held at the engine's rate". A file at another rate, or the tape when Rack's rate changes, is converted by the worker; a set made for an older request never replaces one for a newer request (`WorkerBook`).
- A tape cut by the 2^24-frame capacity at a high rate is logged ("the tape is longer than it can be at %g Hz: cut to %.2f s") and stays cut when the rate comes down. A stored tape cut at load leaves the stored files as they were until the tape changes (`StoreRecord::keep`).
- The tail: frames of audio that ran on after the tape's length (a recording's last 10 ms or less of input). The join reads on into it so a turn is exactly Length long.

---

## 6. Transport: Rec, Play, Loop, EOC and Restore

### 6.1 Rec

[code, `EddyEngine.hpp:719-878`; meas, test: `test_transport`]

- **Press Rec** (button, or the REC gate rising): a recording starts on a new tape, at **speed 1, forwards**, whatever Speed says. The old tape's voices fade out (10 ms); the old tape is kept until the new one stands. Dub turns off. The first 0.3 s goes into the engine's start buffer, so a recording that is thrown away has touched nothing.
- **It stands** when it reaches its minimum: **0.3 s if the button started or ended it, 50 ms if the REC gate started and ended it** (one gate edge each way). Measured on a 1 s tape, 48 kHz [meas]:

| Held | By button | By gate |
|---|---|---|
| 40 ms | old tape kept | old tape kept |
| 60 ms | old tape kept | **new 2,880-frame recording kept** (60 ms) |
| 290 ms | old tape kept | new recording kept (13,919 frames) |
| 310 ms | new recording kept (14,880 frames) | new recording kept |

  A recording thrown away leaves the old tape and its waveform exactly as they were (`REC by button, 20 ms and 49 ms: left alone (tape same, 48000 frames, original same, overview same, counters still, playing)` [test: `test_transport`]). The tape pauses for the moment of a tap (fades out at the first press and back where it stopped).
- **At 5 minutes** (the capacity) recording ends by itself.
- **When it ends**, up to 10 ms more of the input (`min(F, N/4)` frames, held to the tail capacity) is taken as the **tail**, the recording becomes the original, Play comes on and the tape plays from Start (once, if Loop is off). The worker copies the original onto the tape meanwhile; the voices read the original until the copy is done (`readsOriginal`). The recorded loop has length N: recorded 88,200, 96,000 and 192,000 frames at 44.1, 48 and 96 kHz gave tapes of exactly those lengths, tails 441, 480 and 960, and EOC intervals of exactly 96,000 samples at 48 kHz [meas; test: `length`, `exact_loop`].
- **The join is made from real audio**: the tail is the input after Rec ended, so the end runs into the beginning.
- **Mono**: IN R unpatched records IN L to both sides.
- While recording the input is heard at full whatever Mix says.
- A rate change during a recording ends it as a recording (what was recorded becomes the original at its length) [test: `rate_change_during_rec`]; a recording still under its minimum is thrown away.
- A Rec request that cannot be met at once waits (a copy under way, a set not yet taken) rather than being dropped; with no set at the engine's rate it is dropped, not held [code].

### 6.2 Play, Loop, EOC

- **PLAY** (the cap) starts and stops the tape by a 10 ms linear fade (`playGain_`). Stopped, the head stays where it is [meas: a 2 V DC tape at Mix 1 reaches full after 479 samples (9.98 ms) on Play on, and 0 after 479 samples on Play off].
- **A PLAY trigger** plays the tape from Start (from the window's end if the speed is backwards), running or not; a retrigger while it plays fades from the old place to the new (equal power, 10 ms).
- **LOOP on** (default): the head comes round the window for ever. **Off**: it plays to the window's end, fades out through the join, EOC fires, and Play goes off with its light; the next Play starts from Start.
- **EOC**: 10 V for 1 ms (section 2.10) at every join in either direction and at the end of a one-shot. The EOC follows the turns as heard (shorter with Smear).
- During a recording the PLAY trigger sets the Play flag but is otherwise not acted on (the discrepancy in 1.1).

### 6.3 Restore and Make this the original

- **Restore** (the button or a trigger at TRIG): the original goes back on the tape, wear and dubs gone. The head keeps its place and the tape keeps playing. The change is a **10 ms equal-gain** crossfade: the voices swap to read the original at the same place at once (the two hold the same audio there, or nearly, so equal power would swell 3 dB) and the worker copies the original onto the tape meanwhile [code; test: `restore_level`, no swell; the 2.0.27 behaviour swelled 4.24 V from a 3 V sine].
- Restore **ends a dub** in progress, does nothing on an empty tape or while recording, and does not act on a tape being cleared. While the copy is wanted or under way, writes (Dub and Wear) fade out over 10 ms and are held (`holdGain_`); the voices read whichever of tape and original the copy is not writing.
- A copy of a 5-minute 48 kHz tape (115 MB) takes 1.8 to 6.3 ms of memory copy on this Mac [meas], plus up to the worker's 5 ms poll and the 10 ms hold. A Restore or Make-original that arrives while another copy is under way waits behind it; the later of two replaces the earlier [code].
- **Make this the original** (menu): the tape (worn, dubbed) becomes the new original (a worker copy, tape to original). The audio is unchanged.
- The original changes only at a recording, a file load and "Make this the original"; the file on disk is never written to.
- At audio rate, triggers on PLAY, RESTORE, REC and DUB for 20 s of audio with a real worker thread leave every invariant intact [test: `test_threads`, `storm`].

---

## 7. Speed and the read: band-limiting

### 7.1 The read

[code, `BandLimitedRead.hpp`; test: `test_band_read`, `test_alias`]

Every read of the tape goes through `BandLimitedRead` (the Hermite read of `Tape::read` is no longer used by Eddy; it stays for Spindrift and as a test reference).

| Constant | Value | Line |
|---|---|---|
| Half-width (output samples either side) | 18 | `:77` |
| Cutoff of the faster-than-1 kernel | 0.9 of the output's Nyquist (-6 dB) | `:78` |
| Kaiser beta | 5.65 (about 60 dB) | `:79` |
| Table steps per output sample | 128 | `:80` |
| Crossfade between speeds 1 and 1.02 | 0.02 | `:81` |
| Widest speed read | 4 (Eddy holds speed to ±4) | `:82` |

- **Faster than ×1**: a windowed-sinc kernel laid over the tape |speed| times wider than at speed 1, always cutting at the same place in the OUTPUT's spectrum. It uses about 36 × |speed| taps a channel (72 at speed 2, 144 at speed 4). Whatever the speed puts above the output's Nyquist is taken out instead of folding back.
- **At ×1 and slower**: the same window as an interpolating kernel (cutoff at the tape's Nyquist, 36 taps). A whole frame read at 1 is that frame to the bit; between frames the band is flat to 0.8 of Nyquist.
- **Between ×1 and ×1.02** the two reads are crossfaded linearly, so nothing steps as a glide or an audio-rate Speed CV passes through 1.
- **At exactly ×1 on whole frames** the output is the tape's own samples to the bit: the knob resting at +1 or -1 since Play or a retrigger, no Speed CV or V/OCT moving it off 1, the window on whole frames and Smear 0. After a glide lands on 1, or at Smear above 0, the head is between frames and the tape is interpolated [test: `speedOneToTheBit`: forwards, backwards, and 2 × 2^-1 all identical to the tape's frames at -159.5 dB or better].

### 7.2 What the tests measure (`test_alias --table`)

Every number below is from `test_alias --table` or `test_band_read --table` [test], at 44.1 and 48 kHz, 5 V tones; "other" is the strongest component that is not the tone or its expected partials, in dB re the tone (or re 5 V where stated). The two rates agree to within 0.2 dB unless shown.

**Faster than ×1** (alias = content the speed pushes above the output's Nyquist):

| Speed | Tone at 0.1 / 0.2 / 0.3 / 0.4 of the rate lands at (× Nyquist) | Worst other | Level of tones that fit (0.1 fs) |
|---|---|---|---|
| ±1.5 | 0.30 / 0.60 / 0.90 / 1.20 | -68.0 dB (the 1.2 × Nyquist tone is removed: level not measurable) | 0.00 dB; 0.9 × Nyquist is -6.02 dB |
| ±2.0 | 0.40 / 0.80 / 1.20 / 1.60 | -68.2 dB | 0.01 dB |
| ±2.37 | 0.47 / 0.95 / 1.42 / 1.90 | -73.6 dB | 0.95 × Nyquist reads -18.04 dB |
| ±3.0 | 0.60 / 1.20 / 1.80 / 2.40 | -68.0 dB | 0.01 dB |
| ±4.0 | 0.80 / 1.60 / 2.40 / 3.20 | -80.5 dB | -0.01 dB |

- Just above ×1 (tones landing at 1.03 to 1.04 × Nyquist; speeds 1.05 to 3): -63.9 to -64.5 dB; tones landing farther above: -72 to -88 dB. The reader's own sweep from ×1.02 to ×4 finds **-57.1 dB at worst** (speed 3, content landing on Nyquist itself), -57.4 at ×1.26, -59 to -62 elsewhere; the passband stays within -0.008 to +0.010 dB up to 0.8 of Nyquist [test: `test_band_read --table`].
- Bounds the tests enforce: alias ≤ **-50 dB** (not 57), passband within 0.1 dB, noise level within 0.2 dB (section 1.1).
- **Across speed 1**: the level of a 0.1 fs tone moves at most 0.003 dB for speeds 0.9 to 1.5, others -80.5 to -82.7 dB (-159.5 dB exactly at 1.00). A 0.42 fs tone glided 0.9 → 1.1 → 0.9 steps 0.221 / 0.226 dB at most per 10 ms (44.1 / 48 kHz; bound 0.3 dB): the faster kernel's roll-off sliding past the tone, not a step at 1. An audio-rate Speed wobble of ±0.002 round 1 at 200 Hz leaves other components at -76.5 / -76.4 dB re the tone (bound -73.4).

**At ×1 and slower** (images):

| Speed | Tone at 0.1 / 0.2 / 0.3 / 0.4 of the rate: images | Level |
|---|---|---|
| ±0.5 | -81.0 / -70.8 / -66.7 / -63.1 dB | ≤ 0.01 dB |
| ±0.25 | -81.7 / -76.8 / -69.4 / -63.9 dB | ≤ 0.01 dB |
| ±1.0 | -159.5 dB (the tape's own samples) | 0.00 dB |

  Images from content between 0.4 and 0.5 of the rate (above 19.2 kHz at 48 kHz) reach **-7.8 dB** at the same speeds; they are recorded, not bounded [test: `test_band_read --table`]. A tape that holds a lot of 20 to 24 kHz material, played at half speed, therefore has images. Files at 44.1 or 96 kHz are converted by a sinc with its own low-pass (below), so such content is mainly an issue for tapes recorded in Rack.

**Level against speed** (the user heard the tape get quieter as the knob was turned down): white noise against the frames the head passed [test]: speed 2 gives **-3.57 dB** (about half the band lands above Nyquist and is taken out; it is not a fault), speed 1 gives 0.000 dB, 0.5, 0.25 and 1/32 give -0.094 to -0.100 dB (the speed-1 kernel's roll-off from 0.8 of Nyquist). A 1 kHz sine is within 0.004 dB from speed 2 down to 1/64; at 1/128 it is -6.02 dB (the stop fade). The Hermite read this replaced lost 0.86 to 0.91 dB of white noise's level and left images at -11 dB.

**Speed CV at audio rate**: the 60 ms glide takes a 200 Hz CV down to about ±0.013 speed; a 1 kHz tone read under it has other components at **-95.9 dB (44.1 kHz) and -114.0 dB (48 kHz)** re the tone (bound -92.9 dB) [test: `test_alias --table`].

**Not band-limited, pinned by the tests** (the numbers are in section 8.3): Wear's saturation inside the loop, the output limit, the write bound, and a dub made away from speed 1.

---

## 8. Dub and Wear

### 8.1 Dub

[code, `EddyEngine.hpp:1171-1212`, `Tape.hpp` `TapeWriter`; test: `test_engine`, `test_tape`]

- **Dub** (the DUB cap, or the DUB gate: on when it rises, off when it falls) writes the input onto the tape at the head, at whatever speed and direction it is going. Played back at the speed it was made at, a dub sounds as it was played [test: `dub_at_speed`, at 0.5 and -1]. Each frame the head passes is rewritten as `old + g × (y - old)`, with `y` the wear result mixed with the dub (`keep × w + add × input`) and passed through the write bound.
- **The write head** (`TapeWriter`) travels with the read head. At speed ≥ 1 each frame gets the input interpolated linearly between the last two samples (speed 1 from a whole frame: the input itself, to the bit). Slower than 1 each frame gets the average of the input over the head's travel across it (a box filter in tape position, centred on the frame). A stop writes nothing; a reversal starts afresh from the head [code].
- **The input is DC-blocked** by a 5 Hz one-pole every sample (so the blocker is settled when a dub starts); only what a dub *adds* passes it. The tape going round is never filtered: what it keeps each pass is exactly `1 - d²` at every frequency, and an offset recorded on the tape only fades as the tape does [code, `Wear.hpp:55`].
- **The write bound** (every write passes it): identity to 8 V, tanh to 10 V. **While it is clipping, and by the share of frames it clips (full from 5 %, exactly 0 when none are), the written signal's DC (5 Hz estimate) is taken off before clipping.** Its state goes from voice to voice on the same tape and is fresh for a new tape. Where nothing is clipped the write is the bound alone [code, `Wear.hpp:99`].
- **Dub does nothing** on an empty tape, while recording, with the tape stopped or under 1/16 speed (fading in to full from 1/8), or while a copy is wanted or in flight (held). It turns off when a recording starts, a file loads or Restore is pressed.
- **With Smear**, each of the two overlapping turns writes what it plays, scaled by its fade, so a dub laid down across the join is heard once at its level next turn [test: `dub_through_join`]. A dub made in the first turn after Play, a retrigger or a window move is heard through the next join within about 2 dB of its level, then exactly (manual).
- **Reverse**: what DC is left on the tape does not settle, wandering within tens of millivolts (a 1 V pulse at Dub 5 % without Wear: between -79 and +64 mV over 2,000 passes; with Wear 100 %: between about -71 and +178 mV) [manual; issue #69, case 3].

### 8.2 Wear

[code, `Wear.hpp:125`, `EddyTuning.hpp:28-38`; meas: `meas1 wear`, `hiss`, `drops`]

Wear is what each **pass** of the head costs the tape, written back as the head goes by, so the loss builds pass over pass. At Wear w (0..1), each pass:

| Part | Law | Constant |
|---|---|---|
| Level | sinks by 0.5 dB × w² | `WEAR_LEVEL_DB` |
| Highs | moves the tape 0.3 × w² of the way toward a one-pole 4 kHz low-pass | `WEAR_HIGHS_AMOUNT`, `WEAR_HIGHS_HZ` |
| Saturation | the soft bound at drive 1 + w, scaled back (never adds), less the DC of the change (5 Hz estimate) | `WEAR_DRIVE_BASE` |
| Hiss | uniform noise of RMS 5 V × 10^(-70/20) × w = 1.58 mV × w | `WEAR_HISS_DB`, `WEAR_HISS_REF_VOLTS` |
| Dropouts | at an average 0.5 × w² per second of tape passed, a stretch of 5 to 40 ms drops to 0.3 to 0.8 of the level, with 2 ms edges | `WEAR_DROPOUT_*` |

- **At Wear 0 the tape is not rewritten at all** and the loop repeats exactly for ever [test: `wear_zero`, 1,000 passes, memory unchanged (checksum), both directions; `test_alias`: at Wear 0 and Dub off, 2 s of turns leave the tape as it was to the bit at speeds 0.5 and 2].
- **Wear acts only where the head passes**: the window and the moment past its end that the join reads on into [test: `wear_window`]. It writes nothing while the tape is stopped, below 1/16 speed, or recording.
- **Each voice has its own Wear state** (seeded from the engine's seed and the voice's number), so hiss and dropouts differ from turn to turn; only the saturation's DC estimate goes from voice to voice on one tape. The engine's seed is `random::u32()` per module, so two Eddys never wear alike [code, `Eddy.cpp:265`].
- **Measured level per pass** on a 0.5 s loop of 2 V sines (median of 10 ms windows, so dropouts do not count) [meas]:

| Wear | Level law (0.5 w²) | 100 Hz | 1 kHz | 8 kHz |
|---|---|---|---|---|
| 0.10 | -0.0050 dB | -0.0051 | -0.0063 | -0.0216 |
| 0.25 | -0.0312 | -0.0321 | -0.0396 | -0.1363 |
| 0.50 | -0.1250 | -0.1253 | -0.1541 | -0.5378 |
| 0.75 | -0.2812 | -0.2820 | -0.3446 | -1.2325 |
| 1.00 | -0.5000 | -0.5011 | -0.6073 | -2.2488 |

  The bass goes only as the level does (100 Hz matches the law within 0.002 dB); the highs also dull. How long a loop takes to fall [code arithmetic for the level law at 100 Hz; the highs make 1 kHz and up fall faster]:

| Wear | dB per pass | 6 dB after | 20 dB after | 20 dB on a 1 s loop | on a 5 s loop | on a 30 s loop |
|---|---|---|---|---|---|---|
| 0.10 | 0.005 | 1,200 passes | 4,000 passes | 67 min | 5.6 h | 33 h |
| 0.25 | 0.031 | 192 | 640 | 11 min | 53 min | 5.3 h |
| 0.50 | 0.125 | 48 | 160 | 2.7 min | 13 min | 80 min |
| 0.75 | 0.281 | 21 | 71 | 71 s | 6 min | 36 min |
| 1.00 | 0.500 | 12 | 40 | 40 s | 3.3 min | 20 min |

- **Hiss** on a silent 1 s tape after one pass [meas]: Wear 0.25 gives 0.395 mV RMS, 0.5 gives 0.789 mV, 0.75 gives 1.184 mV and 1.0 gives 1.579 mV (L and R within 0.001 mV), exactly the law (-70 dB re 5 V × w).
- **Dropouts** on a 120 s tape of a 1 kHz 2 V sine, one pass, 5 ms windows below 0.9 × the median [meas]:

| Wear | Dips in 120 s | Per second (law 0.5 w²) | Mean length (law 5 to 40 ms, mean 22.5) | Deepest window, mean (law 0.3 to 0.8) | Min / longest |
|---|---|---|---|---|---|
| 0.5 | 15 | 0.125 (0.125) | 29.3 ms | 0.55 | 0.34 / 40 ms |
| 0.75 | 31 | 0.258 (0.281) | 26.1 ms | 0.54 | 0.34 / 40 ms |
| 1.0 | 62 | 0.517 (0.500) | 24.2 ms | 0.57 | 0.30 / 40 ms |

- **The saturation's DC** is taken off through the 5 Hz estimate; the estimate is dropped while Wear is 0 (otherwise it would be written onto whatever was dubbed meanwhile when Wear came back). That is the cause of issue #69 case 1.
- **DC left on the tape** (figures quoted from the design note and the manual, measured on 2.0.28, whose code is unchanged; I did not re-measure them; `test_engine` asserts bounds on these cases): a 5 V 25 % pulse at Dub 50 % in time with the loop: -393 mV; 1 V pulse at Dub 5 %: -0.4 mV; a sine with its 2nd harmonic at 1 V, Dub 5 %: +6 mV; with Wear 30 % on a 50 ms loop: about -60 mV; Wear 100 % and a 5 V pulse dubbed at 50 % moves between -22 and +120 mV [test: `wear_dub_lopsided`, `dub_lopsided`; issue #69 cases 1 to 7].

### 8.3 Aliasing that is pinned, not fixed

[test: `test_alias --table`, rule 16 of TESTING.md]

| Source | What was measured | Result |
|---|---|---|
| Wear's saturation in the loop (Wear 1, 16 passes) | 5 kHz at 5 V: strongest other component | -65.8 dB at pass 1 to -52.5 dB at pass 16 |
| | 5 kHz at 9 V | -33.1 dB at pass 1 to -31.5 / -40.9 dB at pass 16 (44.1 / 48 kHz) |
| | 9 kHz at 5 V | -86.9 dB at pass 1 to -43.3 dB at pass 16 |
| | **9 kHz at 9 V** | **-17.2 dB at pass 1; -10.5 dB (44.1 kHz) at pass 16** |
| Output limit and write bound | 5 kHz at 9 V / 12 V | -55.4 / -31.8 dB |
| | 9 kHz at 9 V / 12 V | -53.5 / -23.3 dB (44.1 kHz), -53.3 / -23.2 dB (48 kHz) |
| Dub at speed 0.5 | a 5 kHz input (lands on the tape at 10 kHz) | clean (under -150 dB), level -1 dB |
| | a 15 kHz input | folds back at -12.7 / -10.2 dB re 5 V |
| Dub at speed 2 | a 5 kHz input | image at -30.1 / -31.5 dB |
| | a 15 kHz input | image at -11.7 / -13.1 dB |

A tone at 0.4 of the rate worn one pass is on the tape the same at speeds 0.5, 1 and 2 (-2.600 dB at 44.1 kHz, -2.674 dB at 48 kHz): the read's kernels do not reach the tape [test]. **For hot, bright material (a tone above 5 kHz past about 8 V) with Wear up, expect audible aliasing that builds with the passes.** The test bounds are the measurements plus 3 dB.

---

## 9. Files: load, save and the patch's tape

### 9.1 Load file

[code, `TapeJobs.hpp:199`, `WavFile.hpp`; test: `test_files`, `test_jobs`; meas: `meas2 files`]

- WAV, **mono** (both sides), stereo, or more (the first two channels of up to 64); 8, 16, 24 or 32-bit integer PCM, or 32 or 64-bit float; **1 Hz to 768 kHz**. Compressed WAV, truncated files (shorter than the header says), empty files and non-WAV files are refused with a reason in the log, leaving the tape as it was (a refused file allocates under 1 MB; it is probed first).
- A file's full scale (±1.0) is ±5 V on the tape. Every sample is held to ±100 V first (so a float file's 1e38 cannot overflow), NaN and infinity read 0. The original is made equal to the tape and the overview built.
- A file longer than the tape's capacity gives its first part (5 minutes at 44.1 and 48 kHz, less above 56 kHz); the log says "longer than the tape: its start".
- A file at another rate is converted by `Resample.hpp`: a Kaiser-windowed sinc (beta 9.0, about -90 dB stopband), 32 zero crossings a side, its cutoff lowered with the rate when it goes down; it costs about 128 multiply-adds a frame, and allocates a table (never on the audio thread). Converting reads only the frames needed, never a long file whole [test: `resample_pitch` within 1 cent, `resample_alias`, `resample_length`].
- Loading stops recording and dubbing; Play stays as it was (if on, the new tape plays from Start). A new tape takes over by a short fade, never a step. The file on disk is only read.
- Time on the worker thread for a 300 s stereo float file [meas, warm disk cache, M5 Pro]:

| Job | Time |
|---|---|
| 48 kHz file at 48 kHz (no conversion), 110 MB | 0.07 s |
| 44.1 kHz file at 48 kHz | 1.28 s |
| 96 kHz file at 48 kHz (220 MB file) | 2.56 s |
| 48 kHz file at 44.1 kHz | 1.30 s |
| 96 kHz file at 96 kHz (cut to 2^24 frames: 174.8 s) | 0.08 s |
| convert a live 300 s tape and original, 48 → 44.1 kHz | 2.47 s |
| convert 48 → 96 kHz (cut to 174.8 s) | 2.99 s |

  Cold disk and Windows will be slower; these are not audio-thread costs. Removing the module while a long file loads holds Rack's audio until the load ends (the worker is joined) [spec; code, `stopWorker`; unverified in Rack].
- The log lines to look for (grep `Eddy:` in Rack's `log.txt`): "loaded &lt;path&gt; (&lt;s&gt; s)" (with "; longer than the tape: its start"), "cannot load &lt;path&gt;: refused: &lt;reason&gt;".

### 9.2 Save file

- Writes the **whole tape** (not the window), as it now is, worn and dubbed, as a **32-bit float stereo WAV at Rack's sample rate** (rounded to an integer), with ±5 V back to ±1.0 (÷ 5, so a tape dubbed hotter than 5 V is not clipped). A file loaded at another rate is saved at Rack's rate. An empty tape writes nothing and the log says "cannot save the tape to &lt;path&gt;: the tape is empty".
- The file is written to `<path>.tmp` then renamed over the old; the write is on the worker thread. A 300 s tape took **0.06 s** to export [meas].
- **Round trip**: saved and loaded again, the tape comes back within one rounding step of a float: the largest difference over 14.4 M frames was **1.19e-7 V** [meas]. The tape stored with the patch, in volts, comes back to the bit [test: `stored_round_trip`].

### 9.3 Clear and Make this the original

See 6.3 and the menu (3). **Clear** empties the tape and the original (the engine empties the set in place once every job asked for before it has been taken); Rec, Dub and Play go off. "A file load from before a Clear does not become the patch's file."

### 9.4 With the patch

[code, `Eddy.cpp:365-435`, `TapeJobs.hpp:361-506`; test: `test_jobs`, `test_book`]

- Saved in the patch: the params, the JSON of section 3.1 and, with **Keep the tape with the patch** on, three files in the module's folder inside the patch (`getPatchStorageDirectory()`): `tape.wav` and `original.wav` (32-bit float stereo, in volts, at Rack's rate, each its length plus its tail), and `tails.wav` (one frame: the tape's tail in frames on the left, the original's on the right). About 115 MB each for a full 5-minute tape at 48 kHz (109.9 MiB; 106 MB at 44.1 kHz); a patch with a full tape is about 230 MB larger. An empty tape, or Keep off, stores nothing (the folder is removed; a failed removal is logged and does not fail the patch's save).
- **They are written in `onSave`, synchronously, on the UI thread**, only those that have changed since they were last written (change counters, `StoreRecord`). A tape that is wearing or being dubbed changes all the time, so it is written at every save. Saving both 300 s files took **0.11 s** (the tape file alone 0.05 s) on this Mac [meas]; Rack's own packing of the patch adds to it (the manual says about 0.16 s). Whether Rack's archive step or autosave behaves as the manual says is [unverified].
- **The accepted race**: `onSave` and the Save-file export read the live tape while the audio thread may write it (Wear, Dub) and the worker may copy onto it (Restore). A torn read costs one sample of wear either side of it. It is not in `test_threads` (TESTING.md, rule 7). The counters moved, so the next save writes it again.
- **On load**: `onAdd` posts an empty set at once (to record into), then, with Keep on and a patch folder that exists, a stored load (`LOAD_STORED`); with Keep off and a file path, a file load; else it applies the transport. The patch's Play and Dub are applied when the tape is offered. A patch whose stored files are missing or damaged opens with what can be read (either of tape and original alone serves as both), or with an empty tape, and the log says so. The stored tape is converted if the patch was saved at another rate. Log lines: "loaded the stored tape (&lt;s&gt; s)" (adding "; not as stored (converted or partial): the next save writes both files" when so), "the stored tape in &lt;dir&gt;: &lt;reason&gt;; the tape is empty", "the stored tape is longer than the tape holds at &lt;rate&gt; Hz: cut to &lt;s&gt; s; the stored files stay as they are until the tape changes".
- **Rack's "Auto" sample rate at launch**: the engine runs at 44.1 kHz until the audio device's first callback. A tape that has not gone live when the rate changes is read **again** from its source at the new rate, not converted twice (log: "the rate is now &lt;rate&gt; Hz before the tape went live: making it again from its source"; "the rate changed to &lt;rate&gt; Hz while the tape was made: making it again"). Only a tape that went live is converted, from itself [test: `test_book`; unverified in Rack].

---

## 10. The worker thread

[code, `Eddy.cpp:1-60`, `172-948`, `WorkerBook.hpp`]

**One worker thread per Eddy** makes, loads, converts, copies, saves and frees tapes, so the audio thread never allocates, locks, copies a tape in bulk or does I/O.

### 10.1 What runs where

| Thread | What it does |
|---|---|
| Audio (`process`) | the engine; `live()`, the change counters, `readsOriginal()` published together in a seqlock for the save; the moment a stored tape goes live (the save's baseline) |
| Worker | `offer()`, `setTransport()` for a patch's tape, `collect()`, `copyWanted()`/`copyDone()` (the bulk copies), `live()` for copies and export, freeing |
| UI (menu, `onSave`, `dataFromJson`, `onReset`, the window) | `clear()` and `setTransport()` under `orderLock_` (the lock the worker takes around `offer()`, so the last of a Clear and an offer wins), `requestCopy()`, `live()` under `viewLock_` |
| `onSampleRateChange` | an atomic store to the engine and the worker's rate, and a notify |

### 10.2 The loop

- Started in `onAdd`, joined in `onRemove` and in the destructor (`stopWorker`: sets stop, **clears the queue**, notifies, joins; a job under way finishes first, so removing the module during a long load holds Rack's audio until the load ends).
- `run()` waits on a condition variable for up to **5 ms** or until a job is queued, then calls `service()` and runs one job. So even with no work the worker wakes **200 times a second per instance** (`wait_for(…, 5 ms)`, `Eddy.cpp:713`) [code]. Its CPU is not in the bench figures (those are the engine alone).
- `service()` does, in order: the copy the engine asks for (`copyWanted()`: Restore, a recording's end, Make-original), collects sets handed back (`handedBack`), notes that a conversion's source can go once the last offered set is live, offers the pending set, and frees what nobody can hold.
- Jobs ([code, `Request::Kind`]): `FRESH` (an empty set at the rate), `LOAD` (a file), `LOAD_STORED` (the patch's folder), `CONVERT` (made by the worker itself, from a set handed back), and the Save-file export.
- A set offered to the engine goes into a **single slot**; the engine takes it only when no copy is in flight, no older set is still fading, and a retire slot is free (4 slots). The old set's voices fade out (10 ms) and it is handed back through `collect()`.

### 10.3 The rules (WorkerBook)

WorkerBook decides what the worker does with the sets it makes, offers and takes back. It is Rack-free and tested in `test_book.cpp`. The rules, from its header comment:

- Every job and every Clear or Initialize is numbered in the order asked (`seq`). **The newest request always wins**: no set may take the place of one made for a later request.
- A job that finishes at another rate than the one in force is run again at the new rate, from its source.
- A set handed back for its rate is made again only if it is the newest: one that never went live is loaded again (or made again) from its source, with the patch's Play and Dub still to apply; one that went live is the user's tape and is converted. A conversion that never went live is converted again from the **same source**, which is kept until the conversion goes live: a tape is converted once, from what was live, to the rate finally in force. If that fails, the live set stays if usable, else an empty set at the rate takes its place.
- A set from before a Clear is not offered over a usable live set; with none it is offered emptied. A file load from before a Clear does not become the patch's file.
- A stored load is "under way" from when its job starts until its set goes live, it fails, a later request's set is made, a Clear cancels it, or its set is freed; while it is under way the save leaves the folder alone.

### 10.4 Freeing a set

The engine hands back sets it is done with, but the window and the save may have read one through `live()` a moment before. They use a set only while holding `viewLock_`; the worker frees a handed-back set only once `live()` no longer returns it **and** after taking and dropping `viewLock_`. The worker keeps every set it made in `sets_`; whatever is left is freed by the destructor after the join.

### 10.5 Coverage

- `test_book` [test]: 400 seeded runs, 3,200 settled states checked, 5,848 conversions, 7,600 loads, 12,403 offers, with rate changes, Clears, loads and failures at every stage.
- `test_threads` (ThreadSanitizer; it ran on this Mac): 20 s of audio with the trigger storm on the audio thread, a real worker offering, collecting and copying sets (50 sets offered, 49 collected, 6 of them stale, 829 copies, 15 clears, 40 rate changes), and a third thread reading the live overview as the window does: no data race reported.
- **Not covered**: `Eddy.cpp` itself (the thread loop, the queue, `viewLock_`, the save, the atomics between the engine and the worker). No test links it. Issue #70 is a hole of this kind (section 13.1). The code is exercised only by `make smoke` (two headless runs, one quitting while a load runs; not run by me) and by QA.

---

## 11. The display

[code, `widgets.hpp:550-776` `TapeWindow`, `Overview.hpp`, `Eddy.cpp:643-663`, `TapeGesture.hpp`, `eddy/Select.hpp`]

The window is **vector-drawn** (NanoVG) in the self-illuminating layer (layer 1, so it stays lit when Rack dims the room) in the theme's cyan (`RkaTheme::SIGNAL`), on dark glass in a brass bezel, at (5.0, 19.5) mm, 71.28 × 14.0 mm. The lettering is D-DIN Condensed (`tools/fonts/D-DINCondensed.ttf`, shipped in the package under the SIL Open Font License 1.1). A ruler in tenths of the tape is printed on the panel under the glass (the generator draws a tick every fortieth of 320 px and a long one every tenth).

### 11.1 What is drawn

- **The waveform**: the overview's 512 columns of the tape's peaks (both sides together: the least and greatest sample in the column), as vertical strokes. The glass spans **±6 V** (`RANGE_VOLTS`), so a ±5 V tape fills most of it; a hotter tape is held to the edges. At 100 % zoom the glass is about 210 px wide, so columns are 0.4 px wide and are grouped 4 to a stroke (`STROKE_PX` 1.6): **128 strokes**. [code arithmetic] Bright strokes are those in the part in play, others at 30 %; a window that runs past the tape's end is drawn in two pieces.
- **The window's ends**: a thin line (80 % of the cyan, 0.6 px) at each end. While recording only the start line is drawn.
- **The head**: an ivory line (1 px), full height.
- **Smear's overlap**: along the bottom edge, back from the window's end (in two pieces if it passes the tape's start).
- **The tape's length** in the top right corner as `m:ss`, truncated, not rounded (`(int) seconds`: a tape of 4:59.9 reads 4:59). With an empty tape no lettering is drawn at all (only the flat line) [code, `widgets.hpp:703`, `:765`].
- **Empty**: a flat line along the middle (60 % cyan).
- **A dub shows as it lands** and **wear as the waveform thins**: both because the overview follows the tape (11.3).

### 11.2 While recording

The window shows a span that starts at **4 s** and doubles each time the recording reaches its end: 4, 8, 16, 32, 64, 128, 256 s, then the tape's capacity (300 s at 48 kHz; 174.8 s at 96 kHz). The recording always fills half to all of the glass. The head is at the recording's front and the corner counts the time recorded. For the first 0.3 s a bright strip grows from the left over the old tape's waveform, now dimmed (over an empty tape, over the flat line); then the old waveform goes. When the recording ends the window shows the new tape edge to edge. The doubling merges columns in pairs (exact, no rescan) [test: `record_span`: 15 s at 48 kHz is shown over 4, then 8, then 16 s].

### 11.3 How the overview is kept up to date

- The overview is a reduced copy: 512 columns of (lo, hi), kept up to date by the **audio thread**, which touches the columns a write passed and rescans touched columns **32 frames a sample** (`OVERVIEW_BUDGET`) [code]. A column of a 5-minute tape is 28,125 frames, so one column takes 879 samples (18.3 ms) to rescan; a full rescan of a 5-minute tape takes 14.4 M / 32 = 450,000 samples, **9.4 s** at 48 kHz [code arithmetic]. A touch in a column being rescanned queues another pass, not a restart, so a head dubbing on in a column does not starve it; a touch shows within two passes.
- A column is published only when complete, so the window never shows a half-scanned column. `lo`/`hi` are relaxed atomics read by the UI.
- The head position, window start/length, overlap, tape seconds, empty and recording flags are posted by the audio thread every **32 samples** (0.67 ms at 48 kHz) as relaxed atomics (`VIEW_SAMPLES`); the window draws at Rack's frame rate.
- The UI copies the 512 columns under `viewLock_` at each draw (`Eddy::view`).
- **Known bug**: on the last step of a recording close to the capacity (from 256 s at 48 kHz: 4:16), the waveform drawn is stale for about 9 s: the span jumps from 256 s to 300 s and the full rescan lags. Measured by recording 270 s at 48 kHz [meas, issue #71]: at 256 s all 512 columns lit (the old 256 s mapping); at 265 s 472 lit against 452 expected; correct at 266 s. Only the picture; the audio and the tape are right, and the window is right after the recording ends.
- Without a module (the browser, screenshots) the window draws a preview tape if `RKA_EDDY_PREVIEW_WAV` names a WAV (as `make smoke` does).

### 11.4 The mouse

- A drag in the window sets START and LENGTH through the params, so the knobs turn to match. **Drag across the waveform** to select the part to play, either way round. **Take hold of an end line** (press within **4 px**, `TAPE_GRAB_PX`) to move that end alone: moving the start keeps the end where it is; moving the end leaves START untouched, to the bit. An end dragged past the other swaps roles. **Double-click** selects the whole tape (Start 0, Length 1). A click that moves under **2 px** (`TAPE_CLICK_PX`) changes nothing. There is no fine mode.
- A selection is at least 10 ms and never wraps past the tape's end. Dragging an end of a window that wraps keeps its other end where it is drawn, so the window stops wrapping. A selection reaching the tape's end stops `END_MARGIN` (1e-6 of the tape, a few frames of 5 minutes) short of it unless it is the whole tape.
- With the Start CV patched, the drag sets the knob so that knob plus CV, as the CV is at that moment, puts the start under the mouse; the knob stops at its ends, so a large CV can leave the start short of the mouse.
- Nothing happens with an empty tape or while recording, and a drag stops where it is if a recording starts or the tape is cleared or replaced while you drag; its release still ends it. A press after a drag whose release was lost ends that drag first.
- One undo step ("set Eddy's window") puts both knobs back, however long the drag.
- Tested offline [test: `test_select`, 54 checks: the LENGTH inverse worst 3.1e-7; a new selection within 1.2e-6 of the tape from the mouse; the start line taken moves the end at most 1.1e-7]. What is not testable offline (Rack's event order, undo history, the real mouse, the dim-room layer) is a manual check.
- Tooltips on Length show the tape's time and need `viewSeconds_`, which the audio thread posts every 32 samples; with no tape or a bypassed module it can be stale.

---

## 12. CPU, sample rates and robustness

### 12.1 Bench figures and budgets

`make bench` cases [code, `src/eddy/bench.inc`], each at 48 kHz, % of one core per instance, at 1, 4 and 16 instances run in turn each sample as Rack runs them:

- **idle**: the defaults, no tape, nothing patched.
- **outputs**: a 10 s tape playing at the defaults.
- **busy**: a full 5-minute tape at Speed 2 (knob 1 plus Speed CV +1), Dub on, Wear 1, Smear 1, a window of about 1 s, noise in, the Speed CV moving about 1.8 to 2.2. Two voices read, write and wear at once all the time and the overview is always rescanning.
- **slow**: the 10 s tape at half speed (the common case, through the interpolating kernel).
- **fast4**: Speed 4 (knob 2 and V/OCT +1 V), Dub on, Wear and Smear 1, a window of about 0.1 s.

| Case | Budget, Apple arm64 | Budget, other (provisional) | TESTING.md (the repo's figure) | Mine, 1 instance, three runs [meas] | Mine, 4 / 16 instances [meas] |
|---|---|---|---|---|---|
| idle | 0.2 | 0.6 | 0.086 | 0.099 to 0.116 | 0.097 / 0.099 |
| outputs | 0.3 | 0.9 | 0.130 | 0.139 to 0.143 | 0.154 / 0.157 |
| busy | 1.7 | 5.1 | 1.36 | 1.466 to 1.484 | 1.465 / 1.493 to 1.506 (a full-suite run, with other work going: 1.577 / 1.679) |
| slow | 0.35 | 1.05 | 0.162 | 0.159 to 0.164 | 0.181 / 0.198 |
| fast4 | 3.85 | 11.55 | 1.91 (1.79 alone) | 2.067 to 2.092 | 2.275 / 2.210 |

- **Machine**: Apple M5 Pro, arm64, `make -f tests.mk bench` and `./build/bench/bench eddy <case> <count> 5`. The suite reported "bench: within budget" for every module. My busy figure is about 8 % over TESTING.md's 1.36 (a different machine), and **in the one full-suite run done with other work going the 16-instance busy figure was 1.679 against its budget of 1.70**. In quiet re-runs it was 1.49 to 1.51. If a loaded machine pushes busy at 16 instances over 1.70, the bench fails: that is a headroom note, not a defect.
- The manual's figures (0.09 empty, 0.13 playing, 0.16 half speed, about 1.4 at speed 2 with Dub, Wear and Smear up) match these within noise.
- **Engine only, by sample rate** [meas, `meas2 cpu`, one instance, 5 s of audio, in-process timing on a 10 s tape]:

| Rate | Playing, speed 1 | Half speed | Busy (speed 2, Dub, Wear 1, Smear 1) |
|---|---|---|---|
| 44.1 kHz | 29.0 ns = 0.128 % | 32.0 ns = 0.141 % | 309 ns = 1.364 % |
| 48 kHz | 29.6 ns = 0.142 % | 33.0 ns = 0.158 % | 296 ns = 1.420 % |
| 96 kHz | 29.3 ns = 0.282 % | 31.6 ns = 0.303 % | 272 ns = 2.610 % |
| 192 kHz | 26.7 ns = 0.512 % | 31.9 ns = 0.612 % | 268 ns = 5.137 % |

  There is no internal rate divider: the cost per sample is flat, so the share of a core grows in proportion to the rate. The worst realistic Eddy is about 5 % of a core at 192 kHz.
- The per-instance cost at 16 instances is within 1.2 times the cost of one alone in every case, inside the bench's 1.5 × + 0.01 % rule.
- **Not in these figures**: the worker thread (it wakes 200 times a second per instance, section 10.2), drawing the window, saving and loading, and the worker's bulk copies. Windows CPU is not covered by any automated budget; the other-machine budgets are 3 × the Mac's and have not been measured on native x86-64 [TESTING.md].
- `make engine-globals` (no writable global state, so instances are independent) and `make bench-modules` (15 module declarations complete) pass on this tree [test]. `make libm-calls` (Linux only) was not run; the engine's own comments say there is no libm in `process()` (fast maths come from `FastMath.hpp`; the libm in `BandLimitedRead`'s constructor is off the audio thread).

### 12.2 Sample rates

- The engine has no rate restriction; a set is allocated for any rate 1 Hz to 4 MHz, and Rack's rates (44.1 to 768 kHz) are all covered by `test_hostile` (1 Hz, 8 kHz, 768 kHz, changes between them mid-run and unusable rates) [test]. Capacity by rate: section 5.
- **A rate change**: `onSampleRateChange` stores the new rate; the engine, which plays only a set at its own rate, ends a recording as a recording (the original is what was recorded), fades all voices out at once, and retires the set when no copy is in flight and no voice reads it. The worker converts the tape (and the original, each from its own) from the live tape (once, even if the rate moves again meanwhile; section 10.3). Until the converted set is taken the tape side is silent, Play stays as it was, Dub is turned off (`applyRate`), and the input still passes at the Mix gain (1.1).
- Conversion time, 300 s stereo: 2.5 s (48 → 44.1 kHz) to 3.0 s (48 → 96 kHz) [meas]; the tape is cut and the log says so if it no longer fits.
- The 0.3 s / 50 ms recording minimums, the 10 ms fade and the 1 ms EOC are in seconds, so they are the same at every rate (EOC rounds to whole samples: 44 samples at 44.1 kHz).
- The start buffer is sized for 0.3 s at 768 kHz (1.84 MB), so above that rate the minimums are shorter [code].
- The 10 ms tail and fades are held to the allocation (`tailCapacity`).

### 12.3 Hostile input and numerical safety

[test: `test_hostile`, ASan and UBSan, tapes fenced with ASan poisoning so a read or write outside a tape and its tail is a report; 46 checks; 42.9 s]

- **NaN, ±infinity and 1e30 on every input and param**: the outputs stay finite (`finite()` and the laws' NaN rules: a NaN knob reads its rest value; NaN volts read 0).
- **Window extremes** for 60 s; **seeded fuzz of every control, gate and button for 10 minutes of audio**; a wearing loop of silence; **denormals**: output and tape under 1e-20 V read 0 (`flushTiny`), so a dub over silence cannot take the tape into subnormals.
- **No allocation or free in `process()`**, across Rec, Restore and a set being taken (counting `operator new/delete` and the sanitizer's malloc and free hooks) [test: `no_allocation`].
- **Large inputs**: the output is held under 10 V; the write bound is exact to 8 V and never passes 10 V; a file's samples are held to ±100 V.
- The tests' own limit: the engine ignores no rate; a rate change with a recording in progress is covered (`rate_change_during_rec`).
- `Eddy.cpp`'s own checks and the Rack-side behaviour are not covered (section 14.2).

---

## 13. Open issues and findings

### 13.1 Filed issues: do not re-file

These are the open issues about Eddy in `ryanarneson/RkaModules` at the time of writing (`gh issue list --state all`: Eddy has none closed that explain present behaviour; the build's work was PR #68). Reference the number when you reproduce one.

| # | Title | QA impact |
|---|---|---|
| 69 | Eddy: DC left on the tape in corners of heavy dubbing | Seven corners (Wear switched between 0 and 30 % at 2 Hz or faster: 0.36 to 1.1 V of DC; low Dub Level with moderate Wear on a very short loop: about 60 mV; reverse: wanders -79 to +64 mV; a recorded offset that starts to clip: a step the size of the offset; a recorded offset on a short loop under Wear: about 32 mV rise; the write bound's state, unmeasured; a 10 V pulse into the ceiling: 1 to 1.5 V). **Case 1 is not in the manual** (the issue's first to-do). The issue asks whether a DC block on what is heard is wanted. Test by listening for a thump on PLAY after heavy dubbing; report on #69 |
| 70 | Eddy: a reopened patch can forget its file when a recording crosses 0.3 s during a file load | With Keep the tape off, a REC-button recording that reaches 0.3 s inside a window of about 1 ms while a file load finishes can drop the remembered path. The tape in memory is right. A race that tests cannot hit. A few-line fix is expected |
| 71 | Eddy: the window shows a stale waveform on the last step of a recording near 5 minutes | Reproduced: stale from 256 s to about 265.5 s at 48 kHz (section 11.3). Picture only |
| 66 | Eddy: trim the tape to the window | Future work, not a defect. The questions it lists (the original, a wrapped window, Smear, undo) are open |
| 67 | Aliasing: measure every module that makes or bends sound | Eddy is the first module measured; this issue is the same for the others. Eddy's remaining pinned-not-fixed items are in section 8.3 |
| 61 | DelayLine: fractional reads lose precision at long delays (before Eddy) | The issue says it matters for Eddy. **It does not affect Eddy's playback**: Eddy includes no `DelayLine.hpp` (only a comment in `Tape.hpp` names it); its heads are doubles read through `BandLimitedRead` [code]. It matters for Spindrift and Wake |

### 13.2 Found while preparing this document (not filed)

These are observations I could not find in an issue. They are listed for the owner to decide; I did not file them. Each is in section 1.1 with its measurement.

1. **The manual's "faint images" for a dub made faster than ×1 is not true above about 5 kHz** (-11.7 to -13.1 dB for a 15 kHz input at speed 2).
2. **The fast read's -57 dB figure is a measurement, not an enforced bound** (the tests assert -50 dB).
3. **A PLAY trigger during a recording lights Play** (the manual says it is ignored).
4. **After a rate change the module is not silent** (the input passes at the Mix setting until the converted tape is offered).
5. **Dub on at Dub Level 0 forces the input to full at any Mix** though nothing is written.
6. **The spec's "±5 V" for Start CV** holds only from noon (knob + CV / 5 V, held to 0..1); the manual's wording is exact.
7. **The manual omits issue #69's case 1.** The listening guide points the user to "the manual" for the DC corners.
8. **`Eddy.cpp` pins three of 13 param ids** with `static_assert`, not all.
9. **A loaded file at the defaults loops 10 ms short of the file** (990 ms for a 1 s file); documented only in the Smear paragraph.
10. **The worker polls every 5 ms per instance** whether or not there is work (200 wake-ups a second each).
11. **A save with a full tape is synchronous on the UI thread** (0.11 s here for the files, plus Rack's packing); the spec's anti-goal is "no hung window when the patch saves". 0.11 s is not a hang, but a slower disk or a network drive might make one [unverified].
12. **`m:ss` truncates**: the corner reads 4:59 for 4:59.9.
13. **At the bench's busy case the 16-instance figure can come close to its budget** under load (1.679 against 1.70 once; 1.50 in quiet runs).

### 13.3 The listening guide

`/Volumes/share1/VCVRACK/eddy-2.0.28-listening.html` (build 2.0.28, 2026-11-22; 19 checks in four groups, three "verdicts needed" and eight patches). I read it as a cross-check; the code wins. It is for 2.0.28, and **Eddy's own files are unchanged in 2.0.39**, so it applies. Disagreements and gaps:

- **Its `<title>` says "Eddy 2.0.27 listening list"** while the page's heading, the list's key (`eddy-2.0.28-listening`) and the notes header say 2.0.28. The title is stale.
- **"Verdicts needed: DC on the tape ... (tens of millivolts; more in a few corners listed in the manual)"**: the manual does not list the corner that issue #69 puts first (Wear switched between 0 and about 30 % at 2 Hz or faster: 0.36 to 1.1 V).
- **Check 1** ("WEAR barely open (about 2 %) ... the low end stays"): consistent with the code: at Wear 0.02 each pass costs 0.5 dB × 0.0004 = 0.0002 dB, and the bass is not filtered (100 Hz matches the level law within 0.002 dB at every Wear measured [meas]).
- **Check 2** ("a little more low end is left than in 2.0.27 (about 2 dB at 50 Hz after 50 passes)"): the spec says 2.3 dB at 50 Hz after 50 passes at Wear 0.5 and 1. I did not re-measure that comparison against 2.0.27.
- **Check 6** ("the lows thin while it is pinned"): matches the code (the write bound's DC removal acts only while clipping, and the manual says the lows thin then).
- **Patch 2** ("Record 10 seconds of silence to set the length"): works (a silent recording is a recording; the 0.3 s button minimum applies).
- **Patch 5** ("set LENGTH to minimum ... START now selects the timbre"): Length 0 is a 10 ms window, so the loop is 100 Hz × speed. The window's minimum fade at this length is 2.5 ms (a quarter of the window), shown in section 2.5.
- **Check 16 ("Save the patch several times in a row ... then record a new tape")**: consistent with the code's `planSave` and the `StoreRecord` logic (the tape is written from the original while it is to become the original, so a new recording is not stored beside the old tape).
- **Everything the guide says is "untested"** (four items: a rate change while a file loads, a rate Auto launch, a rate change round trip, a Save-file overwrite prompt) is in section 14.2 as Rack-side and was not run by me.
- I found nothing in it that contradicts a measurement; the two real defects are the title and the manual pointer above.

---

## 14. What the automated suite covers

### 14.1 What it covers

All 15 test programs pass on the 2.0.39 sources in my run (rc 0). "CHECK sites" counts the `CHECK(` calls in the source (many run in loops).

| Program | Wall time | CHECK sites | Covers |
|---|---|---|---|
| test_tape | 0.17 s | 39 | `Tape` reads (exact, backwards, bounds) and `TapeWriter` at 1, 0.5, reverse and fast; no gaps; stall; hostile writes. Prints e.g. backwards at 0.7: error -113.1 dB; 220 Hz written and read at 0.5: -74.9 dB; at -1: -80.3 dB; at 1.5: -83.1 dB; at -3.7: -90.7 dB; 20 kHz written at 0.5 is on the tape at -16.7 dB |
| test_laws | 0.17 s | 58 | `speed_flats`, `speed_cv`, `voct`, `gains`, `window_table`, `turn_exact`, `turn_no_tail`, `turn_smear`, `window_extremes`, `dub_table` against the plan's constants |
| test_select | 0.21 s | 54 | the window's mouse: inverse LENGTH law, new selection, shortest, moving ends, cross over, wrapped, with CV, hostile, grabs, gestures |
| test_wear | 0.48 s | 44 | noise, off, never grows, rate follows the knob, hiss bounded, dropout gains and rate, seeded, the guard (write bound), saturation DC, hostile |
| test_overview | 0.25 s | 39 | brute-force peaks, build, touch and service, the budget, growing with a recording, doubling, a dub in one column, short tapes (14.4 M frame tape) |
| test_band_read | 5.0 s | 14 | whole frames to the bit at ×1 and slower, constants read exactly, the response above 1 (alias ≤ -50 dB, passband within 0.1 dB), the slow response, the blend, the wrap at the seam, hostile |
| test_alias_meter | 0.43 s | 12 | the measuring helper: a planted alias read at the level planted, clean sine clean |
| test_alias | 4.6 s | 20 | section 7.2 and 8.3, at 44.1 and 48 kHz |
| test_engine | 86.9 s | 118 | exact loop (1,000 passes, to the bit outside the join), length at 44.1, 48 and 96 kHz, speed (pitch within 1e-3, direction, V/OCT within 1 cent over ±2 V, silence at 0, knob glide, V/OCT within 5 ms), window, smear, dub levels (within 0.2 dB), dub at speed, bounded and lopsided dubs, wear and dub keep the bass, offsets kept, tape change clean, wear 0, wear window, play trigger, one-shot, EOC, Restore (and level), Make original, the view, record span, record over a tape, record to capacity, Mix law and default and smoothness, the limit, clicks at the join, dub through the join |
| test_transport | 0.73 s | 127 | the double tap on the button and gate, Rec's tail and mono, Play, sets arriving and leaving, rate changes, trigger storms, the change counters, clear ordering |
| test_hostile | 42.9 s | 46 | section 12.3 |
| test_jobs | 0.77 s | 144 | load levels, rates, long files, bad files, export and stored round trips, stored partial, convert keeps both, save plans, huge samples, a golden stored folder, recording copy |
| test_files | 0.29 s | 35 | WAV round trip, formats, truncation, resample pitch, alias and length, bad files |
| test_book | 0.26 s | 101 | the worker's decisions: 400 runs, 3,200 settled states, 5,848 conversions, 7,600 loads, 12,403 offers |
| test_threads | 2.1 s | 10 | ThreadSanitizer: 50 sets offered, 49 collected (6 stale), 829 copies, 15 clears, 40 rate changes; no race |

Total 861 CHECK sites. `test_hostile`, `test_jobs`, `test_files` and `test_book` ran with ASan and UBSan; `test_threads` with ThreadSanitizer; the rest at the plugin's float flags. Other checks that pass: `make engine-globals`, `make bench-modules`, the bench (12.1).

### 14.2 What it does not cover

- **`Eddy.cpp`** (the module): the worker loop and queue, `viewLock_`, the save and load of the patch (`onSave`, `dataToJson`, `dataFromJson`, `onAdd`), the menu, the drop handler, bypass, the lights and the param latches. Only `make smoke` (not run by me) loads it. Issue #70 is a hole here.
- Anything in Rack: the panel, tooltips, file dialogs, drag and drop, undo history (including the drag's one undo step), Randomize, Initialize, duplicate and delete (whether they call `onSave`), Rack's autosave (the manual's claim that it does not write the tape), bypass, the sample-rate change event order, Rack's CPU meter, the window at other zooms and in the dim room.
- **Windows and Linux**: the suite and bench ran on this Mac; `make libm-calls` is Linux-only. CPU on x86-64 or Windows. File paths with non-ASCII characters (the code widens UTF-8 on Windows; no test runs there), rename-over-an-open-file semantics, and the commit charge of 230 MB per instance.
- The golden saved-state check is the **stored folder only** (`test_jobs`, a hand-made golden); the module's JSON keys and `dataFromJson`'s edge cases are not tested, nor are all param ids pinned.
- The accepted race (save and export reading the live tape while the engine writes it).
- **Listening**: how Wear sounds across the knob, Dub Level's noon, Smear's range, the weight of a Speed change, the aliasing from a hot bright tape under Wear (8.3), whether the window reads well, whether an autosave can be felt with a long wearing tape.
- No test records for longer than the span doubling tests do (the 270 s case of #71 is my measurement, not a test).
- Real files in real formats from other software (the tests write their own WAVs): extensible, odd chunk layouts, broadcast WAV.

---

## 15. Suggested QA test areas and expected results

Defaults for each test unless stated: 48 kHz, all knobs at default (Speed +1, Start 0, Length full, Wear 0, Smear 0, Dub Level 50 %, Mix 50 %, Speed CV attenuverter 0), Loop on, Rec, Dub and Play off, Keep the tape on.

**Method notes.**
- Use Mix noon only to listen. For level checks set Mix to 100 % (tape alone) so the input is not added, and note that while recording or dubbing the input is heard at full whatever Mix says.
- Loaded files loop 10 ms short at the defaults; use a recording (or a Length shorter than the file) for exact-loop checks.
- A scope on L and a counter on EOC are the quickest checks of the join and the speed.
- The Windows memory and file-dialog items are not testable from the engine and are marked [unverified].

### 15.1 Per control

| Area | Procedure | Pass criteria |
|---|---|---|
| Record and loop | record 4 s of a 440 Hz sine into IN L, press Rec again | Play comes on; the window shows the tape edge to edge with "0:04"; the loop is exactly the recorded length (EOC every 4.000 s; 192,000 samples at 48 kHz); no click at the join |
| Rec thresholds | tap REC (button) twice quickly over a tape; hold the REC gate for 40 ms and for 60 ms | button taps under 0.3 s: the old tape and its waveform are left as they were; gate 40 ms: left; gate 60 ms: a 60 ms recording replaces the tape |
| Rec limit | record for the capacity (5:00 at 48 kHz; 2:54 at 96 kHz; 1:27 at 192 kHz) | recording ends by itself at the capacity; the window's span doubles 4, 8, 16 … 256 s then to the capacity; **the last step's picture is stale for about 9 s [issue #71]** |
| Speed knob | turn from -2 to +2 | flat at -1, 0 and +1 (1 % of the travel each); the tooltip reads -1.000, 0.000, 1.000 ×; typing 0.5 sets speed 0.5 (knob 0.5); pitch an octave down at 0.5, up at 2, reversed below 0 |
| Speed 0 | stop the knob at 0 on a playing tape | silence, no DC on a scope |
| Speed CV | +5 V at attenuverter +1 on a stopped knob | speed +2 (octave up); -5 V gives -2; the knob plus CV glide over about 60 ms (63 % in 60 ms) |
| V/OCT | a sequencer line at 0 V = x1, +1 V = x2 | an octave a volt within 1 cent over ±2 V; a step lands in about 2.3 ms with no glide; the speed is held to ±4 (knob 2 and +1 V is 4) |
| Start, Length | set a 1 s window in a 10 s tape; move Start by knob and by CV while playing | the loop is exactly Length long (for a window not at the file's end); moving does not click (the head fades 10 ms); Start CV 5 V is the whole tape; negative CV from 0 does nothing |
| Length tooltip | hover | time in ms below 1 s, s above; typing "250 ms" and "1.5" work; with no tape it shows % |
| Smear | window 1 s, Smear 0 then 1 | Smear 0: a 10 ms fade at the join; Smear 1: the turns overlap by half the window (EOC every 0.5 s); level steady across the overlap (equal power) |
| Dub | tape of silence 4 s; hold Dub, play single notes | each note lands where played at Dub Level noon; old layers fall by 2.5 dB a pass (75 %); at 100 % the old is gone; at 0 nothing is written |
| Dub Level | 25 %, 50 %, 75 % | old kept 94 %, 75 %, 44 %; new added at 50 %, 100 %, 100 % |
| Dub at a speed | dub at 0.5 then play at 0.5 and at 1 | at 0.5 the dub sounds as played; at 1 it is an octave up and 2× shorter |
| Wear | loop 20 s of a bass-heavy sound at Wear 0.5 | dulls, sags (0.125 dB a pass), hiss (0.79 mV RMS), dropouts about every 8 s; the bass keeps its level (matches the level law); Wear 0 repeats exactly for ever |
| Restore | wear a loop for a minute, press RESTORE or send TRIG | the original returns in 10 ms with no click and no 3 dB swell; the head keeps its place and Dub is off; works while playing at any speed |
| Mix | Mix 0, 25, 50, 75, 100 on a playing tape with input | 0: input only; 50: both at full; 100: tape only; no click when turning |
| PLAY, LOOP, EOC | Loop on, then off; PLAY trigger | on: round for ever; off: plays once, EOC fires, Play goes off; the trigger plays from Start (the window's end when backwards); EOC is 10 V for 1 ms |
| Randomize | right-click, Randomize | Speed, Start, Length, Wear, Smear, Dub Level, Mix, CV change; Rec, Dub, Play, Loop and Restore do not |
| Initialize | after changing everything | tape empty; Rec, Dub, Play off, Loop on; the file forgotten; Keep the tape on; knobs at defaults |

### 15.2 Files

| Area | Procedure | Pass criteria |
|---|---|---|
| Load a WAV | menu Load file; drop a WAV on the panel; mono and stereo 16/24-bit 44.1/48/96 kHz and a float file | the tape arrives by a fade, at the right pitch (within 1 cent), mono on both sides; the window shows it with its length; Play stays as it was |
| Refused files | a text file renamed .wav, a truncated WAV, an empty file, an MP3 renamed | the tape is left as it was; Rack's log says "Eddy: cannot load ... refused: ..." |
| Long file | a 6-minute 48 kHz WAV | the first 5:00 is taken; the log says "longer than the tape: its start" |
| Rate | a 96 kHz file at Rack 48 kHz | plays at the right pitch; saved later at 48 kHz |
| Save file | after a few dubs, menu Save file; name without .wav; an existing name | a 32-bit float stereo WAV at Rack's rate, ±5 V = ±1.0; Rack asks before replacing "name.wav"; loading it back is within 1.2e-7 V; with an empty tape the log says "the tape is empty" |
| Time | load and save a 5-minute tape | the audio never drops; the window never hangs; log "loaded ... (300.00 s)". Reference on this Mac: load 0.07 s (same rate), 1.3 to 2.6 s (conversion) [meas]; slower on Windows or a cold disk |
| Remove during load [unverified] | load a 96 kHz 5-minute file, delete the module at once | Rack's audio holds until the load ends (a few seconds), then recovers |

### 15.3 With the patch

| Procedure | Pass criteria |
|---|---|
| record, save the patch, quit Rack, reopen | the tape and the original are back, the same length, and play as recorded; the patch folder holds `tape.wav`, `original.wav` and `tails.wav` (about 230 MB for 5 minutes at 48 kHz) |
| Restore after reopening | the original (not the worn tape) returns |
| Keep the tape off, save, reopen | no tape files in the patch; the last loaded file loads again if it still exists; otherwise empty. (Issue #70 is a rare race here.) |
| patch saved at 48 kHz opened at 44.1 and 96 kHz, then back | the tape plays at the right pitch each time, is not shorter or duller after the round trip; the log shows "not as stored (converted or partial)" |
| Rack's rate on Auto at launch with the device at another rate | the tape loads once; the log may say "making it again from its source" (the new path) |
| save several times with a wearing tape | every save keeps the playing tape; reopening shows the newest; each save writes tape.wav again (it has changed) |
| delete the stored files by hand, reopen | an empty tape (or the tape served as both if one is left); the log says so |
| Play and Dub saved on | after the tape is in, Play and Dub come on as saved; Rec never |
| hand-edit the JSON: `"keepTape": "x"`, a missing `"file"` | `keepTape` true, file empty; no crash |
| duplicate the module (Ctrl+D), delete it, undo | the copy carries the tape; the original's tape is intact [unverified: whether the manual's "written when the module is duplicated or deleted" is how Rack calls `onSave`] |

### 15.4 The worker, rates and memory

| Procedure | Pass criteria |
|---|---|
| change Rack's sample rate (44.1 → 48 → 96 → 192 kHz) with a tape and a tail ringing | the module's tape goes silent until the converted tape arrives (about 2 to 3 s for 5 minutes); the input still passes at the Mix gain; the tape returns at the right pitch; a tape longer than the rate allows is cut (3:10 at 88.2 kHz) and the log says so |
| change the rate twice quickly while a file loads | the file arrives once, at the right pitch |
| load a file, then at once load another, then Clear | whatever was asked for last is what you end up with |
| 16 instances | memory set aside 3.7 GB at 48 kHz (230 MB each) [unverified on Windows: commit charge]; per-instance CPU ≤ 1.5 × one alone |
| Windows CPU | record the Rack meter per instance for idle, playing, half speed, busy (speed 2 with Dub, Wear and Smear) at 48 kHz; Mac figures: 0.10, 0.14, 0.16, 1.5 % |
| log | filter for `Eddy:`; expect only the lines of 9.1 to 9.4 and never "cannot" in a normal session |

### 15.5 The display

| Procedure | Pass criteria |
|---|---|
| empty tape | a flat line; no lettering |
| load a file, set Start and Length | the part in play bright, the rest dim, thin lines at its ends; a window past the tape's end is in two pieces; Start moved by CV is drawn moving |
| play | the ivory head runs; moves at the speed; reverses |
| Smear up | the overlap is marked along the bottom edge, back from the window's end |
| dub and wear | a dub appears where it lands (within about 18 ms per column at 5 minutes); wear thins the waveform; the glass spans ±6 V (a ±5 V tape fills most of it; hotter is held to the edges) |
| record | the span starts at 4 s and doubles; the corner counts the time recorded; for the first 0.3 s a bright strip grows over the dimmed old waveform; over a tape of 4:16 or more the last step is stale for about 9 s [issue #71] |
| drag across the waveform; drag an end line; double-click | START and LENGTH knobs turn to match; the end keeps the other end; double-click is the whole tape; one undo step restores both knobs; nothing happens while recording or on an empty tape |
| drag with Start CV patched | the knob stops at its end; a large CV can leave the start short of the mouse |
| zoom Rack in and out; light and dark panels; the dim room | the glass stays drawn, 128 strokes at any zoom; ivory head line and cyan in both themes; the glass stays lit [unverified] |
| length lettering | "m:ss" truncated: a 4:59.9 tape reads 4:59 |

### 15.6 Speed and the read

| Procedure | Pass criteria |
|---|---|
| a bright loop (cymbals, noise) at ×2 and ×4 | no harsh inharmonic tones (anything that would fold back is under -50 dB, measured -57 dB); the top of the band is rolled off from about 0.8 of the output's Nyquist; speed 2 is 3.6 dB quieter on white noise (half the band is taken out) |
| the same at ×0.5 and ×0.25 | clean; images -81 to -63 dB for content up to 0.4 of the rate; a recording with lots of 20 to 24 kHz at 48 kHz may show images at about -8 dB |
| a sine through ×1 by the Speed CV | no step, no click at 1.00 (the level moves within 0.003 dB) |
| Speed CV from a Sway, attenuverter barely open | a smooth wow; no zipper (the CV is read every 16 samples and glided 60 ms) |
| audio-rate Speed CV | an FM-like tone; everything else at -96 dB or lower |
| Wear at 1 on a hot 9 kHz tone (9 V) | **expect audible aliasing** that grows with passes (-17 dB at pass 1; -10.5 dB at pass 16 at 44.1 kHz); pinned by `test_alias`, not a defect to file |
| a dub of bright material at ×2 or ×0.5 | images/folds as in 8.3 (-30 dB for 5 kHz; -12 dB for 15 kHz); at ×1 clean |
| ×1 on whole frames | the output is the tape's own samples: compare against the file: identical to the bit with Smear 0, the knob resting at +1 or -1 |

### 15.7 Robustness

| Procedure | Pass criteria |
|---|---|
| NaN or ±1e30 from a source into every input | no crash, no stuck output, output under 10 V |
| ±10 V and ±50 V audio into IN L and IN R, Mix 100 % and 0 % | the output is held under 10 V (12 V reads 9.928) |
| 16-channel poly cable into IN L | summed; Eddy outputs one channel each; the sum is held under 10 V at the output |
| poly cable into a CV, V/OCT or gate jack | only channel 1 acts |
| gates at audio rate into REC, DUB, PLAY and TRIG for a minute | no crash; the tape is never lost to a race (the engine's trigger storm test passes) |
| bypass the module | IN L → L, IN R → R (L if unpatched), EOC low; unbypass: the tape resumes where it stopped [unverified] |
| a huge Length with Smear 1 (300 s tape) | an overlap of 150 s; no crash; the level steady |

---

## 16. Glossary

| Term | Meaning |
|---|---|
| Attenuverter | a -1..+1 scale on a CV; negative inverts (Speed CV's, and the small Dub Level and Mix knobs are the "atten" size) |
| Capacity | min(300 s, 2^24 frames) of tape at the engine's rate |
| Dub | writing the input onto the tape at the head as it goes by |
| Dub Level | one knob for the new layer's level (min(1, 2d)) and how much of the old is kept (1 - d²) |
| EOC | end of cycle: 10 V for 1 ms at each join |
| F | the 10 ms fade used by Play, Restore, Dub, Mix, the join and holds |
| Flat stretch | the 1 % of the Speed knob's travel either side of -1, 0 and +1 where the speed is exact |
| Frame | one stereo sample of tape |
| Head | the place the newest voice reads; the ivory line in the window |
| Hold | writes fade out while a bulk copy is wanted or under way |
| Join | where a turn ends and the next begins (a fade; a new voice starts at the window's start) |
| Original | the untouched copy of what was loaded or recorded, kept beside the tape |
| Overview | the 512-column peaks copy of the tape the window draws |
| Pass | one trip of the head over a frame; Wear's laws are per pass |
| Recording minimum | 0.3 s (button) or 50 ms (gate) under which a recording is thrown away |
| Restore | the original put back on the tape, wear and dubs gone |
| Set (TapeSet) | the tape, the original, their tails and the overview, made and freed off the audio thread |
| Smear | the length of the fade at the join (10 ms or a quarter of the window, up to half the window) |
| Tail | frames of audio after the tape's length, read by the join (a recording's last up to 10 ms) |
| Turn | one trip of the head round the window |
| V/OCT | an octave a volt: multiplies the speed by 2^V |
| Voice | one read head with its own write head and wear state; up to four |
| WorkerBook | the Rack-free decisions the worker makes about sets (`WorkerBook.hpp`) |
| Wear | what each pass costs the tape: level, highs, saturation, hiss, dropouts |
| Window | the part of the tape in play, set by Start and Length (also the glass that shows the tape) |
| Write bound | the soft bound (exact to 8 V, ceiling 10 V) every write to the tape passes |

---

### Note on the measurement programs

- My probes are four C++ files in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/eddy/`, compiled outside the repository against the extracted 2.0.39 tree (`tree/`, from `git archive 2e6e960`) with the plugin's floating-point flags (`-O3 -funsafe-math-optimizations -march=armv8-a+fp+simd`):
  - `meas1.cpp` (`laws`, `glide`, `fades`, `dub`, `wear`, `hiss`, `drops`, `misc`): the control laws in real units, glides, fades, Dub and Wear per pass, hiss and dropouts, EOC and recording tests, REC thresholds.
  - `meas2.cpp` (`cap`, `turn`, `stale`, `files`, `cpu`, `copy`): capacity and memory by rate, recorded loop length, the #71 reproduction, file jobs on 300 s files (the WAVs it makes are in `wavs/`), CPU by rate, the bulk copy.
  - `meas3.cpp`, `meas3b.cpp`: behaviour after a rate change, PLAY during a recording, Dub at Dub Level 0.
- They drive `rka::EddyEngine` through the tests' own `Rig` (`src/eddy/test_rig.hpp`), which stands in for the module's worker thread, and `TapeJobs.hpp` directly. They do not exercise Rack's ports, threading, `Eddy.cpp` or the GUI.
- The test logs are in `.../eddy/logs/` (`alias_table.txt` is `test_alias --table`; `bench.log` is `make bench`).
- I did not run Rack, `make smoke`, `make libm-calls` (Linux only) or `tools/cross_build.sh`.

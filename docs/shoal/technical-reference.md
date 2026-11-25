# Shoal 2.0.39: Technical Reference for QA

Module: **Shoal**, plugin `rka` (slug `rka`, module slug `Shoal`), VCV Rack 2 (minRackVersion 2.5.0). 28 HP, 142.24 mm × 128.5 mm.
Build: **2.0.39 = commit `2e6e960`** of the RkaModules repository (plugin.json `"version": "2.0.39"`, branch `neap`, not yet merged to main). Everything here was taken from that tree (`git archive 2e6e960`), not from the working copy.
Package on the share: `rka-2.0.39-win-x64.vcvplugin`, 1,003,365 bytes, 2026-11-24 20:04. It holds `plugin.json` with version 2.0.39, `plugin.dll` (2,287,104 bytes), `res/shoal/Shoal.svg` and `Shoal-dark.svg` (both byte-identical to the commit's) and `LICENSE-shoal-firmware.md`. It has no Shoal presets and no manual (`docs/` is not packaged).
Document date: 2026-11-24.

This document is about the **host**: the module that runs the Ornament & Crime (o_C) firmware inside Rack. It covers the threads, restore and reset, hardware mode against friendly mode, the adapters, the scope, the jacks and the timing. It does not describe what the 13 o_C apps do; for that read the o_C v1.3 user manual. Section 9 lists, per app, what the host adds and where the host shows limits.

---

## Build history

Shoal's code and manual are unchanged from test build 2.0.15 to 2.0.39 (`git diff e78c69a 2e6e960 -- src/shoal docs/shoal res/shoal` is empty). The module was written before the test-build numbering began, and the CHANGELOG lists it under "2.0.0 (unreleased)".

| Date | Commit or build | What changed |
|---|---|---|
| 2026-11-14 | `af70531` to `b4d7db7` | o_C firmware v1.3.6 (upstream `cfbffab`) vendored; fake-hardware shim; per-instance context; voltages, triggers, tick clock, ±5 V input range; controls, menu and scope frames; non-blocking app menu; save and restore through the firmware's own storage; validation of restored data |
| 2026-11-15 | `ba8f627` to `4331b25` | Fault sweeps and desktop-CPU guards; reset and rejected restore boot a new firmware instance; `shoal::Host` (swap-in restore built off the engine thread); the module in hardware mode; settings save with the patch |
| 2026-11-15 | `dc1e78d` to `89a8fda` | Friendly mode: model, edits, 13 adapters, the list widget, UI MODE |
| 2026-11-15 | `3c15c5e`, `66cd6ac`, `e5cf618` | Instrument look; manual gains an "Apps" section; fact-check corrections |
| 2.0.14, 2026-11-19 | `6de0cbf` | Upper screen: lanes, letters and smooth views (traces, X/Y, note circle, grid); strip dividers; patches to try in the manual |
| 2.0.15, 2026-11-19 | `23d12df` | Final review fixes for the scope: frame, view and history under one lock; overlay clipped to the rack view; Harrington Euclidean patch in the manual |
| 2.0.16 to 2.0.39 | | No change to `src/shoal`, `docs/shoal` or `res/shoal` |

Shared files that did change after 2.0.15 and that Shoal uses: `src/widgets.hpp` (the lit-cap reference became `CapLightRef`, which `CapButton` and `CapLatch` use; new widgets for other modules), `tests.mk` and `Makefile` (other modules' tests and the module list). `KnurlEncoder`, `PixelScreen` and `Detents.hpp` are unchanged. Whether Shoal's four buttons and the UI MODE latch still light and click correctly after the `CapLightRef` change has not been looked at in a live Rack [unverified]; the tests do not build widgets.

---

## 0. How to read this document

- Every number here comes from one of these sources, marked where it matters:
  - **[code]**: a constant or formula in the 2.0.39 sources (`src/shoal/*`, `src/Detents.hpp`, `src/widgets.hpp`), cited as file:line where useful.
  - **[test]**: asserted or printed by the module's automated tests. I built them from the 2.0.39 tree with the repository's own Shoal test flags (`-O1 -g -fsanitize=address,undefined -fno-sanitize=alignment,shift-base -fno-sanitize-recover=all`) and ran them on an Apple M-series Mac. All 16 test programs passed, `make shoal-globals` passed, and 50 more seeds of the random fault sweep (`make shoal-fuzz` equivalent, 48 s) passed. I also built `test_host` and `test_instances` under ThreadSanitizer in a scratch copy and ran each three times: no report.
  - **[meas]**: a short program I wrote against the Rack-free core (`shoal::Core`, `shoal::Host`, `shoal::TickClock`). Built with the firmware flags `-std=c++17 -fwrapv -fno-strict-aliasing -fno-unsafe-math-optimizations -funsigned-char -O3 -march=armv8-a+fp+simd` (the plugin's `-O3 -march` plus the firmware's own float rule, `tests.mk` line 55). Programs are listed at the end of the document.
- **[issue #N]** is an open GitHub issue in `ryanarneson/RkaModules`. Only #51 is about Shoal; #64 and #67 mention it (section 11).
- **[unverified]** marks anything I could not check, mostly Rack host behaviour and everything on Windows. Nothing here was run inside Rack, and nothing was run on Windows.
- The user's listening guide `shoal-2.0.39-listening.html` was read as a cross-check. It is not a source. It does not disagree with the code (section 11.3).
- Voltages are Rack volts. A "tick" is one 60 µs firmware tick. "Noon" does not apply: Shoal has no knobs.
- "The firmware" is the vendored o_C v1.3.6 code in `src/shoal/firmware/`. Its changes from upstream are in `src/shoal/firmware/CHANGES.md`; section 10.4 lists the ones that change behaviour.

---

## 1. What Shoal is

Shoal is a 28 HP emulation of the Ornament & Crime module: four trigger inputs, four CV inputs, four CV outputs, two encoders, three buttons, a 128 × 64 screen and 13 switchable apps. It runs the real o_C v1.3.6 firmware behind a fake-hardware layer (`src/shoal/shim/`, `CoreImpl.inc`), so the o_C manual applies as written (spec `docs/specs/2026-11-14-shoal-design.md`). On top of the hardware's own menu it adds **friendly mode**: a mouse-driven list of the current app's settings.

Design goals that QA can check:

- All 13 apps run as the o_C manual says: CopierMaschine, Harrington 1200, Automatonnetz, Quantermain, Meta-Q, Quadraturia, Low-rents, Piqued, Sequins, Dialectic Ping Pong, Viznutcracker sweet, Acid Curds, References.
- Two UI modes on one panel latch. **Friendly mode is the default** (spec revision of 2026-11-15; `configSwitch(..., 1.f, ...)`, Shoal.cpp:96). Hardware mode shows the firmware's own menu.
- Several Shoals in one patch, each independent, also on different engine threads.
- Everything saves with the Rack patch, with no save gesture.
- Never crashes Rack, on any app, with any input.
- Under 2 % of a core per instance at 48 kHz on Apple Silicon. Measured 0.10 to 0.24 % [meas] (section 10.1).
- The voltage ranges are the hardware's, not Rack's standard ±5 V and 0 to 10 V (deliberate, spec "Voltages").

Non-goals (do not file these as defects): calibration mode, the splash screen, polyphony (a polyphonic cable's first channel is used), Rack parameters for firmware settings (no MIDI-map of settings), alternative firmwares, mouse editors for the sub-editors (user-scale notes, Sequins steps, Acid Curds chords, Automatonnetz cells, References' Autotune), exporting settings to a real module.

### 1.1 Where the spec, the manual and the shipped code differ

The code is what ships. The spec is `docs/specs/2026-11-14-shoal-design.md` (design), `...-shoal-scope-design.md` (scope) and `...-shoal-app-survey.md` (friendly-mode survey). The manual is `docs/shoal/README.md`.

| Topic | Spec / manual says | 2.0.39 does |
|---|---|---|
| Right-click menu | Spec: App, UI mode, Screen colour, CV input range, Reset all apps, Encoder acceleration. Manual "Not there yet": the ±5 V range, encoder acceleration, the app choice | `Shoal.cpp` has no `appendContextMenu`: only Rack's own module menu. The app is chosen from the list's header (friendly) or the firmware's app menu (hardware). Reset is Rack's Initialize. The screen colour is fixed (`RkaTheme::SIGNAL`, cyan); the manual does not list that as missing |
| CV input range ±5 V | Spec: a menu option, saved in the patch. Manual: saved already, option to come | Engine, host event and saved key `"inputRange"` all work; **no control reaches it**. Only a hand-edited patch (`"inputRange": 1`) switches it on [test: test_io `testInputRange`, test_patch `testInputRange`] |
| Encoder acceleration | Spec: a menu option. Manual: starts on | The host event `SetAcceleration` exists and nothing posts it. Acceleration toggles only through the firmware's app menu (UP button) in hardware mode [code: CHANGES.md] |
| Output lights | Spec: "bipolar lights". Manual: brighten with the size of the voltage, either sign | One light per output at `abs(V) / 5` (Shoal.cpp:175). The second light id of each pair is unused and has no tooltip. The manual is right |
| UI MODE light | Spec: "latch with a light" | The latch cap lights itself from the param (`CapLatch`, `litWithoutModule`); no light id |
| References meters | Spec: frequency and BPM meters in scope, TR4 period "measured at Rack's sample rate". Stage 4 lists "the References meters" | Not built. The FreqMeasure stand-in never has a reading [code: CHANGES.md]. The manual says so. Autotune has nothing to measure |
| Piqued segments | Spec non-goal: "Piqued segment editing" | Piqued's segment values (Attack, Decay, Sustain/Level, Release by type) are list rows, edited through the envelope page [test: test_friendly `testPiquedSegments`] |
| Stage 4 | Spec: "stage 4 is next" (right-click menu, meters, manual) | Only the manual exists, and it says "A fuller manual comes later" |
| Look | Spec: "Nightglow look" | The panel is the "Instrument" look (CLAUDE.md). The spec predates the rename |
| Main loop rate | Spec: "UI event dispatch and `loop()` at about 1 kHz" | One main pass every 17 ticks = 1.02 ms (980.4 Hz); the UI poll averages 1.000 ms (CoreImpl.inc:238, 242) |
| Frame rate | Spec: "about 60 Hz" | A frame draw every 278 ticks = 16.68 ms = 59.95 Hz (Host.hpp:40) |
| Save rate | Spec: "twice a second" | The saved image is refreshed every 8,333 ticks = 0.49997 s (Host.hpp:41). A patch saved within 0.5 s of an edit does not have the edit [meas] (section 3.2) |
| Output range | Spec and manual: −3.75 V to +6.25 V | −3.7463 V to +6.2537 V (DAC codes 0 and 65,535; `shim/Volts.hpp:11`) |
| CV input range | Spec and manual: −3.33 V to +6.67 V | −3.332 V to +6.665 V (ADC codes 4095 and 0, offset 2730); read back as −3.330 V and +6.665 V [meas] |
| Quadraturia output | Manual: "about −3.75 V to +5.2 V, centred near +0.75 V" | −3.707 V to +5.280 V, centre +0.79 V, at the defaults over 30 s [meas] |
| Low-rents output | Manual: x and y "swing either side of about +1.25 V" | A (x): −0.94 to +3.82 V (centre +1.44 V). B (y): −1.72 to +4.97 V (centre +1.62 V). 120 s at the defaults [meas] |
| Piqued output | Manual: "0 V at rest up to +5 V at full Amplitude" | Peak 4.961 V at Amplitude 127 (127/128 of 5 V). Inverted, it rests at 5.000 V [meas] |
| Ping Pong output | Manual: "0 V at rest, rising as far as +6.25 V" | Default peak 4.941 V. 6.254 V only at Amplitude 255 and Velocity 255 [meas] |
| Meta-Q and Sequins gate | Manual: "about +6 V, 25 ms" | 6.000 V, 24.54 ms (409 ticks) [meas] |
| Long click | Manual: "a long press is a long click" | The long-press event fires **when the button is released**, after a hold of 1.000 s or more. At 0.99 s it is a short press [meas; code: OC_ui.cpp:95] |
| References "Mod range oct" | Manual: negative values are lost on save | −2 restores as +3, because the stored byte 254 is clamped to the maximum 3. +2 survives [meas] |
| Initialize | Manual: returns the firmware to defaults | Also keeps the input range (Host::requestReset, Host.hpp:194). A restore sets it. Neither document says so; moot while no control sets it |
| Hardware-mode scope | Scope spec: "exactly as today", no dividers | True. Both frames are drawn every 278 ticks whichever mode shows (issue #51) |

---

## 2. Panel, controls and ports

### 2.1 Layout

28 HP, 142.24 mm × 128.5 mm (`res/shoal/Shoal.svg`). A dark-panel variant `Shoal-dark.svg` is chosen by Rack's preference. Positions in mm from `ShoalLayout.hpp` [code, generated by `tools/gen_panels.py`]:

| Element | Centre (x, y) or rectangle |
|---|---|
| Output strip (four voltages) | rectangle at 4.0, 19.2; 83.2 × 5.0 |
| Upper screen (the scope) | rectangle at 4.0, 25.5; 83.2 × 41.6 |
| Lower screen (the editor) | rectangle at 4.0, 71.0; 83.2 × 41.6 |
| ENCODER L / R (dials, 14.5 mm) | 103.0, 33.0 / 126.5, 43.0 |
| PUSH L / PUSH R | 93.54, 46.52 / 130.77, 58.94 |
| UP / DOWN | 107.0, 56.0 / 107.0, 64.6 |
| UI MODE | 121.5, 65.0 |
| TR1 to TR4 jacks | x 100.0, y 80.0 / 90.4 / 100.8 / 111.2 |
| TR lights | x 105.8, same y |
| CV1 to CV4 jacks | x 111.6, same y |
| Output lights A to D | x 125.6, same y |
| OUT A to D jacks | x 131.4, same y |
| Screws | 134.62, 2.54; 7.62, 125.96; 134.62, 125.96 |

Each screen is 128 × 64 firmware pixels over 83.2 × 41.6 mm: 0.65 mm per pixel, 2:1. The module has no knobs, no attenuverters and no CV-to-control lines.

### 2.2 Parameter, input, output and light ids

Ids follow the enum order in `Shoal.cpp` (lines 45 to 83). There is **no `static_assert` pinning them** (Fog has one). They must never change number: saved patches reference them.

| Id | Param | Range | Default | Tooltip |
|---|---|---|---|---|
| 0 | `ENC_L_PUSH_PARAM` | button 0..1 | 0 | "Left encoder push (shift-click to hold)" |
| 1 | `ENC_R_PUSH_PARAM` | button 0..1 | 0 | "Right encoder push (shift-click to hold)" |
| 2 | `UP_PARAM` | button 0..1 | 0 | "Up (shift-click to hold)" |
| 3 | `DOWN_PARAM` | button 0..1 | 0 | "Down (shift-click to hold)" |
| 4 | `UI_MODE_PARAM` | switch 0..1 | **1** (friendly) | "UI mode": 0 "Hardware (the firmware's menu)", 1 "Friendly (the mouse list)" |

All five have `randomizeEnabled = false` (Rack's `configButton`; explicit for UI mode), and `onRandomize` is empty: **Randomize does nothing** [code]. The encoders are not params: nothing about them is saved or automatable.

| Id | Input | | Id | Output |
|---|---|---|---|---|
| 0..3 | "Trigger 1".."Trigger 4" | | 0..3 | "A".."D" |
| 4..7 | "CV 1".."CV 4" | | | |

| Light id | Meaning |
|---|---|
| 0..3 | "Trigger 1".."Trigger 4" |
| 4, 6, 8, 10 | "Output A".."Output D" (first of each pair) |
| 5, 7, 9, 11 | unused, no tooltip |

### 2.3 The jacks

[code, `Shoal::process`, Shoal.cpp:158; `Host::setCv`, `setGate`; `shim/Volts.hpp`]

**Reading.** Each engine sample, for each channel: `inputs[CV1+ch].getVoltage(0)` and `inputs[TR1+ch].getVoltage(0)`. An unpatched jack reads 0 V. **A polyphonic cable gives channel 1 only.** NaN reads as 0 V, in the host and again in the core; ±infinity is clamped by the converter and the Schmitt trigger [test: test_io `testHostileVolts`].

**CV inputs.** Voltage to ADC code: `code = offset − V × 409.6`, rounded, clamped to 0..4095 (`shim/Volts.hpp:20`). One code is 2.44 mV.

| Input range | Offset | Reads from | To | Measured pitch reading [meas] |
|---|---|---|---|---|
| Hardware (default) | 2730 | −3.332 V | +6.665 V | −3.330 V and +6.665 V clamped; 0 V reads +0.002 V; +1 V reads +1.003; +5 V reads +5.002 |
| ±5 V (patch key only) | 2048 | −4.998 V | +5.000 V | −4.995 V and +5.000 V clamped |

Pitch is 1 V per octave in both: 1,536 firmware units per octave, 128 per semitone. The firmware reads each channel's smoothed code, not the raw one (section 6.2).

**Trigger inputs.** A Schmitt trigger: high above 1 V, low below 0.1 V (`shim/Volts.hpp:30`). Two copies exist: the host's, which drives the TR light, and the core's, which makes the firmware's "clocked" edge and its level (`read_immediate`).

- A rising edge is **latched until the next tick**, so a pulse shorter than a tick still clocks. Several edges inside one tick are one clock. A one-sample pulse clocked CopierMaschine at 48, 192 and 768 kHz (1.30 µs at 768 kHz) [meas; test: test_io `testShortPulseLatched`].
- The level is read at the tick. Apps that read a trigger input's level (for example CopierMaschine's TR2 to TR4 at each clock) see a pulse shorter than 60 µs as low.
- The TR light follows the host's copy: immediate rise, fall with lambda 30 per second (about 33 ms) through `setBrightnessSmooth` (Shoal.cpp:177). A 1-sample pulse lights it.

**Outputs.** Each tick the core takes the firmware's DAC code, and `out(ch)` returns `(code − 24551) / 6553.6` V (`shim/Volts.hpp:11`). One code is 152.6 µV. The value **holds for the whole tick**: the output is a staircase of 60 µs steps, as the hardware's DAC is. Range −3.7463 V (code 0) to +6.2537 V (code 65,535). 0 V is code 24,551. `Shoal::process` writes it with `setVoltage`.

- **A new module, Initialize and a patch load start the outputs at 0 V**, not at the hardware's +6.2 V (user's decision 2026-11-15, CHANGES.md "Deliberate deviations") [test: test_state `testOutputsZeroAfterResetAndRestore`, test_io `testOutputsStartAtZeroVolts`]. CopierMaschine writes its outputs only when clocked, so it stays at 0 V until the first clock.
- **Output light**: `setBrightness(|V| / 5)`; full at ±5 V (Shoal.cpp:175).
- **The output strip** (`OutputStrip`, Shoal.cpp) draws `+1.83V`-style text, `%+.2f`, from the Rack output ports, four cells of one quarter width, in the Share Tech Mono font loaded from Rack's `res/fonts/ShareTechMono-Regular.ttf`. |V| under 5 mV prints as `+0.00V`. If the font does not load, the strip, the lane letters and the list text draw nothing; the code tests for it. Whether that font is in every Rack install is [unverified]. With no module (browser) the strip shows 0.00 V.

### 2.4 Controls

- **ENCODER L and R**: `KnurlEncoder`, endless. Vertical drag: up is clockwise, **8 px per detent**, or **32 px with exactly Ctrl** held (Cmd on a Mac). Ctrl with another modifier (Shift, Alt) gives plain speed (Detents.hpp:9, widgets.hpp:118). The wheel turns it one detent per notch **only when Rack's "scroll wheel knob control" (`settings::knobScroll`) is on**; otherwise it is not consumed and the rack scrolls. A notch is 10 scroll units on a Mac and 50 elsewhere. The knurl draws 1/24 turn per detent. Details of the turn path are in section 4.3.
- **PUSH L, PUSH R, UP, DOWN**: momentary `CapButton`. Click: down while the mouse is held. **Shift-click latches it down** until the next click of either kind. A button latched when a patch was saved is released on load and on Initialize (`releaseButtons`, Shoal.cpp:151). The host passes only changes to the core (`Host::setButton`), and the firmware debounces them (section 6.4).
- **UI MODE**: `CapLatch`; each click flips the param between 0 and 1 and is undoable ("move switch"). Lit = friendly. A patch saved before the control existed has no value for it and gets the default, friendly [code: comment at Shoal.cpp:93].

### 2.5 The module's menu

Only Rack's own items: Initialize, Randomize (does nothing), Duplicate, Bypass, Delete, Preset. There are no Shoal presets. Rack's "Save preset" and "Load preset" use the same `dataToJson` and `dataFromJson` as a patch (section 3). **Bypass**: Shoal calls no `configBypass`, so Rack has no routes; `process()` is not called while bypassed, so the firmware stops and outputs are whatever Rack gives a bypassed module with no routes [unverified in a live Rack].

---

## 3. Saved state

### 3.1 The patch data

[code: Shoal.cpp:23 to 140, PatchState.hpp; test: test_patch, test_state]

```
"data": { "version": 1, "state": "<base64, 2048 bytes>", "inputRange": 0 }
```

| Key | Written | On load |
|---|---|---|
| `"version"` | integer 1 | Must be the **integer** 1. A real (`1.0`), a string or any other number: the whole state is rejected and the module loads defaults |
| `"state"` | base64 of the 2,048-byte firmware image (the o_C "EEPROM": global settings page, app data page, user scales, patterns, chords; the current app is inside) | Must be a string that decodes to **exactly 2,048 bytes**. Bad base64 (`string::fromBase64` throws, caught), a wrong length or a missing key: defaults |
| `"inputRange"` | 0 (Hardware) or 1 (±5 V) | Integer 1 means ±5 V. Anything else, a real, a missing key: Hardware |

Plus the five params (ids 0 to 4) that Rack saves itself. UI mode is **only** a param; the data has no key for it.

What the firmware then accepts: `Core::restore` takes an image **only if both** the global-settings page and the app-data page load (checksums). If either is damaged, the module boots a new core's defaults over an erased image, so no stale page can outrank the next save [test: test_state `testRestoreNeedsBothPages`, `testResetSavesAFreshImage`]. 64 random single-bit flips of a valid image: 6 still restored (the flips fell outside the checksummed pages) and in every case the restored settings equalled the original [test: test_state `testCorruptImage`; 100 random images, all-zero and all-0xFF images are rejected]. A forged image with a correct checksum and any bytes runs every app without a fault and every setting ends inside its table range [test: `testForgedImage`; `sanitize_global_settings` clamps user scales, patterns, chords].

Defaults on any rejection, a patch with no `"data"` object, or Initialize: **CopierMaschine** with its factory settings, user scales/patterns/chords at factory values, outputs 0 V, UI mode friendly (the param's default).

`testdata/GoldenStateV1.hpp` is an image saved on 2026-11-15 (Quantermain with changed settings and one user scale). It is committed once, never regenerated, and restored by `testGoldenImageV1`. It has not changed since it was added (`git log`). `static_assert`s in `OC_apps.ino` pin `GlobalSettings` at 684 bytes and `AppData` at 1,008 bytes with member offsets, for macOS arm64, Linux x86-64 and Windows x86-64 (MinGW) [code: CHANGES.md]. The decode in `Shoal.cpp` itself (Jansson and base64) is not run by any test; only the rules in `PatchState.hpp` are.

### 3.2 When the state is captured

The engine publishes a fresh image **every 8,333 ticks (0.49997 s)** (`kSaveTicks`, Host.hpp:41) into the snapshot that `dataToJson` copies. `dataToJson` does not ask the engine for anything. So an edit reaches a saved patch up to 0.5 s later. Measured, 40 trials at random phases, app change to changed snapshot: shortest 0.011 s, **longest 0.500 s** [meas]. QA consequence: save, duplicate, preset-save, Rack's undo history and closing Rack within 0.5 s of an edit can miss it. After a restore or reset the snapshot is the requested image at once (section 5). If the engine is not processing (a bypassed module), the snapshot does not advance.

### 3.3 What is saved and what is not

Saved: the current app and every app's settings; user scales, patterns and chords; UI mode and the five params; the input range key.
Not saved (reset to power-on): sequence and step positions, shift-register contents, buffers, envelopes in progress, the app menu being open, a sub-editor being open, the list's scroll and tab, encoder queues, held buttons, history behind the scope. A reloaded patch starts as a module that was power-cycled [test: test_state `testRunningStateResets`, `testSequencerPositionResets`; manual].
**Initialize** is `Module::onReset`: it resets the five params (so the mode goes back to friendly and buttons release) and `requestReset` builds a new core (section 5). It resets user scales, patterns and chords too [test: `testResetSavesAFreshImage`].

---

## 4. Signal flow and threads

### 4.1 Block diagram

```
RACK ENGINE THREAD (one of Rack's workers; a module may move between them)            RACK UI THREAD
 per sample: Shoal::process                                                            widgets, menus, undo, patch I/O
   CV1-4 ──getVoltage(0)──► Host::setCv ──► Core::setCv (NaN→0)                          encoders ──► Host::post(Turn)
   TR1-4 ──getVoltage(0)──► Host::setGate ─► Host Schmitt (TR light)                      list     ──► Host::postEdit
                                         └─► Core Schmitt ► edge latched                  app menu ──► Host::post(SelectApp)
   buttons (params) ──► Host::setButton (changes only) ──► Core::setButton                           │  SPSC ring, 64 slots
   Host::process:                                                                                   ▼  (events and edits, in order)
     ticks = TickClock::advance()   (0, 1 or more per sample)                      ┌── Host::requestRestore / requestReset ◄── dataFromJson / onReset
     if ticks: swap in a pending Core (try_lock), then apply queued events          │   builds + boots a NEW Core on the UI thread
     per tick: Core::tick ───────────────────────────────────────────┐              │
        apply latched inputs (ADC codes, gate pins, edge clocks)     │              │
        send DAC codes held since last tick to out()                 │ one          │
        shoal_tick: DAC update, ADC scan (every 3rd tick),           │ 60 µs        │
                    digital inputs, app ISR                          │ tick         │
        every 1 ms: UI poll (encoder pins, buttons, event queue)     │              │
        every 17 ticks: shoal_main_pass (event dispatch, app loop)   ┘              │
        Host::afterTick: record outputs (history rings)                              │
                         every 278 ticks: draw menu + scope frames, build model,     │
                           view, history ──► try_lock frameMutex_ ──► published ─────┼──► widgets: frames(), model(), screen()
                         every 8333 ticks: Core::save ──► try_lock stateMutex_ ──────┼──► dataToJson: snapshot()
   outputs: Host::out ──► setVoltage(A..D), lights, (strip reads the ports)          │
```

### 4.2 Threads and who owns what

[code: header of Host.hpp, Core.hpp]

| Thread | May call | Never does |
|---|---|---|
| Engine (Rack worker) | `Host::setSampleRate`, `setCv`, `setGate`, `setButton`, `process`, `out`, `gateHigh`; all of `Core` through them | Block, allocate, boot firmware, or wait for a lock: it **uses `try_lock`** and retries at the next tick |
| UI (any one non-engine thread) | `post`, `postEdit`, `frames`, `model`, `scope`, `screen`, `snapshot`, `requestRestore`, `requestReset`, `app`, `appMenuOpen` | Touch the running Core |

- **One Core, one thread at a time.** All non-static members of a `Core` must be serialised by the caller. Different Cores are independent and can run on different threads together (`make shoal-globals` fails if the firmware object holds any writable global; the one allowed thread-local is the "current context" pointer).
- **How firmware state is per instance.** Every former firmware global lives in one `shoal::Context` (about 32 KB per Core; `new Core` makes 3 allocations of 32,848 bytes in all [meas]). Each `Core` member that reaches firmware code sets a thread-local `current()` pointer with a `Scope` and restores it on exit. So a module moving from one Rack worker thread to another between samples is safe.
- **Three hand-offs**, each with its own lock or ring:

| Hand-off | Mechanism | Capacity and rules |
|---|---|---|
| UI to engine: control events and friendly edits | One fixed single-producer, single-consumer ring, acquire/release atomics | 64 slots (`kEventSlots`, Host.hpp:42). A full ring makes `post` and `postEdit` return false; the widget keeps the detents or the edit and offers it again every frame. Events keep posting order |
| Engine to UI: frames, model, view, history | `frameMutex_`. Engine `try_lock` every tick until it gets it; UI holds it only to copy | 59.95 Hz. `screen()` returns frame, view and history of **one** publish (final review fix, 2.0.15) |
| Engine to UI: saved image; UI to engine: replacement core | `stateMutex_` and an atomic flag | 2 Hz image. Replacement: section 5 |

- **A restore never runs on the engine thread**. The engine only swaps a pointer. The Core it retires is deleted on the UI thread (`snapshot` or the next `install`) or in `~Host`. [test: test_host `testNoEngineAllocation`: the engine path never allocates, frees or boots firmware, also while swapping in a restored or reset core.]
- **Order**: events posted before a `requestRestore` are applied **after** it, on the replacement Core, unless they name another app (an edit for the old app is dropped by the core) [test: `testEventsWaitForRestore`, `testEditAfterRestore`].
- **Rack calls** `dataToJson`, `dataFromJson` and `onReset` off the engine thread, concurrently with `process()` [unverified in a live Rack; this is what the code comment and the design assume]. `onSampleRateChange` calls `Host::setSampleRate`; whether Rack calls it when a module is added is [unverified]. If it did not, the clock would assume 48 kHz (`TickClock` default) and the module would run 8 % slow at 44.1 kHz (QA test in 11.2).
- **Tested threading**: `testThreads` and `testEditsFromUiThread` (a UI thread posting edits and reading models for 1 s against a running engine), `testHistoryNotTorn` (a reader as fast as it can sees whole histories), test_instances `testThreads` (two Cores on two threads). Under ThreadSanitizer (scratch build, 3 runs each of test_host and test_instances): no report [meas]. The repository's own `make test` runs no ThreadSanitizer for Shoal.

### 4.3 The turn path (hardware controls)

A turn from the widget: `Encoder::onTurn` → `Host::post(Turn)` → ring → `Core::turnEncoder` → the shim synthesises the encoder's quadrature pins → the firmware's own `Encoder::Poll` and **acceleration** → `Ui::DispatchEvents` → the app's handler. One detent is two UI polls (2 ms; measured 40 ticks = 2.4 ms from turn to the handler). The shim queues at most 512 pin samples = **256 detents per encoder per call**; the rest is dropped (`Pins.hpp:42`) [test: test_ui `testEncoderQueueBounded`]. If the host ring is full, the widget keeps up to 16 unsent detents (`TURN_BACKLOG_MAX`) and offers them every frame; a longer stall loses the rest (Detents.hpp:41).

Measured, `Core::encodersPending()` true for [meas]: 1 detent 2.4 ms; 8 detents 16.7 ms; 100 detents 200 ms; **256 detents 512 ms**; 257, 300 and 1,000 detents all 512 ms (the surplus is dropped).

---

## 5. Restore, reset and boot

[code: Host.hpp:185 to 205, 367 to 410; CoreImpl.inc `reboot`]

### 5.1 The paths

| Trigger | Call | Result |
|---|---|---|
| Module created (Rack, or the browser preview) | `Host(seed)`: `new Core`, seed it, save the defaults image | Seed is `random::u32()` per module, so two Shoals differ. The widget with no module builds its own preview Host and runs it 0.25 s (12,000 samples at 48 kHz) so the screens show CopierMaschine |
| Patch or preset load, duplicate, undo | `dataFromJson` → `requestRestore(image or null, range)` | New Core booted from the image. A rejected image boots **twice** (once to try, once to defaults) |
| Initialize | `onReset` → `host.post(Reset)` → `requestReset` | New Core from defaults. Keeps the engine's input range (`pendingKeepsRange_`) |
| Both | then `releaseButtons()` | The four button params set to 0 |

`requestRestore` and `requestReset` build and boot the replacement on the calling (UI) thread, publish the requested image as the snapshot at once, and set a pending flag. A second request before the engine picks up the first deletes the first.

### 5.2 The swap

On the first engine sample with a tick due after the request, `applyRestore` takes `stateMutex_` with `try_lock`. If the UI holds it, the engine **keeps running the old core** and the queued events wait too, then tries at the next sample. When it gets the lock it swaps the pointer, puts the old Core in `retired_`, and re-applies host state.

| Item | After a restore or reset |
|---|---|
| Input range | Restore: the one in the request. Reset: kept |
| CV levels | Re-applied from the host's last values |
| Held buttons | Pressed again on the new Core |
| Gates | **Not carried: low in the new core.** The next `setGate` with a high level counts as one rising edge (comment at Host.hpp:388) |
| Latched gate edges, queued encoder detents | Dropped |
| Outputs | 0 V until the app writes |
| ADC smoothing | **Starts at code 0**, so every CV input reads about +5 V at the first scan and settles to the true value in about 5 ms (section 6.2) |
| Random sources | Restart from the Host's original seed (a reset or a reload repeats the same random sequence) |
| `millis()`, ticks | Restart at 0 (the replacement is a new `Core`; the carry-over inside `Core::reboot` only matters when the Core API is used directly) |
| Encoder acceleration | What the restored image holds (on for defaults) |
| App menu, sub-editors | Closed |
| Scope history, published frames and model | Cleared and redrawn at the next tick (`frameCountdown_ = 1`) |
| An unpublished save | Dropped (it predates the swap) |
| Friendly edits for the old app | Dropped by the core |

Costs [meas, includes malloc]: `new Core` + delete 1.33 µs; `Core::restore` of a good image 2.42 µs; `requestRestore` 3.9 to 4.4 µs; `requestReset` 1.4 to 1.7 µs; a rejected image 3.8 to 4.6 µs; `new Host` + delete 5.5 to 6.4 µs. Restore is cheap enough that nothing here is a performance risk. After `requestRestore` the published app was the restored one within 0.04 ms [meas]; the snapshot equalled the requested image at once, with the requested range.

### 5.3 A gate that is high when the core boots

Found while preparing this document, not filed. A held gate on TR1 and CV1 = 1 V, CopierMaschine: after the module starts, and again after Initialize or a restore, the output settles at **+4.9998 V instead of +1 V**. The new core sees the high gate as a rising edge on its first tick and samples CV 1 before the input smoother has moved from code 0 (section 6.2). The value holds until the next clock. With the gate low through the reset the output stays 0 V [meas: m9_restore]. The same applies to any app that samples CV on a clock. On the hardware a pin that is already high at power-on makes no edge. QA test in 11.6.

---

## 6. Timing

### 6.1 The tick clock

[code: `TickClock`, Core.hpp:26; test: test_io `testTickCountExact`, test_host `testTickRate`]

The firmware runs on a fixed 60 µs core tick (16,666.67 Hz). `TickClock::advance()` adds `16,666.67 / sampleRate` to a fractional accumulator each Rack sample and returns the whole ticks due, 0, 1 or more. A rate of 0 or below, or NaN, uses 48,000 (Core.hpp:30). The accumulator carries across a rate change.

Measured, 60 s of samples [meas: m1_timing]:

| Rate (Hz) | Ticks in 60 s | Most ticks in one sample | Longest run of samples without a tick |
|---|---|---|---|
| 8,000 | 1,000,000 | 3 | 0 |
| 22,050 | 1,000,000 | 1 | 1 sample (45.4 µs) |
| 44,100 | 1,000,000 | 1 | 2 (45.4 µs) |
| 48,000 | 1,000,000 | 1 | 2 (41.7 µs) |
| 88,200 | 1,000,000 | 1 | 5 (56.7 µs) |
| 96,000 | 1,000,000 | 1 | 5 (52.1 µs) |
| 176,400 | 1,000,000 | 1 | 10 (56.7 µs) |
| 192,000 | 1,000,000 | 1 | 11 (57.3 µs) |
| 384,000 | 1,000,000 | 1 | 23 (59.9 µs) |
| 705,600 | 1,000,000 | 1 | 42 (59.5 µs) |
| 768,000 | **999,999** | 1 | 46 (59.9 µs) |

Tests pin 8, 44.1, 48 and 96 kHz and 768 kHz for the core (10 s within one tick of 166,666), 44.1 to 192 kHz through the Host, and a mid-run change 48 → 44.1 kHz [test]. Inputs and `out()` are only sampled at tick times; an output between ticks is the last tick's value.

### 6.2 The inputs

[code: OC_ADC.cpp:22 to 32, OC_ADC.h:29, 83 to 88]

CV is presented to the firmware every tick as a 12-bit code, but the firmware's ADC driver **takes a new sample on every third tick** (5.55 kHz, as the DMA delivered on the hardware) and smooths it with a one-quarter step: `smoothed = (smoothed × 3 + value) / 4` (8 fractional bits). The apps read the smoothed value. This is part of how the apps feel and is not a bug [test: test_io `testAdcCadence`].

Measured [meas: m1_timing], a step on CV 1 from a settled 0 V:

| Step | First change | Within 16 codes (39 mV) | Within 1 code (2.4 mV) |
|---|---|---|---|
| 0 → +2 V | tick 3 (0.18 ms) | tick 42 (2.52 ms) | tick 63 (3.78 ms) |
| 0 → +5 V | tick 3 | tick 51 (3.06 ms) | tick 75 (4.50 ms) |
| 0 → −3 V | tick 3 | tick 48 (2.88 ms) | tick 75 (4.50 ms) |

**Start-up transient.** The smoother starts at code 0, which is +6.67 V. On a new core the apps read, at tick 3, code 682 (about +5.0 V), then 1,194 at tick 6, 1,866 at tick 12, 2,244 at tick 18 and reach within 1 code of the 0 V code (2,730) at **tick 84 (5.04 ms)** [meas]. The hardware has the same transient at power-on. In Shoal it happens after every restore and reset (section 5.2).

### 6.3 Gates and outputs

[meas: m1_timing; CopierMaschine, whose clock is handled in the tick interrupt code; CV 1 at 1 V settled]: from `setGate(high)` the output changes **2 ticks (120 µs) later**. An app that acts on a trigger in its main pass (every 17 ticks) can take up to 1.02 ms more [code; not measured per app]. At 48 kHz the core sees the edge at the next tick, up to 2 samples later, so the total is about 120 to 160 µs plus Rack's own cable delay. Output codes are applied at the start of the tick after the app writes them, which is why it is 2 ticks.

CopierMaschine settled value for 1 V in: 0.99991 V (one ADC code and one DAC code of quantisation).

### 6.4 Buttons and long presses

[code: ui_button.h:34 to 54, OC_ui.cpp:80 to 100; meas: m6_btn, m7_btn2]

The firmware debounces each button with an 8-bit history updated every UI poll (1 ms): a press counts as `just_pressed` after **7 consecutive low polls (7 ms)**; the event is raised on **release**, when the history reads seven highs after a low. It is a short press if less than `kLongPressTicks` = 1,000 polls passed since the `just_pressed` time, otherwise a long press.

- **R held 0.99 s then released: short press (nothing, or toggles edit). Held 1.00 s or more then released: the app menu opens.** Nothing happens while the button is still down, so a long click opens the menu at mouse-up [meas].
- The app menu closes unchanged after **25 s** (`APP_SELECTION_TIMEOUT_MS`, OC_config.h:36) with no input: still open at 24 s, closed at 26 s [meas; test: test_ui `testAppMenuTimeout`].
- **A press shorter than 7 ms, made 1 s or more after the last registered press, is raised as a long press.** The stale press time makes `now − press_time` large. R: the app menu opens; the same code raises a long-press event for every button. Measured after 2 s of running: 0.5, 1, 3 and 6 ms presses opened the app menu; 7 ms and longer gave a normal short press. This is the firmware's own debounce code and the hardware does the same. In Rack it needs a click shorter than 7 ms (a MIDI-mapped or scripted button pulse); a mouse click is longer. Found while preparing this document; not filed.

### 6.5 Publishing and latency

| Item | Value | Source |
|---|---|---|
| Main-loop pass (event dispatch, app `loop()`) | every 17 ticks = 1.02 ms | [code] CoreImpl.inc:242 |
| UI poll (encoders, buttons) | every 1,000 µs of tick time (17, 17, 17, 16 ticks) | [code] CoreImpl.inc:238 |
| Frames, model, view, history published | every 278 ticks = 16.68 ms (59.95 Hz) | [code] Host.hpp:40 |
| Saved image published | every 8,333 ticks = 0.49997 s | [code] Host.hpp:41 |
| A friendly edit to the published model | 0.27 to 16.42 ms (40 trials) | [meas: m4_lat] |
| Snapshot lag after an app change | 0.011 to 0.500 s | [meas: m4_lat] |
| Scope history | 256 columns × 260 ticks (15.6 ms) = 3.994 s; X/Y 512 points × 130 ticks (7.8 ms) = 3.994 s | [code] ScopeView.hpp:52 |

The Host always draws both frames, builds the friendly model, the scope view and the history every 278 ticks, whichever mode or app is shown.

---

## 7. Hardware mode and friendly mode

### 7.1 One firmware, two views

Both modes are views of **one firmware state**. Switching changes nothing in the app. The UI MODE param only selects what the widget shows; `ShoalWidget::step` sets visibility from it every frame (so a click or an undo works while the engine is paused). Default: friendly.

| | Hardware mode (param 0) | Friendly mode (param 1, default) |
|---|---|---|
| Lower screen | The firmware's menu frame (`drawMenu`), 128 × 64 pixels, as `rka::PixelScreen` | `FriendlyList` over the same rectangle |
| Upper screen | The firmware's scope frame exactly: no dividers, letters or smooth views | The scope frame with lane dividers and letters, or Shoal's own smooth view for six apps (section 8) |
| What drives the app | Encoders and buttons through the shim: firmware polling, debounce, **acceleration**, event queue, dispatch | The same controls still work. List edits go to the app's handlers directly: **no debounce, no acceleration** |
| App choice | Long-press R (release after ≥ 1 s), turn R to the app, press R | Click the app name in the header |
| Sub-editors | Reached here | Rows say "edit in hardware mode"; clicking one moves the firmware cursor to the row and switches the latch to hardware (undoable) |
| Both frames drawn? | Yes, every 278 ticks | Yes |

While the list is hidden (hardware mode), `ShoalWidget::step` still calls `friendlyList->flush()` so an edit the host refused just before the switch is offered again.

### 7.2 Friendly mode: the model and the edits

[code: Friendly.hpp, Friendly.inc, FriendlyList.hpp, ListLayout.hpp]

`Core::friendlyModel` builds a `FriendlyModel` on the engine thread from the app's own tables: up to **72 rows** (`kMaxRows`; Sequins' largest list is 46), **4 tabs**, a status string of 23 characters, row text of 11. Every pointer in it points to static storage, so the UI can copy it. Rows are of seven kinds: Number (slider), Choice (dropdown), Toggle (on/off), Mask (one cell per note), Action (a button), SubEditor ("edit in hardware mode") and Heading. A row carries flags: cursor, editing, extra (an encoder-only function the firmware's menu does not list), confirm (the list asks first).

An edit (`FriendlyEdit`: SetValue, ToggleBit, Press, SelectTab, Focus) is **not** a write to the setting. The adapter puts the firmware's menu cursor on the row, then sends the app's own handlers the events a person would: R push to enter edit mode, the R encoder turned by (target − current), R push to leave. So the app's guards and follow-ups run (Quantermain's trigger/source rule, Piqued's Euclidean bounds, list rebuilds). Neither `apply_value` nor `Core::setSettingRaw` is used. If a step makes another setting come under the cursor (the list was rebuilt), the steps stop, edit mode is left, and the rest goes to the setting's new row. Only a note-mask bit toggled from outside its sub-editor calls the app's functions directly.

The core **drops** an edit (returns false) when: it names another app than the current one; the app is not on its settings menu (the app menu or a sub-editor is open: `kModelBusy`); its tab does not exist or the app would not select it; its row is not in that tab's list now; the row does not take that type; or the adapter's cursor cannot reach the row. The dropped edit is simply gone. The next model shows the result of a taken edit, which the app may still have clamped or refused.

**Ordering.** Control events and edits share the ring in posting order. The host holds an edit (and everything behind it) while any earlier encoder turn has not reached the app (`Core::encodersPending`: queued detents, or an event in the firmware's UI queue). A long fast spin followed by an edit therefore applies the edit up to about 0.5 s later, in order [test: `testEditOrder`; manual "Edits during a fast encoder spin"].

**The list** (UI thread only; `FriendlyList.hpp`, `ListLayout.hpp`):

- Layout: a header (14 % of the height), a banner while busy, a tab bar when the app has more than one channel, then up to 8 lines in all; a scrollbar (3 % of the width, hit area at least 12 px) when the rows do not fit. Control width 56 % of a row (20 % for an on/off switch).
- A slider drag moves the value **from where it is**, bar width = whole range; Ctrl gives 1/4 speed, Ctrl+Shift 1/16; double-click sets the default; the bar holds the drag's target for up to 0.25 s so it does not flick back. A mask cell click toggles; press and drag paints.
- The wheel scrolls **2 rows per notch**, only over a list that overflows; otherwise the rack scrolls.
- Edits that ask first (`kRowConfirm`): Harrington 1200 "Reset all settings", Automatonnetz "Clear grid", Meta-Q "Copy scale to all slots, both channels", Sequins and Acid Curds "Clear CV mapping". A small menu with the row's name; nothing happens unless "Confirm" is clicked [test: test_friendly `testConfirmRows`, test_list `testAsksFirst`].
- The list follows the firmware's cursor: the row under it is tinted with a bar on its left edge (cyan; brass while the firmware is editing). Clicking a row's label moves the cursor without changing anything. The list scrolls to keep the cursor's row in view.

### 7.3 The adapters

One adapter per app in `kAdapters[]` (Friendly.inc, in `available_apps` order; `static_assert` of 13). `testEveryAppHasAnAdapter` and `CoreTest::friendlyHasAdapter` confirm every app has one that takes edits. They are in four files:

| File | Lines | Apps |
|---|---|---|
| FriendlyAdapters1.inc | 109 | Quadraturia, Low-rents |
| FriendlyAdapters2.inc | 291 | Dialectic Ping Pong, Viznutcracker sweet, References, Harrington 1200, Automatonnetz |
| FriendlyAdapters3.inc | 381 | CopierMaschine, Quantermain, Meta-Q |
| FriendlyAdapters4.inc | 660 | Piqued, Sequins, Acid Curds |

An adapter is a struct of five optional functions and two tables: `header` (tabs, tab labels, status text), `rows(tab)`, `selectTab`, `edit`, `accepting` (false while a sub-editor has the controls: the model is then busy and every edit is dropped), `nameCounts` (name arrays shorter than their range) and `labels` (labels replacing the table's one- or two-letter or arrow names). Rows follow the firmware's own settings tables and enabled-settings lists, so the list tracks the menu; an adapter adds the extra rows for the things the hardware does with the left encoder, buttons and long presses. Eight apps have `accepting` (they have a sub-editor: CopierMaschine, Automatonnetz, Quantermain, Meta-Q, Piqued, Sequins, Acid Curds, References); five do not (Harrington 1200, Quadraturia, Low-rents, Dialectic Ping Pong, Viznutcracker sweet) [code]. Where a tab switch is the app's channel control, `selectTab` sends the events of that control, so selecting a tab selects the channel in the firmware too.

Rules (header of Friendly.inc): no allocation, no locks, no libc formatting (a fixed-array `Text` writer does the numbers), no writable globals. Five stand-in adapters in `Friendly.inc` (namespace `fake`) reach the drops no real adapter shows, for the tests.

The equivalence tests (`test_friendly.cpp`, 439 CHECK sites) drive a second core with encoder and button events only, from the same seed, and compare every setting, the model and the outputs after thousands of ticks (TESTING.md rules 1 and 2).

### 7.4 What stays in hardware mode

Each of these has a "hardware" row, reached from the list [manual; the rows exist in the model]: user scale notes (CopierMaschine, Quantermain, Meta-Q, Sequins, Acid Curds: through the mask row), Sequins steps, Acid Curds chords, Automatonnetz cells, References' Autotune. While one of these (or the app menu) is open, the model is busy: a brass banner "HARDWARE EDITOR OPEN", the list dimmed and taking no edits. One side effect inherent to the recovery path: a panel button held at the moment a friendly edit has to recover the app's menu loses its press (spec "Left open by stage 3").

---

## 8. The scope (upper screen)

[code: ScopeView.hpp, ScopeView.inc, ScopeLayout.hpp, ScopeScreen.hpp, Host.hpp history; spec 2026-11-19]

In **hardware mode** the upper screen is the firmware's scope frame (the o_C screensaver view) exactly. In **friendly mode** it is drawn from a `ScopeView` that `Core::scopeView` fills, and the history `Host` records.

| App | Kind | Lanes (and letters) | Notes |
|---|---|---|---|
| Quadraturia | Traces | four rows, A at top | Lane −3.75 V (bottom) to +6.25 V (top) |
| Piqued | Traces | four rows | Lane 0 V to +5 V. The firmware's envelope-shape preview is not drawn |
| Dialectic Ping Pong | Traces | four rows | Lane 0 V to +6.25 V |
| Low-rents | X/Y | two halves, "A × B" and "C × D" | Higher voltage right and up; the firmware plots right to left, so it is mirrored against a unit |
| Automatonnetz | Grid and circle | none | Last four cells of the 5 × 5 grid, current cell a disc; note circle (C at top, clockwise by semitone) with the chord's three notes |
| Harrington 1200 | Circle and text | none | Note circle; last four moves (+ or − and P, L, R, N, S, H; `*` a reset); the chord's notes with octaves |
| CopierMaschine, Quantermain | Pixels | four columns A to D | Firmware pixels with dividers and letters |
| References | Pixels | four columns over rows 0 to 43 | Letters sit at the foot of the columns, above the text at rows 44 and 56 |
| Meta-Q, Sequins | Pixels | two halves "A", "B" | Only the main pitch outputs A and B are drawn by the firmware |
| Viznutcracker sweet | Pixels | four bands | Letters have a glass plate behind them |
| Acid Curds | Pixels | none | |

- **History** (engine thread, fixed arrays, no allocation): per output, 256 columns each holding the lowest and highest output over 260 ticks (15.6 ms), so a 1 ms trigger still shows as a spike; and a second ring of 512 plain samples, one per 130 ticks, for X/Y. The visible window is 3.994 s, fixed. Fixed lane scales, no zoom or time base (a non-goal). A trace beyond its lane's range is clipped at the lane's edge.
- Published under the same lock as the frame, so the lanes and letters always match the pixels shown. Changing app, Initialize and a patch load clear the history; nothing from the old app is published after [test: test_host `testHistoryClearedOnChange`]. The Host checks `view.app` against the ring's app on each frame and clears on a change, however it came.
- The history is unrolled (about 4,000 floats) and copied (16,388 bytes) under the lock **every frame, in every app and in both modes** [issue #51]. Cost is small (section 10.1).
- Drawing never changes outputs or firmware state: the menu's scale-editor tick counter and the autotuner status moved from `Draw()` to `Redrawn()` in the firmware so behaviour does not depend on drawing [code: CHANGES.md; test: test_ui `testFramesIndependent`, `testMetaQSlotSwitchWithoutDrawing`, `testAutotunerErrorWithoutDrawing`].
- Colours: `RkaTheme::SIGNAL` (cyan), dividers at about 40 % and letters at about 65 %. Each lane's trace is one smooth line through the columns, a min-max band where they differ by more than a line width. Smoke screenshots and the module browser show a canned preview (CopierMaschine after 0.25 s), not a smooth view.

---

## 9. The apps

All 13 have a friendly adapter [test: `testEveryAppHasAnAdapter`]. Row counts are from the model of a new core, in friendly mode, measured with `dump_models` [meas]; "extras" are the rows that are not firmware menu rows (encoder-only functions, headings, sub-editor rows). Defaults are the firmware's. "Output" figures are at the defaults, no inputs, unless stated.

| App | Friendly adapter | Tabs and rows | Notable settings | Known limits |
|---|---|---|---|---|
| CopierMaschine | Yes. Sub-editor: user-scale notes | 1 tab, 12 rows (2 extras: "Hold / Freeze", "Delay type") | Scale (default Semitone), root, mask, octave, buf.index 0 to 63, hold (buflen) 4 to 63, input gain 0 to 39 (19 = 1.00), CV source (CV1, TM, ByteB, IntSq), CV4 dest. | Outputs stay 0 V until the first clock [test]. Scale is previewed on the left encoder and committed by L push on the hardware; the list's Scale row does this through the app's handlers [test: `testCopierScaleCommit`]. "> BB P0" and "> BB P1" do nothing (firmware slip, manual) [unverified]. Note-mask edits in hardware mode |
| Harrington 1200 | Yes. No sub-editor | 1 tab, 16 rows (1 extra: "Reset all settings", asks first); 38 rows with Trigger type Eucl | Transpose −11 to 11, Octave, Root mode, Inversion, PLR and NSH priority, CV sampling, Output mode, Trigger delay, Trigger type (PLR, NSH, Eucl), and the Euclidean rows when Trigger type is Eucl | **The Euclidean rows stay hidden after a patch saved with Trigger type Eucl loads**: 38 rows before the restore, 16 after, setting still 2 [meas]; change Trigger type to bring them back (firmware, as the hardware). Smooth scope view |
| Automatonnetz | Yes. Sub-editor: cells | 1 tab, 11 rows (4 extras: "Reset", "Clock", "Clear grid" (asks), "Cells") | Step X, Step Y (0 to 39), Mode, Oct, TrDly, OutA (root, trig, arp, strm), Clr | OutA trig is 5.000 V for 0.54 ms (9 ticks) [meas]. Cell contents (25 cells × 4 settings) are reachable in hardware mode only. Smooth grid and circle |
| Quantermain | Yes. Sub-editor: user-scale notes | 4 tabs A to D, 12 rows each (2 extras: "Copy scale to other channels", "Reset mask") | Scale, Root, Active notes (mask), CV Source 0 to 7, Trigger source (TR1 to TR4, cnt+, cnt-), Clock div, Octave, Transpose, Fine ±999 | After "Copy scale to other channels" a channel that had no scale shows its Root and mask rows only after one of its settings is edited (firmware; list stale) [test: `testStaleListRebuild`]. On the default Semitone scale the output is the nearest semitone, but switches to the next note only 5 to 10 mV past the midpoint (hysteresis; ascending 5 mV steps, 143 of 1,800 points one semitone off, all inside such a band); mean deviation 12 mV, from −3.0 V to +6.04 V. Below −3.05 V it holds −3.0 V and above +6.05 V it holds +6.04 V [meas: m3b] |
| Meta-Q | Yes. Sub-editor: user-scale notes | 2 tabs A, B, 15 rows each (2 extras: "Copy scale to all slots, both channels" (asks), "Reset mask") | scale, scale #, seq_mode, Active notes, root #1, transpose #1, octave, CV source (0 to 4, 4 = Turing), CV aux., aux.output (gate, copy, asr), --> pw (25) | Gate output 6.000 V, 24.54 ms [meas]. The "Active notes" row belongs to the slot chosen by "scale #", not the slot playing (manual). Two firmware guards always pass (an OR where an AND was meant; app survey) |
| Quadraturia | Yes. No sub-editor | 1 tab, 22 rows (1 extra: "Phase reset") | Coarse (64), Fine, Tap tempo, Shape, Phase/frq sprd, Output range (230), Offset, Freq range, B/C/D freq ratio, XOR, AM, CV4 dest., TR4 mult | Output −3.707 to +5.280 V at the defaults; A's period 7.28 s (0.137 Hz; title bar says 0.13 Hz) [meas]. Coarse and Fine do nothing while Tap tempo is on (firmware). The scope trace lane is the whole output range |
| Low-rents | Yes. No sub-editor | 1 tab, 10 rows, no extras | Freq 1 and 2 (128), Rho/c 1 and 2 (63, range 4 to 127), LFreq 1 and 2 Rng, Out A to D (22 choices) | Output swing at defaults: see section 1.1. Out A to D x, y, z and sums. Smooth X/Y view, mirrored against a unit. The firmware's scope history is shared: after Low-rents, Quadraturia's, Piqued's and Ping Pong's C and D traces asked for rows past the 64-row frame for the first 64 updates; the write is dropped (firmware deviation) [test: `testScopeAfterVectorscope`] |
| Piqued | Yes. Sub-editor: segment editor (segment values are list rows) | 4 tabs, 24 rows each at the default type AD, no extras (TYPE and other settings show and hide rows) | TYPE (11 types, AD default), Attack, Decay (128), Trigger input (TR1 to TR4, other channels' end of cycle), Tr delay, Eucl length (Off to 31), shapes, multipliers, CV1 to CV4 targets, Amplitude (127), Max loops, Inverted | Peak 4.961 V; inverted rests at 5.000 V [meas]. Default AD: first output after 52 ms, peak 4.961 V at 0.400 s, under 10 % at 0.614 s after a 10 ms trigger [meas: m8_ref]. A negative CV mapped to Euclidean length reaches 0; the firmware clamps (deviation, test `testPiquedEuclideanLengthZero`). Scope drops the shape preview |
| Sequins | Yes. Sub-editors: step pitches and lengths, user-scale notes | 2 tabs, 29 rows each, 2 headings ("Parameters", "CV mapping"), 6 extras ("Sync", "Copy scale to other channel", "Wait for end of sequence", "Clear CV mapping" (asks), the headings) | scale, Active notes, sequence #, Steps (mask, 16 cells), playmode (16), direction, mult/div, octave, root, aux. mode (gate, copy, AD, ADR, ADSR), --> pw, reset/mute, clock src, CV mapping rows | **A new module plays one note**: every step is at the root, so the pitches must be edited in hardware mode first. Gate 6.000 V, 24.54 ms [meas]. Switching the aux envelope from ADSR or ADR to AD after it finished left a stale segment; the firmware now takes it as finished (deviation) [test: `testEnvelopeTypeShrinks`] |
| Dialectic Ping Pong | Yes. No sub-editor | 4 tabs, 11 rows each | Gravity (128), Bounce loss (96), Amplitude (0), Velocity (228), Trigger input, Retrigger, CV1 to CV4 targets, Hard reset | Default peak 4.941 V; 6.254 V at Amplitude 255 and Velocity 255 [meas]. Trace lane 0 to +6.25 V |
| Viznutcracker sweet | Yes. No sub-editor | 4 tabs, 13 rows each | Equation (16, "hope" first), Speed 255, Pitch, Parameter 0 to 2, Loop mode, Trigger input, Step mode, CV1 to CV4 targets | Output −3.746 to +4.961 V at defaults; runs without a trigger [meas]. It is stepped at 16.67 kHz as on the hardware, so audio use is a staircase with images; no band-limiting (aliasing is out of scope, issue #67). Equation 15 and ten others divide by values that can be zero; guarded (`bb_div`, `bb_mod`, `bb_shr`) to the Teensy's results [test: `testBytebeatEquation15`] |
| Acid Curds | Yes. Sub-editors: chords and progressions, user-scale notes | 1 tab, 25 rows (3 extras: 2 headings, "Clear CV mapping" (asks)), "Chords" is a sub-editor row | scale (Semitone), Active notes, root, progression, Chords, playmode (15), direction, transpose, octave, CV source, chords trg src (TR2), TR1 delay, CV mapping rows | **On a new module each progression holds one chord**, so TRIG 2 and 4 do nothing until chords are added in hardware mode. A chord slot past the 32-entry user table plays chord 0 and edits there are dropped (deviation) [test: `testChordEditorPastTable`] |
| References | Yes. Sub-editor: Autotune | 4 tabs; A to C 5 rows, D 9 rows (adds the meter rows) | Octave −3 to 6, Semitone, Mod range oct −3 to 3, Mod rate (s) 0 to 30, D: "Notes/BPM :", "A above mid C", mantissa, "> ppqn" | **No frequency or BPM meter and no autotune measurement**: nothing feeds TR4 (section 1.1). Pitch is exact: Octave 2, Semitone 7 → 2.5832 V (ideal 2.5833); Octave 6 → 5.9998 V; Octave 6 Semitone 11 → 6.2325 V. Mod rate 0 flips at every tick (100,000 flips in 6 s); Mod rate 1 or 2 flips every 1.000 or 2.000 s [meas]. **Negative "Mod range oct" is lost on save: −2 restores as 3** [meas] |

Common to the apps: a list rebuilds as settings hide and show others. Only Quadraturia, Low-rents, Dialectic Ping Pong and Automatonnetz publish no visible-settings list, so their adapters show the whole table (spec "The settings list"). Sequins and Acid Curds show both of their menu pages at once, under two headings.

---

## 10. CPU, sample rates, robustness

### 10.1 CPU

TESTING.md rule 10 and spec: under 2 % of a core per instance at 48 kHz. Shoal is benched by `make shoal-bench`, not by `make bench`.

`bench_core` from the tree, plugin firmware flags with `-O2`, 10 s of firmware time, random CV and gates, both frames drawn at 60 Hz, saves at 2 Hz, the model every 278 ticks [meas]:

| App | us/tick | Ticks, draws, saves, model: share of one core |
|---|---|---|
| CopierMaschine | 0.016 | 0.030 % |
| Harrington 1200 | 0.030 | 0.055 % |
| Automatonnetz | 0.018 | 0.037 % |
| Quantermain | 0.033 | 0.060 % |
| Meta-Q | 0.026 | 0.048 % |
| Quadraturia | 0.036 | 0.066 % |
| Low-rents | 0.019 | 0.036 % |
| **Piqued** | 0.089 | **0.153 %** (the most) |
| Sequins | 0.029 | 0.054 % |
| Dialectic Ping Pong | 0.032 | 0.056 % |
| Viznutcracker sweet | 0.052 | 0.093 % |
| Acid Curds | 0.017 | 0.033 % |
| References | 0.029 | 0.054 % |

The same cost through `shoal::Host` (adds the history, publishing, saves and the eight per-sample setters; a loop that also generates the random inputs), 20 s at 48 kHz [meas: m2_host]: **0.099 % (Acid Curds, Automatonnetz) to 0.240 % (Piqued)** of one core; 16 Piqued instances in turn 0.198 % each. The figures in issue #51 (Piqued 0.147 %, Acid Curds 0.035 %) match the core bench, not this loop. Budget 2 %. Every app is under 0.25 %.

Single worst sample through the Host, 20 s per app [meas]: p99.9 of 1.8 to 2.3 µs (the 60 Hz frame tick), maximum 9.8 to 49.5 µs. The maxima differ from run to run and the sample is 20.8 µs at 48 kHz; they include operating-system scheduling, and Rack's block size makes a single sample's time irrelevant. The worst single tick has not been isolated (issue #51 notes the same).

Memory: `sizeof(Host)` 69,336 bytes (includes the unrolled history, the published copies and the model); one `Core` 32,848 bytes in 3 allocations; peak resident growth 84 KB per Host over 64 [meas].

Not measured: any Windows or x86-64 figure, Rack's own meter, or CPU in hardware mode against friendly mode (the engine work is the same; widget drawing is not part of the engine bench).

### 10.2 Sample rates

[test: tick counts at 8 to 768 kHz; meas: section 6.1] The firmware runs at one tick rate at every sample rate; outputs and CV timing depend only on ticks. QA checks for rate independence are in 11.2. At 768 kHz one tick lasts 46 samples and a one-sample trigger is still clocked. A mid-run rate change keeps the core and the accumulator [test]. Per-rate reference values for those checks: Quadraturia A's period 7.28 s; Piqued default AD under 10 % at 0.614 s; CV settle 4.5 ms to 1 code.

### 10.3 Hostile input and robustness

- `test_faults` (15 tests): every app boots and runs; every setting of every app swept over its full range while ticking (the sweep that would have caught the reported Viznutcracker crash); pairs at extremes; random control mashing (presses, long presses, turns, the app menu); random combinations of settings on both input ranges with drawing; 10 named regressions for firmware undefined behaviour. 50 more fuzz seeds passed [meas]. Sanitizer flags `-fno-sanitize=alignment,shift-base` are off (the firmware uses unaligned casts and shifts on purpose).
- **Hostile volts**: NaN and ±infinity on CV and gate, ±1,000 V, mid-run range change [test: `testHostileVolts`].
- **Hostile controls**: huge turns, hostile button events [test: `testHostileControls`].
- **Instances**: two cores with different seeds differ and never affect each other; the same on two threads; lifetime and `millis()` wrap (after 2^32 ms, not after the 32-bit tick counter's 71.6 hours) [test: test_instances].
- **Desktop-CPU hazards**: the firmware is compiled with `-fwrapv -fno-strict-aliasing -funsigned-char` and without fast-math (the plugin's `-funsafe-math-optimizations` is undone for it). Integer divides, shifts of 32 or more and out-of-table reads that the Teensy gave a value for are guarded to that value (section 10.4).
- Tests ran on macOS arm64 only. The Windows package is cross-built (Docker); nothing was run on Windows, and `long` is 32-bit there [unverified].

### 10.4 Firmware changes that alter behaviour

`src/shoal/firmware/CHANGES.md` marks every change `// SHOAL:`. Beyond the mechanical ones (per-instance state, the non-blocking app menu, no screensaver, the ARM assembly replaced), these change behaviour:

- **Outputs start at 0 V** (the Teensy at +6.2 V), CHANGES.md "Deliberate deviations".
- **No screensaver**: the idle timeout and the UP long press do nothing, and `APP_EVENT_SCREENSAVER_ON/OFF` are never sent.
- **App menu**: non-blocking; it closes unchanged after 25 s; R press or long press switches app with no save (the host saves); UP toggles acceleration; L does nothing. The current app's `loop()` does not run while it is open.
- **Saves**: `save_app_data` writes from app 0 instead of a random app, so every save with no change is byte-identical.
- **Checksum length** fixed for 64-bit hosts (`PageStorage::checksum`).
- **Guards** that return the Teensy's value instead of undefined behaviour: bytebeat division and shifts, Euclidean pattern table index and shifts, `Input_Map` neighbour reads, integer-sequence shift, quantizer far pitch, mask rotate with no notes, chord table past 32, H1200 note names below −120, References note name below the lowest octave, Sequins step −1. Outside the object, no Teensy value is known: `EuclideanFilter(0)` reads row 0; the multistage envelope's segment 8 is taken as finished.
- **Calibration and the splash** are removed; the calibration defaults load at every boot.

---

## 11. Open issues, and found while preparing this document

### 11.1 Filed issues about Shoal: do not re-file

| Issue | Title | Notes |
|---|---|---|
| #51 | Shoal: the output history is copied in full on every published frame | Open. Unroll and copy happen at 60 Hz in every app and both modes; the average cost is negligible. Worst single tick not measured. Suggested: only for Traces and XY, only what changed |
| #64 | Polyphony: modules should be polyphonic where it makes sense | Open. Lists Shoal as "the O&C firmware's four mono outputs". A polyphonic cable gives channel 1; non-goal in the design |
| #67 | Aliasing: measure every module that makes or bends sound | Open. Shoal is "out of scope unless an app is found to alias badly". Viznutcracker sweet is the app to watch |

No other issue mentions Shoal (a title and body search of all states).

### 11.2 Known limits that are not defects

- The right-click options, the ±5 V input range control and the References meters are not built (section 1.1).
- A long press happens at release (6.4).
- State is captured twice a second (3.2).
- New core's CV inputs take 5 ms to settle (6.2). The hardware does the same.
- The staircase output at 16.67 kHz.
- Firmware quirks the manual lists: Harrington's Euclidean rows after a load, Quantermain's stale list after "Copy scale", Meta-Q's mask row slot, Quadraturia's Coarse and Fine under Tap tempo, References' negative Mod range.

### 11.3 Found while preparing this document (not filed)

1. **A gate high at boot, reset or restore clocks the new core with an unsettled CV** (5.3). CopierMaschine: 1 V in, +4.9998 V out.
2. **A press under 7 ms after a second of no presses is a long press** (6.4). Firmware behaviour, same as the hardware; reachable from Rack only by a very short button pulse.
3. **Saved state can be 0.5 s old** (3.2). By design; the manual says "about twice a second" but not what that costs.
4. **No Shoal `static_assert` pins the param, input, output or light ids** (2.2). Fog has them.
5. **Spec and manual gaps**: output and range figures in section 1.1 that are looser than the code (Quadraturia +5.2 V is +5.28 V; Piqued +5 V is 4.961 V; Low-rents centre; Ping Pong 6.25 V needs maximum settings), the long click, and "−2 restores as 3".
6. **The module's own JSON decode** (`decodeState` in `Shoal.cpp`) is not under test; `PatchState.hpp` is. Hand-edited patches should be part of QA (11.6).
7. Ctrl with Shift or Alt makes the encoder drag plain speed (section 2.4); the list slider has a "finer" mode, the encoder does not.
8. The listening guide `shoal-2.0.39-listening.html` agrees with the code: the scale ranges for the trace lanes (Piqued 0 to +5 V, Ping Pong 0 to +6.25 V, Quadraturia the full range), the 4 s window, "outputs start at 0 V", the hardware-only editors, the missing right-click options, and issue #51. Its patch 1 (D three clocks behind A at buf.index 0) matches the CopierMaschine shift test [test: test_apps `testCopierMaschineShifts`], and its patch 7 (a new Sequins plays one note) matches the manual [unverified by me].

---

## 12. What the automated suite covers

Sixteen test programs, 1,149 CHECK call sites, all in `src/shoal/test_*.cpp`, built with ASan and UBSan. They link `Firmware.o` and drive `Core`, `Host`, the layout headers and the shim. All passed [test].

| Program | CHECK sites | Covers |
|---|---|---|
| test_shim | 54 | Volts, Schmitt, tick clock, encoder pins, dspinst, the ARM-replacement maths |
| test_boot | 5 | Boot of every app |
| test_io | 42 | ADC cadence and smoothing, pitch round trip, input range, hostile volts, short pulses, Schmitt, tick count (8 to 768 kHz), outputs hold, outputs start at 0 V |
| test_apps | 34 | Settings tables, CopierMaschine shifts, Quantermain in scale, per-channel writes |
| test_ui | 59 | Pixel layout, both frames, drawing never disturbs the firmware, encoder and cursor, acceleration, app menu (open, timeout, close, turn behind it), no screensaver, held-button chords, switch under load, encoder queue cap, hostile controls |
| test_state | 91 | Round trip for every app, stable saves, corrupt images, running state resets, reset equals a fresh core, 0 V outputs, both pages required, input range survives, user data, golden image, forged images and user scales |
| test_faults | 31 | Every app runs; every setting swept; extremes; control mashing; random combinations; 10 undefined-behaviour regressions; `--sweep <seed>` |
| test_instances | 13 | Independence, determinism, two threads, lifetimes, `millis()` wrap |
| test_host | 143 | Tick rate (44.1, 48, 96, 192 kHz, mid-run change), the core through the Host, events, button edges, frames, snapshot before process, restore rejection, events wait for a restore, model, edits and their order, no engine allocation, two real threads, history (matches outputs, fills then scrolls, spike, cleared on change, one publish, not torn) |
| test_friendly | 439 | Generic model, per-app equivalence against an encoder-only core (13 apps), focus, edit while editing, frame drops, stale edits, edit storm, tabs, actions, confirm rows, sub-editors busy, masks, guards, stale list rebuilds, paged apps, recovery |
| test_friendly_probe | 5 | The review's probe for a state that stranded the list |
| test_list | 130 | Layout, hit testing, asks-first, mask cells and paint, slider and its hold, scroll, the outbox for refused edits, value text |
| test_detents | 30 | Drag, scroll, backlog |
| test_patch | 16 | Version, state size, input range rules |
| test_scope | 38 | Lane geometry, trace and X/Y points, circle and grid |
| test_view | 19 | The view description against the firmware's own frame (grid path and note circle) |

`make shoal-globals`: passed (no writable global in the firmware object).

### 12.1 What it does not cover

- **`Shoal.cpp`**: the jacks, lights, params, `process()`, the JSON decode, `onReset`, `releaseButtons`, `OutputStrip`, `ShoalWidget`. No test builds Rack widgets or runs a module in Rack.
- **Every drawing and mouse path**: `FriendlyList`, `ScopeScreen`, the encoder drag, the click latch, the wheel gates, menus. Their geometry and arithmetic is tested; the events are not. The scope spec says the look is for the user to check in Rack.
- **Rack behaviour**: thread use, `onSampleRateChange` on add, bypass, `dataToJson` against `process`, preset load.
- **Windows runtime**, x86-64 timing, and Linux (CI only).
- **ThreadSanitizer for Shoal** in `make test` (my scratch run was clean).
- **Aliasing** (issue #67) and any audio-domain measure.
- **`make smoke`** was not run (needs Rack). TESTING.md says it screenshots every module with no module behind it, so it shows friendly mode and a canned frame only.

---

## 13. Suggested QA test areas and expected results

### 13.1 Panel and jacks

| Procedure | Pass criteria |
|---|---|
| Add Shoal | Friendly mode, lit UI MODE cap, CopierMaschine, all outputs 0.00 V, strip shows `+0.00V` four times |
| CV 1 at +1 V, TRIG 1 pulse (CopierMaschine) | Output A 0.9999 V (±0.001 V) after the pulse |
| CV range: DC −5, −3.3, 0, +5, +6.6, +9 V into Quantermain or CopierMaschine, scope the clamp | Reads −3.33 V floor and +6.665 V ceiling; the 0 V reading is within 5 mV |
| TR light with a 1 ms pulse | Lights (immediate rise), fades over about 100 ms |
| Output light at ±5 V | Full; at 0 V off; same for negative |
| Polyphonic cable into CV 1 | Only channel 1 is used |
| Unpatched jacks | 0 V; no clocks |
| Output staircase on a scope | Steps of 60 µs; 152.6 µV per code |
| Bypass, with Shoal running | Records what Rack does [unverified]; on un-bypass the firmware resumes where it stopped |

### 13.2 Timing and sample rates

| Procedure | Pass criteria |
|---|---|
| Quadraturia (defaults) A's period at 44.1, 48, 96 and 192 kHz | 7.28 s each (±0.5 %). If it were 8 % longer at 44.1 kHz, `onSampleRateChange` is not reaching the module |
| Piqued A, 10 ms trigger, defaults, same rates | Peak 4.961 V at 0.400 s; under 10 % at 0.614 s |
| Change the engine sample rate while running | Periods continue; no reset, no stuck note |
| 1-sample trigger into CopierMaschine at 192 kHz (a very short gate from a script) | Clocks once |
| Gate to output delay (CopierMaschine, CV 1 steady) | 120 to 160 µs plus cable |
| CV step 0 → +2 V on CV 1 into a pass-through such as Quantermain with cnt+ | Output settles within about 4 ms |

### 13.3 Hardware and friendly mode

| Procedure | Pass criteria |
|---|---|
| Click UI MODE | The lower screen swaps between list and firmware menu; app and settings unchanged; Edit > Undo reverses the switch |
| Edit a value in the list, switch to hardware mode | The right encoder is on the row you edited; the menu shows the new value |
| Turn the encoders in friendly mode | The list highlights the cursor row within a frame (16.7 ms) and scrolls to it |
| Slider drag, Ctrl, Ctrl+Shift, double-click | Normal, a quarter and a sixteenth of the drag speed; default restored; a click alone changes nothing |
| Drag a mask across cells | Every cell crossed takes the first cell's new state; the last active note of a mask does not go dark where the firmware keeps it |
| Long fast encoder spin then a list edit | The edit applies after the spin finishes (up to about 0.5 s), in order |
| Open the app menu (hold PUSH R 1 s, release), then try the list | Brass banner "HARDWARE EDITOR OPEN"; the list is dimmed and takes no edits; clicking the banner switches to hardware mode |
| App menu, 25 s idle | Closes unchanged |
| A list edit in each app (one row of each kind) against the same value set by the encoders | Same settings and same outputs afterwards (as the equivalence tests) |
| Row confirm (Harrington "Reset all settings", Automatonnetz "Clear grid", Meta-Q copy, Sequins and Acid Curds "Clear CV mapping") | A small menu; nothing happens until "Confirm" |
| Wheel over a list that fits; over one that overflows; over an encoder with knobScroll on and off | Rack scrolls; the list scrolls 2 rows per notch; the encoder turns one detent per notch only with knobScroll on |
| Press for under 7 ms (MIDI-map a button) after a pause | See 6.4: this opens the app menu; record it, do not file it as new |

### 13.4 The scope

| Procedure | Pass criteria |
|---|---|
| Each of the 13 apps in friendly mode | The kind and lanes of section 8; letters match the output shown in each lane |
| Quadraturia, Piqued, Ping Pong: change nothing for 4 s | The trace fills the lane from right to left, scrolls, newest at the right; a 1 ms trigger shows as a spike |
| Piqued inverted | Rests at the lane's top, falls to the bottom |
| Low-rents | Two X/Y curves, fading with age, A × B and C × D |
| Change app, Initialize, load a patch | The traces start empty again |
| Hardware mode | No dividers, letters or smooth views; the o_C screen pixel for pixel |
| 100 % and 200 % zoom; light and dark panel | No clipped letters; lines at least 1.2 device pixels |

### 13.5 Save, load, Initialize

| Procedure | Pass criteria |
|---|---|
| Change app and several settings, wait 1 s, save, reload | Same app, same settings, sequences from the top, outputs 0 V until the app writes |
| Edit, save within 0.2 s | The edit may be missing (3.2); record it |
| Duplicate a module | The copy has the settings and its own random values |
| Initialize | CopierMaschine, friendly mode, outputs 0 V, buttons released, user scales factory |
| Hand-edit the patch: `"version": 2`, `"version": 1.0`, a short `"state"`, bad base64, no `"state"`, `"inputRange": 1`, `"inputRange": 1.0` | The first five load defaults without a crash; `1` gives ±5 V (input range −4.998 to +5.000 V); `1.0` gives Hardware |
| Patch saved with Harrington Trigger type Eucl | After load the Euclidean rows are hidden until Trigger type changes (firmware) |
| Patch saved with Sequins or user-scale edits | Step pitches and user scale notes survive |
| A patch saved in 2.0.15 or earlier (the golden image) | Loads |
| Latch a button (shift-click), save, load | Released on load |
| Preset Save and Load | Round trip as a patch |

### 13.6 Restore and reset under load

| Procedure | Pass criteria |
|---|---|
| Initialize while TRIG 1 is held high and CV 1 = 1 V (CopierMaschine) | Known issue 5.3: output goes to about +5 V instead of 1 V until the next clock. Record the value |
| Same with the gate low | Output stays 0 V |
| Load a patch while a clock runs | No crash, no stuck output; the first clock after load may read a wrong CV |
| Load a patch repeatedly (20 times) while the list is visible | No flicker of another app's list, no stale lanes (the history clears) |
| Click a list row while loading | The edit is dropped if it names the old app |

### 13.7 Instances and threads

| Procedure | Pass criteria |
|---|---|
| Sixteen Shoals, each its own app, Rack's engine on several threads | No crash, no cross-talk between outputs, CPU per instance about that of one (0.2 % in my benchmark) |
| Two Shoals with the same patch and the same random settings | Different random values (different seeds) |
| Edit lists on two Shoals alternately | Each takes only its own edits |
| Rack with one thread against four threads | Same sound |

### 13.8 Apps

| Procedure | Pass criteria |
|---|---|
| CopierMaschine canon: Squall Step → CV 1, Gate → TRIG 1, A to D | Each output one clock behind the last; "buf.index" spreads them |
| Quantermain Semitone, ramp into CV 1, TRIG 1 clock | Output is the nearest semitone (mean deviation 12 mV), switching 5 to 10 mV past the midpoint, from −3.0 to +6.04 V; held at −3.0 V and +6.04 V beyond |
| Meta-Q aux gate | 6.000 V for 24.5 ms per clock |
| Quadraturia default | A from −3.71 to +5.28 V, 0.137 Hz |
| Piqued default | 0 to 4.961 V |
| Viznutcracker default | −3.746 to +4.961 V |
| References A Octave 2 Semitone 7 | 2.583 V |
| References "Mod range oct" −2, save, reload | 3 (known) |
| Sequins on a new module | One note; the aux gate 6 V, 24.5 ms |
| Every app: sweep every list row through its range | No crash; values stay in range |

### 13.9 CPU

| Procedure | Pass criteria |
|---|---|
| One Shoal, any app, 48 kHz | Under 0.25 % of a core on an M-series Mac (my benchmark); spec limit 2 % |
| 16 Shoals on Piqued | About 0.2 % each |
| Windows | No budget; record Rack's meter for idle, Piqued with triggers, and 16 instances |
| Friendly against hardware mode | No visible difference in engine load |

---

## 14. Glossary

| Term | Meaning |
|---|---|
| Adapter | A friendly-mode struct for one app: header, rows, edit, tab select, accepting, name and label tables |
| App | One of the 13 o_C programs (CopierMaschine, Harrington 1200, ...) |
| App menu | The firmware's app chooser, opened by a long press of R; non-blocking in Shoal |
| Busy (model) | The app is not on its settings menu (app menu or sub-editor open); every list edit is dropped |
| Context | `shoal::Context`: every mutable firmware global of one Core |
| Core | `shoal::Core`: the firmware, the shim and one instance's state |
| DAC code | The 16-bit output value; 24,551 = 0 V, 6,553.6 per volt |
| ADC code | The 12-bit input value; offset 2730 (or 2048) at 0 V, 409.6 per volt, falling as volts rise |
| Edit | A `FriendlyEdit` from the list: SetValue, ToggleBit, Press, SelectTab, Focus |
| Friendly mode | The default UI mode: the lower screen is the mouse list |
| Frame | The 128 × 64, 1-bit screen image (1,024 bytes) the firmware draws |
| Hardware mode | The lower screen is the firmware's own menu, driven by encoders and buttons |
| Host | `shoal::Host`: one Core with the thread hand-offs, history and saved image |
| Image | The 2,048-byte firmware EEPROM state saved in the patch |
| Main pass | One run of `shoal_main_pass`: app `loop()` and event dispatch, every 17 ticks |
| Model | The `FriendlyModel`: the current app's rows, tabs and status |
| o_C | Ornament & Crime, the eurorack module and its firmware |
| Sub-editor | An o_C editor with no mouse version: user scales, steps, chords, cells, Autotune |
| Tick | One 60 µs firmware core tick (16,666.67 Hz) |
| TR / TRIG | A trigger input; a rising edge is a clock |
| UI poll | The 1 ms firmware pass that reads encoders and buttons |
| UI MODE | The panel latch that selects friendly or hardware mode |

---

### Note on the measurement programs

All are outside the repository, in `/private/tmp/claude-501/-Users-rarneson/faf81925-d608-455e-9de4-930cb0552717/scratchpad/techref/shoal/meas/` (sources and their `.out` files), built against `Firmware_O3.o` (the tree's `src/shoal/Firmware.cpp` with the plugin's firmware flags). They drive `shoal::Core`, `shoal::Host` and `shoal::TickClock` as `Shoal.cpp` does, one mono signal at a time, with no Rack. They do not exercise Rack's ports, threads, widgets or Windows.

| Program | What it measured |
|---|---|
| `m1_timing.cpp` | Tick clock at 11 rates; ADC step and start-up response; gate to output; one-sample pulses; encoder play-out |
| `m2_host.cpp`, `m2b.cpp` | Host cost per app (batch and per sample), 16 instances, restore and reset costs, memory |
| `m3_out.cpp`, `m3b.cpp` | Output ranges per app, gate levels, References pitch, the input mapping in both ranges, the Quantermain sweep |
| `m4_lat.cpp` | Snapshot lag, edit to model, restore to published app |
| `m5_limits.cpp` | References negative Mod range, Mod rate, Harrington Euclidean rows after a restore, Ping Pong peak, app menu timeout |
| `m6_btn.cpp`, `m7_btn2.cpp` | Long-press threshold and the sub-7 ms press |
| `m8_ref.cpp` | Quadraturia period, Piqued default AD times |
| `m9_restore.cpp` | A held gate through a restore and a reset |
| `m10_names.cpp`, `dump_models.cpp` | Piqued trigger names; every app's model (rows, tabs, defaults) |
| `bench_core` (the repository's `src/shoal/bench/bench_core.cpp`) | The CPU table in 10.1 |

I did not run `make smoke` (it needs Rack), the Linux or Windows builds, or a live Rack session.

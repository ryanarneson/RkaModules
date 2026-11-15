# Shoal Hardware Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shoal appears in VCV Rack as a module the user can patch with the O&C manual open: panel, jacks, both screens, encoders and buttons, and settings that save with the patch.

**Architecture:** The stage 1 core (`shoal::Core`) gains a Rack-free wrapper, `shoal::Host`, that owns everything about running the firmware inside an audio engine: sample-rate conversion, control events from the UI thread, frame hand-off, and the periodic state snapshot. The Rack module (`src/shoal/Shoal.cpp`) only maps ports, params and lights onto the Host and draws what it publishes. The panel comes from `tools/gen_panels.py`, extended with screen rectangles and two new component sizes.

**Tech Stack:** C++11 for plugin code (Rack SDK 2.6.6), C++17 for the firmware unit and tests, Python 3 panel generator, Inkscape for `make panels`, GNU make 3.81-compatible makefiles.

**Spec:** `docs/specs/2026-11-14-shoal-design.md` — stage 2 of "Order of work", plus the "Carried forward from stage 1" list. Read "Panel", "Timing", "Saving" and "Testing".

## Global Constraints

- Module slug `Shoal`, folder `src/shoal/`, panel `res/shoal/Shoal.svg` (and `-dark`), layout header `src/shoal/ShoalLayout.hpp` (generated). Never hand-edit `res/`, `res-src/` or `*Layout.hpp`; change `tools/gen_panels.py` and run `make panels`.
- Nightglow look: colours from `RkaTheme`, shared widgets in `src/widgets.hpp`. No stock knobs, no ad-hoc colours in the module.
- Firmware in `src/shoal/firmware/` changes only with a `// SHOAL:` mark on the line and a line in `src/shoal/firmware/CHANGES.md`. `make shoal-globals` stays clean.
- Any guard for desktop CPUs reproduces the ARM Cortex-M4 result (divide by zero gives 0; `a % 0` gives `a`; shifts of 32 or more give 0 or sign fill; in-object out-of-bounds reads return the real neighbour).
- The engine thread never blocks: no `lock()`, no allocation, no I/O in anything `Host::process()` reaches. It may `try_lock`.
- One `shoal::Core` is touched by one thread at a time. Only the engine thread calls it once the module is in the engine.
- `Host.hpp` and `Core.hpp` are C++11, Rack-free, and include no firmware header.
- Voltages: outputs `(code − 24551) / 6553.6` V; inputs clamp to −3.33..+6.67 V (Hardware) or ±5 V; triggers Schmitt at 0.1 V / 1 V; poly cables use channel 0.
- Decided by the user on 2026-11-15: **outputs start at 0 V** after boot, reset and restore, not at the hardware's power-on code. This is a logged deviation from the firmware.
- Out of scope here (later stages): friendly mode and the UI MODE latch (stage 3); the right-click menu, References meters and the manual in `docs/shoal/` (stage 4). Leave room for the UI MODE latch on the panel; do not add an inert control.
- Scripts stay portable across macOS and Linux (no GNU-only flags). Makefiles work on GNU make 3.81.
- Before calling a task done: `make test`, `make`, `make smoke`, and for panel changes look at `build/screenshots/Shoal.png`. Build with `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64`.
- Do not start Rack against the user's real settings; `make smoke` uses a temporary user folder.
- Commit per task on the working branch; never push. Commit messages start `Shoal: ` and end with a `Co-Authored-By:` trailer naming the model that wrote the commit.

## Review Focus

Conditions the spec implies that are most likely to bite in real patches. Each is pinned by a test in the task named.

1. **A patch saved by an older or damaged Shoal** (missing `state`, wrong length, bad base64, wrong `version`, rejected by the firmware): the module starts with defaults and never crashes. Task 2 (`testRestoreRejects`), Task 5 (decode rules).
2. **Save requested before the engine has run** (duplicate module, preset save straight after load): the snapshot is the restored image, not an empty one. Task 2 (`testSnapshotBeforeProcess`).
3. **Sample rate changes and odd rates** (44.1 kHz, 192 kHz, a change mid-run): the tick rate stays exact and nothing is lost. Task 2 (`testTickRate`).
4. **A UI thread drawing and sending encoder turns while the engine runs**: no data race, no lost or duplicated detents, engine never blocks. Task 2 (`testThreads`).
5. **A button held down when the patch was saved**: after load the firmware does not see a long press. Task 5 (button params cleared on load; manual check).

---

### Task 1: Core hardening from the stage 1 reviews

**Files:**
- Modify: `src/shoal/Core.hpp`, `src/shoal/CoreImpl.inc`, `src/shoal/shim/Pins.hpp`, `src/shoal/shim/Volts.hpp`, `src/shoal/firmware/o_c_REV.ino`, `src/shoal/firmware/OC_apps.ino`, `src/shoal/firmware/shoal_prototypes.h`, `src/shoal/firmware/CHANGES.md`, `src/shoal/bench/bench_core.cpp`, `tests.mk`, `tools/check_shoal_globals.sh`, `tools/test_check_shoal_globals.sh`
- Test: `src/shoal/test_io.cpp`, `src/shoal/test_state.cpp`, `src/shoal/test_ui.cpp`, `src/shoal/test_faults.cpp`

**Interfaces:**
- Consumes: `shoal::Core` as it stands.
- Produces:
  - `shoal::TickClock` declared in `Core.hpp` (moved from `shim/Volts.hpp`, same members: `setSampleRate(double)`, `int advance()`).
  - `Core::out(ch)` is 0 V (code 24551) on a new core, after `resetToDefaults()` and after `restore()`, both before and after the first tick, until the app writes.
  - `Core::restore(in)` returns true only when **both** the global-settings page and the app-data page loaded; otherwise the core is a new core's defaults and it returns false.
  - `shoal::kZeroVoltCode = 24551` in `Core.hpp`.

- [ ] **Step 1: Write the failing tests**

In `test_io.cpp`:

```cpp
static void testOutputsStartAtZeroVolts() {
	shoal::Core c;
	for (int ch = 0; ch < 4; ch++) CHECK(c.outCode(ch) == shoal::kZeroVoltCode);
	c.tick();
	for (int ch = 0; ch < 4; ch++) CHECK(c.outCode(ch) == shoal::kZeroVoltCode);
	// CopierMaschine writes only when clocked: still 0 V after a second of silence.
	for (int i = 0; i < 16667; i++) c.tick();
	for (int ch = 0; ch < 4; ch++) CHECK(std::fabs(c.out(ch)) < 0.001f);
}
```

In `test_state.cpp`:

```cpp
static void testOutputsZeroAfterResetAndRestore();   // drive CopierMaschine to non-zero outputs, save, then
                                                     // resetToDefaults(): outCode == kZeroVoltCode before any tick;
                                                     // restore(image): same.
static void testRestoreNeedsBothPages();             // good image: true. Corrupt one byte of the global page data
                                                     // (CoreTest::pageLayout(false, ...)) leaving app data good: false,
                                                     // and save() equals a fresh core's save(). Same with only the
                                                     // app-data page corrupted.
```

In `test_ui.cpp`:

```cpp
static void testEncoderQueueBounded();   // turnEncoder(ENCODER_R, 100000) then 1 s of ticks: no fault, and the
                                         // setting moved by no more than the documented cap (256 detents).
```

In `test_faults.cpp`: give `testQuantizerFarPitch` an assertion on the returned pitch that would fail if the guard read 0 instead of the neighbouring `enabled_notes_` values.

- [ ] **Step 2: Run them and see them fail**

Run: `make test`
Expected: `test_io` fails in `testOutputsStartAtZeroVolts` (code 0, then 64980); the new `test_state` checks fail.

- [ ] **Step 3: Start the outputs at 0 V**

In `Core::Impl`'s constructor and in `reboot()`, after `shoal_boot()`, set all four DAC channels to `kZeroVoltCode` through the firmware's own `OC::DAC` setter and copy the same code into `Impl::dac`. No firmware file changes for this; add a "Deliberate deviations" entry to `CHANGES.md` saying the Teensy powers up at code 0xffff and Shoal at 0 V, by the user's decision of 2026-11-15. Update the `Core.hpp` comment on `restore`/`resetToDefaults` to say outputs return to 0 V.

- [ ] **Step 4: Make `reboot()` safe and `restore()` all-or-nothing**

- `reboot()` starts with `assert(current() != ctx.get())`.
- `shoal_boot()` reports whether the global page and the app page each loaded (change its return to a small struct or two-bit value; mark and log the change). `restore()` returns true only if both did; otherwise it reboots with no image.

- [ ] **Step 5: Replace the encoder queue's `std::deque` with a fixed ring**

`EncoderPins` keeps the same `turn`, `clear`, `poll` behaviour and the same cap, with a `uint8_t` array of 1024 and head/tail indices. No allocation after construction.

- [ ] **Step 6: Move `TickClock` into `Core.hpp`**

Leave `Volts.hpp` including nothing new; tests that used `shoal::TickClock` keep compiling.

- [ ] **Step 7: Pin the saved-image layout**

Next to the storage declarations in `OC_apps.ino`, add `static_assert`s (marked `// SHOAL:`) on `sizeof` and the member offsets of `GlobalSettings` and `AppData` as they are on this build, so a compiler or platform that lays them out differently fails to build instead of writing incompatible patches. Take the numbers from the current build and confirm they hold in the Linux and Windows cross-builds (`tools/cross_build.sh`; Colima is the Docker host and may already be running — do not start, stop or reconfigure it).

- [ ] **Step 8: Build flags and the globals check**

- Add `-fvisibility=hidden` to the plugin's `Firmware.cpp` object (not to the test build).
- Add `-funsigned-char` to `SHOAL_FW_FLAGS` (plain `char` is unsigned on the Teensy). Run the whole suite. If any test result changes, find out which side matches the hardware before keeping or dropping the flag, and record the outcome in `CHANGES.md`.
- `tools/check_shoal_globals.sh` allows exactly one thread-local, the one behind `shoal::current()`, by name; any other thread-local fails. Extend `tools/test_check_shoal_globals.sh` with a probe that proves a second thread-local is rejected.

- [ ] **Step 9: Bench and fuzz**

- `bench_core.cpp` also reports the cost of `drawMenu` + `drawScope` at 60 Hz and of `save()` at 2 Hz, per app, as a percentage of a core. Expected total per app: under 2 %.
- `tests.mk` gains an opt-in target `shoal-fuzz` that builds `test_faults` once and runs its random-combination sweep for `SHOAL_FUZZ_SEEDS` seeds (default 50) via the existing `SHOAL_FAULT_SEED` mechanism. Not part of `make test`.

- [ ] **Step 10: `bjorklund_row` lengths above 31**

Compare with upstream (`~/ws/plugins/O_C`). If mapping every length above 31 to the 31-step row is what the Teensy does, say so in the comment; if not, fix it under the guard rule with a test.

- [ ] **Step 11: Run everything**

Run: `make test && make && make smoke && make shoal-bench`
Expected: all tests pass; `shoal-globals` clean; smoke prints `ok: Sway` and `ok: Fathom`; bench under 2 % per app including drawing.

- [ ] **Step 12: Commit**

```bash
git add -A src/shoal tests.mk Makefile tools
git commit -m "Shoal: 0 V start, all-or-nothing restore, fixed encoder ring, layout asserts"
```

---

### Task 2: `shoal::Host`, the engine-side wrapper

**Files:**
- Create: `src/shoal/Host.hpp`
- Test: `src/shoal/test_host.cpp`

**Interfaces:**
- Consumes: `shoal::Core`, `shoal::TickClock`, `kFrameBytes`, `kStateBytes`, `InputRange`, `Button`, `Encoder` from `Core.hpp`.
- Produces (header-only, C++11, Rack-free):

```cpp
namespace shoal {
struct ControlEvent {
	enum Type : uint8_t { Turn, SelectApp, Reset, SetInputRange, SetAcceleration };
	Type type;
	int8_t index;    // Turn: Encoder
	int16_t value;   // Turn: detents; SelectApp: app; SetInputRange: InputRange; SetAcceleration: 0/1
};

class Host {
public:
	explicit Host(uint32_t seed);

	// Engine thread only.
	void setSampleRate(double hz);
	void setCv(int ch, float volts);
	void setGate(int ch, float volts);
	void setButton(Button b, bool down);   // call every sample or on change; edges are detected inside
	void process();                        // one engine sample
	float out(int ch) const;
	bool gateHigh(int ch) const;

	// Any one other thread (the UI thread). None of these touch the Core.
	bool post(ControlEvent e);                             // false: queue full, event dropped
	void frames(uint8_t* menu, uint8_t* scope);            // latest published; kFrameBytes each
	void snapshot(uint8_t* state, InputRange* range);      // latest saved image; kStateBytes
	void requestRestore(const uint8_t* state, InputRange range);  // state null: defaults
	int app() const;                                       // as of the last publish
	bool appMenuOpen() const;
};
}
```

Behaviour the tests pin:

- `process()` runs `TickClock::advance()` ticks. Before the first tick of a call it applies, in order: a pending restore, then queued control events.
- Every 278 ticks (60 Hz) it draws both frames into engine-owned buffers and publishes them; every 8333 ticks (2 Hz) it calls `core.save()` and publishes the image with the current input range. Publishing is `try_lock` and copy; on failure it retries at the next tick rather than waiting.
- The constructor seeds the core, then publishes frames and a snapshot once, so the UI has both before the engine first runs.
- `requestRestore` stores the image as pending **and** makes it the current snapshot (or a new core's image for null), so a `snapshot()` straight after returns it. If the firmware then rejects the image, the next publish replaces the snapshot with the defaults.
- The event queue is a fixed single-producer single-consumer ring of 64.

- [ ] **Step 1: Write the failing tests** (`src/shoal/test_host.cpp`, using `testing.hpp`)

```cpp
static void testTickRate();
// For 44100, 48000, 96000, 192000 Hz: run 10 s of process(); the core ran
// round(10 * kTickHz) ticks, +-1 (CoreTest::ticks). Then change the rate
// mid-run 48000 -> 44100 and check the next 10 s the same way.

static void testCopierThroughHost();
// 48 kHz. CV1 = 2.0 V, a 5 ms 5 V pulse on gate 0: out(0) is within one DAC
// code of 2.0 V afterwards. A 1-sample 5 V pulse also clocks it (edge latched).

static void testEventsApplied();
// post({Turn, ENCODER_R, 3}) and post({SelectApp, 0, 3}) from the main thread,
// then 0.2 s of process(): app() == 3 (after the next publish). 65 posts with
// no process(): the 65th returns false.

static void testButtonsAreEdges();
// setButton(BUTTON_R, true) every sample for 1.2 s then false: appMenuOpen()
// becomes true (the firmware's R long press), exactly as Core::setButton.

static void testFramesPublished();
// After construction frames() already returns a non-blank menu frame. After
// SelectApp the menu frame changes within 0.1 s of process().

static void testSnapshotBeforeProcess();
// Host a; run 1 s; select app 3; run 1 s; snapshot(img). Host b;
// b.requestRestore(img, Bipolar5); with NO process(): b.snapshot(out, &r)
// gives out == img and r == Bipolar5. After 1 s of process(): b.app() == 3.

static void testRestoreRejects();
// requestRestore of 2048 bytes of 0x5a: after 1 s of process(), app() == 0
// and snapshot() equals a fresh Host's snapshot taken after the same run.
// requestRestore(nullptr, Hardware): the same.

static void testSaveDoesNotDisturb();
// Two Hosts, same seed, same inputs (Quantermain clocked at 100 Hz with a
// slow CV ramp), 5 s. One has its 2 Hz save disabled through a test hook.
// Their four outputs are identical on every sample.

static void testThreads();
// Engine thread: 5 s of process() at 48 kHz with clocked inputs. Second
// thread, meanwhile: loops frames(), snapshot(), and post({Turn, ENCODER_R, +1})
// then {Turn, ENCODER_R, -1}, counting accepted posts. Afterwards the menu
// cursor (CoreTest::menuSetting) is where it started and no sanitizer fired.
```

- [ ] **Step 2: Run them and see them fail**

Run: `make build/test/shoal/test_host`
Expected: does not compile, `Host.hpp` not found.

- [ ] **Step 3: Implement `Host` in `src/shoal/Host.hpp`**

Use `std::mutex` with `try_lock` on the engine side and `lock` on the UI side, one mutex for frames and one for the snapshot and pending restore. `app()` and `appMenuOpen()` are `std::atomic` values written at each frame publish. No heap allocation after construction.

- [ ] **Step 4: Run the tests**

Run: `make test`
Expected: `test_host: all checks passed`, every other test still passes.

Also run `test_host` once under ThreadSanitizer by hand (`-fsanitize=thread` in place of the address sanitizer, scratch build, not committed) and report the result. A reported race in `Host` is a failure.

- [ ] **Step 5: Commit**

```bash
git add src/shoal/Host.hpp src/shoal/test_host.cpp
git commit -m "Shoal: Host wraps the core for an audio engine"
```

---

### Task 3: Panel generator support and the Shoal panel

**Files:**
- Modify: `tools/gen_panels.py`, `Makefile` (only if `make panels` needs it)
- Create (generated, committed): `res-src/shoal/Shoal.svg`, `res-src/shoal/Shoal-dark.svg`, `res/shoal/Shoal.svg`, `res/shoal/Shoal-dark.svg`, `src/shoal/ShoalLayout.hpp`
- Regenerated: the Sway and Fathom outputs must come out byte-identical.

**Interfaces:**
- Produces, in `namespace ShoalLayout` (mm, as the other layout headers):
  - `PARAM_ENC_L_PUSH`, `PARAM_ENC_R_PUSH`, `PARAM_UP`, `PARAM_DOWN` (`Vec`)
  - `WIDGET_ENC_L`, `WIDGET_ENC_R` (`Vec`, centres of the two encoders; a new component kind `widget`, not a Rack param)
  - `INPUT_TR1..4`, `INPUT_CV1..4`, `OUTPUT_A..D` (`Vec`)
  - `LIGHT_TR1..4`, `LIGHT_A..D` (`Vec`)
  - `SCREEN_SCOPE`, `SCREEN_EDITOR`, `SCREEN_STRIP` (`rack::math::Rect`, position and size in mm)
- Generator additions:
  - `RADII` gains `"encoder": 6.7`, `"button": 3.0`, `"light": 1.1`; `component_size` uses them for kind `widget`, for params named `*_PUSH`/`UP`/`DOWN` via the panel's `sizes`, and for kind `light`.
  - A panel may carry `screens=[(name, x, y, w, h)]`. Each is drawn as a near-black rounded rectangle with a thin accent edge and written to the layout header as a `Rect`. `check_clearance` also fails if a component overlaps a screen.
  - Kind `widget` and kind `light` get placeholder circles in the hidden `components` layer like the others.

Layout, 28 HP (142.24 mm wide). Regions are fixed; exact centres are the implementer's within them, subject to the clearance check:

| Region | x (mm) | y (mm) | Contents |
|---|---|---|---|
| Output strip | 4.0–87.2 | 16.0–21.0 | `SCREEN_STRIP`: live A–D voltages (drawn by the module) |
| Scope | 4.0–87.2 | 21.5–63.1 | `SCREEN_SCOPE`: 128×64 at 0.65 mm per pixel |
| Editor | 4.0–87.2 | 67.0–108.6 | `SCREEN_EDITOR`: 128×64 at 0.65 mm per pixel |
| Controls card | 91.0–138.2 | 15.0–62.0 | encoders L and R on one row, a push button under each, then UP and DOWN on a row with a free third position at the right for the stage 3 UI MODE latch |
| Jacks card | 91.0–138.2 | 66.0–113.0 | three columns TR / CV / OUT, four rows, at least 10 mm row pitch; OUT column on a cyan-edged card; a light beside each TR and each OUT jack |

Labels: `ENC L`, `ENC R`, `UP`, `DOWN`, column heads `TRIG`, `CV`, `OUT`, row labels `1`–`4` on the input side and `A`–`D` on the output side. Title `SHOAL`.

- [ ] **Step 1: Write a generator self-check that fails**

Add to `gen_panels.py`'s `main()` path a check, run for every panel: every `screens` rectangle lies inside the panel, and no component circle (by `RADII`) comes within `CLEARANCE` of a screen. Add the `Shoal` entry to `PANELS` with deliberately overlapping first-guess positions for one jack and the editor.
Run: `python3 tools/gen_panels.py`
Expected: exits with `Shoal: INPUT … overlaps screen SCREEN_EDITOR`.

- [ ] **Step 2: Implement the generator additions and lay out Shoal**

Fix the positions so the check passes.

- [ ] **Step 3: Generate**

Run: `make panels`
Expected: `generated Sway`, `generated Fathom`, `generated Shoal`; `git status` shows no change to any Sway or Fathom file; `res/shoal/Shoal.svg` and `-dark` exist with text converted to paths.

If Inkscape is not installed, stop and report; do not hand-write `res/`.

- [ ] **Step 4: Look at it**

Render `res/shoal/Shoal.svg` to a PNG (Inkscape `--export-type=png`, into the scratch directory, not the repo) and read the image. Check: nothing overlaps, labels are legible and not clipped, the two screens are the same size, the layout matches the spec's sketch.

- [ ] **Step 5: Commit**

```bash
git add tools/gen_panels.py res-src/shoal res/shoal src/shoal/ShoalLayout.hpp
git commit -m "Shoal: panel, and screens/encoders/buttons in the panel generator"
```

---

### Task 4: Shared widgets and the Shoal module

**Files:**
- Modify: `src/widgets.hpp`, `src/plugin.hpp`, `src/plugin.cpp`, `plugin.json`
- Create: `src/shoal/Shoal.cpp`
- Possibly modify: `tools/gen_panels.py` (if a widget needs a generated SVG in `res/components/`, as `Trim` does)

**Interfaces:**
- Consumes: `shoal::Host` (Task 2), `ShoalLayout` (Task 3), `RkaTheme`.
- Produces, in `namespace rka` (`src/widgets.hpp`), reusable by later modules:

```cpp
// A momentary button with a light. Click: down while held. Shift-click:
// latches down until the next click of either kind. The light is on while
// the param is above 0. Not randomised.
struct HoldButton;   // a ParamWidget; use with createParamCentered

// An endless encoder. Not a Rack param. Vertical drag: one detent per 8 px
// of mouse travel (Ctrl/Cmd: 32 px). Scroll wheel over it: one detent per
// notch. Draws a Nightglow disc with a pointer that turns 1/24 turn per detent.
struct Encoder : widget::OpaqueWidget {
	std::function<void(int detents)> onTurn;   // > 0 clockwise
};

// Draws a 1-bit 128x64 frame (shoal layout: bit y%8 of byte (y/8)*128 + x)
// scaled to its box, in the self-illuminating layer, lit pixels in `colour`.
struct PixelScreen : widget::Widget {
	std::function<bool(uint8_t* frame)> source;   // fills 1024 bytes; false: draw nothing
	NVGcolor colour;
};
```

- Module `Shoal`:
  - Params (in this order, never reordered): `ENC_L_PUSH_PARAM`, `ENC_R_PUSH_PARAM`, `UP_PARAM`, `DOWN_PARAM`.
  - Inputs: `TR1_INPUT..TR4_INPUT`, `CV1_INPUT..CV4_INPUT`. Outputs: `A_OUTPUT..D_OUTPUT`.
  - Lights: `TR1_LIGHT..TR4_LIGHT` (amber), `A_LIGHT..D_LIGHT` as two lights each (cyan positive, magenta negative, `CyanMagentaLight`), brightness `|volts| / 5`.
  - Owns one `shoal::Host` seeded from `random::u32()`.
  - `process()`: `setCv`/`setGate` from channel 0 of each input (an unpatched input reads 0 V), `setButton` from the four params (`> 0.5`), `host.process()`, outputs from `host.out(ch)`. `onSampleRateChange` calls `host.setSampleRate`.
  - Button mapping: `ENC_L_PUSH` → `BUTTON_L`, `ENC_R_PUSH` → `BUTTON_R`, `UP` → `BUTTON_UP`, `DOWN` → `BUTTON_DOWN`.
- Widget `ShoalWidget`:
  - Panel `res/shoal/Shoal.svg` with the `-dark` variant, as Fathom's widget does it.
  - Two `PixelScreen`s on `SCREEN_SCOPE` and `SCREEN_EDITOR`, cyan, fed from `host.frames()`.
  - The output strip on `SCREEN_STRIP`: `A:+1.83V  B:… C:… D:…` from the output voltages, two decimals, in Rack's built-in monospace font, cyan.
  - Two `rka::Encoder`s whose `onTurn` posts `{Turn, ENCODER_L|R, detents}`.
  - With no module (module browser and `make smoke` screenshots): the widget builds a private `shoal::Host`, runs 0.25 s of `process()` on it in the constructor, and the screens show its frames; the strip shows `+0.00V` four times.
- `plugin.json` entry: slug `Shoal`, name `Shoal`, description crediting Ornament & Crime and its authors (Patrick Dowling, Max Stadler, Tim Churches), tags `Quantizer`, `Sequencer`, `Envelope generator`, `Low-frequency oscillator`, `Random`, `Utility`, `Hardware clone`.

- [ ] **Step 1: Register the module and see the build fail**

Add `extern Model* modelShoal;` and `p->addModel(modelShoal);` and the `plugin.json` entry.
Run: `make`
Expected: link error, `modelShoal` undefined.

- [ ] **Step 2: Add the three shared widgets to `src/widgets.hpp`**

`HoldButton` and `Encoder` are drawn in Nightglow colours (theme card colour, `hint` rim, `ink` pointer, accent light); if they need SVG parts, generate them from `gen_panels.py` into `res/components/` the way `Trim` is. `PixelScreen` draws lit pixels as horizontal runs, one path per frame, in `drawLayer(…, 1)`.

- [ ] **Step 3: Write `src/shoal/Shoal.cpp`**

`Shoal.cpp` is plugin code: C++11, includes `Host.hpp` only (no firmware or shim header).

- [ ] **Step 4: Build and smoke**

Run: `make && make smoke`
Expected: `ok: Sway`, `ok: Fathom`, `ok: Shoal -> build/screenshots/Shoal.png`, no warnings mentioning `rka` in Rack's log.

- [ ] **Step 5: Look at the screenshot**

Read `build/screenshots/Shoal.png`. Check: both screens show firmware pixels (the CopierMaschine menu below, its scope view above), crisp and aligned to their rectangles; the strip reads four voltages; encoders, buttons, jacks and lights sit on their labels; nothing is clipped at the panel edge.

- [ ] **Step 6: Run the offline suite**

Run: `make test`
Expected: all pass (the module adds no offline test; `Host` carries the logic).

- [ ] **Step 7: Commit**

```bash
git add src/widgets.hpp src/plugin.hpp src/plugin.cpp plugin.json src/shoal/Shoal.cpp tools/gen_panels.py res/components
git commit -m "Shoal: the module in hardware mode"
```

---

### Task 5: Saving, reset, and the docs

**Files:**
- Modify: `src/shoal/Shoal.cpp`, `README.md`, `CHANGELOG.md`, `CLAUDE.md`, `docs/specs/2026-11-14-shoal-design.md` (tick off the carried-forward items this plan closed)
- Create: `docs/shoal/README.md` (a short page; the full manual is stage 4), `docs/shoal/Shoal.png` (the smoke screenshot)

**Interfaces:**
- Consumes: `Host::snapshot`, `Host::requestRestore`, `Host::post`.
- Produces the patch format, version 1:

```json
{ "version": 1, "state": "<base64 of 2048 bytes>", "inputRange": 0 }
```

  - `dataToJson`: `snapshot()` into the three fields. `inputRange` is 0 (Hardware) or 1 (±5 V).
  - `dataFromJson`: accept only `version == 1` with a `state` that decodes to exactly 2048 bytes; then `requestRestore(bytes, range)`. Anything else (field missing, other version, bad base64, wrong length): `requestRestore(nullptr, Hardware)`. `inputRange` outside 0..1 reads as 0.
  - After `dataFromJson`, and in `onReset`, the four button params are set to 0, so a button that was latched when the patch was saved is not held on load.
  - `onReset` (Rack's Initialize): posts `{Reset}`; outputs go to 0 V and the app is CopierMaschine with defaults.
  - `onRandomize`: does nothing to the firmware (override so Rack's default does not apply; the button params are already not randomisable).
- The input range has no control yet (the right-click menu is stage 4); it is stored and restored so the format does not change later.

- [ ] **Step 1: Implement `dataToJson`, `dataFromJson`, `onReset`, `onRandomize`**

Use `rack::string::toBase64` / `fromBase64`. Keep the decode rules in one small static function in `Shoal.cpp`.

- [ ] **Step 2: Build, test, smoke**

Run: `make test && make && make smoke`
Expected: all pass; screenshot unchanged from Task 4.

- [ ] **Step 3: Round-trip check outside the user's Rack**

Write a patch file by hand in the scratch directory containing one Shoal whose `state` is the snapshot of a Host switched to Quantermain (produce the base64 with a few lines of scratch C++ linked against `build/test/shoal/Firmware.o`; do not commit it). Run Rack headless on it with a temporary user folder for a few seconds (`Rack -h -u <tmpdir> <patch>` under the same `timeout`/`gtimeout` wrapper `tools/rack_smoke.sh` uses), then read the autosave it wrote in that temporary folder and confirm the Shoal module's `data.state` decodes to an image whose current app is Quantermain (restore it into a scratch Host and check `app() == 3`). Repeat with `state` truncated: Rack must not crash and the autosaved state must be a default image.

If headless Rack cannot be made to autosave or exit within the timeout, report exactly what happened and leave this check to the manual list below; do not weaken the decode rules.

- [ ] **Step 4: Docs**

- `docs/shoal/README.md`: what Shoal is, the credit to Ornament & Crime, the controls (encoders: drag or scroll; buttons: click, shift-click to hold), the voltage ranges, that settings save with the patch, a pointer to the o_C manual for the apps, and what is not there yet (friendly mode, right-click options).
- `README.md`: add Shoal to the module list. `CHANGELOG.md`: one line in the existing unreleased section. `CLAUDE.md`: one bullet — the module maps onto `shoal::Host`; only the engine thread touches the core.
- In the design doc's "Carried forward from stage 1" list, mark the items this plan closed and leave the rest.

- [ ] **Step 5: Commit**

```bash
git add src/shoal/Shoal.cpp README.md CHANGELOG.md CLAUDE.md docs/shoal docs/specs/2026-11-14-shoal-design.md
git commit -m "Shoal: settings save with the patch; docs"
```

---

## After the last task: checks only the user can make

These need hands on a mouse in a real Rack, so they are handed to the user rather than automated:

1. Add Shoal; clock TR1 and feed CV1: A–D shift as CopierMaschine should.
2. Long-press ENC R's button: the app list opens on the editor screen; pick Quantermain with the right encoder and a click.
3. Encoder feel: drag distance per detent and the scroll wheel.
4. Shift-click a button to hold it, turn an encoder, click to release (the user-scale editor's hold-L-and-turn).
5. Save the patch, reopen it: same app, same settings, outputs at 0 V until clocked.
6. Duplicate the module: the copy has the same settings and then runs independently.
7. Two Shoals with random sources (Low-rents, or Quantermain with Turing): different values.

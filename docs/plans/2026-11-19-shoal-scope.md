# Shoal: upper screen lanes and smooth views: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In friendly mode, Shoal's upper screen separates the outputs' lanes in every app, and draws six apps' views itself with smooth lines; the voltage strip gets dividers.

**Architecture:**
- **Core** fills a plain `shoal::ScopeView` for the current app (`Core::scopeView`), written in a new `src/shoal/ScopeView.inc` beside the friendly adapters.
- **Host** records a history of the four outputs on the engine thread (`shoal::ScopeHistory`) and publishes view and history with the frames.
- **Geometry** (lanes, dividers, labels, volts and time to points) is the Rack-free `src/shoal/ScopeLayout.hpp`.
- **The widget** `shoal::ScopeScreen` (`src/shoal/ScopeScreen.hpp`) subclasses `rka::PixelScreen`: firmware pixels in hardware mode; in friendly mode pixels plus dividers and letters, or a smooth view.

**Tech Stack:** C++11 plugin code (Rack SDK 2.6.6), C++17 offline tests with `src/testing.hpp` (Shoal tests link `build/test/shoal/Firmware.o`, sanitizers on), the Python 3 panel generator, Inkscape.

**Spec:** `docs/specs/2026-11-19-shoal-scope-design.md`, mockup `docs/specs/2026-11-19-shoal-scope-mockup.png`. Background: `TESTING.md`, `CLAUDE.md`, `docs/plans/2026-11-15-shoal-friendly-mode.md` (the adapter pattern). Read the spec before any task. Where this plan and the spec disagree, the spec wins; ledger the difference.

## Global Constraints

- **Build:** `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64`. Agents start Rack only through `make smoke`. Before every `make install`, `mac_dist` or `cross_build`: `tools/bump_version.sh "<what changed>"`; before installing, remove older `rka-*` packages from Rack's plugins folder (#45).
- **Repo:** unit tests only; no audio, no test results beyond unit tests.
- **Firmware:** `src/shoal/firmware/` changes only with a `// SHOAL:` mark and a `CHANGES.md` line; `make shoal-globals` stays clean. Prefer reading app state from `ScopeView.inc` (it is included where the app classes are visible) over changing the firmware.
- **Threads:** `Core::scopeView` runs on the engine thread with the core's Context current (as `friendlyModel`): no allocation, no locks, no libc formatting, no writable globals. `Host` publishes under the existing `frameMutex_` `try_lock` only.
- **`Core.hpp`** stays the only core header the plugin includes; `ScopeView.hpp` is Rack-free, C++11, includes no firmware header, and holds no pointer that is not to static storage.
- **Views by app** (app index: 0 CopierMaschine, 1 Harrington 1200, 2 Automatonnetz, 3 Quantermain, 4 Meta-Q, 5 Quadraturia, 6 Low-rents, 7 Piqued, 8 Sequins, 9 Dialectic Ping Pong, 10 Viznutcracker sweet, 11 Acid Curds, 12 References):

  | Apps | `kind` | `lanes` | Labels |
  |---|---|---|---|
  | 5, 7, 9 | Traces | Rows4 | A, B, C, D |
  | 6 | XY | Halves | "A × B", "C × D" |
  | 2 | GridCircle | None | |
  | 1 | CircleText | None | |
  | 0, 3 | Pixels | Columns4 | A, B, C, D |
  | 12 | Pixels | Columns4Top (rows 0 to 43) | A, B, C, D |
  | 4, 8 | Pixels | Halves | "A", "B" |
  | 10 | Pixels | Rows4 | A, B, C, D |
  | 11 | Pixels | None | |

- **Traces:** 256 columns of 260 ticks each (min and max of the output over the column), newest at the right; each lane spans −5 V (bottom) to +5 V (top), clipped to the lane; a centre line at 0 V.
- **X/Y:** 512 points per pair, one every 32 ticks (plain samples); higher voltage right and up; each axis spans the output range −3.75 V to +6.25 V; newest brightest, fading to 15 % at the oldest.
- **Note circle:** twelve positions, C at the top, clockwise by semitone.
- **Hardware mode:** the firmware's scope frame exactly as today; no dividers, letters or smooth views.
- **Stale data:** after an app change, a restore or a reset, neither a view nor a history from before it is published.
- **Colours** from `RkaTheme` only (SIGNAL at reduced alpha for dividers 40 %, letters 65 %, centre lines 15 %); drawn in layer 1, as `PixelScreen`.
- **Testing rules:** every test is shown to fail first (TESTING.md); name the mutation in the ledger.

## Review Focus

1. **An app change or restore mid-history:** the new app's lanes must not show the old app's traces (Quadraturia to Piqued, Low-rents to anything).
2. **Sample rates where ticks and samples do not line up** (8 kHz: two ticks a sample; 768 kHz: a tick every 46 samples): a column is still 260 ticks and a spike of one tick still shows.
3. **An output outside a lane's ±5 V** (Dialectic Ping Pong reaches +6.25 V): the trace clips at its own lane's edge and never enters the lane above.
4. **The UI holding the frame lock for many ticks:** the published history is never torn (head and data from the same instant), and the engine never waits.
5. **The module browser, the app menu open, and a mode switch between frames:** no module shows the preview host's view with friendly-mode drawing; the app menu changes nothing; switching UI MODE never draws a smooth view over stale pixels or the reverse.

Each has a test in the task named beside it below.

---

### Task 0: Carry the manual's patches

- [ ] **Step 1:** Commit the uncommitted "Patches to try" blocks in `docs/shoal/README.md` on their own (`docs: Shoal patches to try per app`), with the already staged spec and mockup in a second commit (`Shoal scope: design`). Nothing else changes.

### Task 1: Strip dividers on the panel

**Files:** Modify `tools/gen_panels.py` (the `"Shoal"` entry, and the screens' drawing if it has no per-screen option); regenerated `res/shoal/`, `res-src/shoal/`.

- [ ] **Step 1:** Add three vertical dividers inside the STRIP screen at x = 4.0 + 83.2 × k/4 mm (k = 1, 2, 3), from 0.8 mm below the strip's top to 0.8 mm above its bottom, in the dim signal colour the rulers use, 0.2 mm wide. If screens have no option for inner lines, add one (`dividers={"STRIP": 4}`) honoured by the screen-drawing code for both themes.
- [ ] **Step 2:** `make panels && make panels-test`. Expected: pass; `git diff --stat` shows only Shoal's four SVGs and the generator.
- [ ] **Step 3:** `make && make smoke`; read `build/screenshots/Shoal.png`. Expected: three faint lines between the four readouts, none touching the bezel, readouts not overlapped.
- [ ] **Step 4:** Commit.

### Task 2: The view description

**Files:** Create `src/shoal/ScopeView.hpp`, `src/shoal/ScopeView.inc`, `src/shoal/test_view.cpp`. Modify `src/shoal/Core.hpp`, `src/shoal/CoreImpl.inc`, `src/shoal/Firmware.cpp` (include order, after the apps and `Friendly.inc`), `src/shoal/test_faults.cpp`, `src/shoal/test_ui.cpp`.

**Interfaces — Produces:**

```cpp
// ScopeView.hpp
namespace shoal {
enum class ViewKind : uint8_t { Pixels, Traces, XY, GridCircle, CircleText };
enum class Lanes : uint8_t { None, Columns4, Columns4Top, Halves, Rows4 };
constexpr int kViewPath = 4, kViewText = 6, kGridSide = 5;
struct ScopeView {
	uint8_t app;                    // index into Core::appName
	ViewKind kind;
	Lanes lanes;
	uint8_t notes[3];               // GridCircle, CircleText: pitch classes 0..11
	uint8_t pathX[kViewPath], pathY[kViewPath];  // GridCircle: cells 0..4, newest first
	char transforms[4][kViewText];  // CircleText: "+P", "-L" ..., newest first, as the firmware prints them
	char chord[3][kViewText];       // CircleText: "C4", "D#3" ..., as the firmware prints them
};
}
// Core.hpp
void scopeView(ScopeView& out) const;   // never touches firmware state
```

- [ ] **Step 1: Failing tests** in `test_view.cpp` (a fresh `Core` per case, `seed(1)`):
  - `kinds_and_lanes`: for each app, `selectApp`, run 1000 ticks, `scopeView`: `app`, `kind` and `lanes` equal the Global Constraints table; unused fields are zero.
  - `automatonnetz_path_matches_frame`: app 2, clock TRIG 1 twelve times (gate 5 V for 20 ticks, low for 400). After each clock: `drawScope`, and for each i the firmware's frame has the disc the firmware draws at cell (`pathX[i]`, `pathY[i]`): the pixel at the cell's centre is lit for i = 0 (a filled disc) and the ring pixel three to the left is lit for every i. Take the centre's pixel coordinates from `kScreenSaverGridX/Y/W/H` in `APP_AUTOMATONNETZ.ino`, written into the test as numbers. Also: consecutive clocks shift the path (`path[i+1]` is the previous `path[i]`).
  - `circle_notes_match_frame`: apps 1 and 2, after each of twelve triggers: the frame has a lit pixel within 2 px of centre + r·(sin, −cos)(2π·note/12) for each of `notes[0..2]` (take the firmware's radius r, `note_circle_r`, and the two centres from `OC_menus.cpp` and the apps; the angle formula is the test's own, not the firmware's table).
  - `harrington_text_matches_outputs`: app 1, triggers on TRIG 2, 3, 4 in turn: `chord[i]`'s note name equals the pitch class of `round(out(i + 1) × 12)` (names C, C#, D ... B), and raising the "Octave" setting by 1 raises each octave digit by 1; `transforms[0]` is "+P" or "-P" after a TRIG 2, "L" after TRIG 3, "R" after TRIG 4 (sign as the firmware's history bit).
  - `view_changes_nothing` (add to `test_ui.cpp`): the scripted session of `outputs_ignore_drawing`, with `scopeView` called every 17 ticks on one core and never on the other: outputs and menu frame hashes equal.
- [ ] **Step 2:** Run `make build/test/shoal/test_view`. Expected: does not link (`Core::scopeView` undefined).
- [ ] **Step 3:** Implement `ScopeView.inc` (namespace `shoal::view`, a `switch` on `OC::apps::index_of(OC::global_settings.current_app_id)`, zero the struct first) and `Core::scopeView` in `CoreImpl.inc` with `Scope scope(impl_->ctx.get())`. Automatonnetz: `automatonnetz_state.history()` and `tonnetz_state.get_outputs`, as `Automatonnetz_screensaver` reads them. Harrington: `h1200_state.tonnetz_state`, as `H1200_screensaver` (including its negative-note fix). Text through the friendly code's `Text` helper or by hand; no `snprintf`.
- [ ] **Step 4:** Run `test_view` and `test_ui`. Expected: PASS. Then one mutation per test (wrong lanes for one app; path order reversed; notes off by one; octave not added; a `scopeView` that moves the menu cursor), each seen to fail, then restored.
- [ ] **Step 5:** In `test_faults.cpp`, call `scopeView` wherever `drawScope` is called (the sweep and the fixed cases). `make test` then `make shoal-fuzz`. Expected: PASS, `shoal-globals` clean.
- [ ] **Step 6:** Commit.

### Task 3: The output history and publishing

**Files:** Modify `src/shoal/ScopeView.hpp`, `src/shoal/Host.hpp`, `src/shoal/test_host.cpp`.

**Interfaces — Consumes:** `Core::scopeView`. **Produces:**

```cpp
// ScopeView.hpp
constexpr int kTraceColumns = 256, kTraceColumnTicks = 260, kXyPoints = 512, kXyPointTicks = 32;
struct ScopeHistory {
	// Oldest first; `columns` and `points` valid entries (fewer until the rings fill).
	float lo[kNumChannels][kTraceColumns], hi[kNumChannels][kTraceColumns];
	float xy[kNumChannels][kXyPoints];
	int16_t columns, points;
};
// Host.hpp, UI thread: the view and history published with the latest frames.
void scope(ScopeView& view, ScopeHistory& history);
```

`kNumChannels` moves to (or is repeated with a `static_assert` in) `ScopeView.hpp`; builder's choice. Host keeps engine-side rings with heads and unrolls them oldest-first into the published copy when a frame is due.

- [ ] **Step 1: Failing tests** in `test_host.cpp` (Host at `setSampleRate(kTickHz)`, so one tick per `process()`, unless stated):
  - `history_matches_outputs`: Quadraturia (app 5, posted as the tests there select apps), 3 s. The test records `host.out(ch)` after every `process()`; after the run, the last 200 complete columns of `lo`/`hi` equal the test's own min and max over the same 260-tick windows, and the last 400 `xy` points equal its own every-32nd samples (alignment: columns and points count ticks from the Host's construction or its last clear).
  - `history_fills_then_scrolls`: `columns` and `points` are 0 on a new Host, grow to 256 and 512, and stay; the newest entry is last.
  - `spike_shows` (Review Focus 2): Piqued (app 7) with a very short envelope, one trigger, at 8 kHz, 48 kHz and 768 kHz: exactly the columns covering the envelope have `hi` above 1 V; `lo` of the same lane stays at rest in the columns around it.
  - `history_cleared_on_change` (Review Focus 1): run Quadraturia 2 s, select Piqued: the first `scope()` whose `view.app` is 7 has `columns` ≤ 1 and `points` ≤ 9 (one frame's worth). The same after `requestRestore` and `requestReset`.
  - `publish_not_torn` (Review Focus 4): a second thread calls `scope()` in a tight loop for 2 s of engine time while the engine runs Quadraturia; every copy it gets has `lo ≤ hi` everywhere, no value outside −3.76 V to +6.26 V, and `view.app == 5`. (Built with the sanitizers; add `-fsanitize=thread` only if the existing host tests already do.)
- [ ] **Step 2:** Run. Expected: does not compile (`scope` undefined).
- [ ] **Step 3:** Implement in `Host.hpp`: accumulate per tick in `afterTick` (four `core_->out(ch)` reads), close a column every 260 ticks and an XY point every 32; in `drawFrames` call `core_->scopeView(engineView_)`, clear the rings when `engineView_.app` differs from the last drawn app, and unroll into `engineHistory_`; `applyRestore` clears the rings; publish view and history in the `frameMutex_.try_lock()` block; the constructor publishes an empty history and the first view.
- [ ] **Step 4:** Run `test_host`. Expected: PASS. Mutations, each seen to fail: column length 256 ticks; min and max swapped; no clear on app change; unroll from the wrong head.
- [ ] **Step 5:** `make shoal-bench`. Expected: no app's share rises by more than 0.02 of a core from the figures in `TESTING.md` (rule 10); if one does, unroll only the columns that changed. Update the figures in `TESTING.md` if they moved.
- [ ] **Step 6:** `make test`. Commit.

### Task 4: The geometry

**Files:** Create `src/shoal/ScopeLayout.hpp`, `src/shoal/test_scope.cpp`.

**Interfaces — Consumes:** `ScopeView.hpp`. **Produces** (namespace `shoal::scope`, all `inline`, Rack-free; `Box` as `list::Box`, moved to a shared spot or redeclared, builder's choice):

```cpp
struct Point { float x, y; };
struct Line { Point a, b; };
struct Label { Point at; const char* text; };   // top-left anchor; static storage
struct Lanes4 { int count; Box lane[4]; int dividers; Line divider[3]; Label label[4]; };
Lanes4 lanes(const ScopeView& v, float w, float h);       // by v.kind and v.lanes; count 0 for None
Point tracePoint(const Box& lane, int column, int columns, float volts);   // newest column at lane.right()
float traceZeroY(const Box& lane);
Point xyPoint(const Box& half, float xVolts, float yVolts);
float xyAlpha(int point, int points);                       // 0.15 oldest .. 1 newest
Point circleNote(Point centre, float r, int pitchClass);    // C at top, clockwise
Point gridCell(const Box& area, int x, int y);              // 5 x 5, evenly spaced, inset
Box gridArea(float w, float h);   Box circleArea(float w, float h);   Box textArea(float w, float h);
```

- [ ] **Step 1: Failing tests** in `test_scope.cpp`, for a 128 × 64 and a 245.7 × 122.8 screen:
  - `lanes_tile_the_screen`: for every app's view (build the nine rows of the Global Constraints table by hand): lanes do not overlap, lie inside the screen, and together cover it (Columns4Top: cover rows 0 to 43/64 of the height only); `dividers == count − 1`; each divider lies on the shared edge of two lanes and is inset from the bezel by at least 1/64 of the height; each label sits inside its own lane; label texts as the table.
  - `trace_clips_to_lane` (Review Focus 3): `tracePoint` at −5 V is the lane's bottom, +5 V its top, 0 V `traceZeroY`; +6.25 V and −3.75 V give y inside [lane.y, lane.bottom()]; column `columns − 1` is at `lane.right()`, column 0 at least a label's width (the layout's own constant) right of `lane.x`; with `columns` 10 of 256 the points still end at the right edge.
  - `xy_orientation`: higher x volts is further right, higher y volts further up; (−3.75, −3.75) and (+6.25, +6.25) are inside the half; (1.25, 1.25) is its centre; `xyAlpha(0, n) == 0.15f`, `xyAlpha(n − 1, n) == 1.f`, monotonic.
  - `circle_and_grid`: `circleNote` 0 is straight above the centre, 3 to the right, 6 below, 9 left, each at distance r; grid cells are evenly spaced, (0,0) top left, inside `gridArea`; `gridArea` and `circleArea` (and `circleArea` and `textArea`) do not overlap.
- [ ] **Step 2:** Run `make build/test/shoal/test_scope`. Expected: does not compile.
- [ ] **Step 3:** Implement. Every size is a share of w and h (as `ListLayout.hpp`), so it fits any zoom.
- [ ] **Step 4:** Run. Expected: PASS. Mutations seen to fail: Rows4 and Columns4 swapped; no clamp in `tracePoint`; x mirrored in `xyPoint`; circle anticlockwise.
- [ ] **Step 5:** `make test`. Commit.

### Task 5: The widget, then the look stop

**Files:** Create `src/shoal/ScopeScreen.hpp`. Modify `src/shoal/Shoal.cpp`.

**Interfaces — Consumes:** `Host::scope`, `Host::frames`, `shoal::scope::*`, `rka::PixelScreen`.

- [ ] **Step 1:** `struct ScopeScreen : rka::PixelScreen` with `std::function<bool(ScopeView&, ScopeHistory&)> viewSource` and `bool friendly`. `drawLayer(layer 1)`:
  - not friendly, or no `viewSource`: `PixelScreen::drawLayer` only;
  - `Pixels`: `PixelScreen::drawLayer`, then dividers (1 px at the current zoom, SIGNAL at 40 %) and labels (Share Tech Mono as the strip, about 1/9 of the height, SIGNAL at 65 %, no backing plate);
  - `Traces`: per lane the divider, label, centre line (15 %), then one stroked path through the column midpoints; where a column's `hi − lo` maps to more than the stroke width, a filled min-max band for that column;
  - `XY`: divider, labels, and each half's curve stroked in eight alpha steps from `xyAlpha`;
  - `GridCircle`: 25 faint grid dots, the path's lines, rings for older cells and a filled disc for the newest; the circle, twelve faint marks, the chord triangle, discs and note names;
  - `CircleText`: the circle as above on the left; on the right the four transforms and the three chord names in two columns, as the firmware lays them out.
  The history copy (16 kB) lives in the widget, not on the stack.
- [ ] **Step 2:** In `Shoal.cpp`, the SCOPE screen becomes a `ScopeScreen` (keep `addScreen` for the menu); `step()` sets `scopeScreen->friendly` with the same value it gives `friendlyList` (Review Focus 5: one source for both). The preview host (no module) feeds it too.
- [ ] **Step 3:** `make test && make && make smoke`. Read `build/screenshots/Shoal.png`. Expected: CopierMaschine's pixels with three column dividers and A to D letters; strip dividers; nothing drawn outside the bezel.
- [ ] **Step 4:** Commit. Then `tools/bump_version.sh "Shoal: upper screen lanes and smooth views"`, clear old `rka-*` packages from Rack's plugins folder, `tools/mac_dist.sh`, install the Mac package; `tools/cross_build.sh win-x64` and copy the Windows package to `/Volumes/share1/VCVRACK/`.
- [ ] **Step 5: USER STOP (the look).** Ask the user to look, in friendly mode, at: Quadraturia (traces), Piqued (a fast envelope: the spike), Dialectic Ping Pong (top of lane), Low-rents (X/Y), Automatonnetz with a clock, Harrington 1200 with triggers, Quantermain, Meta-Q, References, Viznutcracker sweet; then hardware mode on any of them; at 100 % and 200 % zoom. Collect changes to line weight, brightness, label size and anything unclear. Apply them (geometry changes go through `ScopeLayout.hpp` and its tests) and repeat from Step 3 with a new version until the user accepts.

### Task 6: Manual, changelog, release checks

**Files:** Modify `docs/shoal/README.md`, `docs/shoal/Shoal.png`, `README.md` (only if its Shoal line needs it), `CHANGELOG.md`, `TESTING.md` (bench figures, if changed).

- [ ] **Step 1:** `docs/shoal/README.md`, "Screens": replace "It is the same in both modes" with: what the upper screen shows in friendly mode (the table of apps by view, how to read lanes and letters, the traces' 4 s and ±5 V per lane, X/Y's axes and 1 s tail, that Piqued's shape preview is hardware-mode only, that Low-rents is mirrored against a unit), that hardware mode shows the unit's own screen, and the strip's cells. Keep the manual's plain style.
- [ ] **Step 2:** New `docs/shoal/Shoal.png` from the smoke screenshot, as the other manuals' images are made.
- [ ] **Step 3:** `CHANGELOG.md`: the change under the next release, as earlier entries.
- [ ] **Step 4:** `make test && make && make smoke`; the Linux test run and `tools/cross_build.sh win-x64 lin-x64` on the final commit (TESTING.md rule 11). Expected: all pass; each package holds `res/shoal`.
- [ ] **Step 5:** Commit. Final whole-branch review, then superpowers:finishing-a-development-branch.

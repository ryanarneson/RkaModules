# Shoal Friendly Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Shoal's lower screen becomes a mouse-driven list of the current app's settings (sliders, dropdowns, note-mask cells, channel tabs, a click-to-pick app list), on by default, with a UI MODE control to switch back to the firmware's own menu.

**Architecture:** The firmware core gains a "friendly" view of the current app: a `FriendlyModel` (tabs and rows, built from the firmware's own setting tables and visible-settings lists by one small adapter per app) and `FriendlyEdit`s that the adapter carries out by putting the firmware cursor on the row and calling the app's own encoder and button handlers. `shoal::Host` publishes the model with the frames and queues edits from the UI thread. The Rack module draws the model in place of the editor screen when UI MODE is on. Both modes are views of one firmware state.

**Tech Stack:** C++11 for plugin code (Rack SDK 2.6.6), C++17 for the firmware unit and tests, Python 3 panel generator, Inkscape for `make panels`, GNU make 3.81-compatible makefiles.

**Spec:** `docs/specs/2026-11-14-shoal-design.md` — "Friendly mode", starting with "Revised 2026-11-15, after stage 2", which wins where older text disagrees. Per-app facts: `docs/specs/2026-11-15-shoal-app-survey.md` (read from the firmware source; where it and the source differ, the source is right and the report says so). Reference look: `~/ws/reference/o_c.png`.

## Global Constraints

- Module slug `Shoal`, folder `src/shoal/`. Never hand-edit `res/`, `res-src/`, `*Layout.hpp` or `src/RkaTheme.hpp`; change `tools/gen_panels.py` and run `make panels` (Inkscape at `/opt/homebrew/bin/inkscape`). Regenerating leaves the Sway and Fathom outputs byte-identical.
- Nightglow look: colours from `RkaTheme`, shared widgets in `src/widgets.hpp`. No stock knobs, no ad-hoc colours.
- Firmware in `src/shoal/firmware/` changes only with a `// SHOAL:` mark on the line and a line in `src/shoal/firmware/CHANGES.md`. Prefer reaching firmware state from `src/shoal/*.inc` (compiled in the same unit) over editing firmware files. `make shoal-globals` stays clean.
- **A friendly edit never leaves an app in a state the encoders could not reach.** Edits go through the app's own handlers (see Task 1). `Core::setSettingRaw` is not used by friendly mode.
- **The list is generated from the firmware's tables.** Labels, ranges, defaults and value names come from `settings::value_attr` and the app's enabled-settings list. An adapter supplies text only where the firmware has none (an encoder-only function, or a value-name array shorter than its range).
- The engine thread never blocks: no `lock()`, no allocation, no I/O in anything `Host::process()` reaches. It may `try_lock`.
- One `shoal::Core` is touched by one thread at a time. `Host.hpp`, `Core.hpp` and `Friendly.hpp` are C++11, Rack-free, and include no firmware header.
- The saved-patch format does not change: `{ "version": 1, "state", "inputRange" }`. `testGoldenImageV1` and `test_patch` pass unchanged; the golden image is never regenerated.
- Out of scope (stage 4 or later): mouse versions of the sub-editors (user scale notes, Sequins steps, Acid Curds chords, Piqued segments, Automatonnetz cells); the right-click menu; References meters; the full manual. Firmware bugs the survey found (for example References "Mod range oct" losing negative values on save) are left as the hardware has them and noted in the docs.
- Scripts stay portable across macOS and Linux. Makefiles work on GNU make 3.81. New offline tests need no Rack SDK and build under `make -f tests.mk test TEST_WERROR=-Werror CXX=g++`.
- Before calling a task done: `make test`, `make`, `make smoke`, `make shoal-globals`; for panel or widget changes look at `build/screenshots/Shoal.png`. Build with `RACK_DIR=$HOME/ws/sdk/Rack-SDK-2.6.6-mac-arm64`. Start Rack only through `make smoke`.
- Commit per task on the working branch; never push. Commit messages start `Shoal: ` and end with a `Co-Authored-By:` trailer naming the model that wrote the commit.

## Review Focus

1. **Dragging a slider fast across its whole range while the app is being clocked**: no crash, no blocked audio, the value lands where the pointer stopped or where the firmware's guard stopped it, and the list shows the real value. Task 1 (`testEditStorm`), Task 5 (`testEditsFromUiThread`).
2. **The app changes under an open list or an open dropdown** (app picked from the header, a patch loaded, Initialize): an edit made for the old app is dropped, never applied to the new app's row of the same number. Task 1 (`testStaleEditDropped`), Task 5 (`testEditAfterRestore`).
3. **Scales of every length**: a note-mask row has exactly as many cells as the selected scale has notes (4 to 16 for user scales, 12 for Semitone, none for Off), follows a scale change at once, and never lets the last active note be cleared if the firmware does not. Task 3 (`testMaskCells`).
4. **A patch saved by stage 2** (no UI MODE control in it): loads in friendly mode with the same app and settings. Task 6 (param default; `testGoldenImageV1` unchanged; manual check).
5. **The two modes agree**: after any friendly edit, hardware mode shows the same value on the same row with the cursor there, and after any encoder edit the list shows it within a frame. Tasks 1–4 (the equivalence tests), Task 5 (`testModelFollowsEncoders`).

---

### Task 1: The friendly model, edits, and the first two adapters

**Files:**
- Create: `src/shoal/Friendly.hpp`, `src/shoal/FriendlyAdapters.inc`, `src/shoal/test_friendly.cpp`
- Modify: `src/shoal/Core.hpp`, `src/shoal/CoreImpl.inc`, `src/shoal/CoreTest.hpp`, `src/shoal/Firmware.cpp`, `tests.mk` (only if the new header needs a dependency line), `README.md` layout listing

**Interfaces:**
- Consumes: `shoal::Core`, `SettingsAccess.inc` (`kAppSettings`), the app state globals and cursors named in the survey's "Menu cursor" section.
- Produces, in `src/shoal/Friendly.hpp` (Rack-free, C++11, included by `Core.hpp`):

```cpp
namespace shoal {
constexpr int kMaxRows = 72, kMaxTabs = 4, kRowText = 12, kStatusText = 24;
enum class RowKind : uint8_t {
	Number,     // slider: value in [min, max]
	Choice,     // dropdown: names[value - min], nameCount entries
	Toggle,     // on/off: value 0 or 1
	Mask,       // toggle cells: value is the bit mask, max is the number of cells (0: none)
	Action,     // a button: Press does it; value unused
	SubEditor,  // edited in hardware mode only
	Heading     // a section title, no control
};
enum : uint8_t { kRowCursor = 1, kRowEditing = 2, kRowExtra = 4 };
struct Row {
	RowKind kind;
	uint8_t flags;
	int16_t id;                  // stable for the app: setting index, or >= 256 for an extra row
	const char* label;           // static storage
	int32_t value, min, max, def;
	const char* const* names;    // Choice; static storage
	int16_t nameCount;
	char text[kRowText];         // the value as the firmware prints it; empty: show the number or the name
};
struct FriendlyModel {
	uint8_t app;                 // index into Core::appName
	uint8_t tabs, tab;           // channels and the firmware's selected one; tabs <= 1: no tab bar
	const char* tabLabel[kMaxTabs];
	char status[kStatusText];    // the title bar's live text (freeze, clock, frequency); may be empty
	int16_t rows;
	Row row[kMaxRows];
};
struct FriendlyEdit {
	enum Type : uint8_t { SetValue, ToggleBit, Press, SelectTab, Focus };
	Type type;
	uint8_t app;                 // the app the UI drew; an edit for another app is dropped
	uint8_t tab;                 // the tab the row belongs to
	int16_t id;                  // Row::id; unused for SelectTab
	int32_t value;               // SetValue: target; ToggleBit: bit number; SelectTab: tab
};
}
```

  - In `Core`: `void friendlyModel(FriendlyModel& out) const;` and `bool friendlyEdit(const FriendlyEdit& e);` (false: dropped, because the app or tab no longer matches, the row is not present, or the app menu is open).
  - In `CoreTest`: `static void encoderEvent(Core&, Encoder, int delta);` and `static void buttonEvent(Core&, Button, bool longPress);`, which hand one event straight to the current app's handler, with no debounce and no acceleration. The equivalence tests use these as "what the encoders would do".
  - In `FriendlyAdapters.inc`: one adapter per app in `available_apps` order, with a table and a `static_assert` that there are `NUM_AVAILABLE_APPS` of them, the same shape as `kAppSettings`. Until Tasks 2–4 fill them in, apps other than Low-rents and Quadraturia use the generic adapter.

**How an adapter works (binding for Tasks 1–4):**
- **Rows.** For each tab (settings object), the rows are the app's enabled-settings list in menu order if it publishes one, otherwise the whole table. Kind comes from the table entry: value names present → `Choice`; else min 0 and max 1 → `Toggle`; else `Number`. The adapter overrides the kind where the survey says so (`Mask`, `SubEditor`) and appends extra rows (`id >= 256`, `kRowExtra`). Apps with two menu pages (Sequins, Acid Curds) list both pages, each under a `Heading`.
- **Cursor flags.** The row under the firmware's menu cursor carries `kRowCursor`, plus `kRowEditing` when the cursor is in edit mode.
- **`SetValue` on a setting row.** Select the tab as the app's channel control would; switch menu page if needed as the app's page control would; move the cursor to the row (`Scroll(row - cursor_pos())`); put the cursor in edit mode the way the app's right-push handler does; send the app's right-encoder handler one event carrying `target - current`; leave the cursor on the row, not editing. Where the survey shows a guard that only looks at single steps (Quantermain and Meta-Q trigger/source), send unit steps instead so the guard sees each one. If the handler refuses or clamps, the model simply shows the result.
- **Extra rows and `Press`.** Sent as the same handler events the hardware control produces (for example CopierMaschine's scale: left-encoder events to the target, then a left push).
- **`ToggleBit`.** Calls the function the app's sub-editor calls to write the mask, for the selected channel only unless the sub-editor writes all channels.
- **`Focus`.** Moves the firmware cursor to the row, nothing else.
- **Dropped edits.** `e.app` differs from the current app, the app menu is open, `e.tab` is out of range, or the row id is not in the current model.
- **No allocation, no locks.** Building a model and applying an edit are both engine-thread operations.

**Adapters in this task:** Low-rents and Quadraturia (one tab each, no enabled list). Both have settings the menu never shows because the left encoder owns them (Low-rents frequency 1 and 2; Quadraturia coarse and fine); those are ordinary rows here. Extra rows: Quadraturia "Phase reset" (`Action`, the DOWN long press).

- [ ] **Step 1: Write the failing tests** in `src/shoal/test_friendly.cpp`:
  - `testGenericModel`: for every app, `friendlyModel` gives `app` equal to `Core::app()`, `rows` between 1 and `kMaxRows`, every row a non-null label, `min <= value <= max` for Number/Choice/Toggle rows, `nameCount == max - min + 1` for Choice rows, distinct ids, and exactly one `kRowCursor` row unless the cursor is on a row the model does not list.
  - `testLowRentsEquivalence`, `testQuadraturiaEquivalence`: two cores with one seed. For each row and for the targets min, max, default and three values in between: core A gets `friendlyEdit(SetValue)`; core B gets the same change through `CoreTest::encoderEvent`/`buttonEvent` as the survey describes the hardware gesture. Then every setting on every channel matches, the two models match, and after 2000 ticks with the same inputs all four `outCode`s match.
  - `testQuadraturiaPhaseReset`: `Press` on the extra row equals a DOWN long press.
  - `testFocusMovesCursor`: after `Focus` on row n, `CoreTest::menuSetting` (extended to all apps, or a new hook) reports that row, and `drawMenu` shows it highlighted.
  - `testStaleEditDropped`: an edit built for app 6, applied after `selectApp(3)`, returns false and changes no setting; the same with the app menu open; with a tab out of range; with an id not in the model.
  - `testEditStorm`: 20,000 random valid and invalid edits on each app, clocked throughout, under the sanitizers: no fault, and every model stays well-formed.
- [ ] **Step 2: Run** `make test`; expected: `test_friendly` fails to compile (no `Friendly.hpp`).
- [ ] **Step 3: Implement** `Friendly.hpp`, the adapter table with the generic adapter, the two adapters, the `Core` members and the `CoreTest` hooks.
- [ ] **Step 4: Run** `make test`, `make shoal-globals`, `make shoal-bench` (model built once per 278 ticks must leave every app under 2 % of a core); expected: pass.
- [ ] **Step 5: Commit** `Shoal: friendly model and edits; Low-rents and Quadraturia`.

---

### Task 2: Adapters for Dialectic Ping Pong, Viznutcracker sweet, References, Harrington 1200, Automatonnetz

**Files:**
- Modify: `src/shoal/FriendlyAdapters.inc`, `src/shoal/test_friendly.cpp`

**Interfaces:**
- Consumes: Task 1's adapter rules and test harness.
- Produces: adapters for the five apps; no API change.

Per app, from its survey section:
- **Dialectic Ping Pong, Viznutcracker sweet, References:** four tabs A–D following the firmware's selected channel. Viznutcracker's "Loop mode" changes which rows are visible; the list follows.
- **Harrington 1200:** one tab. "Trigger type" changes visibility; "Root mode" has a follow-up that the handler performs. Extra row: "Reset" (`Action`, the left long press).
- **Automatonnetz:** one tab, grid-level settings. Extra rows: "Reset" (UP), "Clock" (DOWN), "Clear grid" (left long press), and "Cells" as a `SubEditor` row.
- Extra rows follow the rule: every function in the survey's point 4 that is not a row gets one, except channel selection (tabs), page switches (sections), view toggles, shortcuts to a setting that already has a row, and sub-editors (`SubEditor` rows). The report lists, per app, each function and what became of it.
- Channel defaults that differ per channel (trigger inputs TR1–TR4): `Row::def` is the table default; the test below pins what double-click will do.

- [ ] **Step 1: Write the failing tests**: `test<App>Equivalence` for each of the five, as in Task 1, on every tab; `testTabsFollowFirmware` (a `SelectTab` edit equals the app's channel control, and an encoder channel change shows in `model.tab`); `testVisibilityFollows` (Viznutcracker Loop mode and Harrington Trigger type: after the edit the model's rows equal the firmware's enabled list); `testActions` (each `Action` row equals its hardware gesture).
- [ ] **Step 2: Run** `make test`; expected: the new tests fail on the generic adapter (tabs, extras or follow-ups missing).
- [ ] **Step 3: Implement** the five adapters.
- [ ] **Step 4: Run** `make test`, `make shoal-globals`; expected: pass.
- [ ] **Step 5: Commit** `Shoal: friendly adapters for five apps`.

---

### Task 3: Adapters for CopierMaschine, Quantermain, Meta-Q (scales and note masks)

**Files:**
- Modify: `src/shoal/FriendlyAdapters.inc`, `src/shoal/test_friendly.cpp`

**Interfaces:**
- Consumes: Task 1's rules; `OC::Scales::GetScale(scale).num_notes` for cell counts.
- Produces: adapters for the three apps; `RowKind::Mask` rows in use.

- **CopierMaschine:** one tab. Extra rows first, as in the reference: "Scale" (`Choice` over the scale names; left-encoder preview then left push, so the firmware's `set_scale` runs) and "Hold / Freeze" (`Toggle`, DOWN); also the delay-mechanics toggle (DOWN long press). "mask" is a `Mask` row. "CV source" changes visibility.
- **Quantermain:** four tabs. "Active notes" is a `Mask` row written through `update_scale_mask` for the selected channel. Scale, CV source and trigger changes rebuild the visible list. Trigger/source rows step by one so the firmware's combination guard applies.
- **Meta-Q:** two tabs. Four scale slots with a mask each; "scale #" and "seq_mode" need the handler's `reset_scale`. CV source has five values and four names: the adapter supplies the fifth label ("Turing", per the survey). Same stepping rule as Quantermain.
- User-scale note editing stays a `SubEditor` row.

- [ ] **Step 1: Write the failing tests**: the three `test<App>Equivalence`; `testMaskCells` (for Off, Semitone, a 5-note scale, a 7-note scale, and user scales set to 4 and 16 notes through `CoreTest::userScaleNumNotes`: the Mask row's `max` equals the scale's note count, changes at once when the scale row changes, and toggling each cell equals what the scale editor's own toggle does to the mask and to the quantizer's output; clearing the last active note behaves as the scale editor does); `testCopierScaleCommit` (the Scale row leaves `ASR_SETTING_SCALE`, the mask and the title bar as preview-then-push does); `testTriggerSourceGuard` (the combinations the firmware blocks stay blocked through the list).
- [ ] **Step 2: Run** `make test`; expected: fail.
- [ ] **Step 3: Implement** the three adapters.
- [ ] **Step 4: Run** `make test`, `make shoal-globals`; expected: pass.
- [ ] **Step 5: Commit** `Shoal: friendly adapters for the quantizer apps`.

---

### Task 4: Adapters for Piqued, Sequins, Acid Curds

**Files:**
- Modify: `src/shoal/FriendlyAdapters.inc`, `src/shoal/test_friendly.cpp`

**Interfaces:**
- Consumes: Task 1's rules.
- Produces: adapters for the last three apps; every app now has its own adapter and the generic one remains only as the base they share.

- **Piqued:** four tabs. Euclidean offset, fill and length keep the firmware's bounds because the handler enforces them. "delay mode" and "Eucl length" change visibility. Trigger input has seven values and four names: the adapter labels the rest as the other channels' end-of-cycle triggers, per the survey. Segment shapes: `SubEditor`.
- **Sequins:** two tabs; two sections, "Parameters" and "CV mapping", each the firmware's page of that name, built by reading the page's enabled list (switching page and back inside one model build is allowed only if it leaves the cursor and page as they were). The step mask is a `Mask` row written through the function the pattern editor uses (a raw write has no effect in this app). Extra rows: "Sync" (left push), "Copy scale to other channel" (left long press), "Clear CV mapping" (DOWN long press). Step pitches: `SubEditor`.
- **Acid Curds:** one tab; the same two sections. Extra row "Scale" as in CopierMaschine (preview then push). `CHORD_SLOT` has 32 values and 8 names: the adapter shows the number. Chord contents: `SubEditor`.

- [ ] **Step 1: Write the failing tests**: the three `test<App>Equivalence` (both pages); `testPiquedEuclidBounds` (offset ≤ length, fill ≤ length + 1, and shortening length pulls both down, through the list as through the encoder); `testSectionsLeaveMenuAlone` (building the model 1000 times while on each page changes neither the firmware's page, cursor nor a `drawMenu` frame); `testSequinsMask`; `testEveryAppHasAnAdapter` (no app resolves to the bare generic adapter).
- [ ] **Step 2: Run** `make test`; expected: fail.
- [ ] **Step 3: Implement** the three adapters.
- [ ] **Step 4: Run** `make test`, `make shoal-globals`, `make shoal-fuzz`, `make shoal-bench`; expected: pass, every app under 2 % of a core with the model built at 60 Hz.
- [ ] **Step 5: Commit** `Shoal: friendly adapters for Piqued, Sequins and Acid Curds`.

---

### Task 5: `Host` publishes the model and queues edits

**Files:**
- Modify: `src/shoal/Host.hpp`, `src/shoal/test_host.cpp`

**Interfaces:**
- Consumes: `Core::friendlyModel`, `Core::friendlyEdit`.
- Produces, in `Host`:
  - `bool postEdit(const FriendlyEdit& e);` — UI thread; false when the queue is full. Edits and `ControlEvent`s keep their posting order relative to each other.
  - `void model(FriendlyModel& out);` — UI thread; the model published with the latest frames (built on the engine thread every `kFrameTicks`, handed over under the frame lock with `try_lock`, as the frames are).
  - The constructor publishes a model, so the UI has one before the engine first runs. After a restore or reset swap, the next published model is the new core's.

- [ ] **Step 1: Write the failing tests** in `test_host.cpp`: `testModelPublished` (a new Host has a model for app 0; after `post(selectApp(3))` and a run, `model().app == 3`); `testEditsApplied` (a posted `SetValue` shows in the model within two frame periods); `testEditOrder` (a `Turn`, an edit and a `Turn` posted in that order are applied in that order); `testEditAfterRestore` (an edit posted for the old app before `requestRestore` of an image with another app is dropped); `testModelFollowsEncoders` (encoder turns change the cursor row and the value in the model); `testEditsFromUiThread` (as `testThreads`: a UI thread posting edits and reading models for a second while the engine runs; the final value is the last one accepted); extend `testNoEngineAllocation` to cover model publishing and edits.
- [ ] **Step 2: Run** `make test`; expected: fail to compile.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `make test`, and a scratch ThreadSanitizer build of `test_host` six times; expected: pass, no race reports.
- [ ] **Step 5: Commit** `Shoal: Host publishes the friendly model and queues edits`.

---

### Task 6: UI MODE on the panel, and the list on the lower screen

**Files:**
- Create: `src/shoal/ListLayout.hpp` (Rack-free geometry), `src/shoal/test_list.cpp`, `src/shoal/FriendlyList.hpp` (the Rack widget)
- Modify: `tools/gen_panels.py` (Shoal entry), generated `res/shoal/*`, `res-src/shoal/*`, `src/shoal/ShoalLayout.hpp`; `src/widgets.hpp` (a latch in the `HoldButton` style, if `AmberLatch` does not fit the card); `src/shoal/Shoal.cpp`

**Interfaces:**
- Consumes: `Host::model`, `Host::postEdit`, `Host::post` (app selection), `ShoalLayout::SCREEN_EDITOR`.
- Produces:
  - Panel: param `UI_MODE` with its light, in the free third slot of the controls card (about x 129, y 55; the generator's clearance checks decide the exact spot), label "UI MODE". Layout names `PARAM_UI_MODE`, `LIGHT_UI_MODE`. No other component moves.
  - Module: `UI_MODE_PARAM` appended after the four button params (existing param ids keep their numbers), a latching switch, **default 1 (friendly)**, not randomised; `UI_MODE_LIGHT` lit when friendly. `releaseButtons()` still touches only the four buttons.
  - `ListLayout.hpp`, pure functions over a `FriendlyModel` and a size in pixels: the header, tab bar (absent when `tabs <= 1`), row and scrollbar rectangles; which row and which part (label, control, mask cell n) a point hits; a slider's value for an x position and back (with a fine mode at one quarter speed); scroll clamping. All sizes derive from the editor rectangle so the list fills exactly `SCREEN_EDITOR`.

**Behaviour of the list (friendly mode):**
- Header: "v" and the app name on the left, the model's `status` on the right, or "CLICK APP TO SWITCH" when it is empty. Clicking the header opens a Rack menu of the 13 apps with the current one ticked; choosing one posts `SelectApp`.
- Tabs: one per channel with the firmware's label; clicking posts `SelectTab`.
- Rows scroll with the wheel while the pointer is over the list, and with a draggable scrollbar. The row under the pointer and the row with `kRowCursor` are highlighted differently.
- `Number`: a bar filled to the value with the value printed on it; drag horizontally to set (Ctrl/Cmd for fine), double-click for the default. Each drag step posts one `SetValue` with the absolute target; if the queue is full the latest target is sent on the next frame.
- `Choice`: shows the name; clicking opens a Rack menu of the names with the current one ticked.
- `Toggle`: click to flip. `Mask`: one cell per note, lit when active; click toggles, dragging across cells paints the state of the first one.
- `Action`: a button. `SubEditor`: reads "edit in hardware mode"; clicking sets UI MODE to hardware. `Heading`: a title.
- Clicking a row's label posts `Focus`.
- Text is ShareTechMono in `RkaTheme` colours; controls are drawn with nanovg, no SVG assets.
- Hardware mode: the lower screen is the firmware menu exactly as in stage 2. In both modes the encoders, buttons, upper screen and strip work as before.
- With no module (browser, screenshots): the list draws the preview Host's model; nothing is clickable.

- [ ] **Step 1: Write the failing tests** in `test_list.cpp`: rectangles tile the area without overlap and stay inside it, with and without tabs, for 1, 8 and `kMaxRows` rows; hit testing returns the right row and part at row boundaries and for each of 4, 12 and 16 mask cells; slider value↔x round-trips at min, max and mid for ranges 0..1, −3..3, 0..255 and 0..65535, and fine mode moves a quarter as far; scroll clamps at both ends and is zero when everything fits.
- [ ] **Step 2: Run** `make test`; expected: fail to compile.
- [ ] **Step 3: Implement** `ListLayout.hpp`; then the panel entry and `make panels` (confirm with `git status` that only Shoal's generated files changed); then `FriendlyList.hpp` and the wiring in `Shoal.cpp`.
- [ ] **Step 4: Run** `make test`, `make`, `make smoke`; view `build/screenshots/Shoal.png`: the list fills the lower screen in friendly mode, UI MODE and its light sit in the free slot, nothing overlaps. Compare with `~/ws/reference/o_c.png` for row density and legibility.
- [ ] **Step 5: Commit** `Shoal: UI MODE and the friendly list`.

---

### Task 7: Docs, and the end-of-stage checks

**Files:**
- Modify: `docs/shoal/README.md`, `README.md`, `CLAUDE.md` (one line, if a new rule emerged), `docs/specs/2026-11-14-shoal-design.md` ("Carried forward" and stage status), `docs/shoal/Shoal.png` if the repo keeps a screenshot there

- [ ] **Step 1:** Rewrite `docs/shoal/README.md` around friendly mode: the list and each row type, tabs, the app picker, UI MODE, what stays in hardware mode (the sub-editors, with how to reach each), and the known firmware quirks the survey found that a user can meet. Describe controls as the code behaves; read `FriendlyList.hpp` before writing.
- [ ] **Step 2:** Update the design doc: stage 3 done, anything deferred added to the carried-forward list.
- [ ] **Step 3: Run** the full set once more on the final tree: `make test`, `make`, `make smoke`, `make shoal-globals`, `make shoal-bench`; then the Linux test run and both cross-builds on a `git archive` copy piped into `docker run --rm -i` (Colima must already be running; never start, stop or reconfigure it; if it is not running, say so in the report and skip).
- [ ] **Step 4: Commit** `Shoal: friendly mode docs`.

---

## After the last task: checks only the user can make

1. Add Shoal: it opens in friendly mode on CopierMaschine, with Scale and Hold / Freeze at the top of the list.
2. Drag a slider, pick from a dropdown, toggle mask cells while a clock runs: the sound follows, and the upper screen shows the app's picture.
3. Click the app name, pick Quantermain: tabs Ch A–D appear and each keeps its own settings.
4. Switch UI MODE to hardware: the firmware menu shows the same values with the cursor on the row last touched; turn an encoder, switch back: the list shows the change.
5. A row that says "edit in hardware mode" switches mode when clicked.
6. Save and reopen: same app, settings and UI mode. Open a patch saved before this stage: it loads in friendly mode with its settings intact.
7. Scroll the list with the wheel; with the pointer off the list the rack scrolls as usual.
8. Row size and text are comfortable to read and hit at your usual zoom.

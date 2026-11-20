# Shoal: the upper screen, lanes and smooth views: design

Status: design, 2026-11-19. Mockup: `2026-11-19-shoal-scope-mockup.png`
(a sketch: real firmware pixels for Quantermain, invented signals for the
rest).

## Intent

Shoal's upper screen shows the firmware's idle view (its "screensaver") as
128 x 64 pixels under a strip of four output voltages. The user's complaint:
"everything blends together": nothing separates the A/B/C/D lanes, and the
letters over the strip only line up with the picture in three apps, because
each app arranges its view differently (four columns, four quarters, two
halves, four bands, or one picture). Shown the pixel views enlarged, the
user also asked for a higher-resolution picture where one is possible, and
accepted "a bit of deviation" from the hardware "in exchange for a better
view".

So, in friendly mode, the upper screen becomes Shoal's own:

- where a view is geometric (traces, X/Y plots, a note circle, a grid
  path), Shoal draws it itself with smooth lines, from the outputs and the
  app's state;
- where a view is blocky by nature (note bars, bit patterns, text), the
  firmware's pixels stay, with thin dividers and lane letters in that app's
  arrangement;
- the voltage strip gets dividers, so its four readouts are four cells.

## Goals, non-goals, anti-goals

Goals:
- At a glance, which part of the upper screen belongs to which output, in
  every app.
- Smooth, full-resolution views for the six apps listed below.
- Four-trace views as four stacked full-width lanes, A at the top, sharing
  one time axis (the user chose this over the hardware's quarters).

Non-goals:
- Redrawing the pixel views (Quantermain, Meta-Q, CopierMaschine, Sequins,
  Viznutcracker sweet, Acid Curds, References).
- Any change to the lower screen, the friendly list, the engine's outputs,
  the patch format or the panel's size.
- Scope controls (time base, zoom, trigger).

Anti-goals:
- Hardware mode no longer matching a real unit's screen.
- A wrong lane letter (a letter over a region that shows another output).
- Any effect on the outputs or the firmware's state from drawing or not
  drawing (today's rule: the screens are drawn on request and disturb
  nothing).
- Work on the engine thread that allocates, locks without `try_lock`, or
  costs measurably more than today's frame draw.

## What the user decided

- Dividers that follow each app's arrangement (over fixed columns on the
  panel, and over one generic scope for every app).
- Smooth redraws for the geometric views, accepting deviation from the
  hardware.
- Four stacked lanes for the trace views.

## Approaches (how the smooth views get their data)

1. **A published view description (chosen, my call).** `Core` fills a small
   plain struct for the current app (which view, the note circle's notes,
   the grid path, text lines); `Host` adds a history of the four outputs it
   records itself; both are published to the UI with the frames, as the
   friendly model is today. The widget draws from that. Costs one new Core
   call and a per-app switch; keeps every firmware type hidden behind
   `Core.hpp`.
2. **Draw from the output voltages only.** No firmware access; but
   Automatonnetz's grid path and Harrington's transform names are not in
   the voltages.
3. **Vectorise the pixel frame** (trace outlines, smooth them). No new
   data path, but the result is a blurred copy of 64-point data, and lanes
   cannot be rearranged.

## The views

In friendly mode (and in the module browser), by app:

| App | View | Lanes and letters |
|---|---|---|
| Quadraturia, Piqued, Dialectic Ping Pong | **Traces**, smooth | four stacked lanes: A, B, C, D |
| Low-rents | **X/Y**, smooth | two halves: "A × B", "C × D" |
| Automatonnetz | **Grid and circle**, smooth | none |
| Harrington 1200 | **Circle and text**, smooth | none |
| CopierMaschine, Quantermain | firmware pixels | four columns: A, B, C, D |
| References | firmware pixels | four columns: A, B, C, D, over rows 0 to 43 only |
| Meta-Q, Sequins | firmware pixels | two halves: "A", "B" |
| Viznutcracker sweet | firmware pixels | four bands: A, B, C, D |
| Acid Curds | firmware pixels | none |

While the firmware's app menu is open, the view is whatever the current app
shows (as today).

**Hardware mode** shows the firmware's scope frame exactly as today, with
no dividers, letters or smooth views (my call: hardware mode is the promise
that the screens match a unit). The strip's dividers are on the panel and
show in both modes.

### Traces

- Four lanes of equal height, a thin divider between them, the lane's
  letter at its top left, a faint line at 0 V.
- Time runs left to right, newest at the right edge, scrolling. The window
  is **4 s** (about the firmware's own span at 60 frames a second).
- Vertical scale: fixed per app, so each lane is filled by what the app
  puts out (the user's correction: in a ±5 V lane an envelope would use
  only the top half). Piqued: 0 V at the lane's bottom to +5 V at its top
  (an inverted envelope rests at +5 V and falls to 0). Dialectic Ping
  Pong: 0 V to +6.25 V. Quadraturia: −3.75 V to +6.25 V, the whole output
  range. A trace beyond the range is clipped at the lane's edge. The faint
  line marks 0 V (the lane's bottom edge for the envelopes). No
  auto-ranging.
- `Host` records the history on the engine thread: per output, a ring of
  **256 columns**, each the minimum and maximum output over its 260 ticks
  (15.6 ms), so a 1 ms trigger still shows as a spike. The widget draws
  each lane as one smooth line through the columns (a filled min-max band
  where they differ by more than a line's width).
- Piqued's firmware view also draws each envelope's shape preview behind
  its trace. The smooth view drops it: the trace is the envelope. (My
  call; a deviation.)

### X/Y (Low-rents)

- Two square-ish plots side by side with a divider: output A horizontally
  against B vertically on the left, C against D on the right, each centred
  on the outputs' mid-range and scaled so the firmware's full output range
  fits.
- Higher voltage is to the right and up (my call: the firmware's plot runs
  right to left, so against a unit the picture is mirrored).
- The curve is the last **1 s** of output, newest brightest, fading to
  about 15 % at the oldest end. It needs finer time resolution than the
  trace ring: `Host` keeps a second ring of **512 points** per pair, one
  every 32 ticks (1.9 ms), plain samples.

### Grid and circle (Automatonnetz)

- Left half: the 5 x 5 grid as faint dots, and the app's path through it:
  its last four cells (the firmware's history) joined by lines, the
  current cell a filled disc, older ones rings.
- Right half: the note circle: twelve positions (C at the top, clockwise
  by semitone, as the firmware's table), the three chord notes as discs
  joined into a triangle, each labelled with its note name.

### Circle and text (Harrington 1200)

- Left half: the note circle as above.
- Right half: what the firmware prints, in the screen's typeface (Share
  Tech Mono, as the strip): the last four transforms with their signs
  ("+P", "-L", ...) and the three chord notes with octaves ("C4").

### Dividers and letters over pixel views

- Drawn by the widget over the firmware's pixels: lines one device-pixel
  wide at about 40 % of the signal colour, inset from the frame's edge;
  letters small, at about 65 %, at each lane's top left.
- A letter may sit on firmware pixels; it is drawn without a backing
  plate (the mockup's first round used one and hid pixels). Builder's
  choice if a faint plate reads better on Viznutcracker.

### The strip

Three thin vertical dividers between the four readouts, on the panel
(`tools/gen_panels.py`), in the panel's existing engraving style.

## Constraints

- `Core.hpp` stays the only header the plugin includes from the core; the
  view description is a plain struct in a Rack-free header (C++11), its
  strings pointing to static storage or held in fixed char arrays, so it
  can be copied to the UI thread (the FriendlyModel pattern).
- The firmware is not changed for this, or only with `// SHOAL:` marks and
  `CHANGES.md` lines for accessors that cannot be reached otherwise;
  `make shoal-globals` stays clean.
- The engine thread: the history rings are fixed arrays in `Host`, written
  per tick with no allocation; publishing copies them under the existing
  `frameMutex_` `try_lock`.
- A restore, reset or app change must not leave a stale history or view
  on screen for more than a frame.
- Lane geometry (rectangles for lanes, divider lines, label positions, the
  mapping from volts and time to points) lives in a Rack-free header with
  tests, as `ListLayout.hpp` does for the list; the widget only strokes
  what it returns.
- All colours from `RkaTheme`; drawn in the self-illuminating layer like
  `PixelScreen`.
- TESTING.md applies: each test shown to fail; the view description
  compared against an independent path (the firmware's own scope frame:
  e.g. the grid path's cells against the discs the firmware draws, the
  note circle's notes against its disc positions); sanitizers on the
  hostile tests (every app, restore and reset mid-run, fuzzed inputs);
  drawing versus never drawing leaves outputs and state identical.

## Checked against the firmware

- Meta-Q and Sequins draw only each channel's main pitch (outputs A and
  B), never the aux outputs C and D (`APP_DQ.ino` `RenderScreensaver`,
  `APP_A_SEQ.ino` `RenderScreensaver`), so the halves are "A" and "B".
  Sequins already draws its own line at x = 64; Shoal's divider replaces
  nothing and may coincide with it.
- Viznutcracker sweet's bands, top to bottom, are outputs A to D
  (`APP_BYTEBEATGEN.ino`: `bytebeats_[0..3]`, 16 rows each).
- CopierMaschine's four columns and Acid Curds' single picture hold at
  every setting: the arrangement is fixed per app.
- References prints full-width text at rows 44 and 56 under its four
  columns, so its dividers stop at row 43 (my call).

## Checking the look

`make smoke` captures Shoal with no module: the preview host, which shows
CopierMaschine after a quarter of a second. It cannot show a smooth view
with a full history. So (my call):

- the lane geometry is unit-tested (the Rack-free header);
- the look of each view is checked by the user in Rack on a numbered test
  build, at a stop in the plan after the views are drawn and before the
  manual: Quadraturia, Low-rents, Automatonnetz, Harrington 1200 and one
  pixel view of each arrangement.

## Left to the builder

- Names and exact layout of the view struct and the widget.
- Line widths, opacities and label size within the mockup's look, checked
  on `make smoke` screenshots at 100 % and 200 % zoom.
- Whether the module browser's preview host shows a smooth view (it shows
  CopierMaschine by default, a pixel view).

## Manual

`docs/shoal/README.md`, "Screens": the upper screen in friendly mode
(which apps are smooth, how to read lanes and letters), that hardware mode
shows the unit's own screen, and the strip's cells. New screenshot.

# Shoal: design

Date: 2026-11-14. Status: approved 2026-11-14.

Shoal is an emulation of the Ornament & Crime (o_C) eurorack module for
VCV Rack: four trigger inputs, four CV inputs, four CV outputs, a small
screen and the 13 switchable apps of the v1.3 firmware. It runs the real
firmware behind a fake-hardware layer and adds a friendlier way to edit
settings than the hardware's two encoders. It is the third `rka` module.

## Intent

The user wants an O&C in Rack for their own patching, to start with.
Release may come later; nothing here should block it, but it is not the
target.

References:

- **Firmware source**, `~/ws/plugins/O_C` (github.com/mxmxmx/O_C, commit
  `cfbffab`, `OC_VERSION` v1.3.6). This is the ground truth for behaviour.
- **User manual v1.3**, `~/ws/reference/o_c_user_manual-1.3.html`. Because
  the real firmware runs, the manual applies as written.
- **Predefined scales**, `~/ws/reference/scales.html`, from the o_C
  documentation site: every built-in scale with each note in cents and in
  the firmware's pitch units (128 per semitone). An independent check for
  the quantiser tests.
- **Piqued envelope zoo**, `~/ws/reference/envelope.html`, from the same
  site: the envelope types and how each responds to a trigger, a gate and a
  long gate. The local copy has the list but not the diagrams.
- **Benn Jordan's emulation**, shown in the stream "282: The God Module,
  Emulated" (youtube.com/watch?v=m0Ccw5l_HSc) and in the screenshot
  `~/ws/reference/o_c.png`. Shoal borrows its layout and its two UI modes.
  It also runs the real firmware. He reports that Viznutcracker crashed
  Rack until fixed by hand, which is a warning about firmware code that is
  harmless on the Teensy and fatal on a desktop CPU (see Risks).

## Goals, non-goals, anti-goals

Goals:

- All 13 apps behave as the manual says: CopierMaschine, Harrington 1200,
  Automatonnetz, Quantermain, Meta-Q, Quadraturia, Low-rents, Piqued,
  Sequins, Dialectic Ping Pong, Viznutcracker sweet, Acid Curds,
  References.
- Two UI modes, switched on the panel:
  - **Hardware mode**: the firmware's own menu on an enlarged screen,
    driven by two encoders and two buttons, exactly as on the module.
  - **Friendly mode**: a scrolling list of the current app's settings with
    dropdowns, drag bars and toggle cells, and a click-to-pick app list.
- Several Shoals in one patch, each independent.
- Everything saves with the Rack patch, with no save gesture. Presets and
  duplicate work.
- Never crashes Rack, on any app, with any input.
- Under 2% of a core per instance at 48 kHz on Apple Silicon.

Non-goals for the first version:

- Calibration mode, the splash screen and the debug screens. The fake
  converters are ideal, so there is nothing to calibrate.
- The References app's closed-loop auto-tune. Its reference voltages and
  its frequency and BPM meters are in scope.
- Polyphony. Jacks are mono; a poly cable's first channel is used.
- Making firmware settings into Rack parameters (MIDI-map, automation).
  They change per app, which Rack parameters cannot.
- Friendly-mode versions of the firmware's sub-editors: user scale editor,
  Sequins sequence editor, Acid Curds chord editor, Piqued segment
  editing, Automatonnetz grid cells. These stay reachable in hardware
  mode. Note masks (the toggle cells) are in scope.
- Hemisphere, Phazerville or other alternative firmwares.
- Exporting settings to a physical module.
- Native rewrites of individual apps as separate sea-family modules. That
  is a possible follow-on, not this project.

Anti-goals:

- A second, hand-maintained description of each app's settings that drifts
  from the firmware. The friendly list is generated from the firmware's
  own tables.
- Friendly-mode edits that leave an app in a state the encoders could
  never reach.
- A fork of the firmware so heavily edited that pulling an upstream fix is
  impractical.

## Names and house rules

The module is **Shoal** (slug `Shoal`, folder `src/shoal/`, panel
`res/shoal/Shoal.svg`, manual `docs/shoal/`). The name is from the sea
family: a shoal is many fish moving as one, as the module is many apps in
one. The panel title is "SHOAL"; the plugin.json description and the
manual credit Ornament & Crime and its authors (Patrick Dowling, Max
Stadler, Tim Churches).

It follows the rules in `CLAUDE.md`: Nightglow look, panel from
`tools/gen_panels.py`, shared widgets from `src/widgets.hpp`, Rack-free
core with offline tests, `make test`, `make`, `make smoke` before done.

Two places where Shoal bends the house pattern, both deliberate:

- The "DSP core" is the vendored firmware plus the fake-hardware layer,
  not hand-written headers. It is still Rack-free and tested offline.
- The firmware is `.cpp`/`.ino` files that need their own compiler flags
  and a link step, so the Makefile's test rule (one header-only file per
  test) needs extending for this module.

## Approaches

Three were considered.

1. **Run the real firmware behind a fake-hardware layer.** The app files
   compile close to untouched; the Teensy parts are replaced. All 13 apps
   arrive together and the manual applies word for word. The cost is the
   firmware's global state, which has to be restructured so several
   instances can run, and code that does not look like Sway or Fathom.
2. **Rewrite each app natively** in the house style. Clean tested cores,
   native sample rate, possible polyphony. About 14,000 lines of app code
   to re-derive; months of work; behaviour drifts from the manual.
3. **Do 1, then rewrite favourites** as separate modules. This is 1 with a
   follow-on and does not change what gets built first.

**Chosen: 1**, by the user. It is the only route to all the apps soon, and
it is what the reference does.

## Decided with the user

- Approach 1.
- Lives in `RkaModules`, in the Nightglow look, named Shoal.
- Settings save with the patch automatically; no long-press save.
- Layout and the two UI modes follow the reference screenshot.
- Voltages follow the hardware by default, with a right-click option that
  centres the CV inputs on ±5 V (see Voltages).
- No polyphony in this project, in either form discussed: A to D on one
  poly cable, or a firmware copy per poly channel. The second would reuse
  the per-instance work, so the context struct should not assume exactly
  one firmware copy per module.

Everything below this line is the designer's call unless marked otherwise,
and is open to correction at review.

## Panel

About 28 HP (the reference's proportions; the builder may adjust by a few
HP to fit). Regions, as in `~/ws/reference/o_c.png`:

```
+--------------------------------------------------------------+
|                            SHOAL                             |
| +--------------------------------------+ +-----------------+ |
| | A:+1.83V  B:+1.83V  C:+1.58V  D:+2.42V| |  ENC L   ENC R  | |
| |--------------------------------------| |  ( o )   ( o )  | |
| |                                      | |  [push]  [push] | |
| |   scope: the app's own visualisation | |                 | |
| |   (128 x 64 firmware pixels)         | |  [UP] [DN] [UI] | |
| +--------------------------------------+ +-----------------+ |
| +--------------------------------------+ +-----------------+ |
| | v CopierMaschine                     | | TR1  CV1 | OUT A| |
| |--------------------------------------| | TR2  CV2 | OUT B| |
| |  editor: firmware menu (hardware     | | TR3  CV3 | OUT C| |
| |  mode) or settings list (friendly    | | TR4  CV4 | OUT D| |
| |  mode)                               | |          |      | |
| +--------------------------------------+ +-----------------+ |
+--------------------------------------------------------------+
```

- **Scope** (top left): always shows what the hardware shows as its
  "screensaver", the app's visualisation of its outputs, with a strip of
  live A to D output voltages above it.
- **Editor** (bottom left): in hardware mode, the firmware's settings menu
  drawn large; in friendly mode, the settings list. So the firmware draws
  two frames, menu and screensaver, into two separate buffers; on hardware
  it only ever draws one or the other.
- **Controls** (top right): two endless encoders, each with a press; UP
  and DOWN buttons; the UI MODE latch with a light (lit = friendly).
- **Jacks** (bottom right): TR1 to TR4 with activity lights, CV1 to CV4,
  OUT A to D in a highlighted column with bipolar lights.

Screen pixels are drawn lit, in the self-illuminating layer, in a Nightglow
accent (cyan by default; the right-click menu offers the other accents).
Jack and control positions come from `gen_panels.py`; the two screen
rectangles are custom widgets placed from the generated layout.

## The firmware core

### Vendoring

Copy `software/o_c_REV/` into `src/shoal/firmware/`, recording the upstream
commit. Keep upstream file names and layout so a diff against upstream
stays readable. Every local change is marked with a `// SHOAL:` comment,
and `src/shoal/firmware/CHANGES.md` lists them by file. Drop what is not
used: the SH1106 driver, the DMA ADC code, the FreqMeasure driver,
`OC_debug`, the calibration UI, the VOR and Buchla variants.

Licences: most firmware files carry an MIT header, which is kept.
`OC_DAC.cpp` carries a GPL version 3 header. Both are compatible with the
plugin's GPL-3.0-or-later. The Mutable Instruments derived files
(`peaks_*`, `braids_*`, `frames_*`, `streams_*`, `extern/stmlib_*`) are
MIT. About 40 files have no licence header at all, including ones Shoal
needs (`OC_ui.cpp`, `OC_menus.cpp`, `OC_strings.*`, `OC_scales.*`,
`OC_patterns.*`, `OC_chords.*`, the scale and sequence editors, `OC_ADC.*`,
`OC_DAC.h`, `OC_digital_inputs.*`), and the checkout has no LICENSE file.
That is fine for the user's own patching. The attribution section in the
plugin's licence notes must not claim MIT for the unheadered files, and
**before any public release** their licence has to be confirmed with
upstream.

### What replaces the Teensy

| Hardware | Replacement |
|---|---|
| 60 µs core timer interrupt | called from `process()`; see Timing |
| 12-bit ADC, four CV inputs | Rack input voltages converted to ADC codes |
| 16-bit DAC, four outputs | DAC codes converted to Rack output voltages |
| TR1 to TR4 pin interrupts | edge detection on the Rack inputs, latched |
| OLED over SPI | the firmware's 128×64 one-bit frame buffer, read directly |
| Encoders and buttons | events pushed into the firmware's own UI event queue |
| EEPROM | a byte array per instance, saved in the patch |
| `millis`, `micros`, `delay` | derived from the tick counter; `delay` does nothing |
| FreqMeasure on TR4 | period of TR4 rising edges, measured at Rack's sample rate |
| Arduino build | a prototypes header and a fixed include order (Arduino generates prototypes and concatenates `.ino` files; a host build must do both by hand) |

Voltages. The shim uses the firmware's own default calibration as its
ideal table, and converts to and from volts with the exact inverse, so
pitch is 1 V/octave with no calibration step. From those defaults:
outputs span about −3.75 V to +6.25 V (0 V at DAC code 24551, 6553.6
codes per volt); inputs span about −3.33 V to +6.67 V (ADC offset 2730,
409.6 codes per volt, `pitch_cv_scale` 15360) and are clamped to that.
The hardware's resolution is kept (12-bit in, 16-bit out), and so is its
input filtering: the hardware picks up a new CV sample every third tick
(5.55 kHz) and applies a one-quarter smoothing step, and the shim does
the same. These are part of how the apps feel.

Input range option. A ±5 V Rack LFO into a hardware-range input loses
everything below −3.33 V. So the right-click menu has "CV input range":
**Hardware** (−3.33 V to +6.67 V, the default) or **±5 V**. The ±5 V
setting shifts the input window without stretching it, by using an ADC
offset at mid-scale in the shim's calibration, so 1 V/octave and 0 V = "no
modulation" both still hold; what is given up is input above +5 V. It
applies to all four CV inputs, is saved in the patch, and does not touch
the outputs, which always use the hardware range. A blanket rescale to
Rack's standards was rejected: the same jack carries pitch in one app and
modulation in another, and stretching would break 1 V/octave. Triggers use the plugin's standard Schmitt thresholds
(0.1 V / 1 V); a rising edge at the Rack input is the firmware's "clocked"
event, and the gate level is its "read immediate".

This departs from the plugin's usual ±5 V / 0 to 10 V CV standards on
purpose: an O&C that outputs anything other than its own range would not
match the manual.

### Timing

The firmware's core tick is fixed at 60 µs (16,666.67 Hz). `process()`
keeps a fractional accumulator and runs zero, one or more ticks per Rack
sample so that the long-run rate is exact at any engine sample rate.
CV inputs are sampled every third tick, as above; trigger edges that fall between ticks are
latched so none are lost; outputs hold their value between ticks, as a DAC
does. Viznutcracker used as an audio source is therefore stepped at
16.67 kHz, as on the hardware.

The firmware's main loop (run the app's `loop()`, dispatch UI events, draw)
is not run as a loop. Its body becomes a step function called from the
engine thread at a fixed fraction of the tick rate: UI event dispatch and
`loop()` at about 1 kHz, the two frame draws at about 60 Hz. Running it on
the engine thread, between ticks, means the firmware sees the same
"interrupt never overlaps itself" world it was written for, with no locks
inside firmware code. The finished frames are handed to the UI thread
through a double buffer.

The blocking loops in the firmware are removed or restructured:

- App selection (`Ui::AppSettings`) becomes a non-blocking state, so the
  hardware-mode long-press on the right encoder still opens the firmware's
  app menu on the editor screen.
- Splash screen, calibration and debug loops are removed. The module boots
  straight into the saved app.
- The firmware's screensaver state is never entered: the idle timeout and
  the UP long-press shortcut are disabled (otherwise the firmware swallows
  the next input to "wake" a screen that Shoal always shows), and the
  screensaver on/off events are never sent. Only four apps react to those
  events and none needs them.

Encoders. Encoder acceleration lives in the firmware's pin-polling encoder
class, not in its event queue, so the shim drives that class (or reuses
its arithmetic) rather than pushing raw encoder events.

Held-button gestures. The sub-editors use chords a mouse cannot play:
hold L and turn to edit user-scale notes or fine pitch, hold UP and turn L
in Meta-Q's editor, hold L or DOWN with a long press to clear a mask. The
panel's button and encoder-press widgets therefore have a hold latch
(the builder designs the gesture, for example shift-click to latch).

### Several instances

On the Teensy there is one of everything: about 60 file-scope objects
(each app's state, the UI, the frame buffer, settings storage), the static
members of `OC::ADC`, `OC::DAC`, `OC::DigitalInputs` and friends, and six
function-local statics. Requirements:

- Every piece of mutable firmware state belongs to one Shoal instance.
- Two instances can be processed on different engine threads at once.
- Read-only tables (scales, wavetables, fonts, strings) stay shared.
- No loading of extra copies of a library at run time.

Recommended mechanism, builder's choice if a better one appears: gather the
mutable state into one context struct per instance, reached through a
thread-local "current instance" pointer that the shim sets on every entry
into firmware code (tick, step, draw, save, restore). Global names in the
firmware become references into that struct, so the thousands of call
sites do not change, only the few dozen declarations.

### Desktop-CPU hazards

The firmware was written for a 32-bit ARM microcontroller and leans on
its behaviour:

- Integer divide by zero returns 0 on ARM and kills the process on x86.
  The byte beat equations and several clock dividers divide by values that
  can be zero.
- Signed overflow, unaligned casts and type punning are used freely.
- `long`, `size_t` and pointers are 32-bit there, 64-bit here.

So: compile the firmware files with `-fwrapv -fno-strict-aliasing`; guard
divisions found by the tests; and run the offline tests under the address
and undefined-behaviour sanitizers. Shoal is cross-built for x64 Linux and
Windows as well as arm64 macOS, so a clean run on the Mac alone proves
little: the sanitizer run is what stands in for the other CPUs.

## Friendly mode

### The settings list

Every app declares its settings in a table (`SETTINGS_DECLARE`): name,
default, minimum, maximum, optional value names. Most apps also publish
which settings are visible right now (`num_enabled_settings`,
`enabled_setting_at`), which changes as other settings change. The list is
built from these at run time, so it tracks the firmware's menu.

Each app gets a small adapter, the only per-app code in friendly mode:

- how many channels it has and their labels (four for Quantermain, two for
  Meta-Q and Sequins, one for CopierMaschine, and so on), shown as tabs;
- the settings object for each channel and its visible-settings list;
- which settings are note masks, and how many notes the current scale has;
- the follow-up the app performs after an edit (see below).

Row types, chosen from the table entry:

- value names present: a dropdown;
- numeric range: a drag bar with the value shown, double-click to reset to
  the default;
- note mask: a row of toggle cells, one per note of the scale;
- anything the adapter marks as a sub-editor: a row that says "edit in
  hardware mode" and, when clicked, switches mode.

The header shows the app name; clicking it opens the list of 13 apps. The
right-click menu has the same list.

Four apps (Quadraturia, Low-rents, Dialectic Ping Pong, Automatonnetz)
publish no visible-settings list; their adapters show the whole table.

Sequins and Acid Curds split their menu into two pages, parameters and CV
mapping, toggled by DOWN, and report only the current page as visible. The
friendly list shows both pages at once, as two headed sections.

### Panel controls stay live

The encoders, their presses and UP/DOWN work the same in both modes. Many
of their jobs are not settings (octave toggle and freeze in CopierMaschine,
scale preview and commit on the left encoder), so they cannot go dead in
friendly mode. To keep the right encoder meaningful there, the list
highlights the row under the firmware's menu cursor and marks it when the
firmware is in edit mode, and clicking a row moves the firmware cursor to
it. The two modes are then two views of one state.

The list header also carries the firmware title bar's live indicators for
the app (for example freeze and clock in CopierMaschine, channel A's
frequency in Quadraturia), supplied by the adapter.

### Edits must go through the app

When an encoder changes a value, the app's encoder handler does more than
write it. Before the change it may refuse or constrain it (Quantermain
blocks some trigger and source combinations; Piqued holds Euclidean fill
and offset within the length). After it, it does follow-up work: rebuild
the visible-settings list, reconfigure a quantizer, force an update. So
each adapter has an "apply edit" function that runs the same guards and
the same follow-up as that app's encoder handler, and a test per app
checks that setting a value through the list leaves the app in the same
state as reaching that value with the encoder.

### Threads

The list lives on the UI thread; the firmware lives on the engine thread.
Edits travel as small messages (channel, setting, new value) through a
lock-free queue and are applied on the engine thread in the step function.
The engine thread publishes a snapshot of the visible settings and their
values for the UI to draw. Encoder turns and button presses from the panel
take the same queue into the firmware's event queue.

## Saving

Nothing is saved by gesture. About twice a second, if anything changed,
the engine thread runs the firmware's own save for all apps and global
settings (user scales, patterns and chords) into a snapshot. `dataToJson`
writes the latest snapshot as a base64 string, with the current app, the
UI mode, the screen colour and the CV input range. `dataFromJson` hands the bytes back to the
engine thread, which runs the firmware's restore. The firmware's intended
pause while saving does not apply: its save is a small copy.

What is saved is what the hardware saves: settings, not running state. A
reloaded patch starts sequences from the top and the shift register empty,
like a module that was power-cycled.

A patch with no saved data, or with data the firmware rejects, starts with
firmware defaults in CopierMaschine.

## Right-click menu

- App: the 13 apps.
- UI mode: hardware / friendly (same as the panel latch).
- Screen colour: the Nightglow accents.
- CV input range: Hardware / ±5 V.
- Reset all apps to defaults.
- Encoder acceleration on/off (the hardware hides this behind UP in the app
  menu).

## Testing

Offline, no Rack, in `src/shoal/test_*.cpp`, built with the sanitizers:

- Every app boots and runs a few seconds of ticks with silence, with
  random CV and triggers, and with extreme CV, without a fault.
- Every app, every setting swept across its full range while ticking,
  without a fault. This is the test that would have caught the reported
  Viznutcracker crash.
- CopierMaschine shifts sampled values down A, B, C, D on each clock, as
  the manual describes.
- Quantermain returns in-scale 1 V/octave voltages for a sweep of inputs.
- Volts in and out round-trip through the fake converters within one code.
- With the ±5 V input range: 0 V in reads as zero, pitch input stays
  1 V/octave, −5 V and +5 V are reached without clipping, and every app
  runs its fault sweeps in this setting too (no app may assume the
  original window).
- Tick count is exact over ten seconds at 44.1, 48 and 96 kHz.
- Two instances given different inputs produce independent outputs, and
  the same when run on two threads.
- Save, restore into a fresh instance, compare settings for every app.
- For each app: a friendly-mode edit and the equivalent encoder turns give
  the same state.

In Rack: `make smoke` loads the plugin and screenshots the panel. Rack's
screenshot builds the widget with no module behind it and takes one image
per module, so the screens draw a canned preview frame in that case and
the screenshot shows friendly mode only; hardware mode is covered by the
offline tests.

## Order of work

Each stage is usable on its own and gets its own implementation plan.

1. **Firmware core, headless.** Vendoring, the fake hardware, instancing,
   the offline tests above except the friendly-mode one. No Rack code.
2. **Shoal in hardware mode.** Panel, jacks, both screens, encoders and
   buttons, saving. At this point the module is usable for patching with
   the manual open.
3. **Friendly mode.** Adapters, settings list, app picker, channel tabs,
   note masks, the equivalence tests.
4. **Finish.** Right-click menu, the References meters, manual in
   `docs/shoal/`, README and changelog entries, CPU check.

## Risks

- **Instancing touches every firmware file.** It is mechanical but wide.
  Stage 1's two-instance test is the proof; do it before anything else is
  built on top.
- **Faults that only appear on x86.** Covered by the sanitizer runs and
  the full-range sweeps, but only as far as the sweeps reach.
- **Friendly-mode follow-up hooks** have to be read out of 13 encoder
  handlers by hand. The equivalence tests bound the damage.
- **Drawing both frames.** Some apps react to the "screensaver on/off"
  event; since Shoal shows both screens at once, the builder must check
  what each app does on that event and decide per app whether to send it.

## Left to the builder

- Exact HP and the placement of every component, within the regions above.
- Extending `tools/gen_panels.py` and `src/widgets.hpp`: the generator
  emits only params, inputs, outputs and lights today, and there is no
  encoder or momentary-button widget. Shoal needs screen rectangles, an
  endless encoder and a Nightglow momentary button, added as shared
  pieces.
- How the encoders behave under the mouse (drag distance per detent,
  scroll wheel, whether the press is a click on the knob or a separate
  button as in the reference).
- The context mechanism for instancing, within the stated requirements.
- How the screens are drawn (rectangles or a texture).
- Step-function rates, if the suggested ones cost too much or feel slow.
- The Makefile changes for per-file flags and linked tests.

# Shoal

The Ornament & Crime firmware in a 28 HP module. Shoal runs the real
o_C v1.3.6 firmware, all thirteen apps (CopierMaschine, Harrington 1200,
Automatonnetz, Quantermain, Meta-Q, Quadraturia, Low-rents, Piqued,
Sequins, Dialectic Ping Pong, Viznutcracker sweet, Acid Curds and
References), on a stand-in for the module's hardware, so the apps behave
as on the hardware and the o_C manual applies as written.

On top of the hardware's menus Shoal adds **friendly mode**: the lower
screen lists the current app's settings, and you edit them with the
mouse. It is the default. The **UI MODE** button switches to **hardware
mode**, where the lower screen is the firmware's own menu and you work it
with the encoders and buttons as on the real module.

Ornament & Crime is by Patrick Dowling, Max Stadler and Tim Churches
(github.com/mxmxmx/O_C), under the GPL. Shoal is not affiliated with
them. The changes Shoal makes to the firmware are listed in
`src/shoal/firmware/CHANGES.md`.

![Shoal panel](Shoal.png)

This page covers the module itself. For what each app does, read the
o_C user manual for v1.3: <http://ornament-and-cri.me/user-manual-v1_3/>.
A fuller manual comes later.

## Screens

- The **upper screen** is the o_C screen the hardware shows when idle:
  the current app's scope or status view. It is the same in both modes.
- The **lower screen** is the settings list (friendly mode) or the o_C
  menu screen (hardware mode).
- The strip above them shows the four output voltages.

## Friendly mode

### The header

The top line shows the current app's name. **Click it** to open a menu of
the thirteen apps, with the current one ticked; choosing one switches to
it, as the firmware's app menu would. On the right the header shows the
app's own status from the firmware's title bar (for example the scale in
CopierMaschine, the root and the chord's notes in Harrington 1200), or "CLICK APP TO
SWITCH" when the app has none.

### Tabs

Apps with more than one channel show a tab per channel under the header:
A to D for Quantermain, Dialectic Ping Pong, Viznutcracker sweet,
References and Piqued, A and B for Meta-Q and Sequins. Click a tab to
show that channel's settings; each channel keeps its own. Selecting a tab
selects that channel in the firmware too, as the left encoder does on the
hardware.

### Rows

Each row is a label on the left and a control on the right. The rows are
the app's menu, in the firmware's order and with the firmware's names,
plus rows for the things the hardware does with the left encoder, the
buttons and long presses (CopierMaschine's Hold / Freeze, Harrington
1200's Reset all settings, Automatonnetz's Clock, and so on). Rows appear
and disappear as on the hardware: when a setting hides or shows others,
the list follows. Where the firmware labels a row with an arrow, the list
names it instead: "Active notes" for a scale's note mask (the firmware's
"--> edit" or "scale  -->"), "Steps" for Sequins' step mask, "Chords" for
Acid Curds' "chords -->" and "Autotune" for References' "--> autotune".

Piqued lists its envelope's segment values first: Attack, Decay,
Sustain/Level and Release, as the envelope page names them, as many as
the envelope type has (two for AD, ASR and ADL2). An edit goes through the
envelope page as you would on the hardware and leaves it on the settings
page; clicking a segment's label shows the envelope page with that
segment selected.

- **Slider** (a number): drag left or right on the bar. The drag moves the
  value from where it is, so a click on its own changes nothing; the bar's
  width is the whole range. Hold **Ctrl** (Cmd on a Mac) for a quarter of
  the speed, **Ctrl+Shift** (Cmd+Shift) for a sixteenth, to reach single
  steps on a wide range. **Double-click** sets the default. The value is
  printed on the bar; a range that spans zero fills from zero.
- **Dropdown** (a named value): click to open a menu of the values, with
  the current one ticked, and pick one. A value the firmware has no name
  for shows its number.
- **On/off switch**: click to flip it.
- **Note mask** (the notes of the current scale): one cell per note, lit
  when the note is active. Click a cell to toggle it, or press and drag
  across cells to set every cell you cross the way the first one went.
  The number of cells follows the scale (none, shown as "-", when the
  scale is Off). Where the firmware keeps the last active note of a scale
  mask, the last cell will not go dark.
- **Button** (a row that is all one button): click it to do what it says
  ("Reset", "Clock", "Copy scale to other channels", "Reset mask", and so
  on). These are the hardware's button presses and long presses. The ones
  that undo work ask first: Harrington 1200's "Reset all settings",
  Automatonnetz's "Clear grid", Meta-Q's "Copy scale to all slots, both
  channels" and the "Clear CV mapping" rows of Sequins and Acid Curds open
  a small menu with the row's name; nothing happens unless you click
  **Confirm**.
- **Heading**: Sequins and Acid Curds have two menu pages on the
  hardware, parameters and CV mapping; the list shows both, each under a
  heading.
- **"edit in hardware mode"**: a row whose editor is only in hardware
  mode (see below). Clicking it moves the firmware's cursor to that row
  and switches UI MODE to hardware, so hardware mode opens on it.

**Click a row's label** to move the firmware's menu cursor to that row
without changing anything. The row under the cursor is tinted and has a
bar on its left edge: cyan, or amber while the firmware is editing that
value (an R push on the hardware). Any edit made in the list also leaves
the cursor on the edited row, so after switching to hardware mode the
right encoder is on the setting you last touched. The encoders and
buttons keep working in friendly mode; the list shows what they change
within a frame, and scrolls to keep the cursor's row in view.

### Scrolling

When the rows do not fit, a scrollbar appears on the right: drag its
thumb, or click its track to move the thumb there. The scroll wheel scrolls the list
while the pointer is over it, two rows per notch. Over a list that fits,
the wheel scrolls the rack as usual.

### While a hardware editor is open

The app menu (a long press of the right encoder's button) and the apps'
own editors (below) take over the hardware's controls, and the firmware
accepts no settings edits until they close. While one is open, an amber
banner under the header reads "HARDWARE EDITOR OPEN" and the list is
dimmed and takes no edits. Click the banner to switch to hardware mode
and finish there. The app picker in the header still works.

## UI MODE

The **UI MODE** button beside UP and DOWN switches the lower screen
between friendly mode (the button lit cyan) and hardware mode (dark). The
switch is saved with the patch and can be undone with Edit > Undo, like
any switch. A patch saved before friendly mode existed opens in friendly
mode, with its app and settings as saved. Both modes are views of the
same firmware: switching changes nothing in the app.

### What stays in hardware mode

These editors have no mouse version yet. Each is reached from the list as
shown, then worked as the o_C manual describes:

| Editor | App | How to reach it |
| --- | --- | --- |
| User scale notes (and the scale editor's rotate, invert and other pages) | CopierMaschine, Quantermain, Meta-Q, Sequins, Acid Curds | Click the label of the note mask row ("Active notes"; "mask" in CopierMaschine), switch to hardware mode, press R on that row (notes can be edited with a user scale selected). |
| Sequence steps (pitches, lengths, copy and paste) | Sequins | Click the label of the "Steps" row, switch to hardware mode, press R. |
| Chords and progressions | Acid Curds | Click the "Chords" row's control (hardware mode opens on that row) and press R. |
| Cells | Automatonnetz | The "Cells" row switches mode; press L (the left encoder's button) for the cell view. |
| Autotune | References | Click the "Autotune" row's control (hardware mode opens on that row) and press R. |

## Hardware mode

The lower screen is the firmware's menu, and everything the o_C manual
says about the encoders, buttons and screens holds, including the app
menu (long-press the right encoder's button), the settings menus and the
sub-editors.

## Controls

- **ENC L / ENC R**: endless encoders. Drag up to turn clockwise, down
  to turn anticlockwise: 8 px of mouse travel per detent, or 32 px with
  Ctrl (Cmd on a Mac) held for fine steps. The scroll wheel turns them,
  one detent per notch, only when Rack's "scroll wheel knob control"
  setting is on; with it off the wheel scrolls the rack as usual.
  Encoder acceleration starts on, as on the hardware.
- The round button under each encoder is that encoder's push; **UP** and
  **DOWN** are the o_C's two buttons. Click to press: the button is down
  for as long as the mouse is held, so a long press is a long click.
  **Shift-click** latches a button down, for the gestures that hold a
  button while turning an encoder (for example, the user-scale editor's
  hold-L-and-turn); click it again to release it.
- **UI MODE**: friendly or hardware mode, above.

## Inputs and outputs

- **TRIG 1–4**: trigger and gate inputs. A rising edge counts when the
  voltage passes 1 V; the input reads low again below 0.1 V. The amber
  light beside each jack shows its state.
- **CV 1–4**: CV inputs, clamped to −3.33 V to +6.67 V, the hardware's
  input range.
- **A–D**: outputs, −3.75 V to +6.25 V, the hardware's DAC range. The
  light beside each is cyan for positive voltages and magenta for
  negative ones, full at 5 V.
- A polyphonic cable into an input uses its first channel; the outputs
  are monophonic.
- The outputs start at 0 V when the module is added, initialized or
  loaded with a patch, until the app writes them. The hardware starts
  them near its top voltage instead; this difference is deliberate.

## Saving, initialize, randomize

- The firmware's settings (its EEPROM: the current app and every app's
  settings) are saved with the patch, captured about twice a second, so
  reopening a patch brings back the app and its settings. A duplicated
  module starts with the same settings and then runs on its own, with its
  own random values.
- UI MODE is saved with the patch like any switch.
- A button latched with shift-click is released when the patch is loaded.
- **Initialize** (right-click) returns the firmware to a new module's
  defaults: CopierMaschine, outputs at 0 V, friendly mode.
- **Randomize** does nothing.

## Good to know

- **Sub-editors are hardware mode.** User scale notes, Sequins steps,
  Acid Curds chords and Automatonnetz cells are edited
  in hardware mode, reached from the row that says so (see "What stays in
  hardware mode"). While one is open the list shows the amber banner and
  takes no edits.
- **Harrington 1200, Euclidean rows.** After loading a patch saved with
  Trigger type set to Eucl, the Euclidean rows stay hidden until Trigger
  type is changed. The hardware does the same.
- **Quantermain, "Copy scale to other channels".** Afterwards, a channel
  that had no scale shows its Root and mask rows only once one of its
  settings is edited (Scale, CV Source or Trigger source, for example).
  The firmware's own menu behaves the same.
- **Meta-Q's "Active notes" row** (the firmware's "--> edit") belongs
  to the scale slot chosen by "scale #", not to the slot the header shows
  playing (the scale sequencer or the aux CV can move that one).
- **Quadraturia, Coarse and Fine** do nothing while Tap tempo is on: the
  firmware ignores the left encoder then, as on the hardware.
- **References, "Mod range oct".** Negative values are lost when the
  patch is saved: a bug in the firmware itself, kept as the hardware has
  it.
- **Edits during a fast encoder spin.** List edits made while a long,
  fast encoder spin is still being played out apply once the turns have
  reached the app, up to about half a second later, so they land in the
  order you made them.
- **Sliders** are relative: drag from where the value is, with Ctrl/Cmd
  for fine and Ctrl/Cmd+Shift for extra fine steps; double-click resets
  to the default.
- **The scroll wheel** scrolls the list when the pointer is over a list
  longer than the screen, and turns an encoder only when Rack's
  scroll-wheel knob control is on.
- **Undo** covers the UI MODE switch, not edits made in the list or with
  the encoders.

## Not there yet

- Mouse versions of the sub-editors (user scale notes, Sequins steps,
  Acid Curds chords, Automatonnetz cells).
- Right-click options: the ±5 V input range, encoder acceleration on/off
  and the app choice. The input range is already saved with the patch, so
  patches keep it when the option arrives.
- References' meters, and a full manual.

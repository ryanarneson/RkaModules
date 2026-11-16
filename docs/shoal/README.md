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

This page covers the module itself, and "Apps" below says what each app
does and how to patch it. For the full detail of every app, read the o_C
user manual for v1.3: <http://ornament-and-cri.me/user-manual-v1_3/>.
A fuller manual comes later.

## The panel

Shoal is drawn as ripples from two sources: the two encoders are the
sources, set at different heights, and the faint circles spreading from
each cross the whole panel.

- The three screens are on the left. What the firmware draws on them is
  cyan; the friendly-mode list adds brass and ivory where it points at
  hardware mode (the "hardware editor open" banner, the "edit in
  hardware mode" rows) and for the bar of a value being edited.
- **ENCODER L** and **ENCODER R** each sit on a dial ticked once per
  detent. They have no pointer, since they turn without end; the knurled
  rim moves as you turn. Each encoder's **PUSH** button is beside it,
  joined to it by a line.
- **UP** over **DOWN** stand on their own below the encoders, with
  **UI MODE** to their right.
- Below the controls is the jack field: **TRIG** and **CV** inputs in two
  columns, rows 1 to 4, a light beside each trigger jack, and the outputs
  **A** to **D**, each on an ivory tile with its letter and its light.

## Screens

- The **upper screen** is the o_C screen the hardware shows when idle:
  the current app's scope or status view. It is the same in both modes.
- The **lower screen** is the settings list (friendly mode) or the o_C
  menu screen (hardware mode).
- The strip above them shows the four output voltages, each under its
  channel's letter.
- The scales outside the two screens count firmware pixels: a tick every
  8, a longer one every 32, numbered under the lower screen. They are
  128 by 64, as on the hardware.

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
bar on its left edge: cyan, or brass while the firmware is editing that
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
accepts no settings edits until they close. While one is open, a brass
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

- **ENCODER L / ENCODER R**: endless encoders. Drag up to turn clockwise, down
  to turn anticlockwise: 8 px of mouse travel per detent, or 32 px with
  Ctrl (Cmd on a Mac) held for fine steps. The scroll wheel turns them,
  one detent per notch, only when Rack's "scroll wheel knob control"
  setting is on; with it off the wheel scrolls the rack as usual.
  Encoder acceleration starts on, as on the hardware.
- The **PUSH** button beside each encoder is that encoder's push; **UP** and
  **DOWN** are the o_C's two buttons. Click to press: the button is down
  for as long as the mouse is held, so a long press is a long click.
  **Shift-click** latches a button down, for the gestures that hold a
  button while turning an encoder (for example, the user-scale editor's
  hold-L-and-turn); click it again to release it.
- **UI MODE**: friendly or hardware mode, above.

## Inputs and outputs

- **TRIG 1–4**: trigger and gate inputs. A rising edge counts when the
  voltage passes 1 V; the input reads low again below 0.1 V. The
  light beside each jack shows its state.
- **CV 1–4**: CV inputs, clamped to −3.33 V to +6.67 V, the hardware's
  input range.
- **A–D**: outputs, −3.75 V to +6.25 V, the hardware's DAC range. The
  light on each output's tile brightens with the size of the voltage,
  positive or negative, and is full at 5 V.
- A polyphonic cable into an input uses its first channel; the outputs
  are monophonic.
- The outputs start at 0 V when the module is added, initialized or
  loaded with a patch, until the app writes them. The hardware starts
  them near its top voltage instead; this difference is deliberate.

## Apps

To change app in friendly mode, click the app's name at the top of the
lower screen and pick one from the menu. In hardware mode, long-press
ENCODER R's PUSH button: the firmware's app menu opens; turn ENCODER R to
the app and press PUSH R. Every app keeps its own settings while you use
the others. A new module starts on CopierMaschine.

What follows describes each app as a new module sets it up. Names in
quotes are rows of the friendly list. Pitch CV is 1 V per octave, with
0 V as C. The o_C v1.3 user manual
(<http://ornament-and-cri.me/user-manual-v1_3/>) has the full detail.

| App | What it is | Typical use |
| --- | --- | --- |
| CopierMaschine | Quantizing shift register | One melody becomes four that follow each other |
| Harrington 1200 | Triad transformer | Voice-led chord changes from triggers |
| Automatonnetz | Grid of chord moves walked by a clock | Wandering chord sequences and arpeggios |
| Quantermain | Four quantizers with built-in sources | Quantize four CVs, or make random melodies |
| Meta-Q | Two quantizers, four scales each | Melodies that change key or mode in sequence |
| Quadraturia | Four linked wavetable LFOs | Related modulation, phase-shifted sweeps |
| Low-rents | Two chaotic attractors | Slow modulation that never repeats |
| Piqued | Four envelope generators | Envelopes, looping envelopes, Euclidean triggers |
| Sequins | Two step sequencers | Short pitched sequences with gates or envelopes |
| Dialectic Ping Pong | Four bouncing-ball envelopes | Bouncing decays and speeding-up patterns |
| Viznutcracker sweet | Four bytebeat generators | Gritty 8-bit audio or stepped CV |
| Acid Curds | Chord quantizer and progression sequencer | Four-voice chord progressions |
| References | Fixed reference voltages | Tuning oscillators, checking tracking |

### CopierMaschine

A sample and hold whose value is passed down a chain of four outputs at
each clock, quantized to a scale. Feed it one melodic CV and a clock, and
the four outputs play the same line, each a step later than the last.

- **In.** TRIG 1 is the clock: each trigger samples CV 1 and shifts the
  chain. TRIG 2 high freezes it, so the outputs cycle through the last
  "hold (buflen)" values ("Hold / Freeze" does the same by hand). TRIG 3
  high raises every output an octave and TRIG 4 high lowers it (both read
  at each clock). CV 2 adds to "buf.index", CV 3 rotates the note mask,
  and CV 4 goes where "CV4 dest. ->" says (octave; or root, transpose,
  buffer length, input gain).
- **Out.** A to D: pitch CV, quantized. A is the newest value and B to D
  older ones; "buf.index" pushes them further apart in time.
- **Start with** "Scale", "root", "mask" and "input gain". "CV source"
  TM, ByteB or IntSq makes the values inside the app instead of sampling
  CV 1, and CV 1 then modulates that source through its own "CV1" row.
  With ByteB, only "> BB P2" changes the result (a slip in the firmware).
- **Hardware mode:** user scale notes (see "What stays in hardware mode").

### Harrington 1200

Holds a major or minor triad and, on each trigger, moves it to a closely
related chord that shares notes with it (the P, L and R moves of
neo-Riemannian theory, and the compound N, S and H). Patch the outputs to
three or four voices for smooth chord changes driven by gates.

- **In.** TRIG 1 resets to the root's triad. With "Trigger type" PLR (the
  default), TRIG 2, 3 and 4 apply P, L and R; with NSH they apply N, S and
  H; "PLR Priority" and "NSH Priority" set the order when they coincide.
  With Eucl, every trigger clocks six Euclidean patterns, one per move
  (the "EuLeng", "EuFill" and "EuOffs" rows). CV 1–4 do nothing until
  assigned: "Transpose CV" (semitones, 1 V/oct), "Octave CV", "Inversion
  CV", "PLR Prior CV", "NSH Prior CV", and "Eucl CV1 map" to "Eucl CV4
  map". "CV sampling" Cont reads them all the time, Trig only on triggers.
- **Out.** A: the root. B, C, D: the chord's three notes. All pitch CV.
  "Output mode" Tune puts the root on all four, for tuning.
- **Start with** "Transpose", "Root mode", "Inversion", "Trigger type".
  No hardware-only editor; "Reset all settings" restores the defaults.

### Automatonnetz

A 5 by 5 grid whose cells each hold a chord move. A clock walks a
playhead across the grid, applying each cell's move to the current triad:
chord sequences that wander, or an arpeggiator over them.

- **In.** TRIG 1 is the clock: the playhead moves by "Step X" across and
  "Step Y" down. TRIG 3 high when TRIG 1 arrives sends it back to the
  start. TRIG 2 steps the arpeggio when "OutA" is arp or strum, and TRIG 4
  high holds it. CV 1 sets the root (1 V/oct, in semitones) and CV 4 the
  inversion. CV 2 and CV 3 are not used.
- **Out.** B, C, D: the chord's three notes, pitch CV. A by "OutA": root
  (default), trig (a short 5 V trigger at each chord change), arp (a
  chord note per TRIG 2), strum (the three notes once after each change).
- **Start with** "Step X", "Step Y", "Mode", "OutA". A new module's grid
  is filled with random moves, so a clock alone gives chord changes.
  "Reset" sends the playhead to the start and "Clock" moves it one step;
  "Clear grid" refills the grid as "Clr" says (zero: empty, no moves).
- **Hardware mode:** the cells themselves ("Cells" row).

### Quantermain

Four quantizers: each forces a CV onto the notes of a scale, on a trigger
or continuously. A channel can use a built-in source instead of a CV
input (Turing shift register, logistic map, bytebeat, integer sequence)
and so make its own melodies.

- **In.** Channel A quantizes CV 1 at each TRIG 1, B CV 2 at TRIG 2, and
  so on ("CV Source", "Trigger source"). With "Trigger source" cnt+ or
  cnt- the channel quantizes all the time, and its own TRIG input, while
  high, moves it an octave up (cnt+) or down (cnt-). A channel reading
  another channel's CV input can use its own with "CV aux >" (root,
  octave, transpose or mask rotation). The built-in sources need a TRIG
  input as clock and have their own CV rows ("LFSR prb CV >" and so on).
- **Out.** A to D: each channel's pitch CV.
- **Start with** "Scale", "Root", "Active notes", "Octave"; "Copy scale
  to other channels" and "Reset mask" act on the tab shown.
- **Hardware mode:** user scale notes.

### Meta-Q

Two quantizers, each with four scale slots (scale, root, notes and
transpose), stepped through by a trigger or chosen by CV: quantized
melodies that change key or mode as the patch goes on.

- **In.** Channel A quantizes CV 1 at each TRIG 1, channel B CV 3 at TRIG
  3 ("CV source", "trigger source"; cnt+ and cnt- as in Quantermain, with
  TRIG 1 and TRIG 3 as the octave gates). With "seq_mode" TR+1 to TR+3,
  TRIG 2 (A) and TRIG 4 (B) step through two to four slots from "scale #".
  CV 2 (A) and CV 4 (B) go where "CV aux." says: scl# (slot), root, oct,
  trns or mask. "CV source" Turing uses a built-in shift register.
- **Out.** A and B: each channel's pitch CV. C (for A) and D (for B) by
  "aux.output": gate (default, about +6 V, "--> pw" long, 25 ms), copy
  (the pitch again, "--> aux +/-" octaves away) or asr (the note before).
- **Start with** "scale #", "scale", "root #1", "Active notes",
  "seq_mode". The slot rows shown are those of the slot "scale #" picks.
- **Hardware mode:** user scale notes.

### Quadraturia

Four LFOs from one wavetable, locked to one master rate; the other three
follow at set phase offsets or frequency ratios. Use it for four related
modulations: sweeps across a filter bank, panning, rhythmic accents.

- **In.** TRIG 1 resets all phases. TRIG 2 high freezes them. TRIG 3 sets
  the rate by tapping or a clock, when "Tap tempo" is on. TRIG 4 high
  multiplies or divides the rate by "TR4: MULT" (x2). CV 1 is rate, CV 2
  shape, CV 3 spread, and CV 4 goes where "CV4: DEST" says (cplg, the
  coupling, by default).
- **Out.** A to D: the four LFOs. At the default "Output range" they swing
  from about −3.75 V to +5.2 V, centred near +0.75 V, not 0 V; "Output
  range" narrows the swing and "Offset" raises it.
- **Start with** "Coarse", "Freq range", "Shape" and "Phase/frq sprd".
  At 0 (the default) all four outputs move together; towards +127 they
  spread up to a quarter cycle apart, and below 0 their rates spread
  instead. "B freq ratio" to "D freq ratio" set ratios to A. No
  hardware-only editor; "Phase reset" is TRIG 1 by hand.

### Low-rents

Two chaotic generators, each running a Lorenz and a Rössler attractor:
smooth voltages that wander and never quite repeat, for slow organic
modulation.

- **In.** TRIG 1 resets generator 1, TRIG 2 generator 2, TRIG 3 both;
  TRIG 4 high freezes both. CV 1 and CV 3 set the speed of generators 1
  and 2, CV 2 and CV 4 their "Rho/c".
- **Out.** By default A, B carry generator 1's Lorenz x and y, C, D
  generator 2's ("Out A" to "Out D" choose any x, y or z of either
  attractor, or a sum or XOR of two). The x and y outputs swing either
  side of about +1.25 V; z rises from the bottom of the output range.
- **Start with** "Freq 1", "Freq 2", "Rho/c 1", "Rho/c 2" and "LFreq 1
  Rng" (the Lorenz speed range; the Rössler ignores it). No
  hardware-only editor.

### Piqued

Four envelope generators with many types (AD to looping multi-stage),
segment shapes, trigger delays and a Euclidean filter on the triggers:
envelopes, looping envelopes used as LFOs, or envelopes on a rhythmic
subset of a clock.

- **In.** Channel A fires from TRIG 1, B from TRIG 2, and so on
  ("Trigger input", which can also take another channel's end of cycle,
  to chain them). The types with a sustain hold while the gate is high.
  CV 1–4 do nothing until a channel's "CV1 ->" to "CV4 ->" rows send them
  to a parameter (attack, decay, sustain, release, Euclidean, delay,
  amplitude, loops); each channel maps them on its own.
- **Out.** A to D: the envelopes, 0 V at rest up to +5 V at full
  "Amplitude"; "Inverted" turns them upside down.
- **Start with** "TYPE" (AD), "Attack", "Decay" (plus "Sustain/Level" and
  "Release" for the four-segment types) and "Attack mult", "Decay mult"
  for longer times. "Eucl length" (Off) with "Fill" and "Offset" thins
  the triggers. Nothing is hardware-only.

### Sequins

Two step sequencers, each holding four sequences of up to 16 steps
quantized to a scale, with a gate or envelope beside each pitch. Play
modes add an arpeggiator and steps chosen by CV.

- **In.** Both channels clock from TRIG 1 ("clock src" TR1 or TR3; set B
  to TR3 to clock it apart). "reset/mute" is off: RST2 and RST4 reset from
  TRIG 2 or TRIG 4, and =HI2, =LO2, =HI4, =LO4 skip steps while that input
  is high or low. "playmode" TR+1 to TR+3 change sequence on TRIG 2 (A) or
  TRIG 4 (B); S+H#1–4 and CV#1–4 let CV 1–4 pick the step. Otherwise CV
  1–4 do nothing until assigned under the "CV mapping" heading.
- **Out.** A and B: each channel's pitch CV. C (for A) and D (for B) by
  "aux. mode": gate (default, about +6 V, "--> pw" long), copy (the pitch,
  "--> aux +/-" octaves away), or an AD, ADR or ADSR envelope, 0 to +5 V.
- **Start with** "scale", "root", "sequence #", "Steps", "direction",
  "mult/div". On a new module every step is at the same pitch (the root),
  so the outputs repeat one note until the steps are edited.
- **Hardware mode:** step pitches and sequence lengths, and user scale
  notes.

### Dialectic Ping Pong

Four envelopes modelled on a thrown ball: it rises, falls and bounces,
each bounce lower and quicker than the last. Use it for bouncing decays
and patterns that speed up as they die away.

- **In.** Channel A fires from TRIG 1, B from TRIG 2, and so on ("Trigger
  input"). CV 1–4 do nothing until a channel's "CV1 ->" to "CV4 ->" rows
  send them to grav, bnce, ampl, vel or retr.
- **Out.** A to D: the bounce, 0 V at rest, rising as far as +6.25 V.
- **Start with** "Gravity", "Bounce loss", "Velocity" (how hard the ball
  is thrown) and "Amplitude" (the height it starts from, 0 by default).
  "Hard reset" Yes restarts it from that height at each trigger. No
  hardware-only editor.

### Viznutcracker sweet

Four bytebeat generators: short integer formulas run step by step, giving
8-bit, glitchy waveforms. Run fast they are audio; slow or stepped by
triggers, they are stepped CV.

- **In.** Channel A takes TRIG 1, B TRIG 2, and so on ("Trigger input"):
  a trigger restarts the formula (from the loop's start with "Loop mode"
  Yes), or with "Step mode" Yes moves it one step. CV 1–4 do nothing until
  a channel's "CV1 ->" to "CV4 ->" rows send them to equ, spd, p0 to p2,
  the loop points or pitch.
- **Out.** A to D: the waveform, centred on 0 V and nominally ±5 V; the
  part below −3.75 V is clipped by the output range.
- **Start with** "Equation", "Speed", "Pitch" and "Parameter 0" to
  "Parameter 2". It runs without any trigger. No hardware-only editor.

### Acid Curds

A quantizer that harmonizes: at each clock it quantizes a CV and builds a
four-note chord on it, stepping through progressions of chords you make.

- **In.** TRIG 1 is the clock: it samples CV 1 ("CV source") and updates
  the chord. TRIG 2 moves to the next chord ("chords trg src" TR2; TR1
  moves at every clock). TRIG 3 changes progression in the "playmode"
  TR3+1 to TR3+3. TRIG 4 high sends the progression back to its first
  chord. CV 1–4 have no other use until assigned under "CV mapping".
- **Out.** A: the base note. B, C, D: the other chord notes. Pitch CV.
- **Start with** "scale", "root", "progression", "Chords", "playmode".
  A new module's chords are all the "fifth" quality, one per progression.
  Chord intervals count steps of the scale, so in the default Semitone
  scale that "fifth" is a major third; pick a seven-note scale such as
  Ionian for real fifths.
- **Hardware mode:** the chords and progressions ("Chords" row), and user
  scale notes.

### References

Fixed reference voltages, one per output, for tuning oscillators and
checking how well they track.

- **In.** None in Shoal. On the hardware TRIG 4 feeds a frequency and
  tempo meter, set up by channel D's "Notes/BPM :", "A above mid C",
  "> mantissa" and "> ppqn"; Shoal does not measure frequencies yet, so
  the meter reads nothing.
- **Out.** A to D: "Octave" plus "Semitone" as pitch CV (0 V at first).
  A "Mod range oct" other than 0 makes the output jump between that pitch
  and one that many octaves away, every "Mod rate (s)" seconds.
- **Start with** "Octave" and "Semitone".
- **Hardware mode:** "Autotune", which needs the same frequency reading
  and so has nothing to measure in Shoal.

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
  hardware mode"). While one is open the list shows the brass banner and
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

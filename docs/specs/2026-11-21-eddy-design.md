# Eddy: a tape loop that also plays files: design

Status: design, 2026-11-21, approved by the user ("yep looks good"; the
waveform window added the same day, at their word). Panel:
`2026-11-21-eddy-mockup.png`, drawn through `tools/gen_panels.py` from a
draft description that is not in the repo; its positions are listed under
Panel. Grows from the idea in `docs/future/eddy.md`.

(2026-11-22: built, and accepted by the user on test build 2.0.27
("everything in the checklist behaves as expected"). Notes marked "as
built" below say where the build differs from or settles what this
design says; the manual is `docs/eddy/README.md`.)

## Intent

The roadmap put Eddy next: a sound-on-sound looper for layered, slowly
decaying loops, with a speed that runs backwards through zero. The user
asked for research on other loopers first (forsitan's, Panther Cap, which
they have used, and Sapphire Echo), and then said:

- "draft a design, but I think I want the ability to add and save wave.
  how about a looper/sampler combo? Or is that too ambitious?"

It is not too ambitious if the two are one thing and not two. Eddy has
one tape. It is filled by recording or by loading a file, and from then
on it is the same tape: it comes round, at any speed in either direction,
through a window set by Start and Length; new sound can be layered onto
it; and it can wear a little on every pass. A trigger plays it from the
start, once or looped. The tape can be saved as a file, and it is kept
with the patch.

What would make it too ambitious is the rest of Panther Cap: grains,
pitch apart from speed, polyphony, random jumps. Those are left out; the
first two are Spindrift's ground.

The picture is an eddy: water that keeps coming round.

## Research, in short

- **forsitan Tabes** (code local) is the nearest thing: record, loop,
  re-record each pass slightly worse, dub. Taken from it: one Dub Level
  knob that is both the new layer's level and how much of the old is
  erased; wear inside the loop; the click-free seam; the end-of-cycle
  output. Its loop is not saved and it cannot load a file.
- **Panther Cap** (manual read; closed source) is a sampler that records:
  load or record, trim with Start and Length, then warp. Taken from it:
  load and save of files, Start and Length, a play trigger, loop on or
  off. Left out: Time apart from Pitch, grain Size and Overlap, Rnd,
  polyphony, the clock, the filter, the follower.
- **Sapphire Echo** (code local) is a multi-tap delay with Freeze, not a
  looper. Wake's Freeze already covers that kind of loop. Nothing taken.
- **forsitan Olim, Vestigia, Spira, Textor** are always-running buffers
  or grain loopers: Wake's and Spindrift's ground.
- **monome softcut** (from memory, not re-read) is the known design for a
  record head that travels with the play head at any speed, with a
  "pre" level (what is kept) and a "rec" level (what is added). It is the
  reference for the write head below.

## Goals, non-goals, anti-goals

Goals:
- Record a stereo loop of up to 5 minutes, and hear it come round with no
  click at the join.
- Load a WAV file onto the tape, by the menu or by dropping it on the
  panel, at the right pitch whatever its sample rate.
- Save the tape as a WAV file.
- The tape is kept with the patch: a saved piece reopens with its loops.
- Speed from twice forwards, through stopped, to twice backwards, with
  pitch following as on tape; by knob, by CV and by V/OCT.
- A window on the tape, Start and Length, that can be moved while it
  plays.
- Layering: Dub adds the input to the tape, and takes away some of what
  was there, so layers never pile up without limit.
- Wear: each pass can cost the tape a little, so a loop decays over
  minutes or hours. At zero the loop repeats exactly, for ever.
- Smear: the next turn can start before the last has ended, so a short
  loop becomes a wash.
- Wear and dubs never cost the original: the sound as it was loaded or
  first recorded is kept beside the tape, travels with the patch, and
  comes back at a button or a trigger (the user: "add both").
- Played as a sample: a trigger starts it from Start; with Loop off it
  plays once and stops.
- A window that shows the tape's waveform, the part of it in play, and
  where the head is (the user: "i want a waveform window").

Non-goals:
- Grains, and pitch that is independent of speed (Spindrift).
- Polyphony: one stereo tape, one play head. A polyphonic cable is summed
  (as Wake).
- A clock input and recording quantised to it. This music is beatless;
  it can be added if a patch calls for it.
- Setting Start and Length by dragging in the window. The window shows;
  the knobs set. (The designer's call; it can be added once the window
  exists.)
  (2026-11-22, after the listening build: dragging in the window now sets
  Start and Length, through the two knobs: drag to select, drag an end
  line to move that end, double-click for the whole tape; README.)
- Zooming or scrolling the window: it always shows the whole tape.
- A Send and Return inside the loop (Tabes has one). Worth having later;
  it needs four more jacks.
- Long files. Eddy takes files up to its tape's length; a field
  recording of ten or thirty minutes is another module's work (the user,
  of files of 100 to 300 MB: "we can work a long-file sampler module
  into the roadmap"). See `docs/future/fetch.md`.
- Several tapes, or several loops of different lengths. Two Eddys do it.
- Undo a step at a time. Restore goes all the way back to the original,
  dubs and wear together.
- Wow and flutter. Speed has a CV; Sway into it is wow.
- Random jumps of Start and Length (Panther Cap's Rnd): Squall into
  Start's CV does it.

Anti-goals:
- A click at the loop's join, at Rec or Dub in or out, at a retrigger, at
  Play or Stop, at a move of Start or Length, or when a file arrives.
- A click or a step written onto the tape: it would come round for ever.
- A level that builds without limit under repeated dubs, or a DC offset
  building on the tape.
- A loop at Wear 0 that fades, dulls or drifts.
- An audio dropout, or a window that hangs, when a file loads or the
  patch saves.
- A patch that reopens with the tape gone, or with a different tape.

## Approaches

1. **One tape, two ways to fill it (chosen; the designer's call).**
   Recording and loading both put sound on the same tape, and one engine
   plays, dubs and wears it. Every control means one thing. The sampler
   half costs a file reader, a play trigger and a Loop switch; the looper
   half costs the write head and wear.
2. **Two modes on a switch, Loop and Sample.** Each mode could go
   further (a sample mode with one-shot polyphony over V/OCT, a loop mode
   with a clock), but it is two modules behind one panel: controls that
   change meaning with the switch, twice the tests, and a wider panel.
3. **A looper now, files later** (a second module or an expander).
   The smallest build, and close to Tabes. But the user asked for files,
   and adding them later means reworking how the tape is held, saved and
   swapped; it is cheaper to design the tape for them now.

## Panel (16 HP)

As the mockup, which passes the generator's checks at these positions
(mm). Five columns at x = 7.8, 24.22, 40.64, 57.06 and 73.48: the first
and last are as far from the panel's edges as Wake's and Narrows's.

- Window: 71.28 x 14.0 at (5.0, 19.5), with a ruler under it in tenths.
- Speed at (40.64, 54.0) on a face of radius 16; Rec and Dub in the first
  column and Play and Loop in the fifth, at y = 47.5 and 60.5.
- y = 76: Start, Length, (Speed's attenuverter, at 78), Wear, Smear.
- y = 90: Start CV, V/OCT, Speed CV, and Dub Level and Mix at the small
  (attenuverter) size.
- y = 102: IN L, REC, PLAY, the Restore button, L.
- y = 116: IN R, DUB, EOC, Restore's trigger jack (TRIG), R.
- **The audio jacks are where Wake's and Narrows's are**: the inputs
  one over the other in the first column and the outputs in the last, at
  y = 102 and 116, L over R, on ivory. EOC is on ivory in the middle
  column.

The waveform in the mockup is an illustration: the part in play bright,
the rest dim, thin lines at its two ends and an ivory line for the head.
The tape's length in the corner and the Smear mark are not drawn in it.

- **The window**, across the top: dark glass in a brass bezel, as Shoal's
  screens, drawn in cyan. See "The window" below.
- **Speed**, the hero, under it, on a face lettered -2, -1, 0, +1, +2.
  The spiral of the sketch is drawn round it if it fits the generator's
  rules.
- Four lit buttons: **Rec**, **Dub**, **Play**, **Loop**; and
  **Restore**, a push button with a trigger jack beside it.
- Knobs: **Start**, **Length**, Speed's attenuverter, **Wear**,
  **Smear**; smaller, **Dub Level** and **Mix**.
- Jacks in: IN L, IN R, REC, DUB, PLAY, RESTORE, V/OCT, Speed CV,
  Start CV.
- Jacks out: L, R, EOC.

## Controls and behaviour

### The tape

- Stereo, up to 5 minutes at any sample rate. Empty when the module is
  added.
  (As built, 2026-11-22: and at most 2^24 frames, so above about 56 kHz
  the tape is shorter: 2:54 at 96 kHz, 1:27 at 192 kHz.)
- It is held at the engine's sample rate. A file at another rate, and the
  tape itself when the engine's rate changes, are converted off the audio
  thread; until the new tape is ready the old one plays (or silence, for
  a rate change).
  (As built, 2026-11-22, after the final review's I1 and I3: converted
  once. A file or the stored tape that has not gone live when the rate
  changes (on Rack's "Auto" rate this is ordinary at launch: the engine
  runs at 44.1 kHz until the audio device's first callback) is read again
  from its source at the new rate, and the patch's Play and Dub are
  applied when it is offered; only a tape that went live is converted,
  from itself, and again from that same tape if the rate moves before the
  conversion plays. A set made for an older request never takes the
  place of one made for a newer one. The worker's decisions are in
  `src/eddy/WorkerBook.hpp`, tested in `test_book.cpp`. A tape held short
  by 2^24 frames is logged, and a stored tape loaded so is left in the
  folder until the tape changes.)
- A new tape (a recording ending, a file arriving, Clear) takes over by a
  short fade, never a step.

### The window

- It shows the whole tape, left to right, as its waveform (the two sides
  together, as peaks).
- The part between Start and the window's end is bright and the rest is
  dim, so Start and Length can be set by eye. When the part in play wraps
  past the tape's end it is shown in two pieces. Start moved by CV is
  shown moving.
- A line marks the play head. The stretch that Smear overlaps is marked
  at the join.
- While recording, the waveform grows from the left as the tape fills,
  against the 5 minute limit. (Changed at the listening stop, 2026-11-21:
  against a span that starts at 4 s and doubles each time the recording
  reaches its end, up to the limit, so the recording fills half to all of
  the window; the time recorded is drawn in the corner.)
- It shows the tape as it is now: a dub appears as it lands, and wear
  shows as the waveform thins.
- Empty, it says so (a flat line; the builder may add a word).
- A ruler under the glass, printed on the panel, marks tenths of the
  tape; the tape's length is drawn in the window's corner.
- Drawing never touches the audio thread's time: the waveform is a
  reduced copy of the tape (a few hundred columns of peaks), brought up
  to date a part at a time.

### Rec

- Press Rec: recording starts on a new tape, and the old tape falls
  silent (it is kept until the new one is long enough to stand). Press again: it ends, its
  length is the tape's length, Play comes on, and it plays from its
  start (once, if Loop is off). The REC jack
  does the same with a gate: high records.
  (As built, 2026-11-22: the REC and DUB gates act on their edges: a
  rising edge starts, a falling edge ends. A gate already high when a
  recording ends, a file loads or the tape is cleared starts nothing
  until it falls and rises again. The button and the gate can each end
  what the other started.)
- Recording runs at normal speed whatever Speed says.
- At 5 minutes recording ends by itself.
- A recording under 50 ms is thrown away and the tape before it stays.
  (2026-11-22, after the listening build: a quick double tap on REC
  erased the tape, since a person's double tap is 100 to 250 ms. Now a
  recording the button starts or stops is thrown away under 0.3 s; one
  the REC gate starts and stops keeps the 50 ms. The old waveform stays
  in the window, dimmed under the growing recording, until the recording
  is kept.)
- The join is made from real audio: the input goes on being taken for a
  moment after Rec ends, and is faded into the tape's start, so the end
  runs into the beginning (as Tabes).
- IN R takes IN L when unpatched: a mono source records to both sides.
- While recording, the input is heard at the outputs whatever Mix says.

### Play, Loop and the PLAY jack

- **Play** runs and stops the tape, by a short fade. Stopped, the head
  stays where it is.
- A trigger at **PLAY** starts the tape from Start (from the window's end
  when Speed is backwards), running or not. A retrigger while it plays
  fades from the old place to the new.
- **Loop** on (the default): the head comes round the window for ever.
  Off: it plays to the window's end and stops, and Play goes off with
  its light. A PLAY trigger turns Play on.
- **EOC** gives a 1 ms trigger each time the head reaches the window's
  end, in either direction.

### Speed

- The knob runs -2 to +2 times normal speed, +1 by default, with a small
  flat stretch at 0 (stopped), at +1 and at -1 so they are easy to find.
- **Speed CV** with its attenuverter adds to the knob: ±5 V is ±2 at
  full.
- **V/OCT** multiplies the result: 0 V is as set, +1 V is twice as fast,
  an octave up. With V/OCT the speed may reach 4 times either way.
- Speed changes glide a little, as a reel has weight; a V/OCT step does
  not (it lands within a few milliseconds, so the tape can be played as
  a voice).
  (As built, 2026-11-22: the flat stretches are 1 % of the knob's travel
  either side of -1, 0 and +1; the glide is a 60 ms lag on knob plus CV;
  a V/OCT step lands within 2 ms; the output is fully back by 1/64 of
  normal speed.)
- Near stopped the output fades to silence, so a stopped tape gives no
  DC.

### Start and Length

- **Start** places the window's beginning anywhere on the tape. **Length**
  sets how much of the tape the window holds, from 10 ms to all of it; at
  full with Start at 0 the window is the whole tape. A window that would
  run past the tape's end wraps to its beginning.
- **Start CV**: ±5 V moves Start across the whole tape.
- Moving either while the tape plays does not click: the head fades to
  where it should now be.
- The join at the window's end is always a fade (see Smear).

### Dub and Dub Level

- **Dub** on (button or DUB gate): the input is written onto the tape at
  the head, as the tape goes by, at whatever speed and direction it is
  going. Played back at the same speed, a dub sounds as it was played.
- **Dub Level** is one knob for two things, as on a tape machine and as
  Tabes: the level of the new layer, and how much of the old is erased
  under it. Low: a quiet layer, almost nothing lost. Noon (the default):
  the new layer arrives strong and the old sinks a step each time. Full:
  the input replaces what was there (a punch-in). At zero nothing is
  written.
- New layers go on before a soft bound, so stacking saturates gently and
  settles; it cannot clip or run away.
- Dub in and out are faded, on the tape.
- Dub does nothing on an empty tape, while recording, or when the tape is
  stopped or slower than about an eighth of normal speed (there is too
  little tape passing to write on); the write fades out and in around
  that speed.
  (As built, 2026-11-22: writing, Dub's and Wear's both, is off below
  1/16 of normal speed and fully on from 1/8. A dub made away from
  speed 1 is not band-limited: slower, the input averaged over the
  head's travel folds back what lands above the tape's Nyquist; faster,
  it leaves images. Pinned in `src/eddy/test_alias.cpp`.)
- While dubbing, the input is heard at the outputs whatever Mix says.

### Wear

- **Wear** is how much each pass costs the tape, written back as the
  head goes by: the highs dull, the level sinks a little, loud parts
  press into saturation, hiss comes up, and dropouts appear and grow
  more frequent.
- At 0 the tape is not rewritten at all: the loop repeats exactly.
- Low, a loop takes hundreds of passes to change; at full it crumbles in
  a few minutes.
  (As built, 2026-11-22, the values the user accepted, from
  `src/eddy/EddyTuning.hpp`: at Wear w, each pass sinks the level by
  0.5 dB x w^2, moves the tape 0.3 x w^2 of the way to a 4 kHz low-pass,
  drives it into the soft bound by 1 + w (scaled back), adds hiss at
  -70 dB re 5 V RMS x w, and drops out 0.5 x w^2 times a second of tape
  passed, for 5 to 40 ms, to 0.3 to 0.8 of the level, with 2 ms edges.)
- Wear acts only where the window is, since that is the tape that
  passes the head.
- No CV in this build.

### The original, and Restore

- When a file loads or a recording ends, an untouched copy of it is kept
  beside the tape: the original. Dub and Wear change the tape and never
  the original. The file on disk is never written to.
- **Restore** (the button, or a trigger at its jack) puts the original
  back on the tape: wear and every dub are gone. The head keeps its
  place and the tape keeps playing; the change is a short fade, heard
  within a few milliseconds of the trigger.
  (As built, 2026-11-22, after the final review's I4: the fade is equal
  in gain, not in power: the tape and the original at one place are the
  same audio, or nearly, and an equal-power fade between them swelled
  the output 3 dB for its 10 ms. Every other fade between two reads, the
  join's included, is between different audio and stays equal in power.)
- Restore does nothing on an empty tape or while recording. It ends a
  dub in progress.
- Menu: **Make this the original** keeps the tape as it now is, worn and
  dubbed, as the new original.
- The tape was first set at 2 minutes; the user raised it to 5. If a
  wearing 5 minute tape makes autosave felt, it comes down again.
  (As built, 2026-11-22: Rack's autosave does not ask a module to save
  its storage; only saving the patch (or duplicating or deleting the
  module) does, so a wearing tape is never written at autosave, and after
  a crash the tape is as of the last save. A full 5-minute tape saves in
  about a quarter of a second on the Mac: 0.05 to 0.1 s for the files,
  about 0.16 s for Rack's archive. It stays at 5 minutes.)
- The original doubles the tape's memory (about 115 MB each for 5 minutes
  at 48 kHz).

### Smear

- **Smear** is the length of the fade at the window's join. At 0 it is
  the shortest fade that does not click (about 10 ms, less in a short
  window): a plain loop. Turned up, the next turn starts before the last
  has ended and the two overlap, up to half the window at full, where
  the loop plays over itself as a wash.
- The overlap is equal in power, so the level does not dip or rise across
  it.
- The tape still has one write head: a dub lands once per turn of the
  tape, however many times it is heard.
  (As built, 2026-11-22: each voice has its own write head. During the
  overlap at the join both voices write what they play, each scaled by
  its fade, so a dub laid down across the join is heard once at its
  level next turn. A dub made in the first turn after Play, a retrigger
  or a window move is heard through the next join within about 2 dB of
  its level, then exactly.)
- Nothing outside the window is played, with one exception. The shortest
  fade, at Smear 0, runs on into the tape just past the window's end (for
  a whole recording, the moment taken after Rec ended), so a turn is
  exactly Length long. Any overlap beyond that comes out of the turn:
  each turn starts that much sooner, the loop is that much shorter, and
  EOC follows the turns as heard (as Tabes). The designer's call.

### Mix, inputs and outputs

- **Mix** runs from the input alone to the tape alone; the default is the
  tape alone.
  *Changed at the listening stop, 2026-11-21:* Mix m (0 to 1) is a
  balance with both at full in the middle: input gain min(1, 2(1 − m)),
  tape gain min(1, 2m), so 0 is the input alone, 0.5 the input and the
  tape both at full, and 1 the tape alone. The default is 0.5, so the input
  is heard through the module whether or not it is being recorded.
- Audio is ±5 V. The outputs are held under a soft limit.
  (As built, 2026-11-22: exact up to 8 V, never past 10 V; the same
  bound guards what is written to the tape.)
- Recording puts the input on the tape unchanged, offset and all, so a
  plain loop is exact. What is written back by Dub and Wear passes a DC
  blocker, so no offset can build.
  (As built, 2026-11-22, after the final review's I2: the 5 Hz blocker
  is on what a dub adds (the input), not on the tape going round. On the
  whole of what was written it acted once a pass and its loss built pass
  over pass: 15 dB at 50 Hz in 300 passes of Wear at 2 %, whose law says
  0.06 dB. Wear's saturation takes off the offset it puts on lopsided
  material, and the tape's own offset is trimmed at 0.01 Hz (a tape
  dubbed into the bound at a low Dub Level keeps each clipped frame's
  sign, and the share of them would otherwise stand as DC). Each pass
  now costs the tape exactly what the Wear and Dub laws say, from 30 Hz
  up; at Wear 0.5 and 1, 50 passes keep about 2.3 dB more at 50 Hz and
  0.15 dB more at 1 kHz than build 2.0.27 did.)
- Polyphonic cables are summed.

### Files

- **Load file** (menu), or a file dropped on the panel: WAV, mono or
  stereo, any usual bit depth and sample rate. A mono file plays on both
  sides. A file with more than two channels gives its first two. A file
  longer than 5 minutes gives its first 5 minutes.
- Loading replaces the tape, and stops recording and dubbing. Play stays
  as it was: if it is on, the new tape plays from Start.
- A file's full scale (±1.0) is ±5 V on the tape, and back again on
  saving.
- **Save file** (menu) writes the tape as it now is, worn and dubbed, as
  a 32-bit float stereo WAV at the engine's rate, so a tape dubbed hotter
  than ±5 V is saved without clipping and loads back exactly. The whole tape, not the
  window.
  (As built, 2026-11-22: it loads back within one rounding step of a
  float (the level goes through ÷5 and ×5), not to the bit; the tape
  stored with the patch, in volts, is exact. It writes at Rack's rate,
  by the user's decision ("leave as is"): a file at another rate is
  converted when it loads, so a 96 kHz file loaded with Rack at 48 kHz
  is saved at 48 kHz.)
- **Clear** (menu) empties the tape.
- A file that cannot be read leaves the tape as it was and says so in
  Rack's log.
- The reader and writer are `dr_wav` (public domain), added to the repo
  as Fundamental has it.

### With the patch

- Saved: the knobs, Play, Loop, Dub, the path of the last file, and the
  tape and the original, as two files in the patch's storage folder (as Fundamental's
  wavetable oscillator keeps its table).
  (As built, 2026-11-22: three files, part of the patch format:
  `tape.wav` and `original.wav` (32-bit float stereo, in volts, at
  Rack's rate, each its length plus its tail) and `tails.wav` (one frame:
  the tape's tail in frames on the left, the original's on the right).
  About 115 MB each for a full 5-minute tape at 48 kHz. An empty tape,
  or Keep the tape with the patch off, stores nothing.)
- Rec is never saved as on.
- Each is written only when it has changed since it was last written;
  the original changes only at a recording, a file load or "Make this
  the original". A tape that is wearing changes all the time, so it is written
  at every autosave: that write must not hang the window or the audio
  (see Testing), and the builder may lower the 5 minute limit if it
  cannot be met.
  (As built, 2026-11-22: written at every save of the patch, not at
  autosave; see the note under The original.)
- Menu: **Keep the tape with the patch**, on by default. Off, a reopened
  patch loads the last file again from its path, if it is still there,
  and is empty otherwise.

### Bypass and other

- Bypassed, the inputs pass to the outputs.
- NaN or infinite input samples and CVs read as 0 V.
- Initialize empties the tape and returns the buttons to their defaults.

## Code and files

As every module (CLAUDE.md, TESTING.md "Adding a module"):

- `src/eddy/`: Rack-free headers, the module a thin map onto them. Likely
  parts: the tape and its heads; wear; the control laws; the engine;
  file reading, writing and rate conversion (Rack-free, tested on small
  files made in the test); `Eddy.cpp`.
- **The tape and its heads are shared code**, in a header under `src/`
  with its own tests, because Spindrift will read the same kind of
  buffer. A read head at a fractional place moving at any speed in
  either direction (the Hermite read of `src/DelayLine.hpp`), and a
  write head that travels with it and lays the input down at that speed.
  (As built, 2026-11-22: shared under `src/`: `Tape.hpp` (the tape and
  its write head), `BandLimitedRead.hpp` (the read, replacing the
  Hermite one), `Resample.hpp`, `WavFile.hpp/.cpp` over the vendored
  dr_wav, `Overview.hpp` (512 columns of peaks), `TapeGesture.hpp` and
  `TapeWindow` in `widgets.hpp`, and `testing_alias.hpp` for the tests.
  The voices and their join, `src/eddy/Voices.hpp`, and the file jobs,
  `src/eddy/TapeJobs.hpp`, are Eddy's. The window letters the tape's
  length at run time, so the plugin ships `D-DINCondensed.ttf` with its
  licence. Removing the module while a long file loads holds Rack's
  audio until the load ends: the worker is joined.)
- **Less of Wake is reused than the roadmap says.** `src/DelayTap.hpp`
  reads a delay behind a write point that always moves; Eddy's tape
  stands still and the head moves over it, backwards too. The Hermite
  read, the way the frozen loop's join is made, `FastMath.hpp`, and
  Wake's soft bound and tone filters carry over. The write head is new.
- No allocation on the audio thread. The tape's memory is taken off it:
  when the module is added, when a file loads, when the rate changes.
- The tape is swapped under a running engine (file load, rate change,
  patch load) without a lock on the audio thread.
- `src/eddy/bench.inc` (idle, outputs, busy: playing at Speed 2 with Dub,
  Wear and Smear up), the panel entry, plugin.json (slug `Eddy`), manual
  `docs/eddy/README.md`, README table, CHANGELOG, roadmap and
  `docs/future/eddy.md` marked built.
- CLAUDE.md reserves the name Eddy for a random sampler from an older
  list; that line is changed to say what Eddy became.
- **The window is a shared widget** in `src/widgets.hpp`: a glass that
  draws a waveform from a reduced copy, with a bright span and a head
  line, in the theme's colours. It is the first drawn window outside
  Shoal's pixel screens, and Spindrift's and Log's windows start from
  it. The reduced copy and its updating are Rack-free and tested.

## Testing

TESTING.md applies in full. Particular to Eddy:

- **Exact loop.** At Speed +1, Wear 0, Smear 0, Dub off: the output
  repeats with the tape's length, and outside the join it is the
  recorded input to the bit, after 1000 passes.
- **Length.** A recording of N samples gives a loop of N samples, at
  44.1, 48 and 96 kHz.
- **Speed.** A sine on the tape plays at Speed times its pitch across the
  range and backwards (the phase runs the other way); V/OCT is an octave
  a volt within 1 cent over ±2 V; at 0 the output is silent with no DC.
- **Window.** Start and Length give a loop of the stated length at the
  stated place; a window past the tape's end wraps.
- **Dub.** At each Dub Level, the old material's level after one pass
  and the new layer's level against the stated table; at full, the old
  is gone; at zero, the tape is unchanged to the bit. A dub made at
  Speed 0.5 and at -1 plays back, at the same speed, as the input within
  a stated error. Repeated full-level dubs of ±10 V noise for 1000
  passes: the tape stays under its bound, no DC builds.
- **Wear.** At 0 the tape's memory is unchanged after 1000 passes. Above
  0, the level and spectral centroid fall pass by pass at a rate that
  rises with the knob; hiss stays under a stated level; the tape never
  grows.
- **Restore.** After dubs and a thousand worn passes, Restore leaves the
  tape equal to the original to the bit, with no click at the output and
  no allocation, lock or long copy on the audio thread; triggered at
  audio rate for a minute it stays so. The original is unchanged by
  everything except a recording, a file load and "Make this the
  original".
- **Smear.** At 0 the join is the stated short fade. Above 0, noise
  through the overlap keeps its RMS within 0.5 dB.
- **Play.** A PLAY trigger starts at Start within a sample or the fade's
  length; Loop off stops at the window's end with one EOC; EOC fires
  once a turn in both directions.
- **No clicks, heard or written.** Rec in and out; Dub in and out; the
  join; retrigger; Play and Stop; Start and Length by knob and CV; a file
  arriving; Clear: no sample-to-sample jump, at the output or on the
  tape, beyond what the signal's own slope explains.
- **Files.** Round trip: a tape saved and loaded again equals itself
  to the bit. Mono, stereo and four-channel files; 16-bit,
  24-bit and float; 44.1, 48 and 96 kHz files into a 48 kHz engine play a
  sine at its pitch within 1 cent. A truncated file, an empty file and a
  file that is not a WAV leave the tape as it was. A file over 5 minutes
  gives 5 minutes.
  (As built, 2026-11-22: the tape stored with the patch comes back to
  the bit; a file saved from the menu comes back within one rounding
  step.)
- **With the patch.** Golden saved state: a saved module reloads with the
  same tape and the same original, to the bit. A patch saved without its tape file loads with
  an empty tape and no error.
- **Swapping the tape.** A file loading, a rate change and a patch load
  under a running engine: no lock, no allocation and no free on the
  audio thread (sanitizers, and the allocation check).
- **Saving.** Writing a full 5 minute tape takes under a stated time
  (measured, and set at the listening stop); a tape that has not changed
  is not written again.
- **The window.** The reduced copy matches the tape's peaks column by
  column after a recording, a file load, a dub and a thousand worn
  passes; the bright span and head positions match Start, Length and the
  head for stated cases, including a wrapped span. Its cost is in the
  bench (TESTING.md 10): drawing, and keeping the copy up to date while
  dubbing and wearing a full tape. The smoke test's screenshot shows it
  with a tape loaded.
- Sanitizers, extreme and changing sample rates, seeded fuzz of every
  control and gate together, denormals in a wearing loop, bench budgets,
  no libm per sample, Linux and Windows builds.

## Listening stop

Before these are fixed, the user listens on a numbered test build: how
Wear sounds and how fast it goes across the knob (the mix of dulling,
sag, hiss and dropouts); Dub Level's noon; Smear's range; the weight of a
Speed change; the flat stretches on the Speed knob; whether the
window reads well at Rack's usual zoom, and whether dragging in it to
set Start is missed; whether an autosave can be felt with a long wearing tape; the
defaults.

(2026-11-22: dragging in the window was missed; it now sets Start and
Length, as in Non-goals' note.)

## Left to the builder

- How the write head lays sound down at speeds other than 1, and the
  speed under which it stops writing, within the tests.
- What is done about aliasing when the tape is read faster than normal.
  (2026-11-22: every read goes through `src/BandLimitedRead.hpp`. Faster
  than 1, a windowed sinc whose width scales with the speed, cutting at
  the output's Nyquist: from x1.02 up anything landing above it comes out
  at least 57 dB down, and the passband is flat within 0.02 dB to 0.8 of
  Nyquist. At 1 and slower, the same window as an interpolating kernel: a
  whole frame read at 1 is that frame, to the bit; between frames the band
  is flat to 0.8 of Nyquist and the images are 63 dB down or more (the
  Hermite read it replaced lost 0.9 dB of white noise's level and left
  images at -11 dB). The two crossfade between 1 and 1.02. The write is
  unaffected: the voices rewrite frames in place, never what they read.
  Tests: `src/eddy/test_alias.cpp`, `src/eddy/test_band_read.cpp`.)
  (As built, 2026-11-22, a correction to the note above: the images
  slower than 1 are about 63 dB down (measured -63 to -66 dB) for content
  up to 0.4 of the rate; above that they reach about -8 dB. And "a whole
  frame read at 1" holds only from a whole frame: after a glide lands on
  1 the head sits between frames and is interpolated.)
- Where wear's parts sit and how each is made; the hiss and dropout
  generators (seeded, so tests repeat).
- The fade times, the smoothing, and how often controls are read.
- How Restore copies the original back without stalling the audio
  thread (the original can be played from while the copy is made).
- How the tape is swapped without a lock, and which rate converter is
  used (Rack ships libsamplerate and speex).
- How the window is drawn (vector, or the pixel screen), how many
  columns it keeps, and how often it is redrawn.
- The soft bound's curve (reuse Wake's, Ballast's or Rigging's if it
  fits).

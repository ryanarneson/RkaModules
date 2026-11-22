# Eddy

A stereo tape loop that also plays files, 16 HP: water that keeps coming
round. Eddy has one tape. Fill it by recording or by loading a WAV, and
from then on it is the same tape: it comes round at any speed in either
direction, through a window set by **Start** and **Length**; new sound can
be layered onto it with **Dub**; and it can **Wear** a little on every
pass. A trigger plays it from the start, once or looped. The tape can be
saved as a file, and it is kept with the patch.

(First draft. The picture of the panel and the numbers settled at the
listening stop come later.)

## The panel

From the top:

- **The window**: the whole tape as its waveform, over a ruler in tenths.
  The part in play is bright, the rest dim; a thin line at each end of the
  part in play, the head as an ivory line, the stretch Smear overlaps
  marked along the bottom edge, and the tape's length in the top right
  corner. Empty, a flat line. While recording it shows the recording
  filling at least half of the glass: the first 4 seconds across it, then
  8, 16 and so on up to the tape's 5 minutes, with the time recorded in
  the corner; when the recording ends the tape fills the window.
- **REC** and **DUB** on the left, **PLAY** and **LOOP** on the right, lit
  buttons, either side of **SPEED**, the large dial (-2 to +2).
- **START**, **LENGTH**, Speed's attenuverter, **WEAR**, **SMEAR**.
- **START CV**, **V/OCT** and **SPEED CV**; **DUB LEVEL** and **MIX**.
- **IN L**, the **REC** gate, the **PLAY** trigger, the **RESTORE** button
  and, on ivory, the **L** output.
- **IN R**, the **DUB** gate, **EOC** on ivory, **TRIG** (Restore's
  trigger) and the **R** output.

## The tape

Stereo, up to 5 minutes at any sample rate (at 96 kHz and above, 2^24
frames: about 175 s at 96 kHz). Empty when the module is added. It is held
at the engine's sample rate: a file at another rate, and the tape itself
when the engine's rate changes, are converted off the audio thread, and
until a converted tape is ready the module is silent.

A new tape (a recording ending, a file arriving, Clear) comes in by a short
fade, never a step.

## Controls

### Rec

**REC** starts and stops a recording; the **REC** jack records while it is
above 1 V. Recording is always at speed 1, forwards, and takes the input
unchanged. A recording the **REC** button starts or stops is thrown away
if it is shorter than 0.3 s, and the tape before it stays: a quick double
tap on the button does not erase the tape. The tape still pauses for the
moment of the tap (it fades out at the first press and fades back in where
it stopped), and the press turns **DUB** off, as any recording does. One
the **REC** jack starts and stops is kept from 50 ms, so a gate can take a
short slice. From the press the window shows the recording as it grows: a
bright strip and the head from the left edge, the time counting from 0:00,
over the old tape's waveform, now dimmed, until the recording is long
enough to keep; then the old waveform goes. If it is thrown away the old
tape's window comes back as it was. At 5 minutes it ends by itself. When it ends, the tape plays from Start.
**IN R** takes **IN L** when nothing is patched into it; polyphonic cables
are summed.

### Play and Loop

**PLAY** starts and stops the tape. A trigger at the **PLAY** jack plays it
from Start (from the window's end when the speed is backwards). With
**LOOP** off, the tape stops at the window's end. **EOC** gives 10 V for
1 ms at the end of each turn and of a one-shot.

The buttons show what the tape is doing: a press the module cannot take
(Rec with no tape at this rate, Dub on an empty tape) leaves the button
dark.

### Speed

Twice forwards to twice backwards, the pitch following as on tape. The
knob holds exactly at -1, 0 and +1 over a short stretch either side.
**SPEED CV** adds 0.4 per volt through the attenuverter; that sum glides
(60 ms). **V/OCT** multiplies it by an octave a volt. The result is held to
±4. Around 0 the output fades out, so a stopped tape is silent. The tooltip
shows the speed (×); you can type one in.

The tape is read through a fine interpolating filter at every speed.
Faster than ×1 it also takes out what would land above the top of the
audio band, instead of letting it fold back as harsh inharmonic tones;
slower than ×1 it keeps the highs between samples and adds no grit of its
own. Either way the very top of the band (above about 18 kHz at 44.1 kHz)
rolls off a little. At exactly ×1 you hear the tape's own samples.

### Start and Length: the window

**START** sets where the window begins, as a share of the tape; **START
CV** adds the whole tape for 5 V. **LENGTH** sets how long it is, from
10 ms to the whole tape, on a logarithmic scale; its tooltip shows the
time. A window past the tape's end wraps to its beginning (the window shows
it in two pieces). Moving the window leaves the head alone unless it is now
outside it.

You can also set the window with the mouse, in the window itself (the
knobs turn to match, and one Undo puts both back). Drag across the
waveform to select the part to play, either way round. Take hold of either
end line (within about 4 pixels of it) and drag it to move that end alone:
moving the start keeps the end where it is. A selection is at least 10 ms
and never wraps past the tape's end; dragging an end of a window that
wraps keeps its other end and selects the stretch between it and the
mouse. A click without a drag changes nothing; a double-click selects the
whole tape. With **START CV** patched the window is where the knob and the
CV put it, so a drag sets the knob to put the start under the mouse with
the CV as it is at that moment (the knob stops at its ends, so a large CV
can keep the start short of the mouse). Nothing happens while recording or
with an empty tape.

### Smear

The fade at the window's join. At 0, the shortest fade that does not
click; turned up, the next turn starts before the last has ended and the
two overlap, up to half the window, in equal power.

### Dub and Dub Level

**DUB** (or the **DUB** gate, while above 1 V) adds the input to the tape
as it passes, and takes away some of what was there, so layers never pile
up. At Dub Level d the new layer goes on at min(1, 2d) and the old tape
keeps 1 - d². At 0 nothing is written. What is written passes a DC blocker
and a soft bound.

### Wear

Each pass costs the tape a little where the window is: a little level, a
little of the highs, a little saturation, hiss and dropouts. At 0 the tape
is not rewritten at all and the loop repeats exactly.

### Mix

At noon (the default) the input and the tape are both heard at full, so
what you play passes through whether or not it is being recorded. Turning
left fades the tape out, to the input alone at 0 %; turning right fades the
input out, to the tape alone at 100 %. While recording or dubbing the input
is heard at full whatever Mix says. The outputs are held under a soft
limit, which the input and the tape together can reach.

### Restore, and the original

When a file loads or a recording ends, an untouched copy is kept beside
the tape: the original. **RESTORE** (the button, or a trigger at **TRIG**)
puts it back on the tape, wear and dubs gone, with a short fade; the head
keeps its place. It ends a dub, and does nothing on an empty tape or while
recording. The menu's **Make this the original** keeps the tape as it is
now as the new original.

## Files

Right-click the panel:

- **Load file**: a WAV, mono, stereo or more (the first two channels),
  8, 16, 24 or 32-bit integer or 32 or 64-bit float, at any rate. A file
  longer than the tape gives its first 5 minutes. A file's full scale
  (±1.0) is ±5 V. You can also drop a WAV on the panel. Loading stops
  recording and dubbing; Play stays as it was.
- **Save file**: the whole tape as it now is, as a 32-bit float stereo WAV
  at the engine's rate, ±5 V back to ±1.0, so a tape dubbed hotter than
  5 V is not clipped.
- **Make this the original**, and **Clear** (empties the tape).
- **Keep the tape with the patch** (on): see below.

A file that cannot be read leaves the tape as it was and says why in
Rack's log.

## With the patch

Saved: the knobs, Play, Loop and Dub, the path of the last file loaded, and
the tape and the original, as files in the patch's own folder (written only
when they have changed). Rec is never saved as on. With **Keep the tape
with the patch** off, nothing of the tape is saved and a reopened patch
loads the last file again from its path, if it is still there.

A patch whose stored tape is missing or damaged opens with what can be
read (either file alone serves as both), or with an empty tape, and says
so in the log.

**Initialize** empties the tape and returns the buttons to their defaults
(Loop on). **Bypassed**, the inputs pass to the outputs.

# Suggested order for the future modules

Status: a suggestion, 2026-11-15. The designer's ordering of the
thirty-eight ideas in this directory; the user has not ruled on it.

The order serves one goal: dark, space and horror ambient, made in VCV
Rack. It asks of each module, in turn:

1. Does it make a sound or a movement this music needs and the
   collection cannot yet make?
2. Does something later depend on it, as code or in a patch?
3. How much work is it for what it gives?

Effort is a rough guess, not an estimate: **S** is a simple, well-known
process and a plain panel; **M** needs real DSP work or tuning by ear;
**L** needs both, or a window drawn by the module.

The existing three are already in the Instrument look
(`docs/specs/2026-11-15-instrument-look-design.md`): Sway, Fathom and
Shoal moved to it in PR #7, after Shoal's friendly mode landed in PR #6
(both 2026-11-15). Sluice, Fog, Hadal, Ballast, Pan, Squall, Rigging, Wake, Narrows, Eddy, Spindrift, Beacon, Neap, Spring and Doldrums were built in it from the start.

**Changes since (the user, 2026-11-18):** Squall was built next, ahead of
Plimsoll and Lull. Plimsoll is skipped for now, since Ballast's soft limit
and DC blocker already keep the output safe; it moves to the utilities that
wait for a patch to call for them. Lull, a standalone slew, is a low
priority. The numbers below are the original order.

**Changes since (the user, 2026-11-20):** Narrows, a stereo filter, was
added, to be built soon: it follows Wake, which was built the same day
as a smaller module than its sketch (a stereo echo, 14 HP). Narrows has no
number in the original order. It was built on 2026-11-21, polyphonic.

**Changes since (the user, 2026-11-21):** Eddy was designed as a tape
loop that also loads and saves WAV files, with a waveform window, so it
and not Spindrift pays for the first window. Fetch, a player for long
field recordings (the user's are 100 to 300 MB), was added to follow it;
it has no number in the original order. Eddy's Wear covers much of
Rust's ground.

**Changes since (2026-11-22):** Eddy was built (16 HP), with dragging
in its window to set Start and Length added at the listening stop.
Trimming the tape to the window is left for later (issue #66). It left
shared code that Fetch, Spindrift and Log can start from: the tape and
its write head (`src/Tape.hpp`), a band-limited read at any speed
(`src/BandLimitedRead.hpp`), offline rate conversion
(`src/Resample.hpp`), WAV reading and writing (`src/WavFile.hpp`, over
the vendored dr_wav), the waveform overview (`src/Overview.hpp`, 512
columns of peaks, merged in pairs as a recording grows), the window
widget with drag selection (`TapeWindow` in `src/widgets.hpp`, its
gesture in `src/TapeGesture.hpp`), the worker's file jobs
(`src/eddy/TapeJobs.hpp`, Eddy's own but a pattern to copy) and the
aliasing meter for tests (`rkatest::AliasMeter`,
`src/testing_alias.hpp`). Wake's read point (`src/DelayTap.hpp`) did
not carry over. Eddy is also the first module whose aliasing is
measured (`docs/DSP-ALIASING.md`, `TESTING.md` rule 16); measuring the
others that make or bend sound is issue #67.

**Changes since (the user, 2026-11-22, later):** Fetch moves down to the
ideas that wait until they are wanted: Eddy's 5 minutes are enough for
now. Spindrift is next.

**Changes since (2026-11-22, later still):** Spindrift was built (20 HP)
as Beads: Mutable Instruments' DSP vendored and run unchanged
(`src/spindrift/beads/`, `src/spindrift/stmlib/`, every change marked), so
it did not start from Eddy's tape and band-limited read after all, only
from its window's approach and `src/Overview.hpp`. Its window
(`GrainWindow`) draws the buffer with the record head moving across it,
each grain, the delay's taps and the synth's wave. At the listening stop
(test builds 2.0.29 and 2.0.30) the grain count's caption was steadied;
the window, the defaults that make a cloud, the quality in the window and
Beads' ±10 V levels stand as built.

**Changes since (2026-11-23):** Beacon was built (10 HP, polyphonic) after
Spindrift, as the envelope Sluice and Narrows wait on. Against the idea:
no Loop button (envelopes are the focus), a window that draws the
envelope in place of the lamp, 10 HP and not 8, a Hold mode (a slew
limiter, after the Instruō cèis 2) and the inverted output kept. It
adds a gate for each stage, a trigger at every stage start, RETRIG and
Shape. At the listening stop (test build 2.0.34) every stage was made to
take exactly its set time wherever it starts, and the window was made
to draw the running stage from where it really began.

**Changes since (2026-11-24):** Neap was built (8 HP) with its expander
Spring (6 HP). Against the idea: eight cycles after Instruō øchd instead
of four named after tidal constituents, one Weight knob instead of a
height knob per cycle, and a gate (HIGH) above a settable Mark. At the
listening stop (test builds 2.0.38 and 2.0.39) nothing was changed: every
first guess stands. The manual is `docs/neap/README.md`; the design is
`docs/specs/2026-11-24-neap-design.md`.

**Changes since (2026-11-26):** With Neap built, the user asked what
the list was missing and named a Bernoulli gate, a sample and hold and a
true clock. Five ideas were added, none with a number in the original
order: Fluke (the Bernoulli gate), Semaphore (logic and a comparator),
Tender (aux sends for Ballast), Ebb (mutes and slow fades) and Beam
(stereo width). Sextant gained a stepped mode in its page, in place of a
sequential switch of its own. Hold moves to the head of the utilities.
Knot moves up from "only if wanted" into the utilities: Sway, Squall,
Wake and Shoal now all take a clock and nothing in the collection is
one (Shoal's thirteen apps are clocked, and none is a clock, against
what this page said of Knot before); it should be designed round slow ratios (/16 to /64) more than
round the sketch's multiples. The utilities table below is in the new
suggested order. The designer suggests dropping Lull (Beacon's HOLD
mode and Squall's Swell cover it) and Reef (Tackle covers it); the user
dropped both on 2026-11-27 (see "Dropped", at the foot). Doldrums is the suggested next large build,
with the small utilities filling in round it.

**Changes since (2026-11-26, later):** Doldrums was built (16 HP), the
last of the texture modules in the original order. Against the idea: a
30 s tape of the input with a window and a Position control, not a freeze
of the present alone; Stretch from x1 to a true stop; the tuned comb
(Harmonics) played from polyphonic V/OCT; a trigger, a RUN gate and a
play-once setting so a pass can be recorded into Eddy; a frozen tape kept
with the patch. It did not need a worker thread for the audio (each
grain's work is spread over the samples after it), only for the tape's
file. It vendors PFFFT (`src/dep/pffft/`, wrapped as `rka::RealFft` in
`src/Fft.hpp`), the plugin's first FFT, and lifted Eddy's soft bound to
`src/SoftBound.hpp`. Its window is its own (`src/doldrums/TapeView.hpp`, a
ring that scrolls), not Eddy's `TapeWindow`. In its first listening stop: the
user has heard the test builds from 2.0.46 (2.0.50 is the fifth). The manual is
`docs/doldrums/README.md`; the design is
`docs/specs/2026-11-26-doldrums-design.md`.

## 1. A whole patch from RKA alone

With Sway, Fathom and Shoal the original six here (Sluice, Fog, Hadal,
Ballast, and Plimsoll and Lull, since moved and dropped) made a complete dark ambient patch:
sound, space, loudness under control, stereo, and a safe output.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 1 | [Sluice](sluice.md) (built) | S | Four VCAs. The simplest DSP in the list, so the right first module to build new in the look; and nothing else can make a CV control loudness. |
| 2 | [Fog](fog.md) (built) | L | Reverb. The largest single gain for this music: Fathom into Fog is already a piece. |
| 3 | [Hadal](hadal.md) (built) | S | Rumble and noise. Cheap, immediately in genre, and the noise source Rigging will want. |
| 4 | [Ballast](ballast.md) (built) | M | Stereo mixer with level CV; pan in its Pan expander (built). From here a patch needs no other maker's mixer. |
| new | [Tender](tender.md) | M | Aux sends and returns for Ballast, as an expander. Fog, Wake, Spindrift and Eddy are most useful shared between channels, and today each channel needs its own. Probably the most useful missing piece for a real patch. Added 2026-11-26. |
| new | [Ebb](ebb.md) | S | Mutes and slow fades on a button or a gate, as a Ballast expander or alone. Bringing layers in and out by hand is the commonest gesture in a long piece. Added 2026-11-26. |

## 2. Texture

The sounds that define the genre, and the hardest DSP. Wake, Eddy and
Spindrift all read and write a buffer; Wake came first, but its read point
did not carry over: Eddy built its own tape, band-limited read and window.
Spindrift, built as Beads with Beads' own buffer and read, took only the
window's approach and the waveform overview from Eddy (2026-11-22).

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 7 | [Rigging](rigging.md) (built) | M | Six tuned strings. Hadal's noise through Rigging is a tuned drone: a second voice from two modules already built; struck by Squall, it also covers some of Ping's and Buoy's ground. |
| 8 | [Wake](wake.md) (built) | L | The delay. Built as a stereo echo for struck sounds, with three styles, Ratio, ping pong and Freeze; the taps and the Send and Return of the first sketch were left out. Its moving read point (`src/DelayTap.hpp`) was expected to carry over to Eddy and Spindrift; it did not carry over to Eddy, whose tape stands still while the head moves (2026-11-22). |
| new | [Narrows](narrows.md) (built) | M | Polyphonic stereo filter: low-pass, band-pass or high-pass on a Mode sweep, with a comb and a ping input. The collection had no filter outside Fathom's voice, and every texture module here wants one after it. Two cores on a switch: Fathom's ladder, now shared code (`src/fathom/LadderCore.hpp`), and a state-variable filter. Its four-wide float type (`src/Float4.hpp`) is there for the next polyphonic module. |
| 9 | [Eddy](eddy.md) (built) | L | Tape loop that also loads and saves files, up to 5 minutes, with a waveform window (drag in it to set Start and Length), wear in the loop and a Restore to the original. Larger than first guessed: its tape and write head are new code (Wake's read point did not carry over), and it paid for the first drawn window. Built 2026-11-22, 16 HP. |
| 10 | [Spindrift](spindrift.md) (built) | L | Grain cloud; the user singled it out. Built 2026-11-22, 20 HP, as Mutable Instruments Beads (its DSP vendored and run unchanged) with a window showing the buffer and every grain. It took Eddy's window approach and `src/Overview.hpp`, not its tape or read: Beads has its own. |
| 11 | [Doldrums](doldrums.md) (built) | L | Spectral freeze and stretch. The highest reward and the highest risk here, so it came last. Built 2026-11-26, 16 HP: a 30 s tape held or drawn out from x1 to a true stop, Blur, Tilt and Shift, a tuned comb from polyphonic V/OCT, one pass on a trigger with a RUN gate for recording into Eddy. It drew its own window (a scrolling ring), not Eddy's `TapeWindow`, and vendored PFFFT. |

## 3. Slow movement and long form

What turns a static patch into a piece that unfolds over twenty minutes.
Mostly light DSP.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 12 | [Neap](neap.md) (built) | S | Eight slow cycles that take a long time to repeat, with a tide (their sum), a High-water gate and an expander, Spring. Built 2026-11-24, 8 HP, after the Instruō øchd. |
| 13 | [Reckoner](reckoner.md) | S | A wandering X and Y. Into the Pan expander's CV, sounds move by themselves. |
| 14 | [Watch](watch.md) | S | One ramp across a whole piece, and a trigger per section. |
| 15 | [Tackle](tackle.md) | S | Scale and shift. By now there is a lot of modulation to tame. |
| 16 | [Helm](helm.md) | M | Scene morph. Performing a long change with one hand. |
| 17 | [Log](log.md) | L | CV recorder. Captures a performed gesture; needs a window. |

## 4. Voices and events

Things that happen against the drone. Lee sits among them because events
need room made for them.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 18 | [Ping](ping.md) | S | Sonar ping and echo. A small build with a large effect in space and horror pieces. |
| 19 | [Buoy](buoy.md) | M | Bells struck by waves: generative, and never the same twice. |
| 20 | [Lee](lee.md) | S | Ducker. Lets a ping or a bell clear space in the drone. |
| 21 | [Squall](squall.md) (built) | M | Random gates and CV with gusts and memory, to trigger the voices above. Built early, after Pan. |
| 22 | [Foghorn](foghorn.md) | M | A second drone voice. Lower priority than it sounds: Fathom and Rigging already cover much of its ground. |
| 23 | [Beacon](beacon.md) (built) | S | Envelope. Built 2026-11-23 with Gate, Trig and Hold, 10 HP, and a window in place of the lamp. |

## 5. Colour

Processes that change the character of what is already there.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 24 | [Doppler](doppler.md) | M | Frequency shifter. Slow inharmonic beating; could move earlier if space ambient leads. |
| 25 | [Rust](rust.md) | M | Wear and decay. Eddy's Wear now does much of it inside a loop; Rust would be the same on any sound as it passes, with wow and flutter. |
| 26 | [Thermocline](thermocline.md) | S | Crossover, to treat lows and highs apart. |
| new | [Beam](beam.md) | S | Stereo width, mono lows, mid and side. Everything recent is stereo and nothing narrows, widens or checks it. May be built into Thermocline instead. Added 2026-11-26. |

## 6. Utilities, as a patch calls for them

All small. Any of them can jump the queue the day it is needed, or fill
a gap between larger builds.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 29 | [Hold](hold.md) | S | Sample and hold, with track. Nothing built samples a signal patched into it: Sway's RAND and Squall's STEP sample their own noise. First of the utilities (2026-11-26). |
| new | [Fluke](fluke.md) | S | Bernoulli gate: each trigger to A or B by chance. Squall clocked is half of one, with no second output, toggle or latch. Splits one stream of events between two voices. Added 2026-11-26. |
| 34 | [Knot](knot.md) | M | Clock and divider. Moved up from "only if wanted" (2026-11-26): four built modules take a clock and none is one. For beatless music the long divisions matter more than the tempo dial. |
| new | [Semaphore](semaphore.md) | S | Logic and a comparator. Makes events conditional ("only while the tide is high"); Neap's HIGH gate is the only comparator so far. Added 2026-11-26. |
| 27 | [Cleat](cleat.md) | S | Multiple. |
| 28 | [Splice](splice.md) | S | Adder. |
| 31 | [Bell](bell.md) | S | Manual trigger. |
| 32 | [Sextant](sextant.md) | S | Scanner between four sources; with the stepped mode added to its page (2026-11-26) it is also the sequential switch. |
| 5 | [Plimsoll](plimsoll.md) | M | Limiter. Skipped for now: Ballast's soft limit and DC blocker keep the output safe; built when long drones into reverb call for real limiting. |

## 7. Only if wanted

Good ideas that this music does not need, or that Shoal already covers.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 33 | [Signal](signal.md) | M | Morse rhythm. A numbers-station moment in a horror piece, but a narrow use. |
| 35 | [Sounding](sounding.md) | M | Step sequencer. Shoal covers it. |
| 36 | [Bearing](bearing.md) | M | Quantiser. Shoal covers it. |
| 37 | [Seastate](seastate.md) | S | Wavefolder. The weakest idea in the set. |
| new | [Fetch](fetch.md) (later) | M | A player for long field recordings, read from disk. Moved down 2026-11-22: Eddy's 5 minutes are enough for now. Built when a piece needs a recording longer than that; it reuses Eddy's window widget, WAV reader, rate conversion and band-limited read. |

## Dropped

Ideas the user has taken off the list. Their pages are kept, marked
dropped, for the reasoning and the sketches.

| # | Module | Dropped | Why |
|---|--------|---------|-----|
| 6 | [Lull](lull.md) | 2026-11-27 | Slew limiter. Beacon's HOLD mode is a slew limiter and Squall's Swell smooths, so the ground is covered twice. |
| 30 | [Reef](reef.md) | 2026-11-27 | Two attenuators. Tackle (four attenuverters with offset) does the same job and more. |

## Things that could change the order

- **Fathom's own plans.** Its design puts effects and a modulation bus
  in later phases. If those are built into Fathom, Fog and Wake lose some
  urgency, or become the place that work goes.
- **Level CV on Ballast** would make Sluice less pressing, though Sluice
  is still the right first build for its simplicity.
- **How Shoal lands.** If its apps prove awkward for slow work, Neap,
  Squall and Sounding rise.
- **A window is a shared cost.** Spindrift, Doldrums, Log and Signal all
  draw one. The first to be built pays for the rest. (2026-11-22: Eddy
  paid for it: `TapeWindow` in `src/widgets.hpp`.)

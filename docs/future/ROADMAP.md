# Suggested order for the future modules

Status: a suggestion, 2026-11-15. The designer's ordering of the
thirty-seven ideas in this directory; the user has not ruled on it.

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
(both 2026-11-15). Sluice, Fog, Hadal, Ballast, Pan and Squall were built in it from the start.

**Changes since (the user, 2026-11-18):** Squall was built next, ahead of
Plimsoll and Lull. Plimsoll is skipped for now, since Ballast's soft limit
and DC blocker already keep the output safe; it moves to the utilities that
wait for a patch to call for them. Lull, a standalone slew, is a low
priority. The numbers below are the original order.

## 1. A whole patch from RKA alone

With Sway, Fathom and Shoal these six make a complete dark ambient patch:
sound, space, loudness under control, stereo, and a safe output.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 1 | [Sluice](sluice.md) (built) | S | Four VCAs. The simplest DSP in the list, so the right first module to build new in the look; and nothing else can make a CV control loudness. |
| 2 | [Fog](fog.md) (built) | L | Reverb. The largest single gain for this music: Fathom into Fog is already a piece. |
| 3 | [Hadal](hadal.md) (built) | S | Rumble and noise. Cheap, immediately in genre, and the noise source Rigging will want. |
| 4 | [Ballast](ballast.md) (built) | M | Stereo mixer with level CV; pan in its Pan expander (built). From here a patch needs no other maker's mixer. |
| 6 | [Lull](lull.md) (low priority) | S | Slew. Slowing everything down is half of this music, and it is 2 HP of work. Squall's Swell covers much of it for now. |

## 2. Texture

The sounds that define the genre, and the hardest DSP. Wake comes before
Eddy and Spindrift because all three read and write a buffer, and Wake's
is the one to build first and reuse.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 7 | [Rigging](rigging.md) | M | Resonators. Hadal's noise through Rigging is a tuned drone: a second voice from two modules already built. |
| 8 | [Wake](wake.md) | L | The full delay. Its Freeze, feedback Send and Return, and four tap outputs make it a hub for a patch. |
| 9 | [Eddy](eddy.md) | M | Tape loop. Reuses Wake's buffer code; gives layered, decaying loops. |
| 10 | [Spindrift](spindrift.md) | L | Grain cloud. The first module with a window to draw; the user singled it out. |
| 11 | [Doldrums](doldrums.md) | L | Spectral freeze and stretch. The highest reward and the highest risk here, so it follows Spindrift, which settles how a window is drawn. |

## 3. Slow movement and long form

What turns a static patch into a piece that unfolds over twenty minutes.
Mostly light DSP.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 12 | [Neap](neap.md) | S | Layered slow cycles that take a long time to repeat. |
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
| 23 | [Beacon](beacon.md) | S | Envelope. Useful once there are voices to shape and Sluice to shape them with. |

## 5. Colour

Processes that change the character of what is already there.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 24 | [Doppler](doppler.md) | M | Frequency shifter. Slow inharmonic beating; could move earlier if space ambient leads. |
| 25 | [Rust](rust.md) | M | Wear and decay. Could also become part of Eddy. |
| 26 | [Thermocline](thermocline.md) | S | Crossover, to treat lows and highs apart. |

## 6. Utilities, as a patch calls for them

All small. Any of them can jump the queue the day it is needed, or fill
a gap between larger builds.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 27 | [Cleat](cleat.md) | S | Multiple. |
| 28 | [Splice](splice.md) | S | Adder. |
| 29 | [Hold](hold.md) | S | Sample and hold. |
| 30 | [Reef](reef.md) | S | Attenuators. Tackle may make it unnecessary. |
| 31 | [Bell](bell.md) | S | Manual trigger. |
| 32 | [Sextant](sextant.md) | S | Scanner between four sources. |
| 5 | [Plimsoll](plimsoll.md) | M | Limiter. Skipped for now: Ballast's soft limit and DC blocker keep the output safe; built when long drones into reverb call for real limiting. |

## 7. Only if wanted

Good ideas that this music does not need, or that Shoal already covers.

| # | Module | Effort | Why here |
|---|--------|--------|----------|
| 33 | [Signal](signal.md) | M | Morse rhythm. A numbers-station moment in a horror piece, but a narrow use. |
| 34 | [Knot](knot.md) | S | Clock and divider. Little call for it in beatless music; Shoal has clock apps. |
| 35 | [Sounding](sounding.md) | M | Step sequencer. Shoal covers it. |
| 36 | [Bearing](bearing.md) | M | Quantiser. Shoal covers it. |
| 37 | [Seastate](seastate.md) | S | Wavefolder. The weakest idea in the set. |

## Things that could change the order

- **Fathom's own plans.** Its design puts effects and a modulation bus
  in later phases. If those are built into Fathom, Fog and Wake lose some
  urgency, or become the place that work goes.
- **Level CV on Ballast** would make Sluice less pressing, though Sluice
  is still the right first build for its simplicity.
- **How Shoal lands.** If its apps prove awkward for slow work, Neap,
  Squall and Sounding rise.
- **A window is a shared cost.** Spindrift, Doldrums, Log and Signal all
  draw one. The first to be built pays for the rest.

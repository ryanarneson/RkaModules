# Squall: a random source

Status: built (2026-11-18, 8 HP). Design: `docs/specs/2026-11-18-squall-design.md`;
manual: `docs/squall/README.md`. As built there is no Noise output (Hadal makes noise), Slew
became Memory, and the subtitle is RANDOM GATES & CV. The text below is the original idea.

![mockup](squall-mockup.svg)

**What it is.** A source of random voltages and gates with one control
for how much is going on.

**The instrument.** An aneroid barometer. Its dial is lettered with a
real barometer's words, running clockwise from Calm through Fair, Change
and Rain to Stormy. Turn it up toward Stormy and more happens: denser
noise, more steps, more gates. This is the reverse of a real barometer,
where clockwise is fair weather, and it is deliberate: the user ruled
that a dial turns clockwise for more. For the same reason the control is
called Weather, not Pressure, since rising pressure would mean calm.

**Sketched controls.** Weather (the hero) with CV and attenuverter traced
straight down from the dial, Gust (how bursty) and Slew (how smooth) on a
graduated arc beneath it. Clock in and Freeze in. Outputs: Noise, Step
(stepped random), Swell (smooth random) and Gate.

**Open questions.** Overlap with Sway's RAND output and Shoal's random
apps; whether Clock replaces or modulates the internal rate; what
distribution each output has; whether the outputs are related to one
another or independent.

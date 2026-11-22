# Rust: decay and wear

Status: idea, 2026-11-15. Not designed. 8 HP in the sketch. One of nine
added to fill gaps for dark, space and horror ambient.

(2026-11-22: Eddy's Wear, built, covers much of this inside a loop: each
pass sinks the level, dulls the highs, presses loud parts into saturation,
adds hiss and drops out short stretches, written back onto the tape so the
loss builds pass over pass (`docs/eddy/README.md`). What it does not cover:
wear on a sound that is not looped, as it passes; wow and flutter (Eddy has
a Speed CV, so Sway into it is wow); an Age macro. Rust, if built, could
reuse Eddy's `src/eddy/Wear.hpp`.)

![mockup](rust-mockup.svg)

**What it is.** Ages a sound: unsteady pitch, noise and dropouts.

**The instrument.** Rust. Age, the hero, is numbered in years.

**Sketched controls.** Age (the hero) with CV and attenuverter traced straight down; Wow and Flutter on the limb; Hiss and Drops (dropouts) on the next arc. Stereo in, stereo out.

**Open questions.** Whether Age is a macro over the other four or its own thing (saturation, loss of highs); how dropouts sound in stereo; overlap with Eddy, which could do this to its own loop.

# Eddy: a tape loop

Status: built (2026-11-22, 16 HP). Design: `docs/specs/2026-11-21-eddy-design.md`;
manual: `docs/eddy/README.md`. As built it is a stereo tape loop that also
loads and saves WAV files, up to 5 minutes, with a waveform window that can
be dragged to set Start and Length. Of the sketch: Speed (-2 to +2, with CV,
attenuverter and now V/OCT), Smear, Mix, Rec, Dub and the Rec gate stayed;
Decay became Wear (level, highs, saturation, hiss and dropouts, written back
each pass, so Rust's wear is in the loop); Layer became Dub Level (one knob
for the new layer and what it erases). New: Start and Length (the loop is
set by recording or loading, then windowed), Play, Loop and a PLAY trigger
(it plays as a sample too), EOC, a Dub gate, the original kept beside the
tape with Restore, and the tape kept with the patch. Left out: several
loops of different lengths (two Eddys do it), a clock. The spiral was not
drawn; the window took the top of the panel. Trimming the tape to the
window is future work (issue #66). The text below is the original idea.

![mockup](eddy-mockup.svg)

**History.** Eddy began as a small stereo delay, first sketched under the
name Wake. When the comprehensive delay took that name (`wake.md`) the
small one became Eddy, and was then redundant beside it. It was reworked
into an audio looper, which the collection lacked and which suits the
name better: an eddy is water that keeps coming round.

**What it is.** A sound-on-sound looper for layered, slowly decaying
loops, with playback speed that runs backwards through zero to forwards.

**The instrument.** An eddy, drawn as a spiral: each turn of the loop
comes round a little further out. The spiral is graduated on its outer
side like any other limb, and the knobs sit on it.

**Sketched controls.** Speed (the hero, numbered -2 to +2) with CV and
attenuverter; Decay (how much each pass fades), Layer (overdub level),
Smear and Mix along the spiral; lit Record and Dub buttons; a Rec gate
input. Stereo in, stereo out.

**Open questions.** Loop length and how it is set (by recording, by a
clock, by a knob); what Smear does; whether Rust's wear belongs in the
loop; several loops of different lengths, for phasing; saving a loop with
the patch.

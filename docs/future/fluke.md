# Fluke: a Bernoulli gate

Status: idea, 2026-11-26. Not designed; no sketch yet. The name is a
working title and has not been checked against the VCV Library. Added
when the user asked what the roadmap was missing ("bernouli gate?").

**What it is.** A coin tossed on every trigger: the trigger leaves by
output A or by output B, with the odds on a knob. One stream of events
becomes two that never coincide, so one Squall or Knot can strike two
voices in turn without a pattern.

**The name.** A fluke is a stroke of luck, and an anchor has two flukes:
which one bites is chance. Two flukes, two outputs.

**Why Squall does not cover it.** Squall with a clock patched in passes
each tick with a set chance, which is one half of a Bernoulli gate. It
has one GATE output, so it cannot send the ticks it refuses anywhere,
and it has no toggle or latch.

**Sketched controls.** Not drawn. Likely 4 to 6 HP, two channels, each
with: Odds (the chance of A, 0 to 100 %, noon is even) with a CV input;
a trigger input; outputs A and B on ivory tiles, each with a light; a
Toggle switch (each toss flips or keeps the last side, instead of
choosing a side) and a Latch switch (an output stays high until the
other side wins, instead of following the trigger's length). Channel
2's input takes channel 1's when unpatched.

**Open questions.** One channel in 2 HP or two in 4 to 6 HP; whether the
outputs pass the input gate's own length or a fixed trigger; whether
Odds CV is read at the trigger's edge only (it should be, or the odds
on show are not the odds in use); polyphony; a seed or a Memory like
Squall's, so a run of tosses can repeat; what an unpatched B means for A
(nothing: A alone is a probability gate).

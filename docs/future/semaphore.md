# Semaphore: logic and a comparator

Status: idea, 2026-11-26. Not designed; no sketch yet. The name is a
working title and has not been checked against the VCV Library. Added
when the user asked what the roadmap was missing.

**What it is.** Two inputs combined into gates: both high, either high,
only one high; and a comparator in front of each input, so a slow CV
counts as high above a level. It makes events conditional: "strike only
while the tide is high and Squall fires".

**The instrument.** Flag semaphore: two arms, and the message is in how
the two combine.

**Why nothing built covers it.** Neap's HIGH gate is the collection's
only comparator and it reads Neap's own tide. Nothing combines two
gates. Shoal has no logic app among its thirteen.

**Sketched controls.** Not drawn. Likely 4 to 6 HP. Inputs A and B, each
with a Mark knob (the level above which it counts as high; a gate
crosses any setting, a slow CV crosses where Mark says) and a light.
Outputs on ivory tiles: AND, OR, XOR and NOT A, each with a light.
Possibly MIN and MAX of the two voltages as well, the analogue forms of
AND and OR, which shape CV as well as gates.

**Open questions.** Whether Mark needs hysteresis so a slow CV with
ripple does not chatter at the crossing (it does; how much); whether B
unpatched takes a fixed high, so AND passes A and the module is a plain
comparator; MIN and MAX in the same panel or left out; a third input;
polyphony; whether the outputs are gates for as long as the condition
holds or triggers when it becomes true.

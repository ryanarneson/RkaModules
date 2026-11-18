# Ballast: design

Status: design, 2026-11-17. Roadmap #4 (`docs/future/ROADMAP.md`); the idea
page is `docs/future/ballast.md`. Panel: `2026-11-17-ballast-mockup.png`
(drawn by a draft entry in `tools/gen_panels.py`).

## Intent

A four-channel stereo mixer, so a patch made from RKA modules needs no
other maker's mixer. It is the last stage before the audio interface (or
Fog, or a limiter): four stereo sources in, each with a level and a level
CV, summed to one stereo pair with a master.

The user's inspiration is Venom's mixer family (VCA Mix 4 Stereo for the
level CV; Mix 4 Stereo for the layout), and especially Venom's optional
expanders that sit to the right of the mixer and add pan, mute, solo, fade,
offset and aux sends one at a time. Ballast ships alone in this round, with
the connection for expanders designed in. **A Pan expander follows
immediately after Ballast** (the user's plan); the others come later.

## Goals, non-goals, anti-goals

Goals:
- Four stereo channels, each with a Level knob and a level CV.
- A master level with its own CV.
- Chaining two Ballasts (or anything else) into one mix.
- A safe, clean output: DC blocked, and never above 10 V.
- An expander connection that the Pan expander can be built on next, with
  no change to Ballast's sound or panel.

Non-goals (this round):
- **Pan of any kind** (knob, CV or balance). The user's ruling: all pan
  functionality comes in the Pan expander. A stereo source keeps its own
  image; a mono source sits in the centre.
- Mute, solo, fades, sends, offsets: later expanders.
- Polyphonic mixing. Polyphonic cables are summed (below).
- Attenuverters on the CVs.

Anti-goals (a failure even if it works):
- A CV that can push a channel above where its knob is set (the user chose
  the VCA law for exactly this).
- Clicks or zipper noise from turning a knob.
- Any cost, in sound or CPU, from the expander connection when no expander
  is attached.
- Panel jacks that don't line up with the other modules' house rows.

## Approaches

**Expanders: now or later.** (1) Ship Ballast with its first expanders; (2)
ship Ballast alone, with the connection designed in, expanders as their own
small modules after. The user picked (2): the core mixer is in patches
sooner, and the connection is still fixed now because it decides where pan,
mute and sends act inside Ballast.

**Pan.** (A) a pan knob per channel, no pan CV; (B) no pan at all, every
pan feature in the expander; (C) a wider panel with pan, pan CV and
attenuverter. The user tried A in a mockup, found the CV jack's trace
through the pan knob made it unclear whether CV was for level or pan, and
picked B: one module for everything pan, right after Ballast. C would have
duplicated the expander.

**Master.** A large main dial, a small master knob, or no master. The user
questioned the dial; a master stays for gain staging (four channels at
unity can sum to about 20 V on peaks), fading the whole mix by one CV, and
setting the level into the next stage. It is a small knob in the top right
corner, not a main dial.

**Level CV law.** (A) the CV scales the knob, as on a VCA mixer: the knob
is the channel's ceiling; (B) the CV adds to the knob, as on Sluice. The
user picked A: with no attenuverters, the knob is the only way to cap a
CV's effect.

## Panel (12 HP)

As the mockup. Six columns of jacks at 9.5 mm pitch: channels 1 to 4 in
the left four, the master column pair on the right.

- **Channel columns** (one straight trace each, top to bottom): **LEVEL**
  on an ivory dB face (as Sluice's Level), numbered 1 to 4, then the **CV**
  jack (y 83, the CV row of Hadal), **IN L** (y 102) and **IN R** (y 116),
  the house rows. The Level faces are staggered (1 and 3 at y 52, 2 and 4 at
  y 66) so they clear each other.
- **MASTER** on the same face, top right; its **CV** jack in the CV row,
  traced up to it with one elbow.
- **CLIP** light in the CV row, beside the master CV.
- **CHAIN L / CHAIN R** at y 102 above the outputs.
- **L and R outputs** on ivory tiles at y 116, the lower right corner.
- Row notes at the left edge: CV, IN L, IN R. Screws top right and bottom
  left, as every panel.
- The Level faces: a tick every 22.5 degrees, long every 67.5; unity
  (0 dB) is the long tick at three quarters of the turn.

## Controls and behaviour

### Level (per channel) and Master

- **Range:** OFF (silence) to +6 dB. Unity, 0 dB, at three quarters of the
  turn (the long tick). Default: unity.
- **Curve** (*designer's call*): gain = 2 x p^2.41 for knob position p in
  0..1 (an audio taper through unity at 0.75 and x2 at 1: about -8.5 dB at
  half, -23 dB at a quarter). The tooltip shows dB.
  *As built:* the exponent is ln 2 / ln (4/3), about 2.4094, so 0.75 is
  exactly unity (2.41 puts it at -0.0015 dB).
- **Master** has the same range, curve and default, and acts on the whole
  mix, CHAIN included.

### Level CV (per channel) and Master CV: the VCA law

- gain = knob gain x clamp(CV / 10 V, 0, 1). 0 V and below is silence,
  10 V and above is the knob's level, linear in between (*designer's call*:
  linear, as a VCA; 5 V is -6 dB under the knob).
- **Unpatched, a CV reads 10 V**, so the knob alone sets the level.
- A polyphonic CV uses its first channel.
- **CV is not smoothed**, so envelopes stay sharp and audio-rate CV works
  (amplitude modulation). Only the knobs are smoothed (below).

### Inputs

- **IN R is normalled to IN L**: a mono source in IN L fills both sides,
  centred. With only IN R patched, the channel is R only (L silent).
- **A channel is "mono"** (for expanders, *designer's call*) when IN L is
  patched and IN R is not. Both patched, or IN R alone, is stereo.
- **Polyphonic cables are summed** to one signal per side, as on Fog.
- **Unpatched channels** cost nothing.

### CHAIN

- CHAIN L and R are added to the mix at unity, after the channels and
  before Master. CHAIN R is normalled to CHAIN L. Polyphonic cables are
  summed. Two Ballasts make an eight-channel mixer: the first's L/R into the
  second's CHAIN.
- CHAIN is not affected by any channel expander (it is not a channel).

### Outputs

- **L and R**, one channel each (not polyphonic).
- **L alone is a mono out**: with R unpatched, L carries (L + R) / sqrt 2,
  crossfaded over about 10 ms when R is patched or unpatched (as Hadal).
  R alone carries just the right side (*designer's call*: the mono sum is
  L's normal only).
- **The output chain**, per side: mix, Master, DC blocker, soft limit.
  - **DC blocker** (the one earmarked for Ballast in the Sluice spec): a
    gentle high-pass, about 5 Hz or below (*designer's call*; it must not
    audibly thin a 30 Hz drone). It has a right-click toggle, on by
    default, so Ballast can still mix CV with it off. It is saved with
    the patch under the key `dcBlock` (default true; a patch without the
    key loads with it on).
  - **Soft limit**: the house curve (Hadal's): untouched below 8 V, never
    above 10 V.
- **Headroom is the user's to manage**: four channels at unity can sum to
  about 20 V on peaks, more with CHAIN and Master at +6 dB. The soft limit
  is the last stage and a catch-all: hit hard, it saturates (never above
  10 V), and the CLIP light says so (*designer's call*: Master is not
  capped).
- **The limiter is Ballast's own copy** of Hadal's curve (*designer's
  call*): Hadal, a shipped module, is not refactored for it.

### CLIP light

Lit while either output is in the soft limit's range (above 8 V), held for
about 150 ms after so a single peak is visible (*designer's call*). Not a
level meter.

### Smoothing

The Level and Master knobs are smoothed (a one-pole of about 10 ms, as
Sluice), starting at the saved value so a patch loads without a fade-in.
CV is not smoothed.

### When nothing is patched

No outputs patched: no work. An output patched but no inputs and no CHAIN:
silence, at near-zero cost.

### Bypass

Bypass silences both outputs (no bypass routes).

### Hostile values

NaN and infinite values on any input or CV become 0 where they enter.

## The expander connection

Ballast must be ready for expanders without any being written this round.
The requirements (the mechanism is the builder's):

- **Placement:** expanders attach to Ballast's right side, in a contiguous
  chain, as Venom's. An expander that is not in an unbroken chain from
  Ballast does nothing.
- **No delay:** an expander's effect lands on the same sample, not one
  sample late (Rack's double-buffered expander messages add a sample; that
  is acceptable only if the builder shows it cannot be avoided cleanly, and
  says so in the plan).
- **The mechanism** (*designer's call*): same-sample, by reading the
  expander chain directly. In `process()`, Ballast walks `rightExpander`
  while each module's model is one of the Ballast-family expander models,
  and calls into it directly (as Venom does). Rack takes its engine lock to
  add or remove a module, so reading the neighbour in `process()` is safe;
  no pointer is cached across samples. Expanders may sit in any order; a
  module of another family ends the chain.
- **The contract** (*designer's call*): an expander implements, per sample,
  - `preLevel(ch, l, r)`: may change a channel's input (Offset);
  - `postLevel(ch, l, r, mono)`: may change each channel's stereo pair
    after Level and its CV (Pan, Mute, Solo, Fade, send taps); `mono` as
    defined under Inputs;
  - `intoMix(l, r)`: may add to the mix before Master (a send's return).
  Each has a default that does nothing. Ballast calls them in chain order.
- **Where expanders act**, in order:
  1. *Pre-level* (Offset, later): on each channel's input, before Level.
  2. *Per channel, after Level and its CV*, on the stereo pair: pan, mute,
     solo, fade, and send taps. The Pan expander needs each channel's L and
     R here, and must be able to tell a mono source (IN R unpatched) from a
     stereo one, because panning the two differs.
  3. *Into the mix*: a send's return adds to the mix before Master.
- **Cost:** with no expander attached, Ballast's output and CPU are
  identical to a Ballast without the connection. The bench proves it.
- **Robustness:** attaching, removing or bypassing an expander while
  running must not click badly or crash; an expander from another module
  family on the right is ignored.
- The first consumer is the Pan expander (next design, right after
  Ballast). The connection is tested this round with a test-only stand-in
  (for example, a fixed per-channel gain applied through the hook), not a
  shipped module.

## Code and files

As every RKA module (CLAUDE.md, TESTING.md):

- `src/ballast/`: `BallastEngine.hpp` (Rack-free: the mix, curve, VCA law,
  normals, chain, DC blocker, limit, the expander hook as a plain interface),
  `Ballast.cpp` (maps params, ports, menu and the expander chain onto the
  engine), `test_*.cpp`, `bench.inc`, generated `BallastLayout.hpp`.
- Panel: the `Ballast` entry in `tools/gen_panels.py` (the mockup's draft,
  cleaned up), `res/ballast/`.
- Registration: Makefile `MODULES`, `tests.mk`, `plugin.json` (tags:
  Mixer, Polyphonic is not claimed), `plugin.hpp/.cpp`.
- Manual: `docs/ballast/README.md` and `docs/ballast/Ballast.png`. README
  table, CHANGELOG, roadmap and idea page marked built.
- No allocation and no libm in `process()`. The libm check must be run on
  Linux (the toolchain image) before an engine task closes: it is skipped
  on the Mac (issue #36).

## Testing

Per TESTING.md (every test shown to fail by mutation, sanitizers on the
hostile test, bench budgets):

- **Curve:** OFF is silence, 0.75 is unity within 0.01 dB, full is +6 dB;
  monotonic.
- **VCA law:** unpatched CV = knob; 0 V silence; 5 V -6 dB; 10 V and 12 V
  the knob's level; negative CV silence; never above the knob.
- **Normals:** IN R normalled to IN L; CHAIN R to CHAIN L; poly summed.
- **Mix:** each channel and CHAIN reach L and R at the expected gain; master
  scales all of it.
- **Mono out:** L alone is (L + R) / sqrt 2; the R patch switch does not
  click.
- **DC blocker:** removes a DC offset; a 30 Hz sine loses well under 0.5 dB;
  off in the menu passes DC.
- **Limit and CLIP:** under 8 V untouched; never above 10 V; the light
  follows.
- **Smoothing:** a knob step makes no step at the output; CV is not slewed
  (an instant CV step reaches the output on the next sample).
- **Expander hook:** with the test stand-in attached the per-channel stage
  acts on the same sample; detached, output is bit-identical to no hook.
- **Hostile:** NaN/inf inputs, rate changes, no allocation, seeded fuzz.
- **Bench:** idle, all patched, busy; the CPU is small (a mixer).

## For later modules

- **Pan expander** (next): pan knob, CV and attenuverter per channel, on the
  hook's per-channel stage; a pan law for mono sources and balance (or true
  stereo pan) for stereo ones. Venom's pan-law menu is the reference.
- Mute, Solo, Fade, Offset, Aux send: later, on the same hook.

## Left to the builder

- The expander mechanism (shared struct, pointers, message format), within
  the requirements above.
- The DC blocker's exact corner and order, within "about 5 Hz or below, no
  audible loss at 30 Hz".
- The smoothing coefficient, the CLIP hold time, and the bench budgets
  (measured).
- The panel generator details beyond the mockup's positions.

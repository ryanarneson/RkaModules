# Pan (Ballast expander), and Ballast fixes: design

Status: design, 2026-11-17. Panel: `2026-11-17-pan-mockup.png` (drawn by a
draft entry in `tools/gen_panels.py`). Builds on Ballast
(`docs/specs/2026-11-17-ballast-design.md`) and closes issues #38, #39 and
#40.

## Intent

Ballast shipped with no pan by the user's choice: all pan functionality
comes in one expander. **Pan** is that expander: per channel, a pan knob, a
pan CV and its attenuverter, placed against Ballast's right side. It is the
first module on Ballast's expander connection, so it also proves that
connection.

In the same effort, Ballast's open review issues are closed: #40 (the
expander's mono flag, plus small robustness and test gaps), #38 (turning
the DC blocker on steps the output) and #39 (a sample-rate change clears
the DC blocker and snaps the knob smoothing).

## Goals, non-goals, anti-goals

Goals:
- Pan each of Ballast's four channels, by knob and by CV, mono and stereo
  sources alike.
- Two stereo behaviours, chosen in Pan's menu: true stereo pan (default)
  and balance.
- Add a Pan from Ballast's right-click menu, as well as from the browser.
- Close #38, #39 and #40.

Non-goals:
- Other expanders (mute, solo, fade, offset, sends).
- A choice of mono pan laws (Venom offers nine). One law, equal power.
- Panning CHAIN (it is not a channel).

Anti-goals:
- Attaching Pan with every knob at centre changes Ballast's sound.
- Two Pans stacking their pans by accident.
- Zipper noise from turning a pan knob.

## Approaches

**Stereo sources.** (A) balance, (B) true stereo pan, (C) both in a menu.
The user picked C; the default is B, because ambient work moves whole
stereo beds, and balance would drop half of one.

**Panel.** Rows on Ballast's row heights were tried; with no outputs to line
up, the user chose the four rows centred on the panel.

**Build.** Pan as its own module on Ballast's expander connection, its maths
Rack-free and tested offline, its menu on Pan; Ballast's menu gains "Add Pan
expander". Building pan into Ballast was ruled out by the user's decision to
keep Ballast pan-free; Venom's cached expander pointers were ruled out in
Ballast's design. One sensible way.

**Name.** The user's choice: **Pan** (slug `Pan`, folder `pan`).

## Phase 1: Ballast changes

### The expander contract: a source type (#40)

`postLevel(ch, l, r, bool mono)` becomes `postLevel(ch, l, r, BallastSource
src)`, with `enum BallastSource { STEREO, MONO }`:
- `MONO`: exactly one of IN L and IN R is patched. With the normals below,
  l and r both hold that jack's signal.
- `STEREO`: both patched, or neither (an unpatched channel still runs its
  hooks when expanders are attached).

### Ballast normals both ways (the user's decision)

A lone jack in **either** IN L or IN R is a mono source in the centre of
both sides: IN R is normalled to IN L, and now IN L is normalled to IN R
as well. This changes a released behaviour (2.0.7 played IN R alone on the
right only); the CHANGELOG and the manual say so. It makes Pan at centre
the identity in every case.

No expander exists yet, so changing the interface costs nothing.

### One expander of each kind

`BallastExpander` gains `virtual int kind() const` (default 0: any number
may chain). The walk keeps only the **first** expander of each nonzero kind;
later ones are left out (not called) but do not end the chain. Pan's kind
is 1. So two Pans never stack. A bypassed expander does not claim its kind
(*designer's call*): with Pan A bypassed and Pan B behind it, B acts.

### Telling an expander it is linked

Every sample the walk runs, Ballast calls `linked()` on each expander it
keeps (a new hook, default nothing), whether or not an output is patched.
Pan lights LINK from it (below). The walk is still skipped entirely when
the right-hand neighbour is not an expander.

### "Add Pan expander" in Ballast's menu

Places a new Pan directly against Ballast's right side, pushing whatever is
there to the right, as Venom's `addExpander` does
(`~/ws/plugins/VenomModules/src/Venom.hpp:575` is the reference). Disabled
(greyed) when an unbypassed Pan is already in Ballast's chain
(*designer's call*).

### #38: turning the DC blocker on

Today, with the blocker off, its state tracks the input with y = 0, so
turning it on drops the output by the whole DC offset on one sample.
Instead, turning it on starts from y = the current input, so an offset
fades over the 3 Hz time constant (about 50 ms) with no step.

### #39: a sample-rate change

A sample-rate change recomputes the coefficients only. The DC blocker's
state and the knob smoothing carry on (no snap), so an offset does not
re-enter and a knob glide is not cut short. Construction and reset still
start clean.

### The rest of #40

- **`dcBlock` in saved JSON:** anything but `false` loads as on (a missing
  or non-boolean value is on).
- **CLIP hold:** at least one sample at any sample rate (it was 0 below
  about 3 Hz).
- **Manual:** "with neither output patched, Ballast does no work" becomes
  "almost no work (the knobs' smoothing still runs)".
- **Test gaps:** the mono-out switch is tested in both directions (R
  patched and unpatched); the expander tests also run with the DC blocker
  on.

## Phase 2: Pan

### Panel (6 HP)

As the mockup. Four rows, centred on the panel (y 46, 64, 82, 100): the
**PAN** knob (numbered 1 to 4, limb size), its **attenuverter**, the **CV**
jack, traced right to left. "CV" over the jack column; the **LINK** light
low and centred (y 112); the short foot mark "R K A" (the full one does not
fit at 6 HP). Screws top right and bottom left.

### Controls

- **Pan knob:** hard left to hard right, centre at noon, the default.
- **CV law** (the house law): position = clamp(knob + CV / 10 V x
  attenuverter, 0, 1), with 0.5 the centre. At full attenuverter, +-5 V
  from centre sweeps the whole width. The attenuverter defaults to 0. A
  polyphonic CV uses its first channel.
- **Smoothing:** the knob position is smoothed (about 10 ms, as Ballast's
  levels, snapping on load). CV is not smoothed, so audio-rate auto-pan
  works.
- **Menu:** *Stereo sources: True pan (default) / Balance*. Saved with the
  patch as `stereoMode`.

### The pan laws

With p from -1 (hard left) to +1 (hard right), 0 at centre. **At p = 0
every law is the identity**, so attaching Pan at centre changes nothing.

- **Mono sources** (`MONO`): equal power, centre at unity on each side:
  gL = sqrt 2 cos(theta), gR = sqrt 2 sin(theta), theta = (p + 1) pi / 4.
  Hard left: L at +3 dB, R silent. The mono signal is l (equal to r).
- **Stereo, true pan:** for p >= 0, theta = p pi / 2: L' = L cos(theta),
  R' = R + L sin(theta); mirrored for p < 0. Hard right folds all of L into
  R.
- **Stereo, balance:** for p >= 0: L' = L cos(p pi / 2), R' = R; mirrored for
  p < 0. Hard right silences L.

Computed with FastMath (no libm per sample).

### Attaching and removing

Pan's knob smoothing starts **from centre** when Pan is first called after
not being linked, then glides to the knob over about 10 ms, so attaching a
panned Pan does not click (*designer's call*). Removing or bypassing a
panned Pan is a step back to centre; the manual says so (*designer's
call*: a fade there would need a crossfade in Ballast, not worth it for an
edit made rarely and usually in silence).

### LINK

Lit while a Ballast has called `linked()` within the last 50 ms (a counter
Ballast's thread writes and Pan's `process()` reads; the builder may use an
atomic). Dark when Pan is not touching a Ballast, is behind another Pan in
the chain, or is bypassed.

### Threads and state

As Ballast's contract: Pan reads its params and inputs, and smooths, inside
the hook calls; its `process()` only drives LINK.

### Bypass

A bypassed Pan is skipped: Ballast sounds as without it.

## Code and files

- **Phase 1:** `src/ballast/` (BallastExpander.hpp, BallastChain.hpp,
  BallastEngine.hpp, Ballast.cpp, the tests), `docs/ballast/README.md`.
- **Phase 2:** `src/pan/`: `PanEngine.hpp` (Rack-free: the laws, the CV
  law, smoothing, as a `BallastExpander`-shaped core), `Pan.cpp`,
  `test_*.cpp`, `bench.inc`, generated `PanLayout.hpp`; the `Pan` entry in
  `tools/gen_panels.py` (the mockup's draft, cleaned up), `res/pan/`;
  registration (Makefile `MODULES`, `tests.mk`, `plugin.json` with tags
  `Mixer` and `Expander`, `plugin.hpp/.cpp`); manual `docs/pan/README.md`
  and `docs/pan/Pan.png`; README table, CHANGELOG.
- The libm check runs on Linux in the toolchain image before an engine
  task closes (issue #36).

## Testing

- **Phase 1:** the normals both ways (IN R alone is centred, equal to IN L
  alone); the source type for every patch combination; one Pan of
  each kind (a second of the same kind is not called, and does not end the
  chain); `linked()` called every walk, outputs patched or not; #38: turning
  the blocker on with 2 V DC present makes no output step above the
  steady-state step; #39: a rate change mid-run with DC present and a knob
  gliding makes no step; `dcBlock` non-boolean loads on; CLIP at 1 Hz holds
  at least one sample; the mono switch both ways; expander tests with the
  blocker on.
- **Phase 2:** each law at centre is bit-identical to no Pan; mono equal
  power (constant L^2 + R^2 across p); hard-left/right values per law;
  true pan keeps L + R content (no loss at hard right); balance silences
  the far side; the CV law (+-5 V at full attenuverter sweeps the width;
  attenuverter 0 ignores CV); no zipper from a knob step; CV unsmoothed;
  hostile input (NaN CV, rates), no allocation, bench.

## Left to the builder

- The `addExpander` mechanics (from Venom's reference).
- The LINK counter's mechanism and the 50 ms figure's exact form.
- The smoothing coefficient and the bench budgets (measured).
- Panel details beyond the mockup's positions.

# RKA - project notes for Claude

VCV Rack 2 plugin, slug `rka` (never change it, or any module slug, after a
release: saved patches reference them). See `README.md` for commands and
layout.

- One plugin; all code under `src/`, one folder per module (`src/sway/` =
  the Sway LFO), with panels in `res/<folder>/` and manuals in
  `docs/<folder>/`. Module folders are listed in `MODULES` in the Makefile.
  Panels load from `"res/<folder>/<Name>.svg"`.
- Module names are the sea family: plain English words for calm water,
  each also a verb (Sway = a boat at anchor, Fathom = depth). Decided
  2026-10-24. Reserved for the ideas in `~/ws/plugins/IDEAS.md`: Swell
  (slope/function generator), Undertow or Drift (chaos / smooth random),
  Tide (clocked modulator), Loom (a darker voice). Eddy, once reserved
  there for a random sampler, is the tape loop (built 2026-11-22). Pick
  from this family before inventing a new name; never rename a released
  module.
- One shared look, "Instrument" (brass and ivory on near-black; rules in
  `docs/DESIGN-THEME.md`, detail in
  `docs/specs/2026-11-15-instrument-look-design.md`): palette and panel
  language in `tools/gen_panels.py`, the plugin's own components and
  widgets in `src/widgets.hpp`. One typeface, D-DIN Condensed, from
  `tools/fonts/`. No stock knobs, jacks, lights or screws, and no ad-hoc
  colours in a module. `src/RkaTheme.hpp` and `res/components/` (the
  components' SVGs) are generated.
- `RACK_DIR` is set in `~/.bashrc` (`~/ws/sdk/Rack-SDK`, 2.6.6). Non-login
  shells may not have it; pass `RACK_DIR=$HOME/ws/sdk/Rack-SDK` to make.
  On the Mac (arm64) it's in `~/.zshrc`:
  `~/ws/sdk/Rack-SDK-2.6.6-mac-arm64`; `~/ws/sdk/Rack-SDK` there is a
  Linux SDK copy and won't link. Docker for `tools/cross_build.sh` is
  Colima (`colima start --vm-type vz --vz-rosetta`).
- Release packages from `tools/mac_dist.sh` and `tools/cross_build.sh` land
  in `packages/`, not `build/`: `make clean` wipes `build/`.
- Every build handed to the user for testing (`make install`, mac_dist,
  cross_build) gets a new version first: `tools/bump_version.sh "<what
  changed>"` bumps REVISION in `plugin.json`, logs it under "Test builds" in
  `CHANGELOG.md` and commits, so one version = one commit. Mac and Windows
  packages from the same commit share it. Decided 2026-11-16.
- The repo holds unit tests only. Test results beyond unit testing
  (testbed runs, recordings, measurement reports, QA docs) live on the share,
  `/Volumes/share1/VCVRACK/`, never in the repo; audio is git-ignored
  (`*.wav`, `*.flac`, ...) as a backstop. The Fog-vs-LV-426 testbed is
  `/Volumes/share1/VCVRACK/fog-testbed/` (`run_all.py <version>` writes
  `runs-<version>/`); only its Python venv is local
  (`~/ws/plugins/fog-testbed/venv`).
- The repo is meant to work unchanged on Linux and macOS. Keep tools/
  scripts portable: no GNU-only flags (`sed -i`, `timeout` without the
  gtimeout fallback, `readlink -f`), and check `uname -s` before using a
  platform path.
- Before calling a change done: `make test`, `make`, `make smoke`, and look
  at `build/screenshots/*.png` for panel changes. `TESTING.md` has the
  testing rules every module follows (a test must be shown to fail,
  independent-path comparisons, sanitizers, golden saved state, no
  allocation on the audio path, Linux and Windows builds before merge).
- Panels come from `tools/gen_panels.py`; never hand-edit `res/`, `res-src/`
  or `src/<folder>/*Layout.hpp`. `make panels` regenerates
  all three.
- DSP lives in Rack-free headers in `src/<folder>/` with tests in
  `src/<folder>/test_*.cpp` (using `src/testing.hpp`). Modules only map params/ports/polyphony onto the
  core.
- Aliasing: `docs/DSP-ALIASING.md` (the three sources and their remedies,
  what to reuse, how to measure) and `TESTING.md` rule 16: a module that
  makes or bends sound measures its aliasing against a bound in dB.
- A new module with an engine follows `TESTING.md`, "Adding a module":
  `src/<folder>/bench.inc` gets it `make bench`, `libm-calls` and
  `engine-globals` with no other change.
- Shoal's core is the vendored O&C firmware in `src/shoal/firmware/`;
  change it only with a `// SHOAL:` mark and a `CHANGES.md` line;
  `make shoal-globals` must stay clean.
- Spindrift's core is vendored Mutable Instruments Beads in
  `src/spindrift/beads/` and `src/spindrift/stmlib/`; change it only with a
  `// SPINDRIFT:` mark and a line in `src/spindrift/CHANGES.md`. `make test`
  checks both (`spindrift-vendor`, `spindrift-globals`).
- The Shoal module (`src/shoal/Shoal.cpp`) only maps jacks, buttons,
  lights and the patch data onto `shoal::Host`; only the engine thread
  touches the running core (restore and reset build a new one off it).
- Shoal's friendly mode (`src/shoal/Friendly*.inc`): one adapter per app
  in `FriendlyAdapters<n>.inc`; every edit replays the app's own encoder
  and button handlers, never `setSettingRaw` or `apply_value`.
- Voltage standards: audio ±5 V, bipolar CV ±5 V, unipolar CV 0–10 V,
  gates/triggers 0/10 V, V/OCT 1 V per octave; triggers detect with
  `SchmittTrigger` at 0.1 V / 1 V.
- Starting Rack for manual testing must not clobber the user's real
  autosave: use `make smoke` (temp user folder) for automated checks.
- Reference plugins cloned beside this repo in `~/ws/plugins`:
  AudibleInstruments (official, Mutable ports), SanguineMonsters (large
  plugin, CI pattern copied from it), forsitan-modulare (header-only DSP +
  offline test harness, the pattern used here), eurorack
  (pichenettes/eurorack with its submodules: Mutable Instruments
  firmware DSP, with Beads; shared DSP in `stmlib/dsp`).

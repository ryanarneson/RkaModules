# RKA - project notes for Claude

VCV Rack 2 plugin, slug `rka` (never change it, or any module slug, after a
release: saved patches reference them). See `README.md` for commands and
layout.

- One plugin; all code under `src/`, one folder per module (`src/sway/` =
  the Sway LFO), with panels in `res/<folder>/` and manuals in
  `docs/<folder>/`. Module folders are listed in `MODULES` in the Makefile.
  Panels load from `"res/<folder>/<Name>.svg"`.
- One shared look ("Nightglow", see README): palette in
  `tools/gen_panels.py`, shared knobs/lights in `src/widgets.hpp`. Don't use
  stock knobs or ad-hoc colours in a module. `src/RkaTheme.hpp` is
  generated.
- `RACK_DIR` is set in `~/.bashrc` (`~/ws/sdk/Rack-SDK`, 2.6.6). Non-login
  shells may not have it; pass `RACK_DIR=$HOME/ws/sdk/Rack-SDK` to make.
  On the Mac (arm64) it's in `~/.zshrc`:
  `~/ws/sdk/Rack-SDK-2.6.6-mac-arm64`; `~/ws/sdk/Rack-SDK` there is a
  Linux SDK copy and won't link. Docker for `tools/cross_build.sh` is
  Colima (`colima start --vm-type vz --vz-rosetta`).
- Release packages from `tools/mac_dist.sh` and `tools/cross_build.sh` land
  in `packages/`, not `build/`: `make clean` wipes `build/`.
- The repo is meant to work unchanged on Linux and macOS. Keep tools/
  scripts portable: no GNU-only flags (`sed -i`, `timeout` without the
  gtimeout fallback, `readlink -f`), and check `uname -s` before using a
  platform path.
- Before calling a change done: `make test`, `make`, `make smoke`, and look
  at `build/screenshots/*.png` for panel changes.
- Panels come from `tools/gen_panels.py`; never hand-edit `res/`, `res-src/`
  or `src/<folder>/*Layout.hpp`. `make panels` regenerates
  all three.
- DSP lives in Rack-free headers in `src/<folder>/` with tests in
  `src/<folder>/test_*.cpp` (using `src/testing.hpp`). Modules only map params/ports/polyphony onto the
  core.
- Voltage standards: audio ±5 V, bipolar CV ±5 V, unipolar CV 0–10 V,
  gates/triggers 0/10 V, V/OCT 1 V per octave; triggers detect with
  `SchmittTrigger` at 0.1 V / 1 V.
- Starting Rack for manual testing must not clobber the user's real
  autosave: use `make smoke` (temp user folder) for automated checks.
- Reference plugins cloned beside this repo in `~/ws/plugins`:
  AudibleInstruments (official, Mutable ports), SanguineMonsters (large
  plugin, CI pattern copied from it), forsitan-modulare (header-only DSP +
  offline test harness, the pattern used here), pichenettes-eurorack
  (Mutable Instruments firmware DSP).

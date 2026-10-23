# RKA - project notes for Claude

VCV Rack 2 plugin, slug `rka` (never change it, or any module slug, after a
release: saved patches reference them). See `README.md` for commands and
layout.

- `RACK_DIR` is set in `~/.bashrc` (`~/ws/sdk/Rack-SDK`, 2.6.6). Non-login
  shells may not have it; pass `RACK_DIR=$HOME/ws/sdk/Rack-SDK` to make.
- Before calling a change done: `make test`, `make`, `make smoke`, and look
  at `build/screenshots/*.png` for panel changes.
- Panels come from `tools/gen_panels.py`; never hand-edit `res/`, `res-src/`
  or `src/*Layout.hpp`. `make panels` regenerates all three.
- DSP lives in Rack-free headers (`src/*Core.hpp`) with tests in `test/`.
  Modules only map params/ports/polyphony onto the core.
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

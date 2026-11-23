# Vendored Mutable Instruments Beads: change log

- Upstream: https://github.com/pichenettes/eurorack , directories `beads/`
  and `stmlib/` (the submodule)
- Commit: f4cfa7697095a28d292bff758beb51deb49b5a86 (eurorack), with stmlib at
  e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
- Copied: the DSP only (`beads/dsp/`, `beads/resources.h/.cc`) and the stmlib
  files it includes, paths as upstream. Not copied: the hardware glue
  (`beads.cc`, `ui.cc`, `drivers/`, `cv_reader`, `gain_control`, `settings`,
  the bootloader, ...). `testdata/ref/` holds the same files untouched, the
  reference the tests compare against.

Every local change to a file in `beads/` or `stmlib/` carries a
`// SPINDRIFT:` comment on each changed line and a line in this file.
`tools/check_spindrift_vendor.sh` (in `make test`) fails on any unmarked
difference from upstream, and on any change to `testdata/ref/`.

## Files rewritten

Files changed throughout, or not upstream; exempt from the line marks.
None yet.

## Modified files

None of these changes alters a sample: `test_vendor`'s `matches_ref_every_mode`
holds the vendored Processor and Synth to the reference bit for bit.

- `stmlib/utils/random.h`, `random.cc`: `Random`'s state is no longer a
  static but the current `spindrift::BeadsContext`'s `rng` (default 0x21, as
  upstream), reached through `rng_state_()` and one thread-local pointer
  (`spindrift::current()`, BeadsFirmware.cpp; set by `BeadsScope`), as
  Shoal's `stmlib_utils_random.h`. Two instances never share a stream
  (TESTING.md rule 8).
- `beads/resources.h`, `resources.cc`: the four `*_table` pointer arrays are
  `const` (never written), so the object holds no writable globals.
- `beads/dsp/mu_law.h`, `mu_law.cc`: `lut_ulaw_f` is `const`, likewise.
- `beads/dsp/processor.cc`, `granular_processor.h`, `synth.cc`,
  `trigger_generator.cc`: the variable-length arrays in `Processor::Process`,
  `GranularProcessor::Process`, `Synth::Render` and
  `TriggerGenerator::Render` are `spindrift::kMaxBlock`-sized (12 frames,
  Beads' largest block; the decimated arrays fit in the same size), with an
  `assert` on `size`. No VLA is left, so the object builds without
  `-Wno-vla`.
- `beads/dsp/processor.h`, `processor.cc`: `Process`'s `taps_buffer` is the
  member `taps_buffer_` (zeroed in `Init`), so `taps()` can read the last
  block's taps (nothing else keeps them; `MultitapDelay` and `Tap` hold no
  delay or gain).
- Window accessors (const, no allocation; `GrainSnap`, `TapSnap` in
  `BeadsContext.hpp`):
  - `beads/dsp/processor.h`: `grains()`, `taps()`, `writeHead()`, and the
    private `max_delay_times_ratio()` they need.
  - `beads/dsp/granular_processor.h`: `grains(mono, ...)`, `write_head(mono)`.
  - `beads/dsp/synth.h`: `grains()`, `wave()`.
  - `beads/dsp/delay_grain.h`: `DelayGrain::snap()` (`delay_`, `speed_` in
    1/1024 samples; pan from its four gains).
  - `beads/dsp/wavetable_grain.h`: `WavetableGrain::snap()`.
  - `beads/dsp/granular_player.h`: `grain(i)`, read-only access to a grain.
  - `beads/dsp/delay_line.h`: `write_ptr()`, read-only.
  - `beads/dsp/grain_envelope.h`: `value()`, what the next `Render()`
    returns, without advancing.
  - `beads/dsp/voice_stealing_envelope.h`: `value()`.
  - `beads/dsp/wavetable_voice.h`: `wave()`.
- `beads/dsp/wavetable_granular_player.h`: the `printf` upstream makes under
  `TEST` when a synth grain is stolen is compiled out (`#if 0`): nothing
  prints from the audio path. `TEST` stays defined: stmlib needs it for its
  portable code in place of ARM assembly.

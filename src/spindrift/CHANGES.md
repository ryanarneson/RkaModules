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

None yet.

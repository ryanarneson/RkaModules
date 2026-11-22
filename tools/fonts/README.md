# Panel typeface

D-DIN Condensed, Regular and Bold: the one typeface on RKA's Instrument
panels. `tools/gen_panels.py` points Inkscape at this folder only (a private
fontconfig file written to `build/panels/`), so `make panels` gives the same
lettering on any machine and never falls back to a system font.

- Source: https://github.com/amcchord/datto-d-din (Datto's open digitisation
  of DIN 1451; "D-DIN Condensed" is a Reserved Font Name).
- Licence: SIL Open Font License 1.1. See `COPYING.txt` and `OFL-1.1.txt`.

Panels ship with their text converted to paths. One file ships in the
plugin package, with its licence (`DISTRIBUTABLES` in the Makefile):
`D-DINCondensed.ttf`, which Eddy's window (`rka::TapeWindow` in
`src/widgets.hpp`) uses to letter the tape's length at run time.

# Panel typeface

D-DIN Condensed, Regular and Bold: the one typeface on RKA's Instrument
panels. `tools/gen_panels.py` points Inkscape at this folder only (a private
fontconfig file written to `build/panels/`), so `make panels` gives the same
lettering on any machine and never falls back to a system font.

- Source: https://github.com/amcchord/datto-d-din (Datto's open digitisation
  of DIN 1451; "D-DIN Condensed" is a Reserved Font Name).
- Licence: SIL Open Font License 1.1. See `COPYING.txt` and `OFL-1.1.txt`.

The fonts are build inputs only. Panels ship with their text converted to
paths; the font files are not part of the plugin package.

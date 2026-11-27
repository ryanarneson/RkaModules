# pffft

`pffft.c` and `pffft.h` are PFFFT, "a pretty fast FFT", by Julien Pommier
(BSD-like licence, in the header of each file), copied unchanged from
https://bitbucket.org/jpommier/pffft:

- commit 67a4a6a113179ab4ac398298f46fce8dd60498dc
- committed 2026-10-16 (Fri Oct 16 11:11:07 2026 +0200)
- fetched 2026-11-26

Why a copy: the Rack SDK ships only `pffft.h`; the code is inside libRack,
which `make test` and CI (no Rack) cannot link. The plugin's own copy must
not meet libRack's exported `pffft_*`, so `rka_prefix.h` renames every symbol
`pffft.c` exports to `rka_pffft_*` (`-include` for the C file; `src/Fft.hpp`
includes it before `pffft.h`). `src/Fft.hpp` wraps it as `rka::RealFft`.

Updating: replace the two files with upstream's, set the commit and date
here and in the top-level `README.md`, and check that every symbol in
`nm -g build/test/dep/pffft.o` that pffft defines starts with `rka_` (a new
upstream export needs a line in `rka_prefix.h`), and that
`nm -g plugin.dylib | grep -c ' _pffft_'` is 0.

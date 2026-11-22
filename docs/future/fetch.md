# Fetch: a player for long recordings

Status: idea, 2026-11-21. Not designed; no sketch yet. The name is a
working title and has not been checked against the VCV Library.

**Where it came from.** While Eddy was designed, the user said: "I have
a lot of ambient field recordings in the 100-300MB range", and then "we
can work a long-file sampler module into the roadmap. one that can
handle field recordings of large size". Eddy holds its tape in memory
and keeps it with the patch, so it stops at 5 minutes; files of ten to
thirty minutes need a module built round the file on disk.

**What it is.** Plays a long stereo recording from disk as a bed under a
piece: any length, read as it plays, so nothing large is held in memory
or copied into the patch. A place in the file and a stretch of it can be
chosen and moved; it plays once through or comes round.

**The instrument.** Fetch is the distance of open water the wind has
blown across: the longer the fetch, the larger the sea that arrives. And
the module fetches a recording.

**Sketched controls.** Not drawn. Likely: a window showing the whole
file, as Eddy's; Start and Length; Speed in a narrower range than Eddy's;
Play and Loop; a trigger to start from Start; an end-of-file trigger;
level. Stereo out. The file is chosen from the menu or dropped on the
panel.

**What it would take from Eddy.** The window widget, the WAV reader, the
rate conversion, and the play head with its click-free join.
(2026-11-22, Eddy built: these are `TapeWindow` in `src/widgets.hpp`
with `src/TapeGesture.hpp` for its drag selection, `src/WavFile.hpp`,
`src/Resample.hpp`, `src/Overview.hpp` and `src/BandLimitedRead.hpp`,
which reads at any speed without aliasing. The voices and their join are
in `src/eddy/Voices.hpp`, Eddy's own, to be lifted out when Fetch needs
them. Eddy's reader takes a whole file into memory; Fetch's will read as
it plays.)

**Open questions.** How far Speed goes, and backwards, when the file is
read from disk as it plays; whether the patch keeps only the file's path
or may copy the file in; what it does when the file is missing; whether a
stretch can be handed to an Eddy beside it to be dubbed and worn (an
expander link, or a "send to Eddy" in the menu); several files in a
folder, chosen by CV; formats besides WAV (FLAC, since field recorders
and archives use it); the overview of a 300 MB file without reading all
of it first.

# Testing

How modules in this plugin are tested, and the rules a new module must meet.
The rules come from building Shoal, where each one caught a real defect; the
defect is noted beside the rule. Sway and Fathom predate some of them; the
last section lists where they fall short.

## Commands

| Command | What it does | Needs |
|---|---|---|
| `make test` | Builds and runs every `src/<module>/test_*.cpp`, the Shoal and engine globals checks, the libm check and the bench declarations check | A C++17 compiler |
| `make -f tests.mk test TEST_WERROR=-Werror CXX=g++ TSAN_REQUIRED=1` | The same, as CI runs it on Linux (`TSAN_REQUIRED=1`: fail, not skip, if the ThreadSanitizer test cannot start; rule 7) | No Rack SDK |
| `make` | Builds the plugin | Rack SDK (`RACK_DIR`) |
| `make smoke` | Loads the plugin in Rack with a temporary user folder and screenshots every module to `build/screenshots/` | Rack SDK, Rack |
| `make shoal-globals` | Fails if the firmware object holds writable global state | |
| `make shoal-fuzz` | 50 more seeds of the random fault sweep | |
| `make shoal-bench` | CPU share per app, including drawing and saving | |
| `make bench` | CPU share of every module's engine that has a `src/<module>/bench.inc`, in each of its cases at 1, 4 and 16 instances; fails over a case's budget, if cost grows with the count, or if there are no modules | A C++ compiler |
| `make libm-calls` | Fails if any declared engine calls libm per sample in a case its `bench.inc` marks knobs-still (in `make test`) | Linux or macOS |
| `make engine-globals` | Fails if any declared engine holds writable global state (in `make test`) | |
| `make bench-modules` | Fails if a folder has a `*Engine.hpp` but no `bench.inc`, or a `bench.inc` lacks idle/outputs/busy, a budget, or a knobs-still libm case (in `make test`) | |
| `tools/cross_build.sh` | Linux and Windows builds in Docker | Colima running |

Before calling any change done: `make test`, `make`, `make smoke`, and for a
panel or widget change, look at the screenshot.

Never start Rack against your real settings to test; `make smoke` uses a
temporary user folder so it cannot touch your autosave.

## The shape that makes a module testable

1. **The core is Rack-free.** DSP and logic live in headers under
   `src/<module>/` that include nothing from Rack. The Rack module only maps
   params, ports and lights onto the core. Tests include the core directly and
   need no SDK, so CI can run them on a bare Linux runner.
2. **UI logic is Rack-free too.** Geometry, hit testing, value mapping, drag
   arithmetic and queue bookkeeping go in a plain header with offline tests
   (`src/Detents.hpp`, `src/shoal/ListLayout.hpp`). The widget that needs Rack
   stays thin. What cannot be tested offline (Rack's event order, undo
   history, a real mouse) goes on a written list of manual checks.
3. **Thread hand-off has its own layer.** Anything shared between the audio
   thread and the UI thread sits in one Rack-free class (`shoal::Host`) with
   its own tests, not spread through the module.
4. **Tests use `src/testing.hpp`.** `CHECK(cond, fmt, ...)` and
   `rkatest::finish(name)`. One file per subject, named `test_<subject>.cpp`.
   `tests.mk` finds them by name; no registration.

## Rules

### 1. A test must be able to fail

Before a test is accepted, break the behaviour it names in a scratch copy and
confirm the test fails. Record the mutation and the result in the review.

Why: several Shoal tests passed with the feature removed.
- A restore test started from a core already at defaults, so a restore that
  did nothing passed.
- A two-thread encoder test started with the cursor at a clamped end, so a
  lost turn was invisible.
- A step-mask test passed without the firmware's follow-up call until it also
  compared the drawn frame.

Start a test from a state where the wrong result is distinguishable: away
from clamps, away from defaults, with the "before" value checked.

### 2. Compare against an independent path

A test of "A behaves like B" drives B by a different route on a second
instance, then compares everything observable: every setting, the drawn
frame, and the outputs after running. Calling the same function twice proves
nothing.

In Shoal, every list edit is checked against a second core driven only by
encoder and button events, with the same seed, comparing all settings, the
whole model and 2,000 ticks of output.

### 3. Sanitizers on by default

Tests of anything with pointers, buffers or integer tricks build with
`-fsanitize=address,undefined -fno-sanitize-recover=all`. Threaded code also
gets ThreadSanitizer runs (a scratch build, several runs, since the two
sanitizer families cannot be combined).

### 4. Sweep the inputs, then fuzz

For each input and setting: minimum, maximum, default, and values between;
then random combinations under the sanitizers, with a fixed seed in
`make test` and more seeds on request (`make shoal-fuzz`). A failing seed
must be reproducible from the command line. Include hostile values: NaN and
infinite voltages, voltages far outside the range, the low and high ends of
the sample rates Rack offers, a zero or negative rate, and a sample-rate
change mid-run.

### 5. Saved state is a compatibility surface

- Commit one golden image of the saved state per format version
  (`src/shoal/testdata/GoldenStateV1.hpp`) and a test that restores it and
  checks the values. **Never regenerate it to make a test pass.**
- Test every way a patch can be wrong: field missing, wrong type, wrong
  version, wrong length, bad encoding, a checksum failure, each half damaged
  in turn. Each must load as defaults without crashing.
- Put the decode rules in a Rack-free header with tests.
- Pin struct layouts that reach disk with `static_assert`s.
- Do not test a round trip by comparing saved bytes after the code has run,
  unless the format is known to be stable under a no-op save. Compare the
  values.
- Existing param, input, output and light ids never change number.

Why: every Shoal state test saved and restored with the same binary, so a
changed settings table would have turned real patches into defaults with all
tests green.

### 6. Real-time safety is tested, not assumed

Nothing reachable from `process()` may allocate, lock, log or do I/O; it may
`try_lock`. A test counts allocations and frees per thread across the whole
engine path (`testNoEngineAllocation`). Watch for hidden costs: `snprintf`
with `%f` can allocate inside libc, and a patch load or reset that rebuilds
state must build it off the audio thread and swap a pointer.

Why: Shoal's restore allocated and rebooted the firmware inside one audio
sample until the allocation counter existed.

### 7. Threads

- State which thread may call each member, in the header.
- Test with two real threads, running until the UI thread finishes, and
  assert a result that a lost or duplicated event would change.
- Lock-free queues: test capacity exactly, wraparound, and order.
- Order must hold where it matters. Two events in one queue are not ordered
  at the consumer if one is applied at once and the other plays out over
  time; the test checks the final state for each possible order.
- Pointers handed across threads must point to storage that outlives both
  sides.

Eddy hands tape sets between the audio thread and its worker through
atomics. `src/eddy/test_threads.cpp` runs the engine with three real
threads (the audio thread in a trigger storm, a worker offering, collecting
and copying sets, the window reading the live overview) under
ThreadSanitizer, which has its own rule in `tests.mk` because it cannot be
combined with ASan; the build fails, saying so, where the compiler cannot
link it. GCC's TSan runtime cannot start on a Linux kernel whose address
randomisation is wider than it handles (`vm.mmap_rnd_bits` over 28, as in
Colima's VM) or under Rosetta: `make test` then reruns it under
`setarch -R`, and only if it cannot start that way either (Docker's default
seccomp profile refuses `setarch -R`; Rosetta fails either way) skips it
with the reason printed, and `make test` still passes. With
`TSAN_REQUIRED=1` that skip is a failure instead: `make test` fails,
naming the test. CI sets it (and narrows `vm.mmap_rnd_bits` to 28 first),
so a green CI run means the thread test ran. One data race is accepted by design and is not in that test: the
worker reading the live tape to save it while the engine may be writing it
(Wear, Dub). A torn read costs one sample of wear either side of it; the save
names it where it reads.

### 8. Instances are independent

Two instances with different seeds give different results and never affect
each other. No writable global state: `make shoal-globals` inspects the
object file's data sections and fails on any, and has its own tests
(`tools/test_check_shoal_globals.sh`). A module with an engine gets the
same check from `make engine-globals` once it has a `bench.inc` (see
"Adding a module").

### 9. Behaviour on the target is the reference

Where code came from other hardware, the desktop build must give the
original's result for the cases C++ leaves undefined (divide by zero, shifts
of 32 or more, signed overflow, `char` signedness, reads past a table), and a
test pins each. Compile flags that change arithmetic (`-funsigned-char`,
`-fwrapv`, fast-math) must be the same for the plugin and its tests.

### 10. Performance is measured

A bench reports CPU share per mode, including the costs that hide outside the
DSP loop (drawing, periodic saves, UI model building). Shoal's budget is 2 %
of a core per instance at 48 kHz; every app measures under 0.2 %.

`make bench` times every module that declares its engine in
`src/<module>/bench.inc` (what goes in it: `src/bench/BenchModule.hpp`;
tests.mk finds the files, and `src/bench/bench.cpp` names no module). Each
runs with the plugin's compile flags, at 1, 4 and 16 instances run in turn
each sample as Rack runs them, in the cases the declaration lists: idle
(no cables, knobs at defaults), outputs (every output patched, no inputs)
and busy (the module's heaviest realistic use; Sway: 16 channels, a
moving CV on every input; Fathom: full detune, drift and spread, a moving
CV on every input), and any more it adds. It fails if an instance is over
the case's budget at 1 or at 16 instances, or if an instance at 16 costs
more than 1.5 times what one alone costs plus 0.01 % of a core (the slack
is for timer noise on the near-free cases). Each case declares two
budgets: one for Apple arm64 Macs, where they are measured, and one for
every other machine. Sway's and Fathom's other-machine budgets are three
times the Mac's, and provisional: the only x86-64 runs so far were
emulated (Rosetta under Docker), so they need measuring on native
hardware. Budgets and measurements, % of one core at 48 kHz per
instance, Apple M-series Mac:

| | idle | outputs | busy |
|---|---|---|---|
| Sway budget | 0.3 | 0.4 | 3.0 |
| Sway measured (was) | 0.19 (0.50) | 0.27 (0.50) | 1.6 (1.6) |
| Fathom budget | 0.1 | 1.2 | 1.2 |
| Fathom measured (was) | 0.02 (0.58) | 0.55 (0.58) | 0.59 (0.59) |
| Sluice budget | 0.2 | 0.15 | 2.15 |
| Sluice measured | 0.077 | 0.066 | 1.07 |
| Fog budget | 0.15 | 0.15 | 1.6 |
| Fog measured (was) | 0.06 (0.55) | 0.06 (0.56) | 0.78 (0.72) |
| Hadal budget | 0.1 | 0.45 | 0.45 |
| Hadal measured (was) | 0.029 (0.028) | 0.201 (0.147) | 0.214 (0.164) |
| Ballast budget | 0.05 | 0.25 | 0.3 |
| Ballast measured | 0.017 | 0.105 | 0.128 |
| Pan budget (with Ballast) | 0.1 | 0.4 | 0.45 |
| Pan measured (with Ballast) | 0.034 | 0.187 | 0.211 |
| Squall budget | 0.05 | 0.05 | 0.1 |
| Squall measured | 0.023 | 0.024 | 0.029 |
| Rigging budget | 0.3 | 0.3 | 0.6 |
| Rigging measured | 0.14 | 0.14 | 0.30 |
| Wake budget | 0.35 | 0.35 | 0.4 |
| Wake measured | 0.155 | 0.157 | 0.178 |
| Narrows budget | 0.7 | 0.7 | 4.9 |
| Narrows measured | 0.33 | 0.33 | 2.43 |
| Eddy budget | 0.2 | 0.3 | 1.7 |
| Eddy measured (was) | 0.086 (0.089) | 0.130 (0.140) | 1.36 (0.845) |
| Spindrift budget | 0.4 | 2.05 | 2.1 |
| Spindrift measured | 0.245 | 1.347 | 1.385 |
| Beacon budget | 0.25 | 0.4 | 2.05 |
| Beacon measured (most of three runs) | 0.121 | 0.182 | 1.013 |
| Neap budget | 0.4 | 0.5 | 0.7 |
| Neap measured (most of three runs) | 0.185 | 0.250 | 0.337 |
| Doldrums budget | 0.15 | 0.15 | 0.5 |
| Doldrums measured (most of three runs) | 0.100 | 0.100 | 0.308 |

Neap's idle case is the defaults with nothing patched (Drift on, Triangle),
outputs is Sine and 0-10 V, and busy has RATE CV and WEIGHT CV moving at full
attenuverter, RESET pulsing and the mark swept. Its fourth case, spring, is busy
with Spring linked and all four T outputs patched, so the core runs from
the engine's shaped cycles: budget 0.75, measured 0.375 (Spring's own cost
is the difference from busy, about 0.04). Its libm cases are idle, outputs and
spring, knobs-still, and busy (reported). Budgets are about twice the
measurement, rounded up to the next 0.05, written for new code, so nothing "was"
to record; the other-machine budgets are three times the Mac's, provisional.

Doldrums' idle case is the defaults with nothing patched (recording
silence: a grain every hop all the same), outputs records a 220 Hz tone at
the defaults, and busy records stereo noise at the Long grain with a
16-note comb from V/OCT moving (Harmonics 1), Blur 1, Shift +7 and the
Stretch CV moving, so the tables are always being rebuilt. Budgets are 1.5
times the most of three runs, rounded up to the next 0.05; the other-machine
budgets three times the Mac's, provisional. Its libm cases are idle, outputs
and busyStill (busy with the CVs still), knobs-still, and busy (reported;
it measures 0 too). `libm-calls` wraps `sqrtf` and `sqrt` as well as the
list below, for the per-bin roots in Doldrums' grain and tables: on the Mac
they are instructions (0 calls, and a planted call through a pointer is
counted, about 98 000 a second). Doldrums' ring takes a 64-bit remainder
(`now % capacity`) every sample; replacing it with a mask moved idle by
less than the runs' own spread (about 19 to 21 ns a sample either way).

Doldrums also has the design's bound on the worst single sample ("Staging":
under 100 us on the development Mac at 96 kHz with the Long grain), which
the bench measures after its cases and fails over (bench.inc's `extra()`;
elsewhere 300 us, provisional): the busiest tables (busy with Tilt and 32
partials), each sample timed, the least of three identical runs after one
untimed run that brings the core up to its working clock. Measured
2026-11-26 on an M5 Pro, three runs of `make bench`, µs:

| Piece | 96 kHz, Long (65536) | 48 kHz, Long (32768) |
|---|---|---|
| boundary (swap, pass step, copy of the slice: 512 KB at 96 kHz) | 7.5 to 10.9 | 4.0 to 5.4 |
| WINDOW | 11.8 to 12.7 | 6.0 to 6.3 |
| FORWARD_L / FORWARD_R (one transform) | 59.1 to 63.0 | 30.1 to 32.0 |
| LOUDNESS (both sides) | 25.5 to 26.5 | 12.8 to 13.0 |
| SPECTRAL_L / SPECTRAL_R (Tables::apply, every table in use) | 49.6 to 58.0 | 21.8 to 24.6 |
| PHASES (both sides) | 32.2 to 32.6 | 15.7 to 17.4 |
| INVERSE_L / INVERSE_R (a transform and a pass) | 68.5 to 70.7 | 32.1 to 33.3 |
| add | 25.0 to 28.0 | 12.4 to 12.8 |
| the worst Tables::work call (budget 4096 / 2048) | 28.8 to 32.6 | 16.1 to 16.7 |
| **worst** | **68.7 to 70.7** | **32.4 to 33.3** |

So no piece needed splitting. From a light load the core starts slow:
timed without the untimed run, the first grains' pieces took up to 1.7
times as long (SPECTRAL_L 96 us on the first grain, 56 us by the
seventeenth), which no split of a transform could bring under the bound.

Narrows' idle and outputs cases are one stereo voice through the ladder and
its busy case is sixteen voices with everything engaged; it also has a state
case (one voice through the state core): budget 0.5, measured 0.24.

Beacon's idle case is one voice (nothing patched), its outputs case one
channel with a 2 Hz gate, and its busy case sixteen channels with staggered
gates, RETRIG pulses, every CV moving and Shape at 0.8. Its busy libm case
runs with those CVs moving and still requires 0 calls (stricter than Squall's,
whose busy case only reports).

Eddy's idle has no tape; outputs plays a 10 s tape at the defaults; busy
is a full tape (5 minutes) at Speed 2 by a moving CV, Dub on, Wear and Smear
at 1 and a window of about 1 s, so two voices read, write and wear at once
all the time and the overview is always rescanning. Its measurements are
the most of three runs at 1, 4 or 16 instances. Busy rose from 0.845 to
1.36 with the band-limited read (2026-11-22; most of it is the two
voices' kernels at speed 2), inside the budget it had. Two more cases:
slow, the 10 s tape at half speed (the module's common case, through the
interpolating kernel): budget 0.35, measured 0.162 (0.138 with the
Hermite read before); and fast4, the heaviest steady patch (Speed 4 by
the knob at 2 and V/OCT +1 V, Dub on, Wear and Smear at 1, a window of
about 0.1 s, so two voices read at 4 and write all the time): budget
3.85, measured 1.91. Four voices at 4 at once (retriggers and joins
overlapping) are rarer and not a case: about 0.42 % of a core more per
voice read at 4. Its libm cases are idle, outputs, busy with the Speed CV
still and slow, all knobs-still.

Spindrift's idle is nothing patched at the module's defaults, which make a
cloud (Density 0.65, Dry/Wet 0.5), so Beads' grains run on silence, with the
synth set to Never; outputs is Digital with Density 0.85 and Size 0.95, so 30
to 32 of Beads' 32 grains sound, noise into both inputs; busy adds Feedback,
Reverb and a moving CV into all eight CV jacks. Three more cases: dirty (as
outputs, in Dirty Tape, through the edge converters), budget 0.95, measured
0.620; delay (Size fully clockwise, Feedback and Reverb up), budget 1.15,
measured 0.736; synth (the menu's Always), budget 0.35, measured 0.230. Its
budgets are 1.5 times the most measured in three runs at 1, 4 or 16
instances, rounded up to the next 0.05 (the idle one set after the defaults
changed: 0.245, budget 0.4). Its libm cases are all six, all knobs-still but
busy.

Sluice also has a mixDown case (as busy, but only D connected): budget 2.25,
measured 1.11. Its budgets are about twice the measurement, rounded up to
the next 0.05, and are written for a new engine, so nothing "was" to record.
Busy is 16 voices on every row with a moving CV on every row, Levels 0.5,
Exp. Its other-machine budgets are three times the Mac's, as the others are.
Fog sleeps when its input and tail are silent (under 1e-5 V for 0.25 s or
twice the longest line), so idle and outputs cost 0.06 (twice, rounded up to
the next 0.05: 0.15). Its quiet case (+/-3 mV noise into both inputs, knobs at
their defaults, no CV) keeps the engine awake and measures 0.57-0.66; its
budget is 0.95, under twice, so the cache-aliased delay lines (1.03-1.11)
fail it. Fog's lines run whenever it is awake; its busy is stereo noise, all five CVs moving at full attenuverter, Bloom and
Drift up, Mix 0.5, in HAAR (0.78, 0.85 at 16 instances; it was 0.72 before
the four voicings were tuned and the wet leveller landed). busyIce is the
same in ICE, with its in-loop and feed-forward shifters: 0.83 (0.86 at 16),
budget 1.65. The spec's target is busy under 3 %.

The idle and outputs budgets are set so the pre-issue-#8 engines fail
them on the Mac. Sway busy and Fathom outputs and busy cost the same
before and after there (macOS's libm is fast), so those budgets catch
only a gross regression.

Timing on macOS or Linux cannot see what made Windows slow, so
`make libm-calls` (part of `make test`, on Linux and macOS) counts the C maths
library calls every declared engine makes, with every libm entry point it
could reach wrapped, in the cases its `bench.inc` lists for it. On Linux
GNU ld's `--wrap` sends each call to a counter; on macOS, whose ld has no
`--wrap`, the tool defines those functions itself, so ld binds the engine
code's calls to them before libSystem's, and they count the call and
forward it (`src/bench/libm_calls.cpp`). The functions, the cases and the
pass mean the same on both. In a case
marked knobs-still it requires zero per sample after a warm-up (today:
Fathom idle and with outputs patched; Sluice idle, outputs and busy;
Sway idle, with outputs patched and
with a still mono FM cable). In a case with a knob moving, calls are
reported, not failed. The pre-issue-#8 engines made 25 (Fathom) and 44
(Sway) per sample.

Per-instance cost is the same at 16 instances as at one. Before this
bench (issue #8), the engines called libm's exp2, pow, sin, cos and tanh
tens of times per sample; MinGW's libm, which the Windows build uses, is
much slower than macOS's or glibc's, which is why Windows showed several
percent per idle instance. Keep libm out of per-sample paths (use
`src/FastMath.hpp`, whose bounds `test_fast_math` checks) and work out per
block or on change what does not need per-sample precision.

Instances share no mutable state: `make engine-globals` (part of
`make test`) fails if an object that calls every declared engine's
`process` and `reset` holds writable global state, as
`make shoal-globals` does for Shoal's firmware.

`make bench-modules` (part of `make test`) keeps a module from slipping
past all three: it fails, naming the module, if a folder has a
`*Engine.hpp` but no `bench.inc`, or if a declaration lacks an idle,
outputs or busy case, either budget of a case, or a knobs-still libm
case. Shoal is outside this (its firmware is benched by
`make shoal-bench` and checked by `make shoal-globals`).

### 11. Build everywhere before merge

`make test` on macOS proves nothing about Linux or Windows. Before a branch
merges, run the Linux test command and both cross-builds on the final commit.
`git archive HEAD | docker run --rm -i ...` leaves the repo's `build/` alone.

Why: a missing `#include <cstring>` compiled on macOS and broke Linux and
Windows for four commits before the end-of-stage build caught it.

### 12. Look at what you drew

For a panel or widget change, view `build/screenshots/<Module>.png`. Check
for overlap, clipping, alignment and legibility at 100 % zoom. `make smoke`
shows only the default state, so render the others once through a temporary
preview hook (not committed): each mode, the longest list, the densest
control, and any error or busy state.

Why: a loop variable reused in the panel generator put the brand mark
off-centre, and a jack touched its card's border; both were only visible in
the picture.

### 13. Generated files

`res/`, `res-src/`, `src/<module>/*Layout.hpp`, `src/RkaTheme.hpp` and
`res/components/` come from `tools/gen_panels.py`. After `make panels`,
`git status` must show no change to any other module's generated files.
`make test` fails if any committed one is not what the generator writes
now; without Inkscape it checks everything but the lettering's outlines in
`res/<module>/`, which only `make panels` and `git status` can.

### 14. Never fail silently

A limit that can be exceeded (a fixed array, a queue, a row count) either
cannot be exceeded by construction (`static_assert`) or sets a visible flag
that a test checks. No clamping and carrying on.

### 15. Manual checks are written down

What only a person can verify goes in a numbered list at the end of the plan
and is handed over with the build. State plainly what has not been tried with
a real mouse or in a live Rack. Automated checks do not stand in for them.

### 16. Aliasing is measured

A module that generates sound, has a nonlinearity, or reads a buffer at a
variable rate has an offline test that measures its aliasing with
`rkatest::AliasMeter` (`src/testing_alias.hpp`) against a bound stated in
dB, at 44.1 and 48 kHz, and that bound is shown to fail by a mutation that
removes the anti-aliasing (rule 1). A source left alone is pinned at its
measured level plus a few dB. A module that needs no such test (linear,
control-rate only) says why in the table below. `docs/DSP-ALIASING.md`
has the sources, the remedies and how to set a bound.

Why: Eddy read its tape faster than normal through a Hermite interpolator
and nothing else; at speeds 2 to 4 a high tone came out entirely as an
alias at full level, and no test measured it.

## Adding a module

A new module with per-sample DSP gets the CPU bench, the libm check and
the globals check by declaring its engine; nothing in `src/bench/` or
`tests.mk` changes.

1. Put the per-sample logic in a Rack-free engine, `src/<folder>/<Name>Engine.hpp`
   (`rka::<Name>Engine`, fed a frame of params and port voltages), and have
   the module's `process()` only fill the frame, call the engine and write
   the results, as `Sway.cpp` and `Fathom.cpp` do.
2. Write `src/<folder>/bench.inc` (copy `src/sway/bench.inc` or
   `src/fathom/bench.inc`; `src/bench/BenchModule.hpp` lists what it
   defines): a `struct rkabench::<folder>` with an `Instance` (the engine
   and its inputs at the module's defaults, with `process`, `reset` and
   `probe`), the bench cases idle, outputs and busy (each a function that
   sets the inputs for sample t, one-time set-up under `t == 0`), each
   case's budget for Apple arm64 and elsewhere, and the libm cases, at
   least one with the knobs still. Measure before setting budgets
   (`make bench`, three runs) and leave about 1.5x room or more.
3. In per-sample paths use `src/FastMath.hpp` instead of libm, and work out
   per block, or only when its input changes, what does not need
   per-sample precision.
4. Optimising existing code: freeze a copy of the code before the change in
   `src/<folder>/testdata/ref/` (namespace `rkaref`, a "never edit"
   header) and add a `test_engine.cpp` that renders the same input
   through both and bounds the difference, as `src/fathom/test_engine.cpp`
   and `src/sway/test_engine.cpp` do; add it to `PLUGIN_MATH_TESTS` in
   `tests.mk` so it builds with the plugin's floating-point flags.
5. If it makes or bends sound, name its aliasing sources in its design
   (`docs/DSP-ALIASING.md`) and add a `test_alias.cpp` as rule 16 says,
   or say in the table below why it needs none.
6. Then, with no other change: `make test` runs `bench-modules`,
   `engine-globals` and `libm-calls` for it, and
   `make bench` times it and checks its budgets. Add the folder to
   `MODULES` in the Makefile and fill in its column of the table below.

## Review

- Each task is reviewed by someone who did not write it, against its brief,
  for both "does what was asked" and "is built well". Fixes get a scoped
  re-review.
- The whole branch gets one more review before merge, aimed at what a
  per-task review cannot see: behaviour across task boundaries, the
  invariants, and whether each "most likely to bite" case from the plan has a
  test or a manual check.
- Reviewers may write scratch probes against the Rack-free core to try a
  suspected sequence. One such probe found a way to strand Shoal's list in a
  sub-editor that no test covered.
- A finding that is not fixed is written down with a reason, not dropped.

## Lessons from Shoal

Defects found by this process, by kind:

| Defect | Caught by |
|---|---|
| CI compiled each test alone and would have broken | Whole-branch review; verified in a Linux container |
| Reset left user patterns from the previous state | Whole-branch review; fresh-state test |
| Encoder queue accepted 257 detents, not 256 | A test asserting the exact count |
| Restore allocated on the audio thread | Per-thread allocation counter |
| Scroll wheel five times too slow on macOS | Review against Rack's source; offline detent test per platform |
| A list edit could open a sub-editor and strand the list | Reviewer's scratch probe; now an invariant checked after every edit |
| A setting slid under an editing cursor mid-edit | The same invariant, in another app |
| An edit and an encoder turn applied out of order | A test with a distinct result per order |
| A queued panel event flushed by a recovery | Review of the firmware's side effects; test with a queued turn |
| Missing include broke Linux and Windows | End-of-stage cross-builds |
| Float formatting on the audio thread | Review; replaced with integer formatting and a comparison test |
| A dropped edit when the retry buffer was full | Review of the bookkeeping; offline test |

Three habits behind most of them:

1. **State the invariant, then check it everywhere.** "After an edit the app
   is back on its menu and not editing" is checked after every accepted edit
   in every test and in the fuzz run, not in one special-case test.
2. **Read what a borrowed call really does.** Suspend, resume, flush and
   "close" each had side effects beyond their name.
3. **Treat a stale or rebuilt structure as the normal case.** Lists were
   rebuilt mid-edit, apps changed under open menus, rows vanished mid-drag.
   Key on stable ids, not positions, and re-find before each use.

## What each module has today

| | Sway | Fathom | Shoal | Sluice | Fog | Hadal | Ballast | Pan | Squall | Rigging | Wake | Narrows | Eddy | Spindrift | Beacon | Neap | Spring | Doldrums |
|---|---|---|---|---|---|---|---|---|---| --- | --- | --- | --- | --- |---|---|---|---|
| Rack-free core with offline tests | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes (Beads' own DSP, vendored, behind a Rack-free engine; `test_vendor` and `test_engine` hold it to the untouched upstream code) | Yes (`BeaconEngine` over `Voice`, `Timing`, `Curve`, `EnvelopeView`; `test_timing`, `test_voice`, `test_engine`, `test_view`; the MODE switch's stepping in `Toggle3Step.hpp`, `test_switch`) | Yes (`NeapEngine` over `Cycles`, `Tide` and `NeapRate`; `test_rate`, `test_cycles`, `test_tide`, `test_engine`; the expander interface in `NeapExpander.hpp`) | Yes (`SpringCore` and `SpringLink`; `test_core`) | Yes (`DoldrumsEngine` over `Ring`, `PassLogic`, `Tables`, `Grain` and `Laws`; the file `TapeStore`, the window `TapeView`, the module's side of the hand-over `TapeKeeper`, the panel's words `Readout`, the patch data `SavedState`; `test_laws`, `test_ring`, `test_pass`, `test_tables`, `test_grain`, `test_fft`, `test_engine`, `test_view`, `test_store`, `test_readout`, `test_saved`) |
| `-Wall -Wextra`, `-Werror` in CI | Yes | Yes | No (vendored firmware warns) | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes for its own code (the vendored Beads and the reference build without `-Wall`: upstream warns) | Yes | Yes | Yes | Yes for its own code (the vendored pffft is built without `-Wall`: upstream warns) |
| Sanitizers | No | No | Yes | Partly (`test_hostile` only: the fuzz, hostile input, sample rates, allocation) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile`, `test_files`, `test_jobs` and `test_book` (the worker's bookkeeping, `WorkerBook.hpp`, by scenario and a seeded sweep) under ASan and UBSan, with every tape fenced by ASan poisoning; `test_threads` under ThreadSanitizer) | Yes (every test under ASan and UBSan; `test_threads` under ThreadSanitizer) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Partly (`test_hostile` only) | Yes (every test under ASan and UBSan; the tape fenced with ASan's poisoning in `test_ring` and `test_hostile`; `test_threads` and `test_keeper` under ThreadSanitizer) |
| Hostile-input and sample-rate sweeps | Partly | Partly | Yes | Yes (NaN, infinity, 1e30, a sample-rate change, 1 Hz to 768 kHz, 0 and negative rates) | Yes (NaN and infinity on every input, CV and parameter, 1 Hz to 768 kHz, 0, negative and NaN rates, a rate change over a tail) | Yes (NaN and infinity on every CV, 1 Hz to 768 kHz, 0, negative, NaN and 1e9 rates, rate changes) | Yes (NaN and infinity on every input, CV and CHAIN, 1 Hz to 768 kHz, 0, negative, NaN and 1e9 rates, rate changes) | Yes (NaN and infinity on every knob, attenuverter and CV, 0, negative, NaN and 1e9 rates) | Yes (NaN and infinity on every CV, Clock and Freeze, 1 Hz to 768 kHz, 0, negative, NaN and 1e9 rates, rate changes) | Yes (NaN, infinity and 1e30 on every input, CV and knob, 1 Hz to 768 kHz, 0, negative and NaN rates, rate changes while ringing, a tuned square with Ring at 60 s, STRUM at audio rate) | Yes (NaN, infinity and 1e30 on every input, CV and parameter, 1 Hz to 768 kHz, unusable rates, rate changes while frozen and at Feedback 110 %, gate and clock at audio rate) | Yes (NaN and infinity on every port and knob, 1 Hz to 768 kHz, unusable rates, rate changes, channel counts changing every sample, FM, the core switch, Mode and PING at audio rate) | Yes (NaN, infinity and 1e30 on every input and parameter, 1 Hz, 8 kHz and 768 kHz, rate changes while playing, dubbing, recording, copying and clearing, unusable rates, the window's extremes for 60 s) | Partly (`test_hostile`: ±12 V, NaN and infinity on every audio and CV jack, every mode and quality, at 44.1 kHz; `test_engine`: 44.1, 48 and 96 kHz and rate changes, frozen and not) | Yes (NaN, infinity and 1e30 on every param and port, MODE 7 and -3, 1 Hz to 768 kHz, rate changes mid-stage, bad rates, GATE and RETRIG at audio rate on 16 channels, channel counts changing every block) | Yes (NaN, infinity and 1e30 on every frame field, bad sample times, 8 kHz to 768 kHz and changes between them, Wave, Level and Drift toggled every 1 to 481 samples, RESET held, in noise and at audio rate) | Yes (NaN, infinity and 1e30 on every cycle input and the sample time, every mask of patched T outputs and bad indices, 8 kHz to 768 kHz, rate changes mid-pulse, re-linking with no trigger burst, the link counter against a model) | Yes (NaN, infinity, 1e30 and denormals on every input, CV, gate and param, 16 V/OCT channels at +-12 V (roots past Nyquist and far under 16 Hz), channel counts and grain sizes out of range, 44.1 to 768 kHz and changes between them, unusable rates; Position and Stretch CV at audio rate; a +-10 V sine on the narrowest tooth; an empty comb; DC) |
| Random fuzz with seeds | No | No | Yes | Yes (`test_hostile`: 600 fixed-seed trials against a scalar model; no seed sweep) | Yes (`test_hostile`: 200 fixed-seed trials; no seed sweep) | Yes (`test_hostile`: 10^6 samples of fuzzed knobs, CVs, patch states and rates, fixed seed) | Yes (`test_hostile`: 10^6 samples of fuzzed knobs, CVs, poly counts, patch states and rates, fixed seed) | Yes (`test_hostile`: 10^6 samples of fuzzed Pan and Ballast controls, modes and links, fixed seed) | Yes (`test_hostile`: 10^6 samples of fuzzed knobs, CVs, clock, Freeze, menus and rates, fixed seed) | Yes (`test_hostile`: 10^6 samples of fuzzed knobs, CVs, triggers, patch states and rates; no seed sweep) | Yes (`test_hostile`: six seeds of fuzzed knobs, CVs, switches, patch states and resets) | Yes (`test_hostile`: twelve seeds of fuzzed knobs, CVs, channel counts, rates and resets) | Yes (`test_hostile`: five seeds, 10 minutes of audio, every control, gate and button each 32-sample block, and the worker's requests) | No (fixed hostile segments; a seeded fuzz is future work) | Yes (`test_hostile`: 3 fixed seeds x 10^6 samples of everything; no seed sweep) | Yes (`test_hostile`: 3 fixed seeds x 10^6 samples of every field, toggle, rate and reset; no seed sweep) | Yes (`test_hostile`: 3 fixed seeds x 10^6 samples of the core alone, and of Neap's fuzzed engine through it with links dropping; no seed sweep) | Yes (`test_hostile`: 3 fixed seeds x 2 x 10^6 samples of every param, CV, gate, input and menu setting together, rate changes, resets and new seeds; no seed sweep) |
| Golden saved-state image | No (params only) | No (params only) | Yes | No (params only) | No (params only) | No (params only) | No (params and `dcBlock`) | No (params, `stereoMode` and `panLaw`) | No (params, `range`, `loopLength`, `history`, `step` and `swell`; round trip and bad values tested on the engine) | No (params and `freePitch`) | No (params, `style` and the two latches) | No (params only) | Yes (`test_jobs`: the stored tape's files made by hand and loaded to the bit; partial and damaged stores) | Yes (`test_buffer`: a version 1 `buffer.bin` made by hand, `testdata/GoldenBufferV1.hpp`, loaded to the bit; the patch data's keys by round trip and bad values) | No (params only) | No (nothing saved by the engine; the menu's Drift, Wave and Level come with the module) | No (nothing saved) | Yes (`test_saved`: a `tape.wav` made by hand, `testdata/GoldenTapeV1.hpp`, read to the bit and opened through the module's worker; what the save writes is that file byte for byte; the patch data's four keys from a golden set, each bad value alone; a patch saved without its tape opens silent; the param ids pinned) |
| Allocation check on the audio path | No | No | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes (operator new and delete, and every malloc and free through the sanitizer's hooks) | Yes (operator new, in `test_engine` and `test_hostile`) | Yes (operator new, in `test_hostile`) | Yes (operator new, in `test_hostile`) | Yes (operator new, in `test_hostile`) | Yes (operator new and delete, every malloc and free through the sanitizer's hooks, and locks: pthread_mutex_lock and trylock, std::mutex, in `test_hostile`) |
| Thread tests | Not needed (no shared state) | Not needed | Yes | Not needed | Not needed | Not needed | Not needed | Not needed (one atomic counter) | Not needed (no shared state) | Not needed (no shared state) | Not needed (no shared state) | Not needed (no shared state) | Yes (`test_threads`: the audio thread in a trigger storm, a worker offering, collecting and copying sets, and the window reading overviews, under ThreadSanitizer) | Yes (`test_threads`: the window copying the view and the menu writing settings while the engine runs, under ThreadSanitizer; `test_save_race`: the save copying a frozen buffer while it unfreezes, the race detected and the copy discarded) | Not needed (channel 1's stage, phase, level, start, goal and mode are published as relaxed atomics, read by the window) | Not needed here: Neap calls Spring's `fromNeap` on its own thread every sample, so the shared state is Spring's; see Spring's cell and the `fromNeap` contract in `src/neap/NeapExpander.hpp` | Not needed (one relaxed atomic counter, `SpringLink`, checked against a model in `test_hostile`, and the reset latch in `SpringRun`, tested in `test_core`: every core access is inside `fromNeap`); `core.out` is read by Spring's `process()` unsynchronised by ruling (aligned 32-bit floats) | Yes (`test_threads`: the window's view handed over, a tape swapped in, the save against thaws; `test_keeper`: the module's worker, two savers, the window and Rack's lock (onAdd, a rate change, Initialize) modelled with a shared mutex, under ThreadSanitizer, with a watchdog for deadlock) |
| CPU bench | Yes (`make bench`) | Yes (`make bench`) | Yes (`make shoal-bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`, with Ballast) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`) | Yes (`make bench`, inside Neap's declaration: its `spring` case feeds the core from the engine's shaped cycles) | Yes (`make bench`, with the worst single sample at 96 and 48 kHz against the design's 100 us) |
| No libm call per sample, knobs still | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Not checked | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`) | Yes (`make libm-calls`, in Neap's `spring` case) | Yes (`make libm-calls`, with `sqrtf` and `sqrt` wrapped too; the knobs-moving case also 0) |
| No writable global state | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make shoal-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`, and `make spindrift-globals` for the vendored Beads) | Yes (`make engine-globals`) | Yes (`make engine-globals`) | Yes (`make engine-globals`, through Neap's declaration) | Yes (`make engine-globals`) |
| Aliasing measured (rule 16) | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Issue #67 | Yes (`test_alias`: the read at every speed, band-limited to 50 dB from x1.02 up, images 60 dB down below 1, level against speed, x1 to the bit; wear, the limits and dub pinned; `test_band_read`) | Yes (`test_alias`: grains at Pitch 0, +12 and +24 and the synth in every quality, at 44.1 and 48 kHz, held to what Beads' own code measures at its own rate plus 3 dB, worst +0.84 dB; the edge converters alone against an absolute bound) | Not needed: Beacon makes control signals, with no oscillator, waveshaper or sample-rate conversion. Its fastest segment is a 1 ms ramp (at least 44 samples at 44.1 kHz), its levels are piecewise smooth curves, and the Shape (5 ms) and Sustain (2 ms) one-poles take the steps out of their moves; Hold's steps are slew-limited by Attack and Decay (1 ms at the fastest). Anything audible from its corners comes from the VCA or filter it feeds, which the user's patch band-limits; no bound measured | Not needed: Neap makes control signals. Cycle 1 tops out at 160 Hz (40 Hz, +2 octaves of RATE CV), the others are slower, and the sine is `fastSin2pi`; the triangle's corners alias at about -86 dB re the fundamental at 44.1 kHz in a scratch measurement with `AliasMeter` (exactly nothing at 48 kHz, where 160 Hz divides the rate); the crossfade is 10 ms linear; no oscillator or waveshaper in the audio band; no bound pinned | Not needed: Spring's folds, min/max and codes follow Neap's control-rate cycles (at most 160 Hz; F's corners at the zero crossings alias at about -74 dB re the fundamental at 44.1 kHz at the fastest, a scratch measurement with `AliasMeter`; none at 48 kHz); T pulses and S codes are gates and steps, as Beacon's gates are; no bound pinned | Yes (`test_alias`: a held sine at 0.1, 0.25 and 0.4 of the rate, each grain size, Shift 0, +12 and +24, at 44.1 and 48 kHz; at 1 V everything outside a skirt of 32 grain bins (times the shift) at least 60 dB down, worst -64.1 dB, and a tone shifted past the top gone (-130 dB or less), nothing folded back; at 5 V the soft bound's products pinned at -34 dB, worst -37.0 dB) |
| Smoke screenshot | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes (with a tape: `make smoke` makes a WAV for it, and loads it into Eddy in two headless runs, one quit while it loads) | Yes | Yes (the browser preview: the default envelope, idle; the running stage, the dot and Hold are checked by hand) | Yes (the default panel: knobs at their defaults, lights dark) | Yes (the default panel: outputs, LINK dark) | Yes (the browser preview from a real engine run offline in each state: frozen (the default screenshot), and `Doldrums-running`, `-held`, `-stopped`, `-frozen-held`, `-frozen-stopped`; headless runs of a stored tape read, a cut one warned of and a missing one silent) |

Fog also has `make fog-soak` (opt-in, minutes, not in `make test`): every
mode at v = 1 and three corners of Size, Damp and Drift, 10 minutes of noise
then 3 minutes of silence, with the level under the limiter's bound and the
tail following the T60 law.

## Future work

Decided 2026-11-15: bringing Sway and Fathom up to these rules is future
work, to be planned on its own after Shoal's friendly mode. Not started.

- Sway and Fathom tests build with the sanitizers, as Shoal's do (rule 3).
- Sway and Fathom get hostile-input and sample-rate sweeps and a seeded
  random fuzz to the standard of rule 4.
- Sway and Fathom get an allocation check on the audio path (rule 6).
- ~~Sway and Fathom get a CPU bench (rule 10).~~ Done: `make bench`
  (issue #8).
- Their existing tests are each shown to fail by mutation (rule 1).

Other open items:

- Shoal's own code (not the vendored firmware) should compile with
  `-Wall -Wextra -Werror` in CI; today CI's `-Werror` reaches only the other
  modules' tests.
- CI does not run `make smoke`, the benches or the fuzz seeds; those are run
  by hand before a merge. ThreadSanitizer is part of `make test` for
  Eddy's `test_threads` only, and runs only where it can start (rule 7):
  on the Mac, and on Linux where address randomisation is narrow enough or
  `setarch -R` is allowed. Elsewhere `make test` skips it and says why,
  unless `TSAN_REQUIRED=1` is set, when it fails instead. CI sets it.

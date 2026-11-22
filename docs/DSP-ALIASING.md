# Aliasing

How modules in this plugin keep aliasing out, and how it is measured.
`TESTING.md` rule 16 is the testing rule; this page is the practice behind
it. Anything a module makes above Nyquist does not vanish: it folds back
below it, at frequencies unrelated to the pitch, and is heard as grit or
inharmonic whistles. It is cheapest to design out at the start.

## The three sources and their remedies

1. **Generated discontinuities** (oscillator edges, hard sync, a step used
   as audio, a wrapping phase). Use band-limited steps: polyBLEP for saws
   and squares, minBLEP or BLIT where it must hold for high notes and sync,
   polyBLAMP for corners (a triangle, a clipped ramp). Mipmapped wavetables
   for table oscillators. Skip the correction only where the rate is too
   low to alias (an LFO).
2. **Nonlinearities** (saturation, clipping, wavefolding, filter drive).
   Oversample the nonlinearity alone, with half-band filters around it:
   about 2x for soft saturation, 4x for hard drive, 8x or more for folders
   and hard clips. For a memoryless shaper, antiderivative anti-aliasing
   (ADAA) is often cheaper. A nonlinearity inside a feedback loop (a delay
   or tape that is saturated each pass, a driven filter's resonance) is
   worse than one outside it: its aliases go round again and build up.
3. **Resampling and variable-rate reads** (delays with moving time, tape,
   samplers, pitch shifters, grains). Reading slower than the stored rate
   makes images, which the interpolator controls: Hermite at least, never
   linear for audio. Reading **faster** aliases, and no interpolator fixes
   that: the content must be low-passed under the new Nyquist before or as
   it is read (a low-pass that tracks the rate, a kernel whose cutoff
   scales with the rate, or pre-filtered octave copies where the buffer
   does not change). Writing at a variable speed is the mirror image: when
   the head moves less than a frame a sample, average or filter the input
   over its travel rather than dropping samples.

## Across all three

- Audio-rate modulation counts: FM, AM and ring modulation widen a
  spectrum, and a delay time moved at audio rate is a variable-rate read.
- Parameter steps are discontinuities. Every control that reaches the
  audio is smoothed.
- Test at 44.1 and 48 kHz, where it is worst. Never rely on a high session
  rate.
- Do not oversample linear processing: it adds cost and nothing else.
- Spend CPU by audibility. Inharmonic components near a bright, sustained
  pitch are what is heard; content folding into the top octave above
  16 kHz, or masked by noise, matters less.

## What this repo already has

- `src/sway/Waves.hpp`: two-sample polyBLEP (`polyBlep`, `sawBlep`,
  `sqrBlep`).
- `src/fathom/Halfband.hpp`: a 2x polyphase IIR half-band pair
  (`Upsampler2xT`, `Downsampler2xT`), float or `Float4`, stopband 78 dB:
  the building block for oversampling a nonlinearity.
- `src/Resample.hpp`: offline windowed-sinc conversion (Kaiser, 32 zero
  crossings a side, about 90 dB). Allocates: never on the audio thread.
- `src/Tape.hpp`'s write head (`TapeWriter`): slower than 1 it box-filters
  the input over the head's travel; faster than 1 it interpolates linearly
  between input samples (which leaves images; Eddy measures them).
- `src/BandLimitedRead.hpp`: reading a buffer at a variable rate. Faster
  than 1, a windowed sinc whose width scales with the speed, from a table,
  cutting at the output's Nyquist; at 1 and slower the same window as an
  interpolating kernel (whole frames read exactly, flat to 0.8 of Nyquist,
  images 63 dB down); the two crossfaded between 1 and 1.02. Eddy uses it;
  it suits any tape, sampler or delay read.

## At design time

Every design doc for a module that makes or bends sound names its aliasing
sources, by the three kinds above, and says what is done about each, or
why nothing is needed (a linear filter, a control-rate path, content that
cannot reach Nyquist).

## How to measure

`src/testing_alias.hpp` (`rkatest::AliasMeter`, tests only): give it a
rendered signal, its rate and the frequencies that are supposed to be
there (a tone and its harmonics below Nyquist, the partials a test put
in). It returns the strongest component that is not one of them, in dB
re a stated level (a full-scale tone) or re the strongest expected one.
A Kaiser window (beta 20) and an FFT of up to 65536 points; each
component's level comes from its main lobe's power, so it does not depend
on where it falls between bins. Its own test (`src/eddy/test_alias_meter.cpp`)
shows it reads a planted alias within 1 dB and a clean float sine below
-120 dB.

Setting a bound:

- Drive the module the way that makes aliasing worst: a loud tone high in
  the band (0.1 to 0.4 of the rate), the nonlinearity pushed past its knee,
  the read at its fastest.
- Against a full-scale tone, aliases 50 dB down are a reasonable target
  for a correction (quiet under music, audible only alone and bright); 60
  to 70 dB is clean. A source left alone is pinned at its measured level
  plus about 3 dB, so a regression fails and the number is on record.
- Write the measured value and the date beside each bound, and show the
  check fails with the anti-aliasing removed (rule 1).

Eddy's checks are `src/eddy/test_alias.cpp` (every case at 44.1 and
48 kHz; `test_alias --table` prints the measurements) and
`src/eddy/test_band_read.cpp` (the band-limited read alone, swept finely).

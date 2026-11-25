# Pan

Ballast's pan expander, 6 HP: a pan knob, a CV attenuverter and a CV input
for each of Ballast's four channels. Place it directly against Ballast's
right side. Mono sources are panned; stereo sources are moved as a whole
image (true pan) or balanced, as you choose.

![Pan panel](Pan.png)

## Adding Pan

- **From Ballast's menu:** right-click Ballast and choose **Add Pan
  expander**. Rack places a Pan against Ballast's right side, pushing any
  neighbours along. The item is greyed out while a working Pan is already
  attached.
- **From the module browser:** place **Pan** so it touches Ballast's right
  edge.

**LINK** (low on the panel) lights while Pan is connected to a Ballast. It
is dark when Pan is not touching one, when there is a gap, when it sits
behind another Pan, or when it is bypassed.

One Pan acts per Ballast: a second Pan in the chain does nothing (its LINK
stays dark). If the first is bypassed, the second takes over.

## The panel

Four rows, one per Ballast channel, numbered 1 to 4: the **PAN** knob, its
**attenuverter**, then the **CV** jack.

## Controls

- **Pan:** hard left to hard right, centre at noon (the default). Rack's
  tooltip shows -100 % to +100 %. Turning it is smoothed (a 10 ms time constant).
- **CV:** added to the knob, scaled by the attenuverter: at full, +-5 V from
  centre sweeps hard left to hard right; fully anticlockwise inverts. The
  attenuverter defaults to 0. The CV is not smoothed, so audio-rate
  auto-pan works. A polyphonic CV uses its first channel.

**At centre, Pan changes nothing**: Ballast sounds exactly as without it,
in every mode.

## Mono and stereo sources

Ballast tells Pan what each channel carries: **mono** when one input (IN L
or IN R) is patched, **stereo** when both are.

- **Mono sources** are placed in the stereo field: hard left is all left,
  hard right all right.
- **Stereo sources** follow **Stereo sources** in Pan's right-click menu:
  - **True pan** (default): the whole image moves. Turning right folds the
    left side into the right, so nothing is lost; hard right is the two
    sides mixed on the right.
  - **Balance:** turning right fades the left side down, leaving the right
    as it is; hard right silences the left side.

## Pan law

Right-click Pan, **Pan law**:

- **No boost** (default): panning never makes a channel louder than it
  went in. A mono source hard over sits on its side at the same level as
  at centre. A true-panned stereo source hard over is the two sides mixed
  at half level each (a near-mono source keeps its level). Panning can
  never push a clean mix into Ballast's soft limit. The cost: a source
  sounds a little quieter at the sides than at centre (about 3 dB, for
  mono), so an LFO auto-pan dips slightly at each extreme.
- **Constant loudness:** a source keeps the same loudness wherever it is
  panned, which means up to **+3 dB** on its side when hard over (mono, and
  a near-mono stereo source in true pan). Smooth for auto-pan, but on a hot
  mix a hard pan can light Ballast's CLIP; turn the channel down a little.

Balance never boosts, under either law.

## Attaching and removing

Attaching a Pan whose knobs are already turned glides each channel in from
centre, settling in about 85 ms (a 10 ms time constant), so it does not
click. Removing or bypassing Pan is
a step back to centre (as unpatching a cable would be): do it with the
channel quiet if that matters.

## Bypass

A bypassed Pan is skipped: Ballast sounds as without it.

## Other details

- NaN or infinite values on any CV read as 0.
- The module stores its knobs, attenuverters, the stereo mode and the pan
  law with the patch.
- Pan does its work inside Ballast's processing, so it adds about 0.1 % of
  a CPU core to Ballast's own.

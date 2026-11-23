#!/usr/bin/env python3
"""Spindrift's dial diagrams for its manual: docs/spindrift/*.svg and *.png.

Each diagram draws one of Spindrift's controls whose travel is not plain
(Size, Density, Pitch, Time, Shape, the attenuverters) in the Instrument
look, and letters it with numbers worked out here from Beads' own formulas.
The constants those formulas use are read from the source they come from
(src/spindrift/beads/, src/spindrift/Controls.hpp, SpindriftEngine.hpp), so
a change there that moves a number fails this script or redraws the
picture; nothing on a dial is typed in by hand.

Colours, typeface and Inkscape's fonts come from tools/gen_panels.py. The
SVGs are written with their lettering as outlines (so they look the same
without D-DIN Condensed installed), and the PNGs are rendered from them.
Dark only: the manuals use one picture per figure, as the panel's.

Usage: python3 tools/gen_spindrift_dials.py [--inkscape PATH] [--facts]
  --facts  print the numbers the diagrams use, and write nothing.
"""
import argparse
import math
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_panels as gp  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "docs" / "spindrift"
WORK = ROOT / "build" / "dials"
SRC = ROOT / "src" / "spindrift"
DPI = 180

# --------------------------------------------------------------------------
# Beads' numbers, from the source.


def grab(rel, pattern, what, group=1):
    """The first match of `pattern` in src/spindrift/<rel>, or a failed run
    naming what was looked for (the code changed: look again)."""
    text = (SRC / rel).read_text(encoding="utf-8")
    m = re.search(pattern, text, re.S)
    if not m:
        sys.exit(f"gen_spindrift_dials: cannot find {what} in src/spindrift/{rel}; the code has changed: "
                 f"update this script")
    return m.group(group)


def grab_float(rel, pattern, what):
    return float(grab(rel, pattern, what))


F = r"([-+]?[0-9]*\.?[0-9]+)f?"   # a C float literal

# granular_player.h ComputeGrainDuration and Init.
GP = "beads/dsp/granular_player.h"
REVERSE_BELOW = grab_float(GP, r"const bool reverse = duration < " + F, "the reverse threshold")
SHORT_GAIN = grab_float(GP, r"std::min\(1\.0f, " + F + r" \* clamped_shortness\)", "the shortness gain")
SHORT_SEMIS = -grab_float(GP, r"SemitonesToRatio\(shortness \* " + F + r"\)", "the shortness range")
MAX_GRAIN_S = grab_float(GP, r"max_grain_duration_ = sample_rate \* " + F, "the longest grain")
# processor.cc: delay mode past 0.99, back below 0.98, over 1.5 s.
PROC = "beads/dsp/processor.cc"
DELAY_LEAVE = grab_float(PROC, r"delay_mode \? " + F + r" : ", "delay mode's way out")
DELAY_ENTER = grab_float(PROC, r"delay_mode \? [0-9.]+f : " + F, "delay mode's way in")
DELAY_TIME = grab_float(PROC, r"kDelayModeTransitionTime = " + F, "the delay crossover")
# trigger_generator.cc: Density.
TG = "beads/dsp/trigger_generator.cc"
MAX_HZ = grab_float(TG, r"kMaxGrainFrequency = " + F, "the fastest grain rate")
DEAD_LO = grab_float(TG, r"if \(density < " + F + r"\) \{\s*RenderPulseTrain", "the dead zone's left edge")
DEAD_HI = grab_float(TG, r"\} else if \(density > " + F + r"\) \{\s*RenderPulseTrain", "the dead zone's right edge")
DENSITY_SEMIS = grab_float(TG, r"SemitonesToRatio\(-density \* " + F + r"\)", "Density's range")
RANDOM_HALF = grab_float(TG, r"\n\s*" + F + r" \* max_pulse_frequency_ \* \\", "the random side's rate")
# The random gap: 1 / ratio_, ratio_ = -1 / LogOneMinusX(a + b * U).
RAND_A = grab_float(TG, r"LogOneMinusX\(" + F + r" \+ [0-9.]+f \* Random::GetFloat\(\)\)", "the random gap")
RAND_B = grab_float(TG, r"LogOneMinusX\([0-9.]+f \+ " + F + r" \* Random::GetFloat\(\)\)", "the random gap")
LOG_POLY = [float(x) for x in re.search(
    r"return -x \* \(1\.0f \+ x \* \(([0-9.]+)f \+ x \* \(([0-9.]+)f \+ x \* ([0-9.]+)f\)\)\);",
    (SRC / TG).read_text(encoding="utf-8")).groups()]
DIVISIONS = [int(x) for x in grab(TG, r"kDivisionRatios\[\] = \{([^}]*)\}", "the clock divisions").replace(
    "\n", " ").split(",") if x.strip()]
DIV_SCALE = grab_float(TG, r"Lookup\(kDivisionRatios, density \* " + F + r"\)", "the divider's scale")
PROB_SCALE = grab_float(TG, r"max\(\(density - [0-9.]+f\) \* " + F + r", 0\.0f\)", "the probability's scale")
# Controls.hpp: the pot laws.
CT = "Controls.hpp"
ATT_DEAD = grab_float(CT, r"std::fabs\(v\) - " + F, "the attenuverter's dead zone")
ATT_GAIN = grab_float(CT, r"\(2\.0f / " + F + r"\)", "the attenuverter's gain")
INTERVALS = [float(x.strip().rstrip("f")) for x in grab(
    CT, r"intervals\[8\] = \{([^}]*)\}", "Pitch's snap intervals").split(",")]
PLATEAU = grab_float(CT, r"\(fractional - " + F + r"\) \* 1\.5f", "Pitch's plateau")
# SpindriftEngine.hpp: CV scaling (volts to Beads' units) and the jacks' range.
ENG = "SpindriftEngine.hpp"
TIME_CV_SCALE = grab_float(ENG, r"cvs_\[CV_TIME\]\.process\(j\.time, " + F, "Time's CV scale")
TIME_CV_ADC = grab_float(ENG, r"cvs_\[CV_TIME\]\.process\(j\.time, [0-9.]+f, " + F, "Time's CV range")
# grain_envelope.h: the shortest attack and release.
GE = "beads/dsp/grain_envelope.h"
MIN_ATTACK_S = grab_float(GE, r"min_attack_ = " + F, "the shortest attack")
MIN_RELEASE_S = grab_float(GE, r"min_release_ = " + F, "the shortest release")
# The buffer (SpindriftEngine::bufferSeconds): 768,000 bytes, 16-bit stereo at 48 kHz.
BUFFER_BYTES = grab_float(ENG, r"([0-9]+)\.f \* 8\.f / \(16\.f \* 2\.f\) / 48000\.f\};\s*// Digital", "the Digital buffer")
DIGITAL_SECONDS = BUFFER_BYTES * 8 / (16 * 2) / 48000


def grain_seconds(size):
    """GranularPlayer::ComputeGrainDuration, in seconds (the player's
    max_grain_duration_ is that many seconds at any rate)."""
    reverse = size < REVERSE_BELOW
    c = size * 3 * (2 - size * 3) if reverse else 1 - 1.5 * (size - REVERSE_BELOW)
    s = min(1.0, SHORT_GAIN * c)
    return MAX_GRAIN_S * 2 ** (s * -SHORT_SEMIS / 12)


def regular_hz(d):
    """TriggerGenerator, left of the dead zone: a pulse train."""
    return MAX_HZ * 2 ** (-d * DENSITY_SEMIS / 12)


def mean_gap_factor():
    """The mean of -LogOneMinusX(a + b U), U uniform on [0, 1): each random
    gap is that many nominal periods (the polynomial integrated exactly)."""
    c = [1.0] + LOG_POLY          # x (c0 + c1 x + c2 x^2 + c3 x^3)
    lo, hi = RAND_A, RAND_A + RAND_B
    integral = sum(ck * (hi ** (k + 2) - lo ** (k + 2)) / (k + 2) for k, ck in enumerate(c))
    return integral / (hi - lo)


def random_avg_hz(d):
    """TriggerGenerator, right of the dead zone: random gaps; the long-run
    average rate."""
    nominal = RANDOM_HALF * MAX_HZ * 2 ** (-(1 - d) * DENSITY_SEMIS / 12)
    return nominal / mean_gap_factor()


def detune(knob):
    """Controls.hpp detuneLaw (cv_reader.h LAW_DETUNE), semitones."""
    x = 2 * knob - 1
    sign = -1.0 if x < 0 else 1.0
    x = x * sign * 6
    i = int(x)
    f = max((x - i - PLATEAU) * 1.5, 0.0)
    s = f * f * (3 - 2 * f)
    a, b = INTERVALS[i], INTERVALS[i + 1]
    return (a + (b - a) * s) * sign


def att_law(knob):
    """Controls.hpp attenuverterLaw (cv_reader.h LAW_ATTENUVERTER)."""
    v = knob - 0.5
    return (2 / ATT_GAIN) * v * max(abs(v) - ATT_DEAD, 0.0)


def att_knob_for(amount):
    """The knob position (0..1, clockwise half) whose law gives `amount`."""
    lo, hi = 0.5, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        (lo, hi) = (mid, hi) if att_law(mid) < amount else (lo, mid)
    return (lo + hi) / 2


def plateaus():
    """Pitch's flats: (knob from, knob to, semitones), found by scanning
    the law."""
    n = 24000
    out, start, prev = [], None, None
    for k in range(n + 1):
        v = detune(k / n)
        flat = prev is not None and v == prev and v == round(v)
        if flat and start is None:
            start = (k - 1) / n
        if not flat and start is not None:
            out.append((start, (k - 1) / n, prev))
            start = None
        prev = v
    if start is not None:
        out.append((start, 1.0, prev))
    # The end stops: the law reaches its last interval only there.
    for k in (0.0, 1.0):
        v = detune(k)
        if v == round(v) and not any(a <= k <= b for a, b, _ in out):
            out.append((k, k, v))
    return sorted(out)


def envelope(shape, duration_s, sr=48000):
    """GrainEnvelope::Start and Render, sample by sample: the grain's
    envelope for `shape` over a grain `duration_s` long."""
    duration = duration_s * sr
    min_attack, min_release = MIN_ATTACK_S * sr, MIN_RELEASE_S * sr
    sustain = max(1 - 3 * shape, 0.0)
    sh = (shape - 0.33333333) * 1.5
    sh = 0.0 if sh < 0 else (sh * sh * sh * 4 if sh < 0.5 else sh)
    attack = sh
    release = (1 - attack) * (1 - sustain)
    inc = [1 / max(duration * attack, min_attack * (1 + 3 * sustain)),
           1 / max(duration * sustain, 2.0),
           1 / max(duration * release, min_release)]
    smooth = 2 * attack if attack < 0.5 else 2 - 2 * attack
    stage, phase, out = 0, 0.0, []
    while stage < 3:
        if stage == 1:
            v = 1.0
        else:
            t = phase if stage == 0 else 1 - phase
            base = t if stage == 0 else t ** 4
            v = base + (t * t * (3 - 2 * t) - base) * smooth
        out.append(v)
        phase += inc[stage]
        if phase >= 1:
            t = (phase - 1) / inc[stage]
            stage += 1
            phase = t * inc[stage] if stage < 3 else 0.0
    return out, dict(attack=attack, sustain=sustain, release=release, smooth=smooth)


def shape_for_attack(attack):
    """The Shape (above a third) whose envelope has this attack."""
    lo, hi = 1 / 3, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if envelope_params(mid)["attack"] < attack:
            lo = mid
        else:
            hi = mid
    return (lo + hi) / 2


def envelope_params(shape):
    sh = (shape - 0.33333333) * 1.5
    sh = 0.0 if sh < 0 else (sh * sh * sh * 4 if sh < 0.5 else sh)
    return dict(attack=sh, sustain=max(1 - 3 * shape, 0.0))


# --------------------------------------------------------------------------
# Drawing.

PAL = dict(gp.INSTRUMENT[""])


def mix(a, b, t):
    pa = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    pb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#" + "".join(f"{round(x + (y - x) * t):02x}" for x, y in zip(pa, pb))


# Reverse grains: a cool grey between the unlit light and ivory (the one
# colour the panel's palette does not have; the user approved it on Size).
PAL["reverse"] = mix(PAL["unlit"], PAL["ivory"], 0.4)
SPACING = 0.08


class Fig:
    def __init__(self, w, h):
        self.w, self.h, self.parts = w, h, [f'<rect width="{w}" height="{h}" fill="{PAL["ground"]}"/>']

    def text(self, x, y, s, size, colour="ivory", anchor="middle", bold=False):
        weight = "bold" if bold else "normal"
        gp.text_width(s, size, SPACING, weight)   # fails on a glyph the face lacks
        self.parts.append(gp.text_svg(gp.Text(x, y, s, size, colour, weight, anchor, SPACING, s), PAL))

    def para(self, x, y, s, size, width, colour="dim", lead=1.4):
        """Text wrapped to `width` mm; returns the y under it."""
        line = ""
        for word in s.split():
            trial = (line + " " + word).strip()
            if line and gp.text_width(trial, size, SPACING, "normal") > width:
                self.text(x, y, line, size, colour, "start")
                y += size * lead
                line = word
            else:
                line = trial
        if line:
            self.text(x, y, line, size, colour, "start")
            y += size * lead
        return y

    def line(self, x0, y0, x1, y1, colour, w, cap=""):
        c = f' stroke-linecap="{cap}"' if cap else ""
        self.parts.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" '
                          f'stroke="{PAL[colour]}" stroke-width="{w}"{c}/>')

    def path(self, d, colour, w, fill="none", extra=""):
        f = PAL[fill] if fill != "none" else "none"
        self.parts.append(f'<path d="{d}" fill="{f}" stroke="{PAL[colour]}" stroke-width="{w}"{extra}/>')

    def circle(self, x, y, r, fill, stroke=None, w=0.6):
        s = f' stroke="{PAL[stroke]}" stroke-width="{w}"' if stroke else ""
        self.parts.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}" fill="{PAL[fill]}"{s}/>')

    def title(self, name, sub):
        self.text(8, 10, name, 5, "ivory", "start", True)
        self.text(8, 15.5, sub, 2.4, "dim", "start")

    def svg(self):
        return ('<?xml version="1.0" encoding="UTF-8"?>\n'
                f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}mm" height="{self.h}mm" '
                f'viewBox="0 0 {self.w} {self.h}">\n' + "\n".join(self.parts) + "\n</svg>\n")


class Dial:
    """A knob's travel: 0 at seven o'clock, 1 at five, clockwise (Rack's
    270 degrees)."""

    def __init__(self, fig, cx, cy, r):
        self.f, self.cx, self.cy, self.r = fig, cx, cy, r

    def pt(self, v, r):
        a = math.radians(-135 + 270 * v)
        return self.cx + r * math.sin(a), self.cy - r * math.cos(a)

    def arc(self, v0, v1, colour, w, r=None):
        r = self.r if r is None else r
        x0, y0 = self.pt(v0, r)
        x1, y1 = self.pt(v1, r)
        large = 1 if (v1 - v0) * 270 > 180 else 0
        self.f.path(f"M{x0:.2f} {y0:.2f} A{r} {r} 0 {large} 1 {x1:.2f} {y1:.2f}", colour, w)

    def tick(self, v, r0, r1, colour="ivory", w=0.35):
        x0, y0 = self.pt(v, r0)
        x1, y1 = self.pt(v, r1)
        self.f.line(x0, y0, x1, y1, colour, w)

    def label(self, v, r, s, size=2.5, colour="ivory", bold=False):
        x, y = self.pt(v, r)
        self.f.text(x, y + 0.36 * size, s, size, colour, "middle", bold)

    def knob(self, pointer, inset=6.0):
        self.f.circle(self.cx, self.cy, self.r - inset, "knob", "brass", 0.6)
        x1, y1 = self.pt(pointer, self.r - inset - 3)
        self.f.line(self.cx, self.cy, x1, y1, "brass", 1.0, "round")
        self.f.circle(self.cx, self.cy, 1.2, "brass")


def fmt_s(seconds):
    ms = seconds * 1000
    if ms >= 1000:
        return f"{seconds:.1f} s"
    return f"{ms:.0f} ms"


def fmt_hz(hz):
    if hz >= 10:
        return f"{hz:.0f} Hz"
    if hz >= 1:
        return f"{hz:.1f} Hz"
    return f"{hz:.2f} Hz"


def fig_size():
    f = Fig(150, 112)
    f.title("SIZE", "GRAIN LENGTH AND DIRECTION, AS BEADS")
    d = Dial(f, 75, 60, 30)
    third = REVERSE_BELOW
    d.arc(0.0, third, "reverse", 2.2)
    d.arc(third, DELAY_ENTER, "brass", 2.2)
    d.arc(DELAY_ENTER, 1.0, "signal", 2.2)
    for v in [0.0, 0.1, 0.2, third, 0.5, 0.6, 0.7, 0.8, 0.9]:
        d.tick(v, d.r + 1.6, d.r + 3.6)
        d.label(v, d.r + 7.4, fmt_s(grain_seconds(v)))
    d.knob(third)
    f.text(d.cx, d.cy + 9, "POINTER AT ONE THIRD", 2.2, "dim")
    f.text(d.cx, d.cy + 12, "(ABOUT 10 O'CLOCK)", 2.2, "dim")
    lx, ly = d.pt(0.12, d.r + 13)
    f.text(lx - 6, ly + 2, "REVERSE", 3.0, "reverse", "end", True)
    f.text(lx - 6, ly + 5.6, "grains play backwards;", 2.3, "dim", "end")
    f.text(lx - 6, ly + 8.6, "longer towards the end stop", 2.3, "dim", "end")
    fx, fy = d.pt(0.72, d.r + 13)
    # The travel over which the length doubles, from the formula's slope.
    doubling = 12 / (SHORT_SEMIS * SHORT_GAIN * 1.5)
    f.text(fx + 6, fy - 4, "FORWARD", 3.0, "brass", "start", True)
    f.text(fx + 6, fy - 0.4, "grains lengthen as it turns;", 2.3, "dim", "start")
    f.text(fx + 6, fy + 2.6, f"length doubles about every {round(1 / doubling)}th", 2.3, "dim", "start")
    f.text(fx + 6, fy + 5.6, "of the travel", 2.3, "dim", "start")
    dx, dy = d.pt(0.995, d.r + 2)
    ex, ey = d.cx + 44, d.cy + 34
    f.path(f"M{dx:.2f} {dy:.2f} L{ex - 14:.2f} {ey - 3:.2f} L{ex + 10:.2f} {ey - 3:.2f}", "signal", 0.3)
    f.text(ex - 14, ey + 1, "DELAY MODE", 3.0, "signal", "start", True)
    f.text(ex - 14, ey + 4.6, f"past {DELAY_ENTER:g}, back below {DELAY_LEAVE:g};", 2.3, "dim", "start")
    f.text(ex - 14, ey + 7.6, f"crosses over in about {DELAY_TIME:g} s", 2.3, "dim", "start")
    d.tick(third, d.r - 2.2, d.r + 2.2, "ivory", 0.8)
    f.para(8, f.h - 6, "Lengths are at Pitch 0 with nothing randomising Size. The cloud thickens with length: "
           "grains at once = Density's rate x length, up to 32.", 2.2, 140)
    return f


def fig_density():
    f = Fig(150, 118)
    f.title("DENSITY", "HOW OFTEN GRAINS START, AS BEADS")
    d = Dial(f, 75, 60, 30)
    d.arc(0.0, DEAD_LO, "brass", 2.2)
    d.arc(DEAD_LO, DEAD_HI, "ivory", 3.2)
    d.arc(DEAD_HI, 1.0, "signal", 2.2)
    for v in [0.0, 0.1, 0.2, 0.3, 0.4]:
        d.tick(v, d.r + 1.6, d.r + 3.6)
        d.label(v, d.r + 7.6, fmt_hz(regular_hz(v)))
    for v in [0.6, 0.7, 0.8, 0.9, 1.0]:
        d.tick(v, d.r + 1.6, d.r + 3.6)
        d.label(v, d.r + 7.6, fmt_hz(random_avg_hz(v)))
    d.knob(0.65)
    f.text(d.cx, d.cy + 9, "POINTER AT THE DEFAULT", 2.2, "dim")
    f.text(d.cx, d.cy + 12, "(RANDOM, A CLOUD)", 2.2, "dim")
    # The dead band, called out above.
    tx, ty = d.pt(0.5, d.r + 2.5)
    f.line(tx, ty, tx, ty - 3.5, "ivory", 0.3)
    f.text(tx, ty - 8.6, "SEED ONLY", 3.0, "ivory", "middle", True)
    f.text(tx, ty - 5.2, f"{DEAD_LO:g} to {DEAD_HI:g}: no grains of its own", 2.3, "dim")
    lx, ly = d.pt(0.12, d.r + 13)
    f.text(lx - 6, ly + 2, "REGULAR", 3.0, "brass", "end", True)
    f.text(lx - 6, ly + 5.6, "evenly spaced, faster", 2.3, "dim", "end")
    f.text(lx - 6, ly + 8.6, f"to {MAX_HZ:.1f} Hz (C3: a buzz)", 2.3, "dim", "end")
    fx, fy = d.pt(0.88, d.r + 13)
    f.text(fx + 6, fy + 2, "RANDOM", 3.0, "signal", "start", True)
    f.text(fx + 6, fy + 5.6, "random gaps, faster;", 2.3, "dim", "start")
    f.text(fx + 6, fy + 8.6, "figures are averages", 2.3, "dim", "start")
    y = f.h - 13
    first = DIVISIONS[0]
    last = DIVISIONS[-1]
    f.para(8, y, f"Rates with nothing in Density's CV. With Seed latched and a clock in SEED, the left half "
           f"divides the clock (/{first} fully left to /{last} near noon) and the right half lets each tick "
           f"through by chance (never at noon, always from {DEAD_HI + 1 / PROB_SCALE:.3f}).", 2.2, 140)
    return f


def fig_pitch():
    f = Fig(150, 112)
    f.title("PITCH", "TRANSPOSITION IN SEMITONES: THE KNOB CATCHES ON EACH")
    d = Dial(f, 75, 60, 30)
    d.arc(0.0, 1.0, "construction", 1.2)
    for a, b, st in plateaus():
        mid = (a + b) / 2
        if b - a < 0.002:      # the end stops: a point
            d.tick(mid, d.r - 2.0, d.r + 2.0, "brass", 1.2)
        else:
            d.arc(a, b, "brass", 2.6)
        s = "0" if st == 0 else f"{st:+.0f}"
        d.label(mid, d.r + 6.0, s, 2.8, "ivory", True)
    d.knob(0.5)
    f.text(d.cx, d.cy + 9, "NOON: 0", 2.2, "dim")
    f.text(d.cx - 40, d.cy + 30, "DOWN", 3.0, "brass", "middle", True)
    f.text(d.cx + 40, d.cy + 30, "UP", 3.0, "brass", "middle", True)
    f.para(8, f.h - 13, "Brass: the flats where the knob holds 0, 1, 3, 4, 7, 12 or 24 semitones, either way; "
           "between them it glides. V/OCT adds 12 semitones a volt with its attenuverter fully clockwise.", 2.2, 140)
    return f


def fig_attenuverters():
    f = Fig(150, 134)
    f.title("THE ATTENUVERTERS", "TIME, SIZE, SHAPE AND PITCH: TWO CONTROLS IN ONE")
    for cx, head, left, right in (
            (40, "NOTHING PATCHED: RANDOMNESS",
             ["a random amount", "of randomness each", "grain, up to 3x"],
             ["each grain a random", "offset from the knob,", "as much as it says"]),
            (110, "A CV PATCHED",
             ["the CV's positive", "half sets how much", "random is added"],
             ["scales the CV:", "fully clockwise,", "±5 V = full travel"])):
        d = Dial(f, cx, 51, 17)
        f.text(cx, 24, head, 2.8, "ivory", "middle", True)
        lo, hi = 0.5 - ATT_DEAD, 0.5 + ATT_DEAD
        d.arc(0.0, lo, "reverse", 2.0)
        d.arc(lo, hi, "ivory", 3.0)
        d.arc(hi, 1.0, "brass", 2.0)
        d.knob(0.5, 5.0)
        lx, ly = d.pt(0.0, d.r + 3)
        for i, s in enumerate(left):
            f.text(lx + 3, ly + 4 + 3 * i, s, 2.3, "dim", "end")
        rx, ry = d.pt(1.0, d.r + 3)
        for i, s in enumerate(right):
            f.text(rx - 3, ry + 4 + 3 * i, s, 2.3, "dim", "start")
        f.text(cx, d.cy - d.r - 3.0, "NOON: NONE", 2.2, "dim")
    # The law: how much the knob gives along its travel.
    x0, x1, y0, y1 = 30.0, 120.0, 92.0, 118.0
    ym = (y0 + y1) / 2
    f.text(8, 87, "HOW MUCH, ALONG THE TRAVEL", 2.6, "ivory", "start", True)
    f.line(x0, ym, x1, ym, "construction", 0.3)
    f.line(x0, y0, x0, y1, "construction", 0.3)
    for v in (0.0, 0.25, 0.5, 0.75, 1.0):
        x = x0 + (x1 - x0) * v
        f.line(x, y1, x, y1 + 1.2, "dim", 0.3)
        f.text(x, y1 + 4, f"{v * 200 - 100:+.0f} %" if v != 0.5 else "0 %", 2.2, "dim")
    pts = []
    for k in range(201):
        v = k / 200
        pts.append(f"{x0 + (x1 - x0) * v:.2f} {ym - att_law(v) * (y1 - y0) / 2:.2f}")
    f.path("M" + " L".join(pts), "brass", 0.6)
    f.text(x0 - 2, y0 + 1, "full", 2.2, "dim", "end")
    f.text(x0 - 2, y1 + 0.5, "full", 2.2, "dim", "end")
    f.text(x0 - 2, ym + 0.8, "none", 2.2, "dim", "end")
    half = att_knob_for(0.5)
    f.para(x1 + 3, y0 + 2, f"Not straight: half the amount is at ±{half * 200 - 100:.0f} % on the tooltip.",
           2.2, 24)
    f.text(x0 + (x1 - x0) / 2, f.h - 4, "THE TOOLTIP'S PERCENTAGE (THE KNOB'S PLACE)", 2.2, "dim")
    return f


def fig_time():
    f = Fig(150, 112)
    f.title("TIME", "WHERE IN THE BUFFER EACH GRAIN STARTS")
    d = Dial(f, 50, 58, 26)
    d.arc(0.0, 1.0, "brass", 2.2)
    for v in (0.0, 0.25, 0.5, 0.75, 1.0):
        d.tick(v, d.r + 1.6, d.r + 3.6)
        d.label(v, d.r + 7.2, f"{v * DIGITAL_SECONDS:.0f} s")
    d.knob(0.5)
    f.text(d.cx, d.cy + 9, "BEHIND THE RECORD HEAD", 2.2, "dim")
    f.text(d.cx, d.cy + 12, f"(DIGITAL, STEREO: {DIGITAL_SECONDS:.1f} S)", 2.2, "dim")
    lx, ly = d.pt(0.0, d.r + 6)
    f.text(lx - 2, ly + 5, "NEWEST", 3.0, "brass", "end", True)
    f.text(lx - 2, ly + 8.4, "just recorded", 2.3, "dim", "end")
    rx, ry = d.pt(1.0, d.r + 6)
    f.text(rx + 2, ry + 5, "OLDEST", 3.0, "brass", "start", True)
    f.text(rx + 2, ry + 8.4, "about to be", 2.3, "dim", "start")
    f.text(rx + 2, ry + 11.4, "overwritten", 2.3, "dim", "start")
    # The inset: a CV into Time, two ways.
    bx, bw = 96.0, 48.0
    f.text(bx, 24, "A CV INTO TIME", 2.8, "ivory", "start", True)
    adc = TIME_CV_ADC

    def lfo(t, lo, hi):          # a triangle, one cycle per unit of t
        p = t % 1.0
        tri = 2 * p if p < 0.5 else 2 - 2 * p
        return lo + (hi - lo) * tri

    def plot(y0, h, knob, amount, lo, hi, caption, note):
        y1 = y0 + h
        f.line(bx, y0, bx + bw, y0, "construction", 0.3)
        f.line(bx, y1, bx + bw, y1, "construction", 0.3)
        f.text(bx - 1, y0 + 0.8, "old", 2.0, "dim", "end")
        f.text(bx - 1, y1 + 0.8, "new", 2.0, "dim", "end")
        cv, tv = [], []
        for k in range(161):
            t = 2 * k / 160
            volts = lfo(t, lo, hi)
            units = max(-adc, min(adc, volts)) * TIME_CV_SCALE
            value = max(0.0, min(1.0, knob + amount * units))
            x = bx + bw * k / 160
            cv.append(f"{x:.2f} {y1 - (volts - lo) / (hi - lo) * h:.2f}")
            tv.append(f"{x:.2f} {y1 - value * h:.2f}")
        f.path("M" + " L".join(cv), "dim", 0.3, extra=' stroke-dasharray="0.8 0.8"')
        f.path("M" + " L".join(tv), "signal", 0.7)
        f.text(bx, y0 - 4.2, caption, 2.3, "ivory", "start", True)
        f.text(bx, y0 - 1.4, note, 2.1, "dim", "start")

    plot(36, 16, 1.0, att_law(1.0), 0.0, 10.0, "TIME FULLY RIGHT, 0 TO 10 V", "attenuverter +100 %: held at the end")
    half = att_knob_for(0.5)
    plot(66, 16, 0.5, 0.5, -5.0, 5.0, "TIME AT NOON, ±5 V",
         f"attenuverter {half * 200 - 100:+.0f} %: the whole buffer")
    f.text(bx, 88, "cyan: where the grains start", 2.1, "signal", "start")
    f.text(bx, 91, "dashed: the CV", 2.1, "dim", "start")
    f.para(8, f.h - 9, "The knob plus the CV stops at the ends: a unipolar CV on a knob at its end stop has "
           "nowhere to go. Start from noon with a bipolar CV, or from the left end with a unipolar one.",
           2.2, 140)
    return f


def fig_shape():
    f = Fig(150, 112)
    f.title("SHAPE", "EACH GRAIN'S ENVELOPE (GRAIN_ENVELOPE.H)")
    bell = shape_for_attack(0.5)
    swell = shape_for_attack(0.75)
    points = [(0.0, "SQUARE"), (1 / 3, "DECAY"), (bell, "BELL"), (swell, "SWELL"), (1.0, "REVERSE")]
    d = Dial(f, 75, 44, 20)
    d.arc(0.0, 1.0, "brass", 1.6)
    for i, (v, name) in enumerate(points):
        d.tick(v, d.r - 1.8, d.r + 1.8, "ivory", 0.6)
        d.label(v, d.r + 5, str(i + 1), 2.8, "ivory", True)
    d.tick(0.5, d.r + 1.0, d.r + 2.2, "dim", 0.4)
    d.label(0.5, d.r + 4.6, "noon", 2.1, "dim")
    d.knob(0.5, 5.0)
    grain = 0.1
    curves = [envelope(v, grain) for v, _ in points]
    longest = max(len(c) for c, _ in curves)
    bw, bh, gap = 24.0, 16.0, 4.0
    x = (f.w - (5 * bw + 4 * gap)) / 2
    y0 = 76.0
    for i, ((v, name), (c, _)) in enumerate(zip(points, curves)):
        f.path(f"M{x:.2f} {y0 + bh:.2f} L{x + bw:.2f} {y0 + bh:.2f}", "construction", 0.3)
        pts = [f"{x:.2f} {y0 + bh:.2f}"]
        step = max(1, len(c) // 300)
        for k in range(0, len(c), step):
            pts.append(f"{x + bw * k / longest:.2f} {y0 + bh - c[k] * bh:.2f}")
        pts.append(f"{x + bw * (len(c) - 1) / longest:.2f} {y0 + bh:.2f}")
        f.path("M" + " L".join(pts), "signal", 0.6)
        f.text(x + bw / 2, y0 + bh + 4, f"{i + 1}  {name}", 2.5, "ivory", "middle", True)
        f.text(x + bw / 2, y0 + bh + 7, f"Shape {v * 100:.0f} %", 2.2, "dim")
        x += bw + gap
    noon = envelope_params(0.5)
    f.para(8, f.h - 6, f"A {grain * 1000:.0f} ms grain. Beads keeps at least {MIN_ATTACK_S * 1000:g} ms of "
           f"attack and {MIN_RELEASE_S * 1000:g} ms of release. Noon rises in "
           f"{noon['attack'] * grain * 1000:.0f} ms and decays.", 2.2, 140)
    return f


FIGURES = {
    "size-dial": fig_size,
    "density-dial": fig_density,
    "pitch-dial": fig_pitch,
    "attenuverters": fig_attenuverters,
    "time-dial": fig_time,
    "shape-dial": fig_shape,
}


def facts():
    print(f"grain length: Size 0 {grain_seconds(0):.3f} s; 1/3 {grain_seconds(REVERSE_BELOW) * 1000:.2f} ms; "
          f"0.5 {grain_seconds(0.5) * 1000:.1f} ms; {DELAY_ENTER} {grain_seconds(DELAY_ENTER):.3f} s")
    print(f"  doubles every {12 / (SHORT_SEMIS * SHORT_GAIN * 1.5):.4f} of the travel")
    flat = [v / 10000 for v in range(10001) if abs(grain_seconds(v / 10000) - grain_seconds(REVERSE_BELOW)) < 1e-12]
    print(f"  shortest ({grain_seconds(REVERSE_BELOW) * 1000:.2f} ms) from {min(flat):.3f} to {max(flat):.3f}")
    for v in (0, 0.1, 0.2, 0.3, 0.4, 0.48):
        print(f"density {v}: regular {regular_hz(v):.3f} Hz")
    print(f"random mean gap factor {mean_gap_factor():.4f}")
    for v in (0.52, 0.6, 0.65, 0.7, 0.8, 0.9, 1.0):
        print(f"density {v}: random avg {random_avg_hz(v):.3f} Hz")
    for k in range(len(DIVISIONS)):
        print(f"divider /{DIVISIONS[k]} from density {k / (DIV_SCALE * len(DIVISIONS)):.3f}")
    print(f"probability 1 from density {DEAD_HI + 1 / PROB_SCALE:.4f}")
    for a, b, st in plateaus():
        print(f"pitch flat {st:+.0f}: knob {a:.4f}..{b:.4f}")
    for amt in (0.25, 0.5, 0.75, 1.0):
        k = att_knob_for(amt)
        print(f"attenuverter amount {amt}: knob {k:.4f} (tooltip {k * 200 - 100:+.1f} %)")
    for v in (0.55, 0.625, 0.75, 0.875):
        print(f"attenuverter tooltip {v * 200 - 100:+.0f} %: amount {att_law(v):.3f}")
    print(f"Digital stereo buffer {DIGITAL_SECONDS:.2f} s; Time CV {TIME_CV_SCALE} per volt, clipped at "
          f"±{TIME_CV_ADC:g} V")
    for v, name in ((0, "square"), (1 / 6, "half sustain"), (1 / 3, "decay"), (0.5, "noon"),
                    (shape_for_attack(0.5), "bell"), (shape_for_attack(0.75), "swell"), (1, "reverse")):
        _, p = envelope(v, 0.1)
        print(f"shape {v:.4f} ({name}): " + ", ".join(f"{k} {x:.4f}" for k, x in p.items()))


def find_inkscape(given):
    if given:
        return gp.need_inkscape(given)
    found = shutil.which("inkscape")
    if not found:
        app = Path("/Applications/Inkscape.app/Contents/MacOS/inkscape")
        found = str(app) if app.is_file() else "inkscape"
    return gp.need_inkscape(found)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--inkscape", help="the Inkscape to run (default: on PATH, or the macOS app)")
    ap.add_argument("--facts", action="store_true", help="print the numbers, write nothing")
    args = ap.parse_args()
    if args.facts:
        facts()
        return
    inkscape = find_inkscape(args.inkscape)
    env = gp.instrument_env(ROOT)
    gp.check_inkscape_typeface(inkscape, env, ROOT)
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    for name, make in FIGURES.items():
        live = WORK / f"{name}.svg"
        live.write_text(make().svg(), encoding="utf-8")
        svg = OUT / f"{name}.svg"
        gp.text_to_path(inkscape, live, svg, env)
        subprocess.run([inkscape, str(svg), "--export-type=png", f"--export-dpi={DPI}",
                        "--export-background-opacity=1", f"--export-filename={OUT / (name + '.png')}"],
                       env=env, check=True, stderr=subprocess.DEVNULL)
        print(f"wrote {svg.relative_to(ROOT)} and {name}.png")


if __name__ == "__main__":
    main()

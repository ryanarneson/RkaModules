#!/usr/bin/env python3
"""Generate module panels and their C++ layout headers from one description.

For each module in PANELS (folder `dir`) this writes:
  res-src/<dir>/<Name>.svg       light panel, live <text>, hidden `components` layer
  res-src/<dir>/<Name>-dark.svg  dark panel, same geometry
  src/<dir>/<Name>Layout.hpp     component centres in mm, read by the module widget
and, once for the plugin:
  src/RkaTheme.hpp         the palette's colours for widgets drawn in C++
  res/components/*.svg     the plugin's own knobs, port, switch, button cap,
                           light seat and screw, in the palette's colours

With `--inkscape <path>` (as `make panels` runs it) it then has Inkscape
convert text to paths (Rack's SVG renderer cannot draw <text>) and writes
the finished panels to res/<dir>/.

Every panel is drawn in the plugin's one look, "Instrument": brass and
ivory on near-black. See "The Instrument look" below for the keys of a
panel's description, and SPECIMEN for a panel that uses every one of them
(`make specimen` draws it, with the components in place, to build/specimen/).

Usage: python3 tools/gen_panels.py [--inkscape PATH] [--specimen | --preview] [repo dir]
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
from collections import namedtuple
from pathlib import Path
from xml.sax.saxutils import escape

HP = 5.08
PANEL_H = 128.5
# Placeholder colours from https://vcvrack.com/manual/Panel
KIND_COLOUR = {"param": "#ff0000", "input": "#00ff00", "output": "#0000ff", "light": "#ff00ff",
               "widget": "#ffff00"}

# Component sizes: radius in mm of the plugin's own components (src/widgets.hpp:
# HeroKnob, DialKnob, FaceKnob, LimbKnob, Atten, KnurlEncoder, BrassPort,
# Toggle2, CapLatch / CapButton, JewelLight). A knob's radius is to the
# outside of its knurl. The toggle is a 3.6 x 6.4 mm rectangle; its radius
# is the circle round it. Used to keep components clear of each other.
RADII = {"hero": 9.3, "dial": 7.0, "face": 4.8, "limb": 4.3, "atten": 2.8, "enc": 7.25,
         "port": 4.0, "toggle": 3.7, "cap": 3.2, "jewel": 1.0}
TOGGLE_W, TOGGLE_H = 3.6, 6.4
# Clearance a layout must keep between any two components, mm.
CLEARANCE = 0.3


def component_size(spec, name, kind, cname):
    """Which RADII entry a component uses: the panel's `sizes`, else an
    attenuverter for `*_ATT` params, a limb knob for other params, a port,
    a jewel for lights and an encoder for widgets."""
    size = spec.get("sizes", {}).get(cid(kind, cname))
    if size is None:
        size = {"light": "jewel", "widget": "enc", "input": "port", "output": "port"}.get(kind)
    if size is None:
        size = "atten" if cname.endswith("_ATT") else "limb"
    if size not in RADII:
        sys.exit(f"{name}: {cid(kind, cname)} has size {size!r}; the sizes are {', '.join(RADII)}")
    return size


def arc_point(cx, cy, r, deg):
    """The point on the circle of radius r about (cx, cy) at `deg` degrees
    from straight down, positive toward the right."""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy + r * math.cos(a)


def dial_point(cx, cy, r, deg):
    """The point r from a dial's centre at `deg` degrees of turn: 0 straight
    up, positive clockwise, as a knob's pointer moves."""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


# Rack's screws are 15 px (one HP) square, set as Rack's template sets them:
# one grid step in from the left edge, two in from the right, against the
# top or bottom rail. The widget (rka::Screw) is drawn at these centres, from
# the layout header's SCREWS. SCREW_R is the head's radius, for clearances.
SCREW_R = 2.2


def screws_at(hp, *corners):
    """Screw centres in mm, one per corner named: "tl", "tr", "bl", "br"."""
    out = []
    for corner in corners:
        if corner not in ("tl", "tr", "bl", "br"):
            sys.exit(f'a screw goes at a corner: "tl", "tr", "bl" or "br", not {corner!r}')
        x = 1.5 * HP if corner[1] == "l" else (hp - 1.5) * HP
        y = HP / 2 if corner[0] == "t" else PANEL_H - HP / 2
        out.append((round(x, 2), round(y, 2)))
    return out


# Placing a column's control on an arc; module descriptions below use it.
def column_on_arc(cx, cy, r, x):
    """The y at which the vertical line at x meets the circle of radius r
    about (cx, cy), below the centre. ValueError if the column misses it."""
    dx = x - cx
    if abs(dx) > r:
        raise ValueError(f"the column at x = {x} misses the arc of radius {r} about ({cx}, {cy})")
    return cy + math.sqrt(r * r - dx * dx)


# Sway, a pendulum: the pivot top right, the limb (radius 58) carrying the
# five shape knobs, the six attenuverters on the next arc out (radius 68),
# the CV jacks in a row under them. Six columns; FM is column 0 and has no
# knob of its own: its trace runs up to an elbow and on to the Rate dial.
_SW_PIVOT = (44.0, 8.0)
_SW_X = [round(6.98 + 9.4 * i, 2) for i in range(6)]
_SW_COLS = ["FM", "PW", "PHASE", "SKEW", "SMOOTH", "MORPH"]
_SW_LIMB, _SW_ATT = 58, 68


def _sway_on(r, x):
    return round(column_on_arc(*_SW_PIVOT, r, x), 2)


# Fathom, an echo sounder: the origin is PITCH. Angles at the origin from
# straight down, positive toward the right (arc_point). Five columns.
_FA_O = (19.0, 33.0)
_FA_X = [round(6.4 + 9.5 * i, 2) for i in range(5)]
_FA_COLS = ["RES", "DRIFT", "CUTOFF", "DETUNE", "DRIVE"]   # attenuverters and CV jacks
_FA_LIMB_R, _FA_FILTER, _FA_ATT = 26, 47, 58
_FA_LIMB = [("SUB", 108), ("SPREAD", 76), ("DETUNE", 44), ("DRIFT", 12)]
_FA_VOCT = -24
_FA_FILTER_COLS = [("RES", 0), ("CUTOFF", 2), ("DRIVE", 4)]
# The sounding line: a radius at -10 degrees, from the origin to the foot.
_FA_SOUNDING = -10
_FA_SOUNDING_END = tuple(round(v, 2) for v in arc_point(
    *_FA_O, (PANEL_H - _FA_O[1]) / math.cos(math.radians(_FA_SOUNDING)), _FA_SOUNDING))


def _fathom_limb(deg):
    return tuple(round(v, 2) for v in arc_point(*_FA_O, _FA_LIMB_R, deg))


def _fathom_on(r, x):
    return round(column_on_arc(*_FA_O, r, x), 2)


# Shoal, ripples from two sources: the encoders, deliberately not level.
_SH_L, _SH_R = (103.0, 33.0), (126.5, 43.0)
_SH_PUSH = 16.5                                  # each push button from its encoder's centre
_SH_TRIG, _SH_CV, _SH_OUT = 100.0, 111.6, 131.4  # the jack field's columns
_SH_ROWS = [round(80.0 + 10.4 * i, 1) for i in range(4)]
_SH_LIGHT = 5.8                                  # each jack's light, right of a TRIG jack, left of an output


def _shoal_push(encoder, deg):
    """A push button beside its encoder, `deg` from straight below it."""
    return tuple(round(v, 2) for v in arc_point(*encoder, _SH_PUSH, deg))


_SL_O = (6.4, 21.0)                              # Sluice's origin; the rope hangs from it
_SL_ROWS = [33.0, 55.5, 78.0, 100.5]
_SL_OUT_DY = 0.0                                 # an output sits this far below its row

PANELS = {
    "Sway": dict(
        dir="sway",
        hp=12,
        title="SWAY",
        subtitle="POLYPHONIC L.F.O.",
        components=[
            # (kind, name, x mm, y mm, label)
            ("param", "RATE", 21.0, 34.0, "RATE"),
            ("light", "RATE", 21.0, 45.2, None),
            ("param", "SPREAD", 46.5, 23.0, "SPREAD"),
            ("param", "CHANNELS", 52.0, 41.0, "CHANNELS"),
        ]
        + [("param", n, x, _sway_on(_SW_LIMB, x), n) for n, x in zip(_SW_COLS[1:], _SW_X[1:])]
        + [("param", n + "_ATT", x, _sway_on(_SW_ATT, x), None) for n, x in zip(_SW_COLS, _SW_X)]
        + [("input", n, x, 88.0, None) for n, x in zip(_SW_COLS, _SW_X)]
        + [("input", n, x, 102.0, n) for n, x in zip(["CLOCK", "RESET", "HOLD"], _SW_X[:3])]
        + [("output", n, x, 102.0, n) for n, x in zip(["SIN", "TRI", "SAW"], _SW_X[3:])]
        + [("param", "OFFSET", _SW_X[0], 116.0, "BI/UNI"),
           ("param", "ONESHOT", _SW_X[1], 116.0, "1-SHOT")]
        + [("output", n, x, 116.0, n) for n, x in zip(["SQR", "MORPH", "RAND", "EOC"], _SW_X[2:])],
        sizes={"PARAM_RATE": "hero", "PARAM_SPREAD": "face", "PARAM_CHANNELS": "face",
               "PARAM_OFFSET": "toggle", "PARAM_ONESHOT": "cap"},
        origins=[_SW_PIVOT],
        circles=[(*_SW_PIVOT, r) for r in (68, 80, 92, 104, 116)],
        # Faint radii to the first and last knob on the limb.
        radii=[(*_SW_PIVOT, _SW_X[1], _sway_on(_SW_LIMB, _SW_X[1])),
               (*_SW_PIVOT, _SW_X[5], _sway_on(_SW_LIMB, _SW_X[5]))],
        # From off the left edge, past MORPH, to the right edge (as the
        # mockup draws it), so the arc reads behind the knobs.
        limbs=[dict(c=_SW_PIVOT, r=_SW_LIMB, a0=-56, a1=17, step=1, major=5)],
        faces={
            "PARAM_RATE": dict(r=16.4, ticks=50, major=5, numerals=list(range(11))),
            "PARAM_SPREAD": dict(r=7.3, ticks=20, major=5),
            # One tick per channel, 1 to 16; long at 1, 6, 11 and 16.
            "PARAM_CHANNELS": dict(r=7.3, ticks=15, major=5),
        },
        tiles=[dict(port=f"OUTPUT_{n}") for n in ["SIN", "TRI", "SAW", "SQR", "MORPH", "RAND", "EOC"]],
        traces=[["INPUT_FM", "PARAM_FM_ATT", (_SW_X[0], 56.0), "PARAM_RATE"]]
        + [[f"INPUT_{n}", f"PARAM_{n}_ATT", f"PARAM_{n}"] for n in _SW_COLS[1:]],
        label_at={"PARAM_RATE": "face", "PARAM_SPREAD": "below", "PARAM_CHANNELS": "below"},
        label_colour={"PARAM_SPREAD": "dim", "PARAM_CHANNELS": "dim", "PARAM_OFFSET": "dim",
                      "PARAM_ONESHOT": "dim"},
        # 1-SHOT's cap is smaller than BI/UNI's switch: one line for both names.
        label_dy={"PARAM_ONESHOT": -0.5},
        # FM has no knob: its name sits just right of its trace.
        notes=[(_SW_X[0] + 1.6, 61.6, "FM", 2.0, "ivory", "start")],
        preview={"LIGHT_RATE": dict(lit=True)},
        # As the mockup: clear of the nameplate, top left.
        screws=screws_at(12, "tr", "bl", "br"),
    ),
    # Fathom, an echo sounder. PITCH is the transducer, the origin every arc
    # is struck from, and every arc is a ping travelling out from it: the
    # limb (radius 26) carries the voice knobs and V/OCT; the filter sits
    # on the next ping (47), the attenuverters on the one after (58), the
    # CV jacks in a row under them and the outputs on tiles below. A faint
    # sounding line runs down from the origin (unnumbered: depth numerals
    # did not read at 100 %, and the spec drops them first).
    "Fathom": dict(
        dir="fathom",
        hp=10,
        title="FATHOM",
        subtitle="DRONE VOICE",
        components=[
            # (kind, name, x mm, y mm, label)
            ("param", "PITCH", *_FA_O, "PITCH"),
        ]
        + [("param", n, *_fathom_limb(deg), n) for n, deg in _FA_LIMB]
        + [("input", "VOCT", *_fathom_limb(_FA_VOCT), "V/OCT")]
        + [("param", n, _FA_X[i], _fathom_on(_FA_FILTER, _FA_X[i]), n) for n, i in _FA_FILTER_COLS]
        + [("param", n + "_ATT", x, _fathom_on(_FA_ATT, x), None) for n, x in zip(_FA_COLS, _FA_X)]
        + [("input", n + "_CV", x, 101.0, None) for n, x in zip(_FA_COLS, _FA_X)]
        + [("output", "SUBOUT", _FA_X[0], 116.0, "SUB"),
           ("output", "LEFT", _FA_X[3], 116.0, "L"),
           ("output", "RIGHT", _FA_X[4], 116.0, "R")],
        # The smaller main dial: on its 13.0 mm face (the mockup's is 13.4)
        # the hero knob would leave the scale no room.
        sizes={"PARAM_PITCH": "dial", "PARAM_CUTOFF": "face"},
        origins=[_FA_O],
        circles=[(*_FA_O, r) for r in (_FA_FILTER, _FA_ATT, 72, 86, 100)],
        # The sounding line, from the origin to the foot of the panel.
        radii=[(*_FA_O, *_FA_SOUNDING_END)],
        # From the upper right, clear of the nameplate, round through the
        # bottom and off the left edge.
        limbs=[dict(c=_FA_O, r=_FA_LIMB_R, a0=-50, a1=124, step=2, major=5)],
        faces={
            # A tick per semitone over +-24, numbered in octaves.
            "PARAM_PITCH": dict(r=13.0, ticks=48, major=12, numerals=["-2", "-1", "0", "+1", "+2"]),
            # 20 Hz to 20 kHz: a long tick per octave.
            "PARAM_CUTOFF": dict(r=7.3, ticks=20, major=2),
        },
        tiles=[dict(port=f"OUTPUT_{n}") for n in ("SUBOUT", "LEFT", "RIGHT")],
        traces=[[f"INPUT_{n}_CV", f"PARAM_{n}_ATT", f"PARAM_{n}"] for n in ("RES", "CUTOFF", "DRIVE")]
        + [["INPUT_DRIFT_CV", "PARAM_DRIFT_ATT", (_FA_X[1], 68.0), "PARAM_DRIFT"],
           ["INPUT_DETUNE_CV", "PARAM_DETUNE_ATT", (_FA_X[3], 60.0), "PARAM_DETUNE"]],
        # SUB's and SPREAD's names inside the limb: above, they would cross it.
        label_at={"PARAM_PITCH": "face", "PARAM_SUB": "left", "PARAM_SPREAD": "left"},
        # Not in the mockup: as Sway, clear of the nameplate.
        screws=screws_at(10, "tr", "bl"),
    ),
    # Shoal, the Ornament & Crime emulation: ripples from two sources. The
    # two encoders are the sources, not level, and their ripples cross the
    # whole panel; there is no limb. Each encoder's push button sits beside
    # it at the same distance, joined by a trace; UP over DOWN stand apart,
    # on no arc; UI MODE lights its own cap in friendly mode. The left 83 mm
    # is three windows the module draws in (output strip, scope, editor), the
    # scope and editor at 0.65 mm per firmware pixel with pixel rulers. Below
    # the controls, the jack field: TRIG and CV columns, then the outputs,
    # each on a wide tile with its letter and light.
    "Shoal": dict(
        dir="shoal",
        hp=28,
        title="SHOAL",
        subtitle="ORNAMENT & CRIME 1.3.6",
        # In line with the screens' left edge.
        nameplate=(4.0, 11.0),
        components=[
            ("widget", "ENC_L", *_SH_L, "ENCODER L"),
            ("widget", "ENC_R", *_SH_R, "ENCODER R"),
            ("param", "ENC_L_PUSH", *_shoal_push(_SH_L, -35), "PUSH"),
            ("param", "ENC_R_PUSH", *_shoal_push(_SH_R, 15), "PUSH"),
            ("param", "UP", 107.0, 56.0, "UP"),
            ("param", "DOWN", 107.0, 64.6, "DOWN"),
            ("param", "UI_MODE", 121.5, 65.0, "UI MODE"),
        ]
        + [("input", f"TR{i + 1}", _SH_TRIG, y, None) for i, y in enumerate(_SH_ROWS)]
        + [("light", f"TR{i + 1}", round(_SH_TRIG + _SH_LIGHT, 2), y, None) for i, y in enumerate(_SH_ROWS)]
        + [("input", f"CV{i + 1}", _SH_CV, y, None) for i, y in enumerate(_SH_ROWS)]
        + [("light", n, round(_SH_OUT - _SH_LIGHT, 2), y, None) for n, y in zip("ABCD", _SH_ROWS)]
        + [("output", n, _SH_OUT, y, n) for n, y in zip("ABCD", _SH_ROWS)],
        sizes={f"PARAM_{n}": "cap" for n in ("ENC_L_PUSH", "ENC_R_PUSH", "UP", "DOWN", "UI_MODE")},
        circles=[(*src, r) for src in (_SH_L, _SH_R) for r in range(16, 160, 10)],
        # One tick per detent, all the way round; no index, no majors.
        faces={f"WIDGET_ENC_{n}": dict(r=10.4, ticks=24, sweep=360, minor_len=1.3) for n in "LR"},
        tiles=[dict(port=f"OUTPUT_{n}", light=f"LIGHT_{n}", left=11.0, right=5.0, top=4.6, bottom=4.6,
                    name_at=(-8.6, 0.75)) for n in "ABCD"],
        traces=[["PARAM_ENC_L_PUSH", "WIDGET_ENC_L"], ["PARAM_ENC_R_PUSH", "WIDGET_ENC_R"]],
        # name, x, y, w, h in mm; the module draws in exactly these. 128 x 64
        # firmware pixels at 0.65 mm for the scope and the editor. The strip
        # stands clear of the scope, so their bezels stay two bars.
        screens=[("STRIP", 4.0, 19.2, 83.2, 5.0),
                 ("SCOPE", 4.0, 25.5, 83.2, 41.6),
                 ("EDITOR", 4.0, 71.0, 83.2, 41.6)],
        rulers={"SCOPE": dict(px=0.65, sides="lb"),
                "EDITOR": dict(px=0.65, sides="lb", numerals=True)},
        label_at={"PARAM_ENC_L_PUSH": "below", "PARAM_ENC_R_PUSH": "below", "PARAM_UP": "right",
                  "PARAM_DOWN": "right"},
        label_colour={f"PARAM_{n}": "dim" for n in ("ENC_L_PUSH", "ENC_R_PUSH", "UP", "DOWN", "UI_MODE")},
        # Channel letters over the strip's four cells; column heads and row
        # numbers for the jack field.
        notes=[(4.0 + 83.2 * (ch + 0.5) / 4, 17.9, n, 2.0, "dim") for ch, n in enumerate("ABCD")]
        + [(_SH_TRIG, 72.6, "TRIG", 2.0, "ivory"), (_SH_CV, 72.6, "CV", 2.0, "ivory"),
           (_SH_OUT, 72.6, "OUT", 2.0, "ivory")]
        + [(93.2, y + 0.7, str(i + 1), 2.0, "dim") for i, y in enumerate(_SH_ROWS)],
        preview={"PARAM_UI_MODE": dict(lit=True), "LIGHT_TR1": dict(lit=True), "LIGHT_A": dict(lit=True)},
        # Not in the mockup: as Sway, clear of the nameplate.
        screws=screws_at(28, "tr", "bl", "br"),
    ),
    # Sluice, four VCAs. Rings struck from the origin above the first input;
    # a brass rope drops from it through the four signal inputs, which are
    # the only thing the channels share. Each row runs input, Level, CV
    # input, output on a tile; the lamp reads the channel's gain.
    "Sluice": dict(
        dir="sluice",
        hp=10,
        title="SLUICE",
        subtitle="QUAD VCA",
        components=[
            # (kind, name, x mm, y mm, label)
            ("param", "CURVE", 40.0, 12.0, None),
        ]
        + [("param", f"LEVEL_{n}", 19.0, y, "LEVEL" if n == "A" else None) for n, y in zip("ABCD", _SL_ROWS)]
        + [("input", n, 6.4, y, None) for n, y in zip("ABCD", _SL_ROWS)]
        + [("input", f"{n}_CV", 31.5, y, "CV" if n == "A" else None) for n, y in zip("ABCD", _SL_ROWS)]
        + [("output", n, 43.6, y + _SL_OUT_DY, n) for n, y in zip("ABCD", _SL_ROWS)]
        + [("light", n, 37.4, y - 3.4, None) for n, y in zip("ABCD", _SL_ROWS)],
        sizes={"PARAM_CURVE": "toggle"} | {f"PARAM_LEVEL_{n}": "face" for n in "ABCD"},
        origins=[_SL_O],
        circles=[(*_SL_O, r) for r in (30, 50, 70, 90, 110)],
        # 10 divisions; heavier at 0, 5 and 10; no numerals.
        faces={f"PARAM_LEVEL_{n}": dict(r=6.6, ticks=10, major=5, tick_r=4.9, major_len=1.6) for n in "ABCD"},
        tiles=[dict(port=f"OUTPUT_{n}") for n in "ABCD"],
        traces=[[f"INPUT_{n}", f"PARAM_LEVEL_{n}", f"INPUT_{n}_CV", f"OUTPUT_{n}"] for n in "ABCD"],
        ropes=[[_SL_O, "INPUT_A", "INPUT_B", "INPUT_C", "INPUT_D"]],
        notes=[(40.0, 6.9, "EXP", 1.4, "dim"), (40.0, 19.6, "LIN", 1.4, "dim")],
        preview={f"PARAM_LEVEL_{r}": dict(turn=-135) for r in "ABCD"} | {"PARAM_CURVE": dict(frame=1)},
        screws=screws_at(10, "tr", "bl"),
    ),
}

def layout_hpp(name, spec):
    lines = [
        "// Generated by tools/gen_panels.py. Do not edit; change the layout there.",
        "#pragma once",
        "#include <rack.hpp>",
        "",
        f"namespace {name}Layout {{",
    ]
    for kind, cname, x, y, _ in spec["components"]:
        lines.append(f"static const rack::math::Vec {kind.upper()}_{cname}({x}f, {y}f);")
    for sname, x, y, w, h in spec.get("screens", []):
        lines.append(f"static const rack::math::Rect SCREEN_{sname}"
                     f"(rack::math::Vec({x}f, {y}f), rack::math::Vec({w}f, {h}f));")
    if spec.get("screws"):
        lines.append("static const rack::math::Vec SCREWS[] = {"
                     + ", ".join(f"rack::math::Vec({x}f, {y}f)" for x, y in spec["screws"]) + "};")
    lines += ["}", ""]
    return "\n".join(lines)


def theme_hpp():
    """The palette for widgets drawn in code. NAME is the default panel's
    colour, NAME_DARK the one for Rack's "dark panels" preference."""
    lines = [
        "// Generated by tools/gen_panels.py. Do not edit; change INSTRUMENT there.",
        "#pragma once",
        "#include <rack.hpp>",
        "",
        "namespace RkaTheme {",
        "// The Instrument look's palette; _DARK: Rack's dark panels.",
    ]
    for const, key in INSTRUMENT_WIDGET_COLOURS:
        for suffix, tag in (("", ""), ("-dark", "_DARK")):
            hexcol = INSTRUMENT[suffix][key]
            r, g, b = (int(hexcol[i:i + 2], 16) for i in (1, 3, 5))
            lines.append(f"static const NVGcolor {const}{tag} = nvgRGB(0x{r:02x}, 0x{g:02x}, 0x{b:02x});")
    lines += ["}", ""]
    return "\n".join(lines)


def check_clearance(name, spec):
    """Fail if any two components of a panel overlap, by RADII."""
    comps = [(cname, x, y, RADII[component_size(spec, name, kind, cname)]) for kind, cname, x, y, _ in spec["components"]]
    for i, (a, ax, ay, ar) in enumerate(comps):
        for b, bx, by, br in comps[i + 1:]:
            gap = ((ax - bx) ** 2 + (ay - by) ** 2) ** 0.5 - ar - br
            if gap < CLEARANCE:
                sys.exit(f"{name}: {a} and {b} are {gap:.2f} mm apart (need {CLEARANCE})")


def check_screens(name, spec):
    """Fail if a screen leaves the panel, a component comes within CLEARANCE
    of one, or two screens' bezels (with their rulers) come within CLEARANCE
    of each other: side by side they would read as one heavy bar."""
    w = spec["hp"] * HP
    screens = spec.get("screens", [])
    for i, a in enumerate(screens):
        for b in screens[i + 1:]:
            if rect_distance(screen_keepout(spec, a), screen_keepout(spec, b)) < CLEARANCE:
                sys.exit(f"{name}: the bezels of SCREEN_{a[0]} and SCREEN_{b[0]} touch "
                         f"(need {CLEARANCE} mm between them)")
    for sname, sx, sy, sw, sh in screens:
        if sx < 0 or sy < 0 or sx + sw > w or sy + sh > PANEL_H:
            sys.exit(f"{name}: screen SCREEN_{sname} is outside the panel")
        # The bezel and ruler are drawn outside the screen's rectangle.
        x0, y0, x1, y1 = screen_keepout(spec, (sname, sx, sy, sw, sh))
        for kind, cname, x, y, _ in spec["components"]:
            r = RADII[component_size(spec, name, kind, cname)] + CLEARANCE
            # Distance from the centre to the rectangle.
            dx = max(x0 - x, 0.0, x - x1)
            dy = max(y0 - y, 0.0, y - y1)
            if dx * dx + dy * dy < r * r:
                sys.exit(f"{name}: {kind.upper()} {cname} overlaps screen SCREEN_{sname}")


# ---------------------------------------------------------------------------
# The Instrument look
#
# Brass and ivory on near-black; see docs/specs/2026-11-15-instrument-look-
# design.md. Every drawn line is a scale, a centre mark, a construction line,
# a signal trace or a label. A panel is described by:
#
#   title, subtitle   the nameplate, top left; `nameplate` (x, y) moves it
#   mark              the foot mark, if not "R K A . INSTRUMENTS"
#   components        [(kind, name, x, y, label)]: kind is param, input,
#                     output, light or widget; x, y the centre in mm. Elsewhere a
#                     component is named by its id, KIND_NAME, as in the
#                     layout header: "PARAM_RATE", "INPUT_PW", "LIGHT_A"
#   sizes             id -> one of RADII's sizes, where the default is
#                     wrong (param: limb, or atten for *_ATT; input, output:
#                     port; light: jewel; widget: enc)
#   origins           [(x, y)]: a brass crosshair where arcs are struck from
#   circles           [(cx, cy, r)]: construction circles
#   radii             [(x1, y1, x2, y2)]: construction lines
#   limbs             [dict(c=(cx, cy), r=, a0=, a1=, step=, major=)]: a brass
#                     arc from a0 to a1 degrees, ticked on its outer side
#                     every `step` degrees, longer every `major` ticks
#   faces             id -> dict(r=, ticks=, major=, sweep=270, numerals=None):
#                     the enamel dial face under a control; without `major`
#                     every tick is the same (an encoder: sweep=360)
#   tiles             [dict(port=id, name=, light=id, left=, right=, top=,
#                     bottom=, name_at=(dx, dy), name_size=)]: an output's
#                     ivory tile; only `port` is required
#   traces            [[id, ..., id]]: a signal trace from a jack through
#                     the controls on its way to the one it moves; an (x, y)
#                     among the ids is an elbow (at most one)
#   ropes             [[(x, y), id, ..., id]]: a brass rope from an anchor
#                     (usually an origin) through the named components'
#                     centres, top to bottom, drawn under the controls
#   screens           [(name, x, y, w, h)]: a window the module draws in, in
#                     mm, drawn as glass in a brass bezel outside it
#   rulers            screen name -> dict(px=mm per pixel, sides="lb",
#                     numerals=True): pixel rulers outside a screen
#   screws            [(x, y)]: centres of the rack screws the module places
#                     (rka::Screw, from the layout header's SCREWS); usually
#                     screws_at(hp, "tr", ...). Not drawn on the panel; kept clear
#   label_at          id -> "above" (default), "below", "left", "right", or
#                     "face" (in ink at the foot of the control's own face).
#                     An output on a tile is named on the tile instead.
#   label_colour, label_size, label_gap, label_dx, label_dy   id -> value
#   notes             [(x, y, text, size, colour)] or with a sixth, the
#                     anchor ("start", "middle", "end"); colour is "ivory",
#                     "dim", "brass" or "ink"
#   preview           id -> dict(turn=degrees, lit=True, frame=0): how the
#                     component is shown in the preview drawing only
#
# Angles on an arc (limbs, arc_point) are measured at its centre from
# straight down, positive toward the right. Angles on a dial (faces, knob
# turn) are measured from straight up, positive clockwise.
# ---------------------------------------------------------------------------

# The one typeface. No other family is ever written into an Instrument
# panel, and Inkscape is shown only tools/fonts/ when it converts one.
FONT_INSTRUMENT = "D-DIN Condensed"
FONT_DIR = Path(__file__).resolve().parent / "fonts"
FONT_FILES = {"normal": "D-DINCondensed.ttf", "bold": "D-DINCondensed-Bold.ttf"}

# "" is the default panel, "-dark" Rack's "dark panels" preference: the same
# drawing turned down. `signal` (things that light up) is not dimmed.
INSTRUMENT = {
    "": dict(ground="#15171c", construction="#2c3340", brass="#c9a46a", ivory="#efe6d2",
             ink="#1c1a16", dim="#8a8474", knob="#23262d", glass="#05080c", signal="#6fd6e0",
             knob_ring="#343842", brass_shade="#6b5838", cap="#2e323a", hole="#000000",
             unlit="#2b4f55"),
    "-dark": dict(ground="#0d0e12", construction="#1e232c", brass="#9c7f52", ivory="#bab3a3",
                  ink="#161511", dim="#757062", knob="#1a1c22", glass="#030508", signal="#6fd6e0",
                  knob_ring="#282b33", brass_shade="#52432b", cap="#23262c", hole="#000000",
                  unlit="#213c41"),
}
# C++ constant -> palette key (RkaTheme::BRASS, RkaTheme::BRASS_DARK, ...).
INSTRUMENT_WIDGET_COLOURS = (("GROUND", "ground"), ("BRASS", "brass"), ("IVORY", "ivory"),
                             ("INK_ON_IVORY", "ink"), ("DIM", "dim"), ("KNOB", "knob"),
                             ("GLASS", "glass"), ("SIGNAL", "signal"))

FOOT_MARK = "R K A · INSTRUMENTS"
SCREEN_BEZEL = 0.45      # mm of brass round a screen, outside its rectangle
RULER_GAP, RULER_MINOR, RULER_MAJOR = 0.4, 0.6, 1.3   # a screen's pixel ruler: off the bezel, tick lengths
RULER_NUMERAL = 2.0      # mm: as the dim labels; smaller did not read at 100 % in Rack
ROPE_WIDTH = 0.4         # mm; counted in a rope's clearances
TRACE_WIDTH = 0.25       # mm; counted in a trace's clearances
BEND_TOLERANCE = 0.05    # mm off the straight line before a trace counts as bent
ORIGIN_ARM = 1.6         # mm: half the width of an origin's crosshair
LABEL_CLEARANCE = 0.2    # mm between lettering and any component
TILE = dict(left=4.4, right=4.4, top=7.2, bottom=4.6)   # mm from the port's centre


def cid(kind, cname):
    """A component's id, as its constant in the layout header."""
    return f"{kind.upper()}_{cname}"


def num(v):
    """A coordinate for SVG: three decimals at most, no trailing zeros."""
    s = f"{v:.3f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


# --- Geometry helpers (pure; tools/test_gen_panels.py). column_on_arc,
# arc_point and dial_point are above PANELS, whose descriptions use them. ---

def trace_end(start, centre, radius):
    """Where a trace coming from `start`, aimed at a control's centre, stops:
    the point on the control's edge on the line between them. ValueError if
    `start` is not outside the control."""
    dx, dy = centre[0] - start[0], centre[1] - start[1]
    d = math.hypot(dx, dy)
    if d <= radius:
        raise ValueError(f"({start[0]}, {start[1]}) is inside the control at ({centre[0]}, {centre[1]}) "
                         f"that the trace runs to")
    k = (d - radius) / d
    return start[0] + dx * k, start[1] + dy * k


def point_segment_distance(p, a, b):
    """Distance from point p to the segment a-b."""
    vx, vy = b[0] - a[0], b[1] - a[1]
    length2 = vx * vx + vy * vy
    k = 0.0 if length2 == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / length2))
    return math.hypot(p[0] - a[0] - k * vx, p[1] - a[1] - k * vy)


def segment_distance(a, b, c, d):
    """Distance between segments a-b and c-d: 0 if they cross."""
    def side(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    if side(a, b, c) * side(a, b, d) < 0 and side(c, d, a) * side(c, d, b) < 0:
        return 0.0
    return min(point_segment_distance(a, c, d), point_segment_distance(b, c, d),
               point_segment_distance(c, a, b), point_segment_distance(d, a, b))


def point_rect_distance(x, y, rect):
    """Distance from a point to a rectangle (x0, y0, x1, y1): 0 inside."""
    dx = max(rect[0] - x, 0.0, x - rect[2])
    dy = max(rect[1] - y, 0.0, y - rect[3])
    return math.hypot(dx, dy)


def rect_distance(a, b):
    """Distance between two rectangles: 0 if they overlap."""
    dx = max(b[0] - a[2], 0.0, a[0] - b[2])
    dy = max(b[1] - a[3], 0.0, a[1] - b[3])
    return math.hypot(dx, dy)


def segment_rect_distance(a, b, rect):
    """Distance from the segment a-b to a rectangle (x0, y0, x1, y1): 0 if
    it enters or touches it."""
    x0, y0, x1, y1 = rect
    if point_rect_distance(a[0], a[1], rect) == 0 or point_rect_distance(b[0], b[1], rect) == 0:
        return 0.0
    corners = ((x0, y0), (x1, y0), (x1, y1), (x0, y1))
    return min(segment_distance(a, b, corners[i], corners[(i + 1) % 4]) for i in range(4))


def limb_ticks(limb):
    """[(degrees, is_major)] for a limb: a tick at every multiple of `step`
    from a0 to a1; every `major`-th multiple is a long one."""
    step = limb["step"]
    first, last = math.ceil(limb["a0"] / step - 1e-9), math.floor(limb["a1"] / step + 1e-9)
    return [(k * step, k % limb["major"] == 0) for k in range(first, last + 1)]


def face_ticks(face):
    """[(degrees of turn, is_major)] for a dial face: `ticks` equal divisions
    of `sweep` degrees, centred on straight up; every `major`-th is long,
    or none if the face has no `major` (an endless encoder's detents measure
    nothing). A full circle does not repeat its first tick."""
    sweep = face.get("sweep", 270)
    major = face.get("major")
    count = face["ticks"] + (0 if sweep >= 360 else 1)
    return [(-sweep / 2 + sweep * i / face["ticks"], bool(major) and i % major == 0) for i in range(count)]


def limb_segments(limb):
    """A limb as straight strokes: its ticks, and its arc as chords from
    tick to tick. For keeping lettering off it."""
    (cx, cy), r = limb["c"], limb["r"]
    angles = [limb["a0"]] + [deg for deg, _ in limb_ticks(limb)] + [limb["a1"]]
    out = [(arc_point(cx, cy, r, a), arc_point(cx, cy, r, b)) for a, b in zip(angles, angles[1:]) if b > a]
    for deg, major in limb_ticks(limb):
        length = limb.get("major_len", 2.0) if major else limb.get("minor_len", 1.0)
        out.append((arc_point(cx, cy, r, deg), arc_point(cx, cy, r + length, deg)))
    return out


# --- Lettering: metrics read from the typeface itself, so a label's box is
# known without Inkscape. ---

FACES = {}   # weight -> dict(upm, cap, advance)


def read_ttf(path):
    """Units per em, cap height and advance width per character of a
    TrueType file (tables head, hhea, hmtx, OS/2, and cmap format 4)."""
    d = path.read_bytes()
    tables = {}
    for i in range(struct.unpack(">H", d[4:6])[0]):
        tag, _, off, length = struct.unpack(">4sIII", d[12 + 16 * i:28 + 16 * i])
        tables[tag.decode("latin-1")] = d[off:off + length]
    upm = struct.unpack(">H", tables["head"][18:20])[0]
    metrics = struct.unpack(">H", tables["hhea"][34:36])[0]
    widths = [struct.unpack(">H", tables["hmtx"][4 * i:4 * i + 2])[0] for i in range(metrics)]
    os2 = tables["OS/2"]
    cap = struct.unpack(">h", os2[88:90])[0] if struct.unpack(">H", os2[0:2])[0] >= 2 else 0
    cmap = tables["cmap"]
    sub = None
    for i in range(struct.unpack(">H", cmap[2:4])[0]):
        off = struct.unpack(">I", cmap[8 + 8 * i:12 + 8 * i])[0]
        if struct.unpack(">H", cmap[off:off + 2])[0] == 4:
            sub = cmap[off:]
            break
    if sub is None:
        sys.exit(f"{path}: no format 4 character map")
    seg2 = struct.unpack(">H", sub[6:8])[0]
    seg = seg2 // 2
    ends = struct.unpack(f">{seg}H", sub[14:14 + seg2])
    starts = struct.unpack(f">{seg}H", sub[16 + seg2:16 + 2 * seg2])
    deltas = struct.unpack(f">{seg}h", sub[16 + 2 * seg2:16 + 3 * seg2])
    ranges_at = 16 + 3 * seg2
    ranges = struct.unpack(f">{seg}H", sub[ranges_at:ranges_at + seg2])
    advance = {}
    for i in range(seg):
        for c in range(starts[i], min(ends[i], 0xFFFE) + 1):
            if ranges[i] == 0:
                glyph = (c + deltas[i]) & 0xFFFF
            else:
                at = ranges_at + 2 * i + ranges[i] + 2 * (c - starts[i])
                glyph = struct.unpack(">H", sub[at:at + 2])[0]
                if glyph:
                    glyph = (glyph + deltas[i]) & 0xFFFF
            if glyph:
                advance[chr(c)] = widths[min(glyph, metrics - 1)]
    return dict(upm=upm, cap=(cap if cap > 0 else 0.7 * upm) / upm, advance=advance)


def typeface(weight):
    if weight not in FACES:
        path = FONT_DIR / FONT_FILES[weight]
        if not path.is_file():
            sys.exit(f"typeface missing: {path}. Instrument panels are lettered in {FONT_INSTRUMENT} only "
                     f"(tools/fonts/README.md); there is no fallback.")
        FACES[weight] = read_ttf(path)
    return FACES[weight]


def text_width(s, size, spacing, weight):
    """Width in mm of s at `size` mm, `spacing` mm between letters."""
    face = typeface(weight)
    for ch in s:
        if ch not in face["advance"]:
            sys.exit(f"{FONT_INSTRUMENT} ({weight}) has no glyph for {ch!r} in {s!r}")
    return sum(face["advance"][ch] for ch in s) * size / face["upm"] + spacing * max(len(s) - 1, 0)


def cap_height(size, weight="normal"):
    return typeface(weight)["cap"] * size


# Sizes below are about a sixth larger than the mockups': D-DIN Condensed is
# that much narrower than their stand-in face, so the lettering fills the
# same width and reads better at rack zoom.
# One piece of lettering: baseline (x, y), anchored "start", "middle" or
# "end"; `what` describes it in a failed check.
Text = namedtuple("Text", "x y s size colour weight anchor spacing what")


def text_box(t):
    """(x0, y0, x1, y1) of a Text's capitals."""
    w = text_width(t.s, t.size, t.spacing, t.weight)
    x0 = t.x - {"start": 0.0, "middle": w / 2, "end": w}[t.anchor]
    return x0, t.y - cap_height(t.size, t.weight), x0 + w, t.y


def text_svg(t, pal):
    # SVG letterspacing also follows the last letter; shift so that the
    # letters themselves sit where the anchor says.
    x = t.x + {"start": 0.0, "middle": t.spacing / 2, "end": t.spacing}[t.anchor]
    return (f'<text x="{x:.2f}" y="{t.y:.2f}" font-family="{FONT_INSTRUMENT}" font-size="{num(t.size)}" '
            f'font-weight="{t.weight}" letter-spacing="{num(t.spacing)}" text-anchor="{t.anchor}" '
            f'fill="{pal[t.colour]}">{escape(t.s)}</text>')


# --- The layout model ---

Comp = namedtuple("Comp", "id kind name x y size r label")


def comps_of(name, spec):
    """A panel's components, in order."""
    out, seen = [], set()
    for kind, cname, x, y, label in spec["components"]:
        key = cid(kind, cname)
        if key in seen:
            sys.exit(f"{name}: two components are called {key}")
        seen.add(key)
        size = component_size(spec, name, kind, cname)
        out.append(Comp(key, kind, cname, x, y, size, RADII[size], label))
    return out


def face_bezel(face):
    """Width of a face's brass bezel; the stroke is centred on its radius."""
    return max(0.45, min(0.7, 0.045 * face["r"]))


def face_edge(face):
    return face["r"] + face_bezel(face) / 2


def edge_radius(spec, c):
    """How far a control reaches for a trace or the foot mark: to the bezel
    of its dial face if it has one."""
    face = spec.get("faces", {}).get(c.id)
    return face_edge(face) if face else c.r


def face_tick_radius(face, c):
    """Where a face's graduations start: just outside its knob."""
    return face.get("tick_r", c.r + 0.6)


def tile_rect(tile, c):
    e = dict(TILE, **{k: tile[k] for k in TILE if k in tile})
    return c.x - e["left"], c.y - e["top"], c.x + e["right"], c.y + e["bottom"]


def screen_keepout(spec, screen):
    """The rectangle a screen takes on the panel: its glass, its bezel and,
    on the sides that have one, its ruler's ticks."""
    sname, x, y, w, h = screen
    x0, y0, x1, y1 = x - SCREEN_BEZEL, y - SCREEN_BEZEL, x + w + SCREEN_BEZEL, y + h + SCREEN_BEZEL
    ruler = spec.get("rulers", {}).get(sname)
    if ruler:
        if "l" in ruler.get("sides", "lb"):
            x0 -= RULER_GAP + RULER_MAJOR
        if "b" in ruler.get("sides", "lb"):
            y1 += RULER_GAP + RULER_MAJOR
    return x0, y0, x1, y1


def on_face_or_tile(spec, by, c):
    """Whether a component sits on ivory (a dial face or a tile) rather than
    on the ground."""
    for fid, face in spec.get("faces", {}).items():
        f = by[fid]
        if math.hypot(c.x - f.x, c.y - f.y) + c.r <= face["r"]:
            return True
    for tile in spec.get("tiles", []):
        if point_rect_distance(c.x, c.y, tile_rect(tile, by[tile["port"]])) == 0:
            return True
    return False


def trace_name(trace):
    ids = [n for n in trace if isinstance(n, str)]
    return f"trace {ids[0]} > {ids[-1]}" if ids else "trace"


def trace_centres(name, spec, trace):
    """A trace's polyline through the centres of its components and its
    elbow, after checking its shape: it may bend once, at its elbow or under
    a control it passes."""
    by = {c.id: c for c in comps_of(name, spec)}
    label = trace_name(trace)
    for n in trace:
        if isinstance(n, str) and n not in by:
            sys.exit(f"{name}: {label} names {n}, which is not a component")
    if len(trace) < 2 or not isinstance(trace[0], str) or not isinstance(trace[-1], str):
        sys.exit(f"{name}: {label} must start at a component and end at one")
    centres = [(by[n].x, by[n].y) if isinstance(n, str) else (float(n[0]), float(n[1])) for n in trace]
    # A bend hidden under a control it passes is an elbow like any other.
    bends = [str(trace[i]) for i in range(1, len(centres) - 1)
             if point_segment_distance(centres[i], centres[i - 1], centres[i + 1]) > BEND_TOLERANCE]
    if len(bends) > 1:
        sys.exit(f"{name}: {label} has more than one elbow: it bends at {' and at '.join(bends)}")
    return centres


def trace_points(name, spec, trace):
    """The drawn polyline of a trace: from the edge of its first component,
    through its elbow and the centres of the controls it passes under, to
    the edge of the control it moves, arriving aimed at that control's
    centre."""
    by = {c.id: c for c in comps_of(name, spec)}
    centres = trace_centres(name, spec, trace)
    try:
        start = trace_end(centres[1], centres[0], edge_radius(spec, by[trace[0]]))
        end = trace_end(centres[-2], centres[-1], edge_radius(spec, by[trace[-1]]))
    except ValueError as e:
        sys.exit(f"{name}: {trace_name(trace)}: {e}")
    return [start] + centres[1:-1] + [end]


def foot_mark(spec):
    return Text(spec["hp"] * HP / 2, PANEL_H - 2.9, spec.get("mark", FOOT_MARK), 1.6, "dim", "normal",
                "middle", 0.5, "the foot mark")


LABEL_COLOUR = {"param": "ivory", "widget": "ivory", "input": "dim", "output": "dim", "light": "dim"}


def instrument_texts(name, spec):
    """Every piece of lettering on an Instrument panel except the foot mark."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    faces, tiles = spec.get("faces", {}), {t["port"]: t for t in spec.get("tiles", [])}
    out = []
    nx, ny = spec.get("nameplate", (5.0, 10.0))
    out.append(Text(nx, ny, spec["title"], 5.2, "ivory", "normal", "start", 1.4, f'title "{spec["title"]}"'))
    if spec.get("subtitle"):
        out.append(Text(nx, ny + 3.8, spec["subtitle"], 1.7, "dim", "normal", "start", 0.45,
                        f'subtitle "{spec["subtitle"]}"'))

    # Dial numerals: upright, centred on a circle outside the graduations.
    for fid, face in faces.items():
        numerals = face.get("numerals")
        if not numerals:
            continue
        c = by[fid]
        majors = [deg for deg, major in face_ticks(face) if major]
        if len(numerals) != len(majors):
            sys.exit(f"{name}: the face of {fid} has {len(majors)} major ticks and {len(numerals)} numerals")
        size = face.get("numeral_size", 1.9)
        r = face.get("numeral_r", face_tick_radius(face, c) + 3.7)
        for deg, s in zip(majors, numerals):
            x, y = dial_point(c.x, c.y, r, deg)
            out.append(Text(x, y + cap_height(size) / 2, str(s), size, "ink", "normal", "middle", 0.0,
                            f'numeral "{s}" on the face of {fid}'))

    # Names of components.
    for c in comps:
        tile = tiles.get(c.id)
        label = tile.get("name", c.label) if tile else c.label
        if not label:
            continue
        what = f'label "{label}"'
        size = spec.get("label_size", {}).get(c.id)
        dx, dy = spec.get("label_dx", {}).get(c.id, 0.0), spec.get("label_dy", {}).get(c.id, 0.0)
        if tile:
            # In ink on the tile: above the port unless the tile says where.
            ax, ay = tile.get("name_at", (0.0, -4.7))
            out.append(Text(c.x + ax + dx, c.y + ay + dy, label, tile.get("name_size", size or 2.0), "ink", "bold",
                            "middle", 0.25, what))
            continue
        at = spec.get("label_at", {}).get(c.id, "above")
        if at == "face":
            if c.id not in faces:
                sys.exit(f'{name}: {c.id} is labelled at "face" but has no face')
            size = size or 2.3
            out.append(Text(c.x + dx, c.y + faces[c.id]["r"] - 2.0 + dy, label, size, "ink", "bold", "middle",
                            0.6, what))
            continue
        size = size or 2.0
        colour = spec.get("label_colour", {}).get(c.id, LABEL_COLOUR[c.kind])
        weight = "bold" if colour == "ink" else "normal"
        reach = edge_radius(spec, c) + spec.get("label_gap", {}).get(c.id, 1.2)
        cap = cap_height(size, weight)
        if at == "above":
            x, y, anchor = c.x, c.y - reach, "middle"
        elif at == "below":
            x, y, anchor = c.x, c.y + reach + cap, "middle"
        elif at == "right":
            x, y, anchor = c.x + reach, c.y + cap / 2, "start"
        elif at == "left":
            x, y, anchor = c.x - reach, c.y + cap / 2, "end"
        else:
            sys.exit(f'{name}: {c.id} is labelled at {at!r}; use "above", "below", "left", "right" or "face"')
        out.append(Text(x + dx, y + dy, label, size, colour, weight, anchor, 0.25, what))

    for note in spec.get("notes", []):
        x, y, s, size, colour = note[:5]
        anchor = note[5] if len(note) > 5 else "middle"
        out.append(Text(x, y, s, size, colour, "bold" if colour == "ink" else "normal", anchor, 0.25,
                        f'note "{s}"'))

    # Numerals under a screen's pixel ruler.
    for sname, sx, sy, sw, sh in spec.get("screens", []):
        ruler = spec.get("rulers", {}).get(sname)
        if ruler and ruler.get("numerals"):
            y = sy + sh + SCREEN_BEZEL + RULER_GAP + RULER_MAJOR + 0.7 + cap_height(RULER_NUMERAL)
            for px in range(0, round(sw / ruler["px"]) + 1, 32):
                out.append(Text(sx + px * ruler["px"], y, str(px), RULER_NUMERAL, "dim", "normal", "middle", 0.0,
                                f'numeral "{px}" under SCREEN_{sname}'))
    return out


# --- Checks. Each fails the build with a message naming the parts. ---

INSTRUMENT_KEYS = {"dir", "hp", "title", "subtitle", "nameplate", "mark", "components", "sizes",
                   "origins", "circles", "radii", "limbs", "faces", "tiles", "traces", "ropes", "screens",
                   "rulers", "label_at", "label_colour", "label_size", "label_gap", "label_dx", "label_dy", "notes",
                   "preview", "screws"}
FACE_KEYS = {"r", "ticks", "major", "sweep", "numerals", "tick_r", "minor_len", "major_len", "numeral_r",
             "numeral_size"}
TILE_KEYS = {"port", "name", "light", "left", "right", "top", "bottom", "name_at", "name_size"}
LIMB_KEYS = {"c", "r", "a0", "a1", "step", "major", "minor_len", "major_len"}
RULER_KEYS = {"px", "sides", "numerals"}
PREVIEW_KEYS = {"turn", "lit", "frame"}


def check_keys(name, what, d, allowed, required=()):
    """A misspelt key would otherwise be ignored in silence."""
    for key in d:
        if key not in allowed:
            sys.exit(f"{name}: {what} has an unknown key {key!r} (it may have: {', '.join(sorted(allowed))})")
    for key in required:
        if key not in d:
            sys.exit(f"{name}: {what} needs {key!r}")


def check_references(name, spec):
    check_keys(name, "the panel", spec, INSTRUMENT_KEYS, ("dir", "hp", "title", "components"))
    ids = {c.id for c in comps_of(name, spec)}
    for key in ("sizes", "faces", "label_at", "label_colour", "label_size", "label_gap", "label_dx", "label_dy",
                "preview"):
        for ref in spec.get(key, {}):
            if ref not in ids:
                sys.exit(f"{name}: `{key}` names {ref}, which is not a component")
    for fid, face in spec.get("faces", {}).items():
        check_keys(name, f"the face of {fid}", face, FACE_KEYS, ("r", "ticks"))
    for limb in spec.get("limbs", []):
        check_keys(name, "a limb", limb, LIMB_KEYS, ("c", "r", "a0", "a1", "step", "major"))
    for ref, state in spec.get("preview", {}).items():
        check_keys(name, f"the preview of {ref}", state, PREVIEW_KEYS)
    screens = {s[0] for s in spec.get("screens", [])}
    for ref, ruler in spec.get("rulers", {}).items():
        if ref not in screens:
            sys.exit(f"{name}: `rulers` names {ref}, which is not a screen")
        check_keys(name, f"the ruler of SCREEN_{ref}", ruler, RULER_KEYS, ("px",))
    for tile in spec.get("tiles", []):
        check_keys(name, f"the tile of {tile.get('port', '?')}", tile, TILE_KEYS, ("port",))
        for key in ("port", "light"):
            if key in tile and tile[key] not in ids:
                sys.exit(f"{name}: a tile names {tile[key]}, which is not a component")
    for colours in (spec.get("label_colour", {}).values(), (n[4] for n in spec.get("notes", []))):
        for colour in colours:
            if colour not in ("ivory", "dim", "brass", "ink"):
                sys.exit(f'{name}: lettering is "ivory", "dim", "brass" or "ink", not {colour!r}')


def check_faces(name, spec):
    """A component is either on a dial face, clear of its rim, or off it,
    clear of its bezel; and faces keep clear of each other, of tiles and of
    screens."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    faces = list(spec.get("faces", {}).items())
    for i, (fid, face) in enumerate(faces):
        f = by[fid]
        longest = max(face.get("minor_len", 0.9), face.get("major_len", 1.7) if face.get("major") else 0.0)
        if face_tick_radius(face, f) + longest > face["r"]:
            sys.exit(f"{name}: the face of {fid} (radius {face['r']}) is too small for its scale")
        for c in comps:
            d = math.hypot(c.x - f.x, c.y - f.y)
            on = d + c.r + CLEARANCE <= face["r"]
            off = d - c.r - CLEARANCE >= face_edge(face)
            if c.id != fid and not on and not off:
                sys.exit(f"{name}: {c.id} straddles the bezel of {fid}'s dial face")
        for oid, other in faces[i + 1:]:
            o = by[oid]
            gap = math.hypot(o.x - f.x, o.y - f.y) - face_edge(face) - face_edge(other)
            if gap < CLEARANCE:
                sys.exit(f"{name}: the dial faces of {fid} and {oid} are {gap:.2f} mm apart (need {CLEARANCE})")
        for tile in spec.get("tiles", []):
            if point_rect_distance(f.x, f.y, tile_rect(tile, by[tile["port"]])) < face_edge(face) + CLEARANCE:
                sys.exit(f"{name}: the dial face of {fid} overlaps the tile of {tile['port']}")
        for screen in spec.get("screens", []):
            if point_rect_distance(f.x, f.y, screen_keepout(spec, screen)) < face_edge(face) + CLEARANCE:
                sys.exit(f"{name}: the dial face of {fid} overlaps SCREEN_{screen[0]}")


def check_tiles(name, spec):
    """A tile holds its port (and its light, if it names one), stays on the
    panel, and keeps clear of every other component, tile and screen."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    w = spec["hp"] * HP
    tiles = [(t, tile_rect(t, by[t["port"]])) for t in spec.get("tiles", [])]
    for i, (tile, rect) in enumerate(tiles):
        port = tile["port"]
        if rect[0] < 0 or rect[1] < 0 or rect[2] > w or rect[3] > PANEL_H:
            sys.exit(f"{name}: the tile of {port} is outside the panel")
        own = {port, tile.get("light")}
        for c in comps:
            if c.id in own:
                inside = (rect[0] <= c.x - c.r and c.x + c.r <= rect[2]
                          and rect[1] <= c.y - c.r and c.y + c.r <= rect[3])
                if not inside:
                    sys.exit(f"{name}: {c.id} does not fit on the tile of {port}")
            elif point_rect_distance(c.x, c.y, rect) < c.r + CLEARANCE:
                sys.exit(f"{name}: {c.id} overlaps the tile of {port}")
        for other, orect in tiles[i + 1:]:
            if rect_distance(rect, orect) < CLEARANCE:
                sys.exit(f"{name}: the tiles of {port} and {other['port']} overlap")
        for screen in spec.get("screens", []):
            if rect_distance(rect, screen_keepout(spec, screen)) < CLEARANCE:
                sys.exit(f"{name}: the tile of {port} overlaps SCREEN_{screen[0]}")


def check_traces(name, spec):
    """A trace, as drawn and counting its width, keeps CLEARANCE from every
    control it does not belong to (to the bezel, for one on a dial face) and
    from every tile whose port it does not belong to; it runs under no
    screen and through no lettering; and no two traces cross or touch."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    texts = instrument_texts(name, spec) + [foot_mark(spec)]
    half = TRACE_WIDTH / 2
    drawn = []
    for trace in spec.get("traces", []):
        label = trace_name(trace)
        own = {n for n in trace if isinstance(n, str)}
        points = trace_points(name, spec, trace)
        for a, b in zip(points, points[1:]):
            for c in comps:
                if c.id in own:
                    continue
                gap = point_segment_distance((c.x, c.y), a, b) - edge_radius(spec, c) - half
                if gap < CLEARANCE:
                    sys.exit(f"{name}: {label} passes {gap:.2f} mm from {c.id}, which it does not belong to "
                             f"(need {CLEARANCE})")
            for tile in spec.get("tiles", []):
                if tile["port"] not in own and \
                        segment_rect_distance(a, b, tile_rect(tile, by[tile["port"]])) - half < CLEARANCE:
                    sys.exit(f"{name}: {label} crosses the tile of {tile['port']}, which it does not belong to")
            for screen in spec.get("screens", []):
                if segment_rect_distance(a, b, screen_keepout(spec, screen)) - half < CLEARANCE:
                    sys.exit(f"{name}: {label} runs under SCREEN_{screen[0]}")
            for t in texts:
                if segment_rect_distance(a, b, text_box(t)) - half < LABEL_CLEARANCE:
                    sys.exit(f"{name}: {label} runs through {t.what}")
        drawn.append((label, points))
    for i, (la, pa) in enumerate(drawn):
        for lb, pb in drawn[i + 1:]:
            for a, b in zip(pa, pa[1:]):
                for c, d in zip(pb, pb[1:]):
                    if segment_distance(a, b, c, d) - 2 * half < CLEARANCE:
                        sys.exit(f"{name}: {la} and {lb} cross or touch")


def check_ropes(name, spec):
    """A rope runs through the components it names, and keeps CLEARANCE, as
    drawn, from every other component, dial face and tile."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    for rope in spec.get("ropes", []):
        own = set(rope[1:])
        for cid_ in rope[1:]:
            if cid_ not in by:
                sys.exit(f"{name}: a rope names {cid_}, which is not a component")
        points = [tuple(rope[0])] + [(by[i].x, by[i].y) for i in rope[1:]]
        for a, b in zip(points, points[1:]):
            for c in comps:
                if c.id in own:
                    continue
                gap = point_segment_distance((c.x, c.y), a, b) - edge_radius(spec, c) - ROPE_WIDTH / 2
                if gap < CLEARANCE:
                    sys.exit(f"{name}: a rope passes {gap:.2f} mm from {c.id}, which it does not run "
                             f"through (need {CLEARANCE})")
            for tile in spec.get("tiles", []):
                if tile["port"] not in own and \
                        segment_rect_distance(a, b, tile_rect(tile, by[tile["port"]])) - ROPE_WIDTH / 2 < CLEARANCE:
                    sys.exit(f"{name}: a rope crosses the tile of {tile['port']}, which it does not run through")


def check_lettering(name, spec):
    """Every piece of lettering, the foot mark included, stays on the panel
    and clear of every component, of every other piece of lettering, of
    screens (with their rulers), limbs and origin marks, and of every dial
    face and tile unless it lies wholly on it. Lettering on ivory is ink;
    off it, it is not."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    w = spec["hp"] * HP
    faces = [(fid, face, by[fid]) for fid, face in spec.get("faces", {}).items()]
    tiles = [(t["port"], tile_rect(t, by[t["port"]])) for t in spec.get("tiles", [])]
    limbs = [seg for limb in spec.get("limbs", []) for seg in limb_segments(limb)]
    boxes = []
    for t in instrument_texts(name, spec) + [foot_mark(spec)]:
        box = text_box(t)
        foot = t.what == "the foot mark"
        clear = CLEARANCE if foot else LABEL_CLEARANCE
        if foot and (box[0] < 1.0 or box[2] > w - 1.0):
            sys.exit(f"{name}: the foot mark does not fit the panel's width; shorten it with `mark`")
        if box[0] < 0 or box[1] < 0 or box[2] > w or box[3] > PANEL_H:
            sys.exit(f"{name}: {t.what} runs off the panel")
        for c in comps:
            if point_rect_distance(c.x, c.y, box) < c.r + clear:
                sys.exit(f"{name}: {t.what} overlaps {c.id}")
        on_ivory = False
        for fid, face, f in faces:
            corners = ((box[0], box[1]), (box[2], box[1]), (box[0], box[3]), (box[2], box[3]))
            if all(math.hypot(x - f.x, y - f.y) <= face["r"] - face_bezel(face) / 2 for x, y in corners):
                on_ivory = True
            elif point_rect_distance(f.x, f.y, box) < face_edge(face) + clear:
                sys.exit(f"{name}: {t.what} overlaps the dial face of {fid}")
        for port, rect in tiles:
            if rect[0] <= box[0] and box[2] <= rect[2] and rect[1] <= box[1] and box[3] <= rect[3]:
                on_ivory = True
            elif rect_distance(box, rect) < clear:
                sys.exit(f"{name}: {t.what} overlaps the tile of {port}")
        if on_ivory and t.colour != "ink":
            sys.exit(f"{name}: {t.what} is {t.colour} on ivory; lettering on a face or tile is ink")
        if not on_ivory and t.colour == "ink":
            sys.exit(f"{name}: {t.what} is ink on the ground; ink is for lettering on a face or tile")
        for screen in spec.get("screens", []):
            if rect_distance(box, screen_keepout(spec, screen)) < clear:
                sys.exit(f"{name}: {t.what} overlaps SCREEN_{screen[0]}")
        for a, b in limbs:
            if segment_rect_distance(a, b, box) < clear:
                sys.exit(f"{name}: {t.what} overlaps the limb")
        for x, y in spec.get("origins", []):
            if point_rect_distance(x, y, box) < ORIGIN_ARM + clear:
                sys.exit(f"{name}: {t.what} overlaps the origin mark at ({x}, {y})")
        for other, obox in boxes:
            if rect_distance(box, obox) < LABEL_CLEARANCE:
                sys.exit(f"{name}: {other.what} and {t.what} overlap")
        boxes.append((t, box))


def check_on_panel(name, spec):
    """Every component, and every dial face to its bezel, lies wholly on the
    panel: Rack would clip the rest."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    w = spec["hp"] * HP

    def off(x, y, r):
        return x - r < 0 or y - r < 0 or x + r > w or y + r > PANEL_H

    for c in comps:
        if off(c.x, c.y, c.r):
            sys.exit(f"{name}: {c.id} at ({c.x}, {c.y}) runs off the panel")
    for fid, face in spec.get("faces", {}).items():
        f = by[fid]
        if off(f.x, f.y, face_edge(face)):
            sys.exit(f"{name}: the dial face of {fid} runs off the panel")


def check_limbs(name, spec):
    """A limb, its arc and its ticks, keeps CLEARANCE from every dial face
    (to its bezel), tile and screen: those are drawn over it and would cut
    the scale."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    for i, limb in enumerate(spec.get("limbs", [])):
        what = "the limb" if len(spec["limbs"]) == 1 else f"limb {i + 1}"
        segments = limb_segments(limb)
        for fid, face in spec.get("faces", {}).items():
            f = by[fid]
            if any(point_segment_distance((f.x, f.y), a, b) < face_edge(face) + CLEARANCE for a, b in segments):
                sys.exit(f"{name}: {what} runs under the dial face of {fid}")
        for tile in spec.get("tiles", []):
            rect = tile_rect(tile, by[tile["port"]])
            if any(segment_rect_distance(a, b, rect) < CLEARANCE for a, b in segments):
                sys.exit(f"{name}: {what} crosses the tile of {tile['port']}")
        for screen in spec.get("screens", []):
            rect = screen_keepout(spec, screen)
            if any(segment_rect_distance(a, b, rect) < CLEARANCE for a, b in segments):
                sys.exit(f"{name}: {what} runs under SCREEN_{screen[0]}")


def check_cv_traced(name, spec):
    """Every CV path is traced (the spec's "Signal traces"). A CV input is
    one with an attenuverter: PARAM_<N>_ATT goes with INPUT_<N> or
    INPUT_<N>_CV, and a trace must start at that jack, pass under the
    attenuverter and go on to the control it moves. An attenuverter with
    neither jack fails, so a renamed jack cannot slip past. Inputs without an
    attenuverter (clock, reset, gate, V/OCT) need no trace."""
    ids = {c.id for c in comps_of(name, spec)}
    traces = [[n for n in t if isinstance(n, str)] for t in spec.get("traces", [])]
    for kind, cname, *_ in spec["components"]:
        if kind != "param" or not cname.endswith("_ATT"):
            continue
        att, base = cid(kind, cname), cname[:-len("_ATT")]
        jacks = [j for j in (f"INPUT_{base}", f"INPUT_{base}_CV") if j in ids]
        if not jacks:
            sys.exit(f"{name}: {att} is an attenuverter with no CV input: there is no INPUT_{base} or "
                     f"INPUT_{base}_CV")
        jack = jacks[0]
        if not any(t[0] == jack and att in t[1:-1] for t in traces):
            sys.exit(f"{name}: {jack} has an attenuverter, {att}, but no trace from the jack through it "
                     f"to the control it moves (every CV path is traced)")


def check_screws(name, spec):
    """A rack screw's head stays on the panel and keeps CLEARANCE from every
    component, dial face, tile, screen, piece of lettering, limb, origin mark
    and trace."""
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    w = spec["hp"] * HP
    texts = instrument_texts(name, spec) + [foot_mark(spec)]
    limbs = [seg for limb in spec.get("limbs", []) for seg in limb_segments(limb)]
    need = SCREW_R + CLEARANCE
    for x, y in spec.get("screws", []):
        what = f"{name}: the screw at ({x}, {y})"
        if x - SCREW_R < 0 or y - SCREW_R < 0 or x + SCREW_R > w or y + SCREW_R > PANEL_H:
            sys.exit(f"{what} is off the panel")
        for c in comps:
            if math.hypot(c.x - x, c.y - y) < c.r + need:
                sys.exit(f"{what} overlaps {c.id}")
        for fid, face in spec.get("faces", {}).items():
            f = by[fid]
            if math.hypot(f.x - x, f.y - y) < face_edge(face) + need:
                sys.exit(f"{what} overlaps the dial face of {fid}")
        for tile in spec.get("tiles", []):
            if point_rect_distance(x, y, tile_rect(tile, by[tile["port"]])) < need:
                sys.exit(f"{what} overlaps the tile of {tile['port']}")
        for screen in spec.get("screens", []):
            if point_rect_distance(x, y, screen_keepout(spec, screen)) < need:
                sys.exit(f"{what} overlaps SCREEN_{screen[0]}")
        for t in texts:
            if point_rect_distance(x, y, text_box(t)) < need:
                sys.exit(f"{what} overlaps {t.what}")
        for a, b in limbs:
            if point_segment_distance((x, y), a, b) < need:
                sys.exit(f"{what} overlaps the limb")
        for ox, oy in spec.get("origins", []):
            if math.hypot(ox - x, oy - y) < ORIGIN_ARM + need:
                sys.exit(f"{what} overlaps the origin mark at ({ox}, {oy})")
        for trace in spec.get("traces", []):
            points = trace_points(name, spec, trace)
            for a, b in zip(points, points[1:]):
                if point_segment_distance((x, y), a, b) - TRACE_WIDTH / 2 < need:
                    sys.exit(f"{what} overlaps {trace_name(trace)}")


def check_panel(name, spec):
    """Every layout check for a panel."""
    check_references(name, spec)
    check_clearance(name, spec)
    check_on_panel(name, spec)
    check_screens(name, spec)
    check_faces(name, spec)
    check_tiles(name, spec)
    check_limbs(name, spec)
    check_traces(name, spec)
    check_ropes(name, spec)
    check_cv_traced(name, spec)
    check_lettering(name, spec)
    check_screws(name, spec)


# --- Drawing the panel ---

def stroke_path(segments, colour, width, extra=""):
    """One <path> of separate straight strokes: nanosvg draws no dashes, so
    every tick of a scale or a knurl is its own segment."""
    d = " ".join(f"M{num(x1)} {num(y1)}L{num(x2)} {num(y2)}" for x1, y1, x2, y2 in segments)
    return f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{num(width)}"{extra}/>'


def limb_svg(limb, pal):
    """A brass arc graduated on its outer side, like a sextant's limb."""
    (cx, cy), r = limb["c"], limb["r"]
    a0, a1 = limb["a0"], limb["a1"]
    if not 0 < a1 - a0 < 360:
        sys.exit(f"a limb runs from a0 to a larger a1, less than a full turn (got {a0} to {a1})")
    x0, y0 = arc_point(cx, cy, r, a0)
    x1, y1 = arc_point(cx, cy, r, a1)
    # Increasing angle runs anticlockwise on screen: SVG sweep flag 0.
    out = [f'<path d="M{num(x0)} {num(y0)}A{num(r)} {num(r)} 0 {1 if a1 - a0 > 180 else 0} 0 {num(x1)} {num(y1)}" '
           f'fill="none" stroke="{pal["brass"]}" stroke-width="0.35"/>']
    for major, length, width in ((False, limb.get("minor_len", 1.0), 0.14), (True, limb.get("major_len", 2.0), 0.25)):
        segs = [arc_point(cx, cy, r, deg) + arc_point(cx, cy, r + length, deg)
                for deg, is_major in limb_ticks(limb) if is_major == major]
        if segs:
            out.append(stroke_path(segs, pal["brass"], width))
    return out


def face_svg(face, c, pal):
    """An enamel dial face: ivory disc, brass bezel, graduations in ink."""
    out = [f'<circle cx="{num(c.x)}" cy="{num(c.y)}" r="{num(face["r"])}" fill="{pal["ivory"]}" '
           f'stroke="{pal["brass"]}" stroke-width="{num(face_bezel(face))}"/>']
    r = face_tick_radius(face, c)
    for major, length, width in ((False, face.get("minor_len", 0.9), 0.16), (True, face.get("major_len", 1.7), 0.28)):
        segs = [dial_point(c.x, c.y, r, deg) + dial_point(c.x, c.y, r + length, deg)
                for deg, is_major in face_ticks(face) if is_major == major]
        if segs:
            out.append(stroke_path(segs, pal["ink"], width))
    return out


def screen_svg(sname, x, y, w, h, ruler, pal):
    """A window of dark glass in a brass bezel; the bezel lies outside the
    rectangle the module draws in. With a ruler: a tick every 8 pixels,
    longer every 32, outside the bezel on the left and/or under it."""
    b = SCREEN_BEZEL
    out = [f'<rect x="{num(x - b / 2)}" y="{num(y - b / 2)}" width="{num(w + b)}" height="{num(h + b)}" '
           f'rx="0.8" fill="{pal["glass"]}" stroke="{pal["brass"]}" stroke-width="{num(b)}"/>']
    if ruler:
        px, off = ruler["px"], b + RULER_GAP
        for major, length, width in ((False, RULER_MINOR, 0.15), (True, RULER_MAJOR, 0.25)):
            segs = []
            if "b" in ruler.get("sides", "lb"):
                segs += [(x + i * px, y + h + off, x + i * px, y + h + off + length)
                         for i in range(0, round(w / px) + 1, 8) if (i % 32 == 0) == major]
            if "l" in ruler.get("sides", "lb"):
                segs += [(x - off, y + i * px, x - off - length, y + i * px)
                         for i in range(0, round(h / px) + 1, 8) if (i % 32 == 0) == major]
            if segs:
                out.append(stroke_path(segs, pal["brass"], width))
    return out


def panel_svg(name, spec, pal):
    w = spec["hp"] * HP
    comps = comps_of(name, spec)
    by = {c.id: c for c in comps}
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        f'width="{w:.2f}mm" height="{PANEL_H}mm" viewBox="0 0 {w:.2f} {PANEL_H}">',
        f'<rect width="{w:.2f}" height="{PANEL_H}" fill="{pal["ground"]}"/>',
    ]
    # Construction: faint geometry behind everything.
    for cx, cy, r in spec.get("circles", []):
        out.append(f'<circle cx="{num(cx)}" cy="{num(cy)}" r="{num(r)}" fill="none" '
                   f'stroke="{pal["construction"]}" stroke-width="0.25"/>')
    if spec.get("radii"):
        out.append(stroke_path(spec["radii"], pal["construction"], 0.25))
    for limb in spec.get("limbs", []):
        out += limb_svg(limb, pal)
    for x, y in spec.get("origins", []):
        out.append(stroke_path([(x - ORIGIN_ARM, y, x + ORIGIN_ARM, y), (x, y - ORIGIN_ARM, x, y + ORIGIN_ARM)],
                               pal["brass"], 0.2))
        out.append(f'<circle cx="{num(x)}" cy="{num(y)}" r="0.8" fill="none" stroke="{pal["brass"]}" '
                   f'stroke-width="0.2"/>')
    # Signal traces, under the controls.
    for trace in spec.get("traces", []):
        d = "L".join(f"{num(x)} {num(y)}" for x, y in trace_points(name, spec, trace))
        out.append(f'<path d="M{d}" fill="none" stroke="{pal["dim"]}" stroke-width="{num(TRACE_WIDTH)}" '
                   f'stroke-linejoin="round"/>')
    # Ropes, brass, over the traces and under the seats.
    for rope in spec.get("ropes", []):
        d = "L".join(f"{num(x)} {num(y)}" for x, y in [tuple(rope[0])] + [(by[i].x, by[i].y) for i in rope[1:]])
        out.append(f'<path d="M{d}" fill="none" stroke="{pal["brass"]}" stroke-width="{num(ROPE_WIDTH)}" '
                   f'stroke-linejoin="round"/>')
    # A seat of bare ground under each control that is not on ivory: it
    # masks the limb, traces and construction lines passing beneath, since a
    # knurled ring is not solid.
    for c in comps:
        if not on_face_or_tile(spec, by, c):
            out.append(f'<circle cx="{num(c.x)}" cy="{num(c.y)}" r="{num(c.r + CLEARANCE)}" fill="{pal["ground"]}"/>')
    for sname, sx, sy, sw, sh in spec.get("screens", []):
        out += screen_svg(sname, sx, sy, sw, sh, spec.get("rulers", {}).get(sname), pal)
    for fid, face in spec.get("faces", {}).items():
        out += face_svg(face, by[fid], pal)
    for tile in spec.get("tiles", []):
        x0, y0, x1, y1 = tile_rect(tile, by[tile["port"]])
        out.append(f'<rect x="{num(x0)}" y="{num(y0)}" width="{num(x1 - x0)}" height="{num(y1 - y0)}" rx="0.7" '
                   f'fill="{pal["ivory"]}"/>')
    for t in instrument_texts(name, spec) + [foot_mark(spec)]:
        out.append(text_svg(t, pal))
    # Placeholders for helper.py createmodule; hidden so they never render.
    out.append('<g inkscape:groupmode="layer" inkscape:label="components" id="components" style="display:none">')
    for c in comps:
        out.append(f'<circle id="{c.kind}-{c.name}" cx="{c.x}" cy="{c.y}" r="1" fill="{KIND_COLOUR[c.kind]}"/>')
    out.append('</g>')
    out.append('</svg>')
    return "\n".join(out) + "\n"


# --- Component artwork (res/components/), drawn by the widgets in
# src/widgets.hpp. A knob is two files: <Name>_bg.svg stays still, <Name>.svg
# turns with the param. ---

# Art name: (size, knurl colour, index colour or None, index width, centre dot).
KNOBS = {
    "HeroKnob": ("hero", "brass", "brass", 0.5, True),
    "DialKnob": ("dial", "brass", "brass", 0.5, True),
    "FaceKnob": ("face", "brass", "brass", 0.5, True),
    "LimbKnob": ("limb", "brass", "brass", 0.5, True),
    "Atten": ("atten", "dim", "ivory", 0.4, False),
    "Encoder": ("enc", "brass", None, 0.0, True),
}
KNURL_PITCH = 0.65   # mm from tick to tick round the knurl
# The encoder turns 1/24 of a turn per detent and has no index, so its knurl
# is all that shows a turn. 55 ticks: each detent shifts the knurl 7/24 of a
# tick (0.23 mm) in the direction turned, and as 55 and 24 share no factor
# the picture does not repeat until a full turn.
KNURL_TICKS = {"Encoder": 55}
# Size -> the artwork a component of that size is drawn with, bottom first;
# of a knob's two layers the second turns.
ART_LAYERS = {
    "hero": ("HeroKnob_bg", "HeroKnob"), "dial": ("DialKnob_bg", "DialKnob"), "face": ("FaceKnob_bg", "FaceKnob"),
    "limb": ("LimbKnob_bg", "LimbKnob"), "atten": ("Atten_bg", "Atten"), "enc": ("Encoder_bg", "Encoder"),
    "port": ("BrassPort",), "toggle": ("Toggle2_1",), "cap": ("Cap",), "jewel": ("JewelSeat",),
}
# Artwork that is the same on dark panels: it is light, not paint.
UNTHEMED_ART = ("Cap_lit",)


def component_art(pal):
    """Art name -> (width mm, height mm, [SVG elements]) in one palette."""
    art = {}

    def disc(c, r, fill):
        return f'<circle cx="{num(c)}" cy="{num(c)}" r="{num(r)}" fill="{fill}"/>'

    for art_name, (size, knurl, index, index_w, dot) in KNOBS.items():
        radius = RADII[size]
        c, d = radius, 2 * radius
        body, ring = radius - 0.7, radius - 0.25    # the knurl is 0.5 mm deep
        art[art_name + "_bg"] = (d, d, [
            disc(c, body, pal["knob"]),
            f'<circle cx="{num(c)}" cy="{num(c)}" r="{num(0.7 * body)}" fill="none" stroke="{pal["knob_ring"]}" '
            f'stroke-width="0.25"/>'])
        count = KNURL_TICKS.get(art_name, round(2 * math.pi * ring / KNURL_PITCH))
        ticks = [dial_point(c, c, ring - 0.25, 360 * i / count) + dial_point(c, c, ring + 0.25, 360 * i / count)
                 for i in range(count)]
        turning = [stroke_path(ticks, pal[knurl], 0.54 * 2 * math.pi * ring / count, ' data-part="knurl"')]
        if index:
            turning.append(f'<path d="M{num(c)} {num(c - 0.35 * body)}L{num(c)} {num(c - 0.92 * body)}" fill="none" '
                           f'stroke="{pal[index]}" stroke-width="{num(index_w)}" stroke-linecap="round"/>')
        if dot:
            turning.append(disc(c, 0.45, pal["brass"]))
        art[art_name] = (d, d, turning)

    r = RADII["port"]
    art["BrassPort"] = (2 * r, 2 * r, [disc(r, r, pal["brass"]), disc(r, 0.6875 * r, pal["brass_shade"]),
                                       disc(r, 0.4 * r, pal["hole"])])

    # Two-position switch: frame 0 with the handle down, frame 1 up.
    for frame, top in ((0, TOGGLE_H - 0.675 - 2.6), (1, 0.675)):
        art[f"Toggle2_{frame}"] = (TOGGLE_W, TOGGLE_H, [
            f'<rect x="0.125" y="0.125" width="{num(TOGGLE_W - 0.25)}" height="{num(TOGGLE_H - 0.25)}" rx="0.5" '
            f'fill="{pal["knob"]}" stroke="{pal["brass"]}" stroke-width="0.25"/>',
            f'<rect x="{num(TOGGLE_W / 2 - 1.125)}" y="{num(top)}" width="2.25" height="2.6" rx="0.3" '
            f'fill="{pal["ivory"]}"/>'])

    # Button: a dark body in a brass rim, and its cap. Cap_lit is drawn over
    # Cap while the button is lit.
    r = RADII["cap"]
    art["Cap"] = (2 * r, 2 * r, [
        f'<circle cx="{num(r)}" cy="{num(r)}" r="{num(r - 0.15)}" fill="{pal["knob"]}" stroke="{pal["brass"]}" '
        f'stroke-width="0.3"/>',
        disc(r, 2.0, pal["cap"])])
    art["Cap_lit"] = (2 * r, 2 * r, [disc(r, 2.0, pal["signal"])])

    # Light: a dark seat holding an unlit lens; JewelLight draws the lit one.
    r = RADII["jewel"]
    art["JewelSeat"] = (2 * r, 2 * r, [disc(r, r, pal["ink"]), disc(r, 0.68, pal["unlit"])])

    # Rack screw, one HP square as Rack's own: a dark head with a brass slot.
    c = HP / 2
    art["Screw"] = (HP, HP, [disc(c, SCREW_R, pal["knob"]),
                             stroke_path([(c - 1.5, c, c + 1.5, c)], pal["brass"], 0.45)])
    return art


def art_svg(w, h, elements):
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{num(w)}mm" height="{num(h)}mm" viewBox="0 0 {num(w)} {num(h)}">\n'
            + "\n".join(elements) + "\n</svg>\n")


def component_files():
    """File name -> SVG for everything in res/components/."""
    files = {}
    for suffix, pal in INSTRUMENT.items():
        for art_name, (w, h, elements) in component_art(pal).items():
            if suffix and art_name in UNTHEMED_ART:
                continue
            files[f"{art_name}{suffix}.svg"] = art_svg(w, h, elements)
    return files


def preview_svg(name, spec, suffix):
    """The panel with its components drawn in place, as Rack will show it:
    for looking at a layout without Rack. `preview` in the description turns
    a knob, lights a cap or flips a switch."""
    pal = INSTRUMENT[suffix]
    art = component_art(pal)
    parts = []
    for c in comps_of(name, spec):
        state = spec.get("preview", {}).get(c.id, {})
        layers = list(ART_LAYERS[c.size])
        if c.size == "toggle":
            layers = [f'Toggle2_{state.get("frame", 1)}']
        if c.size == "cap" and state.get("lit"):
            layers.append("Cap_lit")
        for i, layer in enumerate(layers):
            w, h, elements = art[layer]
            move = f"translate({num(c.x - w / 2)} {num(c.y - h / 2)})"
            if layer in KNOBS and state.get("turn"):
                move += f" rotate({num(state['turn'])} {num(w / 2)} {num(h / 2)})"
            parts.append(f'<g transform="{move}">' + "".join(elements) + "</g>")
        if c.size == "jewel" and state.get("lit"):
            parts.append(f'<circle cx="{num(c.x)}" cy="{num(c.y)}" r="0.68" fill="{pal["signal"]}"/>')
    w, h, elements = art["Screw"]
    for x, y in spec.get("screws", []):
        parts.append(f'<g transform="translate({num(x - w / 2)} {num(y - h / 2)})">' + "".join(elements) + "</g>")
    hidden = '<g inkscape:groupmode="layer" inkscape:label="components"'
    svg = panel_svg(name, spec, pal)
    return svg.replace(hidden, "\n".join(parts) + "\n" + hidden)


# --- The specimen: not a module. One panel using every Instrument
# primitive and every component at each size, to look at (`make specimen`
# writes it to build/specimen/, never to res/). ---

_SP = (52.0, 6.0)                 # the pivot its arcs are struck from
_SP_COL = [8.0, 19.0, 30.0, 41.0]
_SP_NAMES = ["FM", "PW", "PHASE", "SKEW"]


def _on(r, x):
    return round(column_on_arc(_SP[0], _SP[1], r, x), 2)


SPECIMEN = dict(
    dir="specimen",
    hp=20,
    title="SPECIMEN",
    subtitle="EVERY PART OF THE INSTRUMENT LOOK",
    components=[
        ("param", "RATE", 22.0, 36.0, "RATE"),
        ("light", "RATE", 22.0, 47.2, None),
        ("param", "SPREAD", 51.0, 27.0, "SPREAD"),
        ("param", "DIAL", 86.0, 12.0, "DIAL"),
        ("widget", "ENC", 77.0, 36.0, "ENCODER"),
        ("param", "PUSH", 89.5, 47.0, "PUSH"),
        ("param", "OFFSET", 53.0, 53.0, "BI/UNI"),
        ("param", "ONESHOT", 64.0, 55.0, "1-SHOT"),
    ]
    # Three knobs where their columns meet the limb; four attenuverters on
    # the next arc out; their jacks in a row.
    + [("param", n, x, _on(66, x), n) for n, x in zip(_SP_NAMES[1:], _SP_COL[1:])]
    + [("param", n + "_ATT", x, _on(78, x), None) for n, x in zip(_SP_NAMES, _SP_COL)]
    + [("input", n, x, 96.0, None) for n, x in zip(_SP_NAMES, _SP_COL)]
    + [
        ("input", "CLOCK", 8.0, 111.0, "CLOCK"),
        ("input", "RESET", 19.0, 111.0, "RESET"),
        ("output", "SIN", 30.0, 111.0, "SIN"),
        ("output", "TRI", 41.0, 111.0, "TRI"),
        ("input", "TRIG", 62.0, 101.0, "TRIG"),
        ("light", "TRIG", 68.2, 101.0, None),
        ("light", "A", 83.2, 101.0, None),
        ("output", "A", 89.0, 101.0, "A"),
        ("light", "B", 83.2, 112.0, None),
        ("output", "B", 89.0, 112.0, "B"),
    ],
    sizes={"PARAM_RATE": "hero", "PARAM_DIAL": "dial", "PARAM_SPREAD": "face", "PARAM_OFFSET": "toggle",
           "PARAM_ONESHOT": "cap", "PARAM_PUSH": "cap"},
    origins=[_SP],
    circles=[(_SP[0], _SP[1], r) for r in (78, 90, 102, 114)],
    radii=[(_SP[0], _SP[1], _SP_COL[1], _on(66, _SP_COL[1])), (_SP[0], _SP[1], _SP_COL[3], _on(66, _SP_COL[3]))],
    # From off the left edge to the last knob on it.
    limbs=[dict(c=_SP, r=66, a0=-54, a1=-9.6, step=1, major=5)],
    faces={
        "PARAM_RATE": dict(r=16.4, ticks=50, major=5, numerals=list(range(11))),
        "PARAM_SPREAD": dict(r=7.3, ticks=20, major=5),
        "PARAM_DIAL": dict(r=9.6, ticks=24, major=6),   # the smaller main dial
        "WIDGET_ENC": dict(r=10.4, ticks=24, sweep=360, minor_len=1.3),   # one tick per detent
    },
    tiles=[dict(port="OUTPUT_SIN"), dict(port="OUTPUT_TRI")]
    + [dict(port=f"OUTPUT_{n}", light=f"LIGHT_{n}", left=11.0, right=5.0, top=4.6, bottom=4.6,
            name_at=(-8.6, 0.75)) for n in "AB"],
    traces=[
        # Up its column, one elbow, then aimed at the centre of the dial.
        ["INPUT_FM", "PARAM_FM_ATT", (8.0, 58.0), "PARAM_RATE"],
        # A push button joined to its encoder.
        ["PARAM_PUSH", "WIDGET_ENC"],
    ]
    # Straight up a column: jack, attenuverter, knob.
    + [[f"INPUT_{n}", f"PARAM_{n}_ATT", f"PARAM_{n}"] for n in _SP_NAMES[1:]],
    screens=[("SCOPE", 56.0, 66.0, 41.6, 20.8)],
    rulers={"SCOPE": dict(px=0.65, sides="lb", numerals=True)},
    label_at={"PARAM_RATE": "face", "PARAM_DIAL": "left", "PARAM_SPREAD": "below", "PARAM_PUSH": "below",
              "LIGHT_TRIG": "right"},
    label_colour={"PARAM_SPREAD": "dim", "PARAM_PUSH": "dim", "PARAM_OFFSET": "dim", "PARAM_ONESHOT": "dim"},
    notes=[(10.0, 64.0, "FM", 2.0, "ivory", "start"),
           (50.5, 100.0, "IVORY", 2.0, "ivory"), (50.5, 104.0, "DIM", 2.0, "dim"),
           (50.5, 108.0, "BRASS", 2.0, "brass")],
    preview={"PARAM_RATE": dict(turn=60), "PARAM_PW": dict(turn=-135), "PARAM_PHASE": dict(turn=135),
             "PARAM_FM_ATT": dict(turn=-90), "PARAM_ONESHOT": dict(lit=True), "LIGHT_RATE": dict(lit=True),
             "LIGHT_A": dict(lit=True)},
)


# --- Inkscape: text to paths, in the one typeface ---

def instrument_env(root):
    """The environment Inkscape runs in for an Instrument panel: a private
    fontconfig file that lists tools/fonts/ and nothing else."""
    for fname in FONT_FILES.values():
        if not (FONT_DIR / fname).is_file():
            sys.exit(f"typeface missing: {FONT_DIR / fname} (tools/fonts/README.md); there is no fallback.")
    work = root / "build" / "panels"
    work.mkdir(parents=True, exist_ok=True)
    conf = work / "fonts.conf"
    conf.write_text('<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "fonts.dtd">\n<fontconfig>\n'
                    f'  <dir>{escape(str(FONT_DIR))}</dir>\n'
                    f'  <cachedir>{escape(str(work / "fontcache"))}</cachedir>\n</fontconfig>\n', encoding="utf-8")
    return dict(os.environ, FONTCONFIG_FILE=str(conf))


def check_inkscape_typeface(inkscape, env, root):
    """Fail unless Inkscape really sets text in D-DIN Condensed, in both
    weights: the advance of a capital H, measured from two rows of them, must
    be the typeface's own. A substituted font does not pass."""
    rows = [(f"{weight}{count}", weight, count) for weight in FONT_FILES for count in (8, 16)]
    probe = root / "build" / "panels" / "probe.svg"
    probe.write_text('<svg xmlns="http://www.w3.org/2000/svg" width="2000" height="600" viewBox="0 0 2000 600">\n'
                     + "".join(f'<text id="{key}" x="10" y="{120 * (i + 1)}" font-family="{FONT_INSTRUMENT}" '
                               f'font-size="100" font-weight="{weight}">{"H" * count}</text>\n'
                               for i, (key, weight, count) in enumerate(rows)) + '</svg>\n', encoding="utf-8")
    done = subprocess.run([inkscape, "--query-all", str(probe)], env=env, check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, encoding="utf-8")
    widths = {}
    for line in done.stdout.splitlines():
        cells = line.split(",")
        if len(cells) == 5:
            widths[cells[0]] = float(cells[3])
    for weight in FONT_FILES:
        face = typeface(weight)
        want = 100.0 * face["advance"]["H"] / face["upm"]
        try:
            got = (widths[f"{weight}16"] - widths[f"{weight}8"]) / 8
        except KeyError:
            sys.exit(f"could not measure Inkscape's lettering ({inkscape} --query-all)")
        if abs(got - want) > 0.01 * want:
            sys.exit(f"Inkscape is not setting text in {FONT_INSTRUMENT} ({weight}): a capital H advances "
                     f"{got:.2f} where the typeface's is {want:.2f}. Refusing to convert with a substitute.")


# The Inkscape the committed res/ panels were made with. Another version may
# write the outlines differently (tools/test_gen_panels.py reads this one's
# plain-SVG output), so `make panels` says so, but goes on.
INKSCAPE_VERSION = "1.4.4"


def inkscape_version(banner):
    """The version in `inkscape --version`'s output, or None."""
    for line in banner.splitlines():
        words = line.split()
        if len(words) >= 2 and words[0] == "Inkscape" and words[1][:1].isdigit():
            return words[1]
    return None


def inkscape_warning(found):
    """A warning if `found` is not the version the panels were made with."""
    if found == INKSCAPE_VERSION:
        return None
    return (f"warning: the committed panels were made with Inkscape {INKSCAPE_VERSION}, and this is "
            + (f"Inkscape {found}" if found else "an Inkscape whose version I could not tell")
            + "; check `git status` and the offline test after `make panels`")


def report_inkscape(inkscape):
    """Print the Inkscape version found, and warn (not fail) if it is not
    the one the committed panels were made with."""
    done = subprocess.run([inkscape, "--version"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                          encoding="utf-8", check=False)
    found = inkscape_version(done.stdout)
    print(f"using Inkscape {found or '(version unknown)'} at {inkscape}")
    warning = inkscape_warning(found)
    if warning:
        print(warning, file=sys.stderr)


def need_inkscape(inkscape):
    """The Inkscape to run, or one sentence and a failed build."""
    found = shutil.which(inkscape)
    if not found:
        sys.exit(f"Inkscape is needed to turn panel lettering into paths (`make panels`, `make specimen`) and "
                 f"was not found: looked for {inkscape!r}"
                 + ("" if os.sep in inkscape else " on PATH")
                 + ". Install it, or pass its path: make panels INKSCAPE=/path/to/inkscape")
    return found


def text_to_path(inkscape, src, out, env=None):
    out.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([inkscape, str(src), "--export-text-to-path", "--export-plain-svg",
                    f"--export-filename={out}"], env=env, check=True)


def write_preview(root, name, spec, out_dir, inkscape):
    """The panel's two variants as Rack loads them (if Inkscape is given),
    its layout header, and previews with the components in place: SVG, and
    PNG at Rack's 100 % and 300 % zoom."""
    check_panel(name, spec)
    out_dir.mkdir(parents=True, exist_ok=True)
    env = instrument_env(root)
    if inkscape:
        check_inkscape_typeface(inkscape, env, root)
    for suffix, pal in INSTRUMENT.items():
        panel = out_dir / f"{name}{suffix}.svg"
        panel.write_text(panel_svg(name, spec, pal), encoding="utf-8")
        preview = out_dir / f"{name}{suffix}-preview.svg"
        preview.write_text(preview_svg(name, spec, suffix), encoding="utf-8")
        (out_dir / f"{name}Layout.hpp").write_text(layout_hpp(name, spec), encoding="utf-8")
        if inkscape:
            text_to_path(inkscape, panel, out_dir / "paths" / panel.name, env)
            for zoom in (100, 300):
                subprocess.run([inkscape, str(preview), "--export-type=png", f"--export-dpi={0.75 * zoom}",
                                f"--export-filename={out_dir / f'{name}{suffix}-{zoom}.png'}"],
                               env=env, check=True, stderr=subprocess.DEVNULL)
    print(f"drew {name} in {out_dir}")


def main():
    ap = argparse.ArgumentParser(description="Generate RKA's panels, layout headers, colours and components.")
    ap.add_argument("root", nargs="?", default=str(Path(__file__).resolve().parent.parent), help="repo dir")
    ap.add_argument("--inkscape", metavar="PATH", help="also convert text to paths with this Inkscape, into res/")
    ap.add_argument("--specimen", action="store_true", help="only draw the specimen panel, to build/specimen/")
    ap.add_argument("--preview", action="store_true",
                    help="only draw each Instrument module with its components in place, to build/preview/")
    args = ap.parse_args()
    root = Path(args.root)
    if args.inkscape:
        args.inkscape = need_inkscape(args.inkscape)
        report_inkscape(args.inkscape)

    if args.specimen:
        write_preview(root, "Specimen", SPECIMEN, root / "build" / "specimen", args.inkscape)
        return
    if args.preview:
        for name, spec in PANELS.items():
            write_preview(root, name, spec, root / "build" / "preview", args.inkscape)
        return

    (root / "src" / "RkaTheme.hpp").write_text(theme_hpp(), encoding="utf-8")
    components = root / "res" / "components"
    components.mkdir(parents=True, exist_ok=True)
    files = component_files()
    for fname, svg in files.items():
        (components / fname).write_text(svg, encoding="utf-8")
    for old in components.glob("*.svg"):
        if old.name not in files:
            old.unlink()   # artwork no component draws any more
    for name, spec in PANELS.items():
        check_panel(name, spec)
        svg_dir = root / "res-src" / spec["dir"]
        svg_dir.mkdir(parents=True, exist_ok=True)
        for suffix, pal in INSTRUMENT.items():
            (svg_dir / f"{name}{suffix}.svg").write_text(panel_svg(name, spec, pal), encoding="utf-8")
        (root / "src" / spec["dir"] / f"{name}Layout.hpp").write_text(layout_hpp(name, spec), encoding="utf-8")
        print(f"generated {name}")

    if args.inkscape:
        # Inkscape sees tools/fonts/ and nothing else.
        env = instrument_env(root)
        check_inkscape_typeface(args.inkscape, env, root)
        for name, spec in PANELS.items():
            for suffix in INSTRUMENT:
                text_to_path(args.inkscape, root / "res-src" / spec["dir"] / f"{name}{suffix}.svg",
                             root / "res" / spec["dir"] / f"{name}{suffix}.svg", env)


if __name__ == "__main__":
    main()

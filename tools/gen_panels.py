#!/usr/bin/env python3
"""Generate module panels and their C++ layout headers from one description.

For each module in PANELS (folder `dir`) this writes:
  res-src/<dir>/<Name>.svg       light panel, live <text>, hidden `components` layer
  res-src/<dir>/<Name>-dark.svg  dark panel, same geometry
  src/<dir>/<Name>Layout.hpp     component centres in mm, read by the module widget
and, once for the plugin:
  src/RkaTheme.hpp         the palette's accent colours for lights and widgets

`make panels` then runs Inkscape to convert text to paths (Rack's SVG renderer
cannot draw <text>) and writes the finished panels to res/<dir>/.

The look ("Nightglow") is shared by every module: navy panel, slate cards
with thin glowing borders, light labels, and neon accents. Rack's renderer
(nanosvg) has gradients and opacity but no blur, so glows are faked with a
wide, faint stroke under a thin bright one.

Usage: python3 tools/gen_panels.py [repo dir]
"""
import sys
from pathlib import Path

HP = 5.08
PANEL_H = 128.5
FONT = "DejaVu Sans"

# Accents, shared by both themes and exported to C++. Each module picks from
# these by name (e.g. one per output, like colour-coded modulation sources).
ACCENTS = {
    "cyan": "#22e6ef",
    "violet": "#a78bfa",
    "magenta": "#f472b6",
    "amber": "#f5a623",
    "green": "#4ade80",
}

# "" is the default panel; "-dark" is Rack's "dark panels" preference, so it
# is the same look turned down for dim rooms: deeper ground, dimmer ink.
THEMES = {
    "": dict(bg_top="#111c30", bg_bottom="#070b14", card="#0f1828", card_edge="#2a3a55",
             ink="#e6f4ff", label="#9fb3c8", hint="#5f7187", glow_opacity=0.35, accent_opacity=1.0),
    "-dark": dict(bg_top="#0a101c", bg_bottom="#030509", card="#0a0f1a", card_edge="#1c2738",
                  ink="#b9c8d6", label="#7d8ea0", hint="#46566a", glow_opacity=0.22, accent_opacity=0.75),
}

# Placeholder colours from https://vcvrack.com/manual/Panel
KIND_COLOUR = {"param": "#ff0000", "input": "#00ff00", "output": "#0000ff", "light": "#ff00ff"}

# Sway column x positions (FM, PW, PHASE, SKEW, SMOOTH, MORPH): six equal
# columns across the 68.44 mm control cards.
_COL = [9.70, 21.11, 32.52, 43.92, 55.33, 66.74]
_COLS = ["FM", "PW", "PHASE", "SKEW", "SMOOTH", "MORPH"]
_OUTS = ["SIN", "TRI", "SAW", "SQR", "MORPH", "STEP", "RAND"]

PANELS = {
    "Sway": dict(
        dir="sway",
        hp=18,
        title="SWAY",
        components=[
            # (kind, name, x mm, y mm, label)
            ("param", "RATE", 13.5, 28.5, "RATE"),
            ("param", "SPREAD", 30.0, 29.0, "SPREAD"),
            ("param", "CHANNELS", 44.0, 29.0, "CHANNELS"),
            ("param", "OFFSET", 57.0, 29.0, "BI/UNI"),
            ("param", "ONESHOT", 67.5, 29.0, "1-SHOT"),
            ("input", "CLOCK", 15.41, 52.5, "CLOCK"),
            ("input", "RESET", 38.22, 52.5, "RESET"),
            ("input", "HOLD", 61.03, 52.5, "HOLD"),
        ]
        # Modulation columns: knob (FM has none; RATE is its knob),
        # attenuverter, CV input. Headers are notes.
        + [("param", n, x, 75.0, None) for n, x in zip(_COLS[1:], _COL[1:])]
        + [("param", n + "_ATT", x, 88.0, None) for n, x in zip(_COLS, _COL)]
        + [("input", n, x, 101.0, None) for n, x in zip(_COLS, _COL)]
        + [("output", n, 80.94, round(23.5 + 13.9 * i, 2), n) for i, n in enumerate(_OUTS)],
        # Cards behind groups of controls: x, y, w, h in mm, and the accent
        # for a glowing border (None: plain slate edge).
        cards=[
            (4.0, 15.0, 68.44, 24.0, None),
            (4.0, 41.0, 68.44, 18.0, None),
            (4.0, 61.0, 68.44, 52.0, None),
            (74.44, 15.0, 13.0, 98.0, "cyan"),
        ],
        # Label colour per component, by accent name; the rest use `label`.
        # Keyed by name: only the labelled component of a name is affected.
        label_accents={"SIN": "cyan", "TRI": "green", "SAW": "amber", "SQR": "magenta",
                       "MORPH": "violet", "STEP": "amber", "RAND": "green"},
        label_size={"RATE": 2.4, "SPREAD": 2.1, "CHANNELS": 1.9, "OFFSET": 2.1, "ONESHOT": 2.1,
                    "CLOCK": 2.3, "RESET": 2.3, "HOLD": 2.3,
                    **{n: 2.3 for n in _OUTS}, "MORPH": 2.0},
        label_gap={"RATE": 9.7, "SPREAD": 9.2, "CHANNELS": 9.2, "OFFSET": 9.2, "ONESHOT": 9.2,
                   **{n: 5.0 for n in _OUTS}},
        # Free text: (x, y, text, size mm, theme colour key).
        notes=[(x, 66.0, n, 1.9 if len(n) > 5 else 2.2, "ink") for n, x in zip(_COLS, _COL)]
        + [(_COL[0], 75.6, "(RATE)", 1.6, "hint"),
           (38.22, 110.5, "ATTENUVERTER \u25b8 CV IN", 1.5, "hint")],
    ),
}

LABEL_SIZE = 2.6   # mm
TITLE_SIZE = 4.5   # mm
LABEL_GAP = {"param": 7.0, "input": 6.2, "output": 6.2}
BIG_KNOBS = {("Sway", "RATE")}  # labels sit further above larger knobs


def text(x, y, s, size, fill, weight="normal", extra=""):
    return (f'<text x="{x:.2f}" y="{y:.2f}" font-family="{FONT}" font-size="{size}" '
            f'font-weight="{weight}" text-anchor="middle" fill="{fill}"{extra}>{s}</text>')


def glow_text(x, y, s, size, fill, glow, opacity):
    """Bright text over a wide, faint stroke of the same glyphs."""
    halo = f' stroke="{glow}" stroke-width="{size * 0.22:.2f}" stroke-linejoin="round" opacity="{opacity}"'
    return text(x, y, s, size, glow, "bold", halo) + "\n" + text(x, y, s, size, fill, "bold")


def card(x, y, w, h, t, accent):
    out = []
    if accent:
        c = ACCENTS[accent]
        # Halo first, then the card on top so only the outer half shows.
        out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="1.5" fill="none" '
                   f'stroke="{c}" stroke-width="1.2" opacity="{t["glow_opacity"] * 0.6:.2f}"/>')
        edge, edge_opacity = c, t["accent_opacity"]
    else:
        edge, edge_opacity = t["card_edge"], 1.0
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="1.5" fill="{t["card"]}" '
               f'stroke="{edge}" stroke-width="0.3" stroke-opacity="{edge_opacity}"/>')
    return out


def panel_svg(name, spec, t):
    w = spec["hp"] * HP
    cyan, violet = ACCENTS["cyan"], ACCENTS["violet"]
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape" '
        f'width="{w:.2f}mm" height="{PANEL_H}mm" viewBox="0 0 {w:.2f} {PANEL_H}">',
        '<defs>',
        f'<linearGradient id="bg" x1="0" y1="0" x2="0" y2="{PANEL_H}" gradientUnits="userSpaceOnUse">'
        f'<stop offset="0" stop-color="{t["bg_top"]}"/><stop offset="1" stop-color="{t["bg_bottom"]}"/></linearGradient>',
        f'<linearGradient id="rule" x1="3" y1="0" x2="{w - 3:.2f}" y2="0" gradientUnits="userSpaceOnUse">'
        f'<stop offset="0" stop-color="{cyan}"/><stop offset="1" stop-color="{violet}"/></linearGradient>',
        '</defs>',
        f'<rect width="{w:.2f}" height="{PANEL_H}" fill="url(#bg)"/>',
        glow_text(w / 2, 11.5, spec["title"], TITLE_SIZE, t["ink"], cyan, t["glow_opacity"]),
        f'<rect x="3" y="12.9" width="{w - 6:.2f}" height="1.0" rx="0.5" fill="url(#rule)" opacity="{t["glow_opacity"]}"/>',
        f'<rect x="3" y="13.2" width="{w - 6:.2f}" height="0.4" fill="url(#rule)" opacity="{t["accent_opacity"]}"/>',
    ]
    for x, y, cw, ch, accent in spec["cards"]:
        out += card(x, y, cw, ch, t, accent)
    accents = spec.get("label_accents", {})
    sizes = spec.get("label_size", {})
    gaps = spec.get("label_gap", {})
    for kind, cname, x, y, label in spec["components"]:
        if not label:
            continue
        gap = gaps.get(cname, 12.0 if (name, cname) in BIG_KNOBS else LABEL_GAP[kind])
        fill = ACCENTS[accents[cname]] if cname in accents else t["label"]
        extra = f' fill-opacity="{t["accent_opacity"]}"' if cname in accents else ""
        out.append(text(x, y - gap, label, sizes.get(cname, LABEL_SIZE), fill, "bold", extra))
    for x, y, s, size, colour in spec.get("notes", []):
        out.append(text(x, y, s, size, t[colour], "bold"))
    out.append(text(w / 2, PANEL_H - 3.0, "RKA", 3.2, ACCENTS["amber"], "bold",
                    f' fill-opacity="{t["accent_opacity"]}"'))
    # Placeholders for helper.py createmodule; hidden so they never render.
    out.append('<g inkscape:groupmode="layer" inkscape:label="components" id="components" style="display:none">')
    for kind, cname, x, y, _ in spec["components"]:
        # ids must be unique; names repeat across kinds (PW knob, PW input).
        out.append(f'<circle id="{kind}-{cname}" cx="{x}" cy="{y}" r="1" fill="{KIND_COLOUR[kind]}"/>')
    out.append('</g>')
    out.append('</svg>')
    return "\n".join(out) + "\n"


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
    lines += ["}", ""]
    return "\n".join(lines)


def theme_hpp():
    lines = [
        "// Generated by tools/gen_panels.py. Do not edit; change ACCENTS there.",
        "#pragma once",
        "#include <rack.hpp>",
        "",
        "namespace RkaTheme {",
    ]
    for cname, hexcol in ACCENTS.items():
        r, g, b = (int(hexcol[i:i + 2], 16) for i in (1, 3, 5))
        lines.append(f"static const NVGcolor {cname.upper()} = nvgRGB(0x{r:02x}, 0x{g:02x}, 0x{b:02x});")
    lines += ["}", ""]
    return "\n".join(lines)


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent)
    (root / "src" / "RkaTheme.hpp").write_text(theme_hpp())
    for name, spec in PANELS.items():
        svg_dir = root / "res-src" / spec["dir"]
        svg_dir.mkdir(parents=True, exist_ok=True)
        for suffix, theme in THEMES.items():
            (svg_dir / f"{name}{suffix}.svg").write_text(panel_svg(name, spec, theme))
        (root / "src" / spec["dir"] / f"{name}Layout.hpp").write_text(layout_hpp(name, spec))
        print(f"generated {name}")


if __name__ == "__main__":
    main()

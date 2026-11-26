"""Draft panel for Doldrums: a mockup only, not in the repo."""
import sys
from pathlib import Path
ROOT = Path.home() / "ws/plugins/RkaModules"
sys.path.insert(0, str(ROOT / "tools"))
import gen_panels as g

X = g._ED_X   # 16 HP, Eddy's five columns
INF = sys.argv[2] if len(sys.argv) > 2 else "∞"
SPEC = dict(
    dir="doldrums", hp=16, title="DOLDRUMS", subtitle="FREEZE & STRETCH",
    components=[
        ("param", "STRETCH", X[2], 54.0, "STRETCH"), ("param", "STRETCH_ATT", X[2], 78.0, None),
        ("input", "STRETCH_CV", X[2], 90.0, None),
        ("param", "POSITION", X[0], 48.0, "POSITION"), ("param", "POSITION_ATT", X[0], 61.0, None),
        ("input", "POSITION_CV", X[0], 73.0, None),
        ("param", "HARMONICS", X[4], 48.0, "HARMONICS"), ("param", "HARMONICS_ATT", X[4], 61.0, None),
        ("input", "HARMONICS_CV", X[4], 73.0, None),
        ("param", "BLUR", X[1], 74.5, "BLUR"), ("param", "ROOT", X[3], 74.5, "ROOT"),
        ("param", "TILT", X[0], 88.0, "TILT"), ("param", "SHIFT", X[1], 88.0, "SHIFT"),
        ("param", "PARTIALS", X[3], 88.0, "PARTIALS"), ("param", "WIDTH", X[4], 88.0, "WIDTH"),
        ("input", "L", X[0], 102.0, "IN L"), ("input", "R", X[0], 116.0, "IN R"),
        ("param", "FREEZE", X[1], 102.0, "FREEZE"), ("input", "FREEZE", X[1], 116.0, "GATE"),
        ("param", "MIX", X[2], 104.0, "MIX"), ("input", "TRIG", X[2], 116.0, "TRIG"),
        ("input", "VOCT", X[3], 102.0, "V/OCT"), ("output", "RUN", X[3], 116.0, "RUN"),
        ("output", "L", X[4], 102.0, "L"), ("output", "R", X[4], 116.0, "R")],
    sizes={"PARAM_STRETCH": "hero", "PARAM_FREEZE": "cap", "PARAM_MIX": "atten"},
    faces={"PARAM_STRETCH": dict(r=16.0, ticks=36, major=6, words_size=1.7,
                                 words=[(-135, "1"), (-90, "2"), (-45, "4"), (0, "8"), (45, "16"), (90, "32")])},
    tiles=[dict(port="OUTPUT_L"), dict(port="OUTPUT_R"), dict(port="OUTPUT_RUN")],
    traces=[["INPUT_STRETCH_CV", "PARAM_STRETCH_ATT", "PARAM_STRETCH"],
            ["INPUT_POSITION_CV", "PARAM_POSITION_ATT", "PARAM_POSITION"],
            ["INPUT_HARMONICS_CV", "PARAM_HARMONICS_ATT", "PARAM_HARMONICS"]],
    screens=[("TAPE", 5.0, 19.5, 71.28, 14.0)],
    rulers={"TAPE": dict(px=71.28 / 320, sides="b")},
    label_at={"PARAM_STRETCH": "face"},
    # "CV" beside each CV jack: there is no room beneath them.
    notes=[(X[0] + 5.6, 73.6, "CV", 1.6, "dim", "start"), (X[4] - 5.6, 73.6, "CV", 1.6, "dim", "end"),
           (X[2] + 5.6, 90.6, "CV", 1.6, "dim", "start")],
    preview={"PARAM_STRETCH": dict(turn=0), "PARAM_POSITION": dict(turn=135), "PARAM_HARMONICS": dict(turn=-135),
             "PARAM_BLUR": dict(turn=-90), "PARAM_TILT": dict(turn=0), "PARAM_SHIFT": dict(turn=0),
             "PARAM_ROOT": dict(turn=0), "PARAM_PARTIALS": dict(turn=-30), "PARAM_WIDTH": dict(turn=-60),
             "PARAM_MIX": dict(turn=135), "PARAM_STRETCH_ATT": dict(turn=0), "PARAM_POSITION_ATT": dict(turn=0),
             "PARAM_HARMONICS_ATT": dict(turn=0)},
    screws=g.screws_at(16, "tr", "bl"),
)

# The typeface has no infinity sign, so the last mark on Stretch's face is
# drawn: a figure of eight on its side, its long axis along the rim as a
# word's baseline is, in the ink and about the stem weight of the numerals.
_panel = g.panel_svg
def panel(name, spec, pal):
    px, py = g.dial_point(X[2], 54.0, 13.0, 135)
    a, h = 1.4, 0.85
    d = (f"M0,0 C{a/3:.2f},{-h} {a},{-h} {a},0 C{a},{h} {a/3:.2f},{h} 0,0 "
         f"C{-a/3:.2f},{-h} {-a},{-h} {-a},0 C{-a},{h} {-a/3:.2f},{h} 0,0 Z")
    mark = (f'<path d="{d}" transform="translate({px:.2f} {py:.2f}) rotate(135)" fill="none" '
            f'stroke="{pal["ink"]}" stroke-width="0.32" stroke-linejoin="round"/>')
    svg = _panel(name, spec, pal)
    i = svg.rindex("</svg>")
    return svg[:i] + mark + svg[i:]
g.panel_svg = panel

if __name__ == "__main__":
    ink = g.need_inkscape("inkscape") if sys.argv[1:2] == ["png"] else None
    g.write_preview(ROOT, "Doldrums", SPEC, Path(__file__).parent / "out", ink)

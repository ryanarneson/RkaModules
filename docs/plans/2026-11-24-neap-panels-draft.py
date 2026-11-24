# Draft PANELS entries for Neap and Spring, as drawn in the mockups
# docs/specs/2026-11-24-neap-mockup.png and 2026-11-24-spring-mockup.png
# (both pass gen_panels.py's checks). Task 1 of docs/plans/2026-11-24-neap.md
# moves them into tools/gen_panels.py as "Neap" and "Spring" entries
# (module-level helper names prefixed _NP_ / _SP_, as the other panels do)
# and deletes this file. `g` is the gen_panels module.
# ---- Neap (8 HP) ----
CX = [7.6, 20.3, 33.0]
ROWS = [79.0, 92.0, 105.0, 118.0]
grid = [["1", "2", "3"], ["4", "5", "6"], ["7", "8", "ALL"], [None, "TIDE", "HIGH"]]
outs = [(n, CX[c], ROWS[r]) for r, row in enumerate(grid) for c, n in enumerate(row) if n]
PX = [5.0 + i * 4.37 for i in range(8)]
PY = [49.6 if i % 2 == 0 else 54.6 for i in range(8)]
neap = dict(
    dir="neap", hp=8, title="NEAP", subtitle="TIDE PREDICTOR",
    components=[("param", "RATE", 14.4, 33.0, "RATE"),
                ("param", "RATE_ATT", 33.6, 27.0, None),
                ("input", "RATE_CV", 33.6, 39.0, None),
                ("param", "WEIGHT", 8.0, 66.0, "WEIGHT"),
                ("input", "WEIGHT_CV", 20.3, 66.0, None),
                ("param", "MARK", 32.6, 66.0, "MARK"),
                ("input", "RESET", CX[0], ROWS[3], "RESET")]
    + [("output", "C" + n if n.isdigit() else n, x, y, n) for n, x, y in outs]
    + [("light", f"P{i+1}", x, y, None) for i, (x, y) in enumerate(zip(PX, PY))]
    + [("light", "HIGH", CX[2] + 3.1, ROWS[3] - 4.9, None)],
    sizes={"PARAM_RATE": "dial", "PARAM_WEIGHT": "limb", "PARAM_MARK": "limb"},
    faces={"PARAM_RATE": dict(r=12.8, ticks=48, major=8, numerals=list(range(0, 13, 2)))},
    label_at={"PARAM_RATE": "face"},
    tiles=[dict(port=f"OUTPUT_{'C' + n if n.isdigit() else n}", **({"light": "LIGHT_HIGH", "name_at": (-1.3, -4.7)} if n == "HIGH" else {})) for n, _, _ in outs],
    traces=[["INPUT_RATE_CV", "PARAM_RATE_ATT", "PARAM_RATE"], ["INPUT_WEIGHT_CV", "PARAM_WEIGHT"]],
    ropes=[[(0.8, 47.0)] + [f"LIGHT_P{i+1}" for i in range(8)]],
    notes=[(20.3, 71.6, "CV", 1.4, "dim"), (33.6, 44.6, "CV", 1.4, "dim")],
    preview={"PARAM_RATE": dict(turn=0), "PARAM_WEIGHT": dict(turn=-30), "PARAM_MARK": dict(turn=60),
             "LIGHT_HIGH": dict(lit=True)} | {f"LIGHT_P{i}": dict(lit=True) for i in (1, 3, 4, 7)},
    screws=g.screws_at(8, "tr", "bl"),
)

# ---- Spring (6 HP, staggered after øchd's expander) ----
hp = 6; W = hp * g.HP
XL, XR = 16.2, 25.4
Y0, PITCH, GAP = 25.0, 5.7, 1.8
blocks = [("FOLD", ["F1", "F3", "F5", "F7"], ["1", "3", "5", "7"]),
          ("MIN/MAX", ["MIN23", "MAX23", "MIN67", "MAX67"], ["MIN 2-3", "MAX 2-3", "MIN 6-7", "MAX 6-7"]),
          ("TRIG", ["T2", "T4", "T6", "T8"], ["2", "4", "6", "8"]),
          ("STEPS", ["S14", "S58", "SODD", "SEVEN"], ["1-4", "5-8", "ODD", "EVEN"])]
comps, notes, order = [], [], []
k = 0
for b, (head, ids, labels) in enumerate(blocks):
    ys = []
    for i, l in zip(ids, labels):
        y = Y0 + k * PITCH + b * GAP
        comps.append(("output", i, XL if k % 2 == 0 else XR, round(y, 2), l)); order.append("OUTPUT_" + i); ys.append(y); k += 1
    notes.append((6.0, (ys[0] + ys[-1]) / 2 + 0.6, head, 1.6, "ivory"))
spring = dict(
    dir="spring", hp=hp, mark="R K A", title="SPRING", subtitle="EXPANDER",
    components=comps + [("light", "LINK", W - 4.0, 19.5, None)],
    tiles=[dict(port=f"OUTPUT_{c[1]}", top=6.5, bottom=4.5,
                **({"name_size": 1.3} if c[4].startswith("M") else {})) for c in comps],
    notes=notes + [(W - 5.6, 20.1, "LINK", 1.3, "dim", "end")],
    preview={"LIGHT_LINK": dict(lit=True)},
    screws=g.screws_at(hp, "tr", "bl"),
)

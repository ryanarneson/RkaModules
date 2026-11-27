#!/usr/bin/env python3
"""Offline tests of tools/gen_panels.py: the Instrument look's geometry
helpers and layout checks, and that the generator's committed outputs are
what it generates. Plain python3, no packages, no Inkscape.

Usage: python3 tools/test_gen_panels.py
"""
import math
import re
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_panels as g  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

def spec(**kw):
    """A small Instrument panel, 12 HP, with nothing on it."""
    base = dict(dir="test", hp=12, title="TEST", subtitle="UNIT", components=[])
    base.update(kw)
    return base


def failure(test, name, s):
    """The message a layout check fails the build with."""
    with test.assertRaises(SystemExit) as cm:
        g.check_panel(name, s)
    test.assertIsInstance(cm.exception.code, str, "a check must fail with a message, not an exit status")
    return cm.exception.code


class Geometry(unittest.TestCase):
    def test_column_meets_arc_below_its_centre(self):
        # Sway's limb: centre (44, 8), radius 58. Straight below the centre.
        self.assertAlmostEqual(g.column_on_arc(44, 8, 58, 44), 66.0, places=9)
        # A 3-4-5 triangle scaled by 11.6: 34.8 across, 46.4 down.
        self.assertAlmostEqual(g.column_on_arc(44, 8, 58, 9.2), 54.4, places=9)
        self.assertAlmostEqual(g.column_on_arc(44, 8, 58, 78.8), 54.4, places=9)
        # Tangent: the column touches the arc level with the centre.
        self.assertAlmostEqual(g.column_on_arc(44, 8, 58, 102), 8.0, places=9)

    def test_column_that_misses_the_arc_is_an_error(self):
        with self.assertRaises(ValueError):
            g.column_on_arc(44, 8, 58, 102.01)
        with self.assertRaises(ValueError):
            g.column_on_arc(44, 8, 58, -14.5)

    def test_arc_angles_run_from_straight_down_positive_to_the_right(self):
        for deg, want in ((0, (19, 59)), (90, (45, 33)), (-90, (-7, 33)), (180, (19, 7)),
                          (30, (19 + 13, 33 + 26 * math.sqrt(3) / 2))):
            x, y = g.arc_point(19, 33, 26, deg)
            self.assertAlmostEqual(x, want[0], places=9, msg=f"{deg} deg")
            self.assertAlmostEqual(y, want[1], places=9, msg=f"{deg} deg")

    def test_arc_point_and_column_agree(self):
        x, y = g.arc_point(44, 8, 58, -37)
        self.assertAlmostEqual(g.column_on_arc(44, 8, 58, x), y, places=9)

    def test_dial_turns_clockwise_from_the_top(self):
        for deg, want in ((0, (10, 0)), (90, (20, 10)), (-90, (0, 10)), (-135, (10 - 50 ** 0.5, 10 + 50 ** 0.5))):
            x, y = g.dial_point(10, 10, 10, deg)
            self.assertAlmostEqual(x, want[0], places=9, msg=f"{deg} deg")
            self.assertAlmostEqual(y, want[1], places=9, msg=f"{deg} deg")

    def test_trace_ends_on_the_edge_on_the_line_to_the_centre(self):
        # 3-4-5: from the origin toward (30, 40), stopping 10 short.
        x, y = g.trace_end((0, 0), (30, 40), 10)
        self.assertAlmostEqual(x, 24.0, places=9)
        self.assertAlmostEqual(y, 32.0, places=9)
        x, y = g.trace_end((7, 90), (7, 40), 4.3)
        self.assertAlmostEqual(x, 7.0, places=9)
        self.assertAlmostEqual(y, 44.3, places=9)

    def test_trace_starting_inside_the_control_is_an_error(self):
        with self.assertRaises(ValueError):
            g.trace_end((28, 38), (30, 40), 10)

    def test_limb_ticks(self):
        ticks = g.limb_ticks(dict(c=(0, 0), r=10, a0=-3, a1=10, step=2, major=5))
        self.assertEqual([a for a, _ in ticks], [-2, 0, 2, 4, 6, 8, 10])
        self.assertEqual([a for a, major in ticks if major], [0, 10])

    def test_face_ticks(self):
        ticks = g.face_ticks(dict(r=10, ticks=10, major=5))
        self.assertEqual(len(ticks), 11)
        self.assertAlmostEqual(ticks[0][0], -135.0)
        self.assertAlmostEqual(ticks[-1][0], 135.0)
        self.assertEqual([i for i, (_, major) in enumerate(ticks) if major], [0, 5, 10])
        # All the way round (an encoder): the last tick would repeat the
        # first, and with no `major` every tick is the same.
        ticks = g.face_ticks(dict(r=10, ticks=24, sweep=360))
        self.assertEqual(len(ticks), 24)
        self.assertAlmostEqual(ticks[1][0] - ticks[0][0], 15.0)
        self.assertEqual([major for _, major in ticks], [False] * 24)

    def test_segment_to_rectangle(self):
        rect = (10, 10, 20, 14)
        self.assertEqual(g.segment_rect_distance((0, 12), (30, 12), rect), 0.0)      # through
        self.assertEqual(g.segment_rect_distance((12, 11), (13, 13), rect), 0.0)     # inside
        self.assertEqual(g.segment_rect_distance((0, 0), (15, 12), rect), 0.0)       # ends inside
        self.assertAlmostEqual(g.segment_rect_distance((5, 0), (5, 30), rect), 5.0)  # beside
        self.assertAlmostEqual(g.segment_rect_distance((0, 17), (30, 17), rect), 3.0)
        self.assertAlmostEqual(g.segment_rect_distance((23, 18), (30, 30), rect), 5.0)   # 3-4-5 off a corner


class Lettering(unittest.TestCase):
    def test_both_weights_load_and_measure(self):
        w = g.text_width("RATE", 2.0, 0.0, "normal")
        self.assertGreater(w, 2.0)
        self.assertLess(w, 5.0)
        self.assertAlmostEqual(g.text_width("RATE", 4.0, 0.0, "normal"), 2 * w, places=9)
        # The two weights are two files with their own widths.
        self.assertNotEqual(g.text_width("RATE", 2.0, 0.0, "bold"), w)
        self.assertAlmostEqual(g.text_width("RATE", 2.0, 0.0, "bold"), w, delta=0.3)
        self.assertGreater(g.cap_height(2.0), 1.2)
        self.assertLess(g.cap_height(2.0), 1.6)
        # Letterspacing goes between letters, not after the last.
        self.assertAlmostEqual(g.text_width("RATE", 2.0, 0.5, "normal"), w + 1.5, places=9)

    def test_missing_typeface_fails(self):
        saved, g.FONT_DIR = g.FONT_DIR, ROOT / "tools" / "no-such-folder"
        g.FACES.clear()
        try:
            with self.assertRaises(SystemExit) as cm:
                g.text_width("RATE", 2.0, 0.0, "normal")
            self.assertIn("D-DINCondensed.ttf", cm.exception.code)
        finally:
            g.FONT_DIR = saved
            g.FACES.clear()

    def test_instrument_panels_use_the_one_typeface(self):
        for t in g.INSTRUMENT.values():
            svg = g.panel_svg("Specimen", g.SPECIMEN, t)
            families = set(re.findall(r'font-family="([^"]*)"', svg))
            self.assertEqual(families, {"D-DIN Condensed"})
            self.assertEqual(set(re.findall(r'font-weight="([^"]*)"', svg)), {"normal", "bold"})


class Traces(unittest.TestCase):
    def straight(self, other_x):
        return spec(components=[("input", "CV", 10.0, 100.0, None),
                                ("param", "KNOB", 10.0, 40.0, None),
                                ("param", "OTHER", other_x, 70.0, None)],
                    traces=[["INPUT_CV", "PARAM_KNOB"]])

    def test_trace_ends_at_the_edge_aimed_at_the_centre(self):
        s = spec(components=[("input", "CV", 10.0, 100.0, None), ("param", "KNOB", 40.0, 40.0, None)],
                 traces=[["INPUT_CV", (10.0, 80.0), "PARAM_KNOB"]])
        g.check_panel("T", s)
        pts = g.trace_points("T", s, s["traces"][0])
        self.assertEqual(len(pts), 3)
        # Starts on the jack's edge, straight up the column.
        self.assertAlmostEqual(pts[0][0], 10.0)
        self.assertAlmostEqual(pts[0][1], 100.0 - g.RADII["port"])
        self.assertEqual(pts[1], (10.0, 80.0))
        # Ends on the knob's edge...
        ex, ey = pts[2]
        self.assertAlmostEqual(math.hypot(ex - 40.0, ey - 40.0), g.RADII["limb"], places=9)
        # ...on the line from the elbow to the knob's centre.
        cross = (ex - 10.0) * (40.0 - 80.0) - (ey - 80.0) * (40.0 - 10.0)
        self.assertAlmostEqual(cross, 0.0, places=9)
        self.assertLess(math.hypot(ex - 10.0, ey - 80.0), math.hypot(40.0 - 10.0, 40.0 - 80.0))

    def test_trace_to_a_dial_stops_at_its_bezel(self):
        s = spec(components=[("input", "CV", 10.0, 100.0, None), ("param", "KNOB", 30.0, 40.0, None)],
                 sizes={"PARAM_KNOB": "hero"},
                 faces={"PARAM_KNOB": dict(r=16.0, ticks=50, major=5)},
                 traces=[["INPUT_CV", (10.0, 70.0), "PARAM_KNOB"]])
        g.check_panel("T", s)
        ex, ey = g.trace_points("T", s, s["traces"][0])[-1]
        self.assertGreater(math.hypot(ex - 30.0, ey - 40.0), 16.0)
        self.assertLess(math.hypot(ex - 30.0, ey - 40.0), 16.5)

    def test_trace_clear_of_a_foreign_knob_passes(self):
        g.check_panel("T", self.straight(15.0))

    def test_trace_grazing_a_foreign_knob_fails_naming_both(self):
        # A limb knob (radius 4.3) 4.5 mm from the trace: 0.2 mm, under the clearance.
        msg = failure(self, "T", self.straight(14.5))
        self.assertIn("INPUT_CV", msg)
        self.assertIn("PARAM_KNOB", msg)
        self.assertIn("PARAM_OTHER", msg)
        self.assertIn("trace", msg)

    def test_trace_grazing_a_foreign_dial_face_fails(self):
        s = self.straight(30.0)
        g.check_panel("T", s)
        s["faces"] = {"PARAM_OTHER": dict(r=19.9, ticks=10, major=5)}
        msg = failure(self, "T", s)
        self.assertIn("PARAM_OTHER", msg)
        self.assertIn("INPUT_CV", msg)

    def crossing(self, b_knob_x):
        return spec(components=[("input", "A", 10.0, 100.0, None), ("param", "A", 40.0, 40.0, None),
                                ("input", "B", 40.0, 100.0, None), ("param", "B", b_knob_x, 40.0, None)],
                    traces=[["INPUT_A", "PARAM_A"], ["INPUT_B", "PARAM_B"]])

    def test_traces_that_do_not_cross_pass(self):
        g.check_panel("T", self.crossing(52.0))

    def test_crossing_traces_fail_naming_both(self):
        msg = failure(self, "T", self.crossing(10.0))
        for part in ("INPUT_A", "PARAM_A", "INPUT_B", "PARAM_B", "cross"):
            self.assertIn(part, msg)

    def test_more_than_one_elbow_fails(self):
        s = spec(components=[("input", "CV", 10.0, 100.0, None), ("param", "KNOB", 40.0, 40.0, None)],
                 traces=[["INPUT_CV", (10.0, 80.0), (20.0, 70.0), "PARAM_KNOB"]])
        self.assertIn("elbow", failure(self, "T", s))

    def test_trace_to_an_unknown_component_fails(self):
        s = spec(components=[("input", "CV", 10.0, 100.0, None)], traces=[["INPUT_CV", "PARAM_NOPE"]])
        self.assertIn("PARAM_NOPE", failure(self, "T", s))


class TraceShape(unittest.TestCase):
    def test_a_bend_under_a_passed_control_is_the_one_elbow(self):
        s = spec(components=[("input", "A", 10.0, 110.0, None), ("param", "A_ATT", 25.0, 90.0, None),
                             ("param", "A", 25.0, 40.0, None)],
                 traces=[["INPUT_A", "PARAM_A_ATT", "PARAM_A"]])
        g.check_panel("T", s)

    def test_a_hidden_second_bend_fails(self):
        s = spec(components=[("input", "A", 10.0, 110.0, None), ("param", "A_ATT", 25.0, 90.0, None),
                             ("param", "A", 10.0, 40.0, None)],
                 traces=[["INPUT_A", "PARAM_A_ATT", (25.0, 60.0), "PARAM_A"]])
        msg = failure(self, "T", s)
        self.assertIn("elbow", msg)
        self.assertIn("PARAM_A_ATT", msg)
        self.assertIn("(25.0, 60.0)", msg)

    def test_a_point_on_the_straight_line_is_not_a_bend(self):
        s = spec(components=[("input", "A", 10.0, 110.0, None), ("param", "A", 40.0, 40.0, None)],
                 traces=[["INPUT_A", (10.0, 90.0), (10.0, 80.0), "PARAM_A"]])
        g.check_panel("T", s)

    def test_a_light_on_the_dial_the_trace_runs_to_is_not_grazed(self):
        # Sway's rate light sits on the Rate face, on the line from straight
        # below to the dial's centre; the trace stops at the bezel, short of it.
        def dial(extra):
            return spec(components=[("input", "A", 21.0, 110.0, None), ("param", "R", 21.0, 34.0, None),
                                    ("light", "R", 21.0, 45.2, None)] + extra,
                        sizes={"PARAM_R": "hero"}, faces={"PARAM_R": dict(r=16.4, ticks=50, major=5)},
                        traces=[["INPUT_A", "PARAM_R"]])
        g.check_panel("T", dial([]))
        # A control beside the drawn part of the same trace still fails.
        msg = failure(self, "T", dial([("param", "O", 25.5, 80.0, None)]))
        self.assertIn("PARAM_O", msg)
        self.assertIn("INPUT_A", msg)

    def test_the_width_of_the_stroke_counts(self):
        def beside(x):
            return spec(components=[("input", "A", 10.0, 100.0, None), ("param", "K", 10.0, 40.0, None),
                                    ("param", "O", x, 70.0, None)], traces=[["INPUT_A", "PARAM_K"]])
        clear = g.RADII["limb"] + g.CLEARANCE + g.TRACE_WIDTH / 2
        self.assertGreater(g.TRACE_WIDTH, 0.2)
        g.check_panel("T", beside(10.0 + clear + 0.01))
        # 0.31 mm from the trace's centre line, but only 0.185 from its edge.
        self.assertIn("PARAM_O", failure(self, "T", beside(10.0 + g.RADII["limb"] + 0.31)))

    def test_an_elbow_touching_another_trace_fails(self):
        def pair(elbow_x):
            return spec(components=[("input", "A", 10.0, 100.0, None), ("param", "A", 10.0, 40.0, None),
                                    ("input", "B", 30.0, 100.0, None), ("param", "B", 30.0, 40.0, None)],
                        traces=[["INPUT_A", "PARAM_A"], ["INPUT_B", (elbow_x, 70.0), "PARAM_B"]])
        g.check_panel("T", pair(10.0 + g.CLEARANCE + g.TRACE_WIDTH + 0.01))
        for x in (10.0, 10.1, 10.0 + g.CLEARANCE + g.TRACE_WIDTH - 0.01):
            msg = failure(self, "T", pair(x))
            for part in ("INPUT_A", "PARAM_A", "INPUT_B", "PARAM_B", "cross or touch"):
                self.assertIn(part, msg)

    def test_traces_sharing_a_line_fail(self):
        s = spec(components=[("input", "A", 10.0, 110.0, None), ("param", "A", 40.0, 30.0, None),
                             ("input", "B", 25.0, 100.0, None), ("param", "B", 10.0, 20.0, None)],
                 traces=[["INPUT_A", (10.0, 60.0), "PARAM_A"], ["INPUT_B", (10.0, 85.0), "PARAM_B"]])
        msg = failure(self, "T", s)
        for part in ("INPUT_A", "PARAM_A", "INPUT_B", "PARAM_B", "cross or touch"):
            self.assertIn(part, msg)


class TraceObstacles(unittest.TestCase):
    def column(self, **kw):
        return spec(components=[("input", "A", 10.0, 110.0, None), ("param", "A", 10.0, 40.0, None)]
                    + kw.pop("more", []), traces=[["INPUT_A", "PARAM_A"]], **kw)

    def test_trace_under_a_screen_fails(self):
        g.check_panel("T", self.column(screens=[("S", 20.0, 60.0, 20.0, 10.0)]))
        msg = failure(self, "T", self.column(screens=[("S", 5.0, 60.0, 20.0, 10.0)]))
        for part in ("trace INPUT_A > PARAM_A", "SCREEN_S"):
            self.assertIn(part, msg)

    def test_trace_through_a_screens_ruler_fails(self):
        screens = [("S", 12.4, 60.0, 20.8, 10.4)]
        g.check_panel("T", self.column(screens=screens))
        msg = failure(self, "T", self.column(screens=screens, rulers={"S": dict(px=0.65, sides="l")}))
        self.assertIn("SCREEN_S", msg)

    def test_trace_through_a_note_fails(self):
        g.check_panel("T", self.column(notes=[(13.0, 75.0, "BESIDE", 2.0, "ivory", "start")]))
        msg = failure(self, "T", self.column(notes=[(10.0, 75.0, "OVERLAP", 2.0, "ivory")]))
        for part in ("trace INPUT_A > PARAM_A", 'note "OVERLAP"'):
            self.assertIn(part, msg)

    def test_trace_through_the_label_under_its_knob_fails(self):
        comps = [("input", "A", 10.0, 110.0, None), ("param", "A", 10.0, 40.0, "LONGNAME")]
        g.check_panel("T", spec(components=comps, traces=[["INPUT_A", "PARAM_A"]]))
        msg = failure(self, "T", spec(components=comps, traces=[["INPUT_A", "PARAM_A"]],
                                      label_at={"PARAM_A": "below"}))
        for part in ("trace INPUT_A > PARAM_A", 'label "LONGNAME"'):
            self.assertIn(part, msg)

    def test_trace_through_the_label_over_its_jack_fails(self):
        comps = [("input", "A", 10.0, 110.0, "IN"), ("param", "A", 10.0, 40.0, None)]
        g.check_panel("T", spec(components=comps, traces=[["INPUT_A", "PARAM_A"]], label_at={"INPUT_A": "left"}))
        msg = failure(self, "T", spec(components=comps, traces=[["INPUT_A", "PARAM_A"]]))
        for part in ("trace INPUT_A > PARAM_A", 'label "IN"'):
            self.assertIn(part, msg)

    def test_trace_across_a_wide_tile_fails(self):
        more = [("output", "X", 20.0, 80.0, "X")]
        g.check_panel("T", self.column(more=more, tiles=[dict(port="OUTPUT_X")]))
        msg = failure(self, "T", self.column(more=more, tiles=[dict(port="OUTPUT_X", left=11.0)]))
        for part in ("trace INPUT_A > PARAM_A", "tile of OUTPUT_X"):
            self.assertIn(part, msg)


class FootMarkAndLabels(unittest.TestCase):
    def test_two_notes_overlapping_fail_naming_both(self):
        g.check_panel("T", spec(notes=[(20.0, 60.0, "ALPHA", 2.0, "ivory"), (20.5, 63.0, "BETA", 2.0, "ivory")]))
        msg = failure(self, "T", spec(notes=[(20.0, 60.0, "ALPHA", 2.0, "ivory"),
                                             (20.5, 60.3, "BETA", 2.0, "ivory")]))
        for part in ('note "ALPHA"', 'note "BETA"', "overlap"):
            self.assertIn(part, msg)

    def test_label_over_another_label_fails(self):
        def pair(dx):
            return spec(components=[("input", "A", 20.0, 60.0, "CLOCK"), ("input", "B", 20.0 + dx, 60.0, "RESET")])
        g.check_panel("T", pair(9.4))
        msg = failure(self, "T", pair(9.4) | dict(label_dx={"INPUT_B": -5.0}))
        for part in ('label "CLOCK"', 'label "RESET"'):
            self.assertIn(part, msg)

    def test_foot_mark_over_a_screen_fails(self):
        g.check_panel("T", spec(screens=[("S", 10.0, 100.0, 40.0, 4.0)]))
        msg = failure(self, "T", spec(screens=[("S", 10.0, 120.0, 40.0, 4.0)]))
        self.assertIn("foot mark", msg)
        self.assertIn("SCREEN_S", msg)

    def test_foot_mark_over_a_note_fails(self):
        msg = failure(self, "T", spec(notes=[(30.48, 125.6, "HELLO", 2.0, "ivory")]))
        self.assertIn("foot mark", msg)
        self.assertIn('note "HELLO"', msg)

    def test_lettering_over_a_screen_or_its_ruler_fails(self):
        screens = [("S", 12.4, 60.0, 20.8, 10.4)]
        note = [(9.5, 66.0, "IN", 2.0, "dim")]
        g.check_panel("T", spec(screens=screens, notes=note))
        msg = failure(self, "T", spec(screens=screens, notes=note, rulers={"S": dict(px=0.65, sides="l")}))
        self.assertIn('note "IN"', msg)
        self.assertIn("SCREEN_S", msg)
        msg = failure(self, "T", spec(screens=screens, notes=[(20.0, 66.0, "IN", 2.0, "dim")]))
        self.assertIn("SCREEN_S", msg)

    def test_lettering_and_dial_faces(self):
        def dial(note):
            return spec(components=[("param", "R", 30.0, 60.0, None)], sizes={"PARAM_R": "hero"},
                        faces={"PARAM_R": dict(r=16.4, ticks=50, major=5)}, notes=[note])
        g.check_panel("T", dial((30.0, 74.0, "ON", 2.0, "ink")))         # wholly on the face
        g.check_panel("T", dial((30.0, 80.0, "OFF", 2.0, "ivory")))      # clear of it
        msg = failure(self, "T", dial((30.0, 77.0, "EDGE", 2.0, "ivory")))
        self.assertIn('note "EDGE"', msg)
        self.assertIn("dial face of PARAM_R", msg)
        msg = failure(self, "T", dial((30.0, 74.0, "PALE", 2.0, "ivory")))
        self.assertIn('note "PALE"', msg)
        self.assertIn("ivory on ivory", msg)
        msg = failure(self, "T", dial((30.0, 80.0, "DARK", 2.0, "ink")))
        self.assertIn('note "DARK"', msg)
        self.assertIn("ink on the ground", msg)

    def test_lettering_over_a_foreign_tile_fails(self):
        def tiled(note_x):
            return spec(components=[("output", "X", 30.0, 60.0, "X")], tiles=[dict(port="OUTPUT_X")],
                        notes=[(note_x, 64.4, "NEAR", 2.0, "dim", "end")])
        g.check_panel("T", tiled(25.0))
        msg = failure(self, "T", tiled(26.5))
        self.assertIn('note "NEAR"', msg)
        self.assertIn("tile of OUTPUT_X", msg)

    def test_tile_name_crowding_its_edges_fails(self):
        # A tile's name keeps a margin inside the tile: CRACKLE at the
        # default 2.0 mm fills an 8.8 mm tile almost edge to edge (Hadal's
        # first panel), and must fail; smaller, it passes.
        def tiled(**t):
            return spec(components=[("output", "CRACKLE", 30.0, 60.0, "CRACKLE")],
                        tiles=[dict(port="OUTPUT_CRACKLE", **t)])
        msg = failure(self, "T", tiled())
        self.assertIn('label "CRACKLE"', msg)
        self.assertIn("edge of the tile of OUTPUT_CRACKLE", msg)
        g.check_panel("T", tiled(name_size=1.7))
        # A short name at the default size is untouched.
        g.check_panel("T", spec(components=[("output", "L", 30.0, 60.0, "L")], tiles=[dict(port="OUTPUT_L")]))

    def test_lettering_over_the_limb_or_an_origin_fails(self):
        limb = [dict(c=(30.0, 20.0), r=40.0, a0=-30, a1=30, step=2, major=5)]
        g.check_panel("T", spec(limbs=limb, notes=[(30.0, 57.0, "ABOVE", 2.0, "ivory")]))
        for y in (60.5, 62.0):   # on the arc; among the ticks outside it
            msg = failure(self, "T", spec(limbs=limb, notes=[(30.0, y, "ONIT", 2.0, "ivory")]))
            self.assertIn('note "ONIT"', msg)
            self.assertIn("limb", msg)
        g.check_panel("T", spec(origins=[(30.0, 20.0)], notes=[(30.0, 25.0, "CLEAR", 2.0, "ivory")]))
        msg = failure(self, "T", spec(origins=[(30.0, 20.0)], notes=[(30.0, 22.0, "PIVOT", 2.0, "ivory")]))
        self.assertIn('note "PIVOT"', msg)
        self.assertIn("origin", msg)

    def test_foot_mark_clear_of_a_jack_passes(self):
        g.check_panel("T", spec(components=[("input", "J", 30.48, 115.0, None)]))

    def test_foot_mark_over_a_jack_fails(self):
        msg = failure(self, "T", spec(components=[("input", "J", 30.48, 122.0, None)]))
        self.assertIn("foot mark", msg)
        self.assertIn("INPUT_J", msg)

    def test_foot_mark_beside_a_jack_passes_and_under_it_fails(self):
        # The mark is centred; a jack out at the side is clear of it at the
        # same height, so the check uses the mark's real width.
        g.check_panel("T", spec(components=[("input", "J", 6.0, 123.0, None)]))
        left = g.text_box(g.foot_mark(spec()))[0]
        self.assertGreater(left, 15.0)
        self.assertLess(left, 25.0)

    def test_foot_mark_over_a_tile_fails(self):
        comps = [("output", "X", 30.48, 118.0, "X")]
        g.check_panel("T", spec(components=comps, tiles=[dict(port="OUTPUT_X")]))
        msg = failure(self, "T", spec(components=comps, tiles=[dict(port="OUTPUT_X", bottom=7.0)]))
        self.assertIn("foot mark", msg)
        self.assertIn("OUTPUT_X", msg)
        self.assertIn("tile", msg)

    def test_label_over_a_component_fails(self):
        comps = [("param", "A", 20.0, 60.0, "ALPHA"), ("param", "B", 20.0, 50.0, None)]
        g.check_panel("T", spec(components=comps, label_at={"PARAM_A": "below"}))
        msg = failure(self, "T", spec(components=comps))   # above: onto B
        self.assertIn("ALPHA", msg)
        self.assertIn("PARAM_B", msg)
        self.assertIn("label", msg)

    def test_note_over_a_component_fails(self):
        s = spec(components=[("input", "J", 20.0, 60.0, None)], notes=[(20.0, 60.5, "NOTE", 1.8, "dim")])
        msg = failure(self, "T", s)
        self.assertIn("NOTE", msg)
        self.assertIn("INPUT_J", msg)


class Clearance(unittest.TestCase):
    def test_new_component_radii_are_checked(self):
        def two(dx):
            return spec(components=[("param", "A", 20.0, 60.0, None), ("param", "B", 20.0 + dx, 60.0, None)],
                        sizes={"PARAM_A": "hero", "PARAM_B": "atten"})
        need = g.RADII["hero"] + g.RADII["atten"] + g.CLEARANCE
        g.check_panel("T", two(need + 0.01))
        msg = failure(self, "T", two(need - 0.05))
        self.assertIn("A", msg)
        self.assertIn("B", msg)

    def test_component_straddling_a_dial_face_fails(self):
        def lit(y):
            return spec(components=[("param", "RATE", 30.0, 40.0, None), ("light", "RATE", 30.0, y, None)],
                        sizes={"PARAM_RATE": "hero"}, faces={"PARAM_RATE": dict(r=16.0, ticks=50, major=5)})
        g.check_panel("T", lit(51.5))    # on the face, clear of the knob
        g.check_panel("T", lit(59.0))    # off the face
        msg = failure(self, "T", lit(56.0))
        self.assertIn("LIGHT_RATE", msg)
        self.assertIn("PARAM_RATE", msg)

    def test_artwork_is_as_wide_as_the_radius_used_for_clearance(self):
        art = g.component_art(g.INSTRUMENT[""])
        for size, layers in g.ART_LAYERS.items():
            for layer in layers:
                w, h, _ = art[layer]
                if size in ("toggle", "toggle3"):
                    self.assertLessEqual(math.hypot(w, h) / 2, g.RADII[size] + 1e-9, layer)
                else:
                    self.assertAlmostEqual(w / 2, g.RADII[size], places=9, msg=layer)
                    self.assertAlmostEqual(h / 2, g.RADII[size], places=9, msg=layer)


class Areas(unittest.TestCase):
    def test_component_on_a_screens_ruler_fails(self):
        def beside(rulers):
            return spec(components=[("input", "J", 7.0, 66.0, None)], screens=[("S", 12.4, 60.0, 20.8, 10.4)],
                        rulers=rulers)
        g.check_panel("T", beside({}))
        msg = failure(self, "T", beside({"S": dict(px=0.65, sides="l")}))
        self.assertIn("INPUT J", msg)
        self.assertIn("SCREEN_S", msg)

    def test_dial_face_over_a_tile_or_a_screen_fails(self):
        def dial(**kw):
            comps = [("param", "R", 30.0, 40.0, None)] + kw.pop("more", [])
            return spec(components=comps, sizes={"PARAM_R": "hero"},
                        faces={"PARAM_R": dict(r=16.0, ticks=50, major=5)}, **kw)
        g.check_panel("T", dial(more=[("output", "X", 30.0, 64.0, "X")], tiles=[dict(port="OUTPUT_X")]))
        msg = failure(self, "T", dial(more=[("output", "X", 30.0, 63.0, "X")], tiles=[dict(port="OUTPUT_X")]))
        self.assertIn("PARAM_R", msg)
        self.assertIn("tile of OUTPUT_X", msg)
        g.check_panel("T", dial(screens=[("S", 10.0, 58.0, 40.0, 10.0)]))
        msg = failure(self, "T", dial(screens=[("S", 10.0, 56.5, 40.0, 10.0)]))
        self.assertIn("PARAM_R", msg)
        self.assertIn("SCREEN_S", msg)

    def test_screens_whose_bezels_touch_fail(self):
        # Two bezels side by side would read as one heavy brass bar.
        def pair(gap):
            top = ("A", 10.0, 20.0, 40.0, 5.0)
            y = 20.0 + 5.0 + 2 * g.SCREEN_BEZEL + gap
            return spec(screens=[top, ("B", 10.0, y, 40.0, 10.0)])
        g.check_panel("T", pair(g.CLEARANCE + 0.01))
        for gap in (-0.4, 0.0, g.CLEARANCE - 0.01):
            msg = failure(self, "T", pair(gap))
            for part in ("SCREEN_A", "SCREEN_B", "bezel"):
                self.assertIn(part, msg)
        # Side by side, and counting a ruler as part of its screen.
        g.check_panel("T", spec(screens=[("A", 10.0, 20.0, 10.0, 10.0), ("B", 22.0, 20.0, 10.0, 10.0)]))
        msg = failure(self, "T", spec(screens=[("A", 10.0, 20.0, 10.0, 10.0), ("B", 22.0, 20.0, 10.0, 10.0)],
                                      rulers={"B": dict(px=0.65, sides="l")}))
        self.assertIn("SCREEN_B", msg)

    def test_tile_over_a_screen_fails(self):
        def tiled(y):
            return spec(components=[("output", "X", 30.0, y, "X")], tiles=[dict(port="OUTPUT_X")],
                        screens=[("S", 10.0, 60.0, 40.0, 10.0)])
        g.check_panel("T", tiled(79.0))
        msg = failure(self, "T", tiled(77.5))
        self.assertIn("tile of OUTPUT_X", msg)
        self.assertIn("SCREEN_S", msg)


class OnThePanel(unittest.TestCase):
    """Components and dial faces lie wholly on the panel."""

    def test_a_component_partly_off_the_panel_fails_naming_it(self):
        w = 12 * g.HP
        for x, y in ((g.RADII["port"] + g.CLEARANCE, 60.0), (w - g.RADII["port"] - g.CLEARANCE, 60.0),
                     (30.0, g.RADII["port"] + g.CLEARANCE), (5.0, g.PANEL_H - g.RADII["port"] - g.CLEARANCE)):
            g.check_panel("T", spec(components=[("input", "J", x, y, None)]))
        for x, y in ((0.9, 60.0), (w - 3.9, 60.0), (30.0, 3.9), (5.0, g.PANEL_H - 2.0)):
            msg = failure(self, "T", spec(components=[("input", "J", x, y, None)]))
            self.assertIn("INPUT_J", msg)
            self.assertIn("off the panel", msg)

    def test_a_dial_face_partly_off_the_panel_fails_naming_it(self):
        def dial(x):
            return spec(components=[("param", "R", x, 60.0, None)], sizes={"PARAM_R": "face"},
                        faces={"PARAM_R": dict(r=7.3, ticks=20, major=5)})
        g.check_panel("T", dial(8.0))
        msg = failure(self, "T", dial(7.0))      # the knob is on the panel, its face is not
        self.assertIn("dial face of PARAM_R", msg)
        self.assertIn("off the panel", msg)

    def test_fathoms_res_column_moved_to_the_edge_fails(self):
        # The reviewer's case: Fathom's RES CV jack and attenuverter at x = 0.9.
        import copy
        s = copy.deepcopy(g.PANELS["Fathom"])
        g.check_panel("Fathom", s)
        s["components"] = [(k, n, 0.9 if n in ("RES_CV", "RES_ATT") else x, y, lab)
                           for k, n, x, y, lab in s["components"]]
        s["traces"] = [t for t in s["traces"] if t[0] != "INPUT_RES_CV"]
        s["traces"].append(["INPUT_RES_CV", "PARAM_RES_ATT", (0.9, 70.0), "PARAM_RES"])
        msg = failure(self, "Fathom", s)
        self.assertIn("off the panel", msg)
        self.assertTrue("INPUT_RES_CV" in msg or "PARAM_RES_ATT" in msg, msg)


class LimbClear(unittest.TestCase):
    """The limb, arc and ticks, runs under no dial face, tile or screen."""
    LIMB = [dict(c=(30.0, 0.0), r=40.0, a0=-20, a1=20, step=2, major=5)]

    def test_a_limb_under_a_dial_face_fails_naming_it(self):
        def dial(y):
            return spec(components=[("param", "R", 30.0, y, None)], sizes={"PARAM_R": "face"},
                        faces={"PARAM_R": dict(r=7.3, ticks=20, major=5)}, limbs=self.LIMB)
        g.check_panel("T", dial(25.0))       # inside the arc, clear of it
        g.check_panel("T", dial(55.0))       # outside it, clear of the ticks
        for y in (40.0, 33.0, 48.0):         # on the arc; touching it; over the ticks
            msg = failure(self, "T", dial(y))
            self.assertIn("limb", msg)
            self.assertIn("dial face of PARAM_R", msg)

    def test_a_limb_across_a_tile_fails_naming_it(self):
        def tiled(y):
            return spec(components=[("output", "X", 30.0, y, "X")], tiles=[dict(port="OUTPUT_X")],
                        limbs=self.LIMB)
        g.check_panel("T", tiled(53.0))
        msg = failure(self, "T", tiled(44.0))
        self.assertIn("limb", msg)
        self.assertIn("tile of OUTPUT_X", msg)

    def test_a_limb_under_a_screen_fails_naming_it(self):
        g.check_panel("T", spec(limbs=self.LIMB, screens=[("S", 20.0, 46.0, 20.0, 10.0)]))
        msg = failure(self, "T", spec(limbs=self.LIMB, screens=[("S", 20.0, 38.0, 20.0, 10.0)]))
        self.assertIn("limb", msg)
        self.assertIn("SCREEN_S", msg)


class EveryCVPathTraced(unittest.TestCase):
    """A CV input with an attenuverter (PARAM_<N>_ATT beside INPUT_<N> or
    INPUT_<N>_CV) has a trace from the jack through the attenuverter to the
    control it moves. Inputs with no attenuverter need none."""

    def column(self, jack="A", traces=None):
        return spec(components=[("input", jack, 10.0, 110.0, None), ("param", "A_ATT", 10.0, 90.0, None),
                                ("param", "A", 10.0, 40.0, None), ("input", "CLOCK", 30.0, 110.0, "CLOCK")],
                    traces=traces if traces is not None else [[f"INPUT_{jack}", "PARAM_A_ATT", "PARAM_A"]])

    def test_a_traced_cv_input_passes_and_an_untraced_one_fails(self):
        for jack in ("A", "A_CV"):
            g.check_panel("T", self.column(jack))
            msg = failure(self, "T", self.column(jack, traces=[]))
            for part in (f"INPUT_{jack}", "PARAM_A_ATT", "trace"):
                self.assertIn(part, msg)

    def test_a_trace_that_skips_the_attenuverter_or_stops_at_it_fails(self):
        s = self.column(traces=[["INPUT_A", "PARAM_A"]])
        s["components"][1] = ("param", "A_ATT", 20.0, 90.0, None)    # out of the trace's way
        msg = failure(self, "T", s)
        self.assertIn("PARAM_A_ATT", msg)
        msg = failure(self, "T", self.column(traces=[["INPUT_A", "PARAM_A_ATT"]]))
        self.assertIn("PARAM_A_ATT", msg)

    def test_an_attenuverter_without_its_jack_fails(self):
        s = spec(components=[("param", "A_ATT", 10.0, 90.0, None), ("input", "B", 30.0, 110.0, None)])
        msg = failure(self, "T", s)
        for part in ("PARAM_A_ATT", "INPUT_A", "INPUT_A_CV"):
            self.assertIn(part, msg)

    def test_each_module_loses_a_trace_and_fails(self):
        for name in ("Sway", "Fathom"):
            s = dict(g.PANELS[name])
            jack = s["traces"][0][0]
            s["traces"] = s["traces"][1:]
            msg = failure(self, name, s)
            self.assertIn(jack, msg)


class Ropes(unittest.TestCase):
    def rope(self, **kw):
        return spec(components=[("input", "A", 10.0, 40.0, None), ("input", "B", 10.0, 70.0, None)]
                    + kw.pop("extra", []),
                    ropes=kw.pop("ropes", [[(10.0, 20.0), "INPUT_A", "INPUT_B"]]), **kw)

    def test_rope_is_drawn_in_brass(self):
        s = self.rope()
        g.check_panel("T", s)
        svg = g.panel_svg("T", s, g.INSTRUMENT[""])
        paths = [p for p in re.findall(r"<path [^>]*>", svg)
                 if 'stroke="#c9a46a"' in p and 'stroke-width="0.4"' in p]
        self.assertEqual(len(paths), 1)
        d = paths[0]
        for pt in ("10 20", "10 40", "10 70"):
            self.assertIn(pt, d)

    def test_rope_names_unknown_component(self):
        msg = failure(self, "T", self.rope(ropes=[[(10.0, 20.0), "INPUT_Z"]]))
        self.assertIn("rope", msg)
        self.assertIn("INPUT_Z", msg)

    def test_rope_grazing_other_component_fails(self):
        # A port (radius 4.0) 4.0 mm off the rope's line, which is 0.4 wide: a 0.2 mm gap.
        msg = failure(self, "T", self.rope(extra=[("input", "C", 14.0, 55.0, None)]))
        self.assertIn("INPUT_C", msg)

    def test_rope_clear_of_other_component_passes(self):
        g.check_panel("T", self.rope(extra=[("input", "C", 20.0, 55.0, None)]))

    def test_rope_key_accepted(self):
        self.assertIn("ropes", g.INSTRUMENT_KEYS)
        g.check_references("T", self.rope())


class FaceWords(unittest.TestCase):
    def words(self, words, **face):
        return spec(components=[("param", "V", 30.0, 60.0, None)], sizes={"PARAM_V": "hero"},
                    faces={"PARAM_V": dict(r=19.0, ticks=50, major=10, words=words, **face)})

    def test_words_drawn_rotated(self):
        s = self.words([(0, "UP"), (90, "RIGHT")])
        g.check_panel("T", s)
        svg = g.panel_svg("T", s, g.INSTRUMENT[""])
        up = re.search(r"<text [^>]*>UP</text>", svg).group(0)
        right = re.search(r"<text [^>]*>RIGHT</text>", svg).group(0)
        for t in (up, right):
            self.assertIn('font-weight="bold"', t)
            self.assertIn('fill="#1c1a16"', t)
        # At the top of the face, in line with the knob, and not turned.
        x, y = (float(v) for v in re.search(r'x="([\d.]+)" y="([\d.]+)"', up).groups())
        self.assertAlmostEqual(x, 30.0, delta=0.2)
        self.assertLess(y, 60.0 - 14.0)
        self.assertGreater(y, 60.0 - 19.0)
        self.assertNotIn("transform", up)
        # At three o'clock, turned a quarter turn about its own start.
        m = re.search(r'transform="rotate\(90 ([\d.]+) ([\d.]+)\)"', right)
        self.assertIsNotNone(m, right)
        self.assertGreater(float(m[1]), 30.0 + 13.0)
        self.assertAlmostEqual(float(m[2]), 60.0, delta=1.2)

    def test_word_off_face_fails(self):
        msg = failure(self, "T", self.words([(0, "OUTSIDE")], words_r=21.0))
        self.assertIn("OUTSIDE", msg)

    def test_words_overlapping_fail(self):
        msg = failure(self, "T", self.words([(40, "FIRST"), (40, "SECOND")]))
        self.assertIn("FIRST", msg)
        self.assertIn("SECOND", msg)

    def test_word_over_the_knob_fails(self):
        msg = failure(self, "T", self.words([(0, "KNOB")], words_r=8.0))
        self.assertIn("KNOB", msg)

    def test_words_key_accepted(self):
        for key in ("words", "words_r", "words_size"):
            self.assertIn(key, g.FACE_KEYS)
        g.check_references("T", self.words([(0, "UP")], words_r=15.0, words_size=1.7))


class FaceMarks(unittest.TestCase):
    """A drawn mark on a dial face (the infinity sign on Doldrums' Stretch)."""

    def marked(self, marks, **face):
        return spec(components=[("param", "V", 30.0, 60.0, None)], sizes={"PARAM_V": "hero"},
                    faces={"PARAM_V": dict(r=19.0, ticks=50, major=10, marks=marks, **face)})

    def test_face_mark_is_drawn(self):
        s = g.PANELS["Doldrums"]
        g.check_panel("Doldrums", s)
        svg = g.panel_svg("Doldrums", s, g.INSTRUMENT[""])
        marks = re.findall(r'<path d="(M0,0 C[^"]*)" transform="translate\(([\d.]+) ([\d.]+)\) rotate\(135\)" '
                           r'fill="none" stroke="#1c1a16" stroke-width="0.32"', svg)
        self.assertEqual(len(marks), 1)
        d, x, y = marks[0]
        # Half-length 1.4 mm, half-height 0.85 mm.
        self.assertIn("1.4,0", d)
        self.assertIn("-0.85", d)
        # On the words' radius (r 16 - 3) at 135 degrees on Stretch's face.
        c = next(c for c in g.comps_of("Doldrums", s) if c.id == "PARAM_STRETCH")
        px, py = g.dial_point(c.x, c.y, 13.0, 135)
        self.assertAlmostEqual(float(x), px, delta=0.01)
        self.assertAlmostEqual(float(y), py, delta=0.01)

    def test_face_mark_clearance(self):
        g.check_panel("T", self.marked([(135, "infinity")]))
        # The same mark over a word on the face fails, naming both.
        msg = failure(self, "T", self.marked([(135, "infinity")], words=[(135, "WORD")]))
        self.assertIn("WORD", msg)
        self.assertIn("infinity", msg)
        # Its box has its length: marks 12 degrees apart (3.4 mm between
        # centres, with boxes 3.1 mm long) fail, and 40 degrees apart pass.
        msg = failure(self, "T", self.marked([(100, "infinity"), (112, "infinity")]))
        self.assertIn("infinity", msg)
        g.check_panel("T", self.marked([(100, "infinity"), (140, "infinity")]))
        # Out at the rim, past the face, it fails too.
        msg = failure(self, "T", self.marked([(135, "infinity")], words_r=19.0))
        self.assertIn("infinity", msg)

    def test_unknown_mark_rejected(self):
        msg = failure(self, "T", self.marked([(0, "lemniscate")]))
        self.assertIn("lemniscate", msg)

    def test_marks_key_accepted(self):
        self.assertIn("marks", g.FACE_KEYS)


class Screws(unittest.TestCase):
    """Rack's screws, at the positions the description gives, keep clear of
    everything on the panel."""

    def test_screws_sit_where_rack_puts_them(self):
        # Rack's template: a 15 px screw one grid step in from the left edge
        # or two from the right, against the rail.
        self.assertEqual(g.screws_at(10, "tl", "tr", "bl", "br"),
                         [(7.62, 2.54), (43.18, 2.54), (7.62, 125.96), (43.18, 125.96)])
        with self.assertRaises(SystemExit) as cm:
            g.screws_at(10, "top")
        self.assertIn("'top'", cm.exception.code)

    def test_screws_clear_of_everything_pass(self):
        g.check_panel("T", spec(screws=g.screws_at(12, "tl", "tr", "bl", "br"), components=[("input", "J", 30.0, 60.0, None)]))

    def test_a_screw_over_a_part_fails_naming_it(self):
        screw = [(20.0, 60.0)]
        cases = (
            (dict(components=[("input", "J", 20.0, 66.0, None)]), "INPUT_J"),
            (dict(components=[("param", "R", 20.0, 68.0, None)], sizes={"PARAM_R": "face"},
                  faces={"PARAM_R": dict(r=7.3, ticks=20, major=5)}), "dial face of PARAM_R"),
            (dict(components=[("output", "X", 20.0, 68.0, "X")], tiles=[dict(port="OUTPUT_X")]),
             "tile of OUTPUT_X"),
            (dict(notes=[(20.0, 63.0, "NOTE", 2.0, "dim")]), 'note "NOTE"'),
            (dict(limbs=[dict(c=(20.0, 20.0), r=41.0, a0=-20, a1=20, step=2, major=5)]), "limb"),
            (dict(origins=[(20.0, 63.5)]), "origin"),
            (dict(screens=[("S", 10.0, 62.0, 20.0, 10.0)]), "SCREEN_S"),
            (dict(components=[("input", "A", 22.0, 90.0, None), ("param", "B", 22.0, 40.0, None)],
                  traces=[["INPUT_A", "PARAM_B"]]), "trace INPUT_A > PARAM_B"),
        )
        for extra, what in cases:
            msg = failure(self, "T", spec(screws=screw, **extra))
            self.assertIn("screw at (20.0, 60.0)", msg, what)
            self.assertIn(what, msg)

    def test_the_foot_mark_and_a_screw(self):
        g.check_panel("T", spec(screws=[(45.0, 125.96)]))
        msg = failure(self, "T", spec(screws=[(38.0, 125.96)]))
        self.assertIn("foot mark", msg)
        self.assertIn("screw", msg)

    def test_a_screw_off_the_panel_fails(self):
        msg = failure(self, "T", spec(screws=[(1.0, 60.0)]))
        self.assertIn("screw at (1.0, 60.0)", msg)
        self.assertIn("panel", msg)

    def test_layout_header_lists_the_screws(self):
        hpp = g.layout_hpp("T", spec(screws=g.screws_at(10, "tl", "br")))
        self.assertIn("static const rack::math::Vec SCREWS[] = {rack::math::Vec(7.62f, 2.54f), "
                      "rack::math::Vec(43.18f, 125.96f)};", hpp)
        self.assertNotIn("SCREWS", g.layout_hpp("T", spec()))

    def test_screw_artwork(self):
        for suffix, pal in g.INSTRUMENT.items():
            w, h, els = g.component_art(pal)["Screw"]
            self.assertEqual((w, h), (g.HP, g.HP))        # Rack's 15 px screw
            head = [e for e in els if e.startswith("<circle")]
            self.assertEqual(len(head), 1)
            self.assertIn(f'r="{g.num(g.SCREW_R)}" fill="{pal["knob"]}"', head[0])
            slot = [e for e in els if "<path" in e]
            self.assertEqual(len(slot), 1)
            self.assertIn(f'stroke="{pal["brass"]}"', slot[0])
            ends = [float(v) for v in re.findall(r"[-\d.]+", slot[0].split('d="')[1].split('"')[0])]
            for x, y in zip(ends[::2], ends[1::2]):
                self.assertLess(math.hypot(x - w / 2, y - h / 2), g.SCREW_R)


class Descriptions(unittest.TestCase):
    def test_misspelt_keys_fail_naming_the_key(self):
        comps = [("param", "R", 30.0, 40.0, None), ("output", "X", 30.0, 100.0, "X")]
        good = dict(components=comps, sizes={"PARAM_R": "hero"},
                    faces={"PARAM_R": dict(r=16.0, ticks=50, major=5)}, tiles=[dict(port="OUTPUT_X")])
        g.check_panel("T", spec(**good))
        cases = (
            (dict(good, faces={"PARAM_R": dict(r=16.0, ticks=50, major=5, tick_radius=11.0)}), "tick_radius", "PARAM_R"),
            (dict(good, tiles=[dict(port="OUTPUT_X", botom=7.0)]), "botom", "OUTPUT_X"),
            (dict(good, label_colur={"PARAM_R": "dim"}), "label_colur", "panel"),
            (dict(good, limbs=[dict(c=(30.0, 0.0), r=20, a0=0, a1=9, step=1, major=5, minor=2)]), "minor", "limb"),
            (dict(good, screens=[("S", 5.0, 60.0, 20.8, 10.4)], rulers={"S": dict(px=0.65, side="b")}), "side", "SCREEN_S"),
            (dict(good, preview={"PARAM_R": dict(turned=30)}), "turned", "PARAM_R"),
        )
        for bad, key, where in cases:
            msg = failure(self, "T", spec(**bad))
            self.assertIn(repr(key), msg)
            self.assertIn(where, msg)
            self.assertIn("unknown key", msg)

    def test_missing_required_key_fails(self):
        s = spec(components=[("param", "R", 30.0, 40.0, None)], sizes={"PARAM_R": "hero"},
                 faces={"PARAM_R": dict(r=16.0)})
        self.assertIn("'ticks'", failure(self, "T", s))

    def test_missing_inkscape_is_one_sentence(self):
        for missing in ("/no/such/folder/inkscape", "inkscape-that-is-not-installed"):
            with self.assertRaises(SystemExit) as cm:
                g.need_inkscape(missing)
            self.assertIn("Inkscape is needed", cm.exception.code)
            self.assertIn(missing, cm.exception.code)
            self.assertNotIn("\n", cm.exception.code)
        self.assertEqual(g.need_inkscape(sys.executable), sys.executable)


class InkscapeVersion(unittest.TestCase):
    """The committed panels were made with one Inkscape; another is reported,
    not refused."""

    def test_the_version_is_read_from_its_banner(self):
        self.assertEqual(g.inkscape_version("Inkscape 1.4.4 (dcaf3e7, 2026-05-05)\n"), "1.4.4")
        self.assertEqual(g.inkscape_version("Pango version: 1.50\nInkscape 1.2.2 (b0a8486541, 2022-12-01)"), "1.2.2")
        self.assertIsNone(g.inkscape_version("something else"))

    def test_the_same_version_is_quiet_and_another_warns(self):
        self.assertEqual(g.INKSCAPE_VERSION, "1.4.4")
        self.assertIsNone(g.inkscape_warning("1.4.4"))
        for other in ("1.4.3", "1.5.0", None):
            msg = g.inkscape_warning(other)
            self.assertIn("warning", msg)
            self.assertIn("1.4.4", msg)
            self.assertIn(str(other) if other else "could not tell", msg)


class TextEncoding(unittest.TestCase):
    def test_every_text_read_and_write_names_utf8(self):
        # The foot mark holds "\u00b7"; a platform's default encoding must not
        # decide how it is written or read.
        import ast
        for path in (ROOT / "tools" / "gen_panels.py", Path(__file__).resolve()):
            calls = 0
            for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
                if not isinstance(node, ast.Call):
                    continue
                f = node.func
                called = f.attr if isinstance(f, ast.Attribute) else getattr(f, "id", None)
                if called not in ("write_text", "read_text", "open"):
                    continue
                calls += 1
                enc = [k.value.value for k in node.keywords
                       if k.arg == "encoding" and isinstance(k.value, ast.Constant)]
                self.assertEqual(enc, ["utf-8"], f"{path.name}:{node.lineno}: {called} without encoding=\"utf-8\"")
            self.assertGreater(calls, 3, path)


class Artwork(unittest.TestCase):
    def all_svgs(self):
        out = dict(g.component_files())
        for suffix, t in g.INSTRUMENT.items():
            out[f"Specimen{suffix}.svg"] = g.panel_svg("Specimen", g.SPECIMEN, t)
        return out

    def test_nothing_nanosvg_cannot_draw(self):
        for name, svg in self.all_svgs().items():
            for banned in ("dasharray", "filter", "<style", "class=", "<use", "<mask", "clip-path"):
                self.assertNotIn(banned, svg, name)
        for name, svg in g.component_files().items():
            self.assertNotIn("<text", svg, name)

    def test_knurl_is_individual_ticks_evenly_spaced(self):
        art = g.component_art(g.INSTRUMENT[""])
        for name in ("HeroKnob", "DialKnob", "FaceKnob", "LimbKnob", "Atten", "Encoder"):
            w, _, els = art[name]
            knurl = [e for e in els if 'data-part="knurl"' in e]
            self.assertEqual(len(knurl), 1, name)
            ticks = re.findall(r"M([-\d.]+) ([-\d.]+)L([-\d.]+) ([-\d.]+)", knurl[0])
            self.assertGreater(len(ticks), 20, name)
            angles = sorted(math.atan2(float(x1) - w / 2, w / 2 - float(y1)) % (2 * math.pi) for x1, y1, _, _ in ticks)
            steps = [b - a for a, b in zip(angles, angles[1:])]
            self.assertLess(max(steps) - min(steps), 0.01, name)
            self.assertAlmostEqual(steps[0], 2 * math.pi / len(ticks), places=2, msg=name)

    def test_encoder_knurl_does_not_repeat_within_a_turn(self):
        count = g.KNURL_TICKS["Encoder"]
        self.assertEqual(math.gcd(count, 24), 1)      # 24 detents a turn
        shift = (count / 24) % 1                      # of a tick, per detent
        self.assertGreater(shift, 0.25)
        self.assertLess(shift, 0.4)                   # under half: reads as forward
        _, _, els = g.component_art(g.INSTRUMENT[""])["Encoder"]
        self.assertEqual(len(re.findall(r"M[-\d.]+ ", [e for e in els if "knurl" in e][0])), count)

    def test_encoder_face_has_one_even_tick_per_detent(self):
        face = g.SPECIMEN["faces"]["WIDGET_ENC"]
        self.assertEqual([major for _, major in g.face_ticks(face)], [False] * 24)

    def test_fathoms_pitch_scale_is_as_long_as_sways(self):
        # On the dial-size knob the 13.0 mm face holds the default scale:
        # the same tick lengths and numeral size as Sway's Rate.
        pitch, rate = g.PANELS["Fathom"]["faces"]["PARAM_PITCH"], g.PANELS["Sway"]["faces"]["PARAM_RATE"]
        self.assertEqual(g.component_size(g.PANELS["Fathom"], "Fathom", "param", "PITCH"), "dial")
        for key in ("tick_r", "minor_len", "major_len", "numeral_r", "numeral_size"):
            self.assertNotIn(key, pitch, key)
            self.assertNotIn(key, rate, key)
        self.assertGreaterEqual(pitch["r"] - g.RADII["dial"], 6.0)

    def test_the_lit_cap_is_one_centred_disc(self):
        w, h, els = g.component_art(g.INSTRUMENT[""])["Cap_lit"]
        self.assertEqual(len(els), 1)
        self.assertIn(f'cx="{g.num(w / 2)}" cy="{g.num(h / 2)}"', els[0])

    def test_dark_variants_exist_and_differ(self):
        files = g.component_files()
        for name in ("HeroKnob", "HeroKnob_bg", "DialKnob", "DialKnob_bg", "FaceKnob", "LimbKnob", "Atten", "Encoder", "BrassPort",
                     "Toggle2_0", "Toggle2_1", "Toggle3_0", "Toggle3_1", "Toggle3_2", "Cap", "JewelSeat", "Screw"):
            self.assertIn(f"{name}.svg", files)
            self.assertIn(f"{name}-dark.svg", files)
            self.assertNotEqual(files[f"{name}.svg"], files[f"{name}-dark.svg"], name)
        self.assertIn("Cap_lit.svg", files)

    def test_specimen_passes_its_checks_and_uses_everything(self):
        g.check_panel("Specimen", g.SPECIMEN)
        sizes = {g.component_size(g.SPECIMEN, "Specimen", kind, cname) for kind, cname, *_ in g.SPECIMEN["components"]}
        self.assertEqual(sizes, set(g.ART_LAYERS))
        for key in ("origins", "circles", "radii", "limbs", "faces", "tiles", "traces", "screens", "rulers", "notes"):
            self.assertTrue(g.SPECIMEN.get(key), key)
        elbows = [sum(1 for n in tr if not isinstance(n, str)) for tr in g.SPECIMEN["traces"]]
        self.assertIn(0, elbows)
        self.assertIn(1, elbows)

    def test_hidden_components_layer_and_layout_header(self):
        svg = g.panel_svg("Specimen", g.SPECIMEN, g.INSTRUMENT[""])
        hpp = g.layout_hpp("Specimen", g.SPECIMEN)
        self.assertIn('inkscape:label="components" id="components" style="display:none"', svg)
        for kind, cname, x, y, _ in g.SPECIMEN["components"]:
            self.assertIn(f'<circle id="{kind}-{cname}" cx="{x}" cy="{y}" r="1" fill="{g.KIND_COLOUR[kind]}"/>', svg)
            self.assertIn(f"static const rack::math::Vec {kind.upper()}_{cname}({x}f, {y}f);", hpp)
        for sname, *_ in g.SPECIMEN["screens"]:
            self.assertIn(f"SCREEN_{sname}(", hpp)

    def test_specimen_is_not_a_module(self):
        self.assertNotIn("Specimen", g.PANELS)


class ShoalPanel(unittest.TestCase):
    """What Shoal's layout promises, whatever its positions are tuned to."""

    def setUp(self):
        self.spec = g.PANELS["Shoal"]
        self.by = {c.id: c for c in g.comps_of("Shoal", self.spec)}

    def away(self, a, b):
        return math.hypot(self.by[a].x - self.by[b].x, self.by[a].y - self.by[b].y)

    def test_screens_keep_their_pixel_size(self):
        screens = {name: (w, h) for name, _, _, w, h in self.spec["screens"]}
        self.assertEqual(screens, {"STRIP": (83.2, 5.0), "SCOPE": (83.2, 41.6), "EDITOR": (83.2, 41.6)})
        self.assertAlmostEqual(128 * 0.65, 83.2)
        self.assertAlmostEqual(64 * 0.65, 41.6)
        for name in ("SCOPE", "EDITOR"):
            self.assertEqual(self.spec["rulers"][name]["px"], 0.65)
        # Numbered 0 to 128 under the editor only.
        numerals = [t.s for t in g.instrument_texts("Shoal", self.spec) if "under SCREEN_" in t.what]
        self.assertEqual(numerals, ["0", "32", "64", "96", "128"])
        self.assertTrue(all("SCREEN_EDITOR" in t.what for t in g.instrument_texts("Shoal", self.spec)
                            if "under SCREEN_" in t.what))

    def test_channel_letters_sit_over_the_strips_four_cells(self):
        _, x, y, w, _ = next(s for s in self.spec["screens"] if s[0] == "STRIP")
        letters = {n[2]: n for n in self.spec["notes"] if n[2] in "ABCD" and len(n[2]) == 1}
        for ch, letter in enumerate("ABCD"):
            self.assertAlmostEqual(letters[letter][0], x + w * (ch + 0.5) / 4, places=6)
            self.assertLess(letters[letter][1], y)

    def test_each_push_is_the_same_distance_from_its_encoder_and_joined_to_it(self):
        left, right = self.away("PARAM_ENC_L_PUSH", "WIDGET_ENC_L"), self.away("PARAM_ENC_R_PUSH", "WIDGET_ENC_R")
        self.assertAlmostEqual(left, right, places=2)
        self.assertLess(abs(left - 16.5), 1.0)
        self.assertIn(["PARAM_ENC_L_PUSH", "WIDGET_ENC_L"], self.spec["traces"])
        self.assertIn(["PARAM_ENC_R_PUSH", "WIDGET_ENC_R"], self.spec["traces"])
        # Each push is nearer its own encoder than the other one.
        self.assertGreater(self.away("PARAM_ENC_L_PUSH", "WIDGET_ENC_R"), left + 5)
        self.assertGreater(self.away("PARAM_ENC_R_PUSH", "WIDGET_ENC_L"), right + 5)

    def test_the_sources_are_not_level_and_ripple_across_the_panel(self):
        sources = [(self.by[i].x, self.by[i].y) for i in ("WIDGET_ENC_L", "WIDGET_ENC_R")]
        self.assertNotEqual(sources[0][1], sources[1][1])
        w = self.spec["hp"] * g.HP
        far = max(math.hypot(x - cx, y - cy) for cx, cy in sources for x in (0, w) for y in (0, g.PANEL_H))
        for src in sources:
            radii = sorted(r for cx, cy, r in self.spec["circles"] if (cx, cy) == src)
            self.assertEqual(radii[:3], [16, 26, 36])
            self.assertTrue(all(b - a == 10 for a, b in zip(radii, radii[1:])))
        self.assertGreaterEqual(max(r for _, _, r in self.spec["circles"]), far - 10)
        self.assertFalse(self.spec.get("limbs"))

    def test_encoder_faces_have_one_even_tick_per_detent(self):
        for enc in ("WIDGET_ENC_L", "WIDGET_ENC_R"):
            ticks = g.face_ticks(self.spec["faces"][enc])
            self.assertEqual([major for _, major in ticks], [False] * 24)
            self.assertEqual(self.by[enc].size, "enc")

    def test_up_is_stacked_over_down_on_no_arc_and_apart_from_the_rest(self):
        up, down = self.by["PARAM_UP"], self.by["PARAM_DOWN"]
        self.assertEqual(up.x, down.x)
        self.assertLess(up.y, down.y)
        self.assertEqual(self.spec["label_at"]["PARAM_UP"], "right")
        self.assertEqual(self.spec["label_at"]["PARAM_DOWN"], "right")
        pair = self.away("PARAM_UP", "PARAM_DOWN")
        for b in ("PARAM_UP", "PARAM_DOWN"):
            # No ripple runs through the button's centre: it is on no arc.
            for cx, cy, r in self.spec["circles"]:
                self.assertGreater(abs(math.hypot(self.by[b].x - cx, self.by[b].y - cy) - r), 1.5, (b, cx, cy, r))
            # Farther from every encoder and push than from its partner.
            for other in ("WIDGET_ENC_L", "WIDGET_ENC_R", "PARAM_ENC_L_PUSH", "PARAM_ENC_R_PUSH", "PARAM_UI_MODE"):
                reach = g.edge_radius(self.spec, self.by[other]) - self.by[other].r
                self.assertGreater(self.away(b, other) - reach, pair + 1.0, (b, other))

    def test_room_to_click_round_ui_mode(self):
        # At least 2 mm of ground between UI MODE's cap and every other control.
        ui = self.by["PARAM_UI_MODE"]
        for c in self.by.values():
            if c.id != ui.id and c.kind in ("param", "widget"):
                self.assertGreaterEqual(self.away(ui.id, c.id) - ui.r - g.edge_radius(self.spec, c), 2.0, c.id)

    def test_every_output_is_on_a_tile_with_its_letter_and_its_light(self):
        tiles = {t["port"]: t for t in self.spec["tiles"]}
        for n in "ABCD":
            self.assertEqual(tiles[f"OUTPUT_{n}"]["light"], f"LIGHT_{n}")
        names = [t for t in g.instrument_texts("Shoal", self.spec) if t.what in [f'label "{n}"' for n in "ABCD"]]
        self.assertEqual(len(names), 4)
        self.assertTrue(all(t.colour == "ink" for t in names))

    def test_component_order_is_the_layout_headers(self):
        # The widget reads these constants; none may vanish.
        ids = [c.id for c in g.comps_of("Shoal", self.spec)]
        want = (["WIDGET_ENC_L", "WIDGET_ENC_R", "PARAM_ENC_L_PUSH", "PARAM_ENC_R_PUSH", "PARAM_UP", "PARAM_DOWN",
                 "PARAM_UI_MODE"] + [f"INPUT_TR{i}" for i in "1234"] + [f"LIGHT_TR{i}" for i in "1234"]
                + [f"INPUT_CV{i}" for i in "1234"] + [f"LIGHT_{n}" for n in "ABCD"] + [f"OUTPUT_{n}" for n in "ABCD"])
        self.assertEqual(sorted(ids), sorted(want))


SVG_NS = "{http://www.w3.org/2000/svg}"


def folder_mismatch(folder, want):
    """What is wrong with a generated folder's contents, or None: the files
    the generator writes that are missing, and any other file in it. Dotfiles
    (a Finder .DS_Store) are ignored: .gitignore hides them, and they are
    not the generator's."""
    have = {p.name for p in folder.iterdir() if not p.name.startswith(".")}
    missing, extra = sorted(set(want) - have), sorted(have - set(want))
    if not missing and not extra:
        return None
    return (f"{folder}: " + "; ".join(([f"missing {', '.join(missing)}"] if missing else [])
                                      + ([f"not written by the generator: {', '.join(extra)}"] if extra else [])))


class FolderContents(unittest.TestCase):
    def test_dotfiles_are_ignored_and_any_other_extra_file_is_named(self):
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            (folder / "A.svg").write_text("", encoding="utf-8")
            (folder / ".DS_Store").write_text("", encoding="utf-8")
            self.assertIsNone(folder_mismatch(folder, {"A.svg"}))
            (folder / "Old.svg").write_text("", encoding="utf-8")
            (folder / "notes.txt").write_text("", encoding="utf-8")
            msg = folder_mismatch(folder, {"A.svg", "B.svg"})
            for part in ("missing B.svg", "Old.svg", "notes.txt"):
                self.assertIn(part, msg)
            self.assertNotIn(".DS_Store", msg)


def drawing(svg_text):
    """A panel SVG as a list of what it draws, in order, for comparing a
    generated source with Inkscape's output: each element as (tag,
    attributes but its id and any namespaced ones), and each piece of
    lettering as ("text", the text, colour, size, weight, first point or
    None). In Inkscape's output lettering is an outline: a <path> carrying
    the text as aria-label, the rest in its style, its first point where
    the outline starts. A <text>'s first point is None."""
    out = []
    for el in ET.fromstring(svg_text).iter():
        tag = el.tag.replace(SVG_NS, "")
        attrs = {k: v for k, v in el.attrib.items() if k != "id" and not k.startswith("{")}
        if tag == "svg":
            out.append((tag, {k: attrs[k] for k in ("width", "height", "viewBox")}))
        elif tag == "defs" and len(el) == 0:
            continue    # Inkscape adds an empty one
        elif tag == "text":
            out.append(("text", el.text, attrs["fill"], float(attrs["font-size"]), attrs["font-weight"], None))
        elif "aria-label" in attrs:
            style = dict(kv.split(":", 1) for kv in attrs["style"].split(";"))
            first = re.match(r"\s*[mM]\s*([-\d.]+)[ ,]([-\d.]+)", attrs["d"])
            point = (float(first[1]), float(first[2]))
            # A turned word keeps its turn as a transform: the point is where it ends up.
            turn = re.fullmatch(r"rotate\(([-\d.]+)[ ,]+([-\d.]+)[ ,]+([-\d.]+)\)", attrs.get("transform", ""))
            if turn:
                a, cx, cy = (float(v) for v in turn.groups())
                sa, ca = math.sin(math.radians(a)), math.cos(math.radians(a))
                point = (cx + (point[0] - cx) * ca - (point[1] - cy) * sa,
                         cy + (point[0] - cx) * sa + (point[1] - cy) * ca)
            out.append(("text", attrs["aria-label"], style["fill"], float(style["font-size"].rstrip("px")),
                        style.get("font-weight", "normal"), point))
        else:
            out.append((tag, attrs))
    return out


class Committed(unittest.TestCase):
    """Every generated file in the repository is exactly what the generator
    writes now, so a hand edit or an output left stale fails here.

    Byte for byte, offline and without Inkscape: res-src/<dir>/<Name>.svg
    and <Name>-dark.svg, src/<dir>/<Name>Layout.hpp, src/RkaTheme.hpp, and
    every file in res/components/ (with nothing there it does not write).

    res/<dir>/*.svg is Inkscape's output (the lettering turned to outlines)
    and cannot be made here. Offline, each must draw what its source draws,
    element for element and attribute for attribute (ids aside), with an
    outline of the same text, colour, size and weight for each piece of
    lettering, starting inside that lettering's box. Not covered without
    Inkscape: the outlines themselves (the glyphs' shapes, and where within
    its box each one lies). For those, run `make panels` and see that
    `git status` shows nothing.
    """

    def test_panel_sources_and_layout_headers_are_current(self):
        for name, s in g.PANELS.items():
            folder = ROOT / "res-src" / s["dir"]
            self.assertEqual({p.name for p in folder.glob("*.svg")}, {f"{name}{sfx}.svg" for sfx in g.INSTRUMENT})
            for suffix, pal in g.INSTRUMENT.items():
                path = folder / f"{name}{suffix}.svg"
                self.assertEqual(g.panel_svg(name, s, pal), path.read_text(encoding="utf-8"), path)
            path = ROOT / "src" / s["dir"] / f"{name}Layout.hpp"
            self.assertEqual(g.layout_hpp(name, s), path.read_text(encoding="utf-8"), path)

    def test_layout_headers_carry_no_float_noise(self):
        # A position is a few decimals of a millimetre, never 125.60000000000001.
        for name, s in g.PANELS.items():
            for value in re.findall(r"([-\d.]+)f\b", g.layout_hpp(name, s)):
                self.assertLessEqual(len(value.partition(".")[2]), 3, f"{name}: {value}")

    def test_finished_panels_draw_what_their_sources_draw(self):
        folders = {p.name for p in (ROOT / "res").iterdir() if p.is_dir()}
        self.assertEqual(folders, {s["dir"] for s in g.PANELS.values()} | {"components"})
        for name, s in g.PANELS.items():
            folder = ROOT / "res" / s["dir"]
            mismatch = folder_mismatch(folder, {f"{name}{sfx}.svg" for sfx in g.INSTRUMENT})
            self.assertIsNone(mismatch, mismatch)
            # A drawn mark is a path, not lettering: the comparison below covers it.
            boxes = [g.text_box(t) for t in g.instrument_texts(name, s) + [g.foot_mark(s)]
                     if not isinstance(t, g.Mark)]
            for suffix, pal in g.INSTRUMENT.items():
                path = folder / f"{name}{suffix}.svg"
                want, got = drawing(g.panel_svg(name, s, pal)), drawing(path.read_text(encoding="utf-8"))
                self.assertEqual(len(got), len(want), f"{path}: not what the generator draws")
                lettering = 0
                for w, h in zip(want, got):
                    if w[0] == "text":
                        self.assertEqual(h[:5], w[:5], path)
                        x0, y0, x1, y1 = boxes[lettering]
                        x, y = h[5]
                        self.assertTrue(x0 - 0.5 <= x <= x1 + 0.5 and y0 - 0.5 <= y <= y1 + 0.5,
                                        f"{path}: {w[1]!r} starts at ({x}, {y}), outside its box")
                        lettering += 1
                    else:
                        self.assertEqual(h, w, path)
                self.assertEqual(lettering, len(boxes), path)

    def test_theme_header_is_current_and_holds_only_the_palette(self):
        hpp = g.theme_hpp()
        self.assertEqual(hpp, (ROOT / "src" / "RkaTheme.hpp").read_text(encoding="utf-8"))
        consts = re.findall(r"NVGcolor (\w+) = ", hpp)
        want = [const + tag for const, _ in g.INSTRUMENT_WIDGET_COLOURS for tag in ("", "_DARK")]
        self.assertEqual(consts, want)
        self.assertIn("static const NVGcolor BRASS = nvgRGB(0xc9, 0xa4, 0x6a);", hpp)

    def test_component_artwork_is_current(self):
        files = g.component_files()
        folder = ROOT / "res" / "components"
        mismatch = folder_mismatch(folder, set(files))
        self.assertIsNone(mismatch, mismatch)
        for fname, svg in files.items():
            self.assertEqual(svg, (folder / fname).read_text(encoding="utf-8"), fname)

if __name__ == "__main__":
    unittest.main()

#pragma once
// Shoal's upper screen (docs/specs/2026-11-19-shoal-scope-design.md). In
// hardware mode: the firmware's scope frame, as rka::PixelScreen draws it.
// In friendly mode: that frame with lane dividers and letters, or, for the
// views Shoal draws itself, smooth lines from the published ScopeView and
// ScopeHistory. All geometry is ScopeLayout.hpp's; this only strokes it.
#include <functional>
#include <memory>

#include <rack.hpp>

#include "RkaTheme.hpp"
#include "ScopeLayout.hpp"
#include "ScopeView.hpp"
#include "widgets.hpp"

namespace shoal {

struct ScopeScreen : rka::PixelScreen {
	// Fills the frame (PixelScreen::BYTES), the view and the history, all
	// of one publish; false: nothing to draw. In hardware mode the frame
	// comes from PixelScreen's `source` alone.
	std::function<bool(uint8_t* frame, ScopeView&, ScopeHistory&)> screenSource;
	bool friendly = true;

	ScopeView view = ScopeView();
	// 16 kB: on the heap with the widget, never on the stack.
	std::unique_ptr<ScopeHistory> history{new ScopeHistory()};

	static NVGcolor signal(float alpha) {
		return nvgTransRGBAf(RkaTheme::SIGNAL, alpha);
	}

	// One device pixel, in this widget's units (from the current transform's
	// area scale).
	static float hair(const DrawArgs& args) {
		float m[6];
		nvgCurrentTransform(args.vg, m);
		const float area = std::fabs(m[0] * m[3] - m[1] * m[2]);
		return area > 0.f ? 1.f / std::sqrt(area) : 1.f;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) {
			Widget::drawLayer(args, layer);
			return;
		}
		if (!friendly || !screenSource) {
			rka::PixelScreen::drawLayer(args, layer);
			return;
		}
		// Frame, view and history of one publish, so the lanes and letters
		// are always those of the app whose pixels are shown.
		if (!screenSource(frame, view, *history)) {
			Widget::drawLayer(args, layer);
			return;
		}
		if (view.kind == ViewKind::Pixels)
			drawFrame(args);
		const float w = box.size.x, h = box.size.y;
		nvgSave(args.vg);
		nvgIntersectScissor(args.vg, 0.f, 0.f, w, h);
		const scope::Lanes4 l = scope::lanes(view, w, h);
		switch (view.kind) {
			case ViewKind::Traces: drawTraces(args, l); break;
			case ViewKind::XY: drawXy(args, l); break;
			case ViewKind::GridCircle: drawGridCircle(args, w, h); break;
			case ViewKind::CircleText: drawCircleText(args, w, h); break;
			default: break;
		}
		drawLanes(args, l, h);
		nvgRestore(args.vg);
		Widget::drawLayer(args, layer);
	}

	std::shared_ptr<rack::window::Font> font() {
		return APP->window->loadFont(rack::asset::system("res/fonts/ShareTechMono-Regular.ttf"));
	}

	// The stroke of a drawn line: a firmware pixel's worth, never under 1.2
	// device pixels.
	float lineWidth(const DrawArgs& args) const {
		const float px = scope::pixel(box.size.y) * 0.55f, least = hair(args) * 1.2f;
		return px > least ? px : least;
	}

	void drawLanes(const DrawArgs& args, const scope::Lanes4& l, float h) {
		if (!l.count)
			return;
		nvgBeginPath(args.vg);
		for (int d = 0; d < l.dividers; d++) {
			nvgMoveTo(args.vg, l.divider[d].a.x, l.divider[d].a.y);
			nvgLineTo(args.vg, l.divider[d].b.x, l.divider[d].b.y);
		}
		nvgStrokeColor(args.vg, signal(0.4f));
		nvgStrokeWidth(args.vg, hair(args));
		nvgStroke(args.vg);
		std::shared_ptr<rack::window::Font> f = font();
		if (!f)
			return;
		nvgFontFaceId(args.vg, f->handle);
		nvgFontSize(args.vg, scope::labelSize(h));
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		// Over Viznutcracker's bands, which fill the screen, a letter needs
		// a plate of glass behind it to be read.
		if (view.kind == ViewKind::Pixels && view.lanes == Lanes::Rows4) {
			const float px = scope::pixel(h), size = scope::labelSize(h);
			nvgBeginPath(args.vg);
			for (int i = 0; i < l.count; i++)
				nvgRect(args.vg, l.label[i].at.x - px, l.label[i].at.y - px * 0.5f, size * 0.62f + px * 2.f, size + px * 0.5f);
			nvgFillColor(args.vg, nvgTransRGBAf(RkaTheme::GLASS, 0.9f));
			nvgFill(args.vg);
		}
		nvgFillColor(args.vg, signal(view.kind == ViewKind::Pixels && view.lanes == Lanes::Rows4 ? 0.9f : 0.65f));
		for (int i = 0; i < l.count; i++)
			nvgText(args.vg, l.label[i].at.x, l.label[i].at.y, l.label[i].text, NULL);
	}

	void drawTraces(const DrawArgs& args, const scope::Lanes4& l) {
		const int n = history->columns;
		const float lw = lineWidth(args);
		for (int ch = 0; ch < l.count && ch < kScopeChannels; ch++) {
			const scope::Box& lane = l.lane[ch];
			// 0 V.
			const float zy = scope::traceZeroY(lane, view);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, scope::traceLeft(lane), zy);
			nvgLineTo(args.vg, lane.right(), zy);
			nvgStrokeColor(args.vg, signal(0.15f));
			nvgStrokeWidth(args.vg, hair(args));
			nvgStroke(args.vg);
			if (n < 1)
				continue;
			// Where a column's swing is wider than the line, a band from its
			// lowest to its highest: what happened inside the column shows.
			nvgBeginPath(args.vg);
			bool band = false;
			for (int c = 0; c < n; c++) {
				const scope::Point lo = scope::tracePoint(lane, view, c, n, history->lo[ch][c]);
				const scope::Point hi = scope::tracePoint(lane, view, c, n, history->hi[ch][c]);
				if (lo.y - hi.y > lw) {
					const float pitch = c + 1 < n ? scope::tracePoint(lane, view, c + 1, n, 0.f).x - lo.x
					                              : lo.x - scope::tracePoint(lane, view, c - 1, n, 0.f).x;
					nvgRect(args.vg, lo.x - pitch / 2.f, hi.y, pitch > 0.f ? pitch : lw, lo.y - hi.y);
					band = true;
				}
			}
			if (band) {
				nvgFillColor(args.vg, signal(0.55f));
				nvgFill(args.vg);
			}
			// The line, through each column's middle.
			nvgBeginPath(args.vg);
			for (int c = 0; c < n; c++) {
				const scope::Point p = scope::tracePoint(lane, view, c, n, (history->lo[ch][c] + history->hi[ch][c]) / 2.f);
				if (c == 0)
					nvgMoveTo(args.vg, p.x, p.y);
				else
					nvgLineTo(args.vg, p.x, p.y);
			}
			nvgLineJoin(args.vg, NVG_ROUND);
			nvgStrokeColor(args.vg, signal(1.f));
			nvgStrokeWidth(args.vg, lw);
			nvgStroke(args.vg);
		}
	}

	void drawXy(const DrawArgs& args, const scope::Lanes4& l) {
		const int n = history->points;
		if (n < 2)
			return;
		const float lw = lineWidth(args);
		const int steps = 8;
		for (int half = 0; half < l.count && half < 2; half++) {
			const float* xs = history->xy[half * 2];
			const float* ys = history->xy[half * 2 + 1];
			// Oldest to newest in steps of rising brightness; each step
			// starts on the last point of the one before.
			for (int s = 0; s < steps; s++) {
				const int a = (n - 1) * s / steps, b = (n - 1) * (s + 1) / steps;
				if (b <= a)
					continue;
				nvgBeginPath(args.vg);
				for (int i = a; i <= b; i++) {
					const scope::Point p = scope::xyPoint(l.lane[half], xs[i], ys[i]);
					if (i == a)
						nvgMoveTo(args.vg, p.x, p.y);
					else
						nvgLineTo(args.vg, p.x, p.y);
				}
				nvgLineJoin(args.vg, NVG_ROUND);
				nvgLineCap(args.vg, NVG_ROUND);
				nvgStrokeColor(args.vg, signal(scope::xyAlpha(b, n)));
				nvgStrokeWidth(args.vg, lw);
				nvgStroke(args.vg);
			}
		}
	}

	// The circle of twelve notes with the chord's triangle, inside `area`.
	void drawCircle(const DrawArgs& args, const scope::Box& area, float h) {
		const float lw = lineWidth(args);
		const float disc = scope::pixel(h) * 2.6f;
		const scope::Point c = {area.x + area.w / 2.f, area.y + area.h / 2.f};
		const float r = area.w / 2.f - disc;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c.x, c.y, r);
		nvgStrokeColor(args.vg, signal(0.7f));
		nvgStrokeWidth(args.vg, lw);
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		for (int n = 0; n < 12; n++) {
			const scope::Point p = scope::circleNote(c, r, n);
			nvgCircle(args.vg, p.x, p.y, lw);
		}
		nvgFillColor(args.vg, signal(0.7f));
		nvgFill(args.vg);
		scope::Point at[3];
		for (int i = 0; i < 3; i++)
			at[i] = scope::circleNote(c, r, view.notes[i] % 12);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, at[0].x, at[0].y);
		nvgLineTo(args.vg, at[1].x, at[1].y);
		nvgLineTo(args.vg, at[2].x, at[2].y);
		nvgClosePath(args.vg);
		nvgLineJoin(args.vg, NVG_ROUND);
		nvgStrokeColor(args.vg, signal(1.f));
		nvgStrokeWidth(args.vg, lw);
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		for (int i = 0; i < 3; i++)
			nvgCircle(args.vg, at[i].x, at[i].y, disc);
		nvgFillColor(args.vg, signal(1.f));
		nvgFill(args.vg);
		// Note names, inside the circle beside each disc.
		static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		std::shared_ptr<rack::window::Font> f = font();
		if (!f)
			return;
		nvgFontFaceId(args.vg, f->handle);
		nvgFontSize(args.vg, scope::labelSize(h));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, signal(0.85f));
		for (int i = 0; i < 3; i++) {
			const scope::Point p = scope::circleNote(c, r - disc - scope::labelSize(h) * 0.75f, view.notes[i] % 12);
			nvgText(args.vg, p.x, p.y, kNames[view.notes[i] % 12], NULL);
		}
	}

	void drawGridCircle(const DrawArgs& args, float w, float h) {
		const float lw = lineWidth(args);
		const scope::Box grid = scope::gridArea(w, h);
		nvgBeginPath(args.vg);
		for (int y = 0; y < kGridSide; y++)
			for (int x = 0; x < kGridSide; x++) {
				const scope::Point p = scope::gridCell(grid, x, y);
				nvgCircle(args.vg, p.x, p.y, lw);
			}
		nvgFillColor(args.vg, signal(0.35f));
		nvgFill(args.vg);
		scope::Point at[kViewPath];
		for (int i = 0; i < kViewPath; i++)
			at[i] = scope::gridCell(grid, view.pathX[i] % kGridSide, view.pathY[i] % kGridSide);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, at[0].x, at[0].y);
		for (int i = 1; i < kViewPath; i++)
			nvgLineTo(args.vg, at[i].x, at[i].y);
		nvgLineJoin(args.vg, NVG_ROUND);
		nvgStrokeColor(args.vg, signal(0.8f));
		nvgStrokeWidth(args.vg, lw);
		nvgStroke(args.vg);
		const float disc = scope::pixel(h) * 2.6f;
		// Older cells: rings, hollow. Then the newest: a disc, on top.
		for (int i = kViewPath - 1; i >= 1; i--) {
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, at[i].x, at[i].y, disc);
			nvgFillColor(args.vg, RkaTheme::GLASS);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, signal(1.f));
			nvgStrokeWidth(args.vg, lw);
			nvgStroke(args.vg);
		}
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, at[0].x, at[0].y, disc);
		nvgFillColor(args.vg, signal(1.f));
		nvgFill(args.vg);
		drawCircle(args, scope::circleArea(w, h, true), h);
	}

	void drawCircleText(const DrawArgs& args, float w, float h) {
		drawCircle(args, scope::circleArea(w, h, false), h);
		std::shared_ptr<rack::window::Font> f = font();
		if (!f)
			return;
		// Two columns, as the firmware lays them out: the last four
		// transforms, newest at the top; beside them the chord's notes.
		const scope::Box t = scope::textArea(w, h);
		const float size = h / 6.f, line = t.h / 4.f;
		nvgFontFaceId(args.vg, f->handle);
		nvgFontSize(args.vg, size);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		for (int i = 0; i < 4; i++) {
			nvgFillColor(args.vg, signal(i == 0 ? 1.f : 0.6f));
			nvgText(args.vg, t.x + t.w * 0.08f, t.y + line * (i + 0.5f), view.transforms[i], NULL);
		}
		nvgFillColor(args.vg, signal(1.f));
		for (int i = 0; i < 3; i++)
			nvgText(args.vg, t.x + t.w * 0.5f, t.y + line * (i + 0.5f), view.chord[i], NULL);
	}
};

} // namespace shoal

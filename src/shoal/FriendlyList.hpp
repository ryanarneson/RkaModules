#pragma once
// Shoal friendly list: the lower screen in friendly mode. Draws the model
// shoal::Host publishes (the current app's settings as rows) and turns mouse
// gestures into FriendlyEdits. Where things are and what a gesture means is
// worked out in ListLayout.hpp (Rack-free, tested offline); this file is the
// drawing and the Rack events.
//
// UI thread only. It calls nothing on the Host but model(), postEdit() and
// post(), and never touches the running core.
#include <rack.hpp>

#include "Detents.hpp"
#include "Host.hpp"
#include "ListLayout.hpp"
#include "RkaTheme.hpp"

namespace shoal {

struct FriendlyList : rack::widget::OpaqueWidget {
	// Rows scrolled per wheel notch.
	static constexpr float kRowsPerNotch = 2.f;
	// The longest dropdown offered as a menu.
	static constexpr int kMaxChoices = 1024;

	Host* host = nullptr;                  // where the model comes from; may be a preview host
	bool live = false;                     // false (no module): the list is drawn, nothing is clickable
	std::function<void()> onHardwareMode;  // switch UI MODE to hardware

	FriendlyModel model = {};
	int scroll = 0;
	list::Outbox outbox;

	// The pointer.
	bool hovering = false;
	rack::math::Vec hoverPos, pressPos;
	rka::DetentCounter wheel;

	// The drag in progress, and the row it was started on, as the model
	// showed it then.
	enum class Drag { None, Slider, Mask, Scrollbar, Action };
	Drag drag = Drag::None;
	int dragApp = -1, dragTab = -1, dragId = -1;
	rack::math::Vec dragPos;
	list::SliderDrag slider;
	float sliderWidth = 0.f;
	int32_t sliderMin = 0, sliderMax = 0, sliderTarget = 0;
	list::SliderHold sliderHold;   // the target shown after a slider drag ends
	list::MaskPaint paint;
	float thumbGrab = 0.f;

	// What the last frame showed, to follow the firmware's cursor.
	int shownApp = -1, shownTab = -1, shownCursor = -1;

	list::Layout layout() const {
		return list::Layout(box.size.x, box.size.y, model, scroll);
	}
	bool busy() const {
		return (model.flags & kModelBusy) != 0;
	}

	// --- Edits ---

	// Queue an edit and offer the queue to the host at once; what the host
	// refuses is offered again every frame (step, and ShoalWidget::step
	// while the list is hidden).
	void send(const FriendlyEdit& e) {
		outbox.add(e);
		flush();
	}
	void chooseApp(int app) {
		outbox.selectApp(app);
		flush();
	}
	void flush() {
		if (!live || !host || outbox.empty())
			return;
		Host* h = host;
		outbox.flush(
			[h](int app) {
				ControlEvent ev;
				ev.type = ControlEvent::SelectApp;
				ev.index = 0;
				ev.value = (int16_t) app;
				return h->post(ev);
			},
			[h](const FriendlyEdit& e) { return h->postEdit(e); });
	}
	void toHardware() {
		if (onHardwareMode)
			onHardwareMode();
	}
	// The dragged row in the current model; -1 once the app or the tab has
	// changed under the drag, or the row has gone.
	int dragRow() const {
		if (model.app != dragApp || model.tab != dragTab)
			return -1;
		return list::findRow(model, dragId);
	}
	// An edit on the dragged row, for the app and tab the drag began on.
	FriendlyEdit dragEdit(FriendlyEdit::Type type, int32_t value) const {
		FriendlyEdit e = {};
		e.type = type;
		e.app = (uint8_t) dragApp;
		e.tab = (uint8_t) dragTab;
		e.id = (int16_t) dragId;
		e.value = value;
		return e;
	}

	void step() override {
		if (host)
			host->model(model);
		flush();
		sliderHold.update(model, rack::system::getTime());
		// Follow the firmware's cursor when it moves to another row (an
		// encoder turn, or an edit made here), and start a new app or tab
		// with its cursor in view.
		const int cursor = list::cursorRow(model);
		const int cursorId = cursor >= 0 ? model.row[cursor].id : -1;
		const bool newList = model.app != shownApp || model.tab != shownTab;
		const list::Layout l = layout();
		if (newList)
			scroll = list::scrollToShow(0, cursor, l.rows, l.visible);
		else if (cursorId != shownCursor && drag == Drag::None)
			scroll = list::scrollToShow(l.scroll, cursor, l.rows, l.visible);
		else
			scroll = l.scroll;
		shownApp = model.app;
		shownTab = model.tab;
		shownCursor = cursorId;
		OpaqueWidget::step();
	}

	// --- Mouse ---

	void onButton(const ButtonEvent& e) override {
		OpaqueWidget::onButton(e);
		if (!live || e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS)
			return;
		pressPos = dragPos = e.pos;
		drag = Drag::None;
		const list::Layout l = layout();
		const list::Hit hit = l.hit(model, e.pos.x, e.pos.y);
		switch (hit.part) {
			case list::Part::Header:
				openAppMenu();
				break;
			case list::Part::Banner:
				toHardware();
				break;
			case list::Part::Tab:
				if (!busy() && hit.index != model.tab)
					send(list::editFor(model, FriendlyEdit::SelectTab, 0, hit.index));
				break;
			case list::Part::Scrollbar: {
				const list::Box thumb = l.thumb();
				const bool onThumb = e.pos.y >= thumb.y && e.pos.y < thumb.bottom();
				thumbGrab = onThumb ? e.pos.y - thumb.y : thumb.h / 2;
				scroll = l.scrollForThumbTop(e.pos.y - thumbGrab);
				drag = Drag::Scrollbar;
				break;
			}
			case list::Part::Label:
				if (!busy())
					send(list::editFor(model, FriendlyEdit::Focus, model.row[hit.index].id, 0));
				break;
			case list::Part::Cell: {
				const Row& r = model.row[hit.index];
				if (busy() || !paint.begin((uint32_t) r.value, hit.cell))
					break;
				beginDrag(Drag::Mask, r);
				send(dragEdit(FriendlyEdit::ToggleBit, hit.cell));
				break;
			}
			case list::Part::Control:
				pressControl(l, hit.index);
				break;
			case list::Part::None:
				break;
		}
	}

	void beginDrag(Drag kind, const Row& r) {
		drag = kind;
		dragApp = model.app;
		dragTab = model.tab;
		dragId = r.id;
	}

	void pressControl(const list::Layout& l, int i) {
		const Row& r = model.row[i];
		if (r.kind == RowKind::SubEditor) {
			toHardware();
			return;
		}
		if (busy())
			return;
		switch (r.kind) {
			case RowKind::Number:
				// A drag changes the value from where it is; a press alone
				// changes nothing (and may be the first of a double click).
				beginDrag(Drag::Slider, r);
				sliderHold.clear();
				sliderWidth = l.inset(l.control(r, i)).w;
				sliderMin = r.min;
				sliderMax = r.max;
				sliderTarget = r.value;
				slider.begin(r.value, sliderWidth, r.min, r.max);
				break;
			case RowKind::Choice:
				openChoiceMenu(r);
				break;
			case RowKind::Toggle:
				send(list::editFor(model, FriendlyEdit::SetValue, r.id, r.value ? 0 : 1));
				break;
			case RowKind::Action:
				beginDrag(Drag::Action, r);
				send(list::editFor(model, FriendlyEdit::Press, r.id, 0));
				break;
			default:
				break;
		}
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || drag == Drag::None)
			return;
		const rack::math::Vec delta = e.mouseDelta.div(getAbsoluteZoom());
		dragPos = dragPos.plus(delta);
		const list::Layout l = layout();
		switch (drag) {
			case Drag::Slider: {
				if (dragRow() < 0) {
					drag = Drag::None;
					break;
				}
				const int mods = APP->window->getMods() & RACK_MOD_MASK;
				const float speed = mods == (RACK_MOD_CTRL | GLFW_MOD_SHIFT) ? list::kFiner : mods == RACK_MOD_CTRL ? list::kFine : 1.f;
				const int32_t target = slider.move(delta.x, speed, sliderWidth, sliderMin, sliderMax);
				if (target != sliderTarget) {
					sliderTarget = target;
					send(dragEdit(FriendlyEdit::SetValue, target));
				}
				break;
			}
			case Drag::Mask: {
				const int i = dragRow();
				if (i < 0) {
					drag = Drag::None;
					break;
				}
				const Row& r = model.row[i];
				int cells[32];
				const int n = paint.enter((uint32_t) r.value, l.cellAt(r, i, dragPos.x), cells);
				for (int k = 0; k < n; k++)
					send(dragEdit(FriendlyEdit::ToggleBit, cells[k]));
				break;
			}
			case Drag::Scrollbar:
				scroll = l.scrollForThumbTop(dragPos.y - thumbGrab);
				break;
			default:
				break;
		}
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		// The bar keeps the drag's target until the model has it.
		if (drag == Drag::Slider)
			sliderHold.begin(dragApp, dragTab, dragId, sliderTarget, rack::system::getTime());
		drag = Drag::None;
	}

	// A slider goes back to its default.
	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!live || busy())
			return;
		const list::Hit hit = layout().hit(model, pressPos.x, pressPos.y);
		if (hit.part != list::Part::Control)
			return;
		const Row& r = model.row[hit.index];
		if (r.kind != RowKind::Number)
			return;
		send(list::editFor(model, FriendlyEdit::SetValue, r.id, r.def));
		// The second press also started a drag, from the value before the
		// reset: it goes on from the default, so a pixel of jitter does not
		// put the old value back.
		if (drag == Drag::Slider && dragRow() == hit.index) {
			slider.begin(r.def, sliderWidth, sliderMin, sliderMax);
			sliderTarget = r.def;
		}
	}

	void onHover(const HoverEvent& e) override {
		OpaqueWidget::onHover(e);
		hovering = true;
		hoverPos = e.pos;
	}

	void onLeave(const LeaveEvent& e) override {
		hovering = false;
		wheel.clear();
		OpaqueWidget::onLeave(e);
	}

	// Hidden in hardware mode: no stale highlight or drag when shown again.
	void onHide(const HideEvent& e) override {
		hovering = false;
		drag = Drag::None;
		OpaqueWidget::onHide(e);
	}

	// The wheel scrolls the rows. With nothing to scroll it is left to the
	// rack.
	void onHoverScroll(const HoverScrollEvent& e) override {
		OpaqueWidget::onHoverScroll(e);
		const list::Layout l = layout();
		if (!live || !l.scrollable())
			return;
#if defined ARCH_MAC
		const float notch = rka::scrollUnitsPerNotch(true);
#else
		const float notch = rka::scrollUnitsPerNotch(false);
#endif
		scroll = list::clampScroll(l.scroll - wheel.add(e.scrollDelta.y, notch / kRowsPerNotch), l.rows, l.visible);
		e.consume(this);
	}

	// --- Menus. Each item carries the edit for the model shown when the menu
	// was opened, so a choice made after the app has changed is dropped by
	// the core, not applied to another app's row. ---

	void openAppMenu() {
		rack::ui::Menu* menu = rack::createMenu();
		menu->addChild(rack::createMenuLabel("App"));
		rack::WeakPtr<FriendlyList> weak = this;
		const int current = model.app;
		for (int a = 0; a < kNumApps; a++) {
			menu->addChild(rack::createCheckMenuItem(Core::appName(a), "",
				[=]() { return a == current; },
				[=]() {
					if (weak)
						weak->chooseApp(a);
				}));
		}
	}

	void openChoiceMenu(const Row& r) {
		if ((int64_t) r.max - r.min >= kMaxChoices)
			return;   // no firmware list is near this long
		rack::ui::Menu* menu = rack::createMenu();
		menu->addChild(rack::createMenuLabel(r.label ? r.label : ""));
		rack::WeakPtr<FriendlyList> weak = this;
		const int32_t current = r.value;
		for (int64_t v = r.min; v <= r.max; v++) {
			char name[24];
			list::valueText(r, (int32_t) v, name, sizeof(name));
			const FriendlyEdit edit = list::editFor(model, FriendlyEdit::SetValue, r.id, (int32_t) v);
			menu->addChild(rack::createCheckMenuItem(name, "",
				[=]() { return edit.value == current; },
				[=]() {
					if (weak)
						weak->send(edit);
				}));
		}
	}

	// --- Drawing: nanovg in RkaTheme's colours, in the light layer like the
	// other screens. ---

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1)
			drawList(args.vg);
		Widget::drawLayer(args, layer);
	}

	struct Pen {
		NVGcontext* vg;
		float u;      // about one pixel at Rack's default zoom
		float text;   // row text size
		float pad;    // text inset

		void fill(const list::Box& b, NVGcolor c, float radius = 0.f) const {
			nvgBeginPath(vg);
			if (radius > 0.f)
				nvgRoundedRect(vg, b.x, b.y, b.w, b.h, radius);
			else
				nvgRect(vg, b.x, b.y, b.w, b.h);
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
		void stroke(const list::Box& b, NVGcolor c, float radius) const {
			nvgBeginPath(vg);
			nvgRoundedRect(vg, b.x + 0.5f * u, b.y + 0.5f * u, b.w - u, b.h - u, radius);
			nvgStrokeColor(vg, c);
			nvgStrokeWidth(vg, u);
			nvgStroke(vg);
		}
		// Text clipped to `clip`; align is NVG_ALIGN_LEFT, _CENTER or _RIGHT.
		// Returns the x where the text ends.
		float write(const list::Box& clip, int align, const char* s, NVGcolor c, float size) const {
			if (!s || !s[0] || clip.w <= 0.f)
				return clip.x;
			nvgSave(vg);
			nvgIntersectScissor(vg, clip.x, clip.y, clip.w, clip.h);
			nvgFontSize(vg, size);
			nvgFillColor(vg, c);
			nvgTextAlign(vg, align | NVG_ALIGN_MIDDLE);
			const float x = align == NVG_ALIGN_LEFT ? clip.x + pad : align == NVG_ALIGN_RIGHT ? clip.right() - pad : clip.x + clip.w / 2;
			const float end = nvgText(vg, x, clip.y + clip.h / 2, s, NULL);
			nvgRestore(vg);
			return end;
		}
		// A small filled triangle pointing down (a dropdown's mark) or right.
		void chevron(float cx, float cy, float r, bool down, NVGcolor c) const {
			nvgBeginPath(vg);
			if (down) {
				nvgMoveTo(vg, cx - r, cy - r * 0.55f);
				nvgLineTo(vg, cx + r, cy - r * 0.55f);
				nvgLineTo(vg, cx, cy + r * 0.65f);
			}
			else {
				nvgMoveTo(vg, cx - r * 0.55f, cy - r);
				nvgLineTo(vg, cx - r * 0.55f, cy + r);
				nvgLineTo(vg, cx + r * 0.65f, cy);
			}
			nvgClosePath(vg);
			nvgFillColor(vg, c);
			nvgFill(vg);
		}
	};

	static NVGcolor dim(NVGcolor c, float a) {
		return nvgTransRGBAf(c, a);
	}

	void drawList(NVGcontext* vg) {
		std::shared_ptr<rack::window::Font> font = APP->window->loadFont(rack::asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		const list::Layout l = layout();
		Pen pen;
		pen.vg = vg;
		pen.u = l.lineH() / 13.f;
		pen.text = l.lineH() * 0.8f;
		pen.pad = 5.f * pen.u;
		const list::Hit hover = live && hovering ? l.hit(model, hoverPos.x, hoverPos.y) : list::Hit{ list::Part::None, -1, -1 };

		nvgSave(vg);
		nvgScissor(vg, 0.f, 0.f, box.size.x, box.size.y);
		nvgFontFaceId(vg, font->handle);
		drawHeader(pen, l, hover);
		if (l.busy)
			drawBanner(pen, l, hover);
		// While busy nothing below takes an edit: it is drawn dimmed.
		nvgGlobalAlpha(vg, l.busy ? 0.35f : 1.f);
		drawTabs(pen, l, hover);
		for (int i = l.scroll; i < l.scroll + l.visible && i < l.rows; i++)
			drawRow(pen, l, hover, i);
		nvgGlobalAlpha(vg, 1.f);
		drawScrollbar(pen, l, hover);
		nvgRestore(vg);
	}

	void drawHeader(const Pen& pen, const list::Layout& l, const list::Hit& hover) {
		const list::Box b = l.header();
		const bool hot = hover.part == list::Part::Header;
		// The band stays inside the panel's rounded screen edge.
		nvgBeginPath(pen.vg);
		nvgRoundedRectVarying(pen.vg, b.x + pen.u, b.y + pen.u, b.w - 2.f * pen.u, b.h - pen.u, 2.f * pen.u, 2.f * pen.u, 0.f, 0.f);
		nvgFillColor(pen.vg, RkaTheme::CARD);
		nvgFill(pen.vg);
		pen.fill(list::Box{ b.x + pen.u, b.bottom() - pen.u, b.w - 2.f * pen.u, pen.u }, dim(RkaTheme::CYAN, 0.55f));
		const float r = b.h * 0.2f;
		pen.chevron(pen.pad + r, b.y + b.h / 2, r, true, hot ? RkaTheme::CYAN : dim(RkaTheme::INK, 0.7f));
		const list::Box nameBox = { pen.pad + 2.f * r, b.y, b.w - pen.pad - 2.f * r, b.h - pen.u };
		const float nameEnd = pen.write(nameBox, NVG_ALIGN_LEFT, Core::appName(model.app), hot ? RkaTheme::CYAN : RkaTheme::INK, b.h * 0.72f);
		// On the right: the app's own status line, or the hint.
		const list::Box statusBox = { nameEnd + pen.pad, b.y, b.right() - nameEnd - pen.pad, b.h - pen.u };
		char status[kStatusText + 1];
		std::snprintf(status, sizeof(status), "%.*s", kStatusText, model.status);
		if (model.flags & kModelTruncated)
			pen.write(statusBox, NVG_ALIGN_RIGHT, "LIST TRUNCATED", RkaTheme::MAGENTA, pen.text);
		else if (status[0])
			pen.write(statusBox, NVG_ALIGN_RIGHT, status, dim(RkaTheme::CYAN, 0.9f), pen.text);
		else
			pen.write(statusBox, NVG_ALIGN_RIGHT, "CLICK APP TO SWITCH", RkaTheme::HINT, pen.text * 0.9f);
	}

	// The busy line: the firmware's app menu or one of the app's own editors
	// is open, and only hardware mode shows it.
	void drawBanner(const Pen& pen, const list::Layout& l, const list::Hit& hover) {
		const list::Box b = l.banner();
		const bool hot = hover.part == list::Part::Banner;
		pen.fill(list::Box{ b.x + pen.u, b.y, b.w - 2.f * pen.u, b.h }, dim(RkaTheme::AMBER, hot ? 0.3f : 0.18f));
		const float r = b.h * 0.22f;
		const list::Box textBox = { b.x, b.y, b.w - pen.pad - 2.f * r, b.h };
		pen.write(textBox, NVG_ALIGN_LEFT, "HARDWARE EDITOR OPEN", RkaTheme::AMBER, pen.text);
		pen.write(textBox, NVG_ALIGN_RIGHT, "CLICK TO SWITCH", hot ? RkaTheme::INK : RkaTheme::AMBER, pen.text);
		pen.chevron(b.right() - pen.pad - r, b.y + b.h / 2, r, false, hot ? RkaTheme::INK : RkaTheme::AMBER);
	}

	void drawTabs(const Pen& pen, const list::Layout& l, const list::Hit& hover) {
		if (!l.tabs)
			return;
		const list::Box bar = l.tabBar();
		pen.fill(list::Box{ bar.x, bar.bottom() - pen.u, bar.w, pen.u }, dim(RkaTheme::HINT, 0.6f));
		for (int t = 0; t < l.tabs; t++) {
			const list::Box b = l.tab(t);
			const bool selected = t == model.tab;
			const bool hot = hover.part == list::Part::Tab && hover.index == t && !l.busy;
			if (selected) {
				pen.fill(b, dim(RkaTheme::CYAN, 0.16f));
				pen.fill(list::Box{ b.x, b.bottom() - 2.f * pen.u, b.w, 2.f * pen.u }, RkaTheme::CYAN);
			}
			else if (hot)
				pen.fill(b, dim(RkaTheme::INK, 0.07f));
			const char* label = model.tabLabel[t] ? model.tabLabel[t] : "";
			pen.write(b, NVG_ALIGN_CENTER, label, selected ? RkaTheme::INK : dim(RkaTheme::INK, hot ? 0.9f : 0.55f), pen.text);
		}
	}

	void drawRow(const Pen& pen, const list::Layout& l, const list::Hit& hover, int i) {
		NVGcontext* vg = pen.vg;
		const Row& r = model.row[i];
		const list::Box b = l.row(i);
		const bool rowHot = !l.busy && hover.index == i
			&& (hover.part == list::Part::Label || hover.part == list::Part::Control || hover.part == list::Part::Cell);
		const bool controlHot = rowHot && hover.part != list::Part::Label;
		const bool dragged = drag != Drag::None && drag != Drag::Scrollbar && dragRow() == i;
		const char* label = r.label ? r.label : "";

		if (r.kind == RowKind::Heading) {
			// A section title with a rule to the end of the row.
			const float end = pen.write(b, NVG_ALIGN_LEFT, label, RkaTheme::VIOLET, pen.text * 0.92f);
			const float x = end + pen.pad;
			if (x < b.right() - pen.pad)
				pen.fill(list::Box{ x, b.y + b.h / 2, b.right() - pen.pad - x, pen.u }, dim(RkaTheme::VIOLET, 0.4f));
			return;
		}

		// The pointer's row is lightened. The firmware's cursor row is tinted
		// cyan, with a bar on its left edge (amber while the hardware is
		// editing its value) and a brighter label.
		const bool cursor = (r.flags & kRowCursor) != 0;
		if (cursor)
			pen.fill(b, dim(RkaTheme::CYAN, 0.09f));
		if (rowHot || dragged)
			pen.fill(b, dim(RkaTheme::INK, 0.07f));
		if (cursor)
			pen.fill(list::Box{ b.x + pen.u, b.y + pen.u, 3.f * pen.u, b.h - 2.f * pen.u }, (r.flags & kRowEditing) ? RkaTheme::AMBER : RkaTheme::CYAN);

		const list::Box c = l.inset(l.control(r, i));
		const float radius = 1.5f * pen.u;
		const NVGcolor edge = controlHot || dragged ? RkaTheme::CYAN : dim(RkaTheme::HINT, 0.7f);
		char value[24];

		if (r.kind == RowKind::Action) {
			// A button across the row, carrying the label.
			const list::Box button = { b.x + pen.pad, c.y, b.w - 2.f * pen.pad, c.h };
			pen.fill(button, dragged ? dim(RkaTheme::CYAN, 0.3f) : RkaTheme::CARD, radius);
			pen.stroke(button, edge, radius);
			pen.write(button, NVG_ALIGN_CENTER, label, RkaTheme::INK, pen.text);
			return;
		}

		pen.write(l.label(r, i), NVG_ALIGN_LEFT, label, cursor ? RkaTheme::INK : dim(RkaTheme::INK, 0.72f), pen.text);

		switch (r.kind) {
			case RowKind::Number: {
				// A track the width of the range, filled to the value (from
				// zero when the range straddles it), the value printed on
				// it. During a drag it shows the pointer's target, and for a
				// moment after (SliderHold); the model's value otherwise.
				const int32_t v = dragged && drag == Drag::Slider ? sliderTarget : sliderHold.shown(model, i);
				pen.fill(c, RkaTheme::CARD, radius);
				pen.fill(c, dim(RkaTheme::CYAN, 0.12f), radius);
				const float vx = list::sliderX(v, c.w, r.min, r.max);
				const float zx = r.min < 0 && r.max > 0 ? list::sliderX(0, c.w, r.min, r.max) : 0.f;
				const float x0 = vx < zx ? vx : zx, x1 = vx < zx ? zx : vx;
				nvgSave(vg);
				nvgIntersectScissor(vg, c.x + x0, c.y, x1 - x0, c.h);
				pen.fill(c, dim(RkaTheme::CYAN, 0.38f), radius);
				nvgRestore(vg);
				const float mark = 1.5f * pen.u;
				float mx = c.x + vx - mark / 2;
				mx = mx < c.x ? c.x : mx > c.right() - mark ? c.right() - mark : mx;
				pen.fill(list::Box{ mx, c.y + pen.u, mark, c.h - 2.f * pen.u }, RkaTheme::CYAN);
				pen.stroke(c, edge, radius);
				list::valueText(r, v, value, sizeof(value));
				pen.write(c, NVG_ALIGN_RIGHT, value, RkaTheme::INK, pen.text);
				break;
			}
			case RowKind::Choice: {
				pen.fill(c, RkaTheme::CARD, radius);
				pen.stroke(c, edge, radius);
				const float cr = c.h * 0.22f;
				pen.chevron(c.right() - pen.pad - cr, c.y + c.h / 2, cr, true, controlHot ? RkaTheme::CYAN : dim(RkaTheme::INK, 0.6f));
				list::valueText(r, r.value, value, sizeof(value));
				pen.write(list::Box{ c.x, c.y, c.w - pen.pad - 2.f * cr, c.h }, NVG_ALIGN_RIGHT, value, RkaTheme::INK, pen.text);
				break;
			}
			case RowKind::Toggle: {
				list::valueText(r, r.value, value, sizeof(value));
				if (r.value) {
					pen.fill(c, dim(RkaTheme::CYAN, 0.9f), radius);
					pen.write(c, NVG_ALIGN_CENTER, value, RkaTheme::CARD, pen.text);
				}
				else {
					pen.fill(c, RkaTheme::CARD, radius);
					pen.write(c, NVG_ALIGN_CENTER, value, dim(RkaTheme::INK, 0.6f), pen.text);
				}
				pen.stroke(c, edge, radius);
				break;
			}
			case RowKind::Mask: {
				// One cell per note, lit when active. The cells tile the
				// control's drawn box, with a gap between neighbours only, so
				// the outer edges line up with the dropdowns and sliders.
				const int cells = list::Layout::cellCount(r);
				const float gap = 0.75f * pen.u;
				for (int n = 0; n < cells; n++) {
					const list::Box whole = l.cell(r, i, n);
					const float x0 = n > 0 ? whole.x + gap : whole.x;
					const float x1 = n < cells - 1 ? whole.right() - gap : whole.right();
					const list::Box cell = { x0, whole.y, x1 - x0, whole.h };
					const bool on = (((uint32_t) r.value >> n) & 1u) != 0;
					const bool hot = rowHot && hover.part == list::Part::Cell && hover.cell == n;
					if (on)
						pen.fill(cell, hot ? RkaTheme::INK : RkaTheme::CYAN, radius);
					else {
						pen.fill(cell, RkaTheme::CARD, radius);
						pen.stroke(cell, hot ? RkaTheme::CYAN : dim(RkaTheme::HINT, 0.7f), radius);
					}
				}
				if (cells == 0)
					pen.write(c, NVG_ALIGN_RIGHT, "-", RkaTheme::HINT, pen.text);
				break;
			}
			case RowKind::SubEditor: {
				// Not editable here: a click goes to hardware mode.
				pen.stroke(c, controlHot ? RkaTheme::AMBER : dim(RkaTheme::AMBER, 0.45f), radius);
				const float cr = c.h * 0.22f;
				pen.chevron(c.right() - pen.pad - cr, c.y + c.h / 2, cr, false, RkaTheme::AMBER);
				pen.write(list::Box{ c.x, c.y, c.w - pen.pad - 2.f * cr, c.h }, NVG_ALIGN_RIGHT, "edit in hardware mode", RkaTheme::AMBER, pen.text * 0.92f);
				break;
			}
			default:
				break;
		}
	}

	void drawScrollbar(const Pen& pen, const list::Layout& l, const list::Hit& hover) {
		if (!l.scrollable())
			return;
		const list::Box bar = l.scrollbar(), thumb = l.thumb();
		const bool hot = hover.part == list::Part::Scrollbar || drag == Drag::Scrollbar;
		pen.fill(bar, dim(RkaTheme::INK, 0.05f));
		const list::Box t = { thumb.x + 1.5f * pen.u, thumb.y + pen.u, thumb.w - 3.f * pen.u, thumb.h - 2.f * pen.u };
		pen.fill(t, hot ? dim(RkaTheme::INK, 0.75f) : RkaTheme::HINT, t.w / 2);
	}
};

}

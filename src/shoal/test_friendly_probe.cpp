// Friendly edits mixed with panel events: the invariants of friendly mode,
// checked after every step of a random run in each of the thirteen apps.
// Adapted from the whole-branch review's scratch probe (TESTING.md, Review).
//
// Each step is a panel event straight to the app (either encoder, any of
// the four short presses, an L or DOWN long press, an app-menu round trip)
// or a friendly edit on a random row of the current model, then a few ticks
// with random gates and CVs. After each step:
// - building a model changed nothing: no setting, the menu cursor, its edit
//   mode, the page, the channel or the drawn menu;
// - no edit was accepted while the model was busy;
// - after an accepted edit no sub-editor and no app menu is open;
// - after an accepted SetValue or ToggleBit on a row of the app's menu, the
//   cursor is not editing.
// A failure prints the last steps. A seed reproduces from the command line:
//   build/test/shoal/test_friendly_probe <steps> <seed>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "testing.hpp"
#include "test_util.hpp"
#include "CoreTest.hpp"

using namespace shoal;

namespace {

const char* const kTypes[] = { "SetValue", "ToggleBit", "Press", "SelectTab", "Focus" };

// The last steps, printed with the first failure of each kind.
struct History {
	std::vector<std::string> lines;
	std::vector<std::string> reported;
	void note(const char* s) {
		lines.push_back(s);
		if (lines.size() > 14)
			lines.erase(lines.begin());
	}
	void dump(const std::string& kind) {
		for (const std::string& r : reported)
			if (r == kind)
				return;
		reported.push_back(kind);
		std::printf("  last steps before %s:\n", kind.c_str());
		for (const std::string& l : lines)
			std::printf("    %s\n", l.c_str());
	}
};

// Settings the list shows that the firmware's menu cursor does not reach:
// CopierMaschine's and Acid Curds' scale (L encoder), Piqued's Euclidean
// fill (the Euclidean editor), Quadraturia's Coarse and Fine and Low-rents'
// Freq 1 and 2 (the L encoder).
bool offMenu(int app, int id) {
	return ((app == 0 || app == 11) && id == 0) || (app == 7 && id == 11) || ((app == 5 || app == 6) && id < 2);
}

// What a model must leave alone.
struct State {
	int row, editing, page, ch, app;
	int settings[4][128];
	uint8_t frame[kFrameBytes];
};

void capture(Core& c, State& s) {
	std::memset(&s, 0, sizeof(s));
	s.row = CoreTest::menuRow(c);
	s.editing = CoreTest::menuEditing(c);
	s.page = CoreTest::menuPage(c);
	s.ch = CoreTest::selectedChannel(c);
	s.app = c.app();
	for (int ch = 0; ch < c.channels(); ch++)
		for (int i = 0; i < c.settingCount() && i < 128; i++)
			s.settings[ch][i] = c.setting(ch, i);
	c.drawMenu(s.frame);
}

// One run of `steps` steps in `app` from seed `seed`. Returns the edits
// accepted.
int probe(int app, uint32_t seed, int steps) {
	const char* an = Core::appName(app);
	Core c;
	c.seed(seed);
	c.selectApp(app);
	Lcg rng;
	rng.state = seed * 7919u + (uint32_t) app;
	History history;
	int accepted = 0;
	auto R = [&](uint32_t m) { return (rng.next() >> 8) % m; };
	for (int n = 0; n < steps; n++) {
		char buf[200];
		State before, after;
		capture(c, before);
		FriendlyModel m;
		c.friendlyModel(m);
		c.friendlyModel(m);
		capture(c, after);
		const bool untouched = std::memcmp(&before, &after, sizeof(before)) == 0;
		CHECK(untouched, "%s seed %u step %d: building a model changed the app", an, seed, n);
		if (!untouched)
			history.dump("model-changes-app");

		if (R(100) < 45) {
			// A panel event.
			const uint32_t p = R(20);
			buf[0] = 0;
			if (c.appMenuOpen()) {
				c.selectApp(app);
				std::snprintf(buf, sizeof(buf), "app menu closed");
			} else if (p < 6) {
				const int d = (int) R(7) - 3;
				const bool left = R(3) == 0;
				CoreTest::encoderEvent(c, left ? ENCODER_L : ENCODER_R, d);
				std::snprintf(buf, sizeof(buf), "encoder %c %d", left ? 'L' : 'R', d);
			} else if (p < 16) {
				const int b = (int) R(4);
				CoreTest::buttonEvent(c, (Button) b, false);
				std::snprintf(buf, sizeof(buf), "push %s", b == 0 ? "UP" : b == 1 ? "DOWN" : b == 2 ? "L" : "R");
			} else if (p < 18) {
				const bool l = R(2) == 0;
				CoreTest::buttonEvent(c, l ? BUTTON_L : BUTTON_DOWN, true);
				std::snprintf(buf, sizeof(buf), "long press %s", l ? "L" : "DOWN");
			} else {
				CoreTest::appMenuEvents(c);
				std::snprintf(buf, sizeof(buf), "app menu round trip");
			}
			history.note(buf);
		} else if (m.rows > 0) {
			// A friendly edit on a random row.
			FriendlyEdit e = {};
			const Row row = m.row[R((uint32_t) m.rows)];
			e.app = (uint8_t) app;
			e.tab = (uint8_t) (m.tabs > 1 && R(6) == 0 ? R(m.tabs) : m.tab);
			e.id = row.id;
			bool send = true;
			switch (row.kind) {
				case RowKind::Number:
				case RowKind::Choice:
				case RowKind::Toggle:
					e.type = R(6) == 0 ? FriendlyEdit::Focus : FriendlyEdit::SetValue;
					e.value = row.min + (int32_t) R((uint32_t) (row.max - row.min + 1));
					break;
				case RowKind::Mask:
					e.type = R(6) == 0 ? FriendlyEdit::Focus : FriendlyEdit::ToggleBit;
					e.value = row.max > 0 ? (int32_t) R((uint32_t) row.max) : 0;
					break;
				case RowKind::Action:
					e.type = FriendlyEdit::Press;
					break;
				case RowKind::SubEditor:
					e.type = FriendlyEdit::Focus;
					break;
				default:
					e.type = FriendlyEdit::SelectTab;
					e.value = (int32_t) R(m.tabs > 1 ? m.tabs : 1);
					send = m.tabs > 1;
					break;
			}
			if (send) {
				const bool busy = (m.flags & kModelBusy) != 0;
				const bool ok = c.friendlyEdit(e);
				std::snprintf(buf, sizeof(buf), "%s on %d (%s), tab %d, value %d: %s%s", kTypes[e.type], e.id, row.label ? row.label : "?",
					e.tab, (int) e.value, ok ? "accepted" : "dropped", busy ? " (busy)" : "");
				history.note(buf);
				CHECK(!(busy && ok), "%s seed %u step %d: accepted while busy: %s", an, seed, n, buf);
				if (busy && ok)
					history.dump("accepted-while-busy");
				if (ok) {
					accepted++;
					const bool settled = CoreTest::friendlyAccepting(c) && !c.appMenuOpen();
					CHECK(settled, "%s seed %u step %d: an editor or the app menu is open after: %s", an, seed, n, buf);
					if (!settled)
						history.dump("editor-open");
					if ((e.type == FriendlyEdit::SetValue || e.type == FriendlyEdit::ToggleBit) && e.id < 256 && !offMenu(app, e.id)) {
						const bool editing = CoreTest::menuEditing(c);
						CHECK(!editing, "%s seed %u step %d: editing after: %s", an, seed, n, buf);
						if (editing)
							history.dump("editing-after-edit");
					}
				}
			}
		}
		const int ticks = (int) R(6);
		for (int t = 0; t < ticks; t++) {
			if (R(30) == 0) {
				const int ch = (int) R(4);
				c.setGate(ch, R(2) ? 10.f : 0.f);
				c.setCv(ch, rng.volts(-6.f, 8.f));
			}
			c.tick();
		}
		if (c.app() != app)
			c.selectApp(app);
	}
	return accepted;
}

} // namespace

int main(int argc, char** argv) {
	const int steps = argc > 1 ? std::atoi(argv[1]) : 4000;
	const int firstSeed = argc > 2 ? std::atoi(argv[2]) : 1;
	const int seeds = argc > 2 ? 1 : 3;
	for (int s = firstSeed; s < firstSeed + seeds; s++)
		for (int app = 0; app < kNumApps; app++) {
			const int accepted = probe(app, (uint32_t) s, steps);
			// The run made edits, not only drops.
			CHECK(accepted > steps / 10, "%s seed %d: only %d of %d steps were accepted edits", Core::appName(app), s, accepted, steps);
		}
	return rkatest::finish("test_friendly_probe");
}

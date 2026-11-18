// Squall's parts: Memory (the history and its loop), the gate, and (Task 4)
// the Swell follower.
#include "testing.hpp"
#include "squall/Gate.hpp"
#include "squall/Memory.hpp"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

using namespace rka::squall;

static Event ev(float i) {
	Event e;
	e.gap = 1.f + i;
	e.value = i / 100.f;
	e.length = 0.25f;
	return e;
}

static void memory_loop_period() {
	for (int L : {4, 8, 16}) {
		Memory m;
		m.setLength(L);
		for (int i = 0; i < L; i++)
			m.push(ev((float) i));
		CHECK(m.canReplay(), "L %d: full history must replay", L);
		Event n = m.next();
		CHECK(n.value == ev(0.f).value && n.gap == ev(0.f).gap, "L %d: next is e0 (got value %g)", L, n.value);
		m.push(n);
		n = m.next();
		CHECK(n.value == ev(1.f).value, "L %d: then e1 (got value %g)", L, n.value);
		// Locked: L more replays reproduce the loop exactly.
		for (int k = 0; k < 3 * L; k++) {
			const Event a = m.next();
			CHECK(a.value == ev((float) ((k + 1) % L)).value, "L %d step %d: value %g", L, k, a.value);
			m.push(a);
		}
	}
}

static void memory_before_full() {
	Memory m;
	m.setLength(4);
	CHECK(!m.canReplay(), "empty history must not replay");
	for (int i = 0; i < 3; i++) {
		m.push(ev((float) i));
		CHECK(!m.canReplay(), "%d of 4 events must not replay", i + 1);
	}
	m.push(ev(3.f));
	CHECK(m.canReplay(), "4 of 4 replays");
	m.setLength(16);
	CHECK(!m.canReplay(), "4 of 16 must not replay");
}

static void loop_length_change_while_locked() {
	Memory m;
	m.setLength(16);
	for (int i = 0; i < 16; i++)
		m.push(ev((float) i));
	m.setLength(4);
	CHECK(m.length() == 4, "length %d", m.length());
	Event first[4];
	for (int k = 0; k < 4; k++) {
		const Event a = m.next();
		CHECK(a.value >= 0.f && a.value <= 0.15f + 1e-6f, "replay %d is a stored event (value %g)", k, a.value);
		first[k] = a;
		m.push(a);
	}
	for (int k = 0; k < 12; k++) {
		const Event a = m.next();
		CHECK(a.value == first[k % 4].value, "after the change the loop has period 4 (step %d)", k);
		m.push(a);
	}
	m.setLength(5);
	CHECK(m.length() == 4 || m.length() == 8, "setLength(5) clamps to 4, 8 or 16 (got %d)", m.length());
	m.setLength(100);
	CHECK(m.length() == 16, "setLength(100) clamps to 16 (got %d)", m.length());
}

static void memory_wraps() {
	Memory m;
	for (int i = 0; i < 40; i++)
		m.push(ev((float) i));
	CHECK(m.count() == 16, "count %d", m.count());
	CHECK(m.at(0).value == ev(24.f).value, "oldest is push 24 (value %g)", m.at(0).value);
	CHECK(m.at(15).value == ev(39.f).value, "newest is push 39 (value %g)", m.at(15).value);
	m.clear();
	CHECK(m.count() == 0 && !m.canReplay(), "clear empties the history");
}

static int highRun(Gate& g, int maxN) {
	int n = 0;
	while (n < maxN && g.process())
		n++;
	return n;
}

static void gate_length() {
	Gate g;
	g.setSampleRate(48000.f);
	CHECK(!g.process(), "a new gate is low");
	g.fire(0.1f);
	const int n = highRun(g, 100000);
	CHECK(std::abs(n - 4800) <= 1, "fire(0.1) at 48 kHz: high for %d samples, want 4800", n);
	CHECK(!g.high(), "low after its length");
}

static void gate_retrigger() {
	Gate g;
	g.setSampleRate(48000.f);
	g.fire(0.1f);
	for (int i = 0; i < 1000; i++)
		g.process();
	g.fire(0.05f);
	int low = 0;
	while (low < 1000 && !g.process())
		low++;
	CHECK(std::abs(low - 48) <= 1, "retrigger: low for %d samples, want 48", low);
	const int high = 1 + highRun(g, 100000);
	CHECK(std::abs(high - 2400) <= 2, "then high for %d samples, want 2400", high);
}

int main() {
	memory_loop_period();
	memory_before_full();
	loop_length_change_while_locked();
	memory_wraps();
	gate_length();
	gate_retrigger();
	return rkatest::finish("squall test_parts");
}

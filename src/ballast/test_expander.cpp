// Ballast's expander hook (plan Task 4), with a test-only stand-in: the
// three call points land on the same sample, in chain order, with the mono
// flag as the spec defines it; with no expander the output is unchanged.
// Spec: docs/specs/2026-11-17-ballast-design.md, "The expander connection".
#include "testing.hpp"
#include "BallastEngine.hpp"
#include "BallastChain.hpp"

#include <cstdint>

#include <cmath>
#include <cstdio>
#include <vector>

typedef rka::BallastFrame F;

namespace {

const float FS = 48000.f;
// The DC blocker setting every Rig starts with; main runs the hook tests
// with it off and on.
bool gDcBlock = false;

// Applies x * mul + add at one stage, to one channel (or all, ch = -1).
struct Gain : rka::BallastExpander {
	enum Stage { PRE, POST, MIX } stage;
	int ch;
	float mul, add;
	int srcSeen = -1;    // the last source type passed for ch
	float lSeen = 0, rSeen = 0;
	int links = 0;
	int attaches = 0;
	void attached() override {
		attaches++;
	}
	int kindId = 0;
	int kind() const override {
		return kindId;
	}
	void linked() override {
		links++;
	}
	Gain(Stage s, int c, float m, float a) : stage(s), ch(c), mul(m), add(a) {}
	void preLevel(int c, float& l, float& r) override {
		if (stage == PRE && (ch < 0 || c == ch)) {
			l = l * mul + add;
			r = r * mul + add;
		}
	}
	void postLevel(int c, float& l, float& r, rka::BallastSource src) override {
		if (c == ch) {
			srcSeen = (int) src;
			lSeen = l;
			rSeen = r;
		}
		if (stage == POST && (ch < 0 || c == ch)) {
			l = l * mul + add;
			r = r * mul + add;
		}
	}
	void intoMix(float& l, float& r) override {
		if (stage == MIX) {
			l = l * mul + add;
			r = r * mul + add;
		}
	}
};

struct Rig {
	rka::BallastEngine e;
	F f;
	float inL[4][16] = {}, inR[4][16] = {}, cv[4][16] = {}, mcv[16] = {}, chL[16] = {}, chR[16] = {};
	rka::BallastResult out;
	std::vector<rka::BallastExpander*> chain;
	Rig() {
		e.setSampleRate(FS);
		for (int p = 0; p < F::PARAMS_LEN; p++)
			f.params[p] = 0.75f;
		for (int c = 0; c < 4; c++) {
			f.inL[c].v = inL[c];
			f.inR[c].v = inR[c];
			f.cv[c].v = cv[c];
		}
		f.masterCv.v = mcv;
		f.chainL.v = chL;
		f.chainR.v = chR;
		f.outL = f.outR = true;
		f.dcBlock = gDcBlock;
	}
	void step() {
		if (chain.empty())
			e.process(f, out);
		else
			e.process(f, out, chain.data(), (int) chain.size());
	}
};

// Channel 1 at DC v, Level at p: settle, then return out.l.
float settle(Rig& g, float v) {
	g.f.inL[0].channels = 1;
	g.inL[0][0] = v;
	for (int i = 0; i < 4800; i++)
		g.step();
	return g.out.l;
}

void post_level_same_sample() {
	Rig g;
	const float before = settle(g, 1.f);
	Gain half(Gain::POST, 0, 0.5f, 0.f);
	g.chain.push_back(&half);
	g.step();
	CHECK(std::fabs(g.out.l - 0.5f * before) < 1e-6f, "postLevel x0.5 must act on the same sample (%g, want %g)",
	      g.out.l, 0.5f * before);
}

void pre_level_before_level() {
	const float level = 0.5f, k = rka::BallastEngine::curveExact(0.5f);
	Rig a, b;
	a.f.params[F::LEVEL_1] = b.f.params[F::LEVEL_1] = level;
	Gain pre(Gain::PRE, 0, 1.f, 1.f), post(Gain::POST, 0, 1.f, 1.f);
	a.chain.push_back(&pre);
	b.chain.push_back(&post);
	const float va = settle(a, 0.f), vb = settle(b, 0.f);
	CHECK(std::fabs(va - k) < 1e-3f, "a +1 V preLevel offset must be scaled by Level (%g, want %g)", va, k);
	CHECK(std::fabs(vb - 1.f) < 1e-3f, "a +1 V postLevel offset must not be scaled by Level (%g, want 1)", vb);
}

void into_mix_before_master() {
	Rig g;
	g.f.params[F::MASTER] = 0.5f;
	Gain add(Gain::MIX, -1, 1.f, 1.f);
	g.chain.push_back(&add);
	const float v = settle(g, 0.f), m = rka::BallastEngine::curveExact(0.5f);
	CHECK(std::fabs(v - m) < 1e-3f, "an intoMix +1 V must be scaled by Master (%g, want %g)", v, m);
}

void source_type() {
	struct Case {
		bool l, r;
		rka::BallastSource want;
	} cases[] = {{true, false, rka::MONO}, {false, true, rka::MONO}, {true, true, rka::STEREO}, {false, false, rka::STEREO}};
	for (const Case& c : cases) {
		Rig g;
		Gain probe(Gain::POST, 0, 1.f, 0.f);
		g.chain.push_back(&probe);
		g.f.inL[0].channels = c.l ? 1 : 0;
		g.f.inR[0].channels = c.r ? 1 : 0;
		g.inL[0][0] = 0.25f;
		g.inR[0][0] = 0.5f;
		g.step();
		CHECK(probe.srcSeen == (int) c.want, "IN L %s, IN R %s: source %d, want %d", c.l ? "patched" : "open",
		      c.r ? "patched" : "open", probe.srcSeen, (int) c.want);
		if (!c.l && c.r)
			CHECK(probe.lSeen == probe.rSeen && probe.rSeen > 0.f, "MONO via IN R: l %g and r %g must both hold IN R",
			      probe.lSeen, probe.rSeen);
	}
}

void chain_order() {
	Rig g;
	Gain twice(Gain::POST, 0, 2.f, 0.f), plusOne(Gain::POST, 0, 1.f, 1.f);
	g.chain.push_back(&twice);
	g.chain.push_back(&plusOne);
	const float v = settle(g, 1.f);
	CHECK(std::fabs(v - 3.f) < 1e-3f, "x2 then +1 on 1 V gives %g, want 3 (chain order)", v);
}

void no_expander_bit_identical() {
	Rig a, b, c;
	Gain unity(Gain::POST, -1, 1.f, 0.f);
	c.chain.push_back(&unity);
	uint32_t s = 1;
	bool same = true;
	for (int i = 0; i < (int) (10 * FS); i++) {
		s = s * 1664525u + 1013904223u;
		const float x = (float) (s >> 8) / 16777216.f * 2.f - 1.f;
		for (Rig* g : {&a, &b, &c}) {
			g->f.inL[1].channels = 1;
			g->inL[1][0] = x;
		}
		a.e.process(a.f, a.out);
		b.e.process(b.f, b.out, nullptr, 0);
		c.step();
		same = same && a.out.l == b.out.l && a.out.r == b.out.r && a.out.l == c.out.l && a.out.r == c.out.r;
	}
	CHECK(same, "no expander, an empty chain and a unity expander must all match bit for bit");
}

void expander_attach_detach() {
	Rig fresh, g;
	Gain loud(Gain::POST, -1, 3.f, 0.5f);
	bool finite = true, same = true;
	// With the DC blocker on, the one sample the expander acted on stays in
	// the blocker's state and decays (3 Hz): match within 1e-5 V after 1 s.
	// With it off: bit for bit at once.
	const int from = gDcBlock ? 5001 + (int) FS : 5001;
	for (int i = 0; i < 20000 + (int) FS; i++) {
		const float x = std::sin(0.01f * i);
		for (Rig* r : {&fresh, &g}) {
			r->f.inL[0].channels = 1;
			r->inL[0][0] = x;
		}
		if (i == 5000)
			g.chain.push_back(&loud);
		if (i == 5001)
			g.chain.clear();
		fresh.step();
		g.step();
		finite = finite && g.out.l == g.out.l && g.out.r == g.out.r;
		if (i > from)
			same = same && (gDcBlock ? std::fabs(fresh.out.l - g.out.l) < 1e-5f && std::fabs(fresh.out.r - g.out.r) < 1e-5f
			                         : fresh.out.l == g.out.l && fresh.out.r == g.out.r);
	}
	CHECK(finite, "attaching and detaching an expander must keep the output finite");
	CHECK(same, "after a detach the output must match a fresh engine (DC blocker %s)", gDcBlock ? "on" : "off");
}

// An expander that writes NaN on one sample.
struct Poison : rka::BallastExpander {
	bool armed = true;
	void postLevel(int, float& l, float& r, rka::BallastSource) override {
		if (armed)
			l = r = NAN;
		armed = false;
	}
};

// One NaN from an expander must not leave the outputs dead (DC blocker on,
// the default).
void expander_nan_recovers() {
	Rig g;
	g.f.dcBlock = true;
	Poison p;
	g.chain.push_back(&p);
	settle(g, 1.f);
	bool finite = true;
	for (int i = 0; i < 4800; i++) {
		g.step();
		finite = finite && g.out.l == g.out.l && g.out.r == g.out.r;
	}
	CHECK(finite, "a NaN from an expander left the outputs NaN (L %g, R %g)", g.out.l, g.out.r);
}

// The chain walk over a stand-in for Rack's modules.
struct Node {
	Node* right = nullptr;
	bool bypassed = false;
	rka::BallastExpander* x = nullptr;
};

int walk(Node* first, rka::BallastExpander** out, int max) {
	return rka::collectBallastExpanders(first, out, max, [](Node* n) { return n->right; },
	                                    [](Node* n) { return n->bypassed; }, [](Node* n) { return n->x; });
}

// One expander of each nonzero kind: the first unbypassed one.
void one_of_each_kind() {
	Gain a(Gain::POST, 0, 1.f, 0.f), b(Gain::POST, 0, 1.f, 0.f), c(Gain::POST, 0, 1.f, 0.f), d(Gain::POST, 0, 1.f, 0.f);
	a.kindId = b.kindId = 1;
	Node na, nb, nc, nd;
	na.x = &a;
	nb.x = &b;
	nc.x = &c;
	nd.x = &d;
	na.right = &nb;
	nb.right = &nc;
	nc.right = &nd;
	rka::BallastExpander* out[8];
	int k = walk(&na, out, 8);
	CHECK(k == 3 && out[0] == &a && out[1] == &c && out[2] == &d,
	      "two of kind 1: the walk kept %d (want a, then the kind-0 c and d)", k);
	na.bypassed = true;
	k = walk(&na, out, 8);
	CHECK(k == 3 && out[0] == &b && out[1] == &c, "with a bypassed, b must act (kept %d)", k);
}

// A chain whose links loop (two Pans pointing at each other while being
// dragged, or a damaged patch) must still end: the walk is capped by steps
// taken, not by expanders kept (a second Pan or a bypassed one is skipped
// without counting).
void walk_ends_on_a_loop() {
	Gain a(Gain::POST, 0, 1.f, 0.f), b(Gain::POST, 0, 1.f, 0.f);
	a.kindId = b.kindId = 1;
	Node na, nb;
	na.x = &a;
	nb.x = &b;
	na.right = &nb;
	nb.right = &na;
	rka::BallastExpander* out[8];
	const int k = walk(&na, out, 8);
	CHECK(k == 1 && out[0] == &a, "two Pans in a loop: kept %d (want 1, a)", k);
	na.bypassed = nb.bypassed = true;
	CHECK(walk(&na, out, 8) == 0, "two bypassed expanders in a loop: the walk must end with none kept");
}

void linked_every_walk() {
	Gain a(Gain::POST, 0, 1.f, 0.f), b(Gain::POST, 0, 1.f, 0.f);
	rka::BallastExpander* chain[2] = {&a, &b};
	rka::BallastLinker linker;
	linker.link(chain, 2);
	linker.link(chain, 2);
	CHECK(a.links == 2 && b.links == 2, "linked() calls: %d and %d, want 2 each", a.links, b.links);
}

// attached() comes from Ballast's own thread, once, on the first sample an
// expander is in the chain after not being in it: never from a counter the
// expander's thread also writes (with 4 engine threads that raced, and Pan
// restarted from centre every few samples: the crackle).
void attached_on_change() {
	Gain a(Gain::POST, 0, 1.f, 0.f), b(Gain::POST, 0, 1.f, 0.f);
	rka::BallastLinker linker;
	rka::BallastExpander* ab[2] = {&a, &b};
	rka::BallastExpander* onlyA[1] = {&a};
	rka::BallastExpander* onlyB[1] = {&b};
	linker.link(onlyA, 1);
	CHECK(a.attaches == 1, "a first linked: %d attaches, want 1", a.attaches);
	for (int i = 0; i < 1000; i++)
		linker.link(onlyA, 1);
	CHECK(a.attaches == 1, "a linked 1000 more times: %d attaches, want still 1", a.attaches);
	linker.link(ab, 2);
	CHECK(a.attaches == 1 && b.attaches == 1, "b joins: attaches %d and %d, want 1 and 1", a.attaches, b.attaches);
	linker.link(onlyB, 1);
	linker.link(ab, 2);
	CHECK(a.attaches == 2 && b.attaches == 1, "a leaves and returns: attaches %d and %d, want 2 and 1", a.attaches,
	      b.attaches);
	linker.link(nullptr, 0);
	linker.link(onlyB, 1);
	CHECK(b.attaches == 2, "an empty chain then b: b attaches %d, want 2", b.attaches);
}

void walk_skips_bypassed_and_stops_at_strangers() {
	Gain a(Gain::POST, 0, 1.f, 0.f), b(Gain::POST, 0, 1.f, 0.f), c(Gain::POST, 0, 1.f, 0.f);
	Node n1, n2, n3, stranger, n4;
	n1.x = &a;
	n2.x = &b;
	n2.bypassed = true;
	n3.x = &c;
	n4.x = &a;
	n1.right = &n2;
	n2.right = &n3;
	n3.right = &stranger;
	stranger.right = &n4;
	rka::BallastExpander* out[8];
	const int k = walk(&n1, out, 8);
	CHECK(k == 2 && out[0] == &a && out[1] == &c,
	      "the walk found %d (want 2: a, then c; b is bypassed, the stranger ends the chain)", k);
	CHECK(walk(nullptr, out, 8) == 0, "no neighbour: no expanders");
	CHECK(walk(&n1, out, 1) == 1, "the walk must stop at max");
}

} // namespace

int main() {
	post_level_same_sample();
	pre_level_before_level();
	into_mix_before_master();
	source_type();
	chain_order();
	for (bool dc : {false, true}) {
		gDcBlock = dc;
		no_expander_bit_identical();
		expander_attach_detach();
	}
	gDcBlock = false;
	expander_nan_recovers();
	walk_skips_bypassed_and_stops_at_strangers();
	walk_ends_on_a_loop();
	one_of_each_kind();
	linked_every_walk();
	attached_on_change();
	return rkatest::finish("ballast test_expander");
}

// Eddy's worker bookkeeping (src/eddy/WorkerBook.hpp): the final review's
// scenarios as sequences of events with the decisions expected (I1's launch,
// I3, the three ways the stored load's flag stayed set, a Clear during a
// load, the newest request winning, the patch's Play and Dub surviving), then
// a seeded sweep: a stand-in for the module (carrying out the decisions as
// Eddy.cpp does) and for the engine (taking, refusing and retiring sets for
// their rate, and Clear's ordering against offers), with rate changes,
// Clears, loads and failures at every stage, and the invariants checked
// whenever it has settled. Built with the sanitizers.
#include "eddy/WorkerBook.hpp"
#include "testing.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {

using rka::eddy::laterSeq;
using rka::eddy::Request;
using rka::eddy::WorkerBook;
typedef WorkerBook::SetId SetId;

// Distinct set ids for the scenarios.
int gIds[64];
SetId id(int k) {
	return &gIds[k];
}

Request req(Request::Kind kind, uint32_t seq, bool transport = false, const char* path = "") {
	Request r;
	r.kind = kind;
	r.seq = seq;
	r.transport = transport;
	r.path = path;
	r.epoch = 7;
	return r;
}

// ---- Scenarios.

// I1, at launch on Rack's "Auto" rate: the module is made at 44.1 kHz, its
// stored load runs, and the rate becomes 48 kHz before the job ends. The
// load runs again from the files at 48 kHz (no conversion), whole, with the
// patch's Play and Dub to apply when it is offered.
void i1_launch_during_job() {
	WorkerBook b;
	const Request fresh = req(Request::FRESH, 1), stored = req(Request::LOAD_STORED, 2, true, "/patch/eddy");
	b.started(fresh);
	WorkerBook::Finished d = b.finished(fresh, 44100.f, 44100.f, id(1), false, false, 0, nullptr);
	CHECK(d.pending && !d.rerun, "the empty tape waits to be offered");
	WorkerBook::Offer o = b.offer(nullptr, 0);
	CHECK(o.set == id(1) && !o.drop && !o.empty && !o.watch, "the empty tape is offered as it is");
	CHECK(!b.offered(), "no Play and Dub with the empty tape");
	b.started(stored);
	CHECK(b.storing(), "the stored load is under way");
	d = b.finished(stored, 44100.f, 48000.f, id(2), true, false, 0, nullptr);
	CHECK(d.rerun && d.limbo == id(2) && !d.pending, "made at 44.1 kHz with 48 kHz now in force: freed, and made again");
	CHECK(d.next.kind == Request::LOAD_STORED && d.next.seq == 2 && d.next.transport && d.next.path == "/patch/eddy",
	      "made again as the same request: the stored files, Play and Dub to apply (kind %d, seq %u)", (int) d.next.kind, d.next.seq);
	CHECK(!d.storeOff, "the stored load goes on");
	// Meanwhile the empty tape came back for its rate, with nothing waiting
	// after it: made again (the stored load may yet fail, and the engine
	// must have a tape), cheaply, and given way to the stored tape.
	const Request again = d.next;
	WorkerBook::HandedBack h = b.handedBack(id(1), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN && h.next.kind == Request::FRESH && h.next.seq == 1,
	      "the empty tape handed back with nothing waiting: made again, empty (%d)", (int) h.remake);
	b.started(h.next);
	d = b.finished(h.next, 48000.f, 48000.f, id(4), false, false, 0, nullptr);
	CHECK(d.pending && b.pending() == id(4), "the new empty tape waits");
	b.started(again);
	d = b.finished(again, 48000.f, 48000.f, id(3), true, false, 0, nullptr);
	CHECK(d.pending && !d.rerun && !d.storeOff && d.limbo == id(4), "at 48 kHz the stored tape waits to be offered, in place of the empty one");
	o = b.offer(nullptr, 0);
	CHECK(o.set == id(3) && o.watch && o.whole && o.epoch == 7, "offered as the stored load's set, whole (the save's baseline), epoch %u",
	      o.epoch);
	CHECK(b.offered(), "the patch's Play and Dub are applied as it is offered");
	CHECK(b.storing(), "the stored load ends when the set goes live (the audio thread)");
	CHECK(!b.freed(id(1)) && !b.freed(id(2)), "freeing the old sets does not end it");
	CHECK(b.storing(), "still under way");
	h = b.handedBack(id(3), true);
	CHECK(h.storeOff && !b.storing(), "once it went live and came back, the load is over");
}

// I1, the other order: the stored load finished at 44.1 kHz, was offered,
// and the engine refused it for its rate. It never went live: loaded again
// from the files, not converted.
void i1_launch_refused() {
	WorkerBook b;
	const Request fresh = req(Request::FRESH, 1), stored = req(Request::LOAD_STORED, 2, true, "/patch/eddy");
	b.started(fresh);
	b.finished(fresh, 44100.f, 44100.f, id(1), false, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	b.started(stored);
	WorkerBook::Finished d = b.finished(stored, 44100.f, 44100.f, id(2), true, false, 0, nullptr);
	CHECK(d.pending, "the stored tape waits (the empty one is still in the engine's hands)");
	// The rate becomes 48 kHz; the engine refuses the empty tape.
	WorkerBook::HandedBack h = b.handedBack(id(1), false);
	CHECK(h.remake == WorkerBook::HandedBack::NONE, "the empty tape: the stored one waits, so not made again");
	CHECK(b.pending() == id(2), "the stored tape still waits (I3)");
	WorkerBook::Offer o = b.offer(nullptr, 0);
	CHECK(o.set == id(2) && o.watch && o.whole, "the stored tape is offered");
	CHECK(b.offered(), "with Play and Dub");
	h = b.handedBack(id(2), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN, "refused for its rate, never live: loaded again from its files (%d)", (int) h.remake);
	CHECK(h.next.kind == Request::LOAD_STORED && h.next.seq == 2 && h.next.transport && h.next.fallback,
	      "as the same request, Play and Dub still to apply, an empty tape if the files cannot be read");
	CHECK(!h.storeOff && b.storing(), "the stored load goes on");
	CHECK(!b.freed(id(2)), "freeing the refused set while its re-load is posted does not end the load");
	b.started(h.next);
	d = b.finished(h.next, 48000.f, 48000.f, id(3), true, false, 0, nullptr);
	o = b.offer(nullptr, 0);
	CHECK(d.pending && o.set == id(3) && o.whole && o.watch, "loaded again at 48 kHz, whole: the save's baseline holds");
	CHECK(b.offered(), "and Play and Dub (a saved Dub on survives)");
}

// I3: a set waiting to be offered is never thrown away for the set offered
// before it. The empty tape is still in the engine's hands when the stored
// load finishes; the empty tape is refused for its rate.
void i3_pending_stands() {
	WorkerBook b;
	const Request fresh = req(Request::FRESH, 1), stored = req(Request::LOAD_STORED, 2, true, "/d");
	b.started(fresh);
	b.finished(fresh, 44100.f, 44100.f, id(1), false, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	b.started(stored);
	b.finished(stored, 44100.f, 44100.f, id(2), true, false, 0, nullptr);
	const WorkerBook::HandedBack h = b.handedBack(id(1), false);
	CHECK(h.remake == WorkerBook::HandedBack::NONE && !h.storeOff, "the empty tape refused: nothing made in place of the stored tape");
	CHECK(b.pending() == id(2) && b.storing(), "the stored tape still waits and its load is still under way");
	// A used set handed back with a later one waiting: not converted either.
	WorkerBook c;
	const Request load = req(Request::LOAD, 2, false, "/a.wav");
	c.started(fresh);
	c.finished(fresh, 48000.f, 48000.f, id(5), false, false, 0, nullptr);
	c.offer(nullptr, 0);
	c.offered();
	c.started(load);
	c.finished(load, 48000.f, 48000.f, id(6), false, false, 0, nullptr);
	const WorkerBook::HandedBack k = c.handedBack(id(5), true);
	CHECK(k.remake == WorkerBook::HandedBack::NONE && c.pending() == id(6), "a live tape retired with a newer file waiting: the file stands");
}

// The newest request always wins: a set for an older request never takes
// the place of one waiting for a newer one.
void newest_wins() {
	WorkerBook b;
	const Request a = req(Request::LOAD, 4, false, "/a.wav"), n = req(Request::LOAD, 5, false, "/b.wav");
	b.started(n);
	WorkerBook::Finished d = b.finished(n, 48000.f, 48000.f, id(2), false, false, 0, nullptr);
	CHECK(d.pending && b.pending() == id(2), "the newer file waits");
	b.started(a);
	d = b.finished(a, 48000.f, 48000.f, id(1), false, false, 0, nullptr);
	CHECK(!d.pending && d.limbo == id(1) && b.pending() == id(2), "the older one, finishing later, is freed; the newer still waits");
	// A newer one supersedes an older one waiting, and takes its Play and Dub.
	WorkerBook c;
	const Request p = req(Request::LOAD_STORED, 2, true, "/d"), q = req(Request::LOAD, 3, false, "/c.wav");
	c.started(p);
	c.finished(p, 48000.f, 48000.f, id(3), true, false, 0, nullptr);
	c.started(q);
	d = c.finished(q, 48000.f, 48000.f, id(4), false, false, 0, nullptr);
	CHECK(d.pending && d.limbo == id(3) && d.setFile, "the newer file replaces the stored tape waiting, and becomes the patch's file");
	CHECK(d.storeOff && !c.storing(), "the stored load is over: superseded");
	const WorkerBook::Offer o = c.offer(id(9), 0);
	CHECK(o.set == id(4) && !o.watch, "the file is offered, not watched as the stored load");
	CHECK(c.offered(), "with the patch's Play and Dub the stored tape would have brought");
}

// The ledger's first way in: the stored set refused for its rate and put
// aside (not the last offered: a newer set was offered meanwhile). The newer
// set ended the stored load when it was made.
void store_way_refused_not_last() {
	WorkerBook b;
	const Request stored = req(Request::LOAD_STORED, 2, true, "/d"), load = req(Request::LOAD, 3, false, "/a.wav");
	b.started(stored);
	b.finished(stored, 44100.f, 44100.f, id(1), true, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	b.started(load);
	WorkerBook::Finished d = b.finished(load, 48000.f, 48000.f, id(2), false, false, 0, nullptr);
	CHECK(d.storeOff, "a later request's set ends the stored load");
	b.offer(nullptr, 0);
	b.offered();
	const WorkerBook::HandedBack h = b.handedBack(id(1), false);
	CHECK(h.remake == WorkerBook::HandedBack::NONE, "the stored set, no longer the last offered: not made again");
	b.freed(id(1));
	CHECK(!b.storing(), "the flag stays cleared");
}

// The second way in: the stored set comes back for its rate and cannot be
// made again (its files gone, then no memory for an empty tape). The load
// is over (and failed: the save leaves the folder alone while the tape is
// empty), and Play and Dub are applied with no tape to wait for.
void store_way_remake_fails() {
	WorkerBook b;
	const Request stored = req(Request::LOAD_STORED, 2, true, "/d");
	b.started(stored);
	b.finished(stored, 44100.f, 44100.f, id(1), true, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	WorkerBook::HandedBack h = b.handedBack(id(1), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN && b.storing(), "loaded again");
	b.started(h.next);
	WorkerBook::Finished d = b.finished(h.next, 48000.f, 48000.f, nullptr, false, false, 0, nullptr);
	CHECK(d.storeOff && d.storeFailed && !b.storing(), "its files cannot be read: the load is over, and failed");
	CHECK(d.rerun && d.next.kind == Request::FRESH && !d.next.fallback && d.next.transport && d.next.seq == 2,
	      "an empty tape in its place, as the same request");
	b.started(d.next);
	d = b.finished(d.next, 48000.f, 48000.f, nullptr, false, false, 0, nullptr);
	CHECK(!d.rerun && !d.pending && d.applyTransport, "no memory for it either: nothing to offer, Play and Dub applied now");
	CHECK(!b.storing(), "the flag stays cleared");
	// The same through a conversion: the stored set went live, came back for
	// the rate, and cannot be converted or replaced.
	WorkerBook c;
	c.started(stored);
	c.finished(stored, 48000.f, 48000.f, id(2), true, false, 0, nullptr);
	c.offer(nullptr, 0);
	c.offered();
	h = c.handedBack(id(2), true);
	CHECK(h.remake == WorkerBook::HandedBack::CONVERT && h.storeOff && !c.storing(), "live and retired: converted, the load over");
	CHECK(!h.next.transport, "converted from a live tape: Play and Dub were applied already");
	c.started(h.next);
	d = c.finished(h.next, 96000.f, 96000.f, nullptr, false, false, 0, nullptr);
	CHECK(d.rerun && d.next.kind == Request::FRESH && !d.storeOff, "cannot convert: an empty tape in its place");
}

// The third way in: the stored set offered, refused for its rate, and
// another set offered before it is collected.
void store_way_other_offered_first() {
	WorkerBook b;
	const Request fresh = req(Request::FRESH, 1), stored = req(Request::LOAD_STORED, 2, true, "/d"), load = req(Request::LOAD, 3, false, "/a");
	b.started(fresh);
	b.finished(fresh, 44100.f, 44100.f, id(1), false, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	b.started(stored);
	b.finished(stored, 44100.f, 44100.f, id(2), true, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	// Refused, and before the worker collects it a later file is made and
	// offered.
	b.started(load);
	WorkerBook::Finished d = b.finished(load, 48000.f, 48000.f, id(3), false, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	CHECK(d.storeOff && !b.storing(), "the later file ends the stored load");
	WorkerBook::HandedBack h = b.handedBack(id(2), false);
	CHECK(h.remake == WorkerBook::HandedBack::NONE, "the stored set comes back after: not made again");
	CHECK(!b.freed(id(2)) && !b.storing(), "freed; the flag stays cleared");
	// And if what was offered meanwhile is not a later request's but the
	// stored set itself goes unclaimed: freed with nothing of its load left,
	// the load ends there.
	WorkerBook c;
	c.started(stored);
	c.finished(stored, 44100.f, 44100.f, id(4), true, false, 0, nullptr);
	c.offer(nullptr, 0);
	c.offered();
	c.handedBack(id(5), false);   // a set the book never made: ignored
	CHECK(c.storing(), "still under way");
	h = c.handedBack(id(4), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN, "made again");
	c.started(h.next);
	d = c.finished(h.next, 44100.f, 44100.f, id(6), true, false, 0, nullptr);
	CHECK(d.pending, "and waits");
	c.freed(id(4));
	CHECK(c.storing(), "freeing the refused one while its re-load waits does not end it");
	// A later file replaces it waiting: freed, the load is over.
	const Request file = req(Request::LOAD, 3, false, "/f");
	c.started(file);
	d = c.finished(file, 44100.f, 44100.f, id(7), false, false, 0, nullptr);
	CHECK(d.limbo == id(6) && d.storeOff && !c.storing(), "a later file replaces the re-load waiting: the load is over");
	CHECK(!c.freed(id(6)) && !c.storing(), "and freeing it leaves it so");
	// A re-load of the stored tape posted first in the queue, but run after
	// a later file the worker had already taken: the file ends the load, and
	// the re-load starting after does not start it again, nor fail it.
	WorkerBook e;
	e.started(stored);
	e.finished(stored, 44100.f, 44100.f, id(8), true, false, 0, nullptr);
	e.offer(nullptr, 0);
	e.offered();
	h = e.handedBack(id(8), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN, "refused: loaded again");
	e.started(file);
	d = e.finished(file, 48000.f, 48000.f, id(9), false, false, 0, nullptr);
	CHECK(d.storeOff && !e.storing(), "the later file ends the stored load");
	e.started(h.next);
	CHECK(!e.storing(), "the re-load starting after does not start it again");
	d = e.finished(h.next, 48000.f, 48000.f, nullptr, false, false, 0, nullptr);
	CHECK(!d.storeOff && !d.storeFailed, "nor, failing, report the stored load failed");
}

// A conversion refused in its turn (the rate moved again before it went
// live) is converted again from the live tape it came from, which is kept
// till then: never a conversion of a conversion.
void conversion_from_source() {
	WorkerBook b;
	const Request load = req(Request::LOAD, 2, false, "/a.wav");
	b.started(load);
	b.finished(load, 48000.f, 48000.f, id(1), false, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	b.wentLive(id(1));
	WorkerBook::HandedBack h = b.handedBack(id(1), true);
	CHECK(h.remake == WorkerBook::HandedBack::CONVERT && h.from == id(1), "the live tape retired: converted from itself");
	b.started(h.next);
	WorkerBook::Finished d = b.finished(h.next, 44100.f, 44100.f, id(2), false, false, 0, nullptr, h.from);
	b.offer(nullptr, 0);
	b.offered();
	CHECK(b.needed(id(1)), "its source is kept while the conversion is not yet live");
	h = b.handedBack(id(2), false);
	CHECK(h.remake == WorkerBook::HandedBack::CONVERT && h.from == id(1), "refused in its turn: converted again from the live tape it came from");
	b.started(h.next);
	d = b.finished(h.next, 96000.f, 96000.f, id(3), false, false, 0, nullptr, h.from);
	b.offer(nullptr, 0);
	b.offered();
	CHECK(b.needed(id(1)) && !b.needed(id(2)), "the source is still kept; the refused conversion is not");
	b.wentLive(id(3));
	CHECK(!b.needed(id(1)), "once the conversion is live the source can go");
	h = b.handedBack(id(3), true);
	CHECK(h.remake == WorkerBook::HandedBack::CONVERT && h.from == id(3), "a live conversion retired: converted from itself, the user's tape");
	(void) d;
}

// A Clear or Initialize during a load (M1): the cancelled load's set is not
// offered over a usable live set (which may hold a recording made after the
// Clear) and does not become the patch's file; with no usable live set it is
// offered emptied. A stored load cancelled so is over.
void clear_during_load() {
	WorkerBook b;
	const Request load = req(Request::LOAD, 2, true, "/a.wav");
	b.started(load);
	WorkerBook::Finished d = b.finished(load, 48000.f, 48000.f, id(1), false, false, 3, nullptr);
	CHECK(d.pending && !d.setFile, "a load asked for before the Clear (3) is not the patch's file");
	WorkerBook::Offer o = b.offer(id(9), 3);
	CHECK(o.set == id(1) && o.drop && !o.empty, "with a usable live set it is dropped, not offered");
	CHECK(!b.pending(), "nothing waits");
	// No usable live set: offered emptied, as an empty tape as new as the Clear.
	WorkerBook c;
	c.started(load);
	c.finished(load, 48000.f, 48000.f, id(2), false, false, 3, nullptr);
	o = c.offer(nullptr, 3);
	CHECK(o.set == id(2) && o.empty && !o.drop && !o.watch, "with no usable live set it is offered emptied");
	CHECK(!c.offered(), "without the patch's Play and Dub");
	const Request* r = c.requestOf(id(2));
	CHECK(r && r->kind == Request::FRESH && r->seq == 3, "it stands for an empty tape now (so a re-make of it is empty)");
	const WorkerBook::HandedBack h = c.handedBack(id(2), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN && h.next.kind == Request::FRESH, "refused for its rate: made again empty, not loaded");
	// The stored load cancelled.
	WorkerBook s;
	const Request stored = req(Request::LOAD_STORED, 2, true, "/d");
	s.started(stored);
	s.finished(stored, 48000.f, 48000.f, id(3), true, false, 0, nullptr);
	o = s.offer(id(9), 3);
	CHECK(o.drop && o.storeOff && !o.watch && !s.storing(), "a stored load cleared before it is offered: dropped, the load over");
	// Opened and cleared at once (the sweep's seed 2086): the empty tape is
	// emptied as new as the Clear and offered; the stored load, finishing
	// after, is from before the Clear and older than what stands: not kept,
	// and its load is over.
	WorkerBook t;
	const Request fresh = req(Request::FRESH, 1);
	t.started(fresh);
	t.finished(fresh, 44100.f, 44100.f, id(5), false, false, 3, nullptr);
	o = t.offer(nullptr, 3);
	CHECK(o.empty, "the empty tape from before the Clear is offered emptied");
	t.offered();
	t.started(stored);
	CHECK(t.storing(), "the stored load is under way");
	d = t.finished(stored, 44100.f, 44100.f, id(6), true, false, 3, nullptr);
	CHECK(!d.pending && d.limbo == id(6) && d.storeOff && !t.storing(), "the stored tape, older than the Clear's tape: not kept, the load over");
	// A load asked for after the Clear is the patch's file.
	WorkerBook f;
	const Request later = req(Request::LOAD, 4, false, "/b.wav");
	f.started(later);
	d = f.finished(later, 48000.f, 48000.f, id(4), false, false, 3, nullptr);
	CHECK(d.setFile, "a load after the Clear becomes the patch's file");
	o = f.offer(id(9), 3);
	CHECK(!o.drop && !o.empty, "and is offered as it is");
}

// Where a set gives way to the live set, the live set stands: retired for
// its rate later, it is made again (the sweep found a tape lost so: a re-load
// cancelled by a Clear and dropped, then the live set retired with nothing
// standing). A re-make that fails with a usable tape live leaves it.
void live_stands() {
	WorkerBook b;
	const Request fresh = req(Request::FRESH, 1), load = req(Request::LOAD, 2, false, "/a.wav");
	b.started(fresh);
	b.finished(fresh, 44100.f, 44100.f, id(1), false, false, 0, nullptr);
	b.offer(nullptr, 0);
	b.offered();
	b.wentLive(id(1));
	b.started(load);
	b.finished(load, 48000.f, 48000.f, id(2), false, false, 0, id(1));
	b.offer(id(1), 0);
	b.offered();
	// The file was made at 48 kHz; the rate went back to 44.1: refused.
	WorkerBook::HandedBack h = b.handedBack(id(2), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN, "the file refused for its rate: loaded again");
	b.started(h.next);
	b.finished(h.next, 44100.f, 44100.f, id(3), false, false, 0, id(1));
	// A Clear came first: dropped over the live empty tape.
	const WorkerBook::Offer o = b.offer(id(1), 4);
	CHECK(o.drop && b.lastOffered() == id(1), "dropped by the Clear: the live tape stands as the last offered");
	h = b.handedBack(id(1), true);
	CHECK(h.remake == WorkerBook::HandedBack::CONVERT && h.from == id(1), "the live tape retired later: converted, not lost");
	// A re-make that fails with a usable tape live leaves it standing.
	WorkerBook c;
	c.started(fresh);
	c.finished(fresh, 48000.f, 48000.f, id(5), false, false, 0, nullptr);
	c.offer(nullptr, 0);
	c.offered();
	c.started(load);
	c.finished(load, 44100.f, 44100.f, id(6), false, false, 0, nullptr);
	c.offer(id(5), 0);
	c.offered();
	h = c.handedBack(id(6), false);
	CHECK(h.remake == WorkerBook::HandedBack::RERUN, "the file refused: loaded again");
	c.started(h.next);
	const WorkerBook::Finished d = c.finished(h.next, 48000.f, 48000.f, nullptr, false, false, 0, id(5));
	CHECK(!d.rerun && !d.pending && !d.applyTransport && c.lastOffered() == id(5),
	      "it cannot be loaded again: the live tape stays and stands, no empty tape over it");
	h = c.handedBack(id(5), true);
	CHECK(h.remake == WorkerBook::HandedBack::CONVERT, "and is made again if it is retired");
}

// A re-make keeps going while the rate keeps moving; a cut load is
// reported to the save; a load that fails applies Play and Dub at once.
void rerun_and_failures() {
	WorkerBook b;
	const Request stored = req(Request::LOAD_STORED, 2, true, "/d");
	b.started(stored);
	WorkerBook::Finished d = b.finished(stored, 44100.f, 48000.f, id(1), true, false, 0, nullptr);
	b.started(d.next);
	d = b.finished(d.next, 48000.f, 96000.f, id(2), true, false, 0, nullptr);
	CHECK(d.rerun && d.limbo == id(2), "the rate moved again: made again again");
	b.started(d.next);
	d = b.finished(d.next, 96000.f, 96000.f, id(3), false, true, 0, nullptr);
	WorkerBook::Offer o = b.offer(nullptr, 0);
	CHECK(d.pending && o.watch && !o.whole && o.cut, "at 96 kHz, held short by the capacity: reported as cut for the save");
	WorkerBook c;
	const Request load = req(Request::LOAD, 2, true, "/missing.wav");
	c.started(load);
	d = c.finished(load, 48000.f, 48000.f, nullptr, false, false, 0, nullptr);
	CHECK(!d.rerun && !d.pending && d.applyTransport && !d.storeOff, "a file that cannot be loaded: Play and Dub applied now");
	WorkerBook s;
	s.started(stored);
	d = s.finished(stored, 48000.f, 48000.f, nullptr, false, false, 0, nullptr);
	CHECK(d.storeOff && d.storeFailed && !s.storing() && d.applyTransport, "a stored load that fails: over, failed, Play and Dub applied");
}

// ---- The sweep.

struct FakeSet {
	float fs;
	bool used = false;
	bool empty = false;
	std::string content;   // where it came from: "", "stored", "file:<n>", with "~" for each conversion
	uint32_t seq;
};

struct Sim {
	WorkerBook book;
	std::vector<std::unique_ptr<FakeSet>> sets;
	std::deque<Request> queue;
	float fs = 44100.f;
	uint32_t seq = 0, clearSeq = 0;
	// The engine.
	FakeSet* incoming = nullptr;
	FakeSet* live = nullptr;
	std::vector<FakeSet*> retired, limbo;
	uint32_t gen = 0, offerGen = 0, clearGen = 0, clearDone = 0;
	// The module's flags.
	bool storeLoading = false;
	FakeSet* storeSet = nullptr;
	bool storeFailed = false;
	std::string file;
	// What happened.
	uint32_t newestMade = 0;          // the newest request a set was made for
	std::string newestContent;
	long convertedUnused = 0, olderOverNewer = 0, offers = 0, conversions = 0, reloads = 0;
	uint32_t rng;
	bool failures;

	explicit Sim(uint32_t seed, bool withFailures) : rng(seed), failures(withFailures) {}

	uint32_t next() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return rng;
	}
	bool chance(int per1000) {
		return (int) (next() % 1000) < per1000;
	}
	FakeSet* usableLive() {
		return live && live->fs == fs ? live : nullptr;
	}
	FakeSet* find(SetId s) {
		for (auto& p : sets)
			if (p.get() == s)
				return p.get();
		return nullptr;
	}

	// ---- The UI.
	void post(Request::Kind kind, const std::string& path, bool transport) {
		Request r;
		r.kind = kind;
		r.path = path;
		r.transport = transport;
		r.seq = ++seq;
		if (kind == Request::LOAD_STORED)
			storeLoading = true;
		queue.push_back(r);
	}
	void clear() {
		clearSeq = ++seq;
		clearGen = ++gen;
	}

	// ---- The engine (EddyEngine's handoff and Clear, in outline).
	bool later(uint32_t a, uint32_t b) {
		return (int32_t) (a - b) > 0;
	}
	void engineStep() {
		if (live && live->fs != fs) {
			retired.push_back(live);
			live = nullptr;
		}
		if (clearGen != clearDone) {
			if (incoming) {
				if (later(offerGen, clearGen))
					clearDone = clearGen;
			}
			else if (live) {
				clearDone = clearGen;
				live->empty = true;
				live->content.clear();
			}
		}
		if (incoming) {
			FakeSet* s = incoming;
			incoming = nullptr;
			if (s->fs != fs)
				retired.push_back(s);
			else {
				if (live)
					retired.push_back(live);
				live = s;
				s->used = true;
				if (storeSet == s) {
					storeSet = nullptr;
					storeLoading = false;
				}
			}
		}
	}
	bool offer(FakeSet* s) {
		if (incoming)
			return false;
		offerGen = ++gen;
		incoming = s;
		return true;
	}

	// ---- The worker (Eddy.cpp's, in outline).
	FakeSet* make(const Request& r, const FakeSet* from, float rate) {
		// Only loads fail here (a file gone, the folder unreadable); running
		// out of memory for every tape is not a state to promise anything of.
		if (failures && (r.kind == Request::LOAD || r.kind == Request::LOAD_STORED) && chance(150))
			return nullptr;
		auto s = std::unique_ptr<FakeSet>(new FakeSet);
		s->fs = rate;
		s->seq = r.seq;
		switch (r.kind) {
			case Request::FRESH: s->empty = true; break;
			case Request::LOAD: s->content = "file:" + r.path; break;
			case Request::LOAD_STORED: s->content = "stored"; break;
			case Request::CONVERT:
				if (!from)
					return nullptr;
				conversions++;
				if (!from->used)
					convertedUnused++;   // I1: never a conversion of what can be read again
				s->content = from->content + "~";
				s->empty = from->empty;
				break;
		}
		if (r.kind == Request::LOAD || r.kind == Request::LOAD_STORED)
			reloads++;
		FakeSet* p = s.get();
		sets.push_back(std::move(s));
		return p;
	}
	void produce(Request r, const FakeSet* from) {
		for (;;) {
			book.started(r);
			const float used = fs;
			FakeSet* s = make(r, from, used);
			// The rate may move while the job runs.
			if (chance(150))
				fs = chance(500) ? 44100.f : chance(500) ? 48000.f : 96000.f;
			const SetId before = book.pending();
			const FakeSet* was = find(before);
			const WorkerBook::Finished d = book.finished(r, used, fs, s, false, false, clearSeq, usableLive(), from);
			if (d.pending && was && d.limbo == before && laterSeq(was->seq, r.seq))
				olderOverNewer++;
			if (d.pending && !laterSeq(newestMade, r.seq)) {
				newestMade = r.seq;
				newestContent = s->content;
			}
			// A re-make that failed with a usable tape live: that tape stands
			// (as when a load fails, the tape stays as it was).
			if (!s && r.fallback && !d.rerun && usableLive()) {
				newestMade = usableLive()->seq;
				newestContent = usableLive()->content;
			}
			if (d.storeOff)
				storeLoading = false;
			if (d.storeFailed)
				storeFailed = true;
			if (d.limbo)
				limbo.push_back(find(d.limbo));
			if (d.setFile)
				file = r.path;
			if (!d.rerun)
				break;
			r = d.next;
		}
		offerPending();
	}
	void handedBack(FakeSet* s) {
		const WorkerBook::HandedBack h = book.handedBack(s, s->used);
		if (h.storeOff)
			storeLoading = false;
		if (h.remake == WorkerBook::HandedBack::RERUN)
			queue.push_front(h.next);
		else if (h.remake == WorkerBook::HandedBack::CONVERT)
			produce(h.next, find(h.from));
		limbo.push_back(s);
	}
	void offerPending() {
		if (!book.pending())
			return;
		const WorkerBook::Offer o = book.offer(usableLive(), clearSeq);
		FakeSet* s = find(o.set);
		if (o.storeOff)
			storeLoading = false;
		if (o.drop) {
			limbo.push_back(s);
			return;
		}
		if (o.empty) {
			s->empty = true;
			s->content.clear();
		}
		if (o.watch)
			storeSet = s;
		if (!offer(s))
			return;
		offers++;
		book.offered();
	}
	void freeLimbo() {
		std::vector<FakeSet*> keep;
		for (FakeSet* s : limbo) {
			if (!s)
				continue;
			if (s == live || s == incoming || s == book.pending() || book.needed(s)) {
				keep.push_back(s);
				continue;
			}
			bool retiredYet = false;
			for (FakeSet* r : retired)
				retiredYet = retiredYet || r == s;
			if (retiredYet) {
				keep.push_back(s);
				continue;
			}
			if (storeSet == s)
				storeSet = nullptr;
			if (book.freed(s))
				storeLoading = false;
			for (size_t i = 0; i < sets.size(); i++)
				if (sets[i].get() == s) {
					sets.erase(sets.begin() + (long) i);
					break;
				}
			for (FakeSet*& k : keep)
				if (k == s)
					k = nullptr;
		}
		limbo.clear();
		for (FakeSet* s : keep)
			if (s && std::find(limbo.begin(), limbo.end(), s) == limbo.end())
				limbo.push_back(s);
	}
	void service() {
		while (!retired.empty()) {
			FakeSet* s = retired.front();
			retired.erase(retired.begin());
			handedBack(s);
		}
		if (live && live == book.lastOffered())
			book.wentLive(live);
		offerPending();
		freeLimbo();
	}
	void workerStep() {
		if (!queue.empty()) {
			const Request r = queue.front();
			queue.pop_front();
			service();
			produce(r, nullptr);
		}
		service();
	}
	bool settled() const {
		return queue.empty() && !incoming && retired.empty() && !book.pending();
	}
};

void sweep() {
	long runs = 0, checked = 0, bad = 0;
	long conversions = 0, reloads = 0, offers = 0;
	for (uint32_t seed = 1; seed <= 400; seed++) {
		const bool failures = seed % 4 == 0;
		Sim m(seed * 2654435761u + 1, failures);
		// onAdd: an empty tape, then the stored one.
		m.post(Request::FRESH, "", false);
		m.post(Request::LOAD_STORED, "/d", true);
		for (int step = 0; step < 400; step++) {
			if (m.chance(40))
				m.post(Request::LOAD, std::to_string(m.next() % 5), m.chance(300));
			if (m.chance(15))
				m.clear();
			if (m.chance(60))
				m.fs = m.chance(500) ? 44100.f : m.chance(500) ? 48000.f : 96000.f;
			if (m.chance(700))
				m.engineStep();
			if (m.chance(700))
				m.workerStep();
			// Now and then, let it settle (no rate change, no failure) and look.
			if (step % 50 == 49 || step == 399) {
				const bool f = m.failures;
				m.failures = false;
				for (int k = 0; k < 200 && !m.settled(); k++) {
					m.engineStep();
					m.workerStep();
				}
				m.engineStep();
				m.workerStep();
				m.engineStep();
				m.workerStep();
				m.failures = f;
				checked++;
				bool ok = m.settled();
				ok = ok && !m.storeLoading;
				ok = ok && m.live && m.live->fs == m.fs;
				// The newest wins: after a Clear newer than every set made the
				// tape is empty; else it is the newest request's.
				if (ok && laterSeq(m.clearSeq, m.newestMade))
					ok = m.live->empty;
				else if (ok)
					ok = m.live->seq == m.newestMade && m.live->content.substr(0, m.live->content.find('~')) == m.newestContent.substr(0, m.newestContent.find('~'));
				if (!ok) {
					if (bad < 5)
						std::printf("seed %u step %d: settled %d, storeLoading %d, live %s at %g (rate %g), live seq %u, newest made %u ('%s' vs '%s'), clear %u\n",
						            seed, step, (int) m.settled(), (int) m.storeLoading, m.live ? "yes" : "no", m.live ? m.live->fs : 0.f, m.fs,
						            m.live ? m.live->seq : 0u, m.newestMade, m.live ? m.live->content.c_str() : "", m.newestContent.c_str(), m.clearSeq);
					bad++;
				}
			}
		}
		CHECK(m.convertedUnused == 0, "seed %u: %ld conversions of a set that never went live (I1)", seed, m.convertedUnused);
		CHECK(m.olderOverNewer == 0, "seed %u: %ld sets for an older request took the place of a newer one's (I3)", seed, m.olderOverNewer);
		conversions += m.conversions;
		reloads += m.reloads;
		offers += m.offers;
		runs++;
	}
	CHECK(bad == 0, "%ld of %ld settled states broke an invariant (the stored load over, a live set at the rate, the newest request's)", bad,
	      checked);
	CHECK(conversions > 100 && reloads > 1000 && offers > 1000, "the sweep reached them: %ld conversions, %ld loads, %ld offers", conversions,
	      reloads, offers);
	std::printf("eddy test_book: %ld runs, %ld settled states checked; %ld conversions, %ld loads, %ld offers\n", runs, checked, conversions,
	            reloads, offers);
}

} // namespace

int main() {
	i1_launch_during_job();
	i1_launch_refused();
	i3_pending_stands();
	newest_wins();
	store_way_refused_not_last();
	store_way_remake_fails();
	store_way_other_offered_first();
	clear_during_load();
	rerun_and_failures();
	conversion_from_source();
	live_stands();
	sweep();
	return rkatest::finish("eddy test_book");
}

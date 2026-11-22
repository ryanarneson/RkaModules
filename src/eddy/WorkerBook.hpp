#pragma once
// Eddy's worker bookkeeping: the decisions the module's worker thread
// (Eddy.cpp) makes about the tape sets it makes, offers, takes back and
// frees, and about the patch's stored load. Rack-free, C++11, no threads, no
// files: plain data in, a decision out. The module calls it from the worker
// thread only and carries out what it says (makes, loads, converts, offers,
// frees, publishes the atomics the save and the audio thread read).
//
// Requests. Every job the UI posts (an empty tape, a file, the stored tape)
// and every Clear or Initialize is numbered in the order it was asked for
// (`seq`, the module's counter; a Clear's number is `clearSeq`). A set
// carries the number of the request that made it, through every re-make.
// The newest request always wins: no set may take the place of one made for
// a later request.
//
// The rules:
// - A job that finishes at another rate than the one in force now is run
//   again at the new rate (from its source: never a conversion of a
//   conversion of something that can be read again; the final review's I1).
// - A set that comes back from the engine for its rate is made again only
//   if it is the newest: the last offered, with nothing waiting to be
//   offered after it (I3). Made again: one that never went live is loaded
//   (or made) again from its source at the new rate, with the patch's Play
//   and Dub still to apply; one that went live is the user's tape, which
//   exists nowhere else, and is converted. A conversion that never went
//   live is converted again from the same source, which is kept (not
//   freed: needed()) until the conversion goes live (wentLive()) or gives
//   way; so a tape is converted once from what was live to the rate finally
//   in force. If that fails, the live set stays if it is usable (as when a
//   load fails), else an empty set at the rate takes its place.
// - A set waiting to be offered gives way only to one for a later request,
//   and a set for an older request than the last offered is not kept.
// - A set from before a Clear is not offered over a usable live set (the
//   live set has been cleared and may already hold a new recording; M1); with
//   none it is offered emptied, so there is a tape to record into. A file
//   load from before a Clear does not become the patch's file.
// - Where a set gives way to the live set (dropped by a Clear, or a re-make
//   that failed), the live set stands from then on as the last offered: if
//   it is retired for its rate later, it is made again.
// - The stored load (onAdd's LOAD_STORED) is under way from when its job
//   starts until its set goes live (the audio thread says so), it fails, a
//   later request's set is made, a Clear cancels it, or its set is freed
//   without a re-make under way. `storeOff` says when the module is to clear
//   the save's flag; nothing here ever sets it again (only onAdd does), so a
//   flag the audio thread has cleared stays cleared.
#include <cstdint>
#include <string>
#include <vector>

namespace rka {
namespace eddy {

struct Request {
	enum Kind { FRESH, LOAD, LOAD_STORED, CONVERT } kind = FRESH;
	std::string path;          // LOAD: the file; LOAD_STORED: the folder
	uint32_t seq = 0;          // the order it was asked in (later: larger, modulo 2^32)
	uint32_t epoch = 0;        // LOAD_STORED: the store's epoch when asked
	bool transport = false;    // apply the patch's Play and Dub when its set is offered
	bool fallback = false;     // a re-make: if it cannot be made, an empty set at the rate instead
};

// a later than b, for numbers that may wrap.
inline bool laterSeq(uint32_t a, uint32_t b) {
	return (int32_t) (a - b) > 0;
}

class WorkerBook {
public:
	typedef const void* SetId;

	// What the module does when a job (or a conversion) has finished.
	struct Finished {
		bool rerun = false;       // make `next` now, at the rate now in force (and free `made`)
		Request next;
		SetId limbo = nullptr;    // to free once nobody can hold it (a set superseded, or `made` if not kept)
		bool pending = false;     // `made` now waits to be offered
		bool setFile = false;     // LOAD: the path becomes the patch's file
		bool applyTransport = false;   // nothing to offer: apply the patch's Play and Dub now
		bool storeOff = false;    // the stored load is over: clear the save's flag
		bool storeFailed = false; // ... and it failed: the save leaves the folder alone while the tape is empty
	};

	// What the module does with a set the engine handed back.
	struct HandedBack {
		enum Remake { NONE, RERUN, CONVERT } remake = NONE;
		Request next;             // RERUN: post it first in the queue; CONVERT: convert `from` to it now
		SetId from = nullptr;     // CONVERT: the set handed back, or the live tape it was itself converted from
		bool storeOff = false;
	};

	// What the module does with the set waiting to be offered.
	struct Offer {
		SetId set = nullptr;      // null: nothing waits
		bool drop = false;        // cancelled by a Clear with a usable live set: free it, offer nothing
		bool empty = false;       // cancelled with no usable live set: empty it, then offer it
		bool watch = false;       // the stored load's set: publish it, `whole`, `cut` and `epoch`, for the save
		bool whole = false, cut = false;
		uint32_t epoch = 0;
		bool storeOff = false;
	};

	// ---- Events.

	// A job starts (the stored load is under way from here; a re-load of a
	// stored load already over does not start it again).
	void started(const Request& r) {
		if (r.kind == Request::LOAD_STORED && (storing_ || r.seq != storeSeq_)) {
			storeSeq_ = r.seq;
			storing_ = true;
		}
		if (rerunning_ && r.seq == rerunSeq_)
			rerunning_ = false;
	}

	// A job or conversion for `r`, made at fsUsed, is done: `made` (null if it
	// failed), exactly the stored files if `whole`, held short by the tape's
	// capacity if `cut`. fsNow: the rate in force now; clearSeq: the last
	// Clear's number; live: the engine's live set if it is at fsNow, else
	// null; from: a conversion's source.
	Finished finished(const Request& r, float fsUsed, float fsNow, SetId made, bool whole, bool cut, uint32_t clearSeq, SetId live,
	                  SetId from = nullptr) {
		Finished d;
		if (fsNow != fsUsed) {
			// Made at a rate no longer in force: again, from the source.
			d.rerun = true;
			d.next = r;
			d.limbo = made;
			return d;
		}
		if (!made) {
			if (r.kind == Request::LOAD_STORED && storing_ && storeSeq_ == r.seq) {
				storing_ = false;
				d.storeOff = d.storeFailed = true;
			}
			if (r.fallback && live && find(live) && !pending_) {
				// A re-make that cannot be made, with a usable tape live: it
				// stays, as when a load fails, and stands from now on.
				offered_ = live;
				return d;
			}
			if (r.fallback) {
				// A re-make that cannot be made, and no usable tape: an empty
				// one instead.
				d.rerun = true;
				d.next = r;
				d.next.kind = Request::FRESH;
				d.next.path.clear();
				d.next.fallback = false;
				return d;
			}
			d.applyTransport = r.transport;
			return d;
		}
		const Entry* p = find(pending_);
		const Entry* o = find(offered_);
		if ((p && laterSeq(p->req.seq, r.seq)) || (o && laterSeq(o->req.seq, r.seq))) {
			// A later request's set waits or is offered already: it stands.
			// (A re-make of the newest, posted while a later job had already
			// been taken from the queue, finishes after it; or a load from
			// before a Clear, after an empty tape as new as the Clear.)
			d.limbo = made;
			if (storing_ && storeSeq_ == r.seq) {
				storing_ = false;
				d.storeOff = true;
			}
			return d;
		}
		Entry e;
		e.set = made;
		e.req = r;
		e.whole = whole;
		e.cut = cut;
		e.source = r.kind == Request::CONVERT ? from : nullptr;
		if (p) {
			// Superseded: the patch's Play and Dub, if not yet applied, go on
			// with the newer set.
			e.req.transport = e.req.transport || p->req.transport;
			d.limbo = pending_;
		}
		entries_.push_back(e);
		pending_ = made;
		d.pending = true;
		d.setFile = r.kind == Request::LOAD && laterSeq(r.seq, clearSeq);
		if (storing_ && laterSeq(r.seq, storeSeq_)) {
			// A later request's set: the stored tape will not go live.
			storing_ = false;
			d.storeOff = true;
		}
		return d;
	}

	// The set waiting, and what to do with it. live: the engine's live set if
	// it is at the rate in force, else null.
	Offer offer(SetId live, uint32_t clearSeq) {
		Offer o;
		Entry* p = find(pending_);
		if (!p)
			return o;
		o.set = pending_;
		const bool stored = p->req.kind == Request::LOAD_STORED && storing_ && storeSeq_ == p->req.seq;
		if (laterSeq(clearSeq, p->req.seq)) {
			// Asked for before a Clear: from here on it is an empty tape, as
			// new as the Clear (so a re-make of it is an empty tape too).
			if (stored) {
				storing_ = false;
				o.storeOff = true;
			}
			p->req = Request();
			p->req.seq = clearSeq;
			p->whole = p->cut = false;
			p->source = nullptr;
			if (live) {
				o.drop = true;
				pending_ = nullptr;
				if (find(live))
					offered_ = live;   // it stands
				return o;
			}
			o.empty = true;
			return o;
		}
		if (stored) {
			o.watch = true;
			o.whole = p->whole;
			o.cut = p->cut;
			o.epoch = p->req.epoch;
		}
		return o;
	}

	// The engine took the offer (offer() said neither drop nor nothing):
	// true if the patch's Play and Dub are to be applied now.
	bool offered() {
		const Entry* p = find(pending_);
		const bool t = p && p->req.transport;
		offered_ = pending_;
		pending_ = nullptr;
		return t;
	}

	// The engine handed `s` back; `used`: it went live.
	HandedBack handedBack(SetId s, bool used) {
		HandedBack h;
		const Entry* e = find(s);
		if (!e)
			return h;
		const Request r = e->req;
		if (used && storing_ && storeSeq_ == r.seq) {
			// It went live: the stored load ended there (the audio thread
			// cleared the flag then; this only agrees).
			storing_ = false;
			h.storeOff = true;
		}
		const bool newest = s == offered_ && !pending_;
		if (s == offered_)
			offered_ = nullptr;
		if (!newest)
			return h;
		h.next = r;
		h.next.fallback = true;
		if (!used && r.kind == Request::CONVERT && e->source) {
			// A conversion refused in its turn: from its own source again.
			h.remake = HandedBack::CONVERT;
			h.from = e->source;
		}
		else if (!used && r.kind != Request::CONVERT) {
			// Never live: made again from its source (a file, the stored
			// files, or nothing), the patch's Play and Dub still to apply.
			h.remake = HandedBack::RERUN;
			rerunning_ = true;
			rerunSeq_ = r.seq;
		}
		else {
			// The user's tape, which exists nowhere else: converted.
			h.remake = HandedBack::CONVERT;
			h.from = s;
			h.next.kind = Request::CONVERT;
			h.next.path.clear();
			h.next.transport = r.transport && !used;
		}
		return h;
	}

	// The last offered set is live now (the module saw it): a conversion's
	// source is no longer needed.
	void wentLive(SetId s) {
		Entry* e = find(s);
		if (e && s == offered_)
			e->source = nullptr;
	}

	// Not to be freed yet: the source of a conversion waiting or offered and
	// not yet live.
	bool needed(SetId s) const {
		if (!s)
			return false;
		const Entry* p = find(pending_);
		const Entry* o = find(offered_);
		return (p && p->source == s) || (o && o->source == s);
	}

	// `s` is about to be freed: true if the stored load ends with it (its set
	// gone, no re-make of it under way, nothing of it waiting or offered).
	bool freed(SetId s) {
		const Entry* e = find(s);
		bool off = false;
		if (e && storing_ && storeSeq_ == e->req.seq && !(rerunning_ && rerunSeq_ == storeSeq_) && !holds(pending_, storeSeq_)
		    && !holds(offered_, storeSeq_)) {
			storing_ = false;
			off = true;
		}
		for (size_t i = 0; i < entries_.size(); i++)
			if (entries_[i].set == s) {
				entries_.erase(entries_.begin() + (long) i);
				break;
			}
		if (pending_ == s)
			pending_ = nullptr;
		if (offered_ == s)
			offered_ = nullptr;
		return off;
	}

	// ---- For the module and the tests.

	SetId pending() const { return pending_; }
	SetId lastOffered() const { return offered_; }
	bool storing() const { return storing_; }
	// The request a set stands for now (null if the book does not know it).
	const Request* requestOf(SetId s) const {
		const Entry* e = find(s);
		return e ? &e->req : nullptr;
	}

private:
	struct Entry {
		SetId set = nullptr;
		Request req;
		bool whole = false, cut = false;
		SetId source = nullptr;   // a conversion not yet live: what it was converted from
	};

	const Entry* find(SetId s) const {
		if (!s)
			return nullptr;
		for (const Entry& e : entries_)
			if (e.set == s)
				return &e;
		return nullptr;
	}
	Entry* find(SetId s) {
		return const_cast<Entry*>(static_cast<const WorkerBook*>(this)->find(s));
	}
	bool holds(SetId s, uint32_t seq) const {
		const Entry* e = find(s);
		return e && e->req.seq == seq;
	}

	std::vector<Entry> entries_;   // every set made and not yet freed, with the request it stands for
	SetId pending_ = nullptr;      // waiting to be offered
	SetId offered_ = nullptr;      // the last offered (waiting in the engine, live, or refused and not yet back)
	bool storing_ = false;         // the stored load is under way
	uint32_t storeSeq_ = 0;        // its request
	bool rerunning_ = false;       // a re-made request is posted and not yet started
	uint32_t rerunSeq_ = 0;
};

} // namespace eddy
} // namespace rka

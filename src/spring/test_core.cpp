// Spring's core (SpringCore.hpp): folds, min/max, triggers, cascade, steps, link.
#include "spring/SpringCore.hpp"
#include "testing.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using rka::SpringCore;
using rka::SpringLink;

const float kFs = 48000.f;
const float kDt = 1.f / kFs;

void zeros(float x[8]) {
	for (int i = 0; i < 8; i++)
		x[i] = 0.f;
}

void fold() {
	SpringCore c;
	float x[8];
	const int idx[4] = {0, 2, 4, 6};
	const int outs[4] = {SpringCore::F1, SpringCore::F3, SpringCore::F5, SpringCore::F7};
	for (int k = 0; k < 4; k++) {
		// A sine of 10 Hz in cycle idx[k]: F rises through 5 V twice per cycle.
		int crossings = 0;
		float prev = 0.f, lo = 99.f, hi = -99.f;
		const int n = 48000; // 1 s = 10 cycles
		for (int s = 0; s < n; s++) {
			zeros(x);
			const float v = 5.f * std::sin(2.f * 3.14159265f * 10.f * s * kDt);
			x[idx[k]] = v;
			c.step(x, kDt);
			const float f = c.out[outs[k]];
			CHECK(std::fabs(f - 2.f * std::fabs(v)) < 1e-5f, "F = 2|x| at %d", s);
			if (s > 0 && prev < 5.f && f >= 5.f)
				crossings++;
			prev = f;
			if (f < lo) lo = f;
			if (f > hi) hi = f;
		}
		CHECK(crossings == 20, "output %d: %d rising crossings of 5 V (want 20 = twice the rate)", k, crossings);
		CHECK(lo >= 0.f && lo < 0.01f && hi <= 10.f && hi > 9.99f, "range %g..%g", lo, hi);
	}
	zeros(x);
	x[0] = -5.f;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::F1] == 10.f, "F(-5) = 10");
	x[0] = std::nanf("");
	c.step(x, kDt);
	CHECK(c.out[SpringCore::F1] == 0.f, "NaN gives 0");
}

void min_max() {
	SpringCore c;
	float x[8];
	const float vals[] = {-5.f, -2.5f, -0.001f, 0.f, 0.3f, 1.7f, 5.f, 4.999f};
	const int nv = 8;
	for (int i = 0; i < nv; i++)
		for (int j = 0; j < nv; j++) {
			zeros(x);
			x[1] = vals[i];
			x[2] = vals[j];
			x[5] = vals[j];
			x[6] = vals[i];
			c.step(x, kDt);
			const float lo = vals[i] < vals[j] ? vals[i] : vals[j];
			const float hi = vals[i] < vals[j] ? vals[j] : vals[i];
			CHECK(c.out[SpringCore::MIN23] == lo && c.out[SpringCore::MAX23] == hi, "23: %g %g", vals[i], vals[j]);
			CHECK(c.out[SpringCore::MIN67] == lo && c.out[SpringCore::MAX67] == hi, "67: %g %g", vals[i], vals[j]);
		}
	const float nan = std::nanf("");
	zeros(x);
	x[1] = nan;
	x[2] = -3.f;
	x[5] = 2.f;
	x[6] = nan;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::MIN23] == -3.f && c.out[SpringCore::MAX23] == -3.f, "NaN uses the other (23)");
	CHECK(c.out[SpringCore::MIN67] == 2.f && c.out[SpringCore::MAX67] == 2.f, "NaN uses the other (67)");
	x[1] = nan;
	x[2] = nan;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::MIN23] == 0.f && c.out[SpringCore::MAX23] == 0.f, "both NaN gives 0");
}

// Run cycle 2 through a script; returns T2's output per sample with only T2 patched.
std::vector<float> runT2(const std::vector<float>& v2) {
	SpringCore c;
	c.setPatched(0, true);
	std::vector<float> o;
	float x[8];
	for (size_t s = 0; s < v2.size(); s++) {
		zeros(x);
		x[1] = v2[s];
		c.step(x, kDt);
		o.push_back(c.out[SpringCore::T2]);
	}
	return o;
}

void trigger_once_per_rise() {
	// Falling edge first: nothing. Rising at 1000: one pulse. Falling at 5000: none.
	std::vector<float> v(20000, -1.f);
	for (int s = 0; s < 1000; s++) v[s] = 1.f; // starts high, first step records only
	for (int s = 1000; s < 5000; s++) v[s] = -1.f;
	for (int s = 5000; s < 12000; s++) v[s] = 1.f;
	std::vector<float> o = runT2(v);
	int pulses = 0, hiCount = 0;
	long first = -1;
	for (size_t s = 0; s < o.size(); s++) {
		CHECK(o[s] == 0.f || o[s] == 10.f, "value %g", o[s]);
		if (o[s] == 10.f) {
			hiCount++;
			if (first < 0) first = long(s);
		}
		if (o[s] == 10.f && (s == 0 || o[s - 1] == 0.f))
			pulses++;
	}
	CHECK(pulses == 1, "%d pulses", pulses);
	CHECK(first == 5000, "pulse starts on the crossing sample: %ld", first);
	CHECK(std::abs(hiCount - 480) <= 1, "pulse is %d samples (want 480 +-1)", hiCount);

	// Crossing exactly at 0: previous < 0, current >= 0 fires; staying at 0 does not.
	std::vector<float> w(2000, -1.f);
	for (int s = 100; s < 2000; s++) w[s] = 0.f;
	o = runT2(w);
	int hi = 0;
	for (size_t s = 0; s < o.size(); s++) hi += o[s] == 10.f;
	CHECK(std::abs(hi - 480) <= 1 && o[100] == 10.f, "-1 to 0 fires once (%d)", hi);

	// Restart while high: crossing at 5000 and again at 5200 (after a dip) gives one pulse of 200 + 480.
	std::vector<float> r(20000, -1.f);
	for (int s = 5000; s < 5100; s++) r[s] = 1.f;
	for (int s = 5200; s < 5300; s++) r[s] = 1.f;
	o = runT2(r);
	int n = 0, pulses2 = 0;
	for (size_t s = 0; s < o.size(); s++) {
		n += o[s] == 10.f;
		if (o[s] == 10.f && (s == 0 || o[s - 1] == 0.f)) pulses2++;
	}
	CHECK(pulses2 == 1, "merged: %d pulses", pulses2);
	CHECK(std::abs(n - 680) <= 2, "merged length %d (want 680 +-2)", n);
	CHECK(o[5199] == 10.f && o[5679] == 10.f && o[5690] == 0.f, "restart extends to 5200+480");

	// NaN: records nothing, fires nothing; next finite sample is not a crossing.
	std::vector<float> q(3000, -1.f);
	q[500] = std::nanf("");
	for (int s = 501; s < 3000; s++) q[s] = 1.f;
	o = runT2(q);
	int any = 0;
	for (size_t s = 0; s < o.size(); s++) any += o[s] != 0.f;
	CHECK(any == 0, "NaN then high fires nothing (%d)", any);
}

void reset_no_burst() {
	SpringCore c;
	c.setPatched(0, true);
	float x[8];
	zeros(x);
	x[1] = -1.f;
	c.step(x, kDt);
	x[1] = 1.f;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::T2] == 10.f, "fires before reset");
	c.reset();
	CHECK(c.out[SpringCore::T2] == 0.f, "reset clears pulse");
	c.step(x, kDt); // first step after reset: record only
	CHECK(c.out[SpringCore::T2] == 0.f, "no burst on re-link");
	c.step(x, kDt);
	CHECK(c.out[SpringCore::T2] == 0.f, "still none");
	// A negative value just before the reset must not make the first value after it a crossing.
	x[1] = -1.f;
	c.step(x, kDt);
	c.reset();
	x[1] = 1.f;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::T2] == 0.f, "no crossing against a value from before reset");
	c.step(x, kDt);
	CHECK(c.out[SpringCore::T2] == 0.f, "none after");
}

void cascade_all_masks() {
	const int n = 24000;
	const float freq[4] = {7.f, 11.f, 17.f, 23.f};
	std::vector<float> xs[4];
	for (int k = 0; k < 4; k++)
		for (int s = 0; s < n; s++)
			xs[k].push_back(5.f * std::sin(2.f * 3.14159265f * freq[k] * s * kDt + 0.4f * k));

	auto run = [&](int mask, std::vector<float> o[4]) {
		SpringCore c;
		for (int k = 0; k < 4; k++) {
			c.setPatched(k, (mask >> k) & 1);
			o[k].clear();
		}
		float x[8];
		for (int s = 0; s < n; s++) {
			zeros(x);
			for (int k = 0; k < 4; k++) x[2 * k + 1] = xs[k][s];
			c.step(x, kDt);
			for (int k = 0; k < 4; k++) o[k].push_back(c.out[SpringCore::T2 + k]);
		}
	};
	std::vector<float> own[4];
	run(15, own); // all patched: each output is its own source
	// Check that against the crossings of x itself: a run of 480 +-1 starting at each one
	// (the periods are longer than a pulse, so none merge).
	for (int k = 0; k < 4; k++) {
		std::vector<int> starts, want;
		for (int s = 1; s < n; s++)
			if (xs[k][s - 1] < 0.f && xs[k][s] >= 0.f) want.push_back(s);
		int run = 0;
		bool lenOk = true;
		for (int s = 0; s <= n; s++) {
			const bool h = s < n && own[k][s] == 10.f;
			if (h && run == 0) starts.push_back(s);
			if (h) run++;
			if (!h && run) {
				lenOk = lenOk && std::abs(run - 480) <= 1;
				run = 0;
			}
		}
		CHECK(!want.empty() && starts == want, "source %d pulse starts match the crossings", k);
		CHECK(lenOk, "source %d pulse lengths", k);
	}
	for (int mask = 0; mask < 16; mask++) {
		std::vector<float> o[4];
		run(mask, o);
		for (int k = 0; k < 4; k++) {
			for (int s = 0; s < n; s++) {
				float want = 0.f;
				if ((mask >> k) & 1) {
					want = own[k][s];
					for (int j = (k + 3) & 3; j != k && !((mask >> j) & 1); j = (j + 3) & 3)
						if (own[j][s] == 10.f) want = 10.f;
				}
				if (o[k][s] != want) {
					CHECK(false, "mask %d out %d sample %d: %g want %g", mask, k, s, o[k][s], want);
					break;
				}
			}
		}
	}
	// Spot check: only T6 patched takes all four.
	std::vector<float> o[4];
	run(4, o);
	bool all = true;
	for (int s = 0; s < n; s++) {
		bool any = false;
		for (int k = 0; k < 4; k++) any = any || own[k][s] == 10.f;
		if ((o[2][s] == 10.f) != any) all = false;
	}
	CHECK(all, "single patched output carries all four");
}

void steps_codes_and_order() {
	SpringCore c;
	float x[8];
	for (int m = 0; m < 256; m++) {
		for (int i = 0; i < 8; i++) x[i] = ((m >> i) & 1) ? 0.001f : -0.001f; // bit i = cycle i+1
		c.step(x, kDt);
		auto b = [&](int cyc) { return (m >> (cyc - 1)) & 1; };
		const int want[4] = {8 * b(1) + 4 * b(2) + 2 * b(3) + b(4),
		                     8 * b(5) + 4 * b(6) + 2 * b(7) + b(8),
		                     8 * b(1) + 4 * b(3) + 2 * b(5) + b(7),
		                     8 * b(2) + 4 * b(4) + 2 * b(6) + b(8)};
		const int outs[4] = {SpringCore::S14, SpringCore::S58, SpringCore::SODD, SpringCore::SEVEN};
		for (int k = 0; k < 4; k++) {
			const float v = c.out[outs[k]];
			CHECK(std::fabs(v - want[k] * 10.f / 15.f) < 1e-5f, "mask %d out %d: %g want code %d", m, k, v, want[k]);
		}
	}
	for (int i = 0; i < 8; i++) x[i] = 5.f;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::S14] == 10.f && c.out[SpringCore::SEVEN] == 10.f, "code 15 = 10 V");
	for (int i = 0; i < 8; i++) x[i] = -5.f;
	c.step(x, kDt);
	CHECK(c.out[SpringCore::S58] == 0.f && c.out[SpringCore::SODD] == 0.f, "code 0 = 0 V");
	for (int i = 0; i < 8; i++) x[i] = 0.f; // exactly 0 is bit 0
	c.step(x, kDt);
	CHECK(c.out[SpringCore::S14] == 0.f, "x = 0 reads 0");
	for (int i = 0; i < 8; i++) x[i] = std::nanf("");
	c.step(x, kDt);
	CHECK(c.out[SpringCore::S14] == 0.f && c.out[SpringCore::SODD] == 0.f, "NaN reads 0");
}

void sample_time() {
	// A bad sampleTime keeps the last good one: 96 kHz pulse is 960 samples, NaN/0 keep it.
	SpringCore c;
	c.setPatched(0, true);
	float x[8];
	zeros(x);
	const float dt = 1.f / 96000.f;
	x[1] = -1.f;
	c.step(x, dt);
	x[1] = 1.f;
	int hi = 0;
	for (int s = 0; s < 2000; s++) {
		c.step(x, s % 2 ? std::nanf("") : (s % 3 ? 0.f : dt));
		hi += c.out[SpringCore::T2] == 10.f;
	}
	CHECK(std::abs(hi - 960) <= 1, "pulse %d samples at 96 kHz (bad dt ignored)", hi);
}

void link_timeout() {
	SpringLink l;
	CHECK(!l.linkedAfterProcess(), "starts unlinked");
	CHECK(!l.linkedAfterProcess(), "still unlinked with no call");
	l.neapCalled();
	CHECK(l.linkedAfterProcess(), "linked on first change");
	for (int i = 1; i < 64; i++)
		CHECK(l.linkedAfterProcess(), "still linked at idle call %d", i);
	CHECK(!l.linkedAfterProcess(), "unlinked on the 64th call with no change");
	CHECK(!l.linkedAfterProcess(), "stays unlinked");
	l.neapCalled();
	CHECK(l.linkedAfterProcess(), "linked again on the next call");
	// A steady caller keeps it linked.
	for (int i = 0; i < 1000; i++) {
		l.neapCalled();
		l.neapCalled();
		CHECK(l.linkedAfterProcess(), "steady");
	}
	// Idle count restarts after a change.
	for (int i = 0; i < 40; i++) l.linkedAfterProcess();
	l.neapCalled();
	for (int i = 0; i < 65; i++) { // call 0 sees the change; 64 calls without change follow
		const bool lk = l.linkedAfterProcess();
		CHECK(lk == (i < 64), "restarted count at %d", i);
	}
	// Contract: on unlink the module resets the core; no burst on re-link.
	SpringCore c;
	c.setPatched(0, true);
	float x[8];
	zeros(x);
	x[1] = -1.f;
	c.step(x, kDt);
	x[1] = 1.f;
	c.step(x, kDt);
	c.reset();
	for (int i = 0; i < SpringCore::N_OUT; i++)
		CHECK(c.out[i] == 0.f, "reset zeroes output %d", i);
}

} // namespace

int main() {
	fold();
	min_max();
	trigger_once_per_rise();
	reset_no_burst();
	cascade_all_masks();
	steps_codes_and_order();
	sample_time();
	link_timeout();
	return rkatest::finish("spring/test_core");
}

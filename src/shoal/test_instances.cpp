// Many shoal::Core instances in one process: independent state, identical
// results for identical inputs, safe use from several threads, and clean
// creation and destruction in any order.
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "testing.hpp"
#include "test_util.hpp"
#include "CoreTest.hpp"

namespace {

constexpr int kNumOuts = shoal::kNumChannels;

// Feed core c the next step of a random input stream: every 64 ticks new
// CV levels; gates toggle at random.
void randomInputs(shoal::Core& c, Lcg& rng, long tick) {
	if (tick % 64 == 0)
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			c.setCv(ch, rng.volts(-3.f, 6.f));
	if (tick % 16 == 0)
		for (int ch = 0; ch < shoal::kNumChannels; ch++)
			c.setGate(ch, (rng.next() >> 28) < 6 ? 10.f : 0.f);
}

// Run c for `seconds` on the random inputs from seed; return the final codes.
// onTicks(i), if given, runs every 256 ticks, after tick i.
std::vector<uint32_t> drive(shoal::Core& c, uint32_t seed, double seconds,
		const std::function<void(long)>& onTicks = nullptr) {
	Lcg rng;
	rng.state = seed;
	const long n = std::lround(seconds * shoal::kTickHz);
	for (long i = 0; i < n; i++) {
		randomInputs(c, rng, i);
		c.tick();
		if (onTicks && i % 256 == 255)
			onTicks(i);
	}
	std::vector<uint32_t> codes;
	for (int ch = 0; ch < kNumOuts; ch++)
		codes.push_back(c.outCode(ch));
	return codes;
}

void testIndependent() {
	shoal::Core a, b;
	a.seed(1);
	b.seed(1);
	a.setCv(0, 1.0f);
	b.setCv(0, 3.0f);
	for (int i = 0; i < 8; i++) {
		run(a, 0.01);
		run(b, 0.01);
		pulse(a, 0);
		pulse(b, 0);
	}
	CHECK(std::fabs(a.out(0) - 1.0f) < 0.01f, "a sampled its own CV: %f", a.out(0));
	CHECK(std::fabs(b.out(0) - 3.0f) < 0.01f, "b sampled its own CV: %f", b.out(0));
	b.selectApp(5);
	CHECK(a.app() == 0 && b.app() == 5, "apps are per instance");
}

void testDeterministic() {
	for (int app = 0; app < shoal::kNumApps; app++) {
		shoal::Core a, b;
		a.seed(7);
		b.seed(7);
		a.selectApp(app);
		b.selectApp(app);
		Lcg ra, rb;
		const long n = std::lround(2.0 * shoal::kTickHz);
		long mismatch = -1;
		for (long i = 0; i < n && mismatch < 0; i++) {
			randomInputs(a, ra, i);
			randomInputs(b, rb, i);
			a.tick();
			b.tick();
			for (int ch = 0; ch < kNumOuts; ch++)
				if (a.outCode(ch) != b.outCode(ch))
					mismatch = i;
		}
		CHECK(mismatch < 0, "app %d (%s): outputs differ at tick %ld", app,
			shoal::Core::appName(app), mismatch);
	}
}

void testThreads() {
	const int apps[2] = { 10, 5 };
	const uint32_t seeds[2] = { 101, 202 };
	std::vector<uint32_t> threaded[2];
	// Start barrier: both cores are built and in their app before either
	// thread ticks. Each thread records whether, in the middle of its run, it
	// saw the other one also mid-run (started ticking, not finished).
	std::atomic<int> ready{ 0 };
	std::atomic<bool> ticking[2] = { { false }, { false } };
	std::atomic<bool> finished[2] = { { false }, { false } };
	bool sawOther[2] = { false, false };
	std::thread t[2];
	for (int k = 0; k < 2; k++)
		t[k] = std::thread([&, k] {
			shoal::Core c;
			c.seed(seeds[k]);
			c.selectApp(apps[k]);
			ready++;
			while (ready.load() < 2) {
			}
			ticking[k] = true;
			threaded[k] = drive(c, seeds[k], 5.0, [&, k](long) {
				if (ticking[1 - k] && !finished[1 - k])
					sawOther[k] = true;
			});
			finished[k] = true;
		});
	for (auto& th : t)
		th.join();
	CHECK(sawOther[0] || sawOther[1], "the two threads ticked at the same time");
	for (int k = 0; k < 2; k++) {
		shoal::Core c;
		c.seed(seeds[k]);
		c.selectApp(apps[k]);
		std::vector<uint32_t> sequential = drive(c, seeds[k], 5.0);
		for (int ch = 0; ch < kNumOuts; ch++)
			CHECK(threaded[k][ch] == sequential[ch], "app %d out %d: threaded %u, sequential %u",
				apps[k], ch, threaded[k][ch], sequential[ch]);
	}
}

void testLifetimes() {
	std::vector<std::unique_ptr<shoal::Core>> v;
	for (int i = 0; i < 64; i++)
		v.emplace_back(new shoal::Core);
	for (auto& c : v)
		run(*c, 0.01);
	for (size_t i = 0; i < v.size(); i += 2)
		v[i].reset();
	for (auto& c : v)
		if (c)
			run(*c, 0.01);
	for (int i = 0; i < 32; i++)
		v.emplace_back(new shoal::Core);
	for (auto& c : v)
		if (c)
			run(*c, 0.01);
	v.clear();
	CHECK(shoal_test_current_is_null(), "no current context after destroying all");

	shoal::Core x;
	std::atomic<bool> threadNull{ false };
	std::thread t([&] {
		run(x, 2.0);
		threadNull = shoal_test_current_is_null();
	});
	for (int i = 0; i < 200; i++) {
		shoal::Core c;
		c.tick();
	}
	t.join();
	CHECK(allFinite(x), "x outputs finite");
	CHECK(threadNull, "no dangling current pointer on thread T");
	CHECK(shoal_test_current_is_null(), "no dangling current pointer on this thread");
}

// millis() must not jump back when the firmware's 32-bit tick counter wraps
// (after about 71.6 hours); it wraps only at 2^32 ms.
void testMillisWrap() {
	shoal::Core c;
	const uint64_t start = (uint64_t(1) << 32) - 10;
	shoal::CoreTest::setTicks(c, start);
	uint32_t last = shoal::CoreTest::millis(c);
	CHECK(last == (uint32_t) (start * 60 / 1000), "millis at start: %u", last);
	bool monotonic = true;
	for (int i = 0; i < 40; i++) {
		c.tick();
		uint32_t now = shoal::CoreTest::millis(c);
		if (now < last)
			monotonic = false;
		last = now;
	}
	CHECK(monotonic, "millis never goes backwards across the 32-bit tick wrap");
	CHECK(last == (uint32_t) ((start + 40) * 60 / 1000), "millis after the wrap: %u", last);
}

} // namespace

int main() {
	testIndependent();
	testDeterministic();
	testThreads();
	testLifetimes();
	testMillisWrap();
	return rkatest::finish("test_instances");
}

// Offline tests of the Shoal shim primitives: no firmware, no Rack.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <vector>

#include "testing.hpp"

#include "Arduino.h"
#include "EEPROM.h"
#include "Pins.hpp"
#include "Volts.hpp"
#include "dspinst.h"

static void testVolts() {
	CHECK(std::fabs(shoal::dacToVolts(24551) - 0.f) < 1e-4f, "0 V");
	CHECK(std::fabs(shoal::dacToVolts(0) - (-3.7462f)) < 1e-3f, "bottom");
	CHECK(std::fabs(shoal::dacToVolts(65535) - 6.2537f) < 1e-3f, "top");
	CHECK(shoal::voltsToAdc12(0.f, 2730) == 2730, "hw zero");
	CHECK(shoal::voltsToAdc12(1.f, 2730) == 2320, "1 V = 409.6 codes, rounded");
	CHECK(shoal::voltsToAdc12(100.f, 2730) == 0 && shoal::voltsToAdc12(-100.f, 2730) == 4095, "clamp");
	CHECK(shoal::voltsToAdc12(INFINITY, 2730) == 0 && shoal::voltsToAdc12(-INFINITY, 2730) == 4095, "inf clamps");
	CHECK(shoal::voltsToAdc12(std::nanf(""), 2730) == 2730, "NaN is 0 V");
	CHECK(shoal::voltsToAdc12(5.f, 2048) == 0 && shoal::voltsToAdc12(-4.99f, 2048) > 4090, "\xc2\xb1" "5 V window");
}

static void testSchmitt() {
	shoal::Schmitt s;
	CHECK(!s.high, "starts low");
	CHECK(!s.process(0.f) && !s.high, "0 stays low");
	CHECK(!s.process(0.5f) && !s.high, "0.5 stays low");
	CHECK(s.process(1.01f) && s.high, "1.01 goes high (rising edge reported)");
	CHECK(!s.process(0.5f) && s.high, "0.5 stays high, no new edge");
	CHECK(!s.process(0.09f) && !s.high, "0.09 goes low");
	CHECK(!s.process(std::nanf("")) && !s.high, "NaN changes nothing");
}

static void testTickClock() {
	const double rates[] = {8000, 44100, 48000, 96000, 768000};
	for (double rate : rates) {
		shoal::TickClock c;
		c.setSampleRate(rate);
		long total = 0;
		long samples = (long) (rate * 10);
		for (long i = 0; i < samples; i++) {
			int n = c.advance();
			CHECK(n >= 0, "non-negative");
			total += n;
		}
		CHECK(total == 166666 || total == 166667, "rate %g: %ld ticks in 10 s", rate, total);
	}
}

// The firmware's Encoder::Poll/Read rule (UI/ui_encoder.h), reproduced.
struct FwEncoder {
	uint8_t a = 0xff, b = 0xff;
	int read(shoal::EncoderPins& p) {
		bool pa, pb;
		p.poll(pa, pb);
		a = (uint8_t) ((a << 1) | (pa ? 1 : 0));
		b = (uint8_t) ((b << 1) | (pb ? 1 : 0));
		if ((a & 3) == 2 && (b & 3) == 0) return 1;
		if ((b & 3) == 2 && (a & 3) == 0) return -1;
		return 0;
	}
};

static void testEncoderPins() {
	{
		shoal::EncoderPins p;
		FwEncoder e;
		p.turn(3);
		int sum = 0, events = 0;
		for (int i = 0; i < 6; i++) {
			int r = e.read(p);
			sum += r;
			events += r != 0;
		}
		CHECK(sum == 3 && events == 3, "turn(+3): sum %d events %d", sum, events);
		for (int i = 0; i < 10; i++)
			CHECK(e.read(p) == 0, "idle after turn");
	}
	{
		shoal::EncoderPins p;
		FwEncoder e;
		p.turn(-2);
		int sum = 0, events = 0;
		for (int i = 0; i < 4; i++) {
			int r = e.read(p);
			sum += r;
			events += r != 0;
		}
		CHECK(sum == -2 && events == 2, "turn(-2): sum %d events %d", sum, events);
	}
	{
		// Direction changes back to back keep their order.
		shoal::EncoderPins p;
		FwEncoder e;
		p.turn(2);
		p.turn(-1);
		int seq[8], n = 0;
		for (int i = 0; i < 6; i++) {
			int r = e.read(p);
			if (r) seq[n++] = r;
		}
		CHECK(n == 3 && seq[0] == 1 && seq[1] == 1 && seq[2] == -1, "+2 then -1 in order");
	}
}

static void testDspinst() {
	std::mt19937 rng(12345);
	std::vector<int32_t> vals = {0, 1, -1, INT32_MIN, INT32_MAX, 0x7fff, -0x8000, 0x8000, 0x10000};
	for (int i = 0; i < 1000; i++) vals.push_back((int32_t) rng());
	for (int32_t a : vals) {
		for (int32_t bs : vals) {
			uint32_t b = (uint32_t) bs;
			CHECK(signed_multiply_32x16b(a, b) == (int32_t) (((int64_t) a * (int16_t) (b & 0xffff)) >> 16), "32x16b");
			CHECK(multiply_u32xu32_rshift32((uint32_t) a, b) == (uint32_t) (((uint64_t) (uint32_t) a * b) >> 32), "u32xu32");
		}
		for (int bits : {8, 16, 24}) {
			for (int sh : {0, 1, 8}) {
				int64_t v = (int64_t) (a >> sh);
				int64_t hi = ((int64_t) 1 << (bits - 1)) - 1, lo = -((int64_t) 1 << (bits - 1));
				int64_t want = v > hi ? hi : (v < lo ? lo : v);
				CHECK(signed_saturate_rshift(a, bits, sh) == (int32_t) want, "ssat a=%d bits=%d sh=%d", a, bits, sh);
			}
		}
	}
	// Spot values, written by hand.
	CHECK(signed_multiply_32x16b(65536, 0x4000) == 16384, "65536 * 16384 >> 16");
	CHECK(signed_multiply_32x16b(65536, 0x8000) == -32768, "low half is signed");
	CHECK(multiply_u32xu32_rshift32(0xffffffffu, 0xffffffffu) == 0xfffffffeu, "max*max");
	CHECK(signed_saturate_rshift(100000, 16, 0) == 32767 && signed_saturate_rshift(-100000, 16, 0) == -32768, "ssat 16");
}

int main() {
	testVolts();
	testSchmitt();
	testTickClock();
	testEncoderPins();
	testDspinst();
	return rkatest::finish("test_shim");
}

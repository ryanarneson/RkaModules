// The patch decode rules (PatchState.hpp): which "version", decoded "state"
// length and "inputRange" values a saved patch may hold.
#include <climits>
#include <cstddef>
#include <initializer_list>

#include "PatchState.hpp"
#include "testing.hpp"

using shoal::InputRange;

namespace {

void testVersion() {
	CHECK(shoal::kPatchVersion == 1, "the patch format is version %d", shoal::kPatchVersion);
	CHECK(shoal::patchVersionOk(true, 1), "the integer 1");
	CHECK(!shoal::patchVersionOk(false, 1), "1 that is not an integer (a real, a string)");
	const long long others[] = { 0, 2, -1, 257, LLONG_MIN, LLONG_MAX };
	for (long long v : others)
		CHECK(!shoal::patchVersionOk(true, v), "version %lld", v);
	CHECK(!shoal::patchVersionOk(false, 0), "missing");
}

void testStateSize() {
	CHECK(shoal::patchStateSizeOk(shoal::kStateBytes), "exactly kStateBytes");
	CHECK(shoal::patchStateSizeOk(2048), "kStateBytes is 2048");
	const size_t others[] = { 0, 1, 1024, 2047, 2049, 4096 };
	for (size_t n : others)
		CHECK(!shoal::patchStateSizeOk(n), "%zu bytes", n);
}

void testInputRange() {
	CHECK(shoal::patchInputRange(true, 1) == InputRange::Bipolar5, "the integer 1 is ±5 V");
	CHECK(shoal::patchInputRange(true, 0) == InputRange::Hardware, "0 is Hardware");
	CHECK(shoal::patchInputRange(false, 1) == InputRange::Hardware, "1 that is not an integer");
	CHECK(shoal::patchInputRange(false, 0) == InputRange::Hardware, "missing");
	const long long others[] = { 2, -1, 257, LLONG_MAX };
	for (long long v : others)
		CHECK(shoal::patchInputRange(true, v) == InputRange::Hardware, "inputRange %lld", v);
	CHECK(shoal::patchInputRangeValue(InputRange::Hardware) == 0, "Hardware saves 0");
	CHECK(shoal::patchInputRangeValue(InputRange::Bipolar5) == 1, "±5 V saves 1");
	for (InputRange r : { InputRange::Hardware, InputRange::Bipolar5 })
		CHECK(shoal::patchInputRange(true, shoal::patchInputRangeValue(r)) == r, "range round trip");
}

} // namespace

int main() {
	testVersion();
	testStateSize();
	testInputRange();
	return rkatest::finish("test_patch");
}

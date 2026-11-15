#pragma once
// The rules for Shoal's state in a saved patch that do not need Rack. The
// module (Shoal.cpp) reads the JSON and the base64; these decide what the
// fields it found mean. Header-only, Rack-free, C++11.
//
// The patch data, version 1 (the keys and their meaning never change):
//   { "version": 1, "state": "<base64 of kStateBytes bytes>", "inputRange": 0 }
// state is the firmware's EEPROM image; inputRange is 0 (Hardware) or 1 (±5 V).
#include <cstddef>

#include "Core.hpp"

namespace shoal {

constexpr int kPatchVersion = 1;

// The patch's state is decoded only when "version" is the integer 1 ...
inline bool patchVersionOk(bool isInteger, long long value) {
	return isInteger && value == kPatchVersion;
}
// ... and "state" decodes to exactly kStateBytes bytes. Anything else (a field
// missing, another version, bad base64, the wrong length): the caller
// restores a new core's defaults.
inline bool patchStateSizeOk(size_t decodedBytes) {
	return decodedBytes == (size_t) kStateBytes;
}
// "inputRange" is ±5 V only for the integer 1; anything else, or none, is Hardware.
inline InputRange patchInputRange(bool isInteger, long long value) {
	return isInteger && value == 1 ? InputRange::Bipolar5 : InputRange::Hardware;
}
inline int patchInputRangeValue(InputRange r) {
	return r == InputRange::Bipolar5 ? 1 : 0;
}

} // namespace shoal

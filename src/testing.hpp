#pragma once
// Minimal check harness shared by the offline unit tests (src/*/test_*.cpp).
// C++17; never included by plugin code.
#include <cstdio>

namespace rkatest {
inline int failures = 0;

// Print the summary line and return the process exit code.
inline int finish(const char* name) {
	if (failures) {
		std::printf("%s: %d check(s) failed\n", name, failures);
		return 1;
	}
	std::printf("%s: all checks passed\n", name);
	return 0;
}
}

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		rkatest::failures++; \
		std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
		std::printf(__VA_ARGS__); \
		std::printf("\n"); \
	} \
} while (0)

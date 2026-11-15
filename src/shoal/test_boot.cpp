// Boots the firmware headless through shoal::Core and runs it for a second.
#include <string>

#include "testing.hpp"
#include "test_util.hpp"

static void testBoot() {
	shoal::Core c;
	CHECK(c.app() == 0, "boots into CopierMaschine");
	CHECK(std::string(shoal::Core::appName(0)) == "CopierMaschine", "name");
	CHECK(std::string(shoal::Core::appName(12)) == "References", "last name");
	run(c, 1.0);                                   // 16667 ticks, inputs at 0 V
	CHECK(allFinite(c), "outputs finite");
	for (int ch = 0; ch < 4; ch++)
		CHECK(c.out(ch) >= -3.75f && c.out(ch) <= 6.26f, "out %d in hardware range", ch);
}

int main() {
	testBoot();
	return rkatest::finish("test_boot");
}

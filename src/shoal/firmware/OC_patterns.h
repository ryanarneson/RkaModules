#ifndef OC_PATTERNS_H_
#define OC_PATTERNS_H_

#include "OC_patterns_presets.h"


namespace OC {

typedef OC::Pattern Pattern;

static constexpr int kMaxPatternLength = 16;
static constexpr int kMinPatternLength = 2;

class Patterns {
public:

  static const int NUM_PATTERNS;

  enum {
    PATTERN_USER_0_1,
    PATTERN_USER_1_1,
    PATTERN_USER_2_1,
    PATTERN_USER_3_1,
    PATTERN_USER_LAST,
    PATTERN_USER_0_2,
    PATTERN_USER_1_2,
    PATTERN_USER_2_2,
    PATTERN_USER_3_2, 
    PATTERN_USER_ALL = PATTERN_USER_3_2,
    PATTERN_NONE = PATTERN_USER_LAST
  };

  static void Init();
  static const int kMin = kMinPatternLength;
  static const int kMax = kMaxPatternLength;
};


extern const char *const pattern_names[];
extern const char *const pattern_names_short[];
SHOAL_STATE(Pattern[OC::Patterns::PATTERN_USER_ALL], g_user_patterns) // SHOAL: per-instance state (shoal::Context)
#define g_user_patterns g_user_patterns_ref()
SHOAL_STATE(Pattern, dummy_pattern) // SHOAL: per-instance state (shoal::Context)
#define dummy_pattern dummy_pattern_ref()

};

#endif // OC_PATTERNS_H_

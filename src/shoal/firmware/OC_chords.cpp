#include "OC_chords.h"
#include "OC_chords_presets.h"

namespace OC {

    // SHOAL: g_user_chords lives in shoal::Context (g_ prefix: the name clashes with a GlobalSettings member).

    /*static*/
    const int Chords::NUM_CHORD_PROGRESSIONS = 0x4;
    const int Chords::NUM_CHORDS_TOTAL = OC::Chords::CHORDS_USER_LAST; // = 8
    const int Chords::NUM_CHORDS_PROPERTIES = sizeof(Chord);
    const int Chords::NUM_CHORDS = Chords::NUM_CHORDS_TOTAL / Chords::NUM_CHORD_PROGRESSIONS;

    /*static*/
    // 
    void Chords::Init() {
      for (size_t i = 0; i < OC::Chords::CHORDS_USER_LAST; ++i)
        memcpy(&g_user_chords[i], &OC::chords[0], sizeof(Chord)); // SHOAL: g_ prefix (name clashes with GlobalSettings members)
    }

    const Chord &Chords::GetChord(int index, int progression) {

       uint8_t _index = index + progression * Chords::NUM_CHORDS;
       if (_index < CHORDS_USER_LAST) 
        return g_user_chords[_index]; // SHOAL: g_ prefix (name clashes with GlobalSettings members)
       else
        return g_user_chords[0x0]; // SHOAL: g_ prefix (name clashes with GlobalSettings members)
    }
} // namespace OC

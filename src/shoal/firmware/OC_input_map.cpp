// Copyright 2015 Olivier Gillet, 2016 mxmxmx
//
// Author: Olivier Gillet (ol.gillet@gmail.com)
// adapted for OC, mxmxmx
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// input mapping, based on braids quantizer

#include "OC_input_map.h"
#include "Arduino.h"
#include <algorithm>
#include <cstdlib>

namespace OC {


void Input_Map::Init() {
  enabled_ = true;
  update_ = false;
  code_index_ = 0;
  previous_boundary_ = 0;
  next_boundary_ = 0;
  num_slots_ = 15;
  for (int16_t i = 0; i < 16; ++i) {
    codebook_[i] = i << 7;
  }
}

void Input_Map::Configure(const int8_t num_slots, const int16_t* ranges, uint8_t range) {

  enabled_ = ranges != NULL;

  if (enabled_) {
  
    int16_t delta = ranges[range];

    for (int32_t i = 0; i < 16; ++i) {
      codebook_[i] = delta*i;
    }
    update_ = true;
    num_slots_ = num_slots;
  }
}

int8_t Input_Map::Process(int32_t input) {

  int8_t _slot = 0;

  if (!enabled_) {
    return _slot;
  }

  if (!update_ && input >= previous_boundary_ && input <= next_boundary_) {
    // We're still in the voronoi cell for the active codeword.
    _slot = code_index_;
  } else {
    // Search for the nearest neighbour in the codebook.
    int16_t upper_bound_index = std::upper_bound(
        &codebook_[2],
        &codebook_[15],
        static_cast<int16_t>(input)) - &codebook_[0];
    int16_t lower_bound_index = upper_bound_index - 2;

    int16_t best_distance = 512;
    int16_t q = -1;
    for (int16_t i = lower_bound_index; i <= upper_bound_index; ++i) {
      int16_t distance = abs(input - codebook_[i]);
      if (distance < best_distance) {
        best_distance = distance;
        q = i;
      }
    }

    // Enlarge the current voronoi cell a bit for hysteresis.
    // SHOAL: q is -1 when the input is 512 or more from every code word, and
    // q - 1 and q + 1 reach index -2, -1 and 16 of codebook_, outside the
    // array. Upstream read the neighbouring members. The layout is the same on
    // the Teensy and here (see the class: bool enabled_, bool update_, int16_t
    // codebook_[16], int8_t code_index_, int8_t num_slots_, no padding before
    // codebook_ since int16_t aligns to 2, little-endian both), so codebook_[16]
    // is the int16 of bytes code_index_ (low) and num_slots_ (high), and
    // codebook_[-1] is enabled_ (low) and update_ (high). Both are read here as
    // they were; only codebook_[-2] (the 2 bytes before the object) was
    // whatever happened to precede it, and is 0 here.
    auto word = [this](int i) -> int32_t {
      if (i >= 0 && i < 16) return codebook_[i];
      if (i == 16)
        return static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint8_t>(code_index_) | (static_cast<uint8_t>(num_slots_) << 8)));
      if (i == -1)
        return static_cast<int16_t>(static_cast<uint16_t>((enabled_ ? 1 : 0) | ((update_ ? 1 : 0) << 8)));
      return 0;
    };
    previous_boundary_ = (9 * word(q - 1) + 7 * word(q)) >> 4;
    next_boundary_ = (9 * word(q + 1) + 7 * word(q)) >> 4;
    if (q < 0) q = 0;
    else if (q > num_slots_) q = num_slots_;
    code_index_ = _slot = q;
    update_ = false;
  }
  return _slot;
}


}  // namespace OC

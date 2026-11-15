// Copyright (c) 2016 Patrick Dowling
//
// Author: Patrick Dowling (pld@gurkenkiste.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef DRIVERS_DISPLAY_H_
#define DRIVERS_DISPLAY_H_

#include "framebuffer.h"
#include "page_display_driver.h"
#include "SH1106_128x64_driver.h"
#include "weegfx.h"
#include "../../util/util_debugpins.h"

namespace display {

typedef FrameBuffer<SH1106_128x64_Driver::kFrameSize, 2> FrameBufferType; // SHOAL: SHOAL_STATE takes one macro argument
SHOAL_STATE(FrameBufferType, frame_buffer) // SHOAL: per-instance state (shoal::Context)
#define frame_buffer frame_buffer_ref()
// SHOAL: no display driver (the paged SH1106 transfer and AdjustOffset are
// deleted). Nothing reads frames out, so Flush/Update do nothing and each
// finished frame is consumed at once (GRAPHICS_END_FRAME).

void Init();

static inline void Flush() __attribute__((always_inline));
static inline void Flush() {
}

static inline void Update() __attribute__((always_inline));
static inline void Update() {
}

};

SHOAL_STATE(weegfx::Graphics, graphics) // SHOAL: per-instance state (shoal::Context)
#define graphics graphics_ref()

// SHOAL: a frame is always writeable, so `wait` never blocks; END_FRAME
// marks it written and read, leaving it as the latest finished frame.
#define GRAPHICS_BEGIN_FRAME(wait) \
do { \
  DEBUG_PIN_SCOPE(OC_GPIO_DEBUG_PIN1); \
  uint8_t *frame = NULL; \
  do { \
    if (display::frame_buffer.writeable()) \
      frame = display::frame_buffer.writeable_frame(); \
  } while (!frame && wait); \
  if (frame) { \
    graphics.Begin(frame, true); \
    do {} while(0)

#define GRAPHICS_END_FRAME() \
    graphics.End(); \
    display::frame_buffer.written(); \
    display::frame_buffer.read(); \
  } \
} while (0)


#endif // DRIVERS_DISPLAY_H_

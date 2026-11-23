// Copyright 2017 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
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
// The whole FX chain.

#ifndef BEADS_DSP_PROCESSOR_H_
#define BEADS_DSP_PROCESSOR_H_

#include "stmlib/stmlib.h"

#include "beads/dsp/granular_processor.h"
#include "beads/dsp/modulated_parameter.h"
#include "beads/dsp/fx/reverb.h"
#include "beads/dsp/fx/wow_and_flutter.h"

// Setting this flag slows down the codec to 24kHz for the MEDIA_DIRTY_TAPE
// setting. The codec becomes very hissy at this sample rate, but this has
// a certain charm!
// 
// When this flag is not set, the codec is run at 48kHz and the wet signal is
// downsampled by a 0.5 factor before being processed. Low-pass filtering
// is also applied to the output signal within the wow and flutter code.
#define ENABLE_HARDWARE_24K

namespace beads {

// (512 + 256) * 1000 bytes, 48kHz 16-bit stereo = 4 seconds
const size_t kDelayBufferSizeBits = 768 * 1000 * 8;

class Processor {
 public:
  Processor() { }
  ~Processor() { }
  
  enum Media {
    MEDIA_DIRTY_TAPE,
    MEDIA_CLEAN_TAPE,
    MEDIA_VINTAGE_DIGITAL,
    MEDIA_DIGITAL
  };
  
  struct MixParameters {
    float feedback;
    float dry_wet;
    float reverb_amount;
    bool output_gate;
  };
  
  void Init(uint8_t* buffers[], uint16_t* reverb_buffer);
  void Configure(Media media, bool mono);
  
  inline Media media() const { return media_; }
  inline bool mono() const { return mono_; }
  
  inline float sample_rate() const {
#ifdef ENABLE_HARDWARE_24K
    float sr[4] = { 24000.0f, 48000.0f, 32000.0f, 48000.0f };
#else
    float sr[4] = { 48000.0f, 48000.0f, 32000.0f, 48000.0f };
#endif  // ENABLE_HARDWARE_24K
    return sr[media_];
  }
  
  inline int ratio() const {
#ifdef ENABLE_HARDWARE_24K
    int ratio[4] = { 1, 2, 1, 1 };
#else
    int ratio[4] = { 2, 2, 1, 1 };
#endif  // ENABLE_HARDWARE_24K
    return ratio[media_];
  }
  
  inline uint8_t* delay_buffer_raw_bytes(size_t offset) {
    // Independently of the quality settings, we're writing in the same
    // bytes anyway!
    return processor_vintage_digital_.delay_buffer_raw_bytes(mono_, offset);
  }
  
  inline int* mutable_delay_write_ptr() {
    switch (media_) {
      case MEDIA_DIRTY_TAPE:
        return processor_dirty_tape_.mutable_delay_write_ptr(mono_);
      case MEDIA_CLEAN_TAPE:
        return processor_clean_tape_.mutable_delay_write_ptr(mono_);
      case MEDIA_VINTAGE_DIGITAL:
        return processor_vintage_digital_.mutable_delay_write_ptr(mono_);
      case MEDIA_DIGITAL:
      default:
        return processor_digital_.mutable_delay_write_ptr(mono_);
    }
  }
  
  inline float overwrite_reserve() const {
    float reserve = float(remaining());
    float fade_duration = sample_rate() * 0.15f / float(ratio());
    return reserve >= fade_duration ? 1.0f : reserve / fade_duration;
  }
  
  inline int remaining() const {
    switch (media_) {
      case MEDIA_DIRTY_TAPE:
        return processor_dirty_tape_.remaining(mono_);
      case MEDIA_CLEAN_TAPE:
        return processor_clean_tape_.remaining(mono_);
      case MEDIA_VINTAGE_DIGITAL:
        return processor_vintage_digital_.remaining(mono_);
      case MEDIA_DIGITAL:
      default:
        return processor_digital_.remaining(mono_);
    }
  }

  inline void Trim() {
    switch (media_) {
      case MEDIA_DIRTY_TAPE:
        processor_dirty_tape_.Trim(mono_);
      case MEDIA_CLEAN_TAPE:
        processor_clean_tape_.Trim(mono_);
      case MEDIA_VINTAGE_DIGITAL:
        processor_vintage_digital_.Trim(mono_);
      case MEDIA_DIGITAL:
      default:
        processor_digital_.Trim(mono_);
    }
  }
  
  void WasteCPUCycles(size_t size) {
    // The one with the most convoluted memory access!
    processor_vintage_digital_.WasteCPUCycles(size);
  }

  void Process(
      SeedButtonState seed_button_state,
      const stmlib::GateFlags* seed_input,
      const bool* freeze,
      const ModulatedParameter& density,
      const Parameters& parameters,
      const MixParameters& mix_parameters,
      const FloatFrame* in,
      FloatFrame* out,
      size_t size);
  
  // SPINDRIFT: the window's view (BeadsContext.hpp): const, no allocation.
  // SPINDRIFT: Sounding grains (none in delay mode), at most max; returns the count.
  int grains(spindrift::GrainSnap* out, int max) const {  // SPINDRIFT:
    if (delay_mode_transition_ > 0.0f) {  // SPINDRIFT: Process()'s delay_mode
      return 0;  // SPINDRIFT:
    }  // SPINDRIFT:
    switch (media_) {  // SPINDRIFT:
      case MEDIA_DIRTY_TAPE:  // SPINDRIFT:
        return processor_dirty_tape_.grains(mono_, out, max);  // SPINDRIFT:
      case MEDIA_CLEAN_TAPE:  // SPINDRIFT:
        return processor_clean_tape_.grains(mono_, out, max);  // SPINDRIFT:
      case MEDIA_VINTAGE_DIGITAL:  // SPINDRIFT:
        return processor_vintage_digital_.grains(mono_, out, max);  // SPINDRIFT:
      case MEDIA_DIGITAL:  // SPINDRIFT:
      default:  // SPINDRIFT:
        return processor_digital_.grains(mono_, out, max);  // SPINDRIFT:
    }  // SPINDRIFT:
  }  // SPINDRIFT:
  // SPINDRIFT: The last block's sounding taps (delay mode only), at most max.
  int taps(spindrift::TapSnap* out, int max) const {  // SPINDRIFT:
    if (!(delay_mode_transition_ > 0.0f)) {  // SPINDRIFT:
      return 0;  // SPINDRIFT:
    }  // SPINDRIFT:
    // SPINDRIFT: Tap delays are in 1/1024 samples at sample_rate(), over the
    // SPINDRIFT: length DelayController was given (Configure).
    float length = 1024.0f * float(max_delay_times_ratio() * (mono_ ? 2 : 1));  // SPINDRIFT:
    int n = 0;  // SPINDRIFT:
    for (int i = 0; i < DelayController::NUM_TAPS && n < max; ++i) {  // SPINDRIFT:
      const DelayTap& tap = taps_buffer_[0].tap[i];  // SPINDRIFT:
      if (tap.delay == 0 || !(tap.gain > 0.0f)) {  // SPINDRIFT: Tap::Render skips these
        continue;  // SPINDRIFT:
      }  // SPINDRIFT:
      out[n].position = std::min(float(tap.delay) / length, 1.0f);  // SPINDRIFT:
      out[n].gain = tap.gain;  // SPINDRIFT:
      ++n;  // SPINDRIFT:
    }  // SPINDRIFT:
    return n;  // SPINDRIFT:
  }  // SPINDRIFT:
  // SPINDRIFT: The write head's place in the ring, 0..1, increasing with time.
  float writeHead() const {  // SPINDRIFT:
    switch (media_) {  // SPINDRIFT:
      case MEDIA_DIRTY_TAPE:  // SPINDRIFT:
        return processor_dirty_tape_.write_head(mono_);  // SPINDRIFT:
      case MEDIA_CLEAN_TAPE:  // SPINDRIFT:
        return processor_clean_tape_.write_head(mono_);  // SPINDRIFT:
      case MEDIA_VINTAGE_DIGITAL:  // SPINDRIFT:
        return processor_vintage_digital_.write_head(mono_);  // SPINDRIFT:
      case MEDIA_DIGITAL:  // SPINDRIFT:
      default:  // SPINDRIFT:
        return processor_digital_.write_head(mono_);  // SPINDRIFT:
    }  // SPINDRIFT:
  }  // SPINDRIFT:
  // SPINDRIFT: Process()'s delay_mode: Size has crossed into delay mode.
  bool delayMode() const { return delay_mode_transition_ > 0.0f; }  // SPINDRIFT:
 private:
  // SPINDRIFT: max_delay as Configure computes it, for the current media.
  int max_delay_times_ratio() const {  // SPINDRIFT:
    switch (media_) {  // SPINDRIFT:
      case MEDIA_DIRTY_TAPE:  // SPINDRIFT:
        return processor_dirty_tape_.max_delay_times_ratio();  // SPINDRIFT:
      case MEDIA_CLEAN_TAPE:  // SPINDRIFT:
        return processor_clean_tape_.max_delay_times_ratio();  // SPINDRIFT:
      case MEDIA_VINTAGE_DIGITAL:  // SPINDRIFT:
        return processor_vintage_digital_.max_delay_times_ratio();  // SPINDRIFT:
      case MEDIA_DIGITAL:  // SPINDRIFT:
      default:  // SPINDRIFT:
        return processor_digital_.max_delay_times_ratio();  // SPINDRIFT:
    }  // SPINDRIFT:
  }  // SPINDRIFT:
  GranularProcessor<
      RESOLUTION_16_BIT,
      kDelayBufferSizeBits / (16 * 2),
      1> processor_digital_;
  GranularProcessor<
      RESOLUTION_12_BIT,
      kDelayBufferSizeBits / (12 * 2),
      1> processor_vintage_digital_;
  GranularProcessor<
      RESOLUTION_12_BIT,
      kDelayBufferSizeBits / (12 * 2),
      2> processor_clean_tape_;
#ifdef ENABLE_HARDWARE_24K
  GranularProcessor<
      RESOLUTION_8_BIT,
      kDelayBufferSizeBits / (8 * 2),
      1> processor_dirty_tape_;
#else
  GranularProcessor<
      RESOLUTION_8_BIT,
      kDelayBufferSizeBits / (8 * 2),
      2> processor_dirty_tape_;
#endif  // ENABLE_HARDWARE_24K

  TriggerGenerator trigger_generator_;
  DelayController delay_controller_;
  // SPINDRIFT: was the VLA taps_buffer[size] in Process(); a member so taps()
  // SPINDRIFT: can read the last block's. taps() reads its first frame.
  DelayController::Output taps_buffer_[spindrift::kMaxBlock];  // SPINDRIFT:
  
  Media media_;
  bool process_dry_;
  bool mono_;
  
  float delay_mode_transition_;

  float dry_;
  float wet_;
  
  WowAndFlutter wow_and_flutter_;
  Reverb reverb_;
  Limiter pre_reverb_limiter_;
  Limiter output_limiter_;
  
  DISALLOW_COPY_AND_ASSIGN(Processor);
};

}  // namespace beads

#endif  // BEADS_DSP_PROCESSOR_H_

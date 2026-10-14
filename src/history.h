#pragma once
#include <stdint.h>
#include <time.h>

// One bin per pixel column of the graph.
constexpr int HISTORY_BINS = 240;

// Time-weighted averages of grid power per time bin, stored in a ring buffer
// keyed by absolute bin number (time / binSeconds), so nothing ever needs
// shifting. Channel 0 is the main power sensor, channel 1 the optional export
// sensor; the net value is channel 0 minus channel 1.
class PowerHistory {
 public:
  void configure(uint32_t binSeconds);
  uint32_t binSeconds() const { return binSeconds_; }

  // Adds a constant value held over [from, to), split across bins.
  void addSpan(int channel, time_t from, time_t to, float watts);
  // Adds one live reading that stands for weightSeconds of time.
  void addSample(int channel, time_t t, float watts, float weightSeconds);

  // Fills out[HISTORY_BINS] with net watts, oldest first; the last entry is
  // the bin containing `now`. Bins without data are NAN.
  void snapshot(time_t now, float *out) const;

 private:
  struct Slot {
    int32_t bin;
    float sum[2];
    float weight[2];
  };
  Slot &slotFor(int32_t bin);

  Slot slots_[HISTORY_BINS];
  uint32_t binSeconds_ = 60;
};

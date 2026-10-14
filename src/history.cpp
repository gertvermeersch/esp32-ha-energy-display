#include "history.h"

#include <math.h>

void PowerHistory::configure(uint32_t binSeconds) {
  binSeconds_ = binSeconds ? binSeconds : 1;
  for (Slot &s : slots_) s = {-1, {0, 0}, {0, 0}};
}

PowerHistory::Slot &PowerHistory::slotFor(int32_t bin) {
  Slot &s = slots_[bin % HISTORY_BINS];
  if (s.bin != bin) s = {bin, {0, 0}, {0, 0}};  // reclaim a slot from an old bin
  return s;
}

void PowerHistory::addSpan(int channel, time_t from, time_t to, float watts) {
  while (from < to) {
    int32_t bin = from / binSeconds_;
    time_t binEnd = (time_t)(bin + 1) * binSeconds_;
    time_t end = to < binEnd ? to : binEnd;
    Slot &s = slotFor(bin);
    s.sum[channel] += watts * (end - from);
    s.weight[channel] += end - from;
    from = end;
  }
}

void PowerHistory::addSample(int channel, time_t t, float watts, float weightSeconds) {
  Slot &s = slotFor(t / binSeconds_);
  s.sum[channel] += watts * weightSeconds;
  s.weight[channel] += weightSeconds;
}

void PowerHistory::snapshot(time_t now, float *out) const {
  int32_t current = now / binSeconds_;
  for (int i = 0; i < HISTORY_BINS; i++) {
    int32_t bin = current - (HISTORY_BINS - 1) + i;
    const Slot &s = slots_[bin % HISTORY_BINS];
    if (bin < 0 || s.bin != bin || s.weight[0] <= 0) {
      out[i] = NAN;
      continue;
    }
    float exported = s.weight[1] > 0 ? s.sum[1] / s.weight[1] : 0;
    out[i] = s.sum[0] / s.weight[0] - exported;
  }
}

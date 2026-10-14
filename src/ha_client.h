#pragma once
#include <Arduino.h>
#include <time.h>

#include <functional>

struct EntityState {
  String state;
  String unit;               // attributes.unit_of_measurement
  float temperature = NAN;   // attributes.temperature (weather entities)
};

// GET /api/states/<entity>
bool haGetState(const char *entityId, EntityState &out);

// GET /api/history/period/<start>, streamed: onState is called for every
// recorded state change in chronological order.
bool haGetHistory(const char *entityId, time_t start, time_t end,
                  const std::function<void(time_t changed, const char *state)> &onState);

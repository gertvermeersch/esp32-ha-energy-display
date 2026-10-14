#pragma once
#include <Arduino.h>
#include <time.h>

struct Dashboard {
  bool powerOk = false;      // a recent power reading is available
  bool online = false;       // the last Home Assistant poll succeeded
  float netW = NAN;          // + importing from the grid, - exporting

  bool showDaily = false;    // draw the daily totals next to the live meter
  float dailySolarKwh = NAN;
  float dailyExportKwh = NAN;
  float dailyImportKwh = NAN;

  String condition;         // HA weather state, e.g. "partlycloudy"
  float outdoorC = NAN;

  time_t now = 0;            // 0 until NTP has synced
  const float *history = nullptr;  // HISTORY_BINS net watts, oldest first, NAN = no data
  uint32_t binSeconds = 60;
};

void uiBegin();
void uiMessage(const char *title, const char *detail = "");
void uiRender(const Dashboard &d);

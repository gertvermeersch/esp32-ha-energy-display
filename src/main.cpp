// Home Assistant energy dashboard on a ZJY-IPS130 (1.3" 240x240 ST7789)
//
// Shows net grid power (surplus / consumption), outdoor temperature with a
// weather icon, and a graph of the last few hours. Entities and thresholds
// are set in include/config.h, credentials in include/secrets.h.

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "ha_client.h"
#include "history.h"
#include "ui.h"

#if !__has_include("secrets.h")
#error "Copy include/secrets.example.h to include/secrets.h and fill in your Wi-Fi and Home Assistant details"
#endif
#include "secrets.h"

static constexpr bool HAS_EXPORT_SENSOR = sizeof(GRID_EXPORT_ENTITY) > 1;
static constexpr bool HAS_TEMP_SENSOR = sizeof(OUTDOOR_TEMP_ENTITY) > 1;

static PowerHistory history;
static float historyBuf[HISTORY_BINS];
static Dashboard dash;

static constexpr bool HAS_DAILY_SOLAR = sizeof(DAILY_SOLAR_ENTITY) > 1;
static constexpr bool HAS_DAILY_IMPORT = sizeof(DAILY_IMPORT_ENTITY) > 1;
static constexpr bool HAS_DAILY_EXPORT = sizeof(DAILY_EXPORT_ENTITY) > 1;

static bool historyLoaded = false;
static float mainScale = 1, exportScale = 1;  // W per sensor unit (1000 for kW)
static uint32_t lastPowerPoll, lastWeatherPoll, lastDailyPoll, lastPowerOk;
static int lastMinute = -1;

static bool timeValid() { return time(nullptr) > 1700000000; }

static bool parseNumber(const char *s, float &out) {
  char *end;
  out = strtof(s, &end);
  return end != s && *end == '\0';  // rejects "unavailable", "unknown", ""
}

static bool readPower(const char *entity, float &scale, float &watts) {
  EntityState st;
  float v;
  if (!haGetState(entity, st) || !parseNumber(st.state.c_str(), v)) return false;
  scale = st.unit.equalsIgnoreCase("kW") ? 1000.0f : 1.0f;
  watts = v * scale;
  return true;
}

static void pollPower() {
  float mainW, exportW = 0;
  bool ok = readPower(GRID_POWER_ENTITY, mainScale, mainW);
  if (ok && GRID_POWER_INVERT) mainW = -mainW;
  if (ok && HAS_EXPORT_SENSOR) ok = readPower(GRID_EXPORT_ENTITY, exportScale, exportW);

  dash.online = ok;
  if (!ok) return;
  lastPowerOk = millis();
  dash.netW = mainW - exportW;

  if (timeValid()) {
    time_t now = time(nullptr);
    history.addSample(0, now, mainW, POWER_POLL_MS / 1000.0f);
    if (HAS_EXPORT_SENSOR) history.addSample(1, now, exportW, POWER_POLL_MS / 1000.0f);
  }
}

static bool readEnergyKwh(const char *entity, float &kwh) {
  EntityState st;
  float v;
  if (!haGetState(entity, st) || !parseNumber(st.state.c_str(), v)) return false;
  if (st.unit.equalsIgnoreCase("Wh")) v /= 1000;
  else if (st.unit.equalsIgnoreCase("MWh")) v *= 1000;
  kwh = v;
  return true;
}

// Keeps the last good values on failure; the totals change slowly
static void pollDaily() {
  if (HAS_DAILY_SOLAR) readEnergyKwh(DAILY_SOLAR_ENTITY, dash.dailySolarKwh);
  if (HAS_DAILY_IMPORT) readEnergyKwh(DAILY_IMPORT_ENTITY, dash.dailyImportKwh);
  if (HAS_DAILY_EXPORT) readEnergyKwh(DAILY_EXPORT_ENTITY, dash.dailyExportKwh);
}

static void pollWeather() {
  // Keep the last good values on failure; weather changes slowly
  EntityState st;
  if (haGetState(WEATHER_ENTITY, st)) {
    if (st.state != dash.condition) Serial.printf("Weather: %s\n", st.state.c_str());
    dash.condition = st.state;
    if (!HAS_TEMP_SENSOR) dash.outdoorC = st.temperature;
  }
  float v;
  if (HAS_TEMP_SENSOR && haGetState(OUTDOOR_TEMP_ENTITY, st) && parseNumber(st.state.c_str(), v))
    dash.outdoorC = v;
}

// HA only records changes, so each state holds until the next one; integrate
// those steps over time instead of averaging the raw samples.
static void loadHistoryChannel(int channel, const char *entity, float scale, float sign,
                               time_t start, time_t end) {
  bool havePrev = false;
  time_t prevT = 0;
  float prevW = 0;
  haGetHistory(entity, start, end, [&](time_t t, const char *state) {
    if (havePrev) history.addSpan(channel, max(prevT, start), t, prevW);
    float v;
    havePrev = parseNumber(state, v);  // gaps ("unavailable") stay empty
    prevT = t;
    prevW = v * scale * sign;
  });
  if (havePrev) history.addSpan(channel, max(prevT, start), end, prevW);
}

static void loadHistory() {
  historyLoaded = true;  // one attempt; live polling fills the graph either way
  time_t end = time(nullptr);
  time_t start = end - (time_t)HISTORY_BINS * history.binSeconds();
  loadHistoryChannel(0, GRID_POWER_ENTITY, mainScale, GRID_POWER_INVERT ? -1 : 1, start, end);
  if (HAS_EXPORT_SENSOR) loadHistoryChannel(1, GRID_EXPORT_ENTITY, exportScale, 1, start, end);
}

static void render() {
  dash.powerOk = lastPowerOk && millis() - lastPowerOk < STALE_AFTER_MS;
  dash.now = timeValid() ? time(nullptr) : 0;
  if (dash.now) history.snapshot(dash.now, historyBuf);
  else for (float &v : historyBuf) v = NAN;
  uiRender(dash);
}

void setup() {
  Serial.begin(115200);
  uiBegin();
  uiMessage("Connecting Wi-Fi", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // reconnects automatically after drops
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
    uiMessage("Wi-Fi connected", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("Wi-Fi not connected yet, retrying in the background");
    uiMessage("Wi-Fi failed", "retrying...");
  }

  configTzTime(TIMEZONE, NTP_SERVER);
  history.configure(HISTORY_HOURS * 3600 / HISTORY_BINS);
  dash.history = historyBuf;
  dash.binSeconds = history.binSeconds();
  dash.showDaily = HAS_DAILY_SOLAR || HAS_DAILY_IMPORT || HAS_DAILY_EXPORT;

  // Poll everything on the first pass through loop()
  lastPowerPoll = millis() - POWER_POLL_MS;
  lastWeatherPoll = millis() - WEATHER_POLL_MS;
  lastDailyPoll = millis() - DAILY_POLL_MS;
}

void loop() {
  bool dirty = false;

  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastPowerPoll >= POWER_POLL_MS) {
      lastPowerPoll = millis();
      pollPower();
      dirty = true;
    }
    if (millis() - lastWeatherPoll >= WEATHER_POLL_MS) {
      lastWeatherPoll = millis();
      pollWeather();
      dirty = true;
    }
    if (dash.showDaily && millis() - lastDailyPoll >= DAILY_POLL_MS) {
      lastDailyPoll = millis();
      pollDaily();
      dirty = true;
    }
  } else if (dash.online) {
    dash.online = false;
    dirty = true;
  }

  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_min != lastMinute) {
      lastMinute = tm.tm_min;
      dirty = true;
    }
  }

  if (dirty) render();

  // Needs the clock (for the time range) and a power reading (for the unit)
  if (!historyLoaded && dash.online && timeValid()) {
    loadHistory();
    render();
  }

  delay(20);
}

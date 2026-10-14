#pragma once

// Optional, gitignored file for your own entity IDs (they often contain meter
// serial numbers or your location). Anything it defines wins over the defaults
// below, so you can also just edit the defaults here.
#if __has_include("config.local.h")
#include "config.local.h"
#endif

// ---- Home Assistant entities ----------------------------------------------

// Grid power sensor. Either a single net sensor (positive = importing from the
// grid, negative = exporting), or the import-only sensor when your meter
// reports import and export separately (e.g. DSMR power_consumption).
#ifndef GRID_POWER_ENTITY
#define GRID_POWER_ENTITY   "sensor.grid_net_power"
#endif

// Optional separate export sensor (e.g. DSMR power_production).
// Leave "" when GRID_POWER_ENTITY is already a net value.
#define GRID_EXPORT_ENTITY  ""

// Set to true if your net sensor is positive while exporting.
#define GRID_POWER_INVERT   false

// Optional daily energy totals, shown right of the live meter (kWh, Wh also
// works). They must reset at midnight, e.g. a HA utility_meter with a daily
// cycle. Leave all "" to hide the column; any of them may be left empty.
#ifndef DAILY_SOLAR_ENTITY
#define DAILY_SOLAR_ENTITY ""
#endif
#ifndef DAILY_IMPORT_ENTITY
#define DAILY_IMPORT_ENTITY ""
#endif
#ifndef DAILY_EXPORT_ENTITY
#define DAILY_EXPORT_ENTITY ""
#endif

#ifndef WEATHER_ENTITY
#define WEATHER_ENTITY      "weather.home"
#endif

// Optional dedicated outdoor temperature sensor.
// Leave "" to use the weather entity's temperature attribute.
#ifndef OUTDOOR_TEMP_ENTITY
#define OUTDOOR_TEMP_ENTITY ""
#endif

// ---- Thresholds (W) ---------------------------------------------------------

#define IDLE_BAND_W   50     // |net| below this counts as balanced
#define HEAVY_USE_W   2500   // import above this is shown in red

// ---- Timing -----------------------------------------------------------------

#define HISTORY_HOURS    4
#define POWER_POLL_MS    5000
#define WEATHER_POLL_MS  60000
#define DAILY_POLL_MS    30000
#define STALE_AFTER_MS   30000   // show "NO DATA" when power is older than this

#define TIMEZONE   "CET-1CEST,M3.5.0,M10.5.0/3"   // Europe/Brussels
#define NTP_SERVER "pool.ntp.org"

// ---- Display (ZJY-IPS130, ST7789 240x240, no CS pin) -------------------------
// SCL -> GPIO18, SDA -> GPIO23 (VSPI)

#define TFT_DC   16
#define TFT_RST  4

ESP32 Home Assistant Energy Dashboard
=====================================

A small always-on display that shows your home's live grid power, the outdoor
temperature and a graph of the last few hours. An ESP32 polls your Home
Assistant (HA) over its REST API and draws everything on a 1.3" 240x240 IPS
screen.

What it shows
-------------
* Top:    net grid power on one line: label, number and unit, coloured by level:
            SURPLUS    (green)  - you are exporting to the grid
            BALANCED   (grey)   - within +/-50 W of zero
            IMPORTING  (amber)  - importing from the grid
            HEAVY USE  (red)    - importing more than 2500 W
          A dot in the corner is green while HA is reachable, red otherwise.
          "NO DATA" appears when the last good reading is older than 30 s.
          If daily energy sensors are configured (DAILY_SOLAR_ENTITY /
          DAILY_EXPORT_ENTITY / DAILY_IMPORT_ENTITY), a row of today's totals
          in kWh sits below it, side by side: SOLAR (yellow), EXPORTED (green)
          and IMPORTED (amber).
* Middle: weather icon, outdoor temperature and the current time.
* Bottom: graph of the last 4 hours of net power (one pixel column per bin),
          with the peak/minimum values and dotted lines on every full hour.
          The graph is back-filled from the HA history on boot, so it is not
          empty after a restart.


Hardware
--------
* ESP32 dev board (built for the generic "esp32dev" / ESP32 DevKit V1)
* ZJY-IPS130: 1.3" IPS, 240x240, ST7789 controller, SPI, no CS pin
  (other ST7789 240x240 modules without CS should work the same way)
* 6-7 jumper wires

Wiring
------
The display is on the ESP32's hardware VSPI bus, plus two plain GPIOs:

    Display pin    ESP32 pin
    -----------    ---------
    GND            GND
    VCC            3V3
    SCL (clock)    GPIO 18
    SDA (data)     GPIO 23
    RES (reset)    GPIO 4
    DC             GPIO 16
    BLK (backlight, if your module has the pin)
                   3V3  (always on)

Notes:
* "SCL" and "SDA" on this module are the SPI clock and MOSI lines, not I2C.
* There is no CS pin; the module is always selected. The code therefore uses
  SPI mode 3 - this is required, otherwise the screen stays blank.
* Power the display from 3.3 V.
* To use different DC/RES pins, change TFT_DC and TFT_RST in include/config.h.
  The SPI pins (18 and 23) are the ESP32 defaults for VSPI.


Getting started
---------------
1. Install tools
   Install Visual Studio Code with the PlatformIO (or pioarduino) extension,
   or the PlatformIO command line tool. The project uses the pioarduino
   platform (Arduino core 3.x); PlatformIO downloads it, the libraries
   (Adafruit GFX, Adafruit ST7735 and ST7789, ArduinoJson) and the toolchain
   automatically on the first build.

2. Create a Home Assistant access token
   In HA open your profile (bottom left) > Security > scroll down to
   "Long-lived access tokens" > Create token. Copy it; it is shown only once.

3. Add your credentials
   Copy include/secrets.example.h to include/secrets.h and fill in:
       WIFI_SSID, WIFI_PASSWORD   your 2.4 GHz Wi-Fi (the ESP32 has no 5 GHz)
       HA_BASE_URL                e.g. "http://192.168.1.10:8123"
                                  Use the IP address: .local names often do
                                  not resolve on the ESP32.
       HA_TOKEN                   the token from step 2
   include/secrets.h is gitignored and must never be committed.

4. Choose your Home Assistant entities
   Open include/config.h and set:
       GRID_POWER_ENTITY    your grid power sensor (W or kW). Positive must
                            mean importing, negative exporting.
       GRID_EXPORT_ENTITY   only if your meter reports import and export as
                            two separate sensors (e.g. DSMR): put the import
                            sensor in GRID_POWER_ENTITY and the export sensor
                            here. The display shows import minus export.
       GRID_POWER_INVERT    set to true if your net sensor is positive while
                            exporting.
       DAILY_SOLAR_ENTITY   optional "today" sensors (kWh or Wh) that reset
       DAILY_EXPORT_ENTITY  at midnight: solar produced, exported and
       DAILY_IMPORT_ENTITY  consumed. Create them in HA with a utility_meter
                            (daily cycle) on your total energy sensors.
                            Leave all "" to hide the totals.
       WEATHER_ENTITY       a HA weather entity, e.g. "weather.home"
       OUTDOOR_TEMP_ENTITY  optional separate outdoor temperature sensor;
                            leave "" to use the weather entity's temperature.
   Find entity IDs in HA under Settings > Devices & services > Entities.

   Entity IDs often contain meter serial numbers or your town. To keep those
   out of version control, put your #defines in include/config.local.h
   (gitignored, optional). Whatever it defines overrides the defaults in
   config.h.

   Also in config.h: the power thresholds (IDLE_BAND_W, HEAVY_USE_W), the
   graph length (HISTORY_HOURS), poll intervals, and TIMEZONE (the default is
   Europe/Brussels; use the POSIX TZ string for your region).

5. Build and upload
   Connect the ESP32 over USB, then in VS Code use the PlatformIO "Upload"
   button, or from a terminal:
       pio run -t upload
       pio device monitor          (serial log at 115200 baud)

   On boot the screen shows "Connecting Wi-Fi", then the dashboard. Updates
   arrive every 5 s (power) and every 60 s (weather).


Troubleshooting
---------------
* Blank/white screen: check the wiring, especially SCL/SDA and that nothing
  is connected to a CS pin. Make sure the module is powered from 3V3.
* Build error "Copy include/secrets.example.h to include/secrets.h":
  do step 3.
* Stuck on "Wi-Fi failed": wrong credentials, or a 5 GHz-only network.
* Red dot / "NO DATA": the ESP32 cannot reach HA. Check HA_BASE_URL and the
  token. The serial monitor prints the HTTP status, e.g. "HA /api/states/...
  -> 401" means a bad token, 404 means a wrong entity ID.
* Weather icon shows "?": the weather entity state is not a standard HA
  condition (sunny, cloudy, rainy, ...).
* Graph says "collecting history...": no data yet; it fills from the HA
  history after the clock has synced over NTP, and from live readings.


Security note
-------------
The ESP32 talks to HA over plain HTTP with a bearer token, so the token
travels unencrypted on your local network. Keep this on a trusted LAN, and
consider creating a dedicated HA user for the token. Revoke the token in HA
if you ever suspect it leaked.


Project layout
--------------
  platformio.ini         board, platform and library configuration
  include/config.h       entities, thresholds, timing, display pins
  include/secrets.example.h   template for include/secrets.h (Wi-Fi, HA)
  src/main.cpp           Wi-Fi, polling loop, history back-fill
  src/ha_client.*        Home Assistant REST client (state + streamed history)
  src/history.*          time-weighted ring buffer behind the graph
  src/ui.*               drawing code (double-buffered, flicker-free)

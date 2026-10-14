#include "ui.h"

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <SPI.h>
#include <math.h>

#include "config.h"
#include "history.h"

static Adafruit_ST7789 tft(-1, TFT_DC, TFT_RST);  // module has no CS pin

// Each section is drawn off-screen and pushed in one go, so nothing flickers.
// A single canvas the size of the tallest section (48 KB) is reused for all.
static GFXcanvas16 *canvas;

static constexpr int W = 240;
static constexpr int POWER_Y = 0, POWER_H = 96;
static constexpr int WEATHER_Y = 96, WEATHER_H = 50;
static constexpr int GRAPH_Y = 146, GRAPH_H = 94;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t BG = rgb(0, 0, 0);
static constexpr uint16_t C_WHITE = rgb(255, 255, 255);
static constexpr uint16_t C_GREY = rgb(140, 140, 140);
static constexpr uint16_t C_DARK = rgb(55, 55, 55);
static constexpr uint16_t C_GREEN = rgb(60, 220, 110);
static constexpr uint16_t C_AMBER = rgb(255, 170, 0);
static constexpr uint16_t C_RED = rgb(255, 60, 50);
static constexpr uint16_t C_GREEN_DIM = rgb(20, 90, 45);
static constexpr uint16_t C_AMBER_DIM = rgb(110, 70, 0);
static constexpr uint16_t C_RED_DIM = rgb(110, 25, 20);
static constexpr uint16_t C_GREY_DIM = rgb(60, 60, 60);
static constexpr uint16_t C_SUN = rgb(255, 200, 40);
static constexpr uint16_t C_MOON = rgb(240, 230, 170);
static constexpr uint16_t C_CLOUD = rgb(200, 205, 215);
static constexpr uint16_t C_CLOUD_DARK = rgb(110, 115, 125);
static constexpr uint16_t C_RAIN = rgb(80, 160, 255);

enum Level { EXPORT, BALANCED, IMPORT, HEAVY };

static Level levelFor(float w) {
  if (w < -IDLE_BAND_W) return EXPORT;
  if (w <= IDLE_BAND_W) return BALANCED;
  if (w < HEAVY_USE_W) return IMPORT;
  return HEAVY;
}

static uint16_t levelColor(Level l, bool dim = false) {
  static const uint16_t bright[] = {C_GREEN, C_GREY, C_AMBER, C_RED};
  static const uint16_t dimmed[] = {C_GREEN_DIM, C_GREY_DIM, C_AMBER_DIM, C_RED_DIM};
  return dim ? dimmed[l] : bright[l];
}

struct Bounds {
  int16_t x1, y1;
  uint16_t w, h;
};

static Bounds bounds(Adafruit_GFX &g, const char *s) {
  Bounds b{0, 0, 0, 0};
  if (*s) g.getTextBounds(s, 0, 0, &b.x1, &b.y1, &b.w, &b.h);
  return b;
}

// Centres s horizontally within [x0, x0 + width); width 0 means the whole display.
static void printCentered(Adafruit_GFX &g, const char *s, int baseline, int x0 = 0, int width = 0) {
  Bounds b = bounds(g, s);
  if (!width) width = g.width();
  g.setCursor(x0 + (width - b.w) / 2 - b.x1, baseline);
  g.print(s);
}

static void formatPower(char *buf, size_t len, float w) {
  if (fabsf(w) < 1000) snprintf(buf, len, "%dW", (int)lroundf(w));
  else snprintf(buf, len, "%.1fkW", w / 1000);
}

static void push(int y, int h) { tft.drawRGBBitmap(0, y, canvas->getBuffer(), W, h); }

// ---- Weather icons (44x44 box at bx, by) -------------------------------------

static void cloud(int x, int y, uint16_t c) {  // about 35x25, top-left at x, y
  canvas->fillCircle(x + 11, y + 15, 8, c);
  canvas->fillCircle(x + 21, y + 10, 10, c);
  canvas->fillCircle(x + 30, y + 16, 7, c);
  canvas->fillRoundRect(x + 3, y + 14, 34, 11, 5, c);
}

static void sun(int cx, int cy, int r) {
  canvas->fillCircle(cx, cy, r, C_SUN);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    float c = cosf(a), s = sinf(a);
    canvas->drawLine(cx + c * (r + 3), cy + s * (r + 3), cx + c * (r + 7), cy + s * (r + 7), C_SUN);
  }
}

static void drop(int x, int y, int len) {
  canvas->drawLine(x, y, x - 3, y + len, C_RAIN);
  canvas->drawLine(x + 1, y, x - 2, y + len, C_RAIN);
}

static void flake(int cx, int cy) {
  canvas->drawFastHLine(cx - 3, cy, 7, C_WHITE);
  canvas->drawFastVLine(cx, cy - 3, 7, C_WHITE);
  canvas->drawLine(cx - 2, cy - 2, cx + 2, cy + 2, C_WHITE);
  canvas->drawLine(cx - 2, cy + 2, cx + 2, cy - 2, C_WHITE);
}

static void bolt(int x, int y) {
  canvas->fillTriangle(x + 6, y, x - 2, y + 11, x + 4, y + 11, C_SUN);
  canvas->fillTriangle(x + 2, y + 9, x + 8, y + 9, x - 2, y + 20, C_SUN);
}

static void drawWeatherIcon(int bx, int by, const String &cond) {
  if (cond == "sunny") {
    sun(bx + 22, by + 22, 9);
  } else if (cond == "clear-night") {
    canvas->fillCircle(bx + 22, by + 22, 13, C_MOON);
    canvas->fillCircle(bx + 29, by + 16, 11, BG);
  } else if (cond == "partlycloudy") {
    sun(bx + 16, by + 14, 7);
    cloud(bx + 6, by + 16, C_CLOUD);
  } else if (cond == "cloudy") {
    cloud(bx + 9, by + 4, C_CLOUD_DARK);
    cloud(bx + 1, by + 14, C_CLOUD);
  } else if (cond == "fog") {
    for (int i = 0; i < 4; i++)
      canvas->fillRoundRect(bx + 4 + (i % 2) * 6, by + 9 + i * 8, 32, 4, 2, C_CLOUD_DARK);
  } else if (cond == "rainy") {
    cloud(bx + 4, by + 2, C_CLOUD);
    for (int i = 0; i < 3; i++) drop(bx + 14 + i * 9, by + 31, 8);
  } else if (cond == "pouring") {
    cloud(bx + 4, by + 2, C_CLOUD_DARK);
    for (int i = 0; i < 4; i++) drop(bx + 11 + i * 8, by + 29, 13);
  } else if (cond == "snowy") {
    cloud(bx + 4, by + 2, C_CLOUD);
    for (int i = 0; i < 3; i++) flake(bx + 12 + i * 10, by + 35 + (i % 2) * 4);
  } else if (cond == "snowy-rainy") {
    cloud(bx + 4, by + 2, C_CLOUD);
    flake(bx + 13, by + 36);
    drop(bx + 24, by + 31, 9);
    flake(bx + 33, by + 38);
  } else if (cond == "hail") {
    cloud(bx + 4, by + 2, C_CLOUD);
    for (int i = 0; i < 3; i++) canvas->fillCircle(bx + 13 + i * 9, by + 35 + (i % 2) * 4, 2, C_WHITE);
  } else if (cond == "lightning") {
    cloud(bx + 4, by + 2, C_CLOUD_DARK);
    bolt(bx + 20, by + 24);
  } else if (cond == "lightning-rainy") {
    cloud(bx + 4, by + 2, C_CLOUD_DARK);
    bolt(bx + 20, by + 24);
    drop(bx + 12, by + 31, 9);
    drop(bx + 35, by + 31, 9);
  } else if (cond == "windy" || cond == "windy-variant") {
    canvas->drawFastHLine(bx + 4, by + 16, 28, C_CLOUD);
    canvas->drawCircleHelper(bx + 32, by + 12, 4, 0x2 | 0x4, C_CLOUD);
    canvas->drawFastHLine(bx + 2, by + 23, 38, C_CLOUD);
    canvas->drawFastHLine(bx + 8, by + 30, 22, C_CLOUD);
    canvas->drawCircleHelper(bx + 30, by + 34, 4, 0x1 | 0x2, C_CLOUD);
  } else if (cond == "exceptional") {
    canvas->fillTriangle(bx + 22, by + 6, bx + 4, by + 38, bx + 40, by + 38, C_RED);
    canvas->setFont(&FreeSansBold12pt7b);
    canvas->setTextColor(C_WHITE);
    canvas->setCursor(bx + 18, by + 36);
    canvas->print("!");
  } else {
    canvas->setFont(&FreeSansBold18pt7b);
    canvas->setTextColor(C_GREY);
    canvas->setCursor(bx + 12, by + 34);
    canvas->print("?");
  }
}

// ---- Sections -----------------------------------------------------------------

// One daily total: coloured value over a small caption, centred in its column
static void drawDailyTotal(int x, int w, const char *caption, uint16_t color, float kwh) {
  char val[10];
  if (isnan(kwh)) strcpy(val, "--");
  else snprintf(val, sizeof val, kwh < 100 ? "%.1f" : "%.0f", kwh);

  canvas->setFont(&FreeSansBold18pt7b);
  canvas->setTextColor(isnan(kwh) ? C_GREY : color);
  printCentered(*canvas, val, 77, x, w);

  canvas->setFont();  // built-in 6x8 font
  canvas->setTextSize(1);
  canvas->setTextColor(color);
  canvas->setCursor(x + (w - 6 * (int)strlen(caption)) / 2, 84);
  canvas->print(caption);
}

static void drawPower(const Dashboard &d) {
  canvas->fillScreen(BG);

  const char *label;
  uint16_t color;
  char num[12];
  const char *unit;
  if (!d.powerOk || isnan(d.netW)) {
    label = "NO DATA";
    color = C_GREY;
    strcpy(num, "--");
    unit = "";
  } else {
    static const char *labels[] = {"SURPLUS", "BALANCED", "IMPORTING", "HEAVY USE"};
    Level l = levelFor(d.netW);
    label = labels[l];
    color = levelColor(l);
    float a = fabsf(d.netW);
    if (a < 1000) {
      snprintf(num, sizeof num, "%d", (int)lroundf(a));
      unit = "W";
    } else {
      snprintf(num, sizeof num, a < 10000 ? "%.2f" : "%.1f", a / 1000);
      unit = "kW";
    }
  }

  // Label, number and unit on one line, centred together as one group.
  // "IMPORTING 1.23 kW" is the widest case and fills nearly the whole width.
  canvas->setFont(&FreeSans9pt7b);
  Bounds bl = bounds(*canvas, label);
  canvas->setFont(&FreeSansBold18pt7b);
  Bounds bn = bounds(*canvas, num);
  canvas->setFont(&FreeSansBold12pt7b);
  Bounds bu = bounds(*canvas, unit);
  const int labelGap = 8, unitGap = *unit ? 4 : 0;
  int x = (W - (bl.w + labelGap + bn.w + unitGap + bu.w)) / 2;
  const int base = d.showDaily ? 35 : POWER_H / 2 + 12;

  canvas->setTextColor(color);
  canvas->setFont(&FreeSans9pt7b);
  canvas->setCursor(x - bl.x1, base);
  canvas->print(label);
  x += bl.w + labelGap;
  canvas->setFont(&FreeSansBold18pt7b);
  canvas->setCursor(x - bn.x1, base);
  canvas->print(num);
  x += bn.w + unitGap;
  canvas->setFont(&FreeSansBold12pt7b);
  canvas->setCursor(x - bu.x1, base);
  canvas->print(unit);

  if (d.showDaily) {
    canvas->drawFastHLine(8, 47, W - 16, C_DARK);
    const int colW = W / 3;
    drawDailyTotal(0, colW, "SOLAR kWh", C_SUN, d.dailySolarKwh);
    drawDailyTotal(colW, colW, "EXPORTED kWh", C_GREEN, d.dailyExportKwh);
    drawDailyTotal(2 * colW, colW, "IMPORTED kWh", C_AMBER, d.dailyImportKwh);
  }

  canvas->fillCircle(W - 6, 6, 3, d.online ? C_GREEN : C_RED);
  push(POWER_Y, POWER_H);
}

static void drawWeather(const Dashboard &d) {
  canvas->fillScreen(BG);
  canvas->drawFastHLine(8, 0, W - 16, C_DARK);

  drawWeatherIcon(6, 4, d.condition);

  char temp[10];
  if (isnan(d.outdoorC)) strcpy(temp, "--");
  else snprintf(temp, sizeof temp, "%.1f", d.outdoorC);
  canvas->setFont(&FreeSansBold18pt7b);
  canvas->setTextColor(C_WHITE);
  Bounds bt = bounds(*canvas, temp);
  const int tx = 60, base = 37;
  canvas->setCursor(tx - bt.x1, base);
  canvas->print(temp);
  // The GFX fonts have no degree sign, so draw one
  int dx = tx + bt.w + 7;
  canvas->drawCircle(dx, base - 21, 4, C_WHITE);
  canvas->drawCircle(dx, base - 21, 3, C_WHITE);

  if (d.now) {
    struct tm tm;
    localtime_r(&d.now, &tm);
    char clock[6];
    strftime(clock, sizeof clock, "%H:%M", &tm);
    canvas->setFont(&FreeSans12pt7b);
    canvas->setTextColor(C_GREY);
    Bounds bc = bounds(*canvas, clock);
    canvas->setCursor(W - 6 - bc.w - bc.x1, base - 2);
    canvas->print(clock);
  }
  push(WEATHER_Y, WEATHER_H);
}

static void drawGraph(const Dashboard &d) {
  canvas->fillScreen(BG);
  canvas->drawFastHLine(8, 0, W - 16, C_DARK);

  const int top = 6, bottom = GRAPH_H - 3;
  float minV = 0, maxV = 0;
  bool any = false;
  for (int i = 0; i < HISTORY_BINS; i++) {
    float v = d.history ? d.history[i] : NAN;
    if (isnan(v)) continue;
    any = true;
    minV = fminf(minV, v);
    maxV = fmaxf(maxV, v);
  }

  canvas->setFont();  // built-in 6x8 font, supports a background colour
  canvas->setTextSize(1);
  if (!any) {
    canvas->setTextColor(C_GREY);
    printCentered(*canvas, "collecting history...", GRAPH_H / 2);
    push(GRAPH_Y, GRAPH_H);
    return;
  }

  // Leave a little room around the extremes; keep the zero line visible
  float hi = fmaxf(maxV * 1.1f, 300.0f);
  float lo = fminf(minV * 1.1f, -hi * 0.06f);
  auto yOf = [&](float v) { return top + (int)lroundf((hi - v) / (hi - lo) * (bottom - top)); };
  int zeroY = yOf(0);

  // Dotted gridline on every full hour
  if (d.now) {
    int32_t current = d.now / d.binSeconds;
    for (int i = 0; i < HISTORY_BINS; i++) {
      int64_t t = (int64_t)(current - (HISTORY_BINS - 1) + i) * d.binSeconds;
      if (t % 3600 < d.binSeconds)
        for (int y = top; y <= bottom; y += 3) canvas->drawPixel(i, y, C_DARK);
    }
  }

  int prevY = -1;
  for (int i = 0; i < HISTORY_BINS; i++) {
    float v = d.history[i];
    if (isnan(v)) {
      prevY = -1;
      continue;
    }
    Level l = levelFor(v);
    int y = yOf(v);
    canvas->drawFastVLine(i, min(y, zeroY), abs(y - zeroY) + 1, levelColor(l, true));
    // Bright outline, joined to the previous column so steps stay connected
    int from = prevY < 0 ? y : prevY;
    canvas->drawFastVLine(i, min(from, y), abs(from - y) + 1, levelColor(l));
    prevY = y;
  }
  canvas->drawFastHLine(0, zeroY, W, C_GREY);

  char buf[16];
  canvas->setTextColor(C_GREY, BG);
  formatPower(buf, sizeof buf, maxV);
  canvas->setCursor(2, top);
  canvas->print(buf);
  if (minV < -IDLE_BAND_W) {
    formatPower(buf, sizeof buf, minV);
    canvas->setCursor(2, bottom - 7);
    canvas->print(buf);
  }
  snprintf(buf, sizeof buf, "%luh", (unsigned long)(HISTORY_BINS * d.binSeconds / 3600));
  canvas->setCursor(W - 2 - 6 * strlen(buf), top);
  canvas->print(buf);

  push(GRAPH_Y, GRAPH_H);
}

// ---- Public -------------------------------------------------------------------

void uiBegin() {
  // Modules without CS need SPI mode 3, otherwise the screen stays blank
  tft.init(240, 240, SPI_MODE3);
  tft.setSPISpeed(40000000);
  tft.setRotation(0);
  tft.fillScreen(BG);
  canvas = new GFXcanvas16(W, max(POWER_H, max(WEATHER_H, GRAPH_H)));
  if (!canvas->getBuffer()) Serial.println("UI: canvas allocation failed");
  canvas->setTextWrap(false);
}

void uiMessage(const char *title, const char *detail) {
  tft.fillScreen(BG);
  tft.setTextWrap(false);
  tft.setTextColor(C_WHITE);
  tft.setFont(&FreeSansBold12pt7b);
  printCentered(tft, title, 112);
  tft.setTextColor(C_GREY);
  tft.setFont(&FreeSans9pt7b);
  printCentered(tft, detail, 140);
}

void uiRender(const Dashboard &d) {
  if (!canvas->getBuffer()) return;
  drawPower(d);
  drawWeather(d);
  drawGraph(d);
}

#include "ha_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#if !__has_include("secrets.h")
#error "Copy include/secrets.example.h to include/secrets.h and fill in your Wi-Fi and Home Assistant details"
#endif
#include "secrets.h"

static bool beginRequest(HTTPClient &http, const String &path) {
  http.useHTTP10(true);  // no chunked encoding, so the body can be streamed as-is
  http.setConnectTimeout(3000);
  http.setTimeout(10000);
  if (!http.begin(String(HA_BASE_URL) + path)) return false;
  http.addHeader("Authorization", "Bearer " HA_TOKEN);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HA %s -> %d\n", path.c_str(), code);
    http.end();
    return false;
  }
  return true;
}

bool haGetState(const char *entityId, EntityState &out) {
  HTTPClient http;
  if (!beginRequest(http, String("/api/states/") + entityId)) return false;

  JsonDocument filter;
  filter["state"] = true;
  filter["attributes"]["unit_of_measurement"] = true;
  filter["attributes"]["temperature"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("HA %s: JSON error %s\n", entityId, err.c_str());
    return false;
  }

  out.state = doc["state"] | "";
  out.unit = doc["attributes"]["unit_of_measurement"] | "";
  out.temperature = doc["attributes"]["temperature"] | NAN;
  return true;
}

// HA timestamps are UTC, e.g. "2026-10-14T10:00:00.123456+00:00".
static time_t parseIsoUtc(const char *s) {
  int y, mo, d, h, mi, sec;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
  // Days since 1970-01-01 (Howard Hinnant's days_from_civil)
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = y - era * 400;
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + sec;
}

static String isoUtcForUrl(time_t t) {
  struct tm tm;
  gmtime_r(&t, &tm);
  char buf[40];
  strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S%%2B00:00", &tm);  // "+00:00", URL-encoded
  return buf;
}

// Several hours of a meter that updates every second is easily 1 MB of JSON,
// far too big for a JsonDocument. The minimal_response format is a flat list
// of {"state": "...", "last_changed": "..."} objects, so a tiny scanner that
// tracks the last key and its string value is enough.
class HistoryScanner {
 public:
  // Constructor: just remembers the callback we were given. `onState` is any
  // function/lambda taking (timestamp, state text). It is stored as a
  // REFERENCE (see onState_ below), so the caller's callback must outlive
  // this scanner. `: onState_(onState)` is the member initializer list.
  explicit HistoryScanner(const std::function<void(time_t, const char *)> &onState)
      : onState_(onState) {}

  // Called once for every character of the HTTP response body, in order.
  // Works as a small state machine; it never stores more than one string.
  void feed(char c) {
    // Phase 1: inside a "quoted string" -> collect its characters in text_.
    if (inString_) {
      if (escape_) {
        escape_ = false;             // char after a backslash: take it as-is
      } else if (c == '\\') {
        escape_ = true;
      } else if (c == '"') {         // closing quote: string is complete
        inString_ = false;
        text_[len_] = '\0';
        stringPending_ = true;       // key or value? decided by the NEXT char
      } else if (len_ < sizeof text_ - 1) {
        text_[len_++] = c;           // extra chars beyond the buffer are dropped
      }
      return;
    }
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return;

    // Phase 2: a string just ended, and c is the first char after it.
    if (stringPending_) {
      stringPending_ = false;
      if (c == ':') {
        // "text": -> the string was a KEY, remember which one
        strlcpy(key_, text_, sizeof key_);
      } else if (strcmp(key_, "state") == 0) {
        // otherwise it was a VALUE of the current key
        strlcpy(state_, text_, sizeof state_);
        haveState_ = true;
      } else if (strcmp(key_, "last_changed") == 0 && haveState_) {
        // state + timestamp both known: hand the pair to the caller
        onState_(parseIsoUtc(text_), state_);
        haveState_ = false;
        count++;
      }
    }

    // Phase 3: an opening quote starts collecting the next string.
    if (c == '"') {
      inString_ = true;
      len_ = 0;
    }
  }

  size_t count = 0;  // number of state/timestamp pairs delivered so far

 private:
  // Reference, not a copy: points at the std::function owned by the caller
  // of haGetHistory(). Safe because the scanner lives only inside that call.
  const std::function<void(time_t, const char *)> &onState_;
  char text_[40];
  char key_[16] = "";
  char state_[24];
  size_t len_ = 0;
  bool inString_ = false, escape_ = false, stringPending_ = false, haveState_ = false;
};

bool haGetHistory(const char *entityId, time_t start, time_t end,
                  const std::function<void(time_t, const char *)> &onState) {
  String path = "/api/history/period/" + isoUtcForUrl(start) +
                "?filter_entity_id=" + entityId + "&end_time=" + isoUtcForUrl(end) +
                "&minimal_response&no_attributes";
  HTTPClient http;
  if (!beginRequest(http, path)) return false;

  // `onState` is our own parameter (a reference to the caller's callback);
  // passing it here hands that same reference on to the scanner's constructor.
  // Nothing is copied: scanner.onState_ ends up pointing at the caller's object.
  HistoryScanner scanner(onState);
  WiFiClient *stream = http.getStreamPtr();
  int remaining = http.getSize();  // -1 when the server sends no length
  uint8_t chunk[512];
  uint32_t lastData = millis();

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    size_t avail = stream->available();
    if (!avail) {
      if (millis() - lastData > 10000) break;
      delay(2);
      continue;
    }
    int n = stream->readBytes(chunk, avail < sizeof chunk ? avail : sizeof chunk);
    lastData = millis();
    if (remaining > 0) remaining -= n;
    for (int i = 0; i < n; i++) scanner.feed(chunk[i]);
  }
  http.end();

  Serial.printf("HA history %s: %u states\n", entityId, (unsigned)scanner.count);
  return true;
}

#include <Arduino.h>
#include <Adafruit_GC9A01A.h>
#include <Fonts/FreeSans9pt7b.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "hardware_config.h"
#include "clock_text.h"
#include "provisioning.h"
#include "tls_trust.h"
#include "https_dns_retry.h"
#if __has_include("rocky_typing.h")
#include "rocky_typing.h"
#define GAUGE_HAS_ROCKY 1
#else
// Public package: original code-drawn laptop, no external sprite required.
constexpr uint16_t PET_WIDTH = 96, PET_HEIGHT = 104;
#define GAUGE_HAS_ROCKY 0
#endif
#include "pet_motion.h"
#include "pet_visible_bounds.h"

#ifndef GAUGE_DEMO_MODE
#define GAUGE_DEMO_MODE 0
#endif

#if !GAUGE_DEMO_MODE
#if __has_include("config.h")
#include "config.h"
#endif
#if !defined(GAUGE_INITIAL_WIFI_SSID) && defined(GAUGE_WIFI_SSID)
#define GAUGE_INITIAL_WIFI_SSID GAUGE_WIFI_SSID
#endif
#if !defined(GAUGE_INITIAL_WIFI_PASSWORD) && defined(GAUGE_WIFI_PASSWORD)
#define GAUGE_INITIAL_WIFI_PASSWORD GAUGE_WIFI_PASSWORD
#endif
#if !defined(GAUGE_INITIAL_ENDPOINT_URL) && defined(GAUGE_ENDPOINT_URL)
#define GAUGE_INITIAL_ENDPOINT_URL GAUGE_ENDPOINT_URL
#endif
#ifndef GAUGE_INITIAL_WIFI_SSID
#define GAUGE_INITIAL_WIFI_SSID ""
#endif
#ifndef GAUGE_INITIAL_WIFI_PASSWORD
#define GAUGE_INITIAL_WIFI_PASSWORD ""
#endif
#ifndef GAUGE_INITIAL_ENDPOINT_URL
#define GAUGE_INITIAL_ENDPOINT_URL ""
#endif
#endif

namespace {

constexpr uint32_t FETCH_INTERVAL_MS = 300000;
constexpr uint32_t RETRY_INTERVAL_MS = 30000;
constexpr uint32_t DRAW_INTERVAL_MS = 1000;
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
constexpr size_t MAX_RESPONSE_BYTES = 4096;
constexpr uint32_t DEFAULT_STALE_SECONDS = 900;
constexpr uint32_t MAX_REASONABLE_SECONDS = 315576000;  // Ten years.

constexpr uint16_t COLOR_BG = 0x0862;
constexpr uint16_t COLOR_TRACK = 0x2104;
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_MUTED = 0x8C71;
constexpr uint16_t COLOR_OK = 0x2EF3;
constexpr uint16_t COLOR_WARN = 0xFD20;
constexpr uint16_t COLOR_BAD = 0xF9E7;

Adafruit_GC9A01A display(GAUGE_TFT_CS, GAUGE_TFT_DC, GAUGE_TFT_RST);

enum class FeedState : uint8_t { Ok, Missing, Stale };

struct LimitData {
  String id;
  String label;
  float remainingPercent = 0.0F;
  bool hasWindowMinutes = false;
  uint32_t windowMinutes = 0;
  bool hasResetCountdown = false;
  uint32_t resetSecondsAtReceipt = 0;
};

struct GaugeData {
  bool valid = false;
  bool sourceDemo = false;
  FeedState reportedState = FeedState::Missing;
  bool hasAge = false;
  uint32_t ageSecondsAtReceipt = 0;
  uint32_t staleAfterSeconds = DEFAULT_STALE_SECONDS;
  uint8_t limitCount = 0;
  LimitData limits[2];
  uint64_t receivedAtSeconds = 0;
};

GaugeData gauge;
uint32_t nextFetchAtMs = 0;
uint32_t lastDrawMs = 0;
bool requestInProgress = false;
bool lastFetchFailed = false;
bool fullRedrawNeeded = true;
bool wifiWasConnected = false;
bool timeSyncStarted = false;

enum class RenderMode : uint8_t { Unknown, Setup, NoData, Gauge };

struct TextWidgetCache {
  String text;
  int16_t x = 0, y = 0;
  uint16_t width = 0, height = 0, color = 0;
  uint8_t size = 0;
  const GFXfont *font = nullptr;
  bool valid = false;
};

constexpr uint8_t TEXT_WIDGET_COUNT = 12;
constexpr uint16_t TEXT_CANVAS_WIDTH = 192;
constexpr uint16_t TEXT_CANVAS_HEIGHT = 40;
constexpr uint16_t TEXT_CANVAS_STRIDE = (TEXT_CANVAS_WIDTH + 7) / 8;
GFXcanvas1 textCanvas(TEXT_CANVAS_WIDTH, TEXT_CANVAS_HEIGHT);
uint16_t textPixelRow[TEXT_CANVAS_WIDTH];
TextWidgetCache textWidgets[TEXT_WIDGET_COUNT];
RenderMode renderMode = RenderMode::Unknown;
float ringPercentCache[2] = {-1.0F, -1.0F};
uint16_t ringColorCache[2] = {0, 0};
bool ringCacheValid[2] = {false, false};
uint8_t renderedLimitCount = 0;
uint16_t petFrameBuffer[PET_WIDTH * PET_HEIGHT];
uint32_t petMotionStartedMs = 0;
uint32_t petMotionDurationMs = 0;
uint8_t petPlanStep = 0;
uint8_t petFrame = 0;
bool petFrameValid = false;
uint16_t dotCacheColor = 0;
bool dotCacheValid = false;
#if !GAUGE_DEMO_MODE
ProvisioningManager provisioning;
uint32_t setupButtonPressedAtMs = 0;
bool setupButtonTriggered = false;
bool portalWasActive = false;
#endif

uint64_t monotonicSeconds() {
  static uint32_t previousMs = millis();
  static uint64_t accumulatedMs = 0;
  const uint32_t nowMs = millis();
  accumulatedMs += static_cast<uint32_t>(nowMs - previousMs);
  previousMs = nowMs;
  return accumulatedMs / 1000ULL;
}

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t saturatingAdd(uint32_t a, uint32_t b) {
  return (UINT32_MAX - a < b) ? UINT32_MAX : a + b;
}

bool isBoundedText(const JsonVariantConst value, size_t maxLength) {
  if (!value.is<const char *>()) return false;
  const char *text = value.as<const char *>();
  return text != nullptr && text[0] != '\0' && strlen(text) <= maxLength;
}

bool readNullableUint(const JsonVariantConst value, bool &present, uint32_t &out,
                      uint32_t maximum, bool allowZero = true) {
  if (value.isNull()) {
    present = false;
    out = 0;
    return true;
  }
  if (!value.is<uint32_t>()) return false;
  const uint32_t parsed = value.as<uint32_t>();
  if ((!allowZero && parsed == 0) || parsed > maximum) return false;
  present = true;
  out = parsed;
  return true;
}

bool parsePayload(const String &body, GaugeData &parsed) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error || !doc.is<JsonObjectConst>()) return false;
  const JsonObjectConst root = doc.as<JsonObjectConst>();

  // v1 is additive: unknown optional fields are ignored, while every core field
  // below remains mandatory and strictly type/range checked.

  if (!root["schema_version"].is<uint8_t>() || root["schema_version"].as<uint8_t>() != 1) {
    return false;
  }
  if (!root["source"].is<const char *>() || !root["state"].is<const char *>()) return false;
  const char *source = root["source"].as<const char *>();
  const char *state = root["state"].as<const char *>();
  if (strcmp(source, "codex") != 0 && strcmp(source, "demo") != 0) return false;
  if (strcmp(state, "ok") != 0 && strcmp(state, "missing") != 0 && strcmp(state, "stale") != 0) {
    return false;
  }
  if (!root["stale_after_seconds"].is<uint32_t>()) return false;
  const uint32_t staleAfter = root["stale_after_seconds"].as<uint32_t>();
  if (staleAfter < 60 || staleAfter > 86400) return false;
  if (!root["server_time"].is<uint32_t>()) return false;
  const uint32_t serverTime = root["server_time"].as<uint32_t>();
  if (serverTime == 0 || serverTime > 4102444800UL) return false;
  if (root.containsKey("updated_at") && !root["updated_at"].isNull()) {
    if (!root["updated_at"].is<uint32_t>() || root["updated_at"].as<uint32_t>() > 4102444800UL) {
      return false;
    }
  }
  if (root.containsKey("error") && !root["error"].isNull()) {
    if (!root["error"].is<const char *>() ||
        strcmp(root["error"].as<const char *>(), "source_unavailable") != 0) {
      return false;
    }
  }

  bool hasAge = false;
  uint32_t age = 0;
  if (!root.containsKey("age_seconds")) return false;
  if (!readNullableUint(root["age_seconds"], hasAge, age, MAX_REASONABLE_SECONDS)) return false;
  if (strcmp(state, "ok") == 0 && !hasAge) return false;

  if (!root["limits"].is<JsonArrayConst>()) return false;
  const JsonArrayConst limits = root["limits"].as<JsonArrayConst>();
  if (limits.size() > 2) return false;
  if (strcmp(state, "ok") == 0 && limits.size() == 0) return false;

  parsed = GaugeData{};
  parsed.valid = true;
  parsed.sourceDemo = strcmp(source, "demo") == 0;
  parsed.reportedState = strcmp(state, "ok") == 0
                             ? FeedState::Ok
                             : (strcmp(state, "stale") == 0 ? FeedState::Stale : FeedState::Missing);
  parsed.hasAge = hasAge;
  parsed.ageSecondsAtReceipt = age;
  parsed.staleAfterSeconds = staleAfter;
  parsed.limitCount = limits.size();
  parsed.receivedAtSeconds = monotonicSeconds();

  uint8_t index = 0;
  for (JsonVariantConst itemValue : limits) {
    if (!itemValue.is<JsonObjectConst>()) return false;
    const JsonObjectConst item = itemValue.as<JsonObjectConst>();
    if (item.size() != 6 || !item.containsKey("id") || !item.containsKey("label") ||
        !item.containsKey("remaining_percent") || !item.containsKey("window_minutes") ||
        !item.containsKey("resets_at") || !item.containsKey("reset_in_seconds")) {
      return false;
    }
    if (!isBoundedText(item["id"], 32) || !isBoundedText(item["label"], 28)) return false;
    if (!item["remaining_percent"].is<float>()) return false;
    const float remaining = item["remaining_percent"].as<float>();
    if (!isfinite(remaining) || remaining < 0.0F || remaining > 100.0F) return false;

    bool hasWindow = false;
    uint32_t window = 0;
    if (!readNullableUint(item["window_minutes"], hasWindow, window, 525960, false)) return false;
    bool hasResetsAt = false;
    uint32_t resetsAt = 0;
    if (!readNullableUint(item["resets_at"], hasResetsAt, resetsAt, 4102444800UL, false)) return false;
    bool hasResetIn = false;
    uint32_t resetIn = 0;
    if (!readNullableUint(item["reset_in_seconds"], hasResetIn, resetIn,
                          MAX_REASONABLE_SECONDS)) {
      return false;
    }
    LimitData &limit = parsed.limits[index++];
    limit.id = item["id"].as<const char *>();
    limit.label = item["label"].as<const char *>();
    limit.remainingPercent = remaining;
    limit.hasWindowMinutes = hasWindow;
    limit.windowMinutes = window;
    limit.hasResetCountdown = hasResetIn || hasResetsAt;
    limit.resetSecondsAtReceipt = hasResetIn
                                      ? resetIn
                                      : (hasResetsAt && resetsAt > serverTime ? resetsAt - serverTime : 0);
  }
  return true;
}

#if !GAUGE_DEMO_MODE
String readResponse(HTTPClient &http, bool &complete) {
  complete = false;
  const int length = http.getSize();
  if (length > static_cast<int>(MAX_RESPONSE_BYTES)) return String();
  WiFiClient *stream = http.getStreamPtr();
  String body;
  body.reserve(length > 0 ? static_cast<size_t>(length) : 512U);
  const uint32_t startedAt = millis();
  while (http.connected() && (length < 0 || body.length() < static_cast<size_t>(length))) {
    const size_t available = stream->available();
    if (available > 0) {
      const size_t room = MAX_RESPONSE_BYTES - body.length();
      if (room == 0) return String();
      const size_t take = min(available, room);
      char buffer[257];
      const size_t chunk = min(take, sizeof(buffer) - 1);
      const int got = stream->readBytes(buffer, chunk);
      if (got > 0) body.concat(buffer, static_cast<unsigned int>(got));
    } else {
      delay(1);
    }
    if (millis() - startedAt > HTTP_TIMEOUT_MS) return String();
  }
  complete = length < 0 ? !http.connected() : body.length() == static_cast<size_t>(length);
  return body;
}

bool fetchGauge(const GaugeNetworkConfig &config, bool resolverRetry = false) {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  const bool secure = config.secureEndpoint();
  if (secure) {
    if (config.deviceToken.isEmpty() || time(nullptr) < 1700000000) {
      Serial.println("[fetch] secure_precondition_missing");
      return false;
    }
    secureClient.setCACert(GAUGE_ISRG_ROOT_X1);
    secureClient.setHandshakeTimeout(10);
    if (!http.begin(secureClient, config.endpoint)) return false;
  } else {
    if (!http.begin(plainClient, config.endpoint)) return false;
  }
  http.addHeader("Accept", "application/json");
  if (secure) http.addHeader("Authorization", String("Bearer ") + config.deviceToken);
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[fetch] status=%d\n", status);
    http.end();
    if (secure && status < 0 && !resolverRetry && WiFi.status() == WL_CONNECTED) {
      HttpsDnsRetry retry;
      if (retry.active()) {
        Serial.println("[fetch] public_dns_retry");
        return fetchGauge(config, true);
      }
    }
    return false;
  }
  bool complete = false;
  const String body = readResponse(http, complete);
  http.end();
  if (!complete || body.isEmpty()) return false;
  GaugeData parsed;
  if (!parsePayload(body, parsed)) return false;
  gauge = parsed;
  Serial.println("[fetch] data_received");
  return true;
}
#endif

void installDemoData() {
  gauge = GaugeData{};
  gauge.valid = true;
  gauge.sourceDemo = true;
  gauge.reportedState = FeedState::Ok;
  gauge.hasAge = true;
  gauge.ageSecondsAtReceipt = 3;
  gauge.limitCount = 2;
  gauge.receivedAtSeconds = monotonicSeconds();
  gauge.limits[0].id = "five-hour";
  gauge.limits[0].label = "5 HOUR";
  gauge.limits[0].remainingPercent = 72.0F;
  gauge.limits[0].hasWindowMinutes = true;
  gauge.limits[0].windowMinutes = 300;
  gauge.limits[0].hasResetCountdown = true;
  gauge.limits[0].resetSecondsAtReceipt = 2U * 3600U + 41U * 60U;
  gauge.limits[1].id = "weekly";
  gauge.limits[1].label = "WEEKLY";
  gauge.limits[1].remainingPercent = 38.0F;
  gauge.limits[1].hasWindowMinutes = true;
  gauge.limits[1].windowMinutes = 10080;
  gauge.limits[1].hasResetCountdown = true;
  gauge.limits[1].resetSecondsAtReceipt = 3U * 86400U + 7U * 3600U;
}

uint32_t currentAgeSeconds(uint32_t now) {
  if (!gauge.valid || !gauge.hasAge) return 0;
  const uint64_t elapsed = monotonicSeconds() - gauge.receivedAtSeconds;
  return saturatingAdd(gauge.ageSecondsAtReceipt,
                       elapsed > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(elapsed));
}

uint32_t currentResetSeconds(const LimitData &limit, uint32_t now);

bool currentlyStale(uint32_t now) {
  if (!gauge.valid || gauge.reportedState == FeedState::Stale) return true;
  for (uint8_t i = 0; i < gauge.limitCount; ++i) {
    if (gauge.limits[i].hasResetCountdown && currentResetSeconds(gauge.limits[i], now) == 0) return true;
  }
  return gauge.hasAge && currentAgeSeconds(now) >= gauge.staleAfterSeconds;
}

uint32_t currentResetSeconds(const LimitData &limit, uint32_t now) {
  if (!limit.hasResetCountdown) return 0;
  const uint64_t elapsed64 = monotonicSeconds() - gauge.receivedAtSeconds;
  const uint32_t elapsed = elapsed64 > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(elapsed64);
  return elapsed >= limit.resetSecondsAtReceipt ? 0 : limit.resetSecondsAtReceipt - elapsed;
}

String countdownText(const LimitData &limit, uint32_t now) {
  if (!limit.hasResetCountdown) return "RESET --";
  uint32_t seconds = currentResetSeconds(limit, now);
  const uint32_t days = seconds / 86400U;
  seconds %= 86400U;
  const uint32_t hours = seconds / 3600U;
  const uint32_t minutes = (seconds % 3600U) / 60U;
  char text[24];
  if (days > 0) {
    snprintf(text, sizeof(text), "RESET %lud %02lu:%02lu", static_cast<unsigned long>(days),
             static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes));
  } else {
    snprintf(text, sizeof(text), "RESET %02lu:%02lu", static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  }
  return String(text);
}

uint16_t percentColor(float percent) {
  if (percent < 20.0F) return COLOR_BAD;
  if (percent < 45.0F) return COLOR_WARN;
  return COLOR_OK;
}

void invalidateRenderCaches() {
  for (TextWidgetCache &widget : textWidgets) widget.valid = false;
  ringCacheValid[0] = ringCacheValid[1] = false;
  dotCacheValid = false;
  petFrameValid = false;
}

void enterRenderMode(RenderMode mode) {
  if (renderMode == mode) return;
  renderMode = mode;
  display.fillScreen(COLOR_BG);
  invalidateRenderCaches();
}

void renderTextWidget(uint8_t index, int16_t x, int16_t y, uint16_t width,
                      uint16_t height, const String &text, uint8_t size, uint16_t color, const GFXfont *font = nullptr) {
  if (index >= TEXT_WIDGET_COUNT || width == 0 || height == 0 ||
      width > TEXT_CANVAS_WIDTH || height > TEXT_CANVAS_HEIGHT) return;
  TextWidgetCache &cache = textWidgets[index];
  if (cache.valid && cache.text == text && cache.x == x && cache.y == y &&
      cache.width == width && cache.height == height && cache.size == size &&
      cache.color == color && cache.font == font) return;

  uint8_t *mask = textCanvas.getBuffer();
  if (mask != nullptr) {
    textCanvas.fillScreen(0);
    textCanvas.setTextWrap(false);
    textCanvas.setFont(font);
    textCanvas.setTextSize(size);
    textCanvas.setTextColor(1);
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    textCanvas.getTextBounds(text, 0, 0, &x1, &y1, &textWidth, &textHeight);
    const int16_t cursorX = (static_cast<int16_t>(width) - static_cast<int16_t>(textWidth)) / 2 - x1;
    const int16_t cursorY = (static_cast<int16_t>(height) - static_cast<int16_t>(textHeight)) / 2 - y1;
    textCanvas.setCursor(cursorX, cursorY);
    textCanvas.print(text);

    display.startWrite();
    display.setAddrWindow(x, y, width, height);
    for (uint16_t row = 0; row < height; ++row) {
      const uint8_t *source = mask + row * TEXT_CANVAS_STRIDE;
      for (uint16_t column = 0; column < width; ++column) {
        const bool set = source[column >> 3] & (0x80U >> (column & 7U));
        textPixelRow[column] = set ? color : COLOR_BG;
      }
      display.writePixels(textPixelRow, width, true, false);
    }
    display.endWrite();
  } else {
    // Allocation failure fallback: still bounded to the widget, never clear the screen.
    display.fillRect(x, y, width, height, COLOR_BG);
    display.setTextWrap(false);
    display.setFont(font);
    display.setTextSize(size);
    display.setTextColor(color, COLOR_BG);
    int16_t x1, y1;
    uint16_t textWidth, textHeight;
    display.getTextBounds(text, 0, 0, &x1, &y1, &textWidth, &textHeight);
    display.setCursor(x + (static_cast<int16_t>(width) - static_cast<int16_t>(textWidth)) / 2 - x1,
                      y + (static_cast<int16_t>(height) - static_cast<int16_t>(textHeight)) / 2 - y1);
    display.print(text);
  }

  cache.text = text;
  cache.x = x;
  cache.y = y;
  cache.width = width;
  cache.height = height;
  cache.size = size;
  cache.color = color;
  cache.font = font;
  cache.valid = true;
}

// Blend native RGB565 channels; only a 192-pixel scanline is needed.
uint16_t blend565(uint16_t background, uint16_t foreground, float coverage) {
  const uint16_t alpha = static_cast<uint16_t>(constrain(coverage, 0.0F, 1.0F) * 256.0F + 0.5F);
  const uint16_t inverse = 256 - alpha;
  const uint16_t r = (((background >> 11) * inverse + (foreground >> 11) * alpha + 128) >> 8);
  const uint16_t g = ((((background >> 5) & 63) * inverse + ((foreground >> 5) & 63) * alpha + 128) >> 8);
  const uint16_t b = (((background & 31) * inverse + (foreground & 31) * alpha + 128) >> 8);
  return (r << 11) | (g << 5) | b;
}

// Analytic circular edges with one-pixel coverage, including the progress endpoint.
// Only annulus spans are transferred: the centre and its text stay untouched.
void renderRing(float percent, uint16_t activeColor, uint8_t ring = 0) {
  if (ring > 1) return;
  const float bounded = constrain(percent, 0.0F, 100.0F);
  if (ringCacheValid[ring] && ringPercentCache[ring] == bounded &&
      ringColorCache[ring] == activeColor) return;
  const float outer = ring == 0 ? 113.0F : 100.0F;
  const float inner = outer - 8.0F;
  const float minSquared = (inner - 0.5F) * (inner - 0.5F);
  const float maxSquared = (outer + 0.5F) * (outer + 0.5F);
  const float sweep = 1.5F * PI;
  const float progressSweep = sweep * bounded / 100.0F;
  const int16_t extent = static_cast<int16_t>(outer) + 1;
  for (int16_t dy = -extent; dy <= extent; ++dy) {
    uint16_t length = 0;
    int16_t spanStart = 0;
    for (int16_t dx = -extent; dx <= extent + 1; ++dx) {
      const float squared = static_cast<float>(dx) * dx + static_cast<float>(dy) * dy;
      const bool inBand = dx <= extent && squared >= minSquared && squared <= maxSquared;
      if (length && (!inBand || length == TEXT_CANVAS_WIDTH)) {
        display.drawRGBBitmap(120 + spanStart, 120 + dy, textPixelRow, length, 1);
        length = 0;
      }
      if (!inBand) continue;
      if (!length) spanStart = dx;
      const float distance = sqrtf(squared);
      float angle = atan2f(dy, dx) - 0.75F * PI;
      if (angle < -0.02F) angle += 2.0F * PI;
      const float radialCoverage = constrain(fminf(outer + 0.5F - distance,
                                                       distance - inner + 0.5F), 0.0F, 1.0F);
      const float arcCoverage = constrain(fminf(angle * distance + 0.5F,
                                                    (sweep - angle) * distance + 0.5F), 0.0F, 1.0F);
      const float activeCoverage = bounded <= 0.0F ? 0.0F :
          constrain((progressSweep - angle) * distance + 0.5F, 0.0F, 1.0F);
      const uint16_t arcColor = blend565(COLOR_TRACK, activeColor, activeCoverage);
      textPixelRow[length++] = blend565(COLOR_BG, arcColor, radialCoverage * arcCoverage);
    }
    if (length) display.drawRGBBitmap(120 + spanStart, 120 + dy, textPixelRow, length, 1);
    yield();
  }
  ringPercentCache[ring] = bounded;
  ringColorCache[ring] = activeColor;
  ringCacheValid[ring] = true;
}

void renderStatusDot(uint16_t color) {
  if (dotCacheValid && dotCacheColor == color) return;
  display.fillCircle(gauge.limitCount == 1 ? 82 : 120, gauge.limitCount == 1 ? 231 : 207, 2, color);
  dotCacheColor = color;
  dotCacheValid = true;
}

String compactAge(uint32_t seconds) {
  if (seconds < 60) return "<1m";
  const uint32_t minutes = seconds / 60U;
  if (minutes < 60) return String(minutes) + "m";
  const uint32_t hours = minutes / 60U;
  const uint32_t remainingMinutes = minutes % 60U;
  char text[16];
  snprintf(text, sizeof(text), "%luh%02lum", static_cast<unsigned long>(hours),
           static_cast<unsigned long>(remainingMinutes));
  return String(text);
}

void animatePet(uint32_t now) {
  if (renderMode != RenderMode::Gauge || gauge.limitCount != 1) return;
  if (!petFrameValid) {
    petPlanStep = 0;
    petMotionStartedMs = now;
    petMotionDurationMs = petMotionDuration(PET_PLAN[petPlanStep], esp_random());
  } else if (now - petMotionStartedMs >= petMotionDurationMs) {
    petPlanStep = (petPlanStep + 1) % PET_PLAN_LENGTH;
    petMotionStartedMs = now;
    petMotionDurationMs = petMotionDuration(PET_PLAN[petPlanStep], esp_random());
  }
  const uint8_t next = petMotionFrame(PET_PLAN[petPlanStep], now - petMotionStartedMs);
  if (petFrameValid && next == petFrame) return;
  petFrame = next;
  // Centre actual opaque content, not the transparent cell around it.
#if GAUGE_HAS_ROCKY
  const uint8_t sourceTop = PET_VISIBLE_TOP[petFrame];
  const uint8_t visibleHeight = PET_VISIBLE_HEIGHT[petFrame];
  const uint8_t destinationTop = (PET_DRAW_HEIGHT - visibleHeight) / 2;
  for (uint16_t i = 0; i < PET_WIDTH * PET_DRAW_HEIGHT; ++i) petFrameBuffer[i] = COLOR_BG;
  for (uint8_t row = 0; row < visibleHeight; ++row) {
    for (uint16_t x = 0; x < PET_WIDTH; ++x) {
      const uint16_t source = (sourceTop + row) * PET_WIDTH + x;
      const uint8_t paletteIndex = pgm_read_byte(&PET_PIXELS[petFrame][source]);
      petFrameBuffer[(destinationTop + row) * PET_WIDTH + x] = pgm_read_word(&PET_PALETTE[paletteIndex]);
    }
  }
#else
  for (uint16_t i = 0; i < PET_WIDTH * PET_DRAW_HEIGHT; ++i) petFrameBuffer[i] = COLOR_BG;
  auto rect = [](int x, int y, int w, int h, uint16_t color) {
    for (int row = y; row < y + h; ++row)
      for (int col = x; col < x + w; ++col)
        petFrameBuffer[row * PET_WIDTH + col] = color;
  };
  rect(10, 18, 76, 52, COLOR_MUTED);
  rect(13, 21, 70, 46, COLOR_BG);
  rect(5, 72, 86, 7, COLOR_MUTED);
  rect(36, 73, 24, 3, COLOR_BG);
  // Small terminal lines and a slowly changing cursor.
  rect(21, 32, 8, 3, COLOR_OK);
  rect(28, 36, 5, 3, COLOR_OK);
  rect(21, 40, 8, 3, COLOR_OK);
  rect(39, 40, 18, 3, COLOR_OK);
  if (petFrame % 2 == 0) rect(60, 40, 7, 3, COLOR_OK);
#endif
  display.drawRGBBitmap(72, 104, petFrameBuffer, PET_WIDTH, PET_DRAW_HEIGHT);
  petFrameValid = true;
}

void drawScreen(uint32_t now, bool reevaluate) {
  (void)reevaluate;
  const bool online = GAUGE_DEMO_MODE ? true : WiFi.status() == WL_CONNECTED;
  const bool noData = !gauge.valid || gauge.reportedState == FeedState::Missing || gauge.limitCount == 0;
  if (noData) {
    enterRenderMode(RenderMode::NoData);
    renderRing(0.0F, COLOR_MUTED);
    renderTextWidget(0, 90, 32, 60, 8,
                     (GAUGE_DEMO_MODE || gauge.sourceDemo) ? "DEMO" : "", 1, COLOR_MUTED);
    renderTextWidget(2, 45, 92, 150, 24, "NO DATA", 3, COLOR_MUTED);
    const String state = GAUGE_DEMO_MODE
                             ? "DEMO"
                             : (!online ? "OFFLINE" : (lastFetchFailed ? "CONNECTION LOST" : "MISSING"));
    renderTextWidget(3, 45, 133, 150, 16, state, state.length() > 12 ? 1 : 2,
                     GAUGE_DEMO_MODE ? COLOR_MUTED : (online && !lastFetchFailed ? COLOR_WARN : COLOR_BAD));
    const bool showHint = !GAUGE_DEMO_MODE && (!online || lastFetchFailed);
    renderTextWidget(5, 60, 174, 120, 8, showHint ? "BOOT 3s: SETUP" : "", 1, COLOR_MUTED);
    char clockText[13];
    formatClockDate(time(nullptr), clockText, sizeof(clockText));
    renderTextWidget(11, 84, 61, 72, 8, clockText, 1, COLOR_MUTED);
    return;
  }

  // A one/two-limit layout change clears once; routine polling never clears.
  if (renderMode == RenderMode::Gauge && renderedLimitCount != gauge.limitCount)
    renderMode = RenderMode::Unknown;
  enterRenderMode(RenderMode::Gauge);
  renderedLimitCount = gauge.limitCount;
  const bool dual = gauge.limitCount > 1;
  char clockText[13];
  const bool clockValid = formatClockDate(time(nullptr), clockText, sizeof(clockText));
  if (dual) {
    renderTextWidget(0, 78, 45, 84, 8, clockText, 1, clockValid ? COLOR_TEXT : COLOR_MUTED);
  } else {
    renderTextWidget(0, 60, 36, 120, 20, clockText, 1,
                     clockValid ? COLOR_TEXT : COLOR_MUTED, &FreeSans9pt7b);
  }
  renderTextWidget(10, dual ? 96 : 180, dual ? 57 : 120, dual ? 48 : 36, 8,
                   (GAUGE_DEMO_MODE || gauge.sourceDemo) ? "DEMO" : "", 1, COLOR_WARN);
  for (uint8_t i = 0; i < gauge.limitCount; ++i) {
    const LimitData &limit = gauge.limits[i];
    const uint16_t color = percentColor(limit.remainingPercent);
    renderRing(limit.remainingPercent, color, i);
    String label;
    if (limit.hasWindowMinutes && limit.windowMinutes >= 10080) label = "WOCHE";
    else if (limit.hasWindowMinutes && limit.windowMinutes == 300) label = "5 STUNDEN";
    else { label = limit.label; if (label.length() > 16) label.remove(16); }
    // Explicit ring mapping remains unambiguous even when both have the same color.
    if (dual) label = String(i == 0 ? "AUSSEN: " : "INNEN: ") + label;
    const uint8_t base = 1 + i * 3;
    const int16_t labelY = i == 0 ? (dual ? 69 : 37) : 145;
    const int16_t valueY = i == 0 ? (dual ? 84 : 60) : 157;
    const int16_t resetY = i == 0 ? (dual ? 121 : 209) : 185;
    if (dual) renderTextWidget(base, 42, labelY, 156, 8, label, 1, COLOR_MUTED);
    char percent[12];
    snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(lroundf(limit.remainingPercent)));
    renderTextWidget(base + 1, i == 0 ? 48 : 72, valueY, i == 0 ? 144 : 96, i == 0 ? (dual ? 32 : 40) : 24, percent, i == 0 ? (dual ? 4 : 5) : 3, color);
    renderTextWidget(base + 2, 66, resetY, 108, 8, countdownText(limit, now), 1, COLOR_MUTED);
  }
  const String age = gauge.hasAge ? compactAge(currentAgeSeconds(now)) : String();
  String footer;
  uint16_t footerColor = COLOR_MUTED;
  if (!online) {
    footer = gauge.hasAge ? String("OFFLINE ") + age : "OFFLINE";
    footerColor = COLOR_BAD;
  } else if (lastFetchFailed) {
    footer = gauge.hasAge ? String("LOST ") + age : "CONNECTION LOST";
    footerColor = COLOR_BAD;
  } else if (currentlyStale(now)) {
    footer = gauge.hasAge ? String("STALE ") + age : "STALE";
    footerColor = COLOR_WARN;
  } else if (gauge.hasAge) {
    footer = String("UPDATED ") + age;
  } else {
    footer = "READY";
  }
  renderTextWidget(8, 63, dual ? 216 : 228, 114, 8, footer, 1, footerColor);
  const bool showHint = !GAUGE_DEMO_MODE && (!online || lastFetchFailed);
  renderTextWidget(9, 72, dual ? 198 : 217, 96, 8, showHint ? "BOOT 3s: SETUP" : "", 1, COLOR_MUTED);
  animatePet(now);
  renderStatusDot(GAUGE_DEMO_MODE ? COLOR_MUTED : (online ? COLOR_OK : COLOR_BAD));
}

#if !GAUGE_DEMO_MODE
void drawProvisioningScreen() {
  enterRenderMode(RenderMode::Setup);
  renderTextWidget(0, 60, 26, 120, 16, "WLAN SETUP", 2, COLOR_WARN);
  renderTextWidget(1, 60, 54, 120, 8, provisioning.statusText(), 1, COLOR_TEXT);
  renderTextWidget(2, 90, 74, 60, 8, "SSID", 1, COLOR_MUTED);
  renderTextWidget(3, 36, 87, 168, 8, provisioning.apSsid(), 1, COLOR_TEXT);
  renderTextWidget(4, 90, 111, 60, 8, "PASSWORT", 1, COLOR_MUTED);
  renderTextWidget(5, 24, 127, 192, 32, provisioning.apPassword(), 4, COLOR_TEXT);
  renderTextWidget(6, 48, 174, 144, 16, "192.168.4.1", 2, COLOR_OK);
  renderTextWidget(7, 60, 203, 120, 8, "MIT HANDY VERBINDEN", 1, COLOR_MUTED);
}

void serviceSetupButton(uint32_t now) {
  if (digitalRead(GAUGE_SETUP_BUTTON) == LOW) {
    if (setupButtonPressedAtMs == 0) setupButtonPressedAtMs = now == 0 ? 1 : now;
    if (!setupButtonTriggered && now - setupButtonPressedAtMs >= 3000) {
      setupButtonTriggered = true;
      provisioning.requestPortal(now);
    }
  } else {
    setupButtonPressedAtMs = 0;
    setupButtonTriggered = false;
  }
}
#endif

void serviceWifi(uint32_t now) {
#if !GAUGE_DEMO_MODE
  provisioning.loop(now);
  if (provisioning.consumeConfigChanged()) {
    gauge = GaugeData{};
    lastFetchFailed = false;
    nextFetchAtMs = now;
    fullRedrawNeeded = true;
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiWasConnected) {
    wifiWasConnected = connected;
    fullRedrawNeeded = true;
    if (connected) {
      nextFetchAtMs = now;
      if (!timeSyncStarted) {
        configTzTime(GAUGE_TIMEZONE, "pool.ntp.org", "time.cloudflare.com");
        timeSyncStarted = true;
      }
    }
  }
#else
  (void)now;
#endif
}

void serviceFetch(uint32_t now) {
#if !GAUGE_DEMO_MODE
  if (provisioning.pauseFetch() || !provisioning.configured() || WiFi.status() != WL_CONNECTED ||
      requestInProgress || !due(now, nextFetchAtMs)) return;
  requestInProgress = true;
  const bool ok = fetchGauge(provisioning.config());
  lastFetchFailed = !ok;
  fullRedrawNeeded = true;
  requestInProgress = false;
  nextFetchAtMs = millis() + (ok ? FETCH_INTERVAL_MS : RETRY_INTERVAL_MS);
#else
  (void)now;
#endif
}

}  // namespace

void setup() {
  setenv("TZ", GAUGE_TIMEZONE, 1);
  tzset();
  Serial.begin(115200);
  SPI.begin(GAUGE_TFT_SCK, -1, GAUGE_TFT_MOSI, GAUGE_TFT_CS);
  display.begin(GAUGE_SPI_HZ);
  display.setRotation(GAUGE_TFT_ROTATION);

#if GAUGE_DEMO_MODE
  installDemoData();
#else
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setMinSecurity(WIFI_AUTH_OPEN);
  pinMode(GAUGE_SETUP_BUTTON, INPUT_PULLUP);
  provisioning.begin(GAUGE_INITIAL_WIFI_SSID, GAUGE_INITIAL_WIFI_PASSWORD,
                     GAUGE_INITIAL_ENDPOINT_URL, millis());
  portalWasActive = provisioning.portalActive();
#endif
#if !GAUGE_DEMO_MODE
  if (provisioning.portalActive()) drawProvisioningScreen();
  else drawScreen(millis(), true);
#else
  drawScreen(millis(), true);
#endif
  fullRedrawNeeded = false;
}

void loop() {
  const uint32_t now = millis();
  monotonicSeconds();
#if !GAUGE_DEMO_MODE
  serviceSetupButton(now);
#endif
  serviceWifi(now);
  serviceFetch(now);
#if !GAUGE_DEMO_MODE
  const bool portalActive = provisioning.portalActive();
  if (portalActive != portalWasActive) {
    portalWasActive = portalActive;
    fullRedrawNeeded = true;
  }
  if (portalActive) {
    if (provisioning.consumeDisplayDirty() || fullRedrawNeeded) {
      drawProvisioningScreen();
      fullRedrawNeeded = false;
    }
    delay(2);
    return;
  }
#endif
  if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = now;
    drawScreen(now, fullRedrawNeeded);
    fullRedrawNeeded = false;
  }
  animatePet(now);
  delay(2);
}

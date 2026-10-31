#include "provisioning.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_wifi.h>

#include "provisioning_validation.h"
#include "reliable_wifi_scan.h"

namespace {
constexpr uint32_t AUTO_PORTAL_DELAY_MS = 45000;
constexpr uint32_t PORTAL_TIMEOUT_MS = 300000;
constexpr uint32_t PORTAL_COOLDOWN_MS = 600000;
constexpr uint32_t CANDIDATE_TIMEOUT_MS = 30000;
constexpr uint32_t RECONNECT_INTERVAL_MS = 10000;
constexpr uint16_t DNS_PORT = 53;
constexpr size_t MAX_FORM_BYTES = 768;
constexpr uint8_t MAX_SCAN_RESULTS = 16;
constexpr uint32_t SCAN_RATE_LIMIT_MS = 5000;

DNSServer dnsServer;
WebServer webServer(80);
ReliableWifiScan radioScan;
bool setupPasswordReused = false;

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

String randomHex(size_t characters) {
  static const char alphabet[] = "0123456789abcdef";
  String result;
  result.reserve(characters);
  uint32_t random = 0;
  for (size_t i = 0; i < characters; ++i) {
    if ((i & 7U) == 0) random = esp_random();
    result += alphabet[random & 0x0fU];
    random >>= 4U;
  }
  return result;
}

String randomSetupPassword() {
  // Eight WPA2-compatible characters; omit 0/O and 1/I for a readable display.
  static constexpr char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  static_assert(sizeof(alphabet) - 1 == 32, "Setup alphabet must contain 32 symbols");
  String password;
  password.reserve(8);
  for (size_t i = 0; i < 8; ++i) password += alphabet[esp_random() & 31U];
  return password;
}

String persistentSetupPassword() {
  Preferences preferences;
  if (!preferences.begin("gauge-setup", false)) return String();
  String password = preferences.getString("ap-pass", "");
  if (password.length() == 8 &&
      gauge_validation::validWifiPassword(password.c_str(), password.length())) {
    setupPasswordReused = true;
    preferences.end();
    return password;
  }
  password = randomSetupPassword();
  const bool saved = preferences.putString("ap-pass", password) == password.length();
  preferences.end();
  setupPasswordReused = false;
  return saved ? password : String(); // Never open an unprotected AP on a storage error.
}

String htmlEscape(const String &input) {
  String escaped;
  escaped.reserve(input.length() + 16);
  for (size_t i = 0; i < input.length(); ++i) {
    switch (input[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += input[i];
    }
  }
  return escaped;
}

String jsonEscape(const String &input) {
  String escaped;
  escaped.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    if (c == '"' || c == '\\') {
      escaped += '\\';
      escaped += static_cast<char>(c);
    } else if (c >= 0x20) {
      escaped += static_cast<char>(c);
    }
  }
  return escaped;
}

bool enterpriseAuth(uint8_t authMode) {
  return authMode == static_cast<uint8_t>(WIFI_AUTH_WPA2_ENTERPRISE);
}

const char *securityLabel(uint8_t authMode) {
  if (authMode == static_cast<uint8_t>(WIFI_AUTH_OPEN)) return "offen";
  if (enterpriseAuth(authMode)) return "Enterprise – nicht unterstützt";
  return "geschützt";
}

bool loadPersisted(GaugeNetworkConfig &config) {
  Preferences preferences;
  if (!preferences.begin("codex-gauge", true)) return false;
  const uint8_t slot = preferences.getUChar("slot", 0) & 1U;
  const char prefix = slot == 0 ? 'a' : 'b';
  const String ssidKey = String(prefix) + "_ssid";
  const String passKey = String(prefix) + "_pass";
  const String urlKey = String(prefix) + "_url";
  const String tokenKey = String(prefix) + "_token";
  config.ssid = preferences.getString(ssidKey.c_str(), "");
  config.password = preferences.getString(passKey.c_str(), "");
  if (config.password.startsWith("v1:")) config.password.remove(0, 3);
  config.endpoint = preferences.getString(urlKey.c_str(), "");
  config.deviceToken = preferences.getString(tokenKey.c_str(), "");
  if (config.deviceToken.startsWith("v1:")) config.deviceToken.remove(0, 3);
  preferences.end();
  return config.complete() && gauge_validation::validSsid(config.ssid.c_str(), config.ssid.length()) &&
         gauge_validation::validWifiPassword(config.password.c_str(), config.password.length()) &&
         gauge_validation::validDeviceToken(config.deviceToken.c_str(), config.deviceToken.length()) &&
         gauge_validation::validateEndpoint(config.endpoint.c_str(), config.endpoint.length()) ==
             gauge_validation::UrlError::None &&
         (!config.secureEndpoint() || !config.deviceToken.isEmpty());
}

bool savePersisted(const GaugeNetworkConfig &config) {
  Preferences preferences;
  if (!preferences.begin("codex-gauge", false)) return false;
  const uint8_t oldSlot = preferences.getUChar("slot", 0) & 1U;
  const uint8_t newSlot = oldSlot ^ 1U;
  const char prefix = newSlot == 0 ? 'a' : 'b';
  const String ssidKey = String(prefix) + "_ssid";
  const String passKey = String(prefix) + "_pass";
  const String urlKey = String(prefix) + "_url";
  const String tokenKey = String(prefix) + "_token";
  const String encodedPassword = String("v1:") + config.password;
  const String encodedToken = String("v1:") + config.deviceToken;
  const bool valuesWritten = preferences.putString(ssidKey.c_str(), config.ssid) > 0 &&
                             preferences.putString(passKey.c_str(), encodedPassword) > 0 &&
                             preferences.putString(urlKey.c_str(), config.endpoint) > 0 &&
                             preferences.putString(tokenKey.c_str(), encodedToken) > 0;
  // Verify the inactive slot before making it active. Never echo credentials.
  const bool verified = valuesWritten &&
      preferences.getString(ssidKey.c_str(), "") == config.ssid &&
      preferences.getString(passKey.c_str(), "") == encodedPassword &&
      preferences.getString(urlKey.c_str(), "") == config.endpoint &&
      preferences.getString(tokenKey.c_str(), "") == encodedToken;
  const bool committed = verified && preferences.putUChar("slot", newSlot) == 1 &&
                         preferences.getUChar("slot", oldSlot) == newSlot;
  preferences.end();
  return committed;
}

String apHost() {
  return WiFi.softAPIP().toString();
}
}  // namespace

void ProvisioningManager::begin(const char *initialSsid, const char *initialPassword,
                                const char *initialEndpoint, uint32_t now) {
  if (!loadPersisted(current_)) {
    current_ = GaugeNetworkConfig{};
    const size_t ssidLength = initialSsid == nullptr ? 0 : strlen(initialSsid);
    const size_t passwordLength = initialPassword == nullptr ? 0 : strlen(initialPassword);
    const size_t endpointLength = initialEndpoint == nullptr ? 0 : strlen(initialEndpoint);
    if (gauge_validation::validSsid(initialSsid, ssidLength) &&
        gauge_validation::validWifiPassword(initialPassword, passwordLength) &&
        gauge_validation::validateEndpoint(initialEndpoint, endpointLength) ==
            gauge_validation::UrlError::None) {
      current_.ssid = initialSsid;
      current_.password = initialPassword;
      current_.endpoint = initialEndpoint;
      current_.deviceToken = "";
    }
  }

  const uint64_t chip = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06lx", static_cast<unsigned long>(chip & 0xffffffULL));
  apSsid_ = String("CodexGauge-") + suffix;
  disconnectedSinceMs_ = now;
  nextReconnectMs_ = now;
  if (current_.complete()) {
    connectCurrent(now);
  } else {
    startPortal(now, false);
  }
}

void ProvisioningManager::connectCurrent(uint32_t now) {
  if (!current_.complete()) return;
  WiFi.begin(current_.ssid.c_str(), current_.password.c_str());
  nextReconnectMs_ = now + RECONNECT_INTERVAL_MS;
}

void ProvisioningManager::loop(uint32_t now) {
  if (Serial.available() > 0) {
    const int command = Serial.read();
    if (command == 's') {
      if (!portalActive_) requestPortal(now);
      else if (!candidatePending_ && !scanRunning_ && !scanStartPending_ &&
               now - lastScanStartedMs_ >= SCAN_RATE_LIMIT_MS) startScan();
    } else if (command == 'd') {
      char configuredHost[254] = {};
      uint16_t configuredPort = 0;
      gauge_validation::splitQuotaEndpoint(current_.endpoint.c_str(), current_.endpoint.length(),
                                          configuredHost, sizeof(configuredHost), &configuredPort);
      const bool validTarget = configuredHost[0] != '\0';
      IPAddress address;
      const bool resolved = validTarget && WiFi.hostByName(configuredHost, address) == 1;
      Serial.printf("[network] wifi=%u clock_ok=%u target_valid=%u port=%u dns_ok=%u\n",
                    WiFi.status() == WL_CONNECTED ? 1U : 0U,
                    time(nullptr) >= 1700000000 ? 1U : 0U,
                    validTarget ? 1U : 0U, static_cast<unsigned>(configuredPort),
                    resolved ? 1U : 0U);
    } else if (command == 'c') {
      Serial.printf("[config] https=%u token_saved=%u state=%u draft=%u\n",
                    current_.secureEndpoint() ? 1U : 0U, current_.deviceToken.isEmpty() ? 0U : 1U,
                    static_cast<unsigned>(state_), hasFormDraft_ ? 1U : 0U);
    } else if (command == '?') {
      // Deliberately omit SSIDs, passwords, tokens and device/account identifiers.
      Serial.printf("[setup] portal=%u password_reused=%u scan=%u count=%u error=%ld\n",
                    portalActive_ ? 1U : 0U, setupPasswordReused ? 1U : 0U,
                    static_cast<unsigned>(scanState_), static_cast<unsigned>(networkCount_),
                    static_cast<long>(lastScanError_));
    }
  }
  if (portalActive_) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    // A POST may start a candidate with millis() after the caller sampled now.
    // Refresh before unsigned timeout arithmetic to avoid an immediate rollback.
    now = millis();
    if (finishRequested_) {
      finishRequested_ = false;
      stopPortal(now, ProvisioningState::Saved);
      return;
    }
    serviceScan();
    if (candidatePending_) {
      // Never accept a still-reported old connection (especially when SSIDs match).
      // Begin the candidate only after the asynchronous disconnect is observable.
      if (!candidateConnectionStarted_ && WiFi.status() != WL_CONNECTED) {
        WiFi.begin(candidate_.ssid.c_str(), candidate_.password.c_str());
        candidateConnectionStarted_ = true;
      }
      if (candidateConnectionStarted_ && WiFi.status() == WL_CONNECTED &&
          WiFi.SSID() == candidate_.ssid) {
        candidateSucceeded(now);
        return;
      }
      if (now - candidateStartedMs_ >= CANDIDATE_TIMEOUT_MS) candidateFailed(now);
    } else if (!scanRunning_ && !scanStartPending_ && current_.complete() && WiFi.status() != WL_CONNECTED &&
               due(now, nextReconnectMs_)) {
      connectCurrent(now);
    }
    if (portalActive_ && now - portalStartedMs_ >= PORTAL_TIMEOUT_MS) {
      candidatePending_ = false;
      stopPortal(now, ProvisioningState::TimedOut);
      connectCurrent(now);
    }
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSinceMs_ = now;
    return;
  }
  if (current_.complete() && due(now, nextReconnectMs_)) connectCurrent(now);
  if (disconnectedSinceMs_ == 0) disconnectedSinceMs_ = now;
  if (now - disconnectedSinceMs_ >= AUTO_PORTAL_DELAY_MS && due(now, automaticCooldownUntilMs_)) {
    startPortal(now, false);
  }
}

void ProvisioningManager::requestPortal(uint32_t now) {
  if (!portalActive_) startPortal(now, true);
  else portalStartedMs_ = now;
}

void ProvisioningManager::startPortal(uint32_t now, bool manual) {
  (void)manual;
  WiFi.mode(WIFI_AP_STA);
  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress netmask(255, 255, 255, 0);
  WiFi.setAutoReconnect(false);
  if (apPassword_.isEmpty()) apPassword_ = persistentSetupPassword();
  if (apPassword_.length() != 8) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    automaticCooldownUntilMs_ = now + PORTAL_COOLDOWN_MS;
    markState(ProvisioningState::CandidateFailed);
    return;
  }
  csrfToken_ = randomHex(32);
  const bool apStarted = WiFi.softAPConfig(apIp, apIp, netmask) &&
                         WiFi.softAP(apSsid_.c_str(), apPassword_.c_str(), 1, false, 4);
  const bool dnsStarted = apStarted && dnsServer.start(DNS_PORT, "*", apIp);
  if (!dnsStarted) {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    automaticCooldownUntilMs_ = now + PORTAL_COOLDOWN_MS;
    disconnectedSinceMs_ = now;
    markState(ProvisioningState::CandidateFailed);
    return;
  }
  registerRoutes();
  webServer.begin();
  portalStartedMs_ = now;
  portalActive_ = true;
  finishRequested_ = false;
  candidatePending_ = false;
  candidateConnectionStarted_ = false;
  hasFormDraft_ = false;
  draftToken_ = "";
  formMessage_ = F("Bitte WLAN und Server eintragen. Für gauge.example.org HTTPS und Port 443 wählen.");
  markState(ProvisioningState::PortalReady);
  startScan();
}

void ProvisioningManager::stopPortal(uint32_t now, ProvisioningState finalState) {
  dnsServer.stop();
  webServer.stop();
  stopScan();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  portalActive_ = false;
  hasFormDraft_ = false;
  draftToken_ = "";
  finishRequested_ = false;
  candidatePending_ = false;
  candidateConnectionStarted_ = false;
  automaticCooldownUntilMs_ = now + PORTAL_COOLDOWN_MS;
  disconnectedSinceMs_ = now;
  markState(finalState);
}

void ProvisioningManager::startScan() {
  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
  nextReconnectMs_ = now + 15000;
  scanOptions_ = F("<option value=\"\">SSID frei eingeben</option>");
  networkCount_ = 0;
  scanState_ = ProvisioningScanState::Running;
  lastScanStartedMs_ = now;
  scanStartPending_ = true;
  scanStartDueMs_ = now + 250; // Let the asynchronous disconnect settle first.
  scanAttempts_ = 0;
  lastScanError_ = ESP_OK;
}

void ProvisioningManager::serviceScan() {
  const uint32_t now = millis();
  if (scanStartPending_) {
    if (!due(now, scanStartDueMs_)) return;
    scanStartPending_ = false;
    const int result = radioScan.start();
    lastScanError_ = radioScan.lastError();
    Serial.printf("[scan] start=%d native_error=%ld\n", result, static_cast<long>(lastScanError_));
    if (result == WIFI_SCAN_RUNNING) {
      scanRunning_ = true;
    } else if (result >= 0) {
      finishScan(result);
      return;
    } else {
      if (lastScanError_ == ESP_ERR_WIFI_STATE && scanAttempts_++ == 0) {
        if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);
        scanStartPending_ = true;
        scanStartDueMs_ = now + 500;
        return;
      }
      scanState_ = ProvisioningScanState::Error;
      WiFi.scanDelete();
      return;
    }
  }
  if (!scanRunning_) return;
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;
  scanRunning_ = false;
  Serial.printf("[scan] complete=%d elapsed_ms=%lu\n", count,
                static_cast<unsigned long>(now - lastScanStartedMs_));
  if (count < 0) {
    lastScanError_ = ESP_ERR_TIMEOUT;
    scanState_ = ProvisioningScanState::Error;
    WiFi.scanDelete();
    return;
  }
  finishScan(count);
}

void ProvisioningManager::finishScan(int count) {
  networkCount_ = 0;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!gauge_validation::validSsid(ssid.c_str(), ssid.length())) continue;
    const int32_t rssi = WiFi.RSSI(i);
    const uint8_t authMode = static_cast<uint8_t>(WiFi.encryptionType(i));
    int existing = -1;
    for (uint8_t n = 0; n < networkCount_; ++n) {
      if (networks_[n].ssid == ssid) {
        existing = n;
        break;
      }
    }
    if (existing >= 0) {
      if (rssi > networks_[existing].rssi) {
        networks_[existing].rssi = rssi;
        networks_[existing].authMode = authMode;
      }
      continue;
    }
    uint8_t target = networkCount_;
    if (networkCount_ < MAX_SCAN_RESULTS) {
      ++networkCount_;
    } else {
      target = 0;
      for (uint8_t n = 1; n < networkCount_; ++n) {
        if (networks_[n].rssi < networks_[target].rssi) target = n;
      }
      if (rssi <= networks_[target].rssi) continue;
    }
    networks_[target].ssid = ssid;
    networks_[target].rssi = rssi;
    networks_[target].authMode = authMode;
  }
  for (uint8_t i = 1; i < networkCount_; ++i) {
    ProvisioningNetwork value = networks_[i];
    int j = i - 1;
    while (j >= 0 && networks_[j].rssi < value.rssi) {
      networks_[j + 1] = networks_[j];
      --j;
    }
    networks_[j + 1] = value;
  }
  scanOptions_ = F("<option value=\"\">SSID frei eingeben</option>");
  for (uint8_t i = 0; i < networkCount_; ++i) {
    scanOptions_ += enterpriseAuth(networks_[i].authMode) ? F("<option disabled value=\"")
                                                          : F("<option value=\"");
    scanOptions_ += htmlEscape(networks_[i].ssid);
    scanOptions_ += F("\">");
    scanOptions_ += htmlEscape(networks_[i].ssid);
    scanOptions_ += F(" – ");
    scanOptions_ += securityLabel(networks_[i].authMode);
    scanOptions_ += F("</option>");
  }
  scanState_ = ProvisioningScanState::Ready;
  WiFi.scanDelete();
}

void ProvisioningManager::stopScan() {
  scanStartPending_ = false;
  if (scanRunning_) {
    const esp_err_t stopped = esp_wifi_scan_stop();
    if (stopped == ESP_OK || WiFi.scanComplete() != WIFI_SCAN_RUNNING) scanRunning_ = false;
  }
  if (scanRunning_) return;
  WiFi.scanDelete();
  if (scanState_ == ProvisioningScanState::Running) scanState_ = ProvisioningScanState::Idle;
}

void ProvisioningManager::registerRoutes() {
  if (routesRegistered_) return;
  const char *headers[] = {"Host", "Content-Length"};
  webServer.collectHeaders(headers, 2);
  webServer.on("/", HTTP_GET, [this]() { sendPortal(); });
  webServer.on("/networks", HTTP_GET, [this]() { handleNetworks(); });
  webServer.on("/scan", HTTP_POST, [this]() { handleScan(); });
  webServer.on("/save", HTTP_POST, [this]() { handleSave(); });
  webServer.on("/finish", HTTP_POST, [this]() { handleFinish(); });
  webServer.on("/generate_204", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/hotspot-detect.html", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.on("/connecttest.txt", HTTP_GET, [this]() { redirectToPortal(); });
  webServer.onNotFound([this]() { redirectToPortal(); });
  routesRegistered_ = true;
}

bool ProvisioningManager::isApClient(bool requireDirectHost) const {
  const IPAddress remote = webServer.client().remoteIP();
  const IPAddress local = webServer.client().localIP();
  const IPAddress ap = WiFi.softAPIP();
  if (remote[0] != 192 || remote[1] != 168 || remote[2] != 4 || local != ap) return false;
  if (!requireDirectHost) return true;
  String host = webServer.header("Host");
  const int colon = host.indexOf(':');
  if (colon >= 0) host.remove(colon);
  return host == ap.toString();
}

void ProvisioningManager::redirectToPortal() {
  if (!portalActive_ || !isApClient(false)) {
    webServer.send(404, "text/plain", "Not found");
    return;
  }
  webServer.sendHeader("Location", String("http://") + apHost() + "/", true);
  webServer.send(302, "text/plain", "");
}

void ProvisioningManager::handleNetworks() {
  if (!portalActive_ || !isApClient(true)) {
    webServer.send(403, "application/json", "{\"error\":\"forbidden\"}");
    return;
  }
  String json;
  json.reserve(128 + networkCount_ * 120);
  json += F("{\"status\":\"");
  switch (scanState_) {
    case ProvisioningScanState::Running: json += F("running"); break;
    case ProvisioningScanState::Ready: json += F("ready"); break;
    case ProvisioningScanState::Error: json += F("error"); break;
    default: json += F("idle"); break;
  }
  json += F("\",\"networks\":[");
  for (uint8_t i = 0; i < networkCount_; ++i) {
    if (i != 0) json += ',';
    json += F("{\"ssid\":\"");
    json += jsonEscape(networks_[i].ssid);
    json += F("\",\"rssi\":");
    json += networks_[i].rssi;
    json += F(",\"security\":\"");
    json += securityLabel(networks_[i].authMode);
    json += F("\",\"supported\":");
    json += enterpriseAuth(networks_[i].authMode) ? F("false") : F("true");
    json += '}';
  }
  json += F("]}");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.sendHeader("X-Content-Type-Options", "nosniff");
  webServer.send(200, "application/json; charset=utf-8", json);
}

void ProvisioningManager::handleScan() {
  if (!portalActive_ || !isApClient(true)) {
    webServer.send(403, "application/json", "{\"error\":\"forbidden\"}");
    return;
  }
  portalStartedMs_ = millis();
  const String contentLength = webServer.header("Content-Length");
  if (contentLength.isEmpty() || contentLength.length() > 3 || contentLength.toInt() <= 0 ||
      contentLength.toInt() > 96 || webServer.args() != 1 || !webServer.hasArg("csrf") ||
      webServer.arg("csrf").length() != 32 || webServer.arg("csrf") != csrfToken_) {
    webServer.send(403, "application/json", "{\"error\":\"invalid_request\"}");
    return;
  }
  const uint32_t now = millis();
  if (candidatePending_ || scanRunning_ || scanStartPending_) {
    webServer.send(409, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  if (now - lastScanStartedMs_ < SCAN_RATE_LIMIT_MS) {
    webServer.send(429, "application/json", "{\"error\":\"rate_limited\"}");
    return;
  }
  startScan();
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(202, "application/json", "{\"status\":\"started\"}");
}

void ProvisioningManager::sendPortal() {
  if (!portalActive_ || !isApClient(false)) {
    webServer.send(404, "text/plain", "Not found");
    return;
  }
  if (!isApClient(true)) {
    redirectToPortal();
    return;
  }
  portalStartedMs_ = millis();
  char endpointHost[254] = {};
  uint16_t endpointPort = 8787;
  bool endpointSecure = false;
  gauge_validation::splitQuotaEndpoint(current_.endpoint.c_str(), current_.endpoint.length(),
                                       endpointHost, sizeof(endpointHost), &endpointPort,
                                       &endpointSecure);
  char portText[6];
  snprintf(portText, sizeof(portText), "%u", static_cast<unsigned>(endpointPort));
  const String shownHost = hasFormDraft_ ? draftHost_ : String(endpointHost);
  const String shownPort = hasFormDraft_ ? draftPort_ : String(portText);
  const String shownSsid = hasFormDraft_ ? draftSsid_ : current_.ssid;
  if (hasFormDraft_) endpointSecure = draftScheme_ == "https";
  String page;
  page.reserve(6500 + scanOptions_.length());
  page += F("<!doctype html><html lang=\"de\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">");
  if (state_ == ProvisioningState::Testing) {
    page += F("<meta http-equiv=\"refresh\" content=\"3\">");
  }
  page += F("<title>Codex Gauge einrichten</title><style>body{font:16px system-ui;max-width:34rem;margin:2rem auto;padding:0 1rem;background:#081516;color:#eef}label{display:block;margin-top:1rem}input,select,button{box-sizing:border-box;width:100%;padding:.8rem;margin-top:.35rem;font:inherit}button{background:#39d98a;border:0;font-weight:700}.net{margin:.45rem 0;background:#173235;color:#eef;text-align:left}.net:disabled{opacity:.55}small,.hint{color:#abc}.msg,.scanbox{padding:.8rem;background:#102829}.scanbox{margin-top:1rem}</style>");
  page += F("<h1>Codex Gauge einrichten</h1><p class=\"msg\">");
  page += htmlEscape(formMessage_);
  page += F("</p><section class=\"scanbox\"><strong>Verfügbare 2,4-GHz-WLANs</strong><p id=\"scanstatus\" class=\"hint\">WLANs suchen …</p><div id=\"networks\"></div><button type=\"button\" id=\"rescan\">Erneut suchen</button><noscript><p>JavaScript ist aus. Liste nach einem Neuladen:</p><select>");
  page += scanOptions_;
  page += F("</select><p class=\"hint\">SSID unten manuell eintragen.</p></noscript></section><form method=\"post\" action=\"/save\" autocomplete=\"off\"><input type=\"hidden\" name=\"csrf\" value=\"");
  page += csrfToken_;
  page += F("\"><label>SSID<input id=\"ssid\" name=\"ssid\" maxlength=\"32\" required value=\"");
  page += htmlEscape(shownSsid);
  page += F("\"></label><label>WLAN-Passwort<input name=\"password\" type=\"password\" maxlength=\"63\" autocomplete=\"new-password\"></label>");
  page += F("<small>Bei gleicher SSID leer lassen, um das gespeicherte Passwort zu behalten. Bei neuer SSID nur für ein offenes WLAN leer lassen.</small>");
  page += F("<label>IP-Adresse des Servers<input name=\"host\" maxlength=\"253\" required autocapitalize=\"none\" spellcheck=\"false\" onfocus=\"this.select()\" value=\"");
  page += htmlEscape(shownHost);
  page += F("\" placeholder=\"gauge.example.org\"></label><small>HTTP ist nur für lokale Server erlaubt; HTTPS für öffentliche DNS-Namen.</small><label>Verbindung<select id=\"scheme\" name=\"scheme\"><option value=\"http\"");
  if (!endpointSecure) page += F(" selected");
  page += F(">HTTP (lokal)</option><option value=\"https\"");
  if (endpointSecure) page += F(" selected");
  page += F(">HTTPS (sicher)</option></select></label><label>Port<input id=\"port\" name=\"port\" type=\"text\" inputmode=\"numeric\" pattern=\"[0-9]{1,5}\" maxlength=\"5\" required onfocus=\"this.select()\" value=\"");
  page += htmlEscape(shownPort);
  page += F("\"></label><small>Der Pfad /v1/quota wird automatisch ergänzt.</small><label>Geräteschlüssel<input name=\"token\" type=\"password\" minlength=\"32\" maxlength=\"128\" autocomplete=\"new-password\"></label><small>");
  page += !draftToken_.isEmpty() ? F("Schlüssel für diese Sitzung übernommen, noch nicht gespeichert. Feld leer lassen.")
      : current_.deviceToken.isEmpty() ? F("Noch kein Schlüssel gespeichert. HTTPS benötigt einen Schlüssel.")
                                         : F("Schlüssel gespeichert. Leer lassen, um ihn beizubehalten.");
  page += F("</small><button type=\"submit\">Einstellungen speichern</button></form>");
  if (state_ == ProvisioningState::Saved && current_.complete() && WiFi.status() == WL_CONNECTED) {
    page += F("<form method=\"post\" action=\"/finish\"><input type=\"hidden\" name=\"csrf\" value=\"");
    page += csrfToken_;
    page += F("\"><button type=\"submit\">Fertig – Anzeige starten</button></form>");
  }
  page += F("<script>(()=>{const csrf='");
  page += csrfToken_;
  page += F("',box=document.getElementById('networks'),status=document.getElementById('scanstatus'),ssid=document.getElementById('ssid'),rescan=document.getElementById('rescan'),scheme=document.getElementById('scheme'),port=document.getElementById('port');let tries=0,timer=0,lastScheme=scheme.value;scheme.addEventListener('change',()=>{if((lastScheme==='http'&&(port.value==='8787'||port.value==='80'))||(lastScheme==='https'&&port.value==='443'))port.value=scheme.value==='https'?'443':'8787';lastScheme=scheme.value});async function request(url,options){const controller=new AbortController(),timeout=setTimeout(()=>controller.abort(),2500);try{return await fetch(url,Object.assign({cache:'no-store',signal:controller.signal},options||{}));}finally{clearTimeout(timeout)}}function later(){clearTimeout(timer);if(tries<30)timer=setTimeout(load,1500);else status.textContent='WLAN-Suche hat zu lange gedauert.'}function render(data){box.replaceChildren();if(data.status==='running'){status.textContent='WLANs suchen …';later();return}if(data.status==='error'){status.textContent='WLAN-Suche fehlgeschlagen. Bitte erneut versuchen.';return}if(!data.networks||data.networks.length===0){status.textContent='Keine Netze gefunden. SSID kann unten manuell eingetragen werden.';return}status.textContent=data.networks.length+' Netze gefunden';data.networks.forEach(n=>{const b=document.createElement('button');b.type='button';b.className='net';b.disabled=!n.supported;b.textContent=n.ssid+' · '+n.rssi+' dBm · '+n.security;b.addEventListener('click',()=>{ssid.value=n.ssid;ssid.focus()});box.appendChild(b)})}async function load(){if(++tries>30)return later();try{const r=await request('/networks');if(!r.ok)throw 0;render(await r.json())}catch(e){status.textContent='WLAN-Liste vorübergehend nicht erreichbar.';later()}}rescan.addEventListener('click',async()=>{rescan.disabled=true;status.textContent='WLANs suchen …';try{const r=await request('/scan',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({csrf})});if(r.status===429)status.textContent='Bitte kurz warten und erneut versuchen.';else if(!r.ok)status.textContent='Neue Suche konnte nicht gestartet werden.';else{tries=0;await load()}}catch(e){status.textContent='Neue Suche konnte nicht gestartet werden.'}finally{setTimeout(()=>rescan.disabled=false,5000)}});load()})()</script></html>");
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.send(200, "text/html; charset=utf-8", page);
}

void ProvisioningManager::handleSave() {
  if (!portalActive_ || !isApClient(true)) {
    webServer.send(403, "text/plain; charset=utf-8", "Nicht erlaubt");
    return;
  }
  const uint32_t now = millis();
  portalStartedMs_ = now;
  if (candidatePending_) {
    webServer.send(409, "text/plain; charset=utf-8", "Verbindungsprüfung läuft bereits");
    return;
  }
  const String contentLength = webServer.header("Content-Length");
  if (contentLength.isEmpty() || contentLength.length() > 4 || contentLength.toInt() <= 0 ||
      contentLength.toInt() > static_cast<int>(MAX_FORM_BYTES) || webServer.args() != 7 ||
      !webServer.hasArg("csrf") || !webServer.hasArg("ssid") || !webServer.hasArg("password") ||
      !webServer.hasArg("host") || !webServer.hasArg("port") ||
      !webServer.hasArg("scheme") || !webServer.hasArg("token")) {
    webServer.send(413, "text/plain; charset=utf-8", "Formular zu groß oder ungültig");
    return;
  }
  const String csrf = webServer.arg("csrf");
  const String ssid = webServer.arg("ssid");
  const String enteredPassword = webServer.arg("password");
  String host = webServer.arg("host");
  String port = webServer.arg("port");
  String scheme = webServer.arg("scheme");
  String enteredToken = webServer.arg("token");
  host.trim();
  port.trim();
  enteredToken.trim();
  if (csrf.length() != 32 || csrf != csrfToken_) {
    webServer.send(403, "text/plain; charset=utf-8", "Sitzung abgelaufen");
    return;
  }
  // Accept a pasted HTTPS address as well as the intended host-only input.
  if (host.startsWith("https://")) { scheme = "https"; host.remove(0, 8); port = "443"; }
  else if (host.startsWith("http://")) { scheme = "http"; host.remove(0, 7); }
  if (host.endsWith("/v1/quota")) host.remove(host.length() - 9);
  else if (host.endsWith("/")) host.remove(host.length() - 1);
  const int explicitPort = host.indexOf(':');
  if (explicitPort > 0) { port = host.substring(explicitPort + 1); host.remove(explicitPort); }
  draftSsid_ = ssid; draftHost_ = host; draftPort_ = port; draftScheme_ = scheme;
  hasFormDraft_ = true;
  if (!enteredToken.isEmpty() && gauge_validation::validDeviceToken(enteredToken.c_str(), enteredToken.length()))
    draftToken_ = enteredToken;
  String effectivePassword = enteredPassword;
  if (ssid == current_.ssid && enteredPassword.isEmpty()) effectivePassword = current_.password;
  String effectiveToken = enteredToken.isEmpty() ? (draftToken_.isEmpty() ? current_.deviceToken : draftToken_) : enteredToken;
  const bool secure = scheme == "https";
  char endpoint[201];
  size_t endpointLength = 0;
  const gauge_validation::UrlError endpointError = gauge_validation::buildQuotaEndpoint(
      host.c_str(), host.length(), port.c_str(), port.length(), endpoint, sizeof(endpoint),
      &endpointLength, secure);
  if (!gauge_validation::validSsid(ssid.c_str(), ssid.length()))
    formMessage_ = F("Nicht gespeichert: WLAN-Name fehlt oder ist ungültig.");
  else if (!gauge_validation::validWifiPassword(effectivePassword.c_str(), effectivePassword.length()))
    formMessage_ = F("Nicht gespeichert: WLAN-Passwort muss 8 bis 63 Zeichen haben.");
  else if (scheme != "http" && scheme != "https")
    formMessage_ = F("Nicht gespeichert: HTTP oder HTTPS auswählen.");
  else if (!gauge_validation::validDeviceToken(effectiveToken.c_str(), effectiveToken.length()))
    formMessage_ = F("Nicht gespeichert: Geräteschlüssel ungültig. Nur den Schlüssel kopieren, keine Konsolenzeile.");
  else if (secure && effectiveToken.isEmpty())
    formMessage_ = F("Nicht gespeichert: Für HTTPS fehlt der Geräteschlüssel.");
  else if (endpointError == gauge_validation::UrlError::Port)
    formMessage_ = F("Nicht gespeichert: Port muss zwischen 1 und 65535 liegen. Für die Domain 443 verwenden.");
  else if (endpointError == gauge_validation::UrlError::NotTrustedLan && !secure)
    formMessage_ = F("Nicht gespeichert: Für öffentliche Domains HTTPS auswählen, Port 443.");
  else if (endpointError != gauge_validation::UrlError::None)
    formMessage_ = F("Nicht gespeichert: Serveradresse ungültig. Beispiel: gauge.example.org, HTTPS, Port 443.");
  else formMessage_ = "";
  if (!formMessage_.isEmpty()) {
    markState(ProvisioningState::CandidateFailed);
    sendPortal();
    return;
  }
  candidate_.ssid = ssid;
  candidate_.password = effectivePassword;
  candidate_.endpoint = String(endpoint, endpointLength);
  candidate_.deviceToken = effectiveToken;
  const bool serverOnlyChange = ssid == current_.ssid && enteredPassword.isEmpty() &&
                                WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid;
  if (serverOnlyChange) {
    if (savePersisted(candidate_)) {
      current_ = candidate_;
      hasFormDraft_ = false;
      draftToken_ = "";
      candidate_ = GaugeNetworkConfig{};
      configChanged_ = true;
      formMessage_ = F("WLAN verbunden, Einstellungen gespeichert. Serverdaten lassen sich hier weiter ändern.");
      markState(ProvisioningState::Saved);
      portalStartedMs_ = now;
    } else {
      candidate_ = GaugeNetworkConfig{};
      formMessage_ = F("Speichern fehlgeschlagen; bisherige Einstellungen bleiben erhalten.");
      markState(ProvisioningState::CandidateFailed);
    }
  } else {
    beginCandidate(now);
  }
  webServer.sendHeader("Location", String("http://") + apHost() + "/", true);
  webServer.send(303, "text/plain", "");
}

void ProvisioningManager::handleFinish() {
  if (!portalActive_ || !isApClient(true)) {
    webServer.send(403, "text/plain; charset=utf-8", "Nicht erlaubt");
    return;
  }
  portalStartedMs_ = millis();
  const String contentLength = webServer.header("Content-Length");
  if (contentLength.isEmpty() || contentLength.length() > 3 || contentLength.toInt() <= 0 ||
      contentLength.toInt() > 96 || webServer.args() != 1 || !webServer.hasArg("csrf") ||
      webServer.arg("csrf").length() != 32 || webServer.arg("csrf") != csrfToken_) {
    webServer.send(403, "text/plain; charset=utf-8", "Ungültige Anfrage");
    return;
  }
  if (candidatePending_ || !current_.complete() || WiFi.status() != WL_CONNECTED ||
      WiFi.SSID() != current_.ssid) {
    webServer.send(409, "text/plain; charset=utf-8", "WLAN noch nicht verbunden");
    return;
  }
  webServer.send(200, "text/html; charset=utf-8",
                 "<!doctype html><meta charset=utf-8><title>Fertig</title><p>Einrichtung abgeschlossen. Die Anzeige startet.</p>");
  finishRequested_ = true;
}

void ProvisioningManager::beginCandidate(uint32_t now) {
  stopScan();
  if (scanRunning_) {
    formMessage_ = F("WLAN-Suche läuft noch. Bitte gleich erneut speichern.");
    markState(ProvisioningState::CandidateFailed);
    return;
  }
  candidatePending_ = true;
  candidateConnectionStarted_ = false;
  candidateStartedMs_ = now;
  formMessage_ = F("WLAN-Verbindung wird bis zu 30 Sekunden geprüft …");
  markState(ProvisioningState::Testing);
  WiFi.disconnect(false, false);
}

void ProvisioningManager::candidateSucceeded(uint32_t now) {
  if (!savePersisted(candidate_)) {
    candidateFailed(now);
    formMessage_ = F("Speichern fehlgeschlagen; bisherige Konfiguration bleibt erhalten.");
    return;
  }
  current_ = candidate_;
  hasFormDraft_ = false;
  draftToken_ = "";
  configChanged_ = true;
  candidate_ = GaugeNetworkConfig{};
  candidatePending_ = false;
  candidateConnectionStarted_ = false;
  formMessage_ = F("WLAN verbunden, Einstellungen gespeichert. Serverdaten lassen sich hier weiter ändern.");
  portalStartedMs_ = now;
  markState(ProvisioningState::Saved);
}

void ProvisioningManager::candidateFailed(uint32_t now) {
  candidatePending_ = false;
  candidateConnectionStarted_ = false;
  candidate_ = GaugeNetworkConfig{};
  formMessage_ = F("Keine WLAN-Verbindung. Nichts gespeichert; bitte Eingaben prüfen.");
  portalStartedMs_ = now;
  markState(ProvisioningState::CandidateFailed);
  WiFi.disconnect(false, false);
  connectCurrent(now);
}

void ProvisioningManager::markState(ProvisioningState state) {
  state_ = state;
  displayDirty_ = true;
}

bool ProvisioningManager::consumeDisplayDirty() {
  const bool dirty = displayDirty_;
  displayDirty_ = false;
  return dirty;
}

bool ProvisioningManager::consumeConfigChanged() {
  const bool changed = configChanged_;
  configChanged_ = false;
  return changed;
}

String ProvisioningManager::statusText() const {
  switch (state_) {
    case ProvisioningState::PortalReady: return "BEREIT";
    case ProvisioningState::Testing: return "VERBINDE...";
    case ProvisioningState::CandidateFailed: return "FEHLER - NOCHMAL";
    case ProvisioningState::Saved: return "GESPEICHERT";
    case ProvisioningState::TimedOut: return "SETUP ENDE";
    default: return "";
  }
}

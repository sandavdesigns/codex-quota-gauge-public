#pragma once

#include <WiFi.h>
#include <esp_wifi.h>

// Arduino-ESP32 2.0.17's scanner leaves fields of wifi_scan_config_t unset.
// Keep its shared result/event handling, but initialize the entire native config.
class ReliableWifiScan : public WiFiScanClass, public WiFiGenericClass {
 public:
  int16_t start() {
    error_ = ESP_OK;
    if (getStatusBits() & WIFI_SCANNING_BIT) return WIFI_SCAN_RUNNING;
    if (!WiFi.enableSTA(true)) {
      error_ = ESP_ERR_WIFI_NOT_STARTED;
      return WIFI_SCAN_FAILED;
    }
    scanDelete();
    wifi_scan_config_t config{};
    config.show_hidden = true;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min = 100;
    config.scan_time.active.max = 300;
    config.home_chan_dwell_time = 30;
    _scanAsync = true;
    _scanTimeout = 10000;
    _scanStarted = millis();
    if (_scanStarted == 0) _scanStarted = 1;
    clearStatusBits(WIFI_SCAN_DONE_BIT);
    setStatusBits(WIFI_SCANNING_BIT);
    error_ = esp_wifi_scan_start(&config, false);
    if (error_ != ESP_OK) {
      _scanStarted = 0;
      clearStatusBits(WIFI_SCANNING_BIT | WIFI_SCAN_DONE_BIT);
      return WIFI_SCAN_FAILED;
    }
    return WIFI_SCAN_RUNNING;
  }
  esp_err_t lastError() const { return error_; }
 private:
  esp_err_t error_ = ESP_OK;
};

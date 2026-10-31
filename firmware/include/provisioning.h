#pragma once

#include <Arduino.h>

struct GaugeNetworkConfig {
  String ssid;
  String password;
  String endpoint;
  String deviceToken;
  bool secureEndpoint() const { return endpoint.startsWith("https://"); }
  bool complete() const {
    return !ssid.isEmpty() && !endpoint.isEmpty() && (!secureEndpoint() || !deviceToken.isEmpty());
  }
};

enum class ProvisioningState : uint8_t { Idle, PortalReady, Testing, CandidateFailed, Saved, TimedOut };
enum class ProvisioningScanState : uint8_t { Idle, Running, Ready, Error };

struct ProvisioningNetwork {
  String ssid;
  int32_t rssi = -127;
  uint8_t authMode = 0;
};

class ProvisioningManager {
 public:
  void begin(const char *initialSsid, const char *initialPassword, const char *initialEndpoint, uint32_t now);
  void loop(uint32_t now);
  void requestPortal(uint32_t now);
  const GaugeNetworkConfig &config() const { return current_; }
  bool portalActive() const { return portalActive_; }
  bool pauseFetch() const { return portalActive_; }
  bool configured() const { return current_.complete(); }
  bool consumeDisplayDirty();
  bool consumeConfigChanged();
  ProvisioningState state() const { return state_; }
  const String &apSsid() const { return apSsid_; }
  const String &apPassword() const { return apPassword_; }
  String statusText() const;

 private:
  void connectCurrent(uint32_t now);
  void startPortal(uint32_t now, bool manual);
  void stopPortal(uint32_t now, ProvisioningState finalState);
  void beginCandidate(uint32_t now);
  void candidateSucceeded(uint32_t now);
  void candidateFailed(uint32_t now);
  void startScan();
  void serviceScan();
  void finishScan(int count);
  void stopScan();
  void registerRoutes();
  bool isApClient(bool requireDirectHost) const;
  void sendPortal();
  void handleSave();
  void handleFinish();
  void handleNetworks();
  void handleScan();
  void redirectToPortal();
  void markState(ProvisioningState state);

  GaugeNetworkConfig current_;
  GaugeNetworkConfig candidate_;
  String apSsid_, apPassword_, csrfToken_, scanOptions_, formMessage_;
  String draftSsid_, draftHost_, draftPort_, draftScheme_, draftToken_;
  bool hasFormDraft_ = false;
  ProvisioningNetwork networks_[16];
  ProvisioningScanState scanState_ = ProvisioningScanState::Idle;
  uint8_t networkCount_ = 0;
  ProvisioningState state_ = ProvisioningState::Idle;
  uint32_t disconnectedSinceMs_ = 0, nextReconnectMs_ = 0, portalStartedMs_ = 0;
  uint32_t automaticCooldownUntilMs_ = 0, candidateStartedMs_ = 0;
  uint32_t lastScanStartedMs_ = 0, scanStartDueMs_ = 0;
  int32_t lastScanError_ = 0;
  uint8_t scanAttempts_ = 0;
  bool scanStartPending_ = false;
  bool portalActive_ = false, candidatePending_ = false, candidateConnectionStarted_ = false;
  bool finishRequested_ = false;
  bool displayDirty_ = true;
  bool configChanged_ = false;
  bool routesRegistered_ = false, scanRunning_ = false;
};

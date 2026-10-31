#pragma once
#include <Arduino.h>
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <freertos/semphr.h>

// Temporary resolver retry, exclusively for failed public HTTPS requests.
// All LwIP DNS mutations execute on its own TCP/IP thread. DHCP is untouched.
class HttpsDnsRetry {
 public:
  HttpsDnsRetry() {
    context_.done = xSemaphoreCreateBinary();
    if (!context_.done) return;
    if (tcpip_callback(change, &context_) != ERR_OK) return;
    xSemaphoreTake(context_.done, portMAX_DELAY);
    active_ = true;
  }
  ~HttpsDnsRetry() {
    if (active_) {
      context_.restore = true;
      // tcpip_callback posts with a blocking mailbox. Retry allocation failure
      // rather than leaving pointers to this stack object in the TCP/IP queue.
      while (tcpip_callback(change, &context_) != ERR_OK) delay(1);
      xSemaphoreTake(context_.done, portMAX_DELAY);
    }
    if (context_.done) vSemaphoreDelete(context_.done);
  }
  bool active() const { return active_; }
  HttpsDnsRetry(const HttpsDnsRetry &) = delete;
  HttpsDnsRetry &operator=(const HttpsDnsRetry &) = delete;
 private:
  struct Context {
    ip_addr_t saved[2];
    SemaphoreHandle_t done = nullptr;
    bool restore = false;
  } context_;
  bool active_ = false;
  static void change(void *argument) {
    Context &context = *static_cast<Context *>(argument);
    for (uint8_t i = 0; i < 2; ++i) {
      if (context.restore) dns_setserver(i, &context.saved[i]);
      else {
        context.saved[i] = *dns_getserver(i);
        ip_addr_t fallback;
        IP_ADDR4(&fallback, i == 0 ? 1 : 8, i == 0 ? 1 : 8,
                 i == 0 ? 1 : 8, i == 0 ? 1 : 8);
        dns_setserver(i, &fallback);
      }
    }
    dns_clear_cache();
    xSemaphoreGive(context.done);
  }
};

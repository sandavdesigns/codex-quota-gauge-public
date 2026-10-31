#include "provisioning_validation.h"
#include <assert.h>
#include <stdio.h>
using namespace gauge_validation;
int main() {
  size_t checks = 0;
  const auto check = [&](bool condition) {
    ++checks;
    if (!condition) fprintf(stderr, "validation check %zu failed\n", checks);
    assert(condition);
  };
  const char *validUrls[] = {"http://192.168.1.50:8787/v1/quota", "http://10.0.0.1/v1/quota",
      "http://172.16.0.2:1/v1/quota", "http://172.31.255.1:65535/v1/quota",
      "http://gauge-server:8787/v1/quota", "http://nas.home.arpa/v1/quota",
      "http://server.local/v1/quota", "http://server.lan/v1/quota",
      "http://100.64.0.1:8787/v1/quota", "http://100.127.255.254/v1/quota",
      "https://gauge.example.org/v1/quota", "https://api.example.com:8443/v1/quota"};
  for (const char *url : validUrls) {
    check(validateEndpoint(url, strlen(url)) == UrlError::None);
  }
  const char *invalidUrls[] = {"", "https://192.168.1.2/v1/quota", "ftp://192.168.1.2/v1/quota",
      "http://", "http:///v1/quota", "http://192.168.1.1:0/v1/quota",
      "http://192.168.1.1:65536/v1/quota", "http://192.168.1.1:bad/v1/quota",
      "http://192.168.1.1:/v1/quota", "http://user:secret@192.168.1.1/v1/quota",
      "http://192.168.1.1/v1/quota?token=secret", "http://192.168.1.1/v1/quota#fragment",
      "http://192.168.1.1/space here", "http://192.168.1.1/a\r\nX-Header:value",
      "http://8.8.8.8/v1/quota", "http://127.0.0.1/v1/quota", "http://0.0.0.0/v1/quota",
      "http://172.32.0.1/v1/quota", "http://example.com/v1/quota",
      "http://192.168.1.256/v1/quota", "http://-invalid.local/v1/quota",
      "http://invalid-.local/v1/quota", "http://invalid..local/v1/quota",
      "http://[::1]:8787/v1/quota", "http://localhost:8787/v1/quota",
      "http://100.63.255.1/v1/quota", "http://100.128.0.1/v1/quota"};
  for (const char *url : invalidUrls) {
    check(validateEndpoint(url, strlen(url)) != UrlError::None);
  }
  char longUrl[240]; memset(longUrl, 'a',sizeof(longUrl));
  memcpy(longUrl,"http://192.168.1.1/",18);
  check(validateEndpoint(longUrl, sizeof(longUrl)) == UrlError::TooLong);
  const char nulUrl[] = "http://192.168.1.1/\0evil";
  check(validateEndpoint(nulUrl, sizeof(nulUrl)-1) != UrlError::None);
  UrlParts parts{};
  const char *url = "http://nas.local:8787/v1/quota";
  check(validateEndpoint(url, strlen(url), &parts) == UrlError::None);
  check(parts.hostLength == 9 && memcmp(url+parts.hostOffset,"nas.local",9) == 0);
  check(parts.port == 8787 && parts.explicitPort);
  check(!parts.secure);
  const char *secureUrl = "https://gauge.example.org/v1/quota";
  check(validateEndpoint(secureUrl, strlen(secureUrl), &parts) == UrlError::None);
  check(parts.secure && parts.port == 443 && !parts.explicitPort);
  check(validDeviceToken("", 0));
  check(validDeviceToken("0123456789abcdef0123456789ABCDEF", 32));
  check(validDeviceToken("key_with-dashes_0123456789abcdef", 32));
  check(!validDeviceToken("too-short", 9));
  check(!validDeviceToken("0123456789abcdef0123456789abcde!", 32));
  check(!validSsid("", 0));
  check(validSsid("Work WiFi", 9));
  check(validSsid("A<&\"B", 5)); // Escaping belongs to the HTML renderer, not SSID rejection.
  char ssidBuffer[33]; memset(ssidBuffer, 's',sizeof(ssidBuffer));
  check(validSsid(ssidBuffer, 32));
  check(!validSsid(ssidBuffer, 33));
  check(!validSsid("bad\nname", 8));
  const char nulSsid[] = {'a',0,'b'};
  check(!validSsid(nulSsid, sizeof(nulSsid)));
  check(validWifiPassword("",0)); // Open networks have no PSK; browser login is not implemented.
  check(!validWifiPassword("short",5));
  check(validWifiPassword("12345678",8));
  char passBuffer[64]; memset(passBuffer,'p',sizeof(passBuffer));
  check(validWifiPassword(passBuffer,63));
  check(!validWifiPassword(passBuffer,64));
  check(!validWifiPassword("bad\nword",8));
  check(!validWifiPassword("bad\tword",8));
  char built[201] = {};
  size_t builtLength = 0;
  check(buildQuotaEndpoint("192.168.1.50",12,"8787",4,built,sizeof(built),&builtLength) == UrlError::None);
  check(strcmp(built,"http://192.168.1.50:8787/v1/quota") == 0);
  check(builtLength == strlen(built));
  check(buildQuotaEndpoint("nas.lan",7,"8088",4,built,sizeof(built)) == UrlError::None);
  check(strcmp(built,"http://nas.lan:8088/v1/quota") == 0);
  check(buildQuotaEndpoint("gauge.example.org",strlen("gauge.example.org"),"443",3,built,sizeof(built),&builtLength,true) == UrlError::None);
  check(strcmp(built,"https://gauge.example.org:443/v1/quota") == 0);
  check(buildQuotaEndpoint("192.168.1.50",12,"443",3,built,sizeof(built),nullptr,true) != UrlError::None);
  check(buildQuotaEndpoint("example.com",11,"80",2,built,sizeof(built)) != UrlError::None);
  const char *badHosts[] = {"", "http://192.168.1.2", "192.168.1.2/path", "user@nas.lan", "nas.lan:8787", "8.8.8.8", "localhost"};
  for (const char *host : badHosts)
    check(buildQuotaEndpoint(host,strlen(host),"8787",4,built,sizeof(built)) != UrlError::None);
  const char *badPorts[] = {"", "0", "65536", "-1", "80x", " 80", "123456"};
  for (const char *port : badPorts)
    check(buildQuotaEndpoint("nas.lan",7,port,strlen(port),built,sizeof(built)) != UrlError::None);
  check(buildQuotaEndpoint("nas.lan",7,"1",1,built,sizeof(built)) == UrlError::None);
  check(buildQuotaEndpoint("nas.lan",7,"65535",5,built,sizeof(built)) == UrlError::None);
  char tiny[8] = "guard";
  check(buildQuotaEndpoint("nas.lan",7,"8787",4,tiny,sizeof(tiny)) == UrlError::TooLong);
  check(strcmp(tiny,"guard") == 0);
  char restoredHost[64];uint16_t restoredPort = 0;
  const char *legacy = "http://192.168.1.50:8088/wrong-path";
  check(splitQuotaEndpoint(legacy,strlen(legacy),restoredHost,sizeof(restoredHost),&restoredPort));
  check(strcmp(restoredHost,"192.168.1.50") == 0 && restoredPort == 8088);
  const char *implicitPort = "http://nas.lan/wrong-path";
  check(splitQuotaEndpoint(implicitPort,strlen(implicitPort),restoredHost,sizeof(restoredHost),&restoredPort));
  check(strcmp(restoredHost,"nas.lan") == 0 && restoredPort == 80);
  check(!splitQuotaEndpoint(legacy,strlen(legacy),tiny,sizeof(tiny),&restoredPort));
  check(!splitQuotaEndpoint("invalid",7,restoredHost,sizeof(restoredHost),&restoredPort));
  bool restoredSecure = false;
  check(splitQuotaEndpoint(secureUrl,strlen(secureUrl),restoredHost,sizeof(restoredHost),
                           &restoredPort,&restoredSecure));
  check(restoredSecure && restoredPort == 443 && strcmp(restoredHost,"gauge.example.org") == 0);
  printf("%zu provisioning validation checks passed\n", checks);
}

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Header-only and Arduino-independent so host-side tests can include it directly.
namespace gauge_validation {

enum class UrlError : uint8_t { None, Empty, TooLong, Scheme, ForbiddenCharacter, UserInfo, Host, Port, NotTrustedLan };

struct UrlParts {
  size_t hostOffset;
  size_t hostLength;
  uint16_t port;
  bool explicitPort;
  bool secure;
};

inline bool validDeviceToken(const char *value, size_t length) {
  if (value == nullptr) return false;
  if (length == 0) return true;
  if (length < 32 || length > 128) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = value[i];
    const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!valid) return false;
  }
  return true;
}

inline bool isAsciiSpaceOrControl(char c) {
  const unsigned char value = static_cast<unsigned char>(c);
  return value <= 0x20 || value == 0x7f;
}

inline bool validSsid(const char *value, size_t length) {
  if (value == nullptr || length == 0 || length > 32) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (c == 0 || c < 0x20 || c == 0x7f) return false;
  }
  return true;
}

// Empty permits an open WLAN. WPA/WPA2 personal passphrases are 8..63 bytes.
inline bool validWifiPassword(const char *value, size_t length) {
  if (value == nullptr || (length != 0 && (length < 8 || length > 63))) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

inline bool parseIpv4(const char *host, size_t length, uint8_t octets[4]) {
  size_t cursor = 0;
  for (size_t part = 0; part < 4; ++part) {
    if (cursor >= length) return false;
    unsigned value = 0;
    size_t digits = 0;
    while (cursor < length && host[cursor] >= '0' && host[cursor] <= '9') {
      value = value * 10U + static_cast<unsigned>(host[cursor++] - '0');
      if (++digits > 3 || value > 255) return false;
    }
    if (digits == 0) return false;
    octets[part] = static_cast<uint8_t>(value);
    if (part < 3 && (cursor >= length || host[cursor++] != '.')) return false;
  }
  return cursor == length;
}

inline bool validHostname(const char *host, size_t length) {
  if (length == 0 || length > 253 || host[0] == '.' || host[length - 1] == '.') return false;
  size_t labelLength = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = host[i];
    if (c == '.') {
      if (labelLength == 0 || labelLength > 63 || host[i - 1] == '-') return false;
      labelLength = 0;
      continue;
    }
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9');
    if ((!alnum && c != '-') || (labelLength == 0 && c == '-')) return false;
    ++labelLength;
  }
  return labelLength > 0 && labelLength <= 63 && host[length - 1] != '-';
}

inline bool endsWithIgnoreCase(const char *value, size_t length, const char *suffix) {
  const size_t suffixLength = strlen(suffix);
  if (length < suffixLength) return false;
  const size_t offset = length - suffixLength;
  for (size_t i = 0; i < suffixLength; ++i) {
    char a = value[offset + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
    if (a != b) return false;
  }
  return true;
}

inline bool equalsIgnoreCase(const char *value, size_t length, const char *expected) {
  return length == strlen(expected) && endsWithIgnoreCase(value, length, expected);
}

inline bool isTrustedLanHost(const char *host, size_t length) {
  uint8_t ip[4];
  if (parseIpv4(host, length, ip)) {
    return ip[0] == 10 || (ip[0] == 100 && ip[1] >= 64 && ip[1] <= 127) ||
           (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
           (ip[0] == 192 && ip[1] == 168) || (ip[0] == 169 && ip[1] == 254);
  }
  if (!validHostname(host, length)) return false;
  if (equalsIgnoreCase(host, length, "localhost")) return false;
  bool hasDot = false;
  for (size_t i = 0; i < length; ++i) hasDot = hasDot || host[i] == '.';
  return !hasDot || endsWithIgnoreCase(host, length, ".local") ||
         endsWithIgnoreCase(host, length, ".lan") ||
         endsWithIgnoreCase(host, length, ".home.arpa");
}

inline UrlError validateEndpoint(const char *value, size_t length, UrlParts *parts = nullptr) {
  if (value == nullptr || length == 0) return UrlError::Empty;
  if (length > 200) return UrlError::TooLong;
  static const char httpPrefix[] = "http://";
  static const char httpsPrefix[] = "https://";
  const bool secure = length > sizeof(httpsPrefix) - 1 &&
                      memcmp(value, httpsPrefix, sizeof(httpsPrefix) - 1) == 0;
  const bool plain = length > sizeof(httpPrefix) - 1 &&
                     memcmp(value, httpPrefix, sizeof(httpPrefix) - 1) == 0;
  if (!secure && !plain) return UrlError::Scheme;
  for (size_t i = 0; i < length; ++i) {
    if (isAsciiSpaceOrControl(value[i]) || value[i] == '?' || value[i] == '#') return UrlError::ForbiddenCharacter;
  }
  const size_t authorityStart = secure ? sizeof(httpsPrefix) - 1 : sizeof(httpPrefix) - 1;
  size_t authorityEnd = authorityStart;
  while (authorityEnd < length && value[authorityEnd] != '/') ++authorityEnd;
  if (authorityEnd == authorityStart) return UrlError::Host;
  for (size_t i = authorityStart; i < authorityEnd; ++i) if (value[i] == '@') return UrlError::UserInfo;

  size_t hostEnd = authorityEnd;
  size_t colon = authorityEnd;
  for (size_t i = authorityStart; i < authorityEnd; ++i) {
    if (value[i] == ':') {
      if (colon != authorityEnd) return UrlError::Host;  // IPv6 intentionally unsupported.
      colon = i;
      hostEnd = i;
    }
  }
  uint8_t ignored[4];
  if (hostEnd == authorityStart ||
      (!validHostname(value + authorityStart, hostEnd - authorityStart) &&
       !parseIpv4(value + authorityStart, hostEnd - authorityStart, ignored))) return UrlError::Host;

  uint16_t port = secure ? 443 : 80;
  const bool explicitPort = colon != authorityEnd;
  if (explicitPort) {
    if (colon + 1 == authorityEnd) return UrlError::Port;
    uint32_t parsedPort = 0;
    for (size_t i = colon + 1; i < authorityEnd; ++i) {
      if (value[i] < '0' || value[i] > '9') return UrlError::Port;
      parsedPort = parsedPort * 10U + static_cast<uint32_t>(value[i] - '0');
      if (parsedPort > 65535U) return UrlError::Port;
    }
    if (parsedPort == 0) return UrlError::Port;
    port = static_cast<uint16_t>(parsedPort);
  }
  if (secure) {
    // Public TLS endpoints require a DNS name so certificate host verification is meaningful.
    const size_t hostLength = hostEnd - authorityStart;
    bool hasDot = false;
    for (size_t i = 0; i < hostLength; ++i) hasDot = hasDot || value[authorityStart + i] == '.';
    uint8_t secureIp[4];
    if (!validHostname(value + authorityStart, hostLength) ||
        parseIpv4(value + authorityStart, hostLength, secureIp) || !hasDot ||
        equalsIgnoreCase(value + authorityStart, hostLength, "localhost") ||
        endsWithIgnoreCase(value + authorityStart, hostLength, ".local") ||
        endsWithIgnoreCase(value + authorityStart, hostLength, ".lan") ||
        endsWithIgnoreCase(value + authorityStart, hostLength, ".home.arpa")) {
      return UrlError::NotTrustedLan;
    }
  } else if (!isTrustedLanHost(value + authorityStart, hostEnd - authorityStart)) {
    return UrlError::NotTrustedLan;
  }
  if (parts != nullptr) {
    parts->hostOffset = authorityStart;
    parts->hostLength = hostEnd - authorityStart;
    parts->port = port;
    parts->explicitPort = explicitPort;
    parts->secure = secure;
  }
  return UrlError::None;
}

inline UrlError buildQuotaEndpoint(const char *host, size_t hostLength,
                                   const char *portText, size_t portLength,
                                   char *output, size_t outputCapacity,
                                   size_t *outputLength = nullptr, bool secure = false) {
  if (host == nullptr || hostLength == 0 || output == nullptr) return UrlError::Host;
  if (portText == nullptr || portLength == 0 || portLength > 5) return UrlError::Port;
  for (size_t i = 0; i < hostLength; ++i) {
    const char c = host[i];
    if (isAsciiSpaceOrControl(c) || c == ':' || c == '/' || c == '@' || c == '?' || c == '#') {
      return c == '@' ? UrlError::UserInfo : UrlError::Host;
    }
  }
  uint8_t ignored[4];
  bool hasDot = false;
  for (size_t i = 0; i < hostLength; ++i) hasDot = hasDot || host[i] == '.';
  const bool hostAllowed = secure
      ? validHostname(host, hostLength) && !parseIpv4(host, hostLength, ignored) && hasDot &&
            !equalsIgnoreCase(host, hostLength, "localhost") &&
            !endsWithIgnoreCase(host, hostLength, ".local") &&
            !endsWithIgnoreCase(host, hostLength, ".lan") &&
            !endsWithIgnoreCase(host, hostLength, ".home.arpa")
      : (validHostname(host, hostLength) || parseIpv4(host, hostLength, ignored)) &&
            isTrustedLanHost(host, hostLength);
  if (!hostAllowed) {
    return UrlError::NotTrustedLan;
  }
  uint32_t port = 0;
  for (size_t i = 0; i < portLength; ++i) {
    if (portText[i] < '0' || portText[i] > '9') return UrlError::Port;
    port = port * 10U + static_cast<uint32_t>(portText[i] - '0');
  }
  if (port == 0 || port > 65535U) return UrlError::Port;
  static const char httpPrefix[] = "http://";
  static const char httpsPrefix[] = "https://";
  const char *prefix = secure ? httpsPrefix : httpPrefix;
  const size_t prefixLength = secure ? sizeof(httpsPrefix) - 1 : sizeof(httpPrefix) - 1;
  static const char suffix[] = "/v1/quota";
  const size_t length = prefixLength + hostLength + 1 + portLength + sizeof(suffix) - 1;
  if (length > 200) return UrlError::TooLong;
  if (outputCapacity <= length) return UrlError::TooLong;
  size_t cursor = 0;
  memcpy(output + cursor, prefix, prefixLength);
  cursor += prefixLength;
  memcpy(output + cursor, host, hostLength);
  cursor += hostLength;
  output[cursor++] = ':';
  memcpy(output + cursor, portText, portLength);
  cursor += portLength;
  memcpy(output + cursor, suffix, sizeof(suffix) - 1);
  cursor += sizeof(suffix) - 1;
  output[cursor] = '\0';
  if (outputLength != nullptr) *outputLength = cursor;
  return validateEndpoint(output, cursor);
}

inline bool splitQuotaEndpoint(const char *endpoint, size_t length,
                               char *host, size_t hostCapacity, uint16_t *port,
                               bool *secure = nullptr) {
  if (endpoint == nullptr || host == nullptr || port == nullptr) return false;
  UrlParts parts{};
  if (validateEndpoint(endpoint, length, &parts) != UrlError::None) return false;
  // Migration intentionally accepts any otherwise valid legacy path. The builder
  // always replaces it with the fixed /v1/quota path on the next save.
  if (hostCapacity <= parts.hostLength) return false;
  memcpy(host, endpoint + parts.hostOffset, parts.hostLength);
  host[parts.hostLength] = '\0';
  *port = parts.port;
  if (secure != nullptr) *secure = parts.secure;
  return true;
}

}  // namespace gauge_validation

#pragma once

// Optional factory defaults only. The device profile builds without config.h;
// normal setup happens from a phone through the temporary device WLAN.
// Never commit a config.h that contains real credentials.
#define GAUGE_INITIAL_WIFI_SSID ""
#define GAUGE_INITIAL_WIFI_PASSWORD ""
#define GAUGE_INITIAL_ENDPOINT_URL "http://192.168.1.50:8787/v1/quota"

// This service is intended for the trusted local network. Do not put an OpenAI
// API token or any other secret into this file, URL, or firmware image.

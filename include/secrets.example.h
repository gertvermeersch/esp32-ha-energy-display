#pragma once

// Copy this file to secrets.h and fill in your details.
// secrets.h is gitignored; never commit real credentials.

#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-wifi-password"

// Use the IP address: .local names don't always resolve on the ESP32.
#define HA_BASE_URL   "http://192.168.1.10:8123"

// Home Assistant > your profile > Security > Long-lived access tokens
#define HA_TOKEN      "paste-your-long-lived-access-token-here"

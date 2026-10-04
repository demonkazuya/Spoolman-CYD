#include "WifiManager.h"

#include <Preferences.h>
#include <WiFi.h>

namespace cyd_wifi {
namespace {
Preferences preferences;
State currentState = State::NoCredentials;
String currentSsid;
String pendingPassword;
String failureMessage;
bool saveOnConnect = false;
uint32_t connectionStarted = 0;
constexpr uint32_t kConnectTimeoutMs = 30000;

void connectSaved() {
  currentSsid = preferences.getString("ssid", "");
  const String password = preferences.getString("password", "");
  if (currentSsid.isEmpty()) {
    currentState = State::NoCredentials;
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(currentSsid.c_str(), password.c_str());
  pendingPassword = password;
  saveOnConnect = false;
  connectionStarted = millis();
  failureMessage = "";
  currentState = State::Connecting;
}
}  // namespace

void begin() {
  preferences.begin("spoolmansync", false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  connectSaved();
}

void poll() {
  if (currentState == State::Connected && WiFi.status() != WL_CONNECTED) {
    currentState = State::Connecting;
    connectionStarted = millis();
    failureMessage = "Wi-Fi disconnected. Reconnecting...";
    Serial.println("Wi-Fi link lost; waiting for automatic reconnection.");
  }
  if (currentState != State::Connecting) return;
  if (WiFi.status() == WL_CONNECTED) {
    currentState = State::Connected;
    if (saveOnConnect) {
      preferences.putString("ssid", currentSsid);
      preferences.putString("password", pendingPassword);
    }
    pendingPassword = "";
    saveOnConnect = false;
    Serial.printf("Wi-Fi connected: %s (%s)\n", currentSsid.c_str(),
                  WiFi.localIP().toString().c_str());
  } else if (millis() - connectionStarted >= kConnectTimeoutMs) {
    currentState = State::Failed;
    const wl_status_t status = WiFi.status();
    if (status == WL_NO_SSID_AVAIL) {
      failureMessage = "Network not found. Scan and select it again.";
    } else if (status == WL_CONNECT_FAILED) {
      failureMessage = "Connection rejected. Check the Wi-Fi password.";
    } else {
      failureMessage = "Connection timed out. Check the password and signal.";
    }
    pendingPassword = "";
    WiFi.disconnect();
    Serial.printf("Wi-Fi connection failed: %s\n", failureMessage.c_str());
  }
}

bool saveCredentials(const String &ssid, const String &password) {
  if (ssid.isEmpty()) return false;
  WiFi.disconnect(true, false);
  currentSsid = ssid;
  pendingPassword = password;
  saveOnConnect = true;
  failureMessage = "";
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), password.c_str());
  connectionStarted = millis();
  currentState = State::Connecting;
  return true;
}

State state() { return currentState; }
bool connected() { return currentState == State::Connected; }
bool hasSavedCredentials() {
  return !preferences.getString("ssid", "").isEmpty();
}
String savedSsid() { return currentSsid; }

const char *stateText() {
  switch (currentState) {
    case State::NoCredentials: return "Wi-Fi not configured";
    case State::Connecting: return "Connecting to Wi-Fi...";
    case State::Connected: return "Wi-Fi connected";
    case State::Failed: return failureMessage.isEmpty()
                                  ? "Wi-Fi connection failed. Check the password."
                                  : failureMessage.c_str();
  }
  return "Wi-Fi status unknown";
}

}  // namespace cyd_wifi

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdint>

#include "Config.h"
#include "network/WifiManager.h"
#include "network/ServerConfig.h"
#include "network/DisplayConfig.h"
#include "ThemeConfig.h"
#include "FirmwareVersion.h"
#include "OtaUpdater.h"
#include "api/SpoolmanSyncClient.h"

namespace {

constexpr int kPortraitWidth = 240;
constexpr int kPortraitHeight = 320;
constexpr int kLandscapeWidth = 320;
constexpr int kLandscapeHeight = 240;
int kDisplayWidth = kPortraitWidth;
int kDisplayHeight = kPortraitHeight;
bool landscapeMode = false;
bool setupScreenActive = false;
bool initializingScreenActive = false;
bool firstSetupPendingServer = false;
constexpr int kTouchIrq = 36;
constexpr int kTouchMosi = 32;
constexpr int kTouchMiso = 39;
constexpr int kTouchClock = 25;
constexpr int kTouchCs = 33;
constexpr int kBacklight = 21;
constexpr int kDrawBufferRows = 20;

uint32_t kColorBackground = 0x151515;
uint32_t kColorPanel = 0x252525;
uint32_t kColorAccent = 0xFF7A1A;
uint32_t kColorSecondary = 0xB74700;
uint32_t kColorText = 0xF5F5F5;
uint32_t kColorMuted = 0xB8B8B8;

void applyThemeColors() {
  if (theme_config::lightMode()) {
    kColorBackground = 0xF5F5F5;
    kColorPanel = 0xFFFFFF;
    kColorAccent = 0x167A45;
    kColorSecondary = 0x176B42;
    kColorText = 0x242424;
    kColorMuted = 0x5F5F5F;
  } else {
    kColorBackground = 0x101010;
    kColorPanel = 0x242424;
    kColorAccent = 0x35D07F;
    kColorSecondary = 0x176B42;
    kColorText = 0xF5F5F5;
    kColorMuted = 0xB8B8B8;
  }
}

SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(kTouchCs, kTouchIrq);
TFT_eSPI display;

lv_disp_draw_buf_t drawBuffer;
lv_color_t drawPixels[kLandscapeWidth * kDrawBufferRows];
lv_obj_t *currentScreen = nullptr;
lv_obj_t *settingsStatus = nullptr;
lv_obj_t *setupWifiStatus = nullptr;
lv_obj_t *networkList = nullptr;
lv_obj_t *wifiPageStatus = nullptr;
lv_obj_t *scanButton = nullptr;
lv_obj_t *passwordField = nullptr;
lv_obj_t *passwordKeyboard = nullptr;
lv_obj_t *connectButton = nullptr;
lv_obj_t *settingsOrientationButton = nullptr;
lv_obj_t *settingsServerButton = nullptr;
lv_obj_t *serverHostField = nullptr;
lv_obj_t *serverPortField = nullptr;
lv_obj_t *serverKeyboard = nullptr;
lv_obj_t *serverStatus = nullptr;
lv_obj_t *serverTestButton = nullptr;
lv_obj_t *otaStatus = nullptr;
lv_obj_t *otaButton = nullptr;
ota_updater::Manifest pendingOtaManifest;
bool otaUpdateAvailable = false;
String selectedSsid;
std::vector<spoolman_api::PrinterSummary> printerCache;
std::vector<spoolman_api::SpoolSummary> spoolCache;
std::vector<String> materialOptions;
size_t selectedPrinterIndex = 0;
size_t selectedAmsIndex = 0;
size_t selectedTrayIndex = 0;
size_t selectedExternalIndex = 0;
size_t selectedSpoolIndex = 0;
uint16_t spoolPageOffset = 0;
uint16_t spoolMatchCount = 0;
bool selectedExternalSlot = false;
bool selectedCurrentAssigned = false;
uint32_t selectedCurrentSpoolId = 0;
String selectedTrayUniqueId;
lv_obj_t *spoolSearchField = nullptr;
lv_obj_t *materialDropdown = nullptr;
lv_obj_t *spoolKeyboard = nullptr;
lv_obj_t *spoolList = nullptr;
lv_obj_t *spoolNavigationBar = nullptr;
lv_obj_t *spoolPreviousButton = nullptr;
lv_obj_t *spoolNextButton = nullptr;
lv_obj_t *assignmentStatusLabel = nullptr;
lv_obj_t *assignmentButton = nullptr;
lv_obj_t *unassignmentStatusLabel = nullptr;
lv_obj_t *unassignmentButton = nullptr;
String selectedDestination;
uint32_t selectedUnassignSpoolId = 0;
String selectedUnassignSpoolName;
void (*backHandler)() = nullptr;

uint32_t lastTouchLogMs = 0;

enum class Page : intptr_t {
  Home = 0,
  Printers = 1,
  Settings = 2,
};

void showHome();
void showSetup();
void showInitializing();
void showPrinters();
void showAms(size_t printerIndex);
void backToSelectedAms();
void showTrays(size_t printerIndex, size_t amsIndex, bool external,
               size_t externalIndex = 0);
void showSpoolPicker(bool reload);
void showSelectedSpool(size_t spoolIndex);
void showAssignmentSuccess(bool refreshed, const String &refreshError);
void showUnassignConfirmation();
void showUnassignSuccess(bool refreshed, const String &refreshError);
void requestUnassignEvent(lv_event_t *event);
void confirmUnassignEvent(lv_event_t *event);
void backToSelectedTrays();
void backToSpoolPicker();
void showSettings();
void showColorTestPage();
void showWifiNetworksPage();
void openSettingsEvent(lv_event_t *event);
void showServerSettings();
void showOtaPage();
void testAndSaveServerEvent(lv_event_t *event);
void changeOrientationEvent(lv_event_t *event);
void selectAmsEvent(lv_event_t *event);
void selectSpoolEvent(lv_event_t *event);
void chooseSpoolEvent(lv_event_t *event);
void assignSpoolEvent(lv_event_t *event);
void renderSpoolList();
void previousSpoolPageEvent(lv_event_t *event);
void nextSpoolPageEvent(lv_event_t *event);
void updateSpoolPager();
void shortcutAmsEvent(lv_event_t *event);
void shortcutPrintersEvent(lv_event_t *event);
lv_obj_t *addSpoolNavigationBar(lv_obj_t *screen);
void updateWifiStatus();
void buildNetworkList(int count);

void updateWifiStatus() {
  const String wifiText = cyd_wifi::stateText();
  const String ipText = cyd_wifi::connected()
                            ? "Device IP: " + WiFi.localIP().toString()
                            : "Device IP: not connected";
  if (setupWifiStatus != nullptr && lv_obj_is_valid(setupWifiStatus)) {
    String status = "Wi-Fi: " + wifiText + "\n" + ipText;
    if (status != lv_label_get_text(setupWifiStatus))
      lv_label_set_text(setupWifiStatus, status.c_str());
  }
  if (settingsStatus != nullptr && lv_obj_is_valid(settingsStatus)) {
    const String deviceIp = cyd_wifi::connected()
                                ? WiFi.localIP().toString()
                                : "Unavailable";
    const String spoolmanAddress = server_config::configured()
                                       ? server_config::host() + ":" + server_config::port()
                                       : "Not configured";
    const String status = "Device IP: " + deviceIp + "\nSpoolmanSync: " + spoolmanAddress;
    if (status != lv_label_get_text(settingsStatus))
      lv_label_set_text(settingsStatus, status.c_str());
  }
  if (cyd_wifi::state() == cyd_wifi::State::Failed &&
      !selectedSsid.isEmpty()) {
    if (wifiPageStatus != nullptr && lv_obj_is_valid(wifiPageStatus))
      lv_label_set_text(wifiPageStatus, wifiText.c_str());
    if (passwordField != nullptr && lv_obj_is_valid(passwordField))
      lv_obj_clear_flag(passwordField, LV_OBJ_FLAG_HIDDEN);
    if (passwordKeyboard != nullptr && lv_obj_is_valid(passwordKeyboard)) {
      lv_obj_clear_flag(passwordKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (passwordField != nullptr && lv_obj_is_valid(passwordField))
        lv_keyboard_set_textarea(passwordKeyboard, passwordField);
    }
    if (connectButton != nullptr && lv_obj_is_valid(connectButton)) {
      lv_obj_clear_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_state(connectButton, LV_STATE_DISABLED);
      lv_obj_t *label = lv_obj_get_child(connectButton, 0);
      if (label != nullptr) lv_label_set_text(label, "Try again");
    }
  }
}

void scanNetworksEvent(lv_event_t *event) {
  (void)event;
  if (networkList != nullptr && lv_obj_is_valid(networkList)) {
    lv_obj_clean(networkList);
    lv_obj_t *label = lv_label_create(networkList);
    lv_label_set_text(label, "Scanning nearby networks...");
  }
  WiFi.mode(WIFI_STA);
  // Synchronous mode avoids the ESP32 async scan state getting stuck between
  // scans. This takes a few seconds, then the screen is updated with results.
  const int scanResult = WiFi.scanNetworks(false, true);
  Serial.printf("Wi-Fi scan completed, result=%d\n", scanResult);
  if (scanResult >= 0) {
    buildNetworkList(scanResult);
    WiFi.scanDelete();
  } else if (networkList != nullptr && lv_obj_is_valid(networkList)) {
    lv_obj_clean(networkList);
    lv_obj_t *label = lv_label_create(networkList);
    lv_label_set_text_fmt(label, "Wi-Fi scan failed (%d). Tap Scan again.", scanResult);
  }
}

void chooseNetworkEvent(lv_event_t *event) {
  lv_obj_t *button = lv_event_get_target(event);
  lv_obj_t *label = lv_obj_get_child(button, 0);
  selectedSsid = lv_label_get_text(label);
  if (wifiPageStatus != nullptr && lv_obj_is_valid(wifiPageStatus)) {
    lv_label_set_text_fmt(wifiPageStatus, "Selected: %s", selectedSsid.c_str());
    lv_obj_clear_flag(wifiPageStatus, LV_OBJ_FLAG_HIDDEN);
  }
  if (networkList != nullptr && lv_obj_is_valid(networkList))
    lv_obj_add_flag(networkList, LV_OBJ_FLAG_HIDDEN);
  if (scanButton != nullptr && lv_obj_is_valid(scanButton))
    lv_obj_add_flag(scanButton, LV_OBJ_FLAG_HIDDEN);
  if (passwordField != nullptr && lv_obj_is_valid(passwordField)) {
    lv_obj_clear_flag(passwordField, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(passwordField, "");
    lv_textarea_set_placeholder_text(passwordField, "Password for selected Wi-Fi");
    lv_obj_clear_flag(passwordKeyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(passwordKeyboard, passwordField);
    lv_obj_clear_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
  }
  if (connectButton != nullptr && lv_obj_is_valid(connectButton)) {
    lv_obj_t *connectLabel = lv_obj_get_child(connectButton, 0);
    if (connectLabel != nullptr) lv_label_set_text(connectLabel, "Connect");
  }
}

void connectWifiEvent(lv_event_t *event) {
  (void)event;
  if (!selectedSsid.isEmpty() && passwordField != nullptr) {
    const String password = lv_textarea_get_text(passwordField);
    if (cyd_wifi::saveCredentials(selectedSsid, password)) {
      if (passwordKeyboard != nullptr) lv_obj_add_flag(passwordKeyboard, LV_OBJ_FLAG_HIDDEN);
      if (passwordField != nullptr) lv_obj_add_flag(passwordField, LV_OBJ_FLAG_HIDDEN);
      if (connectButton != nullptr) lv_obj_add_flag(connectButton, LV_OBJ_FLAG_HIDDEN);
      updateWifiStatus();
    }
  }
}

void openServerSettingsEvent(lv_event_t *event) {
  (void)event;
  showServerSettings();
}

void openSettingsEvent(lv_event_t *event) {
  (void)event;
  showSettings();
}

void changeOrientationEvent(lv_event_t *event) {
  (void)event;
  if (!display_config::setLandscape(!landscapeMode)) return;
  Serial.printf("Display orientation changed to %s; restarting.\n",
                landscapeMode ? "portrait" : "landscape");
  ESP.restart();
}

void testAndSaveServerEvent(lv_event_t *event) {
  (void)event;
  const String host = lv_textarea_get_text(serverHostField);
  const String port = lv_textarea_get_text(serverPortField);
  IPAddress parsedAddress;
  if (!parsedAddress.fromString(host)) {
    lv_label_set_text(serverStatus, "Enter a valid IPv4 address.");
    return;
  }
  if (port.isEmpty()) {
    lv_label_set_text(serverStatus, "Enter a port from 1 to 65535.");
    return;
  }
  for (size_t i = 0; i < port.length(); ++i) {
    if (port[i] < '0' || port[i] > '9') {
      lv_label_set_text(serverStatus, "Port must contain digits only.");
      return;
    }
  }
  const long portNumber = port.toInt();
  if (portNumber < 1 || portNumber > 65535) {
    lv_label_set_text(serverStatus, "Enter a port from 1 to 65535.");
    return;
  }

  lv_label_set_text(serverStatus, "Testing SpoolmanSync API...");
  const String candidateUrl = "http://" + parsedAddress.toString() + ":" +
                              String(portNumber);
  String error;
  if (!spoolman_api::testConnection(candidateUrl, error)) {
    lv_label_set_text_fmt(serverStatus, "Not saved: %s", error.c_str());
    return;
  }
  server_config::save(parsedAddress.toString(), String(portNumber));
  if (firstSetupPendingServer) {
    firstSetupPendingServer = false;
    setupScreenActive = false;
    showPrinters();
    return;
  }
  lv_label_set_text(serverStatus, "API verified. Server address saved.");
}

void buildNetworkList(int count) {
  if (networkList == nullptr || !lv_obj_is_valid(networkList)) return;
  lv_obj_clean(networkList);
  if (count <= 0) {
    lv_obj_t *label = lv_label_create(networkList);
    lv_label_set_text(label, "Scan completed: no named networks found. Check router range and retry.");
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 205);
    Serial.printf("Wi-Fi scan completed: %d network records, none listed.\n", count);
    return;
  }
  int visibleCount = 0;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    Serial.printf("Wi-Fi scan result %d: SSID='%s', RSSI=%d dBm, auth=%d\n",
                  i, ssid.c_str(), WiFi.RSSI(i), WiFi.encryptionType(i));
    if (ssid.isEmpty()) continue;
    ++visibleCount;
    lv_obj_t *button = lv_btn_create(networkList);
    lv_obj_set_width(button, 208);
    lv_obj_set_height(button, 34);
    lv_obj_set_style_bg_color(button, lv_color_hex(kColorSecondary), LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(button);
    // Keep the button label equal to the SSID; the selection callback reads it.
    lv_label_set_text(label, ssid.c_str());
    lv_obj_set_width(label, 190);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, chooseNetworkEvent, LV_EVENT_CLICKED, nullptr);
  }
  if (visibleCount == 0) {
    lv_obj_t *label = lv_label_create(networkList);
    lv_label_set_text_fmt(label, "Found %d hidden network(s), but no visible SSIDs.", count);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, 205);
  }
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area,
                  lv_color_t *colors) {
  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);
  const uint32_t pixelCount = width * height;

  display.startWrite();
  display.setAddrWindow(area->x1, area->y1, width, height);
  if (display_config::redBlueSwapped()) {
    uint16_t *pixels = reinterpret_cast<uint16_t *>(colors);
    for (uint32_t i = 0; i < pixelCount; ++i) {
      const uint16_t color = pixels[i];
      pixels[i] = static_cast<uint16_t>(
          ((color & 0x001F) << 11) | (color & 0x07E0) | ((color & 0xF800) >> 11));
    }
    display.pushColors(pixels, pixelCount, true);
    for (uint32_t i = 0; i < pixelCount; ++i) {
      const uint16_t color = pixels[i];
      pixels[i] = static_cast<uint16_t>(
          ((color & 0x001F) << 11) | (color & 0x07E0) | ((color & 0xF800) >> 11));
    }
  } else {
    display.pushColors(reinterpret_cast<uint16_t *>(colors), pixelCount, true);
  }
  display.endWrite();

  lv_disp_flush_ready(driver);
}

int16_t mapTouchAxis(int16_t raw, int rawMin, int rawMax, int screenMax) {
  if (rawMax <= rawMin) return 0;
  const long bounded = constrain(raw, rawMin, rawMax);
  return static_cast<int16_t>(
      map(bounded, rawMin, rawMax, 0, screenMax - 1));
}

void readTouch(lv_indev_drv_t *driver, lv_indev_data_t *data) {
  (void)driver;

  if (touch.touched()) {
    const TS_Point point = touch.getPoint();
    data->state = LV_INDEV_STATE_PR;
    data->point.x = mapTouchAxis(point.x, cyd_config::kTouchRawXMin,
                                 cyd_config::kTouchRawXMax, kDisplayWidth);
    data->point.y = mapTouchAxis(point.y, cyd_config::kTouchRawYMin,
                                 cyd_config::kTouchRawYMax, kDisplayHeight);

    const uint32_t now = millis();
    if (now - lastTouchLogMs >= 350) {
      Serial.printf("Touch raw=(%d,%d) screen=(%d,%d) pressure=%d\n", point.x,
                    point.y, data->point.x, data->point.y, point.z);
      lastTouchLogMs = now;
    }
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

lv_obj_t *makeScreen() {
  lv_obj_t *screen = lv_obj_create(nullptr);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kColorBackground),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
  return screen;
}

void loadScreen(lv_obj_t *screen) {
  lv_obj_t *oldScreen = currentScreen;
  currentScreen = screen;
  lv_scr_load(screen);
  if (oldScreen != nullptr && oldScreen != screen) {
    lv_obj_del_async(oldScreen);
  }
}

void addHeader(lv_obj_t *screen, const char *title, bool showBack,
               bool showSettings = false, bool showRefresh = false) {
  lv_obj_t *header = lv_obj_create(screen);
  lv_obj_set_size(header, kDisplayWidth, landscapeMode ? 40 : 48);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(header, lv_color_hex(kColorPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(header, 12, LV_PART_MAIN);

  if (showBack) {
    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_size(back, 42, 36);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(kColorSecondary),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(back, 0, LV_PART_MAIN);
    lv_obj_t *backLabel = lv_label_create(back);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(
        back,
        [](lv_event_t *event) {
          (void)event;
          if (backHandler != nullptr) backHandler();
          else showHome();
        },
        LV_EVENT_CLICKED, nullptr);
  }

  if (showRefresh) {
    lv_obj_t *refresh = lv_btn_create(header);
    lv_obj_set_size(refresh, 38, 36);
    lv_obj_align(refresh, LV_ALIGN_RIGHT_MID, showSettings ? -42 : 0, 0);
    lv_obj_set_style_bg_color(refresh, lv_color_hex(kColorSecondary),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(refresh, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(refresh,
                        [](lv_event_t *event) {
                          (void)event;
                          showPrinters();
                        }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *refreshIcon = lv_label_create(refresh);
    lv_label_set_text(refreshIcon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(refreshIcon, &lv_font_montserrat_14,
                               LV_PART_MAIN);
    lv_obj_center(refreshIcon);
  }

  if (showSettings) {
    lv_obj_t *settings = lv_btn_create(header);
    lv_obj_set_size(settings, 38, 36);
    lv_obj_align(settings, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(settings, lv_color_hex(kColorSecondary),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(settings, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(settings, openSettingsEvent, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *settingsIcon = lv_label_create(settings);
    lv_label_set_text(settingsIcon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settingsIcon, &lv_font_montserrat_14,
                               LV_PART_MAIN);
    lv_obj_center(settingsIcon);
  }

  lv_obj_t *label = lv_label_create(header);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
  lv_obj_align(label, showBack ? LV_ALIGN_LEFT_MID : LV_ALIGN_CENTER, 0, 0);
  if (showBack) lv_obj_align(label, LV_ALIGN_LEFT_MID, 54, 0);
}

void navigationEvent(lv_event_t *event) {
  const auto page = static_cast<Page>(
      reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  switch (page) {
    case Page::Home:
      showHome();
      break;
    case Page::Printers:
      showPrinters();
      break;
    case Page::Settings:
      showSettings();
      break;
  }
}

lv_obj_t *addNavigationButton(lv_obj_t *screen, const char *text, int y,
                              Page destination, uint32_t color) {
  lv_obj_t *button = lv_btn_create(screen);
  lv_obj_set_size(button, 204, 54);
  lv_obj_align(button, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(
      button, navigationEvent, LV_EVENT_CLICKED,
      reinterpret_cast<void *>(static_cast<intptr_t>(destination)));

  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
  lv_obj_center(label);
  return button;
}

lv_obj_t *addBodyLabel(lv_obj_t *screen, const char *text, int y,
                       uint32_t color, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(screen);
  lv_label_set_text(label, text);
  lv_obj_set_width(label, kDisplayWidth - 32);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

void showHome() {
  initializingScreenActive = false;
  if (cyd_wifi::connected()) {
    if (!server_config::configured()) {
      firstSetupPendingServer = true;
      showServerSettings();
    } else {
      showPrinters();
    }
  } else if (cyd_wifi::hasSavedCredentials() &&
             cyd_wifi::state() == cyd_wifi::State::Connecting) {
    showInitializing();
  } else {
    showSetup();
  }
}

void showSetup() {
  setupScreenActive = true;
  initializingScreenActive = false;
  backHandler = nullptr;
  setupWifiStatus = nullptr;
  settingsStatus = nullptr;
  wifiPageStatus = nullptr;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "DEVICE SETUP", false);
  addBodyLabel(screen, "Connect to Wi-Fi to continue.",
               landscapeMode ? 64 : 80, kColorText,
               &lv_font_montserrat_16);
  setupWifiStatus = addBodyLabel(screen, "", landscapeMode ? 95 : 119,
                                 kColorAccent, &lv_font_montserrat_14);
  lv_obj_t *wifiButton = lv_btn_create(screen);
  lv_obj_set_size(wifiButton, 208, 36);
  lv_obj_align(wifiButton, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 165 : 188);
  lv_obj_set_style_bg_color(wifiButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(wifiButton,
                      [](lv_event_t *event) {
                        (void)event;
                        showWifiNetworksPage();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *wifiLabel = lv_label_create(wifiButton);
  lv_label_set_text(wifiLabel, "SCAN WI-FI NETWORKS");
  lv_obj_center(wifiLabel);
  updateWifiStatus();
  loadScreen(screen);
}

void showInitializing() {
  setupScreenActive = false;
  initializingScreenActive = true;
  backHandler = nullptr;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "SPOOLMAN SYNC", false);
  addBodyLabel(screen, "Initializing device...", landscapeMode ? 74 : 104,
               kColorText, &lv_font_montserrat_20);
  addBodyLabel(screen, "Connecting to saved Wi-Fi network.",
               landscapeMode ? 111 : 143, kColorAccent,
               &lv_font_montserrat_14);
  loadScreen(screen);
}

void addDataRow(lv_obj_t *list, const String &title, const String &detail,
                lv_event_cb_t callback, void *userData) {
  lv_obj_t *button = lv_btn_create(list);
  const int rowWidth = kDisplayWidth - 32;
  lv_obj_set_size(button, rowWidth, landscapeMode ? 52 : 58);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanel), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(button, 10, LV_PART_MAIN);
  if (callback != nullptr)
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, userData);

  lv_obj_t *titleLabel = lv_label_create(button);
  lv_label_set_text(titleLabel, title.c_str());
  lv_obj_set_width(titleLabel, rowWidth - 20);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 3);

  lv_obj_t *detailLabel = lv_label_create(button);
  lv_label_set_text(detailLabel, detail.c_str());
  lv_obj_set_width(detailLabel, rowWidth - 20);
  lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(detailLabel, lv_color_hex(kColorMuted), LV_PART_MAIN);
  lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(detailLabel, LV_ALIGN_BOTTOM_LEFT, 0, -2);
}

void addOccupiedTrayRow(lv_obj_t *list, const String &title,
                        const String &detail, lv_event_cb_t selectCallback,
                        void *trayIndex) {
  const int rowWidth = kDisplayWidth - 24;
  const int clearWidth = 56;
  lv_obj_t *row = lv_obj_create(list);
  lv_obj_set_size(row, rowWidth, landscapeMode ? 52 : 58);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  lv_obj_t *selectButton = lv_btn_create(row);
  lv_obj_set_size(selectButton, rowWidth - clearWidth - 4,
                  landscapeMode ? 50 : 56);
  lv_obj_set_style_bg_color(selectButton, lv_color_hex(kColorPanel), LV_PART_MAIN);
  lv_obj_set_style_border_width(selectButton, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(selectButton, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(selectButton, 8, LV_PART_MAIN);
  lv_obj_add_event_cb(selectButton, selectCallback, LV_EVENT_CLICKED, trayIndex);
  lv_obj_t *titleLabel = lv_label_create(selectButton);
  lv_label_set_text(titleLabel, title.c_str());
  lv_obj_set_width(titleLabel, rowWidth - clearWidth - 22);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 3);
  lv_obj_t *detailLabel = lv_label_create(selectButton);
  lv_label_set_text(detailLabel, detail.c_str());
  lv_obj_set_width(detailLabel, rowWidth - clearWidth - 22);
  lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(detailLabel, lv_color_hex(kColorMuted), LV_PART_MAIN);
  lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(detailLabel, LV_ALIGN_BOTTOM_LEFT, 0, -2);

  lv_obj_t *clearButton = lv_btn_create(row);
  lv_obj_set_size(clearButton, clearWidth, landscapeMode ? 42 : 46);
  lv_obj_set_style_bg_color(clearButton, lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_set_style_border_width(clearButton, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(clearButton, 7, LV_PART_MAIN);
  lv_obj_add_event_cb(clearButton, requestUnassignEvent, LV_EVENT_CLICKED,
                      trayIndex);
  lv_obj_t *clearLabel = lv_label_create(clearButton);
  lv_label_set_text(clearLabel, "Clear");
  lv_obj_set_style_text_font(clearLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(clearLabel);
}

void addSpoolResultRow(lv_obj_t *list, const String &title,
                       const String &detail, size_t spoolIndex) {
  lv_obj_t *button = lv_btn_create(list);
  const int rowWidth = kDisplayWidth - 32;
  lv_obj_set_size(button, rowWidth, landscapeMode ? 36 : 58);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanel), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 7, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(button, 8, LV_PART_MAIN);
  if (landscapeMode) lv_obj_set_style_pad_ver(button, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(button, chooseSpoolEvent, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(spoolIndex)));
  lv_obj_t *name = lv_label_create(button);
  lv_label_set_text(name, title.c_str());
  lv_obj_set_width(name, rowWidth - 18);
  lv_obj_set_height(name, landscapeMode ? 34 : 38);
  lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(name, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_set_style_text_font(name, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);
  if (!detail.isEmpty()) {
    lv_obj_t *details = lv_label_create(button);
    lv_label_set_text(details, detail.c_str());
    lv_obj_set_width(details, rowWidth - 18);
    lv_label_set_long_mode(details, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(details, lv_color_hex(kColorMuted), LV_PART_MAIN);
    lv_obj_set_style_text_font(details, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(details, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  } else {
    lv_obj_set_height(name, landscapeMode ? 34 : 54);
  }
}

String spoolIdentity(const spoolman_api::SpoolSummary &spool) {
  String identity = spool.name;
  if (!spool.material.isEmpty()) identity += " - " + spool.material;
  if (!spool.vendor.isEmpty()) identity += " - " + spool.vendor;
  identity += " - ID #" + String(spool.id);
  return identity;
}

String spoolWeight(const spoolman_api::SpoolSummary &spool) {
  return spool.remainingWeight >= 0
             ? String(spool.remainingWeight, 0) + "g remaining"
             : "Remaining weight unavailable";
}

lv_obj_t *addSpoolNavigationBar(lv_obj_t *screen) {
  lv_obj_t *bar = lv_obj_create(screen);
  const int barWidth = kDisplayWidth - 16;
  const int barHeight = landscapeMode ? 26 : 40;
  lv_obj_set_size(bar, barWidth, barHeight);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);

  lv_obj_t *amsButton = lv_btn_create(bar);
  const int navButtonWidth = (barWidth - 4) / 2;
  lv_obj_set_size(amsButton, navButtonWidth, landscapeMode ? 24 : 34);
  lv_obj_align(amsButton, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(amsButton, lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_set_style_border_width(amsButton, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(amsButton, shortcutAmsEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *amsLabel = lv_label_create(amsButton);
  lv_label_set_text(amsLabel, "AMS / TRAYS");
  lv_obj_set_style_text_font(amsLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(amsLabel);

  lv_obj_t *printersButton = lv_btn_create(bar);
  lv_obj_set_size(printersButton, navButtonWidth, landscapeMode ? 24 : 34);
  lv_obj_align(printersButton, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(printersButton, lv_color_hex(kColorPanel), LV_PART_MAIN);
  lv_obj_set_style_border_width(printersButton, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(printersButton, shortcutPrintersEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *printersLabel = lv_label_create(printersButton);
  lv_label_set_text(printersLabel, "PRINTERS");
  lv_obj_set_style_text_font(printersLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(printersLabel);
  return bar;
}

void addAmsRow(lv_obj_t *list, const spoolman_api::AmsSummary &ams,
               size_t index) {
  lv_obj_t *button = lv_btn_create(list);
  const int rowWidth = kDisplayWidth - 32;
  lv_obj_set_size(button, rowWidth, 64);
  lv_obj_set_style_bg_color(button, lv_color_hex(kColorPanel), LV_PART_MAIN);
  lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(button, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_hor(button, 10, LV_PART_MAIN);
  lv_obj_add_event_cb(button, selectAmsEvent, LV_EVENT_CLICKED,
                      reinterpret_cast<void *>(static_cast<intptr_t>(index)));

  String title = ams.name;
  if (title == "AMS" && ams.amsNumber > 0)
    title += " " + String(ams.amsNumber);
  lv_obj_t *titleLabel = lv_label_create(button);
  lv_label_set_text(titleLabel, title.c_str());
  lv_obj_set_width(titleLabel, rowWidth - 20);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_LEFT, 0, 2);

  String contents;
  for (const auto &tray : ams.trays) {
    if (!contents.isEmpty()) contents += "\n";
    contents += "Tray " + String(tray.trayNumber) + ": ";
    if (tray.assigned) {
      contents += tray.spoolName + " (#" + String(tray.spoolId) + ")";
    } else {
      contents += "Empty";
    }
  }
  if (ams.trays.empty()) contents = "No trays reported";

  lv_obj_t *detailLabel = lv_label_create(button);
  lv_label_set_text(detailLabel, contents.c_str());
  lv_obj_set_width(detailLabel, rowWidth - 20);
  lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_color(detailLabel, lv_color_hex(kColorMuted), LV_PART_MAIN);
  lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_align(detailLabel, LV_ALIGN_TOP_LEFT, 0, 22);
  lv_obj_update_layout(button);
  const int detailHeight = lv_obj_get_height(detailLabel);
  lv_obj_set_height(button, detailHeight + 30);
}

lv_obj_t *makeDataList(lv_obj_t *screen, int top = 58) {
  lv_obj_t *list = lv_obj_create(screen);
  lv_obj_set_size(list, kDisplayWidth - 16, kDisplayHeight - top - 8);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, top);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);
  return list;
}

void selectPrinterEvent(lv_event_t *event) {
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(
      lv_event_get_user_data(event)));
  showAms(index);
}

void selectAmsEvent(lv_event_t *event) {
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(
      lv_event_get_user_data(event)));
  showTrays(selectedPrinterIndex, index, false);
}

void selectExternalEvent(lv_event_t *event) {
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(
      lv_event_get_user_data(event)));
  showTrays(selectedPrinterIndex, 0, true, index);
}

void showPrinters() {
  setupScreenActive = false;
  backHandler = showHome;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "PRINTERS", false, true, true);
  lv_obj_t *list = makeDataList(screen);

  lv_obj_t *loading = lv_label_create(list);
  lv_label_set_text(loading, "Loading printers...");
  lv_obj_set_style_text_color(loading, lv_color_hex(kColorMuted), LV_PART_MAIN);
  loadScreen(screen);

  String error;
  if (!spoolman_api::getPrinters(printerCache, error)) {
    lv_obj_clean(list);
    lv_obj_t *message = lv_label_create(list);
    lv_label_set_text(message, error.c_str());
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message, 204);
    lv_obj_set_style_text_color(message, lv_color_hex(kColorAccent), LV_PART_MAIN);
    Serial.printf("Printer request failed: %s\n", error.c_str());
    return;
  }

  lv_obj_clean(list);
  if (printerCache.empty()) {
    lv_obj_t *message = lv_label_create(list);
    lv_label_set_text(message, "No printers returned by SpoolmanSync.");
    return;
  }
  for (size_t i = 0; i < printerCache.size(); ++i) {
    const auto &printer = printerCache[i];
    const String detail = String(printer.amsCount) + " AMS   " +
                          String(printer.trayCount) + " slots   " +
                          String(printer.loadedCount) + " loaded";
    addDataRow(list, printer.name, detail, selectPrinterEvent,
               reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
}

void showAms(size_t printerIndex) {
  if (printerIndex >= printerCache.size()) return;
  selectedPrinterIndex = printerIndex;
  backHandler = showPrinters;
  const auto &printer = printerCache[printerIndex];
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "AMS UNITS", true);
  addBodyLabel(screen, printer.name.c_str(), 51, kColorAccent,
               &lv_font_montserrat_14);
  lv_obj_t *list = makeDataList(screen, 76);
  loadScreen(screen);

  if (printer.amsUnits.empty() && printer.externalSpools.empty()) {
    lv_obj_t *empty = lv_label_create(list);
    lv_label_set_text(empty, "No AMS units or external slots found.");
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, 204);
    return;
  }
  for (size_t i = 0; i < printer.amsUnits.size(); ++i) {
    const auto &ams = printer.amsUnits[i];
    addAmsRow(list, ams, i);
  }
  for (size_t i = 0; i < printer.externalSpools.size(); ++i) {
    const auto &slot = printer.externalSpools[i];
    const String detail = slot.assigned
                              ? "External slot - " + slot.spoolName + " #" + String(slot.spoolId)
                              : "External spool slot - Empty";
    addDataRow(list, "External Spool", detail, selectExternalEvent,
               reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
}

void backToSelectedAms() { showAms(selectedPrinterIndex); }

void showTrays(size_t printerIndex, size_t amsIndex, bool external,
               size_t externalIndex) {
  if (printerIndex >= printerCache.size()) return;
  const auto &printer = printerCache[printerIndex];
  const std::vector<spoolman_api::TraySummary> *trays = nullptr;
  if (external) {
    if (externalIndex >= printer.externalSpools.size()) return;
  } else {
    if (amsIndex >= printer.amsUnits.size()) return;
    selectedAmsIndex = amsIndex;
    trays = &printer.amsUnits[amsIndex].trays;
  }
  selectedPrinterIndex = printerIndex;
  selectedAmsIndex = amsIndex;
  selectedExternalSlot = external;
  selectedExternalIndex = externalIndex;
  backHandler = backToSelectedAms;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, external ? "EXTERNAL SLOT" : "AMS TRAYS", true);
  const String context = external
                             ? printer.name
                             : printer.name + " / " + printer.amsUnits[amsIndex].name;
  addBodyLabel(screen, context.c_str(), 51, kColorAccent,
               &lv_font_montserrat_14);
  lv_obj_t *list = makeDataList(screen, 76);
  loadScreen(screen);

  if (external) {
    const auto &slot = printer.externalSpools[externalIndex];
    String detail = slot.assigned
                        ? slot.spoolName + "  ID #" + String(slot.spoolId)
                        : "Empty";
    if (!slot.material.isEmpty()) detail += " - " + slot.material;
    if (slot.assigned)
      addOccupiedTrayRow(list, "External Spool", detail, selectSpoolEvent,
                         nullptr);
    else
      addDataRow(list, "External Spool", detail, selectSpoolEvent, nullptr);
    return;
  }
  if (trays->empty()) {
    lv_obj_t *empty = lv_label_create(list);
    lv_label_set_text(empty, "This AMS has no trays.");
    return;
  }
  for (size_t i = 0; i < trays->size(); ++i) {
    const auto &tray = (*trays)[i];
    const String title = "Tray " + String(tray.trayNumber);
    String detail;
    if (tray.assigned) {
      detail = tray.spoolName + "  ID #" + String(tray.spoolId);
      if (tray.remainingWeight >= 0)
        detail += "  " + String(tray.remainingWeight, 0) + "g";
    } else {
      detail = "Empty";
      if (!tray.material.isEmpty() && tray.material != "Empty")
        detail += " - " + tray.material;
    }
    void *trayIndex = reinterpret_cast<void *>(static_cast<intptr_t>(i));
    if (tray.assigned)
      addOccupiedTrayRow(list, title, detail, selectSpoolEvent, trayIndex);
    else
      addDataRow(list, title, detail, selectSpoolEvent, trayIndex);
  }
}

bool containsIgnoreCase(String value, String query) {
  value.toLowerCase();
  query.toLowerCase();
  return value.indexOf(query) >= 0;
}

void renderSpoolList() {
  if (spoolList == nullptr || !lv_obj_is_valid(spoolList)) return;
  lv_obj_clean(spoolList);
  const String query = spoolSearchField != nullptr && lv_obj_is_valid(spoolSearchField)
                           ? lv_textarea_get_text(spoolSearchField)
                           : "";
  const uint16_t materialIndex =
      materialDropdown != nullptr && lv_obj_is_valid(materialDropdown)
          ? lv_dropdown_get_selected(materialDropdown)
          : 0;
  const String materialFilter = materialIndex < materialOptions.size()
                                    ? materialOptions[materialIndex]
                                    : "All materials";
  std::vector<size_t> matches;
  for (size_t i = 0; i < spoolCache.size(); ++i) {
    const auto &spool = spoolCache[i];
    if (materialFilter != "All materials" && spool.material != materialFilter)
      continue;
    String searchable = spool.name + " " + spool.material + " " + spool.vendor +
                        " " + String(spool.id);
    if (!query.isEmpty() && !containsIgnoreCase(searchable, query)) continue;
    matches.push_back(i);
  }
  spoolMatchCount = matches.size();
  constexpr size_t kSpoolsPerPage = 2;
  if (spoolPageOffset >= matches.size() && !matches.empty())
    spoolPageOffset = ((matches.size() - 1) / kSpoolsPerPage) * kSpoolsPerPage;
  const size_t end = std::min<size_t>(matches.size(), spoolPageOffset + kSpoolsPerPage);
  for (size_t pageIndex = spoolPageOffset; pageIndex < end; ++pageIndex) {
    const size_t i = matches[pageIndex];
    const auto &spool = spoolCache[i];
    addSpoolResultRow(spoolList, spoolIdentity(spool), "", i);
  }
  if (matches.empty()) {
    lv_obj_t *empty = lv_label_create(spoolList);
    lv_label_set_text(empty, "No spools match this search/filter.");
    lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(empty, 204);
  }
  updateSpoolPager();
}

void updateSpoolPager() {
  constexpr size_t kSpoolsPerPage = 2;
  if (spoolPreviousButton != nullptr && lv_obj_is_valid(spoolPreviousButton)) {
    if (spoolPageOffset == 0)
      lv_obj_add_state(spoolPreviousButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(spoolPreviousButton, LV_STATE_DISABLED);
  }
  if (spoolNextButton != nullptr && lv_obj_is_valid(spoolNextButton)) {
    if (spoolPageOffset + kSpoolsPerPage >= spoolMatchCount)
      lv_obj_add_state(spoolNextButton, LV_STATE_DISABLED);
    else
      lv_obj_clear_state(spoolNextButton, LV_STATE_DISABLED);
  }
}

void previousSpoolPageEvent(lv_event_t *event) {
  (void)event;
  constexpr size_t pageSize = 2;
  if (spoolPageOffset >= pageSize) spoolPageOffset -= pageSize;
  if (spoolList != nullptr && lv_obj_is_valid(spoolList))
    lv_obj_scroll_to_y(spoolList, 0, LV_ANIM_OFF);
  renderSpoolList();
}

void nextSpoolPageEvent(lv_event_t *event) {
  (void)event;
  constexpr size_t pageSize = 2;
  if (spoolPageOffset + pageSize < spoolMatchCount) spoolPageOffset += pageSize;
  if (spoolList != nullptr && lv_obj_is_valid(spoolList))
    lv_obj_scroll_to_y(spoolList, 0, LV_ANIM_OFF);
  renderSpoolList();
}

void backToSelectedTrays() {
  showTrays(selectedPrinterIndex, selectedAmsIndex, selectedExternalSlot,
            selectedExternalIndex);
}

void backToSpoolPicker() { showSpoolPicker(false); }

void shortcutAmsEvent(lv_event_t *event) {
  (void)event;
  if (selectedExternalSlot) {
    showAms(selectedPrinterIndex);
  } else {
    showTrays(selectedPrinterIndex, selectedAmsIndex, false);
  }
}

void shortcutPrintersEvent(lv_event_t *event) {
  (void)event;
  showPrinters();
}

void showSpoolPicker(bool reload) {
  backHandler = backToSelectedTrays;
  spoolSearchField = nullptr;
  materialDropdown = nullptr;
  spoolKeyboard = nullptr;
  spoolList = nullptr;
  spoolNavigationBar = nullptr;
  spoolPreviousButton = nullptr;
  spoolNextButton = nullptr;
  spoolPageOffset = 0;

  lv_obj_t *screen = makeScreen();
  addHeader(screen, "SELECT SPOOL", true);
  addBodyLabel(screen, selectedDestination.c_str(), landscapeMode ? 42 : 50, kColorAccent,
               &lv_font_montserrat_14);

  spoolSearchField = lv_textarea_create(screen);
  lv_obj_set_size(spoolSearchField, kDisplayWidth - 32, landscapeMode ? 30 : 34);
  lv_obj_align(spoolSearchField, LV_ALIGN_TOP_MID, 0, landscapeMode ? 62 : 72);
  lv_textarea_set_one_line(spoolSearchField, true);
  lv_textarea_set_placeholder_text(spoolSearchField, "Search name, vendor, or ID");
  lv_obj_add_event_cb(spoolSearchField,
                      [](lv_event_t *event) {
                        if (lv_event_get_code(event) == LV_EVENT_FOCUSED &&
                            spoolKeyboard != nullptr &&
                            lv_obj_is_valid(spoolKeyboard)) {
                          lv_obj_clear_flag(spoolKeyboard, LV_OBJ_FLAG_HIDDEN);
                          if (spoolList != nullptr && lv_obj_is_valid(spoolList))
                            lv_obj_add_flag(spoolList, LV_OBJ_FLAG_HIDDEN);
                          if (spoolNavigationBar != nullptr &&
                              lv_obj_is_valid(spoolNavigationBar))
                            lv_obj_add_flag(spoolNavigationBar, LV_OBJ_FLAG_HIDDEN);
                        }
                      }, LV_EVENT_FOCUSED, nullptr);

  materialDropdown = lv_dropdown_create(screen);
  const int filterRowY = landscapeMode ? 95 : 112;
  const int materialDropdownWidth = landscapeMode ? 144 : 112;
  const int pagerButtonWidth = 44;
  const int filterGap = 8;
  const int previousButtonX = 8 + materialDropdownWidth + filterGap;
  const int nextButtonX = previousButtonX + pagerButtonWidth + filterGap;
  lv_obj_set_size(materialDropdown, materialDropdownWidth, 28);
  lv_obj_align(materialDropdown, LV_ALIGN_TOP_LEFT, 8, filterRowY);
  lv_dropdown_set_options(materialDropdown, "All materials");
  lv_obj_add_event_cb(materialDropdown,
                      [](lv_event_t *event) {
                        (void)event;
                        spoolPageOffset = 0;
                        renderSpoolList();
                      }, LV_EVENT_VALUE_CHANGED, nullptr);

  spoolPreviousButton = lv_btn_create(screen);
  lv_obj_set_size(spoolPreviousButton, pagerButtonWidth, 28);
  lv_obj_align(spoolPreviousButton, LV_ALIGN_TOP_LEFT, previousButtonX,
               filterRowY);
  lv_obj_add_event_cb(spoolPreviousButton, previousSpoolPageEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *previousLabel = lv_label_create(spoolPreviousButton);
  lv_label_set_text(previousLabel, "Prev");
  lv_obj_set_style_text_font(previousLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(previousLabel);

  spoolNextButton = lv_btn_create(screen);
  lv_obj_set_size(spoolNextButton, pagerButtonWidth, 28);
  lv_obj_align(spoolNextButton, LV_ALIGN_TOP_LEFT, nextButtonX, filterRowY);
  lv_obj_add_event_cb(spoolNextButton, nextSpoolPageEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *nextLabel = lv_label_create(spoolNextButton);
  lv_label_set_text(nextLabel, "Next");
  lv_obj_set_style_text_font(nextLabel, &lv_font_montserrat_14, LV_PART_MAIN);
  lv_obj_center(nextLabel);

  spoolList = makeDataList(screen, landscapeMode ? 127 : 144);
  lv_obj_set_height(spoolList, landscapeMode ? 78 : 130);
  lv_obj_set_style_pad_all(spoolList, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_row(spoolList, 2, LV_PART_MAIN);
  lv_obj_t *loading = lv_label_create(spoolList);
  lv_label_set_text(loading, "Loading active spools...");
  lv_obj_set_style_text_color(loading, lv_color_hex(kColorMuted), LV_PART_MAIN);

  spoolNavigationBar = addSpoolNavigationBar(screen);

  spoolKeyboard = lv_keyboard_create(screen);
  lv_obj_set_size(spoolKeyboard, kDisplayWidth, landscapeMode ? 100 : 142);
  lv_obj_align(spoolKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(spoolKeyboard, spoolSearchField);
  lv_obj_add_flag(spoolKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(spoolKeyboard,
                      [](lv_event_t *event) {
                        const auto code = lv_event_get_code(event);
                        if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
                          lv_obj_add_flag(spoolKeyboard, LV_OBJ_FLAG_HIDDEN);
                          spoolPageOffset = 0;
                          if (spoolList != nullptr && lv_obj_is_valid(spoolList))
                            lv_obj_clear_flag(spoolList, LV_OBJ_FLAG_HIDDEN);
                          if (spoolNavigationBar != nullptr &&
                              lv_obj_is_valid(spoolNavigationBar))
                            lv_obj_clear_flag(spoolNavigationBar, LV_OBJ_FLAG_HIDDEN);
                          renderSpoolList();
                        }
                      }, LV_EVENT_ALL, nullptr);
  loadScreen(screen);

  if (reload) {
    Serial.printf("Loading spool inventory; free heap=%u bytes\n",
                  ESP.getFreeHeap());
    String error;
    if (!spoolman_api::getSpools(spoolCache, error)) {
      lv_obj_clean(spoolList);
      lv_obj_t *message = lv_label_create(spoolList);
      lv_label_set_text(message, error.c_str());
      lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(message, 204);
      lv_obj_set_style_text_color(message, lv_color_hex(kColorAccent), LV_PART_MAIN);
      Serial.printf("Spool request failed: %s\n", error.c_str());
      return;
    }
    Serial.printf("Loaded %u active spools; free heap=%u bytes\n",
                  static_cast<unsigned>(spoolCache.size()), ESP.getFreeHeap());
    materialOptions.clear();
    materialOptions.push_back("All materials");
    for (const auto &spool : spoolCache) {
      if (spool.material.isEmpty()) continue;
      bool found = false;
      for (const auto &existing : materialOptions)
        if (existing == spool.material) found = true;
      if (!found) materialOptions.push_back(spool.material);
    }
    String options;
    for (size_t i = 0; i < materialOptions.size(); ++i) {
      if (i > 0) options += "\n";
      options += materialOptions[i];
    }
    lv_dropdown_set_options(materialDropdown, options.c_str());
  }
  renderSpoolList();
  Serial.printf("Rendered spool picker; free heap=%u bytes\n", ESP.getFreeHeap());
}

void selectSpoolEvent(lv_event_t *event) {
  const auto code = lv_event_get_code(event);
  if (code != LV_EVENT_CLICKED) return;
  if (selectedExternalSlot) {
    if (selectedPrinterIndex >= printerCache.size()) return;
    if (selectedExternalIndex >= printerCache[selectedPrinterIndex].externalSpools.size()) return;
    const auto &slot = printerCache[selectedPrinterIndex].externalSpools[selectedExternalIndex];
    selectedDestination = printerCache[selectedPrinterIndex].name + " / External Spool";
    selectedTrayUniqueId = slot.uniqueId;
    selectedCurrentAssigned = slot.assigned;
    selectedCurrentSpoolId = slot.spoolId;
  } else {
    if (selectedPrinterIndex >= printerCache.size() ||
        selectedAmsIndex >= printerCache[selectedPrinterIndex].amsUnits.size()) return;
    const auto &trays = printerCache[selectedPrinterIndex].amsUnits[selectedAmsIndex].trays;
    selectedTrayIndex = static_cast<size_t>(reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event)));
    if (selectedTrayIndex >= trays.size()) return;
    const auto &tray = trays[selectedTrayIndex];
    selectedDestination = printerCache[selectedPrinterIndex].name + " / " +
                          printerCache[selectedPrinterIndex].amsUnits[selectedAmsIndex].name +
                          " / Tray " + String(tray.trayNumber);
    selectedTrayUniqueId = tray.uniqueId;
    selectedCurrentAssigned = tray.assigned;
    selectedCurrentSpoolId = tray.spoolId;
  }
  showSpoolPicker(true);
}

void chooseSpoolEvent(lv_event_t *event) {
  const size_t index = static_cast<size_t>(reinterpret_cast<intptr_t>(
      lv_event_get_user_data(event)));
  showSelectedSpool(index);
}

void requestUnassignEvent(lv_event_t *event) {
  if (selectedPrinterIndex >= printerCache.size()) return;
  selectedUnassignSpoolId = 0;
  selectedUnassignSpoolName = "";
  const auto &printer = printerCache[selectedPrinterIndex];
  if (selectedExternalSlot) {
    if (selectedExternalIndex >= printer.externalSpools.size()) return;
    const auto &slot = printer.externalSpools[selectedExternalIndex];
    if (!slot.assigned || slot.spoolId == 0) return;
    selectedUnassignSpoolId = slot.spoolId;
    selectedUnassignSpoolName = slot.spoolName;
    selectedDestination = printer.name + " / External Spool";
  } else {
    if (selectedAmsIndex >= printer.amsUnits.size()) return;
    const size_t trayIndex = static_cast<size_t>(reinterpret_cast<intptr_t>(
        lv_event_get_user_data(event)));
    selectedTrayIndex = trayIndex;
    const auto &ams = printer.amsUnits[selectedAmsIndex];
    if (trayIndex >= ams.trays.size()) return;
    const auto &tray = ams.trays[trayIndex];
    if (!tray.assigned || tray.spoolId == 0) return;
    selectedUnassignSpoolId = tray.spoolId;
    selectedUnassignSpoolName = tray.spoolName;
    selectedDestination = printer.name + " / " + ams.name + " / Tray " +
                          String(tray.trayNumber);
  }
  showUnassignConfirmation();
}

void showUnassignConfirmation() {
  backHandler = backToSelectedTrays;
  unassignmentStatusLabel = nullptr;
  unassignmentButton = nullptr;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "CONFIRM UNASSIGN", true);
  addBodyLabel(screen, selectedUnassignSpoolName.c_str(),
               landscapeMode ? 48 : 70, kColorText, &lv_font_montserrat_20);
  addBodyLabel(screen,
               ("Spool ID #" + String(selectedUnassignSpoolId)).c_str(),
               landscapeMode ? 77 : 108, kColorAccent,
               &lv_font_montserrat_16);
  addBodyLabel(screen, selectedDestination.c_str(),
               landscapeMode ? 105 : 142, kColorMuted,
               &lv_font_montserrat_14);
  addBodyLabel(screen, "This removes the spool from this tray.",
               landscapeMode ? 133 : 178, kColorAccent,
               &lv_font_montserrat_14);

  unassignmentStatusLabel = lv_label_create(screen);
  lv_obj_set_width(unassignmentStatusLabel, kDisplayWidth - 24);
  lv_label_set_long_mode(unassignmentStatusLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(unassignmentStatusLabel, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(unassignmentStatusLabel,
                              lv_color_hex(kColorAccent), LV_PART_MAIN);
  lv_obj_align(unassignmentStatusLabel, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 157 : 207);
  lv_label_set_text(unassignmentStatusLabel, "Confirm to clear the tray.");

  unassignmentButton = lv_btn_create(screen);
  lv_obj_set_size(unassignmentButton, 216, landscapeMode ? 32 : 38);
  lv_obj_align(unassignmentButton, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 183 : 235);
  lv_obj_set_style_bg_color(unassignmentButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_set_style_border_width(unassignmentButton, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(unassignmentButton, confirmUnassignEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *buttonLabel = lv_label_create(unassignmentButton);
  lv_label_set_text(buttonLabel, "UNASSIGN SPOOL");
  lv_obj_center(buttonLabel);
  loadScreen(screen);
}

void showUnassignSuccess(bool refreshed, const String &refreshError) {
  backHandler = backToSelectedTrays;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "UNASSIGN DONE", true);
  addBodyLabel(screen,
               landscapeMode ? "SpoolmanSync confirmed."
                             : "SpoolmanSync confirmed the unassignment.",
               landscapeMode ? 46 : 66, kColorAccent,
               &lv_font_montserrat_16);
  addBodyLabel(screen, selectedUnassignSpoolName.c_str(),
               landscapeMode ? 72 : 108, kColorText,
               &lv_font_montserrat_20);
  addBodyLabel(screen,
               ("Spool ID #" + String(selectedUnassignSpoolId)).c_str(),
               landscapeMode ? 99 : 148, kColorAccent,
               &lv_font_montserrat_16);
  addBodyLabel(screen, selectedDestination.c_str(),
               landscapeMode ? 126 : 186, kColorMuted,
               &lv_font_montserrat_14);
  const String refreshText = refreshed
                                 ? "Tray is now empty. Printer data refreshed."
                                 : "Unassigned; refresh failed: " + refreshError;
  addBodyLabel(screen, refreshText.c_str(), landscapeMode ? 152 : 220,
               kColorMuted, &lv_font_montserrat_14);
  addSpoolNavigationBar(screen);
  loadScreen(screen);
}

void confirmUnassignEvent(lv_event_t *event) {
  (void)event;
  if (selectedUnassignSpoolId == 0) return;
  if (unassignmentButton != nullptr && lv_obj_is_valid(unassignmentButton))
    lv_obj_add_state(unassignmentButton, LV_STATE_DISABLED);
  if (unassignmentStatusLabel != nullptr &&
      lv_obj_is_valid(unassignmentStatusLabel))
    lv_label_set_text(unassignmentStatusLabel, "Unassigning spool...");

  String error;
  if (!spoolman_api::unassignSpool(selectedUnassignSpoolId, error)) {
    if (unassignmentStatusLabel != nullptr &&
        lv_obj_is_valid(unassignmentStatusLabel))
      lv_label_set_text(unassignmentStatusLabel, error.c_str());
    if (unassignmentButton != nullptr && lv_obj_is_valid(unassignmentButton))
      lv_obj_clear_state(unassignmentButton, LV_STATE_DISABLED);
    return;
  }

  if (selectedPrinterIndex < printerCache.size()) {
    auto &printer = printerCache[selectedPrinterIndex];
    if (selectedExternalSlot &&
        selectedExternalIndex < printer.externalSpools.size()) {
      auto &slot = printer.externalSpools[selectedExternalIndex];
      slot.assigned = false;
      slot.spoolId = 0;
      slot.spoolName = "";
      slot.remainingWeight = -1;
      slot.material = "Empty";
      slot.color = "";
    } else if (!selectedExternalSlot &&
               selectedAmsIndex < printer.amsUnits.size()) {
      const size_t trayIndex = selectedTrayIndex;
      auto &trays = printer.amsUnits[selectedAmsIndex].trays;
      if (trayIndex < trays.size()) {
        auto &tray = trays[trayIndex];
        tray.assigned = false;
        tray.spoolId = 0;
        tray.spoolName = "";
        tray.remainingWeight = -1;
        tray.material = "Empty";
        tray.color = "";
      }
    }
  }

  String refreshError;
  std::vector<spoolman_api::PrinterSummary> refreshedPrinters;
  const bool refreshed =
      spoolman_api::getPrinters(refreshedPrinters, refreshError);
  if (refreshed) printerCache.swap(refreshedPrinters);
  showUnassignSuccess(refreshed, refreshError);
}

void showSelectedSpool(size_t spoolIndex) {
  if (spoolIndex >= spoolCache.size()) return;
  selectedSpoolIndex = spoolIndex;
  backHandler = backToSpoolPicker;
  assignmentStatusLabel = nullptr;
  assignmentButton = nullptr;
  const auto &spool = spoolCache[spoolIndex];
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "CONFIRM ASSIGN", true);
  const String identity = spoolIdentity(spool);
  addBodyLabel(screen, identity.c_str(), landscapeMode ? 43 : 60, kColorText,
               &lv_font_montserrat_14);
  addBodyLabel(screen, spoolWeight(spool).c_str(), landscapeMode ? 82 : 99,
               kColorMuted, &lv_font_montserrat_14);
  addBodyLabel(screen, "Assign to:", landscapeMode ? 105 : 125, kColorAccent,
               &lv_font_montserrat_14);
  addBodyLabel(screen, selectedDestination.c_str(), landscapeMode ? 123 : 144, kColorText,
               &lv_font_montserrat_14);
  assignmentStatusLabel = lv_label_create(screen);
  lv_obj_set_width(assignmentStatusLabel, 220);
  lv_label_set_long_mode(assignmentStatusLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(assignmentStatusLabel, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(assignmentStatusLabel,
                              lv_color_hex(kColorAccent), LV_PART_MAIN);
  lv_obj_align(assignmentStatusLabel, LV_ALIGN_TOP_MID,
               0, landscapeMode ? 157 : 211);
  if (selectedTrayUniqueId.isEmpty()) {
    lv_label_set_text(assignmentStatusLabel, "Missing tray ID; cannot assign.");
  } else if (selectedCurrentAssigned) {
    lv_label_set_text_fmt(assignmentStatusLabel,
                          "This replaces spool ID #%u.",
                          selectedCurrentSpoolId);
  } else {
    lv_label_set_text(assignmentStatusLabel, "This tray is currently empty.");
  }

  assignmentButton = lv_btn_create(screen);
  lv_obj_set_size(assignmentButton, 216, landscapeMode ? 28 : 38);
  lv_obj_align(assignmentButton, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 177 : 236);
  lv_obj_set_style_bg_color(assignmentButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_set_style_border_width(assignmentButton, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(assignmentButton, assignSpoolEvent, LV_EVENT_CLICKED,
                      nullptr);
  lv_obj_t *assignLabel = lv_label_create(assignmentButton);
  lv_label_set_text(assignLabel, "ASSIGN SPOOL");
  lv_obj_center(assignLabel);
  if (selectedTrayUniqueId.isEmpty())
    lv_obj_add_state(assignmentButton, LV_STATE_DISABLED);
  spoolNavigationBar = addSpoolNavigationBar(screen);
  loadScreen(screen);
}

void showAssignmentSuccess(bool refreshed, const String &refreshError) {
  backHandler = backToSelectedTrays;
  const auto &spool = spoolCache[selectedSpoolIndex];
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "ASSIGNMENT DONE", true);
  addBodyLabel(screen,
               landscapeMode ? "Assignment confirmed."
                             : "SpoolmanSync confirmed the assignment.",
               landscapeMode ? 45 : 66, kColorAccent,
               &lv_font_montserrat_16);
  const String identity = spoolIdentity(spool);
  addBodyLabel(screen, identity.c_str(), landscapeMode ? 72 : 108, kColorText,
               &lv_font_montserrat_14);
  addBodyLabel(screen, selectedDestination.c_str(), landscapeMode ? 126 : 157, kColorMuted,
               &lv_font_montserrat_14);
  const String refreshText = refreshed
                                 ? "Printer assignments refreshed."
                                 : "Assigned; refresh failed: " + refreshError;
  addBodyLabel(screen, refreshText.c_str(), landscapeMode ? 152 : 220, kColorMuted,
               &lv_font_montserrat_14);
  spoolNavigationBar = addSpoolNavigationBar(screen);
  loadScreen(screen);
}

void assignSpoolEvent(lv_event_t *event) {
  (void)event;
  if (selectedSpoolIndex >= spoolCache.size()) return;
  if (selectedTrayUniqueId.isEmpty()) {
    if (assignmentStatusLabel != nullptr && lv_obj_is_valid(assignmentStatusLabel))
      lv_label_set_text(assignmentStatusLabel, "Missing tray ID; cannot assign.");
    return;
  }
  if (assignmentButton != nullptr && lv_obj_is_valid(assignmentButton))
    lv_obj_add_state(assignmentButton, LV_STATE_DISABLED);
  if (assignmentStatusLabel != nullptr && lv_obj_is_valid(assignmentStatusLabel))
    lv_label_set_text(assignmentStatusLabel, "Assigning spool...");

  String error;
  const auto &spool = spoolCache[selectedSpoolIndex];
  Serial.printf("Assigning spool ID %u to tray %s\n", spool.id,
                selectedTrayUniqueId.c_str());
  if (!spoolman_api::assignSpool(spool.id, selectedTrayUniqueId, error)) {
    Serial.printf("Spool assignment failed: %s\n", error.c_str());
    if (assignmentStatusLabel != nullptr && lv_obj_is_valid(assignmentStatusLabel))
      lv_label_set_text(assignmentStatusLabel, error.c_str());
    if (assignmentButton != nullptr && lv_obj_is_valid(assignmentButton))
      lv_obj_clear_state(assignmentButton, LV_STATE_DISABLED);
    return;
  }

  String refreshError;
  const bool refreshed = spoolman_api::getPrinters(printerCache, refreshError);
  if (!refreshed)
    Serial.printf("Assignment succeeded but printer refresh failed: %s\n",
                  refreshError.c_str());
  showAssignmentSuccess(refreshed, refreshError);
}

void showWifiNetworksPage() {
  wifiPageStatus = nullptr;
  setupWifiStatus = nullptr;
  settingsStatus = nullptr;
  settingsOrientationButton = nullptr;
  settingsServerButton = nullptr;
  backHandler = setupScreenActive ? showSetup : showSettings;

  lv_obj_t *screen = makeScreen();
  addHeader(screen, "WI-FI NETWORKS", true);
  loadScreen(screen);

  scanButton = lv_btn_create(screen);
  lv_obj_set_size(scanButton, 208, 34);
  lv_obj_align(scanButton, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 46 : 56);
  lv_obj_set_style_bg_color(scanButton, lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_add_event_cb(scanButton, scanNetworksEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *scanLabel = lv_label_create(scanButton);
  lv_label_set_text(scanLabel, "Scan Again");
  lv_obj_center(scanLabel);

  wifiPageStatus = lv_label_create(screen);
  lv_obj_set_width(wifiPageStatus, 220);
  lv_obj_set_style_text_align(wifiPageStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(wifiPageStatus, lv_color_hex(kColorAccent), LV_PART_MAIN);
  lv_obj_align(wifiPageStatus, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 43 : 54);
  lv_obj_add_flag(wifiPageStatus, LV_OBJ_FLAG_HIDDEN);

  networkList = lv_obj_create(screen);
  lv_obj_set_size(networkList, kDisplayWidth - 20,
                  landscapeMode ? 92 : 150);
  lv_obj_align(networkList, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 84 : 98);
  lv_obj_set_style_bg_opa(networkList, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(networkList, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(networkList, 2, LV_PART_MAIN);
  lv_obj_set_flex_flow(networkList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(networkList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);

  passwordField = lv_textarea_create(screen);
  lv_obj_set_size(passwordField, 216, 36);
  lv_obj_align(passwordField, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 68 : 102);
  lv_textarea_set_one_line(passwordField, true);
  lv_textarea_set_password_mode(passwordField, true);
  lv_textarea_set_placeholder_text(passwordField, "Wi-Fi password");
  lv_obj_add_flag(passwordField, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(passwordField,
                      [](lv_event_t *event) {
                        if (lv_event_get_code(event) == LV_EVENT_FOCUSED &&
                            passwordKeyboard != nullptr)
                          lv_obj_clear_flag(passwordKeyboard, LV_OBJ_FLAG_HIDDEN);
                      }, LV_EVENT_FOCUSED, nullptr);

  connectButton = lv_btn_create(screen);
  lv_obj_set_size(connectButton, 208, 34);
  lv_obj_align(connectButton, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 105 : 145);
  lv_obj_set_style_bg_color(connectButton, lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_add_event_cb(connectButton, connectWifiEvent, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *connectLabel = lv_label_create(connectButton);
  lv_label_set_text(connectLabel, "Connect");
  lv_obj_center(connectLabel);
  lv_obj_add_flag(connectButton, LV_OBJ_FLAG_HIDDEN);

  passwordKeyboard = lv_keyboard_create(screen);
  lv_obj_set_size(passwordKeyboard, kDisplayWidth,
                  landscapeMode ? 100 : 135);
  lv_obj_align(passwordKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(passwordKeyboard, passwordField);
  lv_obj_add_flag(passwordKeyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(passwordKeyboard,
                      [](lv_event_t *event) {
                        const auto code = lv_event_get_code(event);
                        if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
                          lv_obj_add_flag(passwordKeyboard, LV_OBJ_FLAG_HIDDEN);
                      }, LV_EVENT_ALL, nullptr);

  scanNetworksEvent(nullptr);
}

void showSettings() {
  setupWifiStatus = nullptr;
  wifiPageStatus = nullptr;
  backHandler = showHome;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "SETTINGS", true);
  loadScreen(screen);

  settingsStatus = lv_label_create(screen);
  lv_obj_set_width(settingsStatus, kDisplayWidth - 16);
  lv_obj_set_height(settingsStatus, 36);
  lv_label_set_long_mode(settingsStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(settingsStatus, &lv_font_montserrat_12,
                             LV_PART_MAIN);
  lv_obj_set_style_text_align(settingsStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(settingsStatus, lv_color_hex(kColorAccent), LV_PART_MAIN);
  lv_obj_align(settingsStatus, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 39 : 54);
  updateWifiStatus();

  const int settingsButtonHeight = landscapeMode ? 25 : 30;
  const auto settingsButtonY = [](int row) {
    return (landscapeMode ? 77 : 92) + row * (landscapeMode ? 27 : 32);
  };

  settingsOrientationButton = lv_btn_create(screen);
  lv_obj_set_size(settingsOrientationButton, 208, settingsButtonHeight);
  lv_obj_set_style_pad_all(settingsOrientationButton, landscapeMode ? 0 : 5,
                           LV_PART_MAIN);
  lv_obj_align(settingsOrientationButton, LV_ALIGN_TOP_MID, 0, settingsButtonY(0));
  lv_obj_set_style_bg_color(settingsOrientationButton,
                            lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_add_event_cb(settingsOrientationButton, changeOrientationEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *orientationLabel = lv_label_create(settingsOrientationButton);
  lv_label_set_text(orientationLabel,
                    landscapeMode ? "Switch to Portrait"
                                  : "Switch to Landscape");
  lv_obj_set_style_text_font(orientationLabel, &lv_font_montserrat_14,
                             LV_PART_MAIN);
  lv_obj_center(orientationLabel);

  scanButton = lv_btn_create(screen);
  lv_obj_set_size(scanButton, 208, settingsButtonHeight);
  lv_obj_set_style_pad_all(scanButton, landscapeMode ? 0 : 5, LV_PART_MAIN);
  lv_obj_align(scanButton, LV_ALIGN_TOP_MID, 0, settingsButtonY(1));
  lv_obj_set_style_bg_color(scanButton, lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_add_event_cb(scanButton,
                      [](lv_event_t *event) {
                        (void)event;
                        showWifiNetworksPage();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *scanLabel = lv_label_create(scanButton);
  lv_label_set_text(scanLabel, "Scan Wi-Fi networks");
  lv_obj_center(scanLabel);

  settingsServerButton = lv_btn_create(screen);
  lv_obj_set_size(settingsServerButton, 208, settingsButtonHeight);
  lv_obj_set_style_pad_all(settingsServerButton, landscapeMode ? 0 : 5,
                           LV_PART_MAIN);
  lv_obj_align(settingsServerButton, LV_ALIGN_TOP_MID, 0, settingsButtonY(2));
  lv_obj_set_style_bg_color(settingsServerButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(settingsServerButton, openServerSettingsEvent, LV_EVENT_CLICKED,
                      nullptr);
  lv_obj_t *serverLabel = lv_label_create(settingsServerButton);
  lv_label_set_text(serverLabel, "SpoolmanSync Server");
  lv_obj_center(serverLabel);

  lv_obj_t *themeButton = lv_btn_create(screen);
  lv_obj_set_size(themeButton, 208, settingsButtonHeight);
  lv_obj_set_style_pad_all(themeButton, landscapeMode ? 0 : 5, LV_PART_MAIN);
  lv_obj_align(themeButton, LV_ALIGN_TOP_MID, 0, settingsButtonY(3));
  lv_obj_set_style_bg_color(themeButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(themeButton,
                      [](lv_event_t *event) {
                        (void)event;
                        if (!theme_config::setLightMode(!theme_config::lightMode())) return;
                        applyThemeColors();
                        showSettings();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *themeLabel = lv_label_create(themeButton);
  lv_label_set_text(themeLabel,
                    theme_config::lightMode() ? "Theme: Light"
                                              : "Theme: Dark");
  lv_obj_center(themeLabel);

  lv_obj_t *otaSettingsButton = lv_btn_create(screen);
  lv_obj_set_size(otaSettingsButton, 208, settingsButtonHeight);
  lv_obj_set_style_pad_all(otaSettingsButton, landscapeMode ? 0 : 5,
                           LV_PART_MAIN);
  lv_obj_align(otaSettingsButton, LV_ALIGN_TOP_MID, 0, settingsButtonY(5));
  lv_obj_set_style_bg_color(otaSettingsButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(otaSettingsButton,
                      [](lv_event_t *event) {
                        (void)event;
                        showOtaPage();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *otaSettingsLabel = lv_label_create(otaSettingsButton);
  lv_label_set_text(otaSettingsLabel, "Firmware Updates");
  lv_obj_center(otaSettingsLabel);

  lv_obj_t *colorTestButton = lv_btn_create(screen);
  lv_obj_set_size(colorTestButton, 208, settingsButtonHeight);
  lv_obj_set_style_pad_all(colorTestButton, landscapeMode ? 0 : 5,
                           LV_PART_MAIN);
  lv_obj_align(colorTestButton, LV_ALIGN_TOP_MID, 0, settingsButtonY(4));
  lv_obj_set_style_bg_color(colorTestButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(colorTestButton,
                      [](lv_event_t *event) {
                        (void)event;
                        showColorTestPage();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *colorTestLabel = lv_label_create(colorTestButton);
  lv_label_set_text(colorTestLabel, "Color Test");
  lv_obj_center(colorTestLabel);
}

void showColorTestPage() {
  backHandler = showSettings;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "COLOR TEST", true);
  loadScreen(screen);

  struct ColorReference {
    const char *label;
    uint32_t rgb;
  };
  static const ColorReference references[] = {
      {"RED  #FF0000", 0xFF0000},
      {"GREEN  #00FF00", 0x00FF00},
      {"BLUE  #0000FF", 0x0000FF},
      {"ORANGE  #FF7A1A", 0xFF7A1A},
      {"CYAN  #00FFFF", 0x00FFFF},
      {"WHITE  #FFFFFF", 0xFFFFFF},
      {"BLACK  #000000", 0x000000},
  };
  const int rowTop = landscapeMode ? 42 : 52;
  const int rowStep = landscapeMode ? 19 : 27;
  const int swatchHeight = landscapeMode ? 16 : 22;
  const int swatchWidth = landscapeMode ? 66 : 58;
  const int labelX = landscapeMode ? 88 : 78;
  for (size_t i = 0; i < sizeof(references) / sizeof(references[0]); ++i) {
    lv_obj_t *swatch = lv_obj_create(screen);
    lv_obj_set_size(swatch, swatchWidth, swatchHeight);
    lv_obj_align(swatch, LV_ALIGN_TOP_LEFT, 12,
                 rowTop + static_cast<int>(i) * rowStep);
    lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(swatch, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(references[i].rgb),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(swatch, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(swatch, lv_color_hex(kColorMuted),
                                  LV_PART_MAIN);
    lv_obj_set_style_pad_all(swatch, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, references[i].label);
    lv_obj_set_style_text_color(label, lv_color_hex(kColorText), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, labelX,
                 rowTop + static_cast<int>(i) * rowStep);
  }

  const int controlY = landscapeMode ? 178 : 246;
  const int controlHeight = landscapeMode ? 25 : 30;
  lv_obj_t *swapButton = lv_btn_create(screen);
  lv_obj_set_size(swapButton, 208, controlHeight);
  lv_obj_set_style_pad_all(swapButton, landscapeMode ? 0 : 5, LV_PART_MAIN);
  lv_obj_align(swapButton, LV_ALIGN_TOP_MID, 0, controlY);
  lv_obj_set_style_bg_color(swapButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(swapButton,
                      [](lv_event_t *event) {
                        (void)event;
                        if (!display_config::setRedBlueSwapped(
                                !display_config::redBlueSwapped())) return;
                        showColorTestPage();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *swapLabel = lv_label_create(swapButton);
  lv_label_set_text(swapLabel, display_config::redBlueSwapped()
                                   ? "Red/Blue: Swapped"
                                   : "Red/Blue: Normal");
  lv_obj_center(swapLabel);

  lv_obj_t *invertButton = lv_btn_create(screen);
  lv_obj_set_size(invertButton, 208, controlHeight);
  lv_obj_set_style_pad_all(invertButton, landscapeMode ? 0 : 5, LV_PART_MAIN);
  lv_obj_align(invertButton, LV_ALIGN_TOP_MID, 0,
               controlY + (landscapeMode ? 29 : 34));
  lv_obj_set_style_bg_color(invertButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(invertButton,
                      [](lv_event_t *event) {
                        (void)event;
                        const bool inverted = !display_config::colorInverted();
                        if (!display_config::setColorInverted(inverted)) return;
                        display.invertDisplay(inverted);
                        showColorTestPage();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *invertLabel = lv_label_create(invertButton);
  lv_label_set_text(invertLabel, display_config::colorInverted()
                                     ? "LCD Inversion: On"
                                     : "LCD Inversion: Off");
  lv_obj_center(invertLabel);
}

void showOtaPage() {
  otaUpdateAvailable = false;
  backHandler = showSettings;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "FIRMWARE UPDATE", true);
  loadScreen(screen);

  lv_obj_t *versionLabel = lv_label_create(screen);
  lv_label_set_text_fmt(versionLabel, "Installed version: %s",
                        ota_updater::currentVersion());
  lv_obj_set_style_text_color(versionLabel, lv_color_hex(kColorText), LV_PART_MAIN);
  lv_obj_align(versionLabel, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 52 : 62);

  otaStatus = lv_label_create(screen);
  lv_obj_set_width(otaStatus, kDisplayWidth - 32);
  lv_obj_set_height(otaStatus, landscapeMode ? 56 : 70);
  lv_label_set_long_mode(otaStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(otaStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(otaStatus, lv_color_hex(kColorAccent), LV_PART_MAIN);
  lv_label_set_text(otaStatus, "Check GitHub for a newer firmware release.");
  lv_obj_align(otaStatus, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 81 : 97);

  otaButton = lv_btn_create(screen);
  lv_obj_set_size(otaButton, 208, 36);
  lv_obj_align(otaButton, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 151 : 180);
  lv_obj_set_style_bg_color(otaButton, lv_color_hex(kColorSecondary), LV_PART_MAIN);
  lv_obj_add_event_cb(otaButton,
                      [](lv_event_t *event) {
                        (void)event;
                        if (!otaUpdateAvailable) {
                          lv_label_set_text(otaStatus, "Checking GitHub releases...");
                          lv_refr_now(nullptr);
                          String error;
                          if (!ota_updater::checkForUpdate(pendingOtaManifest,
                                                          otaUpdateAvailable,
                                                          error)) {
                            lv_label_set_text(otaStatus, error.c_str());
                            return;
                          }
                          if (!otaUpdateAvailable) {
                            lv_label_set_text_fmt(otaStatus,
                                "Firmware is up to date (v%s).",
                                ota_updater::currentVersion());
                            return;
                          }
                          lv_label_set_text_fmt(otaStatus,
                              "Version v%s is available. Tap install to continue.",
                              pendingOtaManifest.version.c_str());
                          lv_label_set_text_fmt(lv_obj_get_child(otaButton, 0),
                              "Install v%s", pendingOtaManifest.version.c_str());
                          return;
                        }

                        lv_obj_add_state(otaButton, LV_STATE_DISABLED);
                        lv_label_set_text(lv_obj_get_child(otaButton, 0),
                                          "Downloading update...");
                        lv_label_set_text(otaStatus, "Starting secure download...");
                        lv_refr_now(nullptr);
                        String error;
                        const bool installed = ota_updater::install(
                            pendingOtaManifest,
                            [](size_t complete, size_t total) {
                              const unsigned percent = total == 0
                                  ? 0
                                  : static_cast<unsigned>(complete * 100 / total);
                              lv_label_set_text_fmt(otaStatus,
                                  "Downloading firmware: %u%%", percent);
                              lv_refr_now(nullptr);
                            }, error);
                        if (!installed) {
                          otaUpdateAvailable = false;
                          lv_obj_clear_state(otaButton, LV_STATE_DISABLED);
                          lv_obj_t *buttonLabel = lv_obj_get_child(otaButton, 0);
                          if (buttonLabel != nullptr)
                            lv_label_set_text(buttonLabel, "Check for Updates");
                          lv_label_set_text(otaStatus, error.c_str());
                          return;
                        }
                        lv_label_set_text(otaStatus,
                                          "Update verified. Restarting...");
                        lv_refr_now(nullptr);
                        delay(600);
                        ESP.restart();
                      }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *otaButtonLabel = lv_label_create(otaButton);
  lv_label_set_text(otaButtonLabel, "Check for Updates");
  lv_obj_center(otaButtonLabel);
}

void showServerSettings() {
  wifiPageStatus = nullptr;
  setupWifiStatus = nullptr;
  settingsStatus = nullptr;
  setupScreenActive = false;
  initializingScreenActive = false;
  backHandler = showSettings;
  lv_obj_t *screen = makeScreen();
  addHeader(screen, "SPOOLMANSYNC SERVER", true);
  loadScreen(screen);

  lv_obj_t *hostLabel = lv_label_create(screen);
  lv_label_set_text(hostLabel, "Server IP address");
  lv_obj_set_style_text_color(hostLabel, lv_color_hex(kColorMuted), LV_PART_MAIN);
  lv_obj_align(hostLabel, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 42 : 55);
  serverHostField = lv_textarea_create(screen);
  lv_obj_set_size(serverHostField, landscapeMode ? 288 : 216,
                  landscapeMode ? 32 : 38);
  lv_obj_align(serverHostField, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 60 : 76);
  lv_textarea_set_one_line(serverHostField, true);
  lv_textarea_set_accepted_chars(serverHostField, "0123456789.");
  lv_textarea_set_max_length(serverHostField, 15);
  lv_textarea_set_text(serverHostField, server_config::host().c_str());

  lv_obj_t *portLabel = lv_label_create(screen);
  lv_label_set_text(portLabel, "Port");
  lv_obj_set_style_text_color(portLabel, lv_color_hex(kColorMuted), LV_PART_MAIN);
  lv_obj_align(portLabel, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 91 : 116);
  serverPortField = lv_textarea_create(screen);
  lv_obj_set_size(serverPortField, landscapeMode ? 288 : 216,
                  landscapeMode ? 32 : 36);
  lv_obj_align(serverPortField, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 108 : 137);
  lv_textarea_set_one_line(serverPortField, true);
  lv_textarea_set_accepted_chars(serverPortField, "0123456789");
  lv_textarea_set_max_length(serverPortField, 5);
  lv_textarea_set_text(serverPortField, server_config::port().c_str());

  serverKeyboard = lv_keyboard_create(screen);
  lv_obj_set_size(serverKeyboard, kDisplayWidth,
                  landscapeMode ? 100 : 135);
  lv_obj_align(serverKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_mode(serverKeyboard, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(serverKeyboard, serverHostField);
  lv_obj_add_flag(serverKeyboard, LV_OBJ_FLAG_HIDDEN);
  auto focusServerField = [](lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_FOCUSED) return;
    lv_obj_clear_flag(serverKeyboard, LV_OBJ_FLAG_HIDDEN);
    if (serverTestButton != nullptr && lv_obj_is_valid(serverTestButton))
      lv_obj_add_flag(serverTestButton, LV_OBJ_FLAG_HIDDEN);
    if (serverStatus != nullptr && lv_obj_is_valid(serverStatus))
      lv_obj_add_flag(serverStatus, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(serverKeyboard);
    lv_keyboard_set_mode(serverKeyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(serverKeyboard, lv_event_get_target(event));
  };
  lv_obj_add_event_cb(serverHostField, focusServerField, LV_EVENT_FOCUSED,
                      nullptr);
  lv_obj_add_event_cb(serverPortField, focusServerField, LV_EVENT_FOCUSED,
                      nullptr);
  lv_obj_add_event_cb(serverKeyboard,
                      [](lv_event_t *event) {
                        const auto code = lv_event_get_code(event);
                        if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
                          lv_obj_add_flag(serverKeyboard, LV_OBJ_FLAG_HIDDEN);
                          if (serverTestButton != nullptr && lv_obj_is_valid(serverTestButton))
                            lv_obj_clear_flag(serverTestButton, LV_OBJ_FLAG_HIDDEN);
                          if (serverStatus != nullptr && lv_obj_is_valid(serverStatus))
                            lv_obj_clear_flag(serverStatus, LV_OBJ_FLAG_HIDDEN);
                        }
                      }, LV_EVENT_ALL, nullptr);

  serverStatus = lv_label_create(screen);
  lv_obj_set_width(serverStatus, landscapeMode ? 300 : 220);
  lv_obj_set_height(serverStatus, landscapeMode ? 42 : 44);
  lv_label_set_long_mode(serverStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(serverStatus, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(serverStatus, lv_color_hex(kColorAccent),
                              LV_PART_MAIN);
  lv_label_set_text(serverStatus,
                    "Address is saved only after the printers API responds.");
  lv_obj_align(serverStatus, LV_ALIGN_TOP_MID, 0,
               landscapeMode ? 148 : 178);

  serverTestButton = lv_btn_create(screen);
  lv_obj_set_size(serverTestButton, 216, landscapeMode ? 32 : 36);
  lv_obj_align(serverTestButton, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_bg_color(serverTestButton, lv_color_hex(kColorSecondary),
                            LV_PART_MAIN);
  lv_obj_add_event_cb(serverTestButton, testAndSaveServerEvent,
                      LV_EVENT_CLICKED, nullptr);
  lv_obj_t *testLabel = lv_label_create(serverTestButton);
  lv_label_set_text(testLabel, "TEST API AND SAVE");
  lv_obj_center(testLabel);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  theme_config::begin();
  applyThemeColors();
  server_config::begin();
  firstSetupPendingServer = !server_config::configured();
  display_config::begin();
  landscapeMode = display_config::landscape();
  kDisplayWidth = landscapeMode ? kLandscapeWidth : kPortraitWidth;
  kDisplayHeight = landscapeMode ? kLandscapeHeight : kPortraitHeight;
  cyd_wifi::begin();
  pinMode(kBacklight, OUTPUT);
  digitalWrite(kBacklight, HIGH);

  touchSpi.begin(kTouchClock, kTouchMiso, kTouchMosi, kTouchCs);
  touch.begin(touchSpi);
  touch.setRotation(landscapeMode ? 1 : 0);

  display.init();
  display.setRotation(landscapeMode ? 1 : 0);
  display.invertDisplay(display_config::colorInverted());

  lv_init();
  lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr,
                        kDisplayWidth * kDrawBufferRows);

  static lv_disp_drv_t displayDriver;
  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = kDisplayWidth;
  displayDriver.ver_res = kDisplayHeight;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&displayDriver);

  static lv_indev_drv_t inputDriver;
  lv_indev_drv_init(&inputDriver);
  inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch;
  lv_indev_drv_register(&inputDriver);

  Serial.println("LVGL CYD navigation ready.");
  Serial.println("If touch targets are offset, use the raw/screen readings to tune Config.h.");
  showHome();
}

void loop() {
  static uint32_t previousTick = millis();
  static bool wifiWasConnected = false;
  const uint32_t now = millis();
  lv_tick_inc(now - previousTick);
  previousTick = now;

  lv_timer_handler();
  cyd_wifi::poll();
  const bool wifiConnected = cyd_wifi::connected();
  if (wifiWasConnected && !wifiConnected && !initializingScreenActive &&
      !setupScreenActive) {
    showInitializing();
  }
  wifiWasConnected = wifiConnected;
  updateWifiStatus();
  if (initializingScreenActive) {
    if (cyd_wifi::connected()) {
      initializingScreenActive = false;
      if (!server_config::configured()) {
        firstSetupPendingServer = true;
        showServerSettings();
      } else {
        showPrinters();
      }
    } else if (cyd_wifi::state() == cyd_wifi::State::Failed ||
               cyd_wifi::state() == cyd_wifi::State::NoCredentials) {
      initializingScreenActive = false;
      showSetup();
    }
  }
  if (setupScreenActive && cyd_wifi::connected()) {
    if (!server_config::configured()) {
      firstSetupPendingServer = true;
      showServerSettings();
    } else {
      showPrinters();
    }
  }
  delay(5);
}

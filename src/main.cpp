#include <Arduino.h>
#include <BleKeyboard.h>
#include <DNSServer.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

volatile bool callbackConnected = false;
volatile bool gapConnected = false;
volatile bool gapSubscribed = false;

class CodexBleKeyboard : public BleKeyboard {
public:
  CodexBleKeyboard(const std::string &deviceName, const std::string &deviceManufacturer, uint8_t batteryLevel)
      : BleKeyboard(deviceName, deviceManufacturer, batteryLevel) {}

protected:
  void onConnect(BLEServer *pServer) override {
    callbackConnected = true;
    gapConnected = true;
    BleKeyboard::onConnect(pServer);
  }

  void onDisconnect(BLEServer *pServer) override {
    callbackConnected = false;
    gapConnected = false;
    gapSubscribed = false;
    BleKeyboard::onDisconnect(pServer);
  }
};

CodexBleKeyboard bleKeyboard("CodexBtn-S3", "ESP32-S3", 100);
DNSServer dnsServer;
WebServer webServer(80);

const char *PREF_NAMESPACE = "codex-remote";
const char *PREF_WIFI_ENABLED = "wifi_on";
const char *PREF_WIFI_SSID = "wifi_ssid";
const char *PREF_WIFI_PASSWORD = "wifi_pass";
const char *WIFI_SETUP_AP_SSID = "CodexBtn-Setup";
const IPAddress WIFI_SETUP_IP(192, 168, 4, 1);
const IPAddress WIFI_SETUP_GATEWAY(192, 168, 4, 1);
const IPAddress WIFI_SETUP_SUBNET(255, 255, 255, 0);
const int BUTTON_PIN = 0;
const int RAW_BTN_A_PIN = 11;
const int RAW_BTN_B_PIN = 12;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long MESSAGE_HOLD_MS = 800;
const unsigned long FACE_DOWN_CHECK_MS = 200;
const unsigned long AUTO_SCREEN_OFF_MS = 30000;
const unsigned long MENU_LONG_PRESS_MS = 700;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;
const int MENU_ITEM_COUNT = 4;
const int BATTERY_REDRAW_DELTA = 5;
const uint8_t DISPLAY_BRIGHTNESS = 120;
const float FACE_DOWN_Z_THRESHOLD = -0.75f;
const float FACE_UP_Z_THRESHOLD = -0.45f;
const uint16_t UI_BACKGROUND_COLOR = 0x4372; // #476f95 in RGB565
const uint16_t UI_TEXT_COLOR = 0xD6DC;       // #d1dbe4 in RGB565

int bleGapEventHandler(ble_gap_event *event, void *arg);
bool isWifiConfigured();
String stopWifiSetupPortal();
void drawStatus(bool connected, const String &message);

enum UiMode {
  UI_STATUS,
  UI_MENU
};

bool enterLastReading = HIGH;
bool enterStableState = HIGH;
unsigned long enterLastChangeMs = 0;
bool menuLastReading = HIGH;
bool menuStableState = HIGH;
bool menuHandledPress = false;
unsigned long menuLastChangeMs = 0;
unsigned long messageHoldUntilMs = 0;
unsigned long lastActivityMs = 0;
unsigned long lastFaceDownCheckMs = 0;
bool lastConnected = false;
String lastMessage = "";
String lastWifiInfo = "";
bool displayOn = true;
bool faceDown = false;
bool bleStarted = false;
bool wifiEnabled = false;
bool wifiSetupActive = false;
bool wifiRoutesRegistered = false;
String wifiSsid = WIFI_SSID;
String wifiPassword = WIFI_PASSWORD;
int lastBatteryLevel = -100;
UiMode uiMode = UI_STATUS;
int menuSelection = 0;
bool menuReleasePending = false;

int readBatteryLevel() {
  int level = M5.Power.getBatteryLevel();
  return level < 0 ? -1 : level;
}

void drawBluetoothIcon(int x, int y, bool connected) {
  uint16_t color = connected ? TFT_CYAN : UI_TEXT_COLOR;
  M5.Display.drawLine(x + 5, y, x + 5, y + 14, color);
  M5.Display.drawLine(x + 5, y, x + 12, y + 4, color);
  M5.Display.drawLine(x + 12, y + 4, x + 5, y + 7, color);
  M5.Display.drawLine(x + 5, y + 7, x + 12, y + 10, color);
  M5.Display.drawLine(x + 12, y + 10, x + 5, y + 14, color);
  M5.Display.drawLine(x, y + 3, x + 12, y + 10, color);
  M5.Display.drawLine(x, y + 11, x + 12, y + 4, color);
}

void drawWifiIcon(int x, int y) {
  bool connected = WiFi.status() == WL_CONNECTED;
  uint16_t color = connected ? TFT_GREEN : (wifiSetupActive ? TFT_YELLOW : UI_TEXT_COLOR);

  M5.Display.drawArc(x + 8, y + 13, 12, 11, 220, 320, color);
  M5.Display.drawArc(x + 8, y + 13, 8, 7, 225, 315, color);
  M5.Display.drawArc(x + 8, y + 13, 4, 3, 235, 305, color);
  M5.Display.fillCircle(x + 8, y + 13, 1, color);
  if (!connected && !wifiSetupActive) {
    M5.Display.drawLine(x + 1, y + 2, x + 15, y + 14, color);
  }
}

String fitTextToWidth(const String &text, int maxWidth) {
  if (M5.Display.textWidth(text) <= maxWidth) {
    return text;
  }

  String shortened = text;
  while (shortened.length() > 1 && M5.Display.textWidth(shortened + "...") > maxWidth) {
    shortened.remove(shortened.length() - 1);
  }
  return shortened + "...";
}

bool hasNonAscii(const String &text) {
  for (size_t i = 0; i < text.length(); ++i) {
    if (static_cast<uint8_t>(text[i]) > 0x7f) {
      return true;
    }
  }
  return false;
}

String screenSsidText(const String &ssid) {
  if (!ssid.length()) {
    return "SSID hidden";
  }
  return ssid;
}

String wifiInfoText() {
  if (wifiSetupActive) {
    return "WiFi setup: " + String(WIFI_SETUP_AP_SSID);
  }
  if (WiFi.status() == WL_CONNECTED) {
    return "WiFi: " + WiFi.SSID() + " " + WiFi.localIP().toString();
  }
  if (wifiEnabled && isWifiConfigured()) {
    return "WiFi: connecting " + wifiSsid;
  }
  if (isWifiConfigured()) {
    return "WiFi saved: " + wifiSsid;
  }
  return "WiFi: off";
}

String wifiInfoTitle() {
  if (wifiSetupActive) {
    return "Setup AP";
  }
  if (WiFi.status() == WL_CONNECTED) {
    return "WiFi connected";
  }
  if (wifiEnabled && isWifiConfigured()) {
    return "WiFi connecting";
  }
  if (isWifiConfigured()) {
    return "WiFi saved";
  }
  return "WiFi off";
}

String wifiInfoDetail() {
  if (wifiSetupActive) {
    return WIFI_SETUP_IP.toString();
  }
  if (WiFi.status() == WL_CONNECTED) {
    return screenSsidText(WiFi.SSID());
  }
  if (isWifiConfigured()) {
    return screenSsidText(wifiSsid);
  }
  return "No saved network";
}

String wifiInfoIp() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return "";
}

String footerText(const String &message) {
  if (message == "Ready: Enter / Down") {
    return message;
  }
  if (message == "WiFi connecting") {
    return "WiFi conn";
  }
  if (message == "WiFi connect failed") {
    return "WiFi fail";
  }
  if (message == "WiFi not configured") {
    return "No WiFi";
  }
  if (message == "WiFi stopping") {
    return "WiFi off...";
  }
  if (message == "Setup starting") {
    return "Setup on...";
  }
  if (message == "Setup stopping") {
    return "Setup off...";
  }
  return message;
}

bool isWifiActive() {
  return wifiSetupActive || wifiEnabled || WiFi.status() == WL_CONNECTED;
}

bool isBleActuallyConnected() {
  if (!bleStarted) {
    return false;
  }

  if (bleKeyboard.isConnected() || callbackConnected || gapConnected || gapSubscribed) {
    return true;
  }

  if (NimBLEDevice::getInitialized() && NimBLEDevice::getServer() != nullptr) {
    return NimBLEDevice::getServer()->getConnectedCount() > 0;
  }

  return false;
}

void startBle() {
  if (bleStarted) {
    drawStatus(isBleActuallyConnected(), "BLE ready");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    return;
  }

  bleStarted = true;
  bleKeyboard.begin();
  NimBLEDevice::setCustomGapHandler(bleGapEventHandler);
  drawStatus(false, "BLE ready");
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
}

void reconnectBle() {
  if (!bleStarted) {
    startBle();
    return;
  }

  NimBLEServer *server = NimBLEDevice::getServer();
  if (server != nullptr) {
    for (uint16_t connId : server->getPeerDevices()) {
      server->disconnect(connId);
    }
    server->stopAdvertising();
    delay(50);
    server->startAdvertising();
  } else if (NimBLEDevice::getInitialized()) {
    NimBLEDevice::stopAdvertising();
    delay(50);
    NimBLEDevice::startAdvertising();
  }

  callbackConnected = false;
  gapConnected = false;
  gapSubscribed = false;
  drawStatus(false, "BLE reconnecting");
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
}

void loadWifiSettings() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE, true);
  wifiEnabled = prefs.getBool(PREF_WIFI_ENABLED, false);
  wifiSsid = prefs.getString(PREF_WIFI_SSID, WIFI_SSID);
  wifiPassword = prefs.getString(PREF_WIFI_PASSWORD, WIFI_PASSWORD);
  prefs.end();
}

void saveWifiSettings() {
  Preferences prefs;
  prefs.begin(PREF_NAMESPACE, false);
  prefs.putBool(PREF_WIFI_ENABLED, wifiEnabled);
  prefs.putString(PREF_WIFI_SSID, wifiSsid);
  prefs.putString(PREF_WIFI_PASSWORD, wifiPassword);
  prefs.end();
}

bool isWifiConfigured() {
  return wifiSsid.length() > 0;
}

String connectWifi() {
  if (!isWifiConfigured()) {
    wifiEnabled = false;
    saveWifiSettings();
    return "WiFi not configured";
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
    delay(100);
  }

  WiFi.mode(wifiSetupActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    M5.update();
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiEnabled = true;
    saveWifiSettings();
    return "WiFi " + WiFi.localIP().toString();
  }

  wifiEnabled = false;
  saveWifiSettings();
  return "WiFi connect failed";
}

String disableWifi() {
  wifiEnabled = false;
  saveWifiSettings();

  if (wifiSetupActive) {
    stopWifiSetupPortal();
  } else if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(false, false);
  }

  WiFi.mode(WIFI_OFF);
  return "WiFi off";
}

String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    switch (c) {
      case '&':
        escaped += F("&amp;");
        break;
      case '<':
        escaped += F("&lt;");
        break;
      case '>':
        escaped += F("&gt;");
        break;
      case '"':
        escaped += F("&quot;");
        break;
      case '\'':
        escaped += F("&#39;");
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

String wifiStatusText() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Connected to " + WiFi.SSID() + " (" + WiFi.localIP().toString() + ")";
  }
  if (isWifiConfigured()) {
    return "Saved network: " + wifiSsid;
  }
  return "No saved WiFi";
}

String buildWifiSetupPage(const String &notice = "") {
  int networks = WiFi.scanNetworks(false, true);
  String page;
  page.reserve(6000);
  page += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>CodexBtn WiFi</title><style>");
  page += F("body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;margin:24px;background:#f6f8fb;color:#17202a}");
  page += F("main{max-width:560px;margin:auto;background:white;border:1px solid #d8dee8;border-radius:8px;padding:20px}");
  page += F("h1{font-size:22px;margin:0 0 8px}label{display:block;margin:14px 0 6px;font-weight:600}");
  page += F("select,input,button{box-sizing:border-box;width:100%;font-size:16px;padding:10px;border-radius:6px;border:1px solid #b8c2d2}");
  page += F("button{margin-top:16px;background:#155eef;color:white;border-color:#155eef;font-weight:700}");
  page += F(".muted{color:#5b6675;font-size:14px}.notice{background:#eaf2ff;border:1px solid #b8d2ff;border-radius:6px;padding:10px;margin:12px 0}");
  page += F("</style></head><body><main><h1>CodexBtn WiFi Setup</h1>");
  page += F("<p class='muted'>Connect this device to your local WiFi.</p><p class='muted'>Status: ");
  page += htmlEscape(wifiStatusText());
  page += F("</p>");
  if (notice.length()) {
    page += F("<div class='notice'>");
    page += htmlEscape(notice);
    page += F("</div>");
  }
  page += F("<form method='post' action='/save'><label for='ssid'>Network</label><select id='ssid' name='ssid'>");
  if (wifiSsid.length()) {
    page += F("<option selected value='");
    page += htmlEscape(wifiSsid);
    page += F("'>Saved: ");
    page += htmlEscape(wifiSsid);
    page += F("</option>");
  }
  for (int i = 0; i < networks; ++i) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length()) {
      continue;
    }
    page += F("<option value='");
    page += htmlEscape(ssid);
    page += F("'>");
    page += htmlEscape(ssid);
    page += F(" (");
    page += String(WiFi.RSSI(i));
    page += F(" dBm");
    page += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F(", open") : F(", secured");
    page += F(")</option>");
  }
  WiFi.scanDelete();
  page += F("</select><label for='password'>Password</label>");
  page += F("<input id='password' name='password' type='password' placeholder='Leave blank for open networks'>");
  page += F("<button type='submit'>Save and Connect</button></form>");
  page += F("<form method='post' action='/forget'><button type='submit'>Forget Saved WiFi</button></form>");
  page += F("<form method='get' action='/'><button type='submit'>Refresh Scan</button></form>");
  page += F("<p class='muted'>Setup AP: ");
  page += WIFI_SETUP_AP_SSID;
  page += F(" / ");
  page += WIFI_SETUP_IP.toString();
  page += F("</p></main></body></html>");
  return page;
}

void handleWifiSetupRoot() {
  webServer.send(200, "text/html; charset=utf-8", buildWifiSetupPage());
}

void handleWifiSave() {
  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");
  ssid.trim();

  if (!ssid.length()) {
    webServer.send(400, "text/html; charset=utf-8", buildWifiSetupPage("SSID is required."));
    return;
  }

  if (!password.length() && ssid == wifiSsid) {
    password = wifiPassword;
  }

  wifiSsid = ssid;
  wifiPassword = password;
  wifiEnabled = true;
  saveWifiSettings();

  drawStatus(isBleActuallyConnected(), "WiFi connecting");
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
  String result = connectWifi();
  webServer.send(200, "text/html; charset=utf-8", buildWifiSetupPage(result));
  drawStatus(isBleActuallyConnected(), result);
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
}

void handleWifiForget() {
  wifiEnabled = false;
  wifiSsid = "";
  wifiPassword = "";
  saveWifiSettings();
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true, false);
  }
  webServer.send(200, "text/html; charset=utf-8", buildWifiSetupPage("Saved WiFi cleared."));
}

void handleWifiSetupNotFound() {
  webServer.sendHeader("Location", "http://" + WIFI_SETUP_IP.toString() + "/", true);
  webServer.send(302, "text/plain", "");
}

String startWifiSetupPortal() {
  if (wifiSetupActive) {
    return String("Setup ") + WIFI_SETUP_IP.toString();
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(WIFI_SETUP_IP, WIFI_SETUP_GATEWAY, WIFI_SETUP_SUBNET);
  if (!WiFi.softAP(WIFI_SETUP_AP_SSID)) {
    return "Setup AP failed";
  }

  dnsServer.start(53, "*", WIFI_SETUP_IP);
  if (!wifiRoutesRegistered) {
    webServer.on("/", HTTP_GET, handleWifiSetupRoot);
    webServer.on("/save", HTTP_POST, handleWifiSave);
    webServer.on("/forget", HTTP_POST, handleWifiForget);
    webServer.onNotFound(handleWifiSetupNotFound);
    wifiRoutesRegistered = true;
  }
  webServer.begin();
  wifiSetupActive = true;
  return String("AP ") + WIFI_SETUP_IP.toString();
}

String stopWifiSetupPortal() {
  if (!wifiSetupActive) {
    return "Setup off";
  }

  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  wifiSetupActive = false;
  WiFi.mode(WiFi.status() == WL_CONNECTED ? WIFI_STA : WIFI_OFF);
  return "Setup off";
}

String toggleWifiSetupPortal() {
  return wifiSetupActive ? stopWifiSetupPortal() : startWifiSetupPortal();
}

void updateWifiSetupPortal() {
  if (!wifiSetupActive) {
    return;
  }

  dnsServer.processNextRequest();
  webServer.handleClient();
}

void drawBatteryIcon(int x, int y, int level) {
  M5.Display.drawRoundRect(x, y, 28, 14, 2, UI_TEXT_COLOR);
  M5.Display.fillRect(x + 28, y + 4, 3, 6, UI_TEXT_COLOR);

  if (level < 0) {
    M5.Display.drawLine(x + 6, y + 4, x + 20, y + 10, UI_TEXT_COLOR);
    M5.Display.drawLine(x + 20, y + 4, x + 6, y + 10, UI_TEXT_COLOR);
    return;
  }

  int fillWidth = map(constrain(level, 0, 100), 0, 100, 0, 24);
  uint16_t color = level <= 20 ? TFT_RED : (level <= 50 ? TFT_YELLOW : TFT_GREEN);
  M5.Display.fillRect(x + 2, y + 2, fillWidth, 10, color);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextSize(1);
  M5.Display.drawString(String(level) + "%", x + 36, y + 7);
  M5.Display.setTextDatum(middle_center);
}

void drawStatus(bool connected, const String &message) {
  if (!displayOn) {
    return;
  }

  M5.Display.fillScreen(UI_BACKGROUND_COLOR);
  M5.Display.setTextColor(UI_TEXT_COLOR);
  M5.Display.setTextDatum(middle_center);

  int w = M5.Display.width();
  int h = M5.Display.height();

  M5.Display.setTextSize(1);
  drawBluetoothIcon(8, 4, connected);
  drawWifiIcon(28, 3);
  M5.Display.setTextDatum(middle_center);
  drawBatteryIcon(w - 68, 4, readBatteryLevel());

  M5.Display.setTextSize(3);
  M5.Display.drawString("ENTER", w / 2, h / 2 - 15);

  M5.Display.setTextSize(1);
  M5.Display.drawString(fitTextToWidth(wifiInfoTitle(), w - 12), w / 2, h / 2 + 8);
  M5.Display.setFont(&fonts::efontCN_10);
  M5.Display.drawString(fitTextToWidth(wifiInfoDetail(), w - 12), w / 2, h / 2 + 20);
  M5.Display.setFont(&fonts::Font0);
  if (wifiInfoIp().length()) {
    M5.Display.drawString(fitTextToWidth(wifiInfoIp(), w - 12), w / 2, h / 2 + 31);
  }
  if (message == "Ready: Enter / Down") {
    M5.Display.drawString("A: send", w / 2, h - 16);
    M5.Display.drawString("B: down/hold", w / 2, h - 5);
  } else {
    M5.Display.drawString(fitTextToWidth(footerText(message), w - 12), w / 2, h - 10);
  }

  lastConnected = connected;
  lastMessage = message;
  lastWifiInfo = wifiInfoText();
  lastBatteryLevel = readBatteryLevel();
}

void drawMenu() {
  if (!displayOn) {
    return;
  }

  M5.Display.fillScreen(UI_BACKGROUND_COLOR);
  M5.Display.setTextColor(UI_TEXT_COLOR);
  M5.Display.setTextDatum(middle_center);

  int w = M5.Display.width();
  int h = M5.Display.height();

  M5.Display.setTextSize(2);
  M5.Display.drawString("MENU", w / 2, 22);

  M5.Display.setTextSize(1);
  String bleLabel = bleStarted ? "Reconnect BLE" : "Connect BLE";
  String wifiPowerLabel = isWifiActive() ? "WiFi off" : "WiFi on";
  String setupLabel = wifiSetupActive ? "WiFi setup off" : "WiFi setup";
  int menuX = 18;
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString(String(menuSelection == 0 ? "> " : "  ") + bleLabel, menuX, h / 2 - 27);
  M5.Display.drawString(String(menuSelection == 1 ? "> " : "  ") + wifiPowerLabel, menuX, h / 2 - 9);
  M5.Display.drawString(String(menuSelection == 2 ? "> " : "  ") + setupLabel, menuX, h / 2 + 9);
  M5.Display.drawString(String(menuSelection == 3 ? "> " : "  ") + "Exit", menuX, h / 2 + 27);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("B: move  A: select", w / 2, h - 12);
}

void applyDisplayRotation() {
  M5.Display.setRotation(0);
  if (M5.Display.width() > M5.Display.height()) {
    M5.Display.setRotation(1);
  }
}

void setDisplayPower(bool on, const String &message = "") {
  if (displayOn == on) {
    return;
  }

  displayOn = on;

  if (displayOn) {
    M5.Display.wakeup();
    applyDisplayRotation();
    M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
    lastMessage = "";
    lastWifiInfo = "";
    lastBatteryLevel = -100;
    drawStatus(isBleActuallyConnected(), message.length() ? message : "Screen on");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    return;
  }

  // Screen-only sleep keeps the ESP32-S3 and BLE HID connection running.
  messageHoldUntilMs = 0;
  M5.Display.sleep();
}

void toggleDisplayPower() {
  setDisplayPower(!displayOn, displayOn ? "" : "Screen on");
}

void noteActivity() {
  lastActivityMs = millis();
}

void updateAutoScreenOff() {
  if (displayOn && millis() - lastActivityMs >= AUTO_SCREEN_OFF_MS) {
    setDisplayPower(false);
  }
}

void enforceDisplayOff() {
  if (displayOn) {
    return;
  }

  M5.Display.setBrightness(0);
  M5.Display.sleep();
}

void refreshStatus(const String &message) {
  if (!displayOn) {
    return;
  }

  if (messageHoldUntilMs != 0 && millis() < messageHoldUntilMs) {
    return;
  }

  messageHoldUntilMs = 0;
  bool connected = isBleActuallyConnected();
  String wifiInfo = wifiInfoText();
  int batteryLevel = readBatteryLevel();
  bool batteryChanged = lastBatteryLevel == -100 ||
                        (batteryLevel >= 0 && lastBatteryLevel >= 0 && abs(batteryLevel - lastBatteryLevel) >= BATTERY_REDRAW_DELTA) ||
                        (batteryLevel < 0 && lastBatteryLevel >= 0) ||
                        (batteryLevel >= 0 && lastBatteryLevel < 0);

  if (connected != lastConnected ||
      message != lastMessage ||
      wifiInfo != lastWifiInfo ||
      batteryChanged) {
    drawStatus(connected, message);
  }
}

void sendSelectedAction() {
  if (!bleStarted) {
    drawStatus(false, "Hold B: menu");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    return;
  }

  bool connected = isBleActuallyConnected();

  if (!connected) {
    drawStatus(false, "Not connected");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    return;
  }

  bleKeyboard.write(KEY_RETURN);
  drawStatus(true, "Sent Enter");
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
}

void sendDownAction() {
  if (!bleStarted) {
    drawStatus(false, "Hold B: menu");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    return;
  }

  if (!isBleActuallyConnected()) {
    drawStatus(false, "Not connected");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    return;
  }

  bleKeyboard.write(KEY_DOWN_ARROW);
  drawStatus(true, "Sent Down");
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
}

void setupDisplay() {
  M5.Display.setBrightness(DISPLAY_BRIGHTNESS);
  applyDisplayRotation();
  drawStatus(false, "Default: confirm");
}

bool readEnterButtonPressed() {
  return M5.BtnA.isPressed() ||
         digitalRead(RAW_BTN_A_PIN) == LOW ||
         digitalRead(BUTTON_PIN) == LOW;
}

bool readMenuButtonPressed() {
  return M5.BtnB.isPressed() ||
         digitalRead(RAW_BTN_B_PIN) == LOW;
}

void updateEnterButton(bool &clicked) {
  bool reading = readEnterButtonPressed() ? LOW : HIGH;

  if (reading != enterLastReading) {
    enterLastChangeMs = millis();
    enterLastReading = reading;
  }

  if ((millis() - enterLastChangeMs) <= DEBOUNCE_MS) {
    return;
  }

  if (reading != enterStableState) {
    enterStableState = reading;

    if (enterStableState == LOW) {
      return;
    }

    clicked = true;
    return;
  }
}

void updateMenuButton(bool &clicked, bool &longPressed) {
  bool reading = readMenuButtonPressed() ? LOW : HIGH;

  if (reading != menuLastReading) {
    menuLastChangeMs = millis();
    menuLastReading = reading;
  }

  if ((millis() - menuLastChangeMs) <= DEBOUNCE_MS) {
    return;
  }

  if (reading != menuStableState) {
    menuStableState = reading;

    if (menuStableState == LOW) {
      menuHandledPress = false;
      return;
    }

    if (!menuHandledPress) {
      clicked = true;
    }
    return;
  }

  if (menuStableState == LOW && !menuHandledPress && millis() - menuLastChangeMs >= MENU_LONG_PRESS_MS) {
    menuHandledPress = true;
    longPressed = true;
  }
}

void openMenu() {
  if (!displayOn) {
    setDisplayPower(true, "");
  }

  uiMode = UI_MENU;
  menuSelection = 0;
  drawMenu();
}

void closeMenu(const String &message) {
  uiMode = UI_STATUS;
  drawStatus(isBleActuallyConnected(), message);
  messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
}

void handleMenu(bool moveClicked, bool selectClicked) {
  if (moveClicked) {
    noteActivity();
    menuSelection = (menuSelection + 1) % MENU_ITEM_COUNT;
    drawMenu();
    delay(120);
    return;
  }

  if (!selectClicked) {
    return;
  }

  noteActivity();
  if (menuSelection == 0) {
    reconnectBle();
    closeMenu(bleStarted ? "BLE reconnecting" : "BLE off");
    return;
  }

  if (menuSelection == 1) {
    uiMode = UI_STATUS;
    if (isWifiActive()) {
      drawStatus(isBleActuallyConnected(), "WiFi stopping");
      closeMenu(disableWifi());
    } else {
      drawStatus(isBleActuallyConnected(), "WiFi connecting");
      closeMenu(connectWifi());
    }
    return;
  }

  if (menuSelection == 2) {
    uiMode = UI_STATUS;
    drawStatus(isBleActuallyConnected(), wifiSetupActive ? "Setup stopping" : "Setup starting");
    closeMenu(toggleWifiSetupPortal());
    return;
  }

  closeMenu("Ready");
}

void updateFaceDownDisplayState() {
  if (!M5.Imu.isEnabled() || millis() - lastFaceDownCheckMs < FACE_DOWN_CHECK_MS) {
    return;
  }

  lastFaceDownCheckMs = millis();

  if (!M5.Imu.update()) {
    return;
  }

  float ax;
  float ay;
  float az;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) {
    return;
  }

  if (az < FACE_DOWN_Z_THRESHOLD) {
    faceDown = true;
    if (displayOn) {
      setDisplayPower(false);
    }
  } else if (faceDown && az > FACE_UP_Z_THRESHOLD) {
    faceDown = false;
  }
}

int bleGapEventHandler(ble_gap_event *event, void *arg) {
  (void)arg;

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      gapConnected = event->connect.status == 0;
      if (!gapConnected) {
        gapSubscribed = false;
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      gapConnected = false;
      gapSubscribed = false;
      break;

    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.cur_notify || event->subscribe.cur_indicate) {
        gapConnected = true;
        gapSubscribed = true;
      }
      break;

    default:
      break;
  }

  return 0;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  loadWifiSettings();
  setupDisplay();
  noteActivity();
  M5.update();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RAW_BTN_A_PIN, INPUT_PULLUP);
  pinMode(RAW_BTN_B_PIN, INPUT_PULLUP);
  delay(300);
  startBle();
  if (wifiEnabled) {
    drawStatus(isBleActuallyConnected(), "WiFi connecting");
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
    String wifiMessage = connectWifi();
    drawStatus(isBleActuallyConnected(), wifiMessage);
    messageHoldUntilMs = millis() + MESSAGE_HOLD_MS;
  }
}

void loop() {
  M5.update();
  updateWifiSetupPortal();

  enforceDisplayOff();
  if (uiMode != UI_MENU) {
    updateFaceDownDisplayState();
  }

  bool middleClicked = false;
  bool rightClicked = false;
  bool rightLongPressed = false;
  updateEnterButton(middleClicked);
  updateMenuButton(rightClicked, rightLongPressed);

  if (rightLongPressed) {
    noteActivity();
    menuReleasePending = true;
    openMenu();
    delay(5);
    return;
  }

  if (menuReleasePending) {
    if (!readMenuButtonPressed()) {
      menuReleasePending = false;
    }
    if (uiMode == UI_MENU) {
      delay(5);
      return;
    }
  }

  if (uiMode == UI_MENU) {
    handleMenu(rightClicked, middleClicked);
    delay(5);
    return;
  }

  bool sendClicked = middleClicked;

  if (rightClicked) {
    noteActivity();
    sendDownAction();
  }

  if (sendClicked) {
    noteActivity();
    sendSelectedAction();
  }

  updateAutoScreenOff();
  refreshStatus("Ready: Enter / Down");
  delay(displayOn ? 5 : 100);
}

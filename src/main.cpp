#include <Arduino.h>
#include <BleKeyboard.h>
#include <M5Unified.h>
#include <NimBLEDevice.h>

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

const int BUTTON_PIN = 0;
const int RAW_BTN_A_PIN = 11;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long MESSAGE_HOLD_MS = 800;
const unsigned long FACE_DOWN_CHECK_MS = 200;
const unsigned long AUTO_SCREEN_OFF_MS = 30000;
const unsigned long MENU_LONG_PRESS_MS = 700;
const int BATTERY_REDRAW_DELTA = 5;
const uint8_t DISPLAY_BRIGHTNESS = 120;
const float FACE_DOWN_Z_THRESHOLD = -0.75f;
const float FACE_UP_Z_THRESHOLD = -0.45f;
const uint16_t UI_BACKGROUND_COLOR = 0x4372; // #476f95 in RGB565
const uint16_t UI_TEXT_COLOR = 0xD6DC;       // #d1dbe4 in RGB565

int bleGapEventHandler(ble_gap_event *event, void *arg);
void drawStatus(bool connected, const String &message);

enum UiMode {
  UI_STATUS,
  UI_MENU
};

bool lastReading = HIGH;
bool stableState = HIGH;
bool handledPress = false;
unsigned long lastChangeMs = 0;
unsigned long messageHoldUntilMs = 0;
unsigned long lastActivityMs = 0;
unsigned long lastFaceDownCheckMs = 0;
bool lastConnected = false;
String lastMessage = "";
bool displayOn = true;
bool faceDown = false;
bool bleStarted = false;
int lastBatteryLevel = -100;
UiMode uiMode = UI_STATUS;
int menuSelection = 0;
bool enterReleasePending = false;

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

String bleStatusLabel(bool connected) {
  if (!bleStarted) {
    return "OFF";
  }
  return connected ? "ON" : "READY";
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
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString(bleStatusLabel(connected), 25, 11);
  M5.Display.setTextDatum(middle_center);
  drawBatteryIcon(w - 68, 4, readBatteryLevel());

  M5.Display.setTextSize(3);
  M5.Display.drawString("ENTER", w / 2, h / 2 - 8);

  M5.Display.setTextSize(1);
  M5.Display.drawString(message, w / 2, h - 32);
  M5.Display.drawString("A: send / hold menu", w / 2, h - 18);
  M5.Display.drawString("B: down", w / 2, h - 6);

  lastConnected = connected;
  lastMessage = message;
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
  String connectLabel = bleStarted ? "Reconnect BLE" : "Connect BLE";
  M5.Display.drawString(String(menuSelection == 0 ? "> " : "  ") + connectLabel, w / 2, h / 2 - 10);
  M5.Display.drawString(String(menuSelection == 1 ? "> " : "  ") + "Exit", w / 2, h / 2 + 12);
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
  int batteryLevel = readBatteryLevel();
  bool batteryChanged = lastBatteryLevel == -100 ||
                        (batteryLevel >= 0 && lastBatteryLevel >= 0 && abs(batteryLevel - lastBatteryLevel) >= BATTERY_REDRAW_DELTA) ||
                        (batteryLevel < 0 && lastBatteryLevel >= 0) ||
                        (batteryLevel >= 0 && lastBatteryLevel < 0);

  if (connected != lastConnected ||
      message != lastMessage ||
      batteryChanged) {
    drawStatus(connected, message);
  }
}

void sendSelectedAction() {
  if (!bleStarted) {
    drawStatus(false, "Hold A: BLE menu");
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
    drawStatus(false, "Hold A: BLE menu");
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

void updateEnterButton(bool &clicked, bool &longPressed) {
  bool reading = readEnterButtonPressed() ? LOW : HIGH;

  if (reading != lastReading) {
    lastChangeMs = millis();
    lastReading = reading;
  }

  if ((millis() - lastChangeMs) <= DEBOUNCE_MS) {
    return;
  }

  if (reading != stableState) {
    stableState = reading;

    if (stableState == LOW) {
      handledPress = false;
      return;
    }

    if (!handledPress) {
      clicked = true;
    }
    return;
  }

  if (stableState == LOW && !handledPress && millis() - lastChangeMs >= MENU_LONG_PRESS_MS) {
    handledPress = true;
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
    menuSelection = (menuSelection + 1) % 2;
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
  setupDisplay();
  noteActivity();
  M5.update();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RAW_BTN_A_PIN, INPUT_PULLUP);
  delay(300);
  startBle();
}

void loop() {
  M5.update();

  enforceDisplayOff();
  if (uiMode != UI_MENU) {
    updateFaceDownDisplayState();
  }

  bool selectClicked = M5.BtnB.wasClicked();
  bool middleClicked = false;
  bool middleLongPressed = false;
  updateEnterButton(middleClicked, middleLongPressed);

  if (middleLongPressed) {
    noteActivity();
    enterReleasePending = true;
    openMenu();
    delay(5);
    return;
  }

  if (enterReleasePending) {
    if (!readEnterButtonPressed()) {
      enterReleasePending = false;
    }
    if (uiMode == UI_MENU) {
      delay(5);
      return;
    }
  }

  if (uiMode == UI_MENU) {
    handleMenu(selectClicked, middleClicked);
    delay(5);
    return;
  }

  bool sendClicked = middleClicked;

  if (selectClicked) {
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

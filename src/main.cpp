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
const int RAW_BTN_A_PIN = 37;
const int RAW_BTN_B_PIN = 39;
const int RAW_BTN_ALT_PIN = 35;
const unsigned long DEBOUNCE_MS = 50;
const unsigned long MESSAGE_HOLD_MS = 800;
const unsigned long BATTERY_REFRESH_MS = 5000;

bool lastReading = HIGH;
bool stableState = HIGH;
bool handledPress = false;
unsigned long lastChangeMs = 0;
unsigned long messageHoldUntilMs = 0;
bool lastConnected = false;
String lastMessage = "";
bool clearBondsOnBoot = false;
int lastBatteryLevel = -100;
unsigned long lastBatteryRefreshMs = 0;

struct RawButton {
  int pin;
  bool lastReading;
  bool stableState;
  unsigned long lastChangeMs;
};

RawButton rawButtonA = {RAW_BTN_A_PIN, HIGH, HIGH, 0};
RawButton rawButtonB = {RAW_BTN_B_PIN, HIGH, HIGH, 0};
RawButton rawButtonAlt = {RAW_BTN_ALT_PIN, HIGH, HIGH, 0};

int readBatteryLevel() {
  int level = M5.Power.getBatteryLevel();
  return level < 0 ? -1 : level;
}

void drawBluetoothIcon(int x, int y, bool connected) {
  uint16_t color = connected ? TFT_CYAN : TFT_WHITE;
  M5.Display.drawLine(x + 5, y, x + 5, y + 14, color);
  M5.Display.drawLine(x + 5, y, x + 12, y + 4, color);
  M5.Display.drawLine(x + 12, y + 4, x + 5, y + 7, color);
  M5.Display.drawLine(x + 5, y + 7, x + 12, y + 10, color);
  M5.Display.drawLine(x + 12, y + 10, x + 5, y + 14, color);
  M5.Display.drawLine(x, y + 3, x + 12, y + 10, color);
  M5.Display.drawLine(x, y + 11, x + 12, y + 4, color);
}

String bleStatusLabel(bool connected) {
  return connected ? "ON" : "READY";
}

bool isBleActuallyConnected() {
  if (bleKeyboard.isConnected() || callbackConnected || gapConnected || gapSubscribed) {
    return true;
  }

  if (NimBLEDevice::getInitialized() && NimBLEDevice::getServer() != nullptr) {
    return NimBLEDevice::getServer()->getConnectedCount() > 0;
  }

  return false;
}

void drawBatteryIcon(int x, int y, int level) {
  M5.Display.drawRoundRect(x, y, 28, 14, 2, TFT_WHITE);
  M5.Display.fillRect(x + 28, y + 4, 3, 6, TFT_WHITE);

  if (level < 0) {
    M5.Display.drawLine(x + 6, y + 4, x + 20, y + 10, TFT_WHITE);
    M5.Display.drawLine(x + 20, y + 4, x + 6, y + 10, TFT_WHITE);
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
  M5.Display.fillScreen(TFT_DARKGREEN);
  M5.Display.setTextColor(TFT_WHITE);
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
  M5.Display.drawString("A: send", w / 2, h - 18);
  M5.Display.drawString("B: down", w / 2, h - 6);

  lastConnected = connected;
  lastMessage = message;
  lastBatteryLevel = readBatteryLevel();
  lastBatteryRefreshMs = millis();
}

void refreshStatus(const String &message) {
  if (messageHoldUntilMs != 0 && millis() < messageHoldUntilMs) {
    return;
  }

  messageHoldUntilMs = 0;
  bool connected = isBleActuallyConnected();
  int batteryLevel = readBatteryLevel();

  if (connected != lastConnected ||
      message != lastMessage ||
      batteryLevel != lastBatteryLevel ||
      millis() - lastBatteryRefreshMs > BATTERY_REFRESH_MS) {
    drawStatus(connected, message);
  }
}

void sendSelectedAction() {
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
  M5.Display.setBrightness(120);
  M5.Display.setRotation(0);
  if (M5.Display.width() > M5.Display.height()) {
    M5.Display.setRotation(1);
  }
  drawStatus(false, "Default: confirm");
}

bool rawButtonClicked(RawButton &button) {
  bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {
    button.lastChangeMs = millis();
    button.lastReading = reading;
  }

  if ((millis() - button.lastChangeMs) <= DEBOUNCE_MS || reading == button.stableState) {
    return false;
  }

  button.stableState = reading;
  return button.stableState == LOW;
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

void clearBleBonds() {
  int bondCount = NimBLEDevice::getNumBonds();

  if (bondCount <= 0) {
    drawStatus(false, "No bonds stored");
    messageHoldUntilMs = millis() + 1500;
    return;
  }

  NimBLEDevice::deleteAllBonds();
  drawStatus(false, "Bonds cleared");
  messageHoldUntilMs = millis() + 1500;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  setupDisplay();
  M5.update();
  clearBondsOnBoot = M5.BtnB.isPressed();
  if (clearBondsOnBoot) {
    drawStatus(false, "Clearing bonds...");
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(RAW_BTN_A_PIN, INPUT_PULLUP);
  pinMode(RAW_BTN_B_PIN, INPUT_PULLUP);
  pinMode(RAW_BTN_ALT_PIN, INPUT_PULLUP);
  delay(300);
  bleKeyboard.begin();
  NimBLEDevice::setCustomGapHandler(bleGapEventHandler);
  delay(300);
  if (clearBondsOnBoot) {
    clearBleBonds();
  }
}

void loop() {
  M5.update();

  bool selectClicked = M5.BtnB.wasClicked() || rawButtonClicked(rawButtonB);
  bool sendClicked = M5.BtnA.wasClicked() || rawButtonClicked(rawButtonA) || rawButtonClicked(rawButtonAlt);

  if (selectClicked) {
    sendDownAction();
  }

  if (sendClicked) {
    sendSelectedAction();
  }

  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastReading) {
    lastChangeMs = millis();
    lastReading = reading;
  }

  if ((millis() - lastChangeMs) > DEBOUNCE_MS && reading != stableState) {
    stableState = reading;

    if (stableState == HIGH) {
      handledPress = false;
    }
  }

  if (stableState == LOW && !handledPress) {
    sendSelectedAction();
    handledPress = true;
  }

  refreshStatus("Ready: Enter / Down");
  delay(5);
}

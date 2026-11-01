/*
 * MIHAI OS TITAN 5.1 NEON CORE — NO SD
 * ESP32-S3 N16R8 + ST7789 320x240
 * MK Neon: cyan / violet / black
 *
 * Libraries:
 * - Adafruit GFX Library
 * - Adafruit ST7735 and ST7789 Library
 *
 * Arduino-ESP32 Core: WiFi, WebServer, SPI
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ---------- HARDWARE ----------
static constexpr int PIN_TFT_SCK  = 12;
static constexpr int PIN_TFT_MOSI = 11;
static constexpr int PIN_TFT_MISO = 13;
static constexpr int PIN_TFT_CS   = 10;
static constexpr int PIN_TFT_DC   = 9;
static constexpr int PIN_TFT_RST  = 14;

static constexpr int PIN_BACK     = 1;
static constexpr int PIN_SELECT   = 2;

// Active-low buttons, connected to GND when pressed.
SPIClass tftSPI(FSPI);
Adafruit_ST7789 tft(&tftSPI, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
WebServer server(80);

// ---------- COLORS ----------
static constexpr uint16_t C_BLACK  = 0x0000;
static constexpr uint16_t C_WHITE  = 0xFFFF;
static constexpr uint16_t C_CYAN   = 0x07FF;
static constexpr uint16_t C_VIOLET = 0xA81F;
static constexpr uint16_t C_DARK   = 0x1082;
static constexpr uint16_t C_GREY   = 0x8410;
static constexpr uint16_t C_GREEN  = 0x07E0;
static constexpr uint16_t C_ORANGE = 0xFD20;
static constexpr uint16_t C_RED    = 0xF800;

// ---------- SYSTEM CONFIG ----------
static const char *AP_NAME = "MK-esp_center";

// Set your own AP password before using the access point.
static const char *AP_PASSWORD = "a7eec136"; // Connect phone to this AP, then open http://10.156.1.1/

IPAddress apIP(10, 156, 1, 1);
IPAddress apGateway(10, 156, 1, 1);
IPAddress apSubnet(255, 255, 255, 0);

static constexpr uint32_t LONG_PRESS_MS = 5000;
static constexpr float TEMP_WARNING_C = 40.0f;
static constexpr float TEMP_SERVICE_OFF_C = 45.0f;
static constexpr float TEMP_RESTART_C = 35.0f;
static constexpr float TEMP_EMERGENCY_C = 48.0f;

// ESP32 internal temperature readings are approximate and
// must not be treated as calibrated chip/package temperatures.

// ---------- APP STATE ----------
enum ScreenId : uint8_t {
  SCREEN_HOME, SCREEN_MONITOR, SCREEN_TOUCHPAD, SCREEN_KEYBOARD,
  SCREEN_SETTINGS, SCREEN_RECOVERY, SCREEN_TERMINAL, SCREEN_CLOCK,
  SCREEN_GAME, SCREEN_ABOUT
};

static const char *MENU_ITEMS[] = {
  "System Monitor", "Touchpad", "Keyboard", "Settings",
  "Recovery", "Neon Mini-Game", "Digital Clock", "About MIHAI OS"
};

static constexpr uint8_t MENU_COUNT =
  sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);

ScreenId currentScreen = SCREEN_HOME;
uint8_t selectedItem = 0;

bool screenDirty = true;
bool wifiEnabled = true;
bool animationsEnabled = true;
bool serviceSuspended = false;
bool thermalLockout = false;
bool webRoutesConfigured = false;

uint32_t bootMillis = 0;
uint32_t lastAnimationMillis = 0;
uint32_t lastMonitorMillis = 0;

float displayedTemperature = 0.0f;
uint32_t freeHeapBytes = 0;
String lastAction = "System boot";

// ---------- BUTTON STATE ----------
struct ButtonState {
  int pin;
  bool stablePressed;
  bool lastRawPressed;
  bool longHandled;
  uint32_t rawChangedAt;
  uint32_t pressedAt;
};

ButtonState backButton = {
  PIN_BACK, false, false, false, 0, 0
};

ButtonState selectButton = {
  PIN_SELECT, false, false, false, 0, 0
};

// Forward declarations: each function is defined only once.
void drawCurrentScreen();
void drawHome();
void drawMonitor();
void drawTouchpad();
void drawKeyboard();
void drawSettings();
void drawRecovery();
void drawTerminal();
void markScreenDirty();
void handleBackShort();
void handleSelectShort();
void handleBackLong();
void handleSelectLong();
void handleButton(ButtonState &button);
void setupWebServer();
void handleThermalPolicy();
void stopAccessPointForThermal();
bool startAccessPointSafely();
void updateMonitorValues();
void serviceWebServer();
void serviceAnimation();
void drawHeader(const char *title);
void drawFooter();
void drawMenuRow(uint8_t index, const char *label, int y);
String systemStatusJson();
uint8_t terminalCommand = 0;
uint32_t gameStartedAt = 0;
uint16_t gameScore = 0;
bool gameRunning = false;

void markScreenDirty() {
  screenDirty = true;
}

void drawHeader(const char *title) {
  tft.fillRect(0, 0, 320, 34, C_BLACK);
  tft.drawFastHLine(0, 33, 320, C_CYAN);

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(8, 8);
  tft.print("MK");

  tft.setTextSize(1);
  tft.setTextColor(C_VIOLET, C_BLACK);
  tft.setCursor(43, 12);
  tft.print("TITAN 5.1");

  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(150, 13);
  tft.print(title);
}

void drawFooter() {
  tft.fillRect(0, 222, 320, 18, C_BLACK);
  tft.drawFastHLine(0, 222, 320, C_VIOLET);

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(6, 228);
  tft.print("MIHAI OS | TITAN 5.1");

  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(215, 228);
  tft.print(WiFi.softAPgetStationNum());
  tft.print(" CLIENT(S)");
}

void drawMenuRow(uint8_t index, const char *label, int y) {
  uint16_t bg = (index == selectedItem) ? C_DARK : C_BLACK;
  uint16_t fg = (index == selectedItem) ? C_CYAN : C_WHITE;

  tft.fillRoundRect(12, y, 296, 29, 5, bg);
  tft.drawRoundRect(
    12, y, 296, 29, 5,
    (index == selectedItem) ? C_VIOLET : C_GREY
  );

  tft.setTextSize(1);
  tft.setTextColor(fg, bg);
  tft.setCursor(22, y + 11);
  tft.print(index == selectedItem ? "> " : "  ");
  tft.print(label);
}

void drawHome() {
  drawHeader("ANDROID");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  // Neon hero card with live status.
  tft.fillRoundRect(8, 40, 304, 35, 8, 0x0841);
  tft.drawRoundRect(8, 40, 304, 35, 8, C_VIOLET);
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, 0x0841);
  tft.setCursor(17, 47);
  tft.print("MIHAI OS");
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, 0x0841);
  tft.setCursor(187, 46);
  tft.print(wifiEnabled ? "WIFI ONLINE" : "WIFI OFFLINE");
  tft.setTextColor(C_VIOLET, 0x0841);
  tft.setCursor(187, 59);
  tft.print("MK / NEON UI");

  // Eight app tiles, two columns, controlled by the physical buttons.
  const char *icons[MENU_COUNT] = {"SYS", "PAD", "KEY", "SET",
                                    "RCV", "GAME", "TIME", "INFO"};
  const uint16_t accents[MENU_COUNT] = {C_CYAN, C_VIOLET, C_GREEN, C_ORANGE,
                                        C_ORANGE, C_CYAN, C_VIOLET, C_GREEN};
  const int cardW = 148;
  const int cardH = 31;
  const int gapX = 8;
  const int startY = 81;
  for (uint8_t i = 0; i < MENU_COUNT; ++i) {
    int col = i % 2;
    int row = i / 2;
    int x = 8 + col * (cardW + gapX);
    int y = startY + row * 34;
    uint16_t bg = (i == selectedItem) ? 0x2125 : 0x0861;
    uint16_t edge = (i == selectedItem) ? C_CYAN : accents[i];
    tft.fillRoundRect(x, y, cardW, cardH, 6, bg);
    tft.drawRoundRect(x, y, cardW, cardH, 6, edge);
    tft.fillRoundRect(x + 5, y + 6, 28, 19, 4, accents[i]);
    tft.setTextColor(C_BLACK, accents[i]);
    tft.setTextSize(1);
    tft.setCursor(x + 10, y + 12);
    tft.print(icons[i]);
    tft.setTextColor((i == selectedItem) ? C_CYAN : C_WHITE, bg);
    tft.setCursor(x + 39, y + 12);
    tft.print(MENU_ITEMS[i]);
    if (i == selectedItem) {
      tft.fillCircle(x + cardW - 9, y + 15, 2, C_CYAN);
    }
  }
  drawFooter();
}

void drawMonitor() {
  drawHeader("MONITOR");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(12, 45);
  tft.print("SYSTEM STATUS");

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);

  tft.setCursor(14, 79);
  tft.print("Uptime: ");
  tft.print((millis() - bootMillis) / 1000);
  tft.print(" sec");

  tft.setCursor(14, 100);
  tft.print("Free heap: ");
  tft.print(freeHeapBytes / 1024);
  tft.print(" KB");

  tft.setCursor(14, 121);
  tft.print("Wi-Fi AP: ");
  tft.print(wifiEnabled ? "ON" : "OFF");

  tft.setCursor(14, 142);
  tft.print("Clients: ");
  tft.print(WiFi.softAPgetStationNum());

  tft.setCursor(14, 163);
  tft.print("Temperature: ");
  tft.print(displayedTemperature, 1);
  tft.print(" C*");

  tft.setCursor(14, 184);
  tft.setTextColor(thermalLockout ? C_RED : (displayedTemperature >= TEMP_WARNING_C ? C_ORANGE : C_GREEN), C_BLACK);
  if (thermalLockout) tft.print("THERMAL LOCK / AP OFF");
  else if (displayedTemperature >= TEMP_WARNING_C) tft.print("TEMP WARNING");
  else tft.print("STATUS NORMAL");

  drawFooter();
}

void drawTouchpad() {
  drawHeader("TOUCHPAD");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(12, 48);
  tft.print("WEB TOUCHPAD");

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(12, 82);
  tft.print("Open this address on a connected device:");

  tft.setTextColor(C_VIOLET, C_BLACK);
  tft.setCursor(12, 105);
  tft.print("http://10.156.1.1/");

  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(12, 145);
  tft.print("A physical touch controller is not assumed.");

  tft.setCursor(12, 164);
  tft.print("Use the browser interface for pointer input.");

  drawFooter();
}

void drawKeyboard() {
  drawHeader("KEYBOARD");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(12, 48);
  tft.print("WEB KEYBOARD");

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(12, 84);
  tft.print("Use the web interface from a connected device.");

  tft.setCursor(12, 108);
  tft.setTextColor(C_VIOLET, C_BLACK);
  tft.print("http://10.156.1.1/");

  tft.setCursor(12, 150);
  tft.setTextColor(C_GREY, C_BLACK);
  tft.print("No USB HID keyboard emulation is enabled.");

  drawFooter();
}

void drawSettings() {
  drawHeader("SETTINGS");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(C_WHITE, C_BLACK);

  tft.setCursor(12, 48);
  tft.print("1. Wi-Fi AP: ");
  tft.print(wifiEnabled ? "ON" : "OFF");

  tft.setCursor(12, 72);
  tft.print("2. Animations: ");
  tft.print(animationsEnabled ? "ON" : "OFF");

  tft.setCursor(12, 96);
  tft.setTextColor(thermalLockout ? C_RED : C_VIOLET, C_BLACK);
  tft.print(thermalLockout ? "THERMAL LOCK: cool below 35C" : "Thermal Guard: ARMED");

  tft.setCursor(12, 120);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.print("SELECT: toggle current setting");

  tft.setCursor(12, 145);
  tft.setTextColor(C_GREY, C_BLACK);
  tft.print("BACK: return to Home");

  drawFooter();
}

void drawRecovery() {
  drawHeader("RECOVERY");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(C_ORANGE, C_BLACK);
  tft.setCursor(12, 42);
  tft.print("RECOVERY");

  const char *items[] = {
    "Terminal",
    "Diagnostics",
    "Restart UI",
    "Return Home"
  };
  for (uint8_t i = 0; i < 4; ++i) {
    drawMenuRow(i, items[i], 70 + i * 32);
  }
  drawFooter();
}

void drawTerminal() {
  drawHeader("TERMINAL");
  tft.fillRect(0, 34, 320, 188, C_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(C_GREEN, C_BLACK);
  tft.setCursor(10, 42);
  tft.print("MIHAI OS LOCAL CONSOLE");
  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(10, 54);
  tft.print("Safe firmware commands only");

  const char *commands[] = {"help","status","meminfo","uptime","wifi","clients","temp","heap","version","ip","clearlog","rebootui"};
  uint8_t first = selectedItem > 4 ? selectedItem - 4 : 0;
  for (uint8_t row = 0; row < 5 && first + row < 12; ++row) {
    drawMenuRow(first + row, commands[first + row], 70 + row * 23);
  }

  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(12, 190);
  tft.print("Hold SELECT 5s: run command");
  tft.setTextColor(C_ORANGE, C_BLACK);
  tft.setCursor(12, 202);
  tft.print(lastAction.substring(0, 36));
  drawFooter();
}

void drawClock() {
  drawHeader("CLOCK"); tft.fillRect(0,34,320,188,C_BLACK);
  uint32_t sec=(millis()-bootMillis)/1000;
  char buf[20]; snprintf(buf,sizeof(buf),"%02lu:%02lu:%02lu",(unsigned long)(sec/3600)%100,(unsigned long)(sec/60)%60,(unsigned long)sec%60);
  tft.setTextColor(C_CYAN,C_BLACK); tft.setTextSize(3); tft.setCursor(42,78); tft.print(buf);
  tft.setTextSize(1); tft.setTextColor(C_VIOLET,C_BLACK); tft.setCursor(70,130); tft.print("MIHAI OS SESSION CLOCK"); drawFooter();
}

void drawGame() {
  drawHeader("NEON GAME");
  tft.fillRect(0, 34, 320, 188, C_BLACK);
  tft.drawRoundRect(8, 41, 304, 173, 12, C_VIOLET);
  tft.drawRoundRect(13, 46, 294, 163, 9, C_CYAN);

  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setTextSize(2);
  tft.setCursor(24, 53);
  tft.print("TITAN CLICK");

  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BLACK);
  tft.setCursor(24, 78);
  tft.print("TAP RED BUTTON TO SCORE");

  uint32_t elapsed = gameRunning ? (millis() - gameStartedAt) : 0;
  uint32_t remaining = gameRunning && elapsed < 15000 ? (15000 - elapsed) / 1000 : 0;
  if (gameRunning && elapsed >= 15000) gameRunning = false;

  // Animated target ring.
  uint8_t pulse = (millis() / 180) % 2;
  uint16_t pulseColor = pulse ? C_CYAN : C_VIOLET;
  tft.drawCircle(160, 133, 27, pulseColor);
  tft.drawCircle(160, 133, 18, C_CYAN);
  tft.fillCircle(160, 133, 7, pulseColor);

  tft.setTextSize(2);
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(26, 112);
  tft.print("SCORE");
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(26, 133);
  tft.print(gameScore);

  tft.setTextSize(1);
  tft.setTextColor(C_VIOLET, C_BLACK);
  tft.setCursor(226, 115);
  tft.print("TIME");
  tft.setTextColor(C_WHITE, C_BLACK);
  tft.setCursor(232, 134);
  tft.print(gameRunning ? remaining : 15);
  tft.print("s");

  tft.setTextColor(gameRunning ? C_GREEN : C_ORANGE, C_BLACK);
  tft.setCursor(25, 181);
  tft.print(gameRunning ? "ROUND LIVE - GO GO GO!" : "SELECT: START NEW ROUND");
  drawFooter();
}

void drawAbout() {
  drawHeader("ABOUT"); tft.fillRect(0,34,320,188,C_BLACK);
  tft.setTextColor(C_CYAN,C_BLACK); tft.setTextSize(2); tft.setCursor(14,52); tft.print("MK CORPORATION");
  tft.setTextSize(1); tft.setTextColor(C_WHITE,C_BLACK); tft.setCursor(14,83); tft.print("MIHAI OS TITAN 5.1");
  tft.setCursor(14,103); tft.print("ESP32-S3 / ST7789 320x240");
  tft.setCursor(14,123); tft.print("Neon cyberpunk + MK Android");
  tft.setTextColor(C_VIOLET,C_BLACK); tft.setCursor(14,157); tft.print("EASTER EGG: hold BLACK 5s in Home"); drawFooter();
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case SCREEN_HOME:     drawHome(); break;
    case SCREEN_MONITOR:  drawMonitor(); break;
    case SCREEN_TOUCHPAD: drawTouchpad(); break;
    case SCREEN_KEYBOARD: drawKeyboard(); break;
    case SCREEN_SETTINGS: drawSettings(); break;
    case SCREEN_RECOVERY: drawRecovery(); break;
    case SCREEN_TERMINAL: drawTerminal(); break;
    case SCREEN_CLOCK: drawClock(); break;
    case SCREEN_GAME: drawGame(); break;
    case SCREEN_ABOUT: drawAbout(); break;
  }

  screenDirty = false;
}

void handleBackShort() {
  if (currentScreen == SCREEN_TERMINAL) {
    currentScreen = SCREEN_RECOVERY;
    selectedItem = 0;
  } else if (currentScreen == SCREEN_HOME) {
    selectedItem = 0;
  } else {
    currentScreen = SCREEN_HOME;
  }

  lastAction = "Back";
  markScreenDirty();
}

void handleSelectShort() {
  if (currentScreen == SCREEN_HOME) {
    selectedItem = (selectedItem + 1) % MENU_COUNT;
  } else if (currentScreen == SCREEN_GAME) {
    if (!gameRunning) {
      gameRunning = true;
      gameStartedAt = millis();
      gameScore = 0;
      lastAction = "Titan Click started";
    } else if (millis() - gameStartedAt <= 15000) {
      gameScore++;
      lastAction = "Score +1";
    } else {
      gameRunning = false;
      lastAction = "Round ended";
    }
  } else if (currentScreen == SCREEN_SETTINGS) {
    selectedItem = (selectedItem + 1) % 2;
  } else if (currentScreen == SCREEN_RECOVERY) {
    selectedItem = (selectedItem + 1) % 4;
  } else if (currentScreen == SCREEN_TERMINAL) {
    selectedItem = (selectedItem + 1) % 12;
  }

  lastAction = "Next";
  markScreenDirty();
}

void handleBackLong() {
  if (currentScreen == SCREEN_HOME) {
    currentScreen = SCREEN_ABOUT;
    lastAction = "MK TITAN EASTER EGG";
  } else {
    currentScreen = SCREEN_RECOVERY;
    lastAction = "Recovery opened";
  }
  selectedItem = 0;
  markScreenDirty();
}

void handleSelectLong() {
  if (currentScreen == SCREEN_HOME) {
    switch (selectedItem) {
      case 0: currentScreen = SCREEN_MONITOR; break;
      case 1: currentScreen = SCREEN_TOUCHPAD; break;
      case 2: currentScreen = SCREEN_KEYBOARD; break;
      case 3: currentScreen = SCREEN_SETTINGS; break;
      case 4: currentScreen = SCREEN_RECOVERY; selectedItem = 0; break;
      case 5: currentScreen = SCREEN_GAME; gameRunning = false; break;
      case 6: currentScreen = SCREEN_CLOCK; break;
      case 7: currentScreen = SCREEN_ABOUT; break;
    }
  } else if (currentScreen == SCREEN_GAME) {
    if (!gameRunning) { gameRunning = true; gameStartedAt = millis(); gameScore = 0; }
    else { gameScore++; if (millis() - gameStartedAt > 15000) gameRunning = false; }
  } else if (currentScreen == SCREEN_SETTINGS) {
    if (selectedItem == 0) {
      if (wifiEnabled) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        wifiEnabled = false;
        lastAction = "Wi-Fi disabled by user";
      } else if (thermalLockout || displayedTemperature >= TEMP_SERVICE_OFF_C) {
        thermalLockout = true;
        serviceSuspended = true;
        lastAction = "THERMAL LOCK: cool below 35C";
      } else {
        startAccessPointSafely();
      }
    } else {
      animationsEnabled = !animationsEnabled;
    }
  } else if (currentScreen == SCREEN_RECOVERY) {
    if (selectedItem == 0) {
      currentScreen = SCREEN_TERMINAL;
      terminalCommand = 0;
      selectedItem = 0;
    } else if (selectedItem == 1) {
      updateMonitorValues();
      lastAction = "Diagnostics refreshed";
    } else if (selectedItem == 2) {
      currentScreen = SCREEN_HOME;
      selectedItem = 0;
      lastAction = "UI restarted";
    } else {
      currentScreen = SCREEN_HOME;
      selectedItem = 0;
    }
  } else if (currentScreen == SCREEN_TERMINAL) {
    terminalCommand = selectedItem;
    switch (terminalCommand) {
      case 0: lastAction = "help: status meminfo uptime wifi clients temp heap version ip clearlog rebootui"; break;
      case 1: lastAction = serviceSuspended ? "status: THERMAL WARNING" : "status: NORMAL"; break;
      case 2: lastAction = "meminfo refreshed"; updateMonitorValues(); break;
      case 3: lastAction = "uptime refreshed"; break;
      case 4: lastAction = wifiEnabled ? "wifi: ON" : "wifi: OFF"; break;
      case 5: lastAction = "clients: " + String(WiFi.softAPgetStationNum()); break;
      case 6: lastAction = "temp: " + String(displayedTemperature,1) + " C"; break;
      case 7: lastAction = "heap: " + String(freeHeapBytes/1024) + " KB"; break;
      case 8: lastAction = "MIHAI OS TITAN 5.1"; break;
      case 9: lastAction = "IP: 10.156.1.1"; break;
      case 10: lastAction = "log cleared"; break;
      case 11: currentScreen = SCREEN_HOME; selectedItem = 0; lastAction = "UI restarted"; break;
    }
  }

  lastAction = (currentScreen == SCREEN_TERMINAL) ? lastAction : "Enter";
  markScreenDirty();
}

void handleButton(ButtonState &button) {
  const bool rawPressed = (digitalRead(button.pin) == LOW);
  const uint32_t now = millis();

  if (rawPressed != button.lastRawPressed) {
    button.lastRawPressed = rawPressed;
    button.rawChangedAt = now;
  }

  // Debounce input.
  if ((now - button.rawChangedAt) >= 35 &&
      rawPressed != button.stablePressed) {
    button.stablePressed = rawPressed;

    if (rawPressed) {
      button.pressedAt = now;
      button.longHandled = false;
    } else if (!button.longHandled) {
      if (button.pin == PIN_BACK) {
        handleBackShort();
      } else {
        handleSelectShort();
      }
    }
  }

  if (button.stablePressed && !button.longHandled &&
      (now - button.pressedAt >= LONG_PRESS_MS)) {
    button.longHandled = true;

    if (button.pin == PIN_BACK) {
      handleBackLong();
    } else {
      handleSelectLong();
    }
  }
}

String systemStatusJson() {
  String json = "{";
  json += "\"name\":\"MIHAI OS - ANDROID VERSION TITAN 5.1\",";
  json += "\"uptime_seconds\":";
  json += String((millis() - bootMillis) / 1000);
  json += ",\"free_heap_bytes\":";
  json += String(freeHeapBytes);
  json += ",\"wifi_enabled\":";
  json += wifiEnabled ? "true" : "false";
  json += ",\"clients\":";
  json += String(WiFi.softAPgetStationNum());
  json += ",\"temperature_c\":";
  json += String(displayedTemperature, 1);
  json += ",\"thermal_warning\":";
  json += serviceSuspended ? "true" : "false";
  json += "}";
  return json;
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    String page = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>MIHAI OS - ANDROID VERSION TITAN 5.1</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:radial-gradient(circle at 15% 0%,#17254c 0,#050711 45%,#090516 100%);color:#e8faff;font:16px system-ui,sans-serif}
main{max-width:900px;margin:auto;padding:18px}
h1{color:#00eaff;text-shadow:0 0 18px #00eaff88;letter-spacing:1px}
h2{color:#c9b8ff}
.panel{border:1px solid #673dff;border-radius:18px;padding:16px;margin:14px 0;background:linear-gradient(145deg,#111a31dd,#0b1020ee);box-shadow:0 8px 30px #0008, inset 0 0 18px #00eaff0b}
button{background:linear-gradient(135deg,#14243e,#201441);border:1px solid #00eaff88;color:#e8faff;border-radius:12px;padding:12px;margin:4px;transition:transform .12s,box-shadow .12s;box-shadow:0 0 10px #00eaff12}
button:active{background:#673dff;transform:scale(.97);box-shadow:0 0 18px #673dff88}
.appgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:8px}
.appgrid button{min-height:58px;font-weight:600}
#pad{height:180px;border:1px solid #00eaff;
border-radius:12px;touch-action:none;display:grid;
place-items:center;user-select:none}
input{background:#050711;color:#fff;padding:10px;
border:1px solid #673dff;border-radius:8px;width:90%}
small{color:#9baec8}
</style>
</head>
<body>
<main>
<h1>MIHAI OS // TITAN 5.1</h1>
<small>MK NEON CONTROL CENTER · NO SD</small>
<div class="panel">
<h2>System status</h2>
<pre id="status">Loading...</pre>
<button onclick="refreshStatus()">Refresh status</button>
<button onclick="fetch('/api/home',{method:'POST'})">Home</button>
</div>
<div class="panel">
<h2>Premium App Launcher</h2>
<div class="appgrid">
<button onclick="showScreen('home')">🏠 Home</button>
<button onclick="showScreen('monitor')">📊 Monitor</button>
<button onclick="showScreen('touchpad')">🖱️ Touchpad</button>
<button onclick="showScreen('keyboard')">⌨️ Keyboard</button>
<button onclick="showScreen('settings')">⚙️ Settings</button>
<button onclick="showScreen('recovery')">🛠️ Recovery</button>
<button onclick="showScreen('terminal')">⌨️ Terminal</button>
<button onclick="showScreen('clock')">🕒 Clock</button>
<button onclick="showScreen('game')">🎮 Mini-game</button>
<button onclick="showScreen('about')">💎 About</button>
<button onclick="refreshStatus()">🔄 Refresh</button>
</div>
<small>Embedded apps run on the ESP32-S3; this is not a full Android OS.</small>
</div>
<div class="panel">
<h2>Touchpad</h2>
<div id="pad">Touch or drag here</div>
<p id="pointer">Pointer: 0, 0</p>
<small>Browser-side pointer demo; does not control another device.</small>
</div>
<div class="panel">
<h2>Quick Launch</h2>
<div class="appgrid">
<button onclick="showScreen('monitor')">📡 System Monitor</button>
<button onclick="showScreen('game')">🎮 Titan Click</button>
<button onclick="showScreen('clock')">🕒 Session Clock</button>
<button onclick="showScreen('recovery')">🛠 Recovery</button>
</div>
</div>
<div class="panel">
<h2>Phone Sound Lab</h2>
<p><small>Audio is generated by your phone browser; the ESP32 has no built-in speaker.</small></p>
<button onclick="tone(660,120)">🔊 MK Beep</button>
<button onclick="tone(880,180)">⚡ Neon Ping</button>
<button onclick="tone(440,100);setTimeout(()=>tone(660,100),130);setTimeout(()=>tone(880,160),260)">🎵 Startup</button>
</div>
<div class="panel">
<h2>Keyboard</h2>
<input id="text" placeholder="Type a local test message">
<p id="preview"></p>
</div>
<div class="panel">
<h2>Display</h2>
<button onclick="fetch('/api/screen?name=monitor')">Monitor</button>
<button onclick="fetch('/api/screen?name=settings')">Settings</button>
<button onclick="fetch('/api/screen?name=home')">Home</button>
</div>
</main>
<script>
function tone(freq,duration){try{const C=window.AudioContext||window.webkitAudioContext;const c=new C();const o=c.createOscillator();const g=c.createGain();o.type='sine';o.frequency.value=freq;g.gain.value=0.06;o.connect(g);g.connect(c.destination);o.start();o.stop(c.currentTime+duration/1000);o.onended=()=>c.close();}catch(e){alert('Audio indisponibil in browser');}}
async function showScreen(name){
 try{
  const r=await fetch('/api/screen?name='+encodeURIComponent(name));
  if(!r.ok) throw new Error('screen');
 }catch(e){}
}
async function refreshStatus(){
 try{
  const r=await fetch('/api/status');
  document.getElementById('status').textContent=
    JSON.stringify(await r.json(),null,2);
 }catch(e){
  document.getElementById('status').textContent='Connection error';
 }
}
setInterval(refreshStatus,2000);refreshStatus();

const pad=document.getElementById('pad');
pad.addEventListener('pointermove',e=>{
 if(!e.buttons && e.pointerType==='mouse')return;
 const r=pad.getBoundingClientRect();
 const x=Math.round(e.clientX-r.left);
 const y=Math.round(e.clientY-r.top);
 document.getElementById('pointer').textContent=
  'Pointer: '+x+', '+y;
});
pad.addEventListener('pointerdown',e=>pad.setPointerCapture(e.pointerId));
document.getElementById('text').addEventListener('input',e=>{
 document.getElementById('preview').textContent=e.target.value;
});
</script>
</body>
</html>
)HTML";
    server.send(200, "text/html; charset=utf-8", page);
  });

  server.on("/api/status", HTTP_GET, []() {
    server.send(200, "application/json", systemStatusJson());
  });

  server.on("/api/home", HTTP_POST, []() {
    currentScreen = SCREEN_HOME;
    selectedItem = 0;
    markScreenDirty();
    server.send(200, "application/json",
                "{\"ok\":true,\"screen\":\"home\"}");
  });

  server.on("/api/screen", HTTP_GET, []() {
    String name = server.arg("name");

    if (name == "home") {
      currentScreen = SCREEN_HOME;
    } else if (name == "monitor") {
      currentScreen = SCREEN_MONITOR;
    } else if (name == "settings") {
      currentScreen = SCREEN_SETTINGS;
    } else if (name == "touchpad") {
      currentScreen = SCREEN_TOUCHPAD;
    } else if (name == "keyboard") {
      currentScreen = SCREEN_KEYBOARD;
    } else if (name == "recovery") {
      currentScreen = SCREEN_RECOVERY;
    } else if (name == "terminal") {
      currentScreen = SCREEN_TERMINAL; selectedItem = 0;
    } else if (name == "clock") {
      currentScreen = SCREEN_CLOCK;
    } else if (name == "game") {
      currentScreen = SCREEN_GAME;
    } else if (name == "about") {
      currentScreen = SCREEN_ABOUT;
    } else {
      server.send(400, "application/json",
                  "{\"ok\":false,\"error\":\"unknown screen\"}");
      return;
    }

    markScreenDirty();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "MIHAI OS: route not found");
  });

  server.begin();
}

void serviceWebServer() {
  // Keep the web server responsive even while the temperature warning
  // is active. Thermal status is informational; it must not silently
  // disable the control page and strand the user.
  if (wifiEnabled && !thermalLockout) {
    server.handleClient();
  }
}

void updateMonitorValues() {
  freeHeapBytes = ESP.getFreeHeap();

  // ESP32-S3 Arduino core exposes temperatureRead() on
  // supported versions. Treat the value as approximate.
  displayedTemperature = temperatureRead();

  if (!isfinite(displayedTemperature) ||
      displayedTemperature < -20.0f ||
      displayedTemperature > 150.0f) {
    displayedTemperature = 0.0f;
  }
}

void stopAccessPointForThermal() {
  // Thermal lockout deliberately does not restart Wi-Fi automatically.
  // The user may re-enable it after the chip cools below TEMP_RESTART_C.
  if (wifiEnabled) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;
  }
  serviceSuspended = true;
  lastAction = (displayedTemperature >= TEMP_EMERGENCY_C)
                 ? "CRITICAL TEMP: AP OFF"
                 : "THERMAL GUARD: AP OFF";
  markScreenDirty();
}

bool startAccessPointSafely() {
  if (thermalLockout || displayedTemperature >= TEMP_SERVICE_OFF_C) {
    thermalLockout = true;
    serviceSuspended = true;
    wifiEnabled = false;
    lastAction = "THERMAL LOCK: AP blocked";
    markScreenDirty();
    return false;
  }

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(true);  // reduce radio activity when the platform permits
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  wifiEnabled = WiFi.softAP(AP_NAME, AP_PASSWORD);
  if (wifiEnabled) {
    lastAction = "AP started safely";
  } else {
    lastAction = "AP start failed";
  }
  markScreenDirty();
  return wifiEnabled;
}

void handleThermalPolicy() {
  // ESP32-S3 temperatureRead() is approximate, not a calibrated measurement.
  // This policy reduces load; it is not a substitute for hardware protection.
  if (displayedTemperature >= TEMP_SERVICE_OFF_C) {
    thermalLockout = true;
    if (wifiEnabled) {
      stopAccessPointForThermal();
    } else {
      serviceSuspended = true;
    }
  } else if (displayedTemperature <= TEMP_RESTART_C) {
    // Clear the lockout only after a meaningful cooldown. Wi-Fi stays OFF
    // until the user explicitly enables it in Settings.
    thermalLockout = false;
    serviceSuspended = false;
  }

  if (displayedTemperature >= TEMP_EMERGENCY_C) {
    animationsEnabled = false;
    lastAction = "CRITICAL TEMP - AP OFF";
    markScreenDirty();
  } else if (displayedTemperature >= TEMP_WARNING_C) {
    lastAction = thermalLockout ? "THERMAL GUARD ACTIVE" : "Temperature warning";
  }
}

void serviceAnimation() {
  const uint32_t now = millis();

  // Refresh the game timer and target pulse while the game is open.
  if (currentScreen == SCREEN_GAME && animationsEnabled) {
    if (now - lastAnimationMillis >= 180) {
      lastAnimationMillis = now;
      if (gameRunning && now - gameStartedAt >= 15000) {
        gameRunning = false;
        lastAction = "Round ended";
      }
      markScreenDirty();
    }
    return;
  }

  if (!animationsEnabled || thermalLockout || currentScreen != SCREEN_HOME) return;
  if (now - lastAnimationMillis < 700) return;
  lastAnimationMillis = now;

  // Small partial update rather than clearing the full display.
  tft.fillCircle(301, 14, 3, C_BLACK);
  tft.fillCircle(301, 14, 3,
                 (now / 700) % 2 ? C_CYAN : C_VIOLET);
}

void setup() {
  Serial.begin(115200);
  delay(150);

  pinMode(PIN_BACK, INPUT_PULLUP);
  pinMode(PIN_SELECT, INPUT_PULLUP);

  bootMillis = millis();

  // ST7789 SPI initialization with the known pin mapping.
  tftSPI.begin(PIN_TFT_SCK, PIN_TFT_MISO,
               PIN_TFT_MOSI, PIN_TFT_CS);

  tft.init(240, 320);
  tft.setRotation(1);
  tft.invertDisplay(false);
  tft.fillScreen(C_BLACK);

  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.setTextColor(C_CYAN, C_BLACK);
  tft.setCursor(20, 90);
  tft.print("MIHAI OS");
  tft.setTextSize(1);
  tft.setTextColor(C_VIOLET, C_BLACK);
  tft.setCursor(20, 120);
  tft.print("TITAN 5.1 / INITIALIZING");

  WiFi.mode(WIFI_AP);
  WiFi.setSleep(true);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);

  wifiEnabled = WiFi.softAP(AP_NAME, AP_PASSWORD);

  if (wifiEnabled) {
    setupWebServer();
    webRoutesConfigured = true;
  }

  updateMonitorValues();
  handleThermalPolicy();

  currentScreen = SCREEN_HOME;
  selectedItem = 0;
  markScreenDirty();

  Serial.println();
  Serial.println("MIHAI OS - ANDROID VERSION TITAN 5.1");
  Serial.print("AP: ");
  Serial.println(AP_NAME);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  handleButton(backButton);
  handleButton(selectButton);

  serviceWebServer();

  const uint32_t now = millis();

  if (now - lastMonitorMillis >= 1000) {
    lastMonitorMillis = now;
    updateMonitorValues();
    handleThermalPolicy();

    if (currentScreen == SCREEN_MONITOR) {
      markScreenDirty();
    }
  }

  if (screenDirty) {
    drawCurrentScreen();
  }

  serviceAnimation();
  delay(2);
}


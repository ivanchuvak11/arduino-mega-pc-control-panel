#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <RTClib.h>

// Arduino Mega hardware SPI: MISO=50, MOSI=51, SCK=52.
constexpr uint8_t TFT_CS = 48;
constexpr uint8_t TFT_DC = 47;
constexpr uint8_t SD_CS = 49;

constexpr uint8_t ENC_CLK = 2;
constexpr uint8_t ENC_DT = 3;
constexpr uint8_t ENC_SW = 4;
constexpr uint8_t BTN_BACK = 5;
constexpr uint8_t BTN_HOME = 6;
constexpr uint8_t BTN_M1 = 7;
constexpr uint8_t BTN_M2 = 8;
constexpr uint8_t BTN_M3 = 9;
constexpr uint8_t BTN_M4 = 10;
constexpr uint8_t LED_R = 11;
constexpr uint8_t LED_G = 12;
constexpr uint8_t LED_B = 13;
constexpr uint8_t BUZZER = 14;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC);
RTC_DS1307 rtc;

enum Screen : uint8_t {
  SCREEN_HOME,
  SCREEN_SYSTEM,
  SCREEN_WEATHER,
  SCREEN_LAUNCHER,
  SCREEN_MEDIA,
  SCREEN_LOGS,
  SCREEN_SETTINGS
};

struct SystemData {
  float cpuLoad = 34;
  float cpuTemp = 52;
  float gpuLoad = 47;
  float gpuTemp = 61;
  float ramUsed = 8.2;
  float ramTotal = 16.0;
  float diskUsed = 68;
  float downloadMbps = 12.4;
  float uploadMbps = 1.7;
  unsigned long uptimeSec = 18320;
} systemData;

struct WeatherData {
  float temperature = 12;
  float feelsLike = 9;
  int humidity = 73;
  int pressure = 1014;
  float wind = 4.2;
  String condition = "Cloudy";
  String city = "Kyiv";
} weatherData;

Screen currentScreen = SCREEN_HOME;
int8_t homeSelection = 0;
int8_t launcherSelection = 0;
int8_t settingsSelection = 0;
bool needsRedraw = true;
bool dynamicDataDirty = false;
bool sdReady = false;
bool rtcReady = false;
bool pcOnline = false;
bool demoMode = true;
bool soundEnabled = true;
bool loggingEnabled = true;
uint16_t logIntervalSec = 5;
uint32_t logRecords = 0;

unsigned long lastDraw = 0;
unsigned long lastFooterDraw = 0;
unsigned long lastDemoUpdate = 0;
unsigned long lastLog = 0;
unsigned long lastPcPacket = 0;
String serialLine;

const char *homeItems[] = {
  "System monitor", "Weather", "Application launcher",
  "Multimedia", "Logs", "Settings"
};

const char *launcherItems[] = {
  "Browser", "VS Code", "Arduino IDE", "Discord", "Task Manager"
};

constexpr uint8_t HOME_COUNT = sizeof(homeItems) / sizeof(homeItems[0]);
constexpr uint8_t LAUNCHER_COUNT = sizeof(launcherItems) / sizeof(launcherItems[0]);

void beep(uint16_t frequency = 1800, uint16_t duration = 45) {
  if (soundEnabled) tone(BUZZER, frequency, duration);
}

void setStatusColor(uint8_t red, uint8_t green, uint8_t blue) {
  analogWrite(LED_R, red);
  analogWrite(LED_G, green);
  analogWrite(LED_B, blue);
}

String timestamp() {
  if (!rtcReady) {
    char fallback[18];
    unsigned long seconds = millis() / 1000;
    snprintf(fallback, sizeof(fallback), "UPTIME %02lu:%02lu:%02lu",
             seconds / 3600, (seconds / 60) % 60, seconds % 60);
    return String(fallback);
  }

  DateTime now = rtc.now();
  char text[20];
  snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(text);
}

void appendEvent(const String &eventType, const String &details) {
  if (!sdReady || !loggingEnabled) return;

  File file = SD.open("EVENTS.CSV", FILE_WRITE);
  if (!file) {
    sdReady = false;
    return;
  }
  file.print(timestamp());
  file.print(',');
  file.print(eventType);
  file.print(',');
  file.println(details);
  file.close();
}

void appendSystemLog() {
  if (!sdReady || !loggingEnabled) return;

  File file = SD.open("SYSTEM.CSV", FILE_WRITE);
  if (!file) {
    sdReady = false;
    return;
  }

  if (file.size() == 0) {
    file.println(F("timestamp,cpu_load,cpu_temp,gpu_load,gpu_temp,ram_used,ram_total,disk_used,download_mbps,upload_mbps,pc_online"));
  }
  file.print(timestamp()); file.print(',');
  file.print(systemData.cpuLoad, 1); file.print(',');
  file.print(systemData.cpuTemp, 1); file.print(',');
  file.print(systemData.gpuLoad, 1); file.print(',');
  file.print(systemData.gpuTemp, 1); file.print(',');
  file.print(systemData.ramUsed, 1); file.print(',');
  file.print(systemData.ramTotal, 1); file.print(',');
  file.print(systemData.diskUsed, 1); file.print(',');
  file.print(systemData.downloadMbps, 1); file.print(',');
  file.print(systemData.uploadMbps, 1); file.print(',');
  file.println(pcOnline ? 1 : 0);
  file.close();
  logRecords++;
}

void sendCommand(const String &command) {
  Serial.print(F("CMD,"));
  Serial.println(command);
  appendEvent("COMMAND", command);
  beep();
}

void drawText(int16_t x, int16_t y, const String &text, uint16_t color,
              uint8_t size = 2) {
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.setTextSize(size);
  tft.print(text);
}

void drawConnectionDot() {
  if (pcOnline) {
    tft.fillCircle(300, 15, 6, ILI9341_GREEN);
  } else if (demoMode) {
    tft.fillCircle(300, 15, 6, ILI9341_YELLOW);
  } else {
    tft.fillCircle(300, 15, 6, ILI9341_RED);
  }
}

void drawHeader(const __FlashStringHelper *title) {
  tft.fillScreen(ILI9341_BLACK);
  tft.fillRect(0, 0, 320, 31, ILI9341_NAVY);
  tft.setCursor(8, 8);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.print(title);
  drawConnectionDot();
}

void drawFooter() {
  tft.fillRect(0, 220, 320, 20, ILI9341_BLACK);
  tft.drawFastHLine(0, 219, 320, ILI9341_DARKGREY);
  drawText(6, 225, timestamp().substring(11), ILI9341_LIGHTGREY, 1);
  drawText(230, 225, sdReady ? "SD:OK" : "SD:ERR",
           sdReady ? ILI9341_GREEN : ILI9341_RED, 1);
}

void drawBar(int16_t x, int16_t y, int16_t width, float value,
             uint16_t normalColor = ILI9341_CYAN) {
  value = constrain(value, 0, 100);
  uint16_t color = value >= 90 ? ILI9341_RED : (value >= 75 ? ILI9341_ORANGE : normalColor);
  tft.drawRect(x, y, width, 12, ILI9341_DARKGREY);
  tft.fillRect(x + 2, y + 2, (width - 4) * value / 100.0, 8, color);
}

void drawHome() {
  drawHeader(F("PC CONTROL PANEL"));
  for (uint8_t i = 0; i < HOME_COUNT; i++) {
    int16_t y = 41 + i * 28;
    if (i == homeSelection) {
      tft.fillRoundRect(8, y - 5, 304, 24, 4, ILI9341_BLUE);
      drawText(16, y, String("> ") + homeItems[i], ILI9341_WHITE, 2);
    } else {
      drawText(16, y, String("  ") + homeItems[i], ILI9341_LIGHTGREY, 2);
    }
  }
  drawFooter();
}

void drawMetric(int16_t y, const char *label, float value, const char *suffix,
                bool showBar = true) {
  // Only the value/bar area is cleared. This prevents visible full-screen
  // redraw stripes in Wokwi and reduces flicker on the physical display.
  tft.fillRect(102, y - 2, 214, 22, ILI9341_BLACK);
  drawText(10, y, label, ILI9341_LIGHTGREY, 2);
  char number[18];
  dtostrf(value, 4, 1, number);
  drawText(216, y, String(number) + suffix, ILI9341_WHITE, 2);
  if (showBar) drawBar(105, y + 2, 100, value);
}

void drawSystem() {
  drawHeader(F("SYSTEM MONITOR"));
  drawMetric(42, "CPU load", systemData.cpuLoad, "%");
  drawMetric(70, "CPU temp", systemData.cpuTemp, "C", false);
  drawMetric(98, "GPU load", systemData.gpuLoad, "%");
  drawMetric(126, "GPU temp", systemData.gpuTemp, "C", false);
  float ramPercent = systemData.ramTotal > 0 ? systemData.ramUsed * 100.0 / systemData.ramTotal : 0;
  drawMetric(154, "RAM", ramPercent, "%");
  drawMetric(182, "Disk", systemData.diskUsed, "%");
  drawFooter();
}

void drawWeather() {
  drawHeader(F("WEATHER"));
  drawText(12, 42, weatherData.city, ILI9341_CYAN, 3);
  drawText(12, 78, String(weatherData.temperature, 1) + " C", ILI9341_YELLOW, 4);
  drawText(174, 82, weatherData.condition, ILI9341_WHITE, 2);
  drawText(12, 132, "Feels like:", ILI9341_LIGHTGREY, 2);
  drawText(190, 132, String(weatherData.feelsLike, 1) + " C", ILI9341_WHITE, 2);
  drawText(12, 158, "Humidity:", ILI9341_LIGHTGREY, 2);
  drawText(190, 158, String(weatherData.humidity) + "%", ILI9341_WHITE, 2);
  drawText(12, 184, "Wind:", ILI9341_LIGHTGREY, 2);
  drawText(190, 184, String(weatherData.wind, 1) + " m/s", ILI9341_WHITE, 2);
  drawFooter();
}

void drawLauncher() {
  drawHeader(F("APPLICATIONS"));
  for (uint8_t i = 0; i < LAUNCHER_COUNT; i++) {
    int16_t y = 45 + i * 32;
    uint16_t color = i == launcherSelection ? ILI9341_WHITE : ILI9341_LIGHTGREY;
    if (i == launcherSelection) tft.fillRoundRect(10, y - 6, 300, 27, 4, ILI9341_PURPLE);
    drawText(18, y, String(i == launcherSelection ? "> " : "  ") + launcherItems[i], color, 2);
  }
  drawFooter();
}

void drawMedia() {
  drawHeader(F("MULTIMEDIA"));
  drawText(28, 52, "M1  Previous track", ILI9341_WHITE, 2);
  drawText(28, 86, "M2  Play / Pause", ILI9341_WHITE, 2);
  drawText(28, 120, "M3  Next track", ILI9341_WHITE, 2);
  drawText(28, 154, "M4  Mute", ILI9341_WHITE, 2);
  drawText(28, 190, "Encoder: volume", ILI9341_CYAN, 2);
  drawFooter();
}

void drawLogs() {
  drawHeader(F("LOGGING"));
  drawText(14, 48, "SD card:", ILI9341_LIGHTGREY, 2);
  drawText(178, 48, sdReady ? "READY" : "ERROR", sdReady ? ILI9341_GREEN : ILI9341_RED, 2);
  drawText(14, 82, "Logging:", ILI9341_LIGHTGREY, 2);
  drawText(178, 82, loggingEnabled ? "ON" : "OFF", loggingEnabled ? ILI9341_GREEN : ILI9341_ORANGE, 2);
  drawText(14, 116, "Interval:", ILI9341_LIGHTGREY, 2);
  drawText(178, 116, String(logIntervalSec) + " sec", ILI9341_WHITE, 2);
  drawText(14, 150, "Records:", ILI9341_LIGHTGREY, 2);
  drawText(178, 150, String(logRecords), ILI9341_WHITE, 2);
  drawText(14, 190, "SYSTEM.CSV / EVENTS.CSV", ILI9341_CYAN, 1);
  drawFooter();
}

void drawSettings() {
  drawHeader(F("SETTINGS"));
  const char *labels[] = {"Log interval", "Demo mode", "Sound", "Logging"};
  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = 50 + i * 38;
    if (i == settingsSelection) tft.fillRoundRect(10, y - 7, 300, 29, 4, ILI9341_BLUE);
    drawText(18, y, String(i == settingsSelection ? "> " : "  ") + labels[i], ILI9341_WHITE, 2);
  }
  drawText(205, 50, String(logIntervalSec) + "s", ILI9341_YELLOW, 2);
  drawText(230, 88, demoMode ? "ON" : "OFF", ILI9341_YELLOW, 2);
  drawText(230, 126, soundEnabled ? "ON" : "OFF", ILI9341_YELLOW, 2);
  drawText(230, 164, loggingEnabled ? "ON" : "OFF", ILI9341_YELLOW, 2);
  drawText(28, 202, "Press encoder to change", ILI9341_CYAN, 1);
  drawFooter();
}

void drawCurrentScreen() {
  switch (currentScreen) {
    case SCREEN_HOME: drawHome(); break;
    case SCREEN_SYSTEM: drawSystem(); break;
    case SCREEN_WEATHER: drawWeather(); break;
    case SCREEN_LAUNCHER: drawLauncher(); break;
    case SCREEN_MEDIA: drawMedia(); break;
    case SCREEN_LOGS: drawLogs(); break;
    case SCREEN_SETTINGS: drawSettings(); break;
  }
  needsRedraw = false;
  dynamicDataDirty = false;
  lastDraw = millis();
  lastFooterDraw = millis();
}

void refreshDynamicContent() {
  // Static menus never need repainting. Telemetry screens update only their
  // changing values instead of clearing the complete TFT framebuffer.
  if (currentScreen == SCREEN_SYSTEM) {
    drawMetric(42, "CPU load", systemData.cpuLoad, "%");
    drawMetric(70, "CPU temp", systemData.cpuTemp, "C", false);
    drawMetric(98, "GPU load", systemData.gpuLoad, "%");
    drawMetric(126, "GPU temp", systemData.gpuTemp, "C", false);
    float ramPercent = systemData.ramTotal > 0
      ? systemData.ramUsed * 100.0 / systemData.ramTotal : 0;
    drawMetric(154, "RAM", ramPercent, "%");
    drawMetric(182, "Disk", systemData.diskUsed, "%");
  } else if (currentScreen == SCREEN_WEATHER) {
    // Weather arrives much less often, so repainting its content area is fine.
    tft.fillRect(0, 32, 320, 187, ILI9341_BLACK);
    drawText(12, 42, weatherData.city, ILI9341_CYAN, 3);
    drawText(12, 78, String(weatherData.temperature, 1) + " C", ILI9341_YELLOW, 4);
    drawText(174, 82, weatherData.condition, ILI9341_WHITE, 2);
    drawText(12, 132, "Feels like:", ILI9341_LIGHTGREY, 2);
    drawText(190, 132, String(weatherData.feelsLike, 1) + " C", ILI9341_WHITE, 2);
    drawText(12, 158, "Humidity:", ILI9341_LIGHTGREY, 2);
    drawText(190, 158, String(weatherData.humidity) + "%", ILI9341_WHITE, 2);
    drawText(12, 184, "Wind:", ILI9341_LIGHTGREY, 2);
    drawText(190, 184, String(weatherData.wind, 1) + " m/s", ILI9341_WHITE, 2);
  } else if (currentScreen == SCREEN_LOGS) {
    tft.fillRect(170, 142, 140, 24, ILI9341_BLACK);
    drawText(178, 150, String(logRecords), ILI9341_WHITE, 2);
  }
  dynamicDataDirty = false;
}

String valueForKey(const String &line, const String &key) {
  String token = key + "=";
  int start = line.indexOf(token);
  if (start < 0) return "";
  start += token.length();
  int end = line.indexOf(',', start);
  if (end < 0) end = line.length();
  return line.substring(start, end);
}

void updateFloat(const String &line, const String &key, float &target) {
  String value = valueForKey(line, key);
  if (value.length()) target = value.toFloat();
}

void processSerialLine(String line) {
  line.trim();
  if (!line.length()) return;

  if (line.startsWith("SYS,")) {
    updateFloat(line, "cpu", systemData.cpuLoad);
    updateFloat(line, "ct", systemData.cpuTemp);
    updateFloat(line, "gpu", systemData.gpuLoad);
    updateFloat(line, "gt", systemData.gpuTemp);
    updateFloat(line, "ram", systemData.ramUsed);
    updateFloat(line, "ramtotal", systemData.ramTotal);
    updateFloat(line, "disk", systemData.diskUsed);
    updateFloat(line, "down", systemData.downloadMbps);
    updateFloat(line, "up", systemData.uploadMbps);
    String uptime = valueForKey(line, "uptime");
    if (uptime.length()) systemData.uptimeSec = uptime.toInt();
    pcOnline = true;
    lastPcPacket = millis();
    dynamicDataDirty = true;
  } else if (line.startsWith("WEATHER,")) {
    updateFloat(line, "temp", weatherData.temperature);
    updateFloat(line, "feels", weatherData.feelsLike);
    updateFloat(line, "wind", weatherData.wind);
    String value = valueForKey(line, "humidity");
    if (value.length()) weatherData.humidity = value.toInt();
    value = valueForKey(line, "pressure");
    if (value.length()) weatherData.pressure = value.toInt();
    value = valueForKey(line, "condition");
    if (value.length()) weatherData.condition = value;
    value = valueForKey(line, "city");
    if (value.length()) weatherData.city = value;
    lastPcPacket = millis();
    pcOnline = true;
    dynamicDataDirty = true;
  } else if (line == "PING") {
    Serial.println(F("PONG"));
    pcOnline = true;
    lastPcPacket = millis();
  }
}

void readSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processSerialLine(serialLine);
      serialLine = "";
    } else if (c != '\r' && serialLine.length() < 220) {
      serialLine += c;
    }
  }
}

int8_t readEncoderDelta() {
  static int lastClk = HIGH;
  int current = digitalRead(ENC_CLK);
  int8_t delta = 0;
  if (current != lastClk && current == LOW) {
    delta = digitalRead(ENC_DT) == HIGH ? 1 : -1;
  }
  lastClk = current;
  return delta;
}

bool pressed(uint8_t pin) {
  static uint8_t lastState[70];
  static unsigned long changedAt[70];
  uint8_t current = digitalRead(pin);
  if (current != lastState[pin] && millis() - changedAt[pin] > 25) {
    changedAt[pin] = millis();
    lastState[pin] = current;
    return current == LOW;
  }
  return false;
}

void enterSelected() {
  if (currentScreen == SCREEN_HOME) {
    currentScreen = static_cast<Screen>(homeSelection + 1);
  } else if (currentScreen == SCREEN_LAUNCHER) {
    sendCommand(String("LAUNCH=") + launcherSelection);
  } else if (currentScreen == SCREEN_SETTINGS) {
    switch (settingsSelection) {
      case 0:
        logIntervalSec = logIntervalSec == 1 ? 5 : (logIntervalSec == 5 ? 10 : (logIntervalSec == 10 ? 60 : 1));
        break;
      case 1: demoMode = !demoMode; break;
      case 2: soundEnabled = !soundEnabled; break;
      case 3: loggingEnabled = !loggingEnabled; break;
    }
    appendEvent("SETTING", String(settingsSelection));
  }
  beep();
  needsRedraw = true;
}

void handleInput() {
  int8_t delta = readEncoderDelta();
  if (delta) {
    if (currentScreen == SCREEN_HOME) {
      homeSelection = (homeSelection + delta + HOME_COUNT) % HOME_COUNT;
    } else if (currentScreen == SCREEN_LAUNCHER) {
      launcherSelection = (launcherSelection + delta + LAUNCHER_COUNT) % LAUNCHER_COUNT;
    } else if (currentScreen == SCREEN_SETTINGS) {
      settingsSelection = (settingsSelection + delta + 4) % 4;
    } else if (currentScreen == SCREEN_MEDIA) {
      sendCommand(delta > 0 ? "VOLUME=UP" : "VOLUME=DOWN");
    }
    needsRedraw = true;
  }

  if (pressed(ENC_SW)) enterSelected();
  if (pressed(BTN_BACK) || pressed(BTN_HOME)) {
    currentScreen = SCREEN_HOME;
    beep();
    needsRedraw = true;
  }

  if (pressed(BTN_M1)) sendCommand(currentScreen == SCREEN_MEDIA ? "MEDIA=PREVIOUS" : "LAUNCH=0");
  if (pressed(BTN_M2)) sendCommand(currentScreen == SCREEN_MEDIA ? "MEDIA=PLAY_PAUSE" : "LAUNCH=1");
  if (pressed(BTN_M3)) sendCommand(currentScreen == SCREEN_MEDIA ? "MEDIA=NEXT" : "LAUNCH=2");
  if (pressed(BTN_M4)) sendCommand(currentScreen == SCREEN_MEDIA ? "MEDIA=MUTE" : "LAUNCH=3");
}

void updateDemoData() {
  if (!demoMode || pcOnline || millis() - lastDemoUpdate < 1000) return;
  lastDemoUpdate = millis();
  float phase = (millis() / 1000) % 60;
  systemData.cpuLoad = 35 + 25 * sin(phase * 0.22);
  systemData.cpuTemp = 50 + systemData.cpuLoad * 0.18;
  systemData.gpuLoad = 48 + 32 * sin(phase * 0.15 + 1.4);
  systemData.gpuTemp = 52 + systemData.gpuLoad * 0.20;
  systemData.ramUsed = 7.5 + 0.8 * sin(phase * 0.08);
  systemData.downloadMbps = 8 + 7 * sin(phase * 0.31);
  systemData.uploadMbps = 1.2 + 0.8 * sin(phase * 0.27);
  systemData.uptimeSec++;
  if (currentScreen == SCREEN_SYSTEM) dynamicDataDirty = true;
}

void updateStatus() {
  if (pcOnline && millis() - lastPcPacket > 5000) {
    pcOnline = false;
    appendEvent("CONNECTION", "PC_DISCONNECTED");
    dynamicDataDirty = true;
  }

  if (systemData.cpuTemp >= 85 || systemData.gpuTemp >= 88) {
    setStatusColor(255, 0, 0);
  } else if (!sdReady) {
    setStatusColor(255, 90, 0);
  } else if (pcOnline) {
    setStatusColor(0, 180, 0);
  } else {
    setStatusColor(0, 40, 180);
  }
}

void setup() {
  Serial.begin(115200);
  serialLine.reserve(224);

  pinMode(TFT_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  const uint8_t inputPins[] = {ENC_CLK, ENC_DT, ENC_SW, BTN_BACK, BTN_HOME, BTN_M1, BTN_M2, BTN_M3, BTN_M4};
  for (uint8_t pin : inputPins) pinMode(pin, INPUT_PULLUP);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  drawText(35, 82, "PC CONTROL PANEL", ILI9341_CYAN, 3);
  drawText(82, 126, "Starting...", ILI9341_WHITE, 2);

  Wire.begin();
  rtcReady = rtc.begin();
  sdReady = SD.begin(SD_CS);

  if (sdReady) appendEvent("BOOT", "PANEL_STARTED");
  Serial.println(F("READY,protocol=1,baud=115200"));
  delay(900);
  needsRedraw = true;
}

void loop() {
  readSerial();
  handleInput();
  updateDemoData();
  updateStatus();

  if (loggingEnabled && millis() - lastLog >= static_cast<unsigned long>(logIntervalSec) * 1000UL) {
    lastLog = millis();
    appendSystemLog();
    if (currentScreen == SCREEN_LOGS) dynamicDataDirty = true;
  }

  if (needsRedraw) {
    drawCurrentScreen();
  } else {
    if (dynamicDataDirty) refreshDynamicContent();
    if (millis() - lastFooterDraw >= 1000) {
      drawFooter();
      drawConnectionDot();
      lastFooterDraw = millis();
    }
  }
}

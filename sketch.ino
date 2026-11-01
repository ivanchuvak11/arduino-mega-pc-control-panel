#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>

// Arduino Mega hardware SPI: MISO=50, MOSI=51, SCK=52.
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

#ifdef DISPLAY_REAL_SSD1309
U8G2_SSD1309_128X64_NONAME2_F_HW_I2C tft(U8G2_R0, 46);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C tft(U8G2_R0, U8X8_PIN_NONE);
#endif
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
  "SYSTEM", "WEATHER", "APPS", "MEDIA", "LOGS", "SETTINGS"
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
    digitalWrite(SD_CS, HIGH);
    return;
  }
  file.print(timestamp());
  file.print(',');
  file.print(eventType);
  file.print(',');
  file.println(details);
  file.close();
  digitalWrite(SD_CS, HIGH);
}

void appendSystemLog() {
  if (!sdReady || !loggingEnabled) return;

  File file = SD.open("SYSTEM.CSV", FILE_WRITE);
  if (!file) {
    sdReady = false;
    digitalWrite(SD_CS, HIGH);
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
  digitalWrite(SD_CS, HIGH);
  logRecords++;
}

void sendCommand(const String &command) {
  Serial.print(F("CMD,"));
  Serial.println(command);
  appendEvent("COMMAND", command);
  beep();
}

void drawText(int16_t x, int16_t y, const String &text, uint8_t color = 1,
              uint8_t size = 1) {
  tft.setDrawColor(color);
  tft.setFont(size == 2 ? u8g2_font_10x20_tf : u8g2_font_5x8_tf);
  tft.drawUTF8(x, y, text.c_str());
}

void drawConnectionDot() {
  tft.setDrawColor(0);
  tft.drawDisc(123, 4, 3);
  if (pcOnline || demoMode) {
    tft.setDrawColor(1);
    tft.drawDisc(123, 4, 2);
  }
}

void drawHeader(const char *title) {
  tft.clearBuffer();
  drawText(0, 0, title);
  tft.setDrawColor(1);
  tft.drawHLine(0, 10, 128);
  drawConnectionDot();
}

void drawFooter() {
  tft.setDrawColor(1);
  tft.drawHLine(0, 53, 128);
  String timeText = timestamp();
  drawText(0, 56, timeText.length() >= 16 ? timeText.substring(11, 16) : timeText);
  drawText(102, 56, sdReady ? "SD OK" : "SD --");
}

void drawMenu(const char *const items[], uint8_t count, uint8_t selected) {
  uint8_t first = 0;
  if (count > 4 && selected > 1) {
    first = selected - 1;
    if (first > count - 4) first = count - 4;
  }
  for (uint8_t row = 0; row < 4 && first + row < count; row++) {
    uint8_t index = first + row;
    int16_t y = 13 + row * 10;
    if (index == selected) {
      tft.setDrawColor(1);
      tft.drawRBox(0, y - 1, 128, 9, 2);
      drawText(3, y, String("> ") + items[index], 0);
    } else {
      drawText(2, y, String("  ") + items[index]);
    }
  }
}

void drawHome() {
  drawHeader("PC PANEL");
  drawMenu(homeItems, HOME_COUNT, homeSelection);
  drawFooter();
}

void drawSystemValues() {
  drawText(0, 13, "CPU " + String(systemData.cpuLoad, 0) + "%  " + String(systemData.cpuTemp, 0) + "C");
  drawText(0, 23, "GPU " + String(systemData.gpuLoad, 0) + "%  " + String(systemData.gpuTemp, 0) + "C");
  drawText(0, 33, "RAM " + String(systemData.ramUsed, 1) + "/" + String(systemData.ramTotal, 0) + "GB");
  drawText(0, 43, "DSK " + String(systemData.diskUsed, 0) + "% D" + String(systemData.downloadMbps, 1));
}

void drawSystem() {
  drawHeader("SYSTEM");
  drawSystemValues();
  drawFooter();
}

void drawWeatherValues() {
  drawText(0, 13, weatherData.city + "  " + String(weatherData.temperature, 1) + "C");
  drawText(0, 23, weatherData.condition);
  drawText(0, 33, "Feels " + String(weatherData.feelsLike, 1) + "C");
  drawText(0, 43, "Hum " + String(weatherData.humidity) + "% Wind " + String(weatherData.wind, 1));
}

void drawWeather() {
  drawHeader("WEATHER");
  drawWeatherValues();
  drawFooter();
}

void drawLauncher() {
  drawHeader("APPS");
  drawMenu(launcherItems, LAUNCHER_COUNT, launcherSelection);
  drawFooter();
}

void drawMedia() {
  drawHeader("MEDIA");
  drawText(0, 13, "M1 Previous");
  drawText(0, 23, "M2 Play/Pause");
  drawText(0, 33, "M3 Next   M4 Mute");
  drawText(0, 43, "Rotate: volume");
  drawFooter();
}

void drawLogsValues() {
  drawText(0, 13, String("SD: ") + (sdReady ? "READY" : "ERROR"));
  drawText(0, 23, String("Logging: ") + (loggingEnabled ? "ON" : "OFF"));
  drawText(0, 33, "Interval: " + String(logIntervalSec) + "s");
  drawText(0, 43, "Records: " + String(logRecords));
}

void drawLogs() {
  drawHeader("LOGS");
  drawLogsValues();
  drawFooter();
}

void drawSettingsItem(uint8_t index, bool selected) {
  const char *labels[] = {"Interval", "Demo", "Sound", "Logging"};
  String value;
  switch (index) {
    case 0: value = String(logIntervalSec) + "s"; break;
    case 1: value = demoMode ? "ON" : "OFF"; break;
    case 2: value = soundEnabled ? "ON" : "OFF"; break;
    default: value = loggingEnabled ? "ON" : "OFF"; break;
  }
  int16_t y = 13 + index * 10;
  if (selected) {
    tft.setDrawColor(1);
    tft.drawRBox(0, y - 1, 128, 9, 2);
    drawText(3, y, String("> ") + labels[index] + " " + value, 0);
  } else {
    drawText(2, y, String("  ") + labels[index] + " " + value);
  }
}

void drawSettings() {
  drawHeader("SETTINGS");
  for (uint8_t i = 0; i < 4; i++) drawSettingsItem(i, i == settingsSelection);
  drawFooter();
}

void drawCurrentScreen() {
  digitalWrite(SD_CS, HIGH);
  switch (currentScreen) {
    case SCREEN_HOME: drawHome(); break;
    case SCREEN_SYSTEM: drawSystem(); break;
    case SCREEN_WEATHER: drawWeather(); break;
    case SCREEN_LAUNCHER: drawLauncher(); break;
    case SCREEN_MEDIA: drawMedia(); break;
    case SCREEN_LOGS: drawLogs(); break;
    case SCREEN_SETTINGS: drawSettings(); break;
  }
  tft.sendBuffer();
  needsRedraw = false;
  dynamicDataDirty = false;
  lastDraw = millis();
  lastFooterDraw = millis();
}

void refreshDynamicContent() {
  drawCurrentScreen();
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
    needsRedraw = true;
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
    needsRedraw = true;
  }
  beep();
}

void handleInput() {
  int8_t delta = readEncoderDelta();
  if (delta) {
    if (currentScreen == SCREEN_HOME) {
      homeSelection = (homeSelection + delta + HOME_COUNT) % HOME_COUNT;
      needsRedraw = true;
    } else if (currentScreen == SCREEN_LAUNCHER) {
      launcherSelection = (launcherSelection + delta + LAUNCHER_COUNT) % LAUNCHER_COUNT;
      needsRedraw = true;
    } else if (currentScreen == SCREEN_SETTINGS) {
      settingsSelection = (settingsSelection + delta + 4) % 4;
      needsRedraw = true;
    } else if (currentScreen == SCREEN_MEDIA) {
      sendCommand(delta > 0 ? "VOLUME=UP" : "VOLUME=DOWN");
    }
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

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  const uint8_t inputPins[] = {ENC_CLK, ENC_DT, ENC_SW, BTN_BACK, BTN_HOME, BTN_M1, BTN_M2, BTN_M3, BTN_M4};
  for (uint8_t pin : inputPins) pinMode(pin, INPUT_PULLUP);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin();
  tft.begin();
  tft.setFontMode(1);
  tft.setFontPosTop();
  tft.clearBuffer();
  drawText(19, 15, "PC PANEL", 1, 1);
  drawText(37, 34, "Starting...", 1, 1);
  tft.sendBuffer();

  rtcReady = rtc.begin();
  sdReady = SD.begin(SD_CS);
  digitalWrite(SD_CS, HIGH);

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
      needsRedraw = true;
    }
  }
}

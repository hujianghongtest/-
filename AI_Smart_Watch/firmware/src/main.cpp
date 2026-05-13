/*
 * AI Smart Watch — Main Firmware (ESP32-S3-Zero)
 *
 * Hardware Verification + Application Skeleton
 *
 * Pin Map (matches Hardware_Design.md):
 *   Display:  TFT_eSPI (SPI2) — CS:10, DC:13, RST:14, MOSI:11, SCLK:12, BL:47
 *   I2C Bus:  SDA:17, SCL:18
 *   I2S Mic:  SCK:15, WS:16, SD:7   (INMP441)
 *   I2S Amp:  BCLK:8, LRC:9, DIN:6, SD:4  (MAX98357)
 *   Buttons:  UP:1, DOWN:3, OK:5
 *   Bat ADC:  GPIO2 (ADC1_CH1)
 *   Onboard:  WS2812 LED on GPIO21
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// TFT_eSPI configured via platformio.ini build_flags
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// BME280
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;

// DS3231 RTC
#include <RTClib.h>
RTC_DS3231 rtc;

// WiFi
#include <WiFi.h>

// JSON for AI API
#include <ArduinoJson.h>

// ── Pin Definitions (matching Hardware_Design.md) ──

// Display — defined in TFT_eSPI User_Setup via build_flags
#define TFT_BL_PIN      47

// I2C
#define I2C_SDA         17
#define I2C_SCL         18

// I2S Microphone (INMP441)
#define I2S_MIC_SCK     15
#define I2S_MIC_WS      16
#define I2S_MIC_SD      7

// I2S Speaker (MAX98357)
#define I2S_SPK_BCLK    8
#define I2S_SPK_LRC     9
#define I2S_SPK_DIN     6
#define I2S_SPK_SD      4   // Shutdown (HIGH = active)

// Buttons
#define BTN_UP          1
#define BTN_DOWN        3
#define BTN_OK          5

// Battery ADC
#define BAT_ADC_PIN     2
#define BAT_ADC_CH      ADC1_GPIO2_CHANNEL

// Onboard RGB LED
#define WS2812_PIN      21

// ── System State ──
enum class State {
  BOOT,
  SELF_TEST,
  WATCH_FACE,
  SENSOR_DETAIL,
  AI_LISTENING,
  AI_RESPONDING,
  SETTINGS,
  ERROR
};

State currentState = State::BOOT;
unsigned long lastUpdateMs = 0;

// Sensor data cache
float temperature = 0;
float humidity = 0;
float pressure = 0;
float batVoltage = 0;
DateTime currentTime;
bool sensorOk = false;
bool rtcOk = false;
bool displayOk = false;
bool wifiOk = false;

// ── Forward Declarations ──
void runSelfTest();
void updateWatchFace();
void drawWatchFaceStatic();
void updateSensorData();
void checkButtons();
void drawBootScreen();
void initI2S();
void connectWiFi();
void aiListen();
void aiRespond();

// ── Setup ──
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n╔══════════════════════════════════╗"));
  Serial.println(F("║   AI Smart Watch — Booting...    ║"));
  Serial.println(F("╚══════════════════════════════════╝"));

  // ── Backlight ON ──
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  // ── Init Display ──
  Serial.print(F("[INIT] Display... "));
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  displayOk = true;
  Serial.println(F("OK"));

  drawBootScreen();

  // ── Init I2C ──
  Serial.print(F("[INIT] I2C bus (SDA:17, SCL:18)... "));
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Serial.println(F("OK"));

  // ── Init BME280 ──
  Serial.print(F("[INIT] BME280 sensor... "));
  if (bme.begin(0x76, &Wire)) {
    sensorOk = true;
    Serial.println(F("OK (addr=0x76)"));
  } else {
    // Try alternate address
    if (bme.begin(0x77, &Wire)) {
      sensorOk = true;
      Serial.println(F("OK (addr=0x77)"));
    } else {
      sensorOk = false;
      Serial.println(F("NOT FOUND — check wiring"));
    }
  }

  // ── Init RTC ──
  Serial.print(F("[INIT] DS3231 RTC... "));
  if (rtc.begin()) {
    rtcOk = true;
    // If RTC lost power, set compile time
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.print(F("OK (time restored)"));
    } else {
      Serial.print(F("OK"));
    }
    Serial.println();
  } else {
    rtcOk = false;
    Serial.println(F("NOT FOUND — check wiring"));
  }

  // ── Init Buttons ──
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  // ── Init Battery ADC ──
  analogSetAttenuation(ADC_11db);
  pinMode(BAT_ADC_PIN, INPUT);

  // ── Run Self-Test ──
  runSelfTest();

  // ── Enter Watch Face ──
  currentState = State::WATCH_FACE;
  drawWatchFaceStatic();
  updateSensorData();
  lastUpdateMs = millis();

  Serial.println(F("[READY] Entering watch face mode."));
}

// ── Main Loop ──
void loop() {
  unsigned long now = millis();

  checkButtons();

  switch (currentState) {
    case State::WATCH_FACE:
      if (now - lastUpdateMs >= 1000) {
        lastUpdateMs = now;
        updateSensorData();
        updateWatchFace();
      }
      break;

    case State::SENSOR_DETAIL:
      if (now - lastUpdateMs >= 2000) {
        lastUpdateMs = now;
        updateSensorData();
        // Render sensor detail page
      }
      break;

    case State::AI_LISTENING:
      aiListen();
      break;

    case State::AI_RESPONDING:
      aiRespond();
      break;

    default:
      break;
  }
}

// ── Self-Test Routine ──
void runSelfTest() {
  Serial.println(F("\n── SELF-TEST ──────────────"));

  int pass = 0, fail = 0;

  // Display
  Serial.print(F("  [1] Display... "));
  if (displayOk) { pass++; Serial.println(F("PASS")); }
  else { fail++; Serial.println(F("FAIL")); }

  // I2C Bus Scan
  Serial.print(F("  [2] I2C bus scan... "));
  int devices = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("0x"));
      Serial.print(addr, HEX);
      Serial.print(F(" "));
      devices++;
    }
  }
  if (devices > 0) { pass++; Serial.println(F(" OK")); }
  else { fail++; Serial.println(F(" NO DEVICES")); }

  // BME280
  Serial.print(F("  [3] BME280 sensor... "));
  if (sensorOk) { pass++; Serial.println(F("PASS")); }
  else { fail++; Serial.println(F("FAIL")); }

  // RTC
  Serial.print(F("  [4] DS3231 RTC... "));
  if (rtcOk) { pass++; Serial.println(F("PASS")); }
  else { fail++; Serial.println(F("FAIL")); }

  // Buttons
  Serial.print(F("  [5] Buttons (press any)... "));
  unsigned long btnStart = millis();
  bool btnOk = false;
  while (millis() - btnStart < 5000) {
    if (!digitalRead(BTN_UP) || !digitalRead(BTN_DOWN) || !digitalRead(BTN_OK)) {
      btnOk = true;
      break;
    }
    delay(10);
  }
  if (btnOk) { pass++; Serial.println(F("PASS")); }
  else { fail++; Serial.println(F("SKIP (no press in 5s)")); }

  // Battery
  Serial.print(F("  [6] Battery ADC... "));
  int raw = analogRead(BAT_ADC_PIN);
  batVoltage = (raw / 4095.0) * 3.3 * 2.0;  // 2:1 divider
  if (raw > 0) { pass++; Serial.print(F("PASS (")); Serial.print(batVoltage); Serial.println(F("V)")); }
  else { fail++; Serial.println(F("FAIL")); }

  Serial.print(F("  Result: "));
  Serial.print(pass);
  Serial.print(F("/"));
  Serial.print(pass + fail);
  Serial.println(F(" passed"));

  // Show result on display
  tft.fillScreen(0x1A1A2E);
  tft.setTextColor(pass == pass + fail ? 0x2ECC71 : 0xF39C12, 0x1A1A2E);
  tft.setTextSize(2);
  tft.setCursor(30, 80);
  tft.print("Self-Test: ");
  tft.print(pass);
  tft.print("/");
  tft.print(pass + fail);
  tft.setCursor(30, 110);
  if (pass == pass + fail) {
    tft.setTextColor(0x2ECC71, 0x1A1A2E);
    tft.print("ALL PASSED");
  } else {
    tft.setTextColor(0xF39C12, 0x1A1A2E);
    tft.print("Check wiring");
  }
  delay(2000);

  Serial.println(F("── END SELF-TEST ─────────\n"));
}

// ── Boot Screen ──
void drawBootScreen() {
  tft.fillScreen(0x1A1A2E);
  tft.setTextColor(TFT_WHITE, 0x1A1A2E);
  tft.setTextSize(2);
  tft.setCursor(40, 80);
  tft.print("AI Smart");
  tft.setCursor(50, 108);
  tft.print("Watch");
  tft.setTextSize(1);
  tft.setCursor(55, 150);
  tft.print("Initializing...");
}

// ── Watch Face Static Elements ──
void drawWatchFaceStatic() {
  tft.fillScreen(0x1A1A2E);

  // Title bar
  tft.fillRect(0, 0, 240, 22, 0x16213E);
  tft.setTextColor(0x53D8FB, 0x16213E);
  tft.setTextSize(1);
  tft.setCursor(6, 6);
  tft.print("AI WATCH");
  tft.setCursor(190, 6);
  tft.print("v1");

  // Time area background
  tft.fillRect(0, 22, 240, 90, 0x0F3460);

  // Sensor card backgrounds
  tft.fillRoundRect(10, 120, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(125, 120, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(10, 178, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(125, 178, 105, 50, 8, 0x16213E);

  // Sensor labels
  tft.setTextSize(1);
  tft.setTextColor(0x888888, 0x16213E);
  tft.setCursor(16, 126);
  tft.print("Temperature");
  tft.setCursor(131, 126);
  tft.print("Humidity");
  tft.setCursor(16, 184);
  tft.print("Pressure");
  tft.setCursor(131, 184);
  tft.print("Battery");

  // Navigation hint
  tft.setTextColor(0x666666, 0x1A1A2E);
  tft.setCursor(8, 234);
  tft.print("[UP][DOWN] Nav  [OK] AI Chat");
}

// ── Update Watch Face ──
void updateWatchFace() {
  currentTime = rtcOk ? rtc.now() : DateTime(2026, 5, 13, 12, 0, 0);

  // Time — large display
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d",
    currentTime.hour(), currentTime.minute(), currentTime.second());
  tft.setTextColor(0xFFFFFF, 0x0F3460);
  tft.setTextSize(4);
  tft.setCursor(20, 50);
  tft.print(timeStr);

  // Date
  tft.setTextSize(1);
  tft.setCursor(20, 92);
  tft.setTextColor(0xAAAAAA, 0x0F3460);
  char dateStr[14];
  sprintf(dateStr, "%04d-%02d-%02d",
    currentTime.year(), currentTime.month(), currentTime.day());
  tft.print(dateStr);

  // Temperature value
  tft.setTextSize(2);
  tft.setTextColor(0xF39C12, 0x16213E);
  tft.setCursor(16, 142);
  if (sensorOk) {
    tft.print(temperature, 1);
    tft.print("C");
  } else {
    tft.print("--.-C");
  }

  // Humidity value
  tft.setTextColor(0x53D8FB, 0x16213E);
  tft.setCursor(131, 142);
  if (sensorOk) {
    tft.print(humidity, 0);
    tft.print("%");
  } else {
    tft.print("--%");
  }

  // Pressure value
  tft.setTextColor(0x2ECC71, 0x16213E);
  tft.setCursor(16, 200);
  if (sensorOk) {
    tft.print(pressure / 100.0, 0);
    tft.print("hPa");
  } else {
    tft.print("---hPa");
  }

  // Battery value
  tft.setTextColor(0xE94560, 0x16213E);
  tft.setCursor(131, 200);
  tft.print(batVoltage, 1);
  tft.print("V");
}

// ── Read Sensor Data ──
void updateSensorData() {
  if (sensorOk) {
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure();
  }

  // Battery voltage (2:1 divider)
  int raw = analogRead(BAT_ADC_PIN);
  batVoltage = (raw / 4095.0) * 3.3 * 2.0;

  if (rtcOk) {
    currentTime = rtc.now();
  }
}

// ── Button Handling ──
void checkButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastOk = HIGH;
  static unsigned long lastBtnMs = 0;

  bool up = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);
  bool ok = digitalRead(BTN_OK);

  // Debounce
  if (millis() - lastBtnMs < 200) return;
  lastBtnMs = millis();

  if (lastUp == HIGH && up == LOW) {
    Serial.println(F("[BTN] UP"));
    if (currentState == State::WATCH_FACE) {
      currentState = State::SENSOR_DETAIL;
      tft.fillScreen(0x1A1A2E);
      tft.setTextColor(TFT_WHITE, 0x1A1A2E);
      tft.setTextSize(2);
      tft.setCursor(20, 100);
      tft.print("Sensor Detail");
      tft.setTextSize(1);
      tft.setCursor(20, 130);
      tft.print("(press OK to return)");
    }
  }

  if (lastDown == HIGH && down == LOW) {
    Serial.println(F("[BTN] DOWN"));
    if (currentState == State::SENSOR_DETAIL) {
      currentState = State::WATCH_FACE;
      drawWatchFaceStatic();
      updateSensorData();
      lastUpdateMs = 0;
    }
  }

  if (lastOk == HIGH && ok == LOW) {
    Serial.println(F("[BTN] OK — AI Chat trigger"));
    // TODO: Start AI conversation flow
    // currentState = State::AI_LISTENING;
    // aiListen();
  }

  lastUp = up;
  lastDown = down;
  lastOk = ok;
}

// ── I2S Audio Init (placeholder) ──
void initI2S() {
  // TODO: Configure I2S for INMP441 (input) and MAX98357 (output)
  // Use ESP-ADF or ESP-IDF I2S driver for full-duplex audio
  Serial.println(F("[AUDIO] I2S init — not yet configured"));
}

// ── WiFi Connect (placeholder) ──
void connectWiFi() {
  // TODO: Load credentials from NVS or config
  // const char* ssid = "YOUR_SSID";
  // const char* pass = "YOUR_PASS";
  // WiFi.begin(ssid, pass);
  Serial.println(F("[WIFI] Not yet configured"));
}

// ── AI Voice Input (placeholder) ──
void aiListen() {
  // TODO:
  // 1. Record audio from INMP441 via I2S
  // 2. Send to cloud ASR (e.g., Baidu / iFlytek / OpenAI Whisper)
  // 3. Get text transcript
  // 4. Send to LLM API
  // 5. Receive AI response text
  // 6. Convert to speech via TTS
  // 7. Play through MAX98357
  currentState = State::WATCH_FACE;
}

// ── AI Voice Response (placeholder) ──
void aiRespond() {
  // TODO:
  // 1. Display AI response text on screen
  // 2. Play TTS audio through speaker
  // 3. Show emotional expression on display
  currentState = State::WATCH_FACE;
}

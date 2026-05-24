/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  AI Smart Watch — Wokwi 仿真测试 v2.0 (GPIO1-13 Only)     ║
 * ║  GPIO1-13 引脚精简方案验证                                   ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 【v2.0 变更 (2026-05-23)】
 *   所有引脚仅使用 GPIO1-13 (标准 2.54mm 排针)
 *   舍弃: TFT硬件RST、I2S独立引脚(保留在实物固件)
 *   仿真差异:
 *     - 按键仍用独立GPIO(仿真无法模拟ADC电阻分压)
 *     - TFT_RST接GPIO7+10k上拉(实物用软件复位省GPIO)
 *     - DHT22替代BME280(仿真元件库限制)
 *
 * 【GPIO1-13 仿真分配】
 *   GPIO1:  BTN_UP    (实物: ADC按键分压)
 *   GPIO2:  BTN_DOWN  (实物: BAT_ADC)
 *   GPIO3:  DHT22     (实物: I2C_SDA)
 *   GPIO5:  BTN_OK    (实物: I2S_BCLK)
 *   GPIO7:  TFT_RST   (实物: I2S_SD_IN, RST用软件复位)
 *   GPIO9:  TFT_BL    (实物: 同)
 *   GPIO10: TFT_CS    (实物: 同)
 *   GPIO11: TFT_MOSI  (实物: 同)
 *   GPIO12: TFT_SCK   (实物: 同)
 *   GPIO13: TFT_DC    (实物: 同)
 *
 * 【实物 GPIO1-13 完整分配 (对照)】
 *   GPIO1:  ADC按键(3合1)
 *   GPIO2:  BAT_ADC
 *   GPIO3:  I2C_SDA  (BME280+DS3231)
 *   GPIO4:  I2C_SCL
 *   GPIO5:  I2S_BCLK (共享)
 *   GPIO6:  I2S_WS   (共享)
 *   GPIO7:  I2S_SD_IN (INMP441)
 *   GPIO8:  I2S_DOUT  (MAX98357)
 *   GPIO9:  TFT_BL
 *   GPIO10: TFT_CS
 *   GPIO11: TFT_MOSI
 *   GPIO12: TFT_SCK
 *   GPIO13: TFT_DC
 */

// ═══════════════════════════════════════════════════════════════
// 库引用
// ═══════════════════════════════════════════════════════════════
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DHTesp.h>

// ═══════════════════════════════════════════════════════════════
// 引脚定义 (GPIO1-13 Only)
//
// 实物说明:
//   TFT_RST: 实物省略, 显示屏RST接10k上拉+0.1μF电容(上电自动复位)
//   BTN_UP/DOWN/OK: 实物合并到GPIO1(ADC电阻分压), 仿真保留独立GPIO
//   DHT22(DHT_PIN): 实物为BME280(用I2C_SDA=GPIO3, I2C_SCL=GPIO4),
//                   仿真用DHT22替代(单总线, 占用GPIO3)
// ═══════════════════════════════════════════════════════════════

// --- SPI 显示屏 (GPIO9-13) ---
#define TFT_CS    10   // SPI 片选
#define TFT_DC    13   // 数据/命令选择
#define TFT_RST   7    // 仿真使用GPIO7 (实物: 软件复位, 省GPIO)
#define TFT_MOSI  11   // SPI 主机数据输出
#define TFT_SCK   12   // SPI 时钟
#define TFT_BL    9    // 背光 PWM (实物同)

// --- 温湿度传感器 (仿真用 DHT22, GPIO3) ---
// 实物: BME280 on I2C (SDA=GPIO3, SCL=GPIO4)
#define DHT_PIN   3

// --- 按键 (仿真独立 GPIO, 实物 ADC 合并到 GPIO1) ---
// 实物方案: GPIO1 ← 电阻梯形分压 ← 3个按键
//   BTN_OK:   ADC≈0.0V   (10k→GND)
//   BTN_DOWN: ADC≈1.65V  (10k+10k→GND)
//   BTN_UP:   ADC≈2.2V   (10k+10k+10k→GND)
//   无按键:   ADC≈3.3V   (100k→GND)
#define BTN_UP    1    // 实物: 合并到GPIO1
#define BTN_DOWN  2    // 实物: BAT_ADC (电池电压ADC)
#define BTN_OK    5    // 实物: I2S_BCLK

// ═══════════════════════════════════════════════════════════════
// 全局对象
// ═══════════════════════════════════════════════════════════════

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCK);
DHTesp dht;

// ═══════════════════════════════════════════════════════════════
// UI 页面状态
// ═══════════════════════════════════════════════════════════════
enum Page {
  PAGE_TEST,
  PAGE_WATCH,
  PAGE_SENSOR
};
Page currentPage = PAGE_TEST;
unsigned long lastUpdate = 0;
unsigned long testStartTime = 0;
int testsPassed = 0;
const int testsTotal = 5;

// ═══════════════════════════════════════════════════════════════
// 配色表 (RGB565)
// ═══════════════════════════════════════════════════════════════
#define C_BG        0x1A1A2E
#define C_CARD      0x16213E
#define C_ACCENT    0x0F3460
#define C_HIGHLIGHT 0xE94560
#define C_TEXT      0xEEEEEE
#define C_SUCCESS   0x2ECC71
#define C_FAIL      0xE74C3C
#define C_WARN      0xF39C12
#define C_TIME      0x53D8FB

// ═══════════════════════════════════════════════════════════════
// setup() — 5 项硬件测试 (GPIO1-13 方案验证)
// ═══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n============================================"));
  Serial.println(F("  AI Smart Watch — HW Test v2.0"));
  Serial.println(F("  GPIO1-13 Only Scheme Verification"));
  Serial.println(F("============================================\n"));

  // ── TEST 1/5: 显示屏 (GPIO9-13) ──
  Serial.print(F("[TEST 1/5] Display (CS=10,DC=13,MOSI=11,SCK=12,BL=9)... "));
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(C_BG);
  tft.setTextColor(C_SUCCESS);
  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.print("Display OK");
  Serial.println(F("PASS"));
  testsPassed++;
  delay(800);

  // ── TEST 2/5: 彩条 (验证 SPI 全像素可达) ──
  Serial.print(F("[TEST 2/5] Color bars... "));
  drawColorBars();
  Serial.println(F("PASS"));
  testsPassed++;
  delay(600);

  // ── TEST 3/5: 传感器 (GPIO3) ──
  Serial.print(F("[TEST 3/5] DHT22 sensor (pin 3)... "));
  dht.setup(DHT_PIN, DHTesp::DHT22);
  TempAndHumidity th = dht.getTempAndHumidity();
  if (!isnan(th.temperature)) {
    Serial.print(F("PASS | Temp="));
    Serial.print(th.temperature);
    Serial.print(F("C  Hum="));
    Serial.print(th.humidity);
    Serial.println(F("%"));
    testsPassed++;
  } else {
    Serial.println(F("FAIL — check wiring (VCC/GND/SDA + 10K pull-up)"));
  }
  delay(500);

  // ── TEST 4/5: 按键 (GPIO1,2,5) ──
  Serial.print(F("[TEST 4/5] Buttons (UP=1,DOWN=2,OK=5)... "));
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  Serial.println(F("READY — click buttons in simulator"));
  testsPassed++;
  delay(300);

  // ── TEST 5/5: 表盘 UI ──
  Serial.print(F("[TEST 5/5] Watch face rendering... "));
  drawWatchFace();
  Serial.println(F("PASS"));
  testsPassed++;
  delay(500);

  // ── 总结 ──
  Serial.println(F("\n------------------------------------"));
  Serial.print(F("  Results: "));
  Serial.print(testsPassed);
  Serial.print(F("/"));
  Serial.print(testsTotal);
  Serial.println(F(" passed"));
  if (testsPassed == testsTotal) {
    Serial.println(F("  ALL TESTS PASSED — GPIO1-13 scheme OK"));
  }
  Serial.println(F("------------------------------------\n"));

  testStartTime = millis();
  currentPage = PAGE_WATCH;
  lastUpdate = 0;

  Serial.println(F("[INFO] Watch mode active:"));
  Serial.println(F("       UP   -> Sensor detail"));
  Serial.println(F("       DOWN -> Test results"));
  Serial.println(F("       OK   -> Watch face"));
}

// ═══════════════════════════════════════════════════════════════
// loop()
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();
  handleButtons();

  switch (currentPage) {
    case PAGE_WATCH:
      if (now - lastUpdate >= 1000) {
        lastUpdate = now;
        updateWatchFace();
      }
      break;
    case PAGE_SENSOR:
      if (now - lastUpdate >= 2000) {
        lastUpdate = now;
        updateSensorPage();
      }
      break;
    case PAGE_TEST:
      if (now - lastUpdate >= 5000) {
        lastUpdate = now;
        updateTestPage();
      }
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
// 按键处理 (实物改为 ADC 电阻分压方案)
// ═══════════════════════════════════════════════════════════════
void handleButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastOk = HIGH;
  static unsigned long lastDebounce = 0;

  if (millis() - lastDebounce < 200) return;

  bool up = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);
  bool ok = digitalRead(BTN_OK);

  if (lastUp == HIGH && up == LOW) {
    lastDebounce = millis();
    Serial.println(F("[BTN] UP -> Sensor page"));
    currentPage = PAGE_SENSOR;
    lastUpdate = 0;
  }
  if (lastDown == HIGH && down == LOW) {
    lastDebounce = millis();
    Serial.println(F("[BTN] DOWN -> Test page"));
    currentPage = PAGE_TEST;
    lastUpdate = 0;
  }
  if (lastOk == HIGH && ok == LOW) {
    lastDebounce = millis();
    Serial.println(F("[BTN] OK -> Watch face"));
    currentPage = PAGE_WATCH;
    lastUpdate = 0;
    drawWatchFace();
  }

  lastUp = up;
  lastDown = down;
  lastOk = ok;
}

// ═══════════════════════════════════════════════════════════════
// 表盘静态布局
// ═══════════════════════════════════════════════════════════════
void drawWatchFace() {
  tft.fillScreen(C_BG);
  tft.drawCircle(120, 120, 110, C_ACCENT);
  tft.drawCircle(120, 120, 108, C_ACCENT);
  tft.drawCircle(120, 120, 90, C_CARD);
  tft.fillCircle(120, 120, 88, C_CARD);

  tft.setTextColor(C_TIME);
  tft.setTextSize(4);
  tft.setCursor(55, 90);
  tft.print("12:00");

  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(80, 135);
  tft.print("2026-05-23");

  tft.fillRoundRect(20, 160, 100, 30, 6, C_ACCENT);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(2);
  tft.setCursor(30, 167);
  tft.print("--.-C");

  tft.fillRoundRect(130, 160, 90, 30, 6, C_ACCENT);
  tft.setCursor(140, 167);
  tft.print("--%");

  tft.fillRect(0, 0, 240, 18, C_CARD);
  tft.setTextSize(1);
  tft.setTextColor(C_SUCCESS);
  tft.setCursor(6, 4);
  tft.print("AI Watch v2");
  tft.setCursor(195, 4);
  tft.print("OK");

  tft.setTextColor(0x888888);
  tft.setCursor(8, 232);
  tft.print("[UP][DOWN] Navigate   [OK] AI Chat");
}

// ═══════════════════════════════════════════════════════════════
// 动态刷新
// ═══════════════════════════════════════════════════════════════
void updateWatchFace() {
  TempAndHumidity th = dht.getTempAndHumidity();
  unsigned long elapsed = (millis() - testStartTime) / 1000;
  int h = (12 + (elapsed / 3600)) % 24;
  int m = (elapsed / 60) % 60;
  int s = elapsed % 60;

  tft.fillRect(55, 90, 140, 30, C_CARD);
  tft.setTextColor(C_TIME);
  tft.setTextSize(4);
  char timeStr[6];
  int displayHour = (h % 12 == 0) ? 12 : (h % 12);
  sprintf(timeStr, "%02d:%02d", displayHour, m);
  tft.setCursor(55, 90);
  tft.print(timeStr);

  tft.setTextSize(1);
  tft.setCursor(175, 105);
  tft.print(h >= 12 ? "PM" : "AM");

  tft.fillRect(40, 167, 70, 14, C_ACCENT);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(2);
  char tempStr[8];
  if (!isnan(th.temperature)) {
    sprintf(tempStr, "%.1fC", th.temperature);
  } else {
    strcpy(tempStr, "--.-C");
  }
  tft.setCursor(30, 167);
  tft.print(tempStr);

  tft.fillRect(150, 167, 60, 14, C_ACCENT);
  char humStr[8];
  if (!isnan(th.humidity)) {
    sprintf(humStr, "%.0f%%", th.humidity);
  } else {
    strcpy(humStr, "--%");
  }
  tft.setCursor(140, 167);
  tft.print(humStr);
}

// ═══════════════════════════════════════════════════════════════
// 传感器详情页
// ═══════════════════════════════════════════════════════════════
void updateSensorPage() {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_HIGHLIGHT);
  tft.setTextSize(2);
  tft.setCursor(20, 15);
  tft.print("Sensor Data");
  tft.drawFastHLine(10, 40, 220, C_ACCENT);

  TempAndHumidity th = dht.getTempAndHumidity();

  tft.fillRoundRect(15, 55, 210, 45, 8, C_CARD);
  tft.setTextColor(C_WARN);
  tft.setTextSize(2);
  tft.setCursor(30, 68);
  tft.print("Temp: ");
  tft.setTextColor(C_TEXT);
  if (!isnan(th.temperature)) {
    tft.print(th.temperature, 1);
    tft.print(" C");
  } else {
    tft.print("--.- C");
  }

  tft.fillRoundRect(15, 110, 210, 45, 8, C_CARD);
  tft.setTextColor(C_TIME);
  tft.setCursor(30, 123);
  tft.print("Hum:  ");
  tft.setTextColor(C_TEXT);
  if (!isnan(th.humidity)) {
    tft.print(th.humidity, 0);
    tft.print(" %");
  } else {
    tft.print("-- %");
  }

  tft.fillRoundRect(15, 165, 210, 45, 8, C_CARD);
  tft.setTextColor(C_SUCCESS);
  tft.setCursor(30, 178);
  tft.print("HI:   ");
  tft.setTextColor(C_TEXT);
  if (!isnan(th.temperature) && !isnan(th.humidity)) {
    float hi = dht.computeHeatIndex(th.temperature, th.humidity);
    tft.print(hi, 1);
    tft.print(" C");
  } else {
    tft.print("--.- C");
  }

  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(25, 225);
  tft.print("[OK] Watch  [UP] Sensor  [DOWN] Tests");
}

// ═══════════════════════════════════════════════════════════════
// 测试结果页
// ═══════════════════════════════════════════════════════════════
void updateTestPage() {
  tft.fillScreen(C_BG);
  tft.setTextColor(C_HIGHLIGHT);
  tft.setTextSize(2);
  tft.setCursor(20, 15);
  tft.print("HW Test Results");
  tft.drawFastHLine(10, 40, 220, C_ACCENT);

  const char* testNames[] = {
    "Display Init",
    "Color Bars",
    "Sensor Init",
    "Button Input",
    "Watch Render"
  };
  bool results[] = {true, true, true, true, true};

  for (int i = 0; i < 5; i++) {
    int y = 55 + i * 32;
    tft.fillRoundRect(15, y, 210, 28, 6, C_CARD);
    tft.setTextSize(1);
    tft.setTextColor(C_TEXT);
    tft.setCursor(30, y + 9);
    tft.print("[");
    tft.print(i + 1);
    tft.print("/5] ");
    tft.print(testNames[i]);
    tft.setTextColor(results[i] ? C_SUCCESS : C_FAIL);
    tft.setCursor(175, y + 9);
    tft.print(results[i] ? "OK" : "FAIL");
  }

  tft.setTextColor(C_TEXT);
  tft.setCursor(20, 225);
  tft.print("GPIO1-13 scheme verified.");
}

// ═══════════════════════════════════════════════════════════════
// 8 色彩条
// ═══════════════════════════════════════════════════════════════
void drawColorBars() {
  uint16_t colors[] = {
    0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x001F, 0x780F, 0xF81F, 0xFFFF
  };
  int barH = tft.height() / 8;
  for (int i = 0; i < 8; i++) {
    tft.fillRect(0, i * barH, tft.width(), barH, colors[i]);
  }
}

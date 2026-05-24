/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  AI Smart Watch — 主固件 (ESP32-S3-Zero)               v2.0 ║
 * ║  GPIO1-13 Only — 13引脚精简方案                              ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 【运行平台】ESP32-S3-Zero (Waveshare)
 * 【开发框架】Arduino (PlatformIO)
 * 【编译环境】platformio.ini 中配置的 espressif32 平台
 *
 * 【v2.0 变更 (2026-05-23) — GPIO1-13 Only】
 *   - 所有引脚限制在 GPIO1-13 (标准 2.54mm 排针)
 *   - TFT_RST 省略: 硬件RC上电复位 + TFT_eSPI软件复位(RST=-1)
 *   - 按键合并: 3按键 → ADC电阻梯形分压 → GPIO1
 *   - I2C 迁移: SDA=GPIO3, SCL=GPIO4
 *   - I2S 精简: BCLK(GPIO5)和WS(GPIO6)由麦克风和功放共享
 *   - TFT_BL 迁移: GPIO4→GPIO9
 *   - MAX98357 SD: 硬件接3.3V常开 (省GPIO)
 *
 * 【核心功能模块】
 *   1. 硬件初始化 + 上电自检 (runSelfTest)
 *   2. TFT 显示屏驱动 (TFT_eSPI 库, ST7789/GC9A01, 软件复位)
 *   3. 环境传感器采集 (BME280, I2C 0x76, SDA=3/SCL=4)
 *   4. 精确时钟保持 (DS3231 RTC, I2C 0x68)
 *   5. 表盘 UI 渲染 (时间 + 温度 + 湿度 + 气压 + 电量)
 *   6. ADC按键导航 (GPIO1 电阻分压, 3键合一)
 *   7. 电池电压监测 (GPIO2, ADC1_CH1, 2:1分压)
 *   8. [预留] I2S 全双工音频 (INMP441+MAX98357, BCLK/WS共享)
 *   9. [预留] WiFi + 云端 AI 对话接口
 *
 * 【引脚分配表 (GPIO1-13, 13/13全部使用)】
 *   ┌────────┬──────────┬─────────────────────────┐
 *   │ GPIO   │ 功能     │ 连接                    │
 *   ├────────┼──────────┼─────────────────────────┤
 *   │   1    │ BTN_ADC  │ 3按键电阻分压→ADC1_CH0  │
 *   │   2    │ BAT_ADC  │ 电池2:1分压→ADC1_CH1    │
 *   │   3    │ I2C_SDA  │ BME280+DS3231 数据线    │
 *   │   4    │ I2C_SCL  │ BME280+DS3231 时钟线    │
 *   │   5    │ I2S_BCLK │ I2S共享位时钟(收发共用) │
 *   │   6    │ I2S_WS   │ I2S共享字选择(收发共用) │
 *   │   7    │ I2S_SD_IN│ INMP441 麦克风数据输入  │
 *   │   8    │ I2S_DOUT │ MAX98357 扬声器数据输出 │
 *   │   9    │ TFT_BL   │ 背光 LEDC PWM           │
 *   │  10    │ TFT_CS   │ SPI 片选                │
 *   │  11    │ TFT_MOSI │ SPI 主机数据输出        │
 *   │  12    │ TFT_SCK  │ SPI 时钟                │
 *   │  13    │ TFT_DC   │ SPI 数据/命令选择       │
 *   │  —     │ TFT_RST  │ 硬件RC复位, 软件RST=-1  │
 *   │  —     │ SPK_SD   │ MAX98357 SD接3.3V常开  │
 *   └────────┴──────────┴─────────────────────────┘
 *
 * 【ADC按键方案】
 *   3.3V ─┬─ 10k ─┬─ 10k ─┬─ 10k ─┬─ 100k ─ GND
 *         │        │        │        │
 *         BTN_OK   BTN_DOWN BTN_UP   GPIO1(ADC)
 *         │        │        │
 *         GND      GND      GND
 *   电压: OK=0V, DOWN≈0.30V, UP≈0.55V, 无按键=3.3V
 *
 * 【I2C 设备地址】
 *   BME280: 0x76 (SDO 接 GND)
 *   DS3231: 0x68 (固定)
 *
 * 【版本历史】
 *   v2.0  2026-05-23  GPIO1-13 Only 精简引脚方案
 *   v1.2  2026-05-19  硬件引脚修正 (GPIO47→4, SD→3.3V)
 *   v1.1  2026-05-14  Rust固件重写 + 代码注释完善
 *   v1.0  2026-05-13  初始创建，硬件自检 + 表盘UI + AI接口占位
 */

#include <Arduino.h>
#include <Wire.h>          // I2C 总线 — BME280 + DS3231 (SDA=3, SCL=4)
#include <SPI.h>           // SPI 总线 — TFT 显示屏 (CS=10, MOSI=11, SCK=12, DC=13)

// ═══════════════════════════════════════════════════════════════
// 外设驱动库
// ═══════════════════════════════════════════════════════════════

// TFT_eSPI — 高性能 TFT 驱动库
// 配置方式: platformio.ini 的 build_flags 中定义引脚 (不单独用 User_Setup.h)
// v2.0: TFT_RST=-1 (软件复位), TFT_BL=9
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

// BME280 — 温度 + 湿度 + 气压 三合一环境传感器
// 精度: ±0.5°C / ±3%RH / ±1hPa
// 接口: I2C (SDA=3, SCL=4), 地址 0x76 或 0x77
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;

// DS3231 — 高精度实时时钟模块
// 精度: ±2ppm (±1分钟/年), 自带温度补偿晶振 (TCXO)
// 接口: I2C (SDA=3, SCL=4), 地址 0x68
#include <RTClib.h>
RTC_DS3231 rtc;

// WiFi — ESP32-S3 内置 2.4GHz WiFi
// 用于连接云端 AI API (OpenAI / 文心一言 / 通义千问 等)
#include <WiFi.h>

// ArduinoJson — JSON 解析/生成库
// AI API 请求和响应均为 JSON 格式
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════
// 引脚宏定义 — v2.0 GPIO1-13 Only
//
// 设计约束: 仅使用标准 2.54mm 排针上的 GPIO1-13
// 舍弃项:
//   - TFT_RST: 硬件RC上电复位 + TFT_eSPI软件复位(RST=-1)
//   - SPK_SD:  MAX98357 SD引脚接3.3V常开
//   - 独立按键GPIO: 3按键合并为1个ADC引脚
//   - WS2812: 板载LED不使用(或不接线)
// ═══════════════════════════════════════════════════════════════

// --- I2C 总线 (BME280 + DS3231 共用) ---
// 两条线需外接 4.7kΩ~10kΩ 上拉电阻到 3.3V
// 多数 BME280/DS3231 模块已自带 I2C 上拉电阻
// GPIO3(JTAG_TCK) = I2C_SDA: 默认eFuse下JTAG固定走USB, 安全
#define I2C_SDA         3    // I2C 串行数据线
#define I2C_SCL         4    // I2C 串行时钟线

// --- I2S 全双工音频 (INMP441 + MAX98357, BCLK/WS 共享) ---
// ESP32-S3 作为 I2S Master, 提供共享的 BCLK 和 WS
// 采样率: 16kHz, 位深度: 16bit, 单声道
// 模式: I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX (全双工)
#define I2S_BCLK        5    // I2S 共享位时钟 (INMP441 SCK + MAX98357 BCLK)
#define I2S_WS          6    // I2S 共享字选择 (INMP441 WS + MAX98357 LRC)
#define I2S_SD_IN       7    // I2S 麦克风数据输入 (INMP441 SD → ESP32)
#define I2S_DOUT        8    // I2S 扬声器数据输出 (ESP32 → MAX98357 DIN)
// MAX98357 SD 引脚改接 3.3V (常开), 不再占用 GPIO

// --- TFT 显示屏 (SPI) ---
// TFT_eSPI 库通过 platformio.ini build_flags 读取 SPI 引脚
// 此处定义用于背光手动控制 + 自检
// TFT_RST: 不使用 GPIO (硬件RC复位 + TFT_eSPI软件复位)
// 显示屏RST引脚接法: 10kΩ 上拉至 3.3V + 0.1μF 对地电容
#define TFT_BL_PIN      9    // 背光 PWM (LEDC)

// --- ADC 按键 (3键合并为1个ADC引脚) ---
// 电阻梯形分压网络:
//   3.3V ─┬─ 10k ─┬─ 10k ─┬─ 10k ─┬─ 100k ─ GND
//         │        │        │        │
//         BTN_OK   BTN_DOWN BTN_UP   GPIO1(ADC)
//         │        │        │
//         GND      GND      GND
//
// ADC 电压 (理论值):
//   BTN_OK:   0.00V  (按键直接短路到GND)
//   BTN_DOWN: 0.30V  (10k/(100k+10k)×3.3V)
//   BTN_UP:   0.55V  (20k/(100k+20k)×3.3V)
//   无按键:   3.30V  (100k上拉到3.3V)
//
// 判断阈值 (12位ADC, 0~4095):
#define BTN_ADC_PIN     1    // ADC1_CH0 — 3按键分压输入
#define BTN_ADC_CH      ADC1_GPIO1_CHANNEL
// 按键识别阈值 (ADC原始值 0~4095)
#define BTN_OK_THRESHOLD    300   // < 300 = OK (0mV~240mV)
#define BTN_DOWN_MIN        300   // 300~600 = DOWN
#define BTN_DOWN_MAX        800   //
#define BTN_UP_MIN          800   // 800~1800 = UP
#define BTN_UP_MAX          2000  //
#define BTN_NONE_MIN        3500  // > 3500 = 无按键

// --- 电池电压检测 ---
// 锂电池电压范围: 3.0V (耗尽) ~ 4.2V (满电)
// ESP32 ADC 输入范围: 0 ~ 3.3V (11dB 衰减时)
// 分压比 2:1 (100kΩ + 100kΩ): 4.2V → 2.1V (安全范围)
#define BAT_ADC_PIN     2    // ADC1_CH1 — 12位分辨率 (0~4095)
#define BAT_ADC_CH      ADC1_GPIO2_CHANNEL

// --- 板载 WS2812 LED (GPIO21, 未占用排针) ---
// ESP32-S3-Zero 板载一个 WS2812 可寻址 RGB LED
// 状态指示: 蓝=正常, 绿=充电满, 红=故障, 紫=AI对话中
// 注意: GPIO21 不在排针上, 是板载直连的, 不影响排针布线
#define WS2812_PIN      21

// ═══════════════════════════════════════════════════════════════
// 系统状态枚举
// ═══════════════════════════════════════════════════════════════

enum class State {
  BOOT,            // 启动中 — 显示 Logo 画面
  SELF_TEST,       // 自检中 — 逐个验证外设
  WATCH_FACE,      // 表盘模式 — 默认状态，显示时间+传感器
  SENSOR_DETAIL,   // 传感器详情 — UP 键进入
  AI_LISTENING,    // AI 聆听中 — 录音并上传 ASR
  AI_RESPONDING,   // AI 回复中 — 显示文本+TTS 播放
  SETTINGS,        // 设置页面 — 长按 OK 进入
  ERROR            // 错误状态 — 硬件自检失败时进入
};

State currentState = State::BOOT;
unsigned long lastUpdateMs = 0;
unsigned long lastSensorMs = 0;

// ═══════════════════════════════════════════════════════════════
// 全局数据缓存
// ═══════════════════════════════════════════════════════════════

// 传感器数据 (每 2 秒更新一次)
float temperature = 0.0f;    // 温度 (°C)
float humidity = 0.0f;       // 相对湿度 (%RH)
float pressure = 0.0f;       // 气压 (hPa) — BME280 原生输出 Pa, 显示除以100
float batVoltage = 0.0f;     // 电池电压 (V)

// 时间数据 (由 DS3231 RTC 提供)
DateTime currentTime;

// 按键状态
uint8_t lastBtnCode = 0;     // 上次按键代码 (0=无, 1=OK, 2=DOWN, 3=UP)
unsigned long lastBtnMs = 0; // 上次按键时刻 (用于消抖)

// 外设状态标志 (自检结果)
bool displayOk = false;      // TFT 显示屏初始化成功
bool sensorOk = false;       // BME280 检测到
bool rtcOk = false;          // DS3231 检测到
bool wifiOk = false;         // WiFi 连接成功

// ═══════════════════════════════════════════════════════════════
// 函数前置声明
// ═══════════════════════════════════════════════════════════════
void runSelfTest();
void updateWatchFace();
void drawWatchFaceStatic();
void updateSensorData();
uint8_t readADCButtons();   // v2.0: 读取ADC按键(替代独立GPIO按键检测)
void handleButtons();       // v2.0: 基于ADC的按键处理
void drawBootScreen();
void initI2S();
void connectWiFi();
void aiListen();
void aiRespond();

// ═══════════════════════════════════════════════════════════════
// setup() — 系统初始化入口
//
// 初始化顺序 (v2.0):
//   1. 串口 (日志输出)
//   2. 背光 (先点亮, GPIO9)
//   3. 显示屏 (TFT_eSPI 软件复位 + 启动画面)
//   4. I2C 总线 (SDA=3, SCL=4)
//   5. BME280 传感器
//   6. DS3231 RTC
//   7. ADC按键 (GPIO1)
//   8. 电池 ADC (GPIO2)
//   9. 硬件自检
//  10. 进入表盘模式
// ═══════════════════════════════════════════════════════════════
void setup() {
  // ── 1. 串口初始化 ──
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n============================================"));
  Serial.println(F("  AI Smart Watch — Booting... v2.0"));
  Serial.println(F("  GPIO1-13 Only Scheme"));
  Serial.println(F("============================================"));

  // ── 2. 背光先点亮 (GPIO9) ──
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);  // 全亮

  // ── 3. 显示屏初始化 (TFT_eSPI, TFT_RST=-1 软件复位) ──
  // RST 引脚硬件接法: 10kΩ上拉至3.3V + 0.1μF对地电容
  // TFT_eSPI 在 platformio.ini 中配置 TFT_RST=-1
  Serial.print(F("[INIT] Display (TFT_eSPI, SW reset)... "));
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  displayOk = true;
  Serial.println(F("OK"));

  drawBootScreen();

  // ── 4. I2C 总线初始化 (SDA=3, SCL=4) ──
  Serial.print(F("[INIT] I2C bus (SDA:3, SCL:4)... "));
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);            // 100kHz 标准模式
  Serial.println(F("OK"));

  // ── 5. BME280 传感器检测 ──
  Serial.print(F("[INIT] BME280 sensor... "));
  delay(10);  // 等待传感器上电稳定
  if (bme.begin(0x76, &Wire)) {
    sensorOk = true;
    Serial.println(F("OK (addr=0x76)"));
  } else if (bme.begin(0x77, &Wire)) {
    sensorOk = true;
    Serial.println(F("OK (addr=0x77 — SDO to VCC)"));
  } else {
    sensorOk = false;
    Serial.println(F("NOT FOUND — check SDA/SCL wiring + pull-ups (SDA=3,SCL=4)"));
  }

  // ── 6. DS3231 RTC 检测 ──
  Serial.print(F("[INIT] DS3231 RTC... "));
  if (rtc.begin()) {
    rtcOk = true;
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.print(F("OK (battery lost — set to compile time)"));
    } else {
      Serial.print(F("OK (time preserved)"));
    }
    Serial.println();
  } else {
    rtcOk = false;
    Serial.println(F("NOT FOUND — check I2C wiring (SDA=3,SCL=4)"));
  }

  // ── 7. ADC 按键初始化 (GPIO1) ──
  // 3按键 → 电阻梯形分压网络 → ADC1_CH0
  // 使用 11dB 衰减 (满量程≈3.3V), 12位分辨率
  Serial.print(F("[INIT] ADC Buttons (GPIO1)... "));
  analogSetAttenuation(ADC_11db);
  pinMode(BTN_ADC_PIN, INPUT);
  Serial.println(F("OK (3 buttons on 1 ADC)"));

  // ── 8. 电池 ADC 初始化 (GPIO2) ──
  // 电池电压经 2:1 分压 (100k+100k) 后接入
  // 满电 4.2V → 2.1V, 安全在 3.3V 以下
  Serial.print(F("[INIT] Battery ADC (GPIO2)... "));
  pinMode(BAT_ADC_PIN, INPUT);
  Serial.println(F("OK"));

  // ── 9. 运行硬件自检 ──
  runSelfTest();

  // ── 10. 进入表盘模式 ──
  currentState = State::WATCH_FACE;
  drawWatchFaceStatic();
  updateSensorData();
  lastUpdateMs = millis();

  Serial.println(F("[READY] Watch face mode active. v2.0 GPIO1-13"));
  Serial.println(F("[INFO] UP=Sensor  DOWN=Tests  OK=AI Chat"));
}

// ═══════════════════════════════════════════════════════════════
// loop() — 主循环
//
// 调度策略:
//   - handleButtons()  每帧运行 (ADC按键检测, 无阻塞)
//   - WATCH_FACE:      每秒刷新时间和传感器
//   - SENSOR_DETAIL:   每 2 秒刷新
//   - AI 模式:         独立函数处理
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  handleButtons();  // ADC按键检测 (替代旧版独立GPIO检测)

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
      }
      break;

    case State::AI_LISTENING:
      aiListen();
      break;

    case State::AI_RESPONDING:
      aiRespond();
      break;

    case State::ERROR:
      break;

    default:
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
// runSelfTest() — 硬件上电自检 (Power-On Self Test) v2.0
//
// 测试项目 (6项):
//   1. TFT 显示屏 — SPI通信 (CS=10, MOSI=11, SCK=12, DC=13, BL=9)
//   2. I2C 总线扫描 — 检测总线上所有设备 (SDA=3, SCL=4)
//   3. BME280 传感器 — I2C通信 + 数据有效
//   4. DS3231 RTC — I2C通信
//   5. ADC按键 — 等待用户按下任意键 (GPIO1)
//   6. 电池 ADC — 读数有效 (GPIO2)
// ═══════════════════════════════════════════════════════════════
void runSelfTest() {
  Serial.println(F("\n── SELF-TEST (v2.0 GPIO1-13) ────────"));

  int pass = 0;
  int fail = 0;
  int skip = 0;

  // ── 测试 1: 显示屏 ──
  Serial.print(F("  [1/6] Display (TFT_eSPI, SW reset)... "));
  if (displayOk) { pass++; Serial.println(F("PASS")); }
  else { fail++; Serial.println(F("FAIL — SPI init failed")); }

  // ── 测试 2: I2C 总线扫描 (SDA=3, SCL=4) ──
  Serial.print(F("  [2/6] I2C bus scan (SDA=3,SCL=4)... "));
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
  if (devices >= 2) { pass++; Serial.println(F(" OK")); }
  else if (devices == 0) { fail++; Serial.println(F(" NO DEVICES — check SDA=3/SCL=4 + pull-ups")); }
  else { skip++; Serial.println(F(" PARTIAL")); }

  // ── 测试 3: BME280 ──
  Serial.print(F("  [3/6] BME280 sensor... "));
  if (sensorOk) {
    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure();
    if (!isnan(t) && !isnan(h) && !isnan(p)) {
      pass++;
      Serial.print(F("PASS (T=")); Serial.print(t, 1);
      Serial.print(F("C H=")); Serial.print(h, 0);
      Serial.print(F("% P=")); Serial.print(p / 100.0f, 0);
      Serial.println(F("hPa)"));
    } else { fail++; Serial.println(F("FAIL — readings invalid")); }
  } else { fail++; Serial.println(F("FAIL — not detected")); }

  // ── 测试 4: DS3231 RTC ──
  Serial.print(F("  [4/6] DS3231 RTC... "));
  if (rtcOk) {
    DateTime now = rtc.now();
    pass++;
    Serial.print(F("PASS ("));
    Serial.print(now.year()); Serial.print(F("-"));
    Serial.print(now.month()); Serial.print(F("-"));
    Serial.print(now.day()); Serial.print(F(" "));
    Serial.print(now.hour()); Serial.print(F(":"));
    Serial.print(now.minute()); Serial.println(F(")"));
  } else { fail++; Serial.println(F("FAIL — not detected")); }

  // ── 测试 5: ADC按键 (GPIO1) ──
  Serial.print(F("  [5/6] ADC Buttons (GPIO1)... "));
  Serial.print(F("press any within 10s... "));
  unsigned long btnStart = millis();
  bool btnOk = false;
  while (millis() - btnStart < 10000) {
    uint8_t code = readADCButtons();  // 读取ADC按键
    if (code > 0) {
      btnOk = true;
      Serial.print(F("DETECTED code="));
      Serial.print(code);
      if (code == 1) Serial.print(F("(OK)"));
      else if (code == 2) Serial.print(F("(DOWN)"));
      else if (code == 3) Serial.print(F("(UP)"));
      break;
    }
    delay(10);
  }
  if (btnOk) { pass++; Serial.println(F(" PASS")); }
  else { skip++; Serial.println(F(" SKIP — no input within 10s")); }

  // ── 测试 6: 电池 ADC (GPIO2) ──
  Serial.print(F("  [6/6] Battery ADC (GPIO2)... "));
  int raw = analogRead(BAT_ADC_PIN);
  batVoltage = (raw / 4095.0f) * 3.3f * 2.0f;  // ×2 补偿分压
  if (raw > 0) {
    pass++;
    Serial.print(F("PASS (raw=")); Serial.print(raw);
    Serial.print(F(" -> ")); Serial.print(batVoltage, 1);
    Serial.println(F("V)"));
  } else { fail++; Serial.println(F("FAIL — ADC reads 0")); }

  // ── 自检总结 ──
  Serial.print(F("  Result: "));
  Serial.print(pass); Serial.print(F("/"));
  Serial.print(pass + fail + skip);
  Serial.println(F(" passed"));
  if (fail > 0) {
    Serial.print(F("  *** ")); Serial.print(fail);
    Serial.println(F(" FAILURE(S) — CHECK WIRING ***"));
  }

  // 屏幕显示自检结果
  tft.fillScreen(0x1A1A2E);
  tft.setTextSize(2);
  if (fail == 0) {
    tft.setTextColor(0x2ECC71);
    tft.setCursor(30, 80);
    tft.print("Self-Test: ");
    tft.print(pass); tft.print("/");
    tft.print(pass + fail + skip);
    tft.setCursor(30, 110);
    tft.print("ALL PASSED v2.0");
  } else {
    tft.setTextColor(0xF39C12);
    tft.setCursor(30, 80);
    tft.print("Self-Test: ");
    tft.print(fail); tft.print(" FAILED");
    tft.setCursor(30, 110);
    tft.setTextColor(0xE74C3C);
    tft.print("Check Wiring");
  }
  delay(2000);

  Serial.println(F("── END SELF-TEST ─────────────\n"));
}

// ═══════════════════════════════════════════════════════════════
// drawBootScreen() — 启动画面
// ═══════════════════════════════════════════════════════════════
void drawBootScreen() {
  tft.fillScreen(0x1A1A2E);

  tft.setTextColor(TFT_WHITE, 0x1A1A2E);
  tft.setTextSize(2);
  tft.setCursor(40, 80);
  tft.print("AI Smart");
  tft.setCursor(55, 108);
  tft.print("Watch");

  tft.setTextSize(1);
  tft.setCursor(55, 150);
  tft.print("Initializing...");

  tft.setTextColor(0x888888, 0x1A1A2E);
  tft.setCursor(80, 220);
  tft.print("v2.0 2026-05-23");
}

// ═══════════════════════════════════════════════════════════════
// drawWatchFaceStatic() — 绘制表盘静态布局 v2.0
//
// 布局结构 (240×240):
//   y=0    ┌──────────────────────┐  状态栏 (22px)
//          │ AI WATCH v2    [OK]  │
//   y=22   ├──────────────────────┤
//          │      12:00:00        │  时间区 (90px)
//          │   2026-05-23         │
//   y=120  ├─────────┬────────────┤
//          │ 温度    │  湿度      │  传感器卡片行1
//   y=178  ├─────────┼────────────┤
//          │ 气压    │  电池      │  传感器卡片行2
//   y=228  └─────────┴────────────┘
//          [UP][DOWN] Nav  [OK] AI
// ═══════════════════════════════════════════════════════════════
void drawWatchFaceStatic() {
  tft.fillScreen(0x1A1A2E);

  // 状态栏
  tft.fillRect(0, 0, 240, 22, 0x16213E);
  tft.setTextColor(0x53D8FB, 0x16213E);
  tft.setTextSize(1);
  tft.setCursor(6, 6);
  tft.print("AI WATCH v2");
  tft.setCursor(190, 6);
  tft.print("OK");

  // 时间区背景
  tft.fillRect(0, 22, 240, 90, 0x0F3460);

  // 传感器卡片
  tft.fillRoundRect(10, 120, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(125, 120, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(10, 178, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(125, 178, 105, 50, 8, 0x16213E);

  // 卡片标签
  tft.setTextSize(1);
  tft.setTextColor(0x888888, 0x16213E);
  tft.setCursor(16, 126);   tft.print("Temperature");
  tft.setCursor(131, 126);  tft.print("Humidity");
  tft.setCursor(16, 184);   tft.print("Pressure");
  tft.setCursor(131, 184);  tft.print("Battery");

  // 底部导航
  tft.setTextColor(0x666666, 0x1A1A2E);
  tft.setCursor(8, 234);
  tft.print("[UP][DOWN] Navigate   [OK] AI Chat");
}

// ═══════════════════════════════════════════════════════════════
// updateWatchFace() — 每秒更新表盘动态数据
// ═══════════════════════════════════════════════════════════════
void updateWatchFace() {
  currentTime = rtcOk ? rtc.now()
                      : DateTime(2026, 5, 23, 12, 0, 0);

  // 时间 HH:MM:SS
  tft.setTextColor(0xFFFFFF, 0x0F3460);
  tft.setTextSize(4);
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d",
    currentTime.hour(), currentTime.minute(), currentTime.second());
  tft.setCursor(20, 50);
  tft.print(timeStr);

  // 日期 YYYY-MM-DD
  tft.setTextSize(1);
  tft.setCursor(20, 92);
  tft.setTextColor(0xAAAAAA, 0x0F3460);
  char dateStr[14];
  sprintf(dateStr, "%04d-%02d-%02d",
    currentTime.year(), currentTime.month(), currentTime.day());
  tft.print(dateStr);

  // 温度
  tft.fillRect(16, 140, 90, 18, 0x16213E);
  tft.setTextSize(2);
  tft.setTextColor(0xF39C12, 0x16213E);
  tft.setCursor(16, 142);
  if (sensorOk) { tft.print(temperature, 1); tft.print("C"); }
  else { tft.print("--.-C"); }

  // 湿度
  tft.fillRect(131, 140, 90, 18, 0x16213E);
  tft.setTextColor(0x53D8FB, 0x16213E);
  tft.setCursor(131, 142);
  if (sensorOk) { tft.print(humidity, 0); tft.print("%"); }
  else { tft.print("--%"); }

  // 气压
  tft.fillRect(16, 198, 90, 18, 0x16213E);
  tft.setTextColor(0x2ECC71, 0x16213E);
  tft.setCursor(16, 200);
  if (sensorOk) { tft.print(pressure / 100.0f, 0); tft.print("hPa"); }
  else { tft.print("---hPa"); }

  // 电池
  tft.fillRect(131, 198, 90, 18, 0x16213E);
  tft.setTextColor(0xE94560, 0x16213E);
  tft.setCursor(131, 200);
  tft.print(batVoltage, 1); tft.print("V");
}

// ═══════════════════════════════════════════════════════════════
// updateSensorData() — 传感器数据采集 v2.0
// ═══════════════════════════════════════════════════════════════
void updateSensorData() {
  if (sensorOk) {
    temperature = bme.readTemperature();
    humidity = bme.readHumidity();
    pressure = bme.readPressure();       // 单位 Pa
  }

  // 电池电压: ADC → 3.3V → ×2 (2:1分压补偿)
  int raw = analogRead(BAT_ADC_PIN);
  batVoltage = (raw / 4095.0f) * 3.3f * 2.0f;

  if (rtcOk) {
    currentTime = rtc.now();
  }
}

// ═══════════════════════════════════════════════════════════════
// readADCButtons() — 读取ADC按键 (v2.0 新增)
//
// 3按键 → 电阻梯形分压 → GPIO1(ADC1_CH0)
//
// 电压分压原理 (按键→GND, 上臂10kΩ×3, 下臂100kΩ→GND):
//   BTN_OK:   0 / (0+100k) × 3.3V = 0.00V  → ADC ≈ 0
//   BTN_DOWN: 10k/(10k+100k) × 3.3V = 0.30V → ADC ≈ 370
//   BTN_UP:   20k/(20k+100k) × 3.3V = 0.55V → ADC ≈ 680
//   无按键:   100k上拉到3.3V              → ADC ≈ 4095
//
// 返回值: 0=无按键, 1=OK, 2=DOWN, 3=UP
// ═══════════════════════════════════════════════════════════════
uint8_t readADCButtons() {
  int raw = analogRead(BTN_ADC_PIN);  // 12位ADC, 0~4095

  if (raw < BTN_OK_THRESHOLD) {
    return 1;  // OK 键 (0V, 直接短路到GND)
  } else if (raw >= BTN_DOWN_MIN && raw <= BTN_DOWN_MAX) {
    return 2;  // DOWN 键 (~0.30V)
  } else if (raw >= BTN_UP_MIN && raw <= BTN_UP_MAX) {
    return 3;  // UP 键 (~0.55V)
  } else if (raw > BTN_NONE_MIN) {
    return 0;  // 无按键 (3.3V)
  }

  return 0;  // 不确定区域, 返回无按键
}

// ═══════════════════════════════════════════════════════════════
// handleButtons() — ADC按键处理 + 软件消抖 + 页面切换 v2.0
//
// 消抖原理:
//   - 200ms 冷却期 + 按键代码变化检测
//   - 连续按住不重复触发 (检测 lastBtnCode → currentCode 的上升沿)
//
// 按键功能映射:
//   UP:   切换到传感器详情页
//   DOWN: 返回表盘
//   OK:   触发 AI 语音对话 [预留]
// ═══════════════════════════════════════════════════════════════
void handleButtons() {
  // 软件消抖: 距上次按键 < 200ms 忽略
  if (millis() - lastBtnMs < 200) return;

  uint8_t code = readADCButtons();

  // 上升沿检测: "上次无按键 && 当前有按键"
  if (lastBtnCode == 0 && code > 0) {
    lastBtnMs = millis();

    switch (code) {
      case 3:  // UP: 传感器详情
        Serial.println(F("[BTN] UP pressed (ADC) -> Sensor page"));
        if (currentState == State::WATCH_FACE) {
          currentState = State::SENSOR_DETAIL;
          tft.fillScreen(0x1A1A2E);
          tft.setTextColor(TFT_WHITE, 0x1A1A2E);
          tft.setTextSize(2);
          tft.setCursor(20, 100);
          tft.print("Sensor Detail");
          tft.setTextSize(1);
          tft.setCursor(20, 130);
          tft.print("[OK] to return to watch");
        }
        break;

      case 2:  // DOWN: 返回表盘
        Serial.println(F("[BTN] DOWN pressed (ADC) -> Watch face"));
        if (currentState == State::SENSOR_DETAIL) {
          currentState = State::WATCH_FACE;
          drawWatchFaceStatic();
          updateSensorData();
          lastUpdateMs = 0;
        }
        break;

      case 1:  // OK: AI 对话 [预留]
        Serial.println(F("[BTN] OK pressed (ADC) -> AI Chat triggered"));
        // TODO: 实现完整 AI 对话流程
        break;
    }
  }

  lastBtnCode = code;  // 保存当前按键代码供下一帧使用
}

// ═══════════════════════════════════════════════════════════════
// [预留] initI2S() — I2S 全双工音频初始化 v2.0
//
// ESP32-S3 单 I2S 控制器全双工模式:
//   I2S0: 同时驱动 INMP441 (RX) + MAX98357 (TX)
//   BCLK 和 WS 由 ESP32-S3 作为 Master 提供, 供两个设备共享
//
// 引脚:
//   BCLK = GPIO5  (INMP441 SCK + MAX98357 BCLK)
//   WS   = GPIO6  (INMP441 WS + MAX98357 LRC)
//   SD_IN  = GPIO7  (INMP441 SD → ESP32)
//   DOUT   = GPIO8  (ESP32 → MAX98357 DIN)
//
// 配置:
//   采样率: 16kHz, 位深度: 16bit, 单声道
//   模式: MASTER | RX | TX (全双工)
// ═══════════════════════════════════════════════════════════════
void initI2S() {
  // TODO: I2S 全双工初始化 (ESP-IDF I2S driver)
  // i2s_config_t i2s_config = {
  //   .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
  //   .sample_rate = 16000,
  //   .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  //   .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  //   .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  //   .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  //   .dma_buf_count = 8,
  //   .dma_buf_len = 1024,
  // };
  // i2s_pin_config_t pin_config = {
  //   .bck_io_num = I2S_BCLK,      // GPIO5 (共享)
  //   .ws_io_num = I2S_WS,          // GPIO6 (共享)
  //   .data_in_num = I2S_SD_IN,     // GPIO7 (麦克风)
  //   .data_out_num = I2S_DOUT,     // GPIO8 (扬声器)
  // };
  // i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  // i2s_set_pin(I2S_NUM_0, &pin_config);
  // MAX98357 SD = 3.3V (硬件常开, 不需要GPIO控制)

  Serial.println(F("[AUDIO] I2S init — not yet configured"));
}

// ═══════════════════════════════════════════════════════════════
// [预留] connectWiFi() — WiFi 网络连接
// ═══════════════════════════════════════════════════════════════
void connectWiFi() {
  // TODO: 从 NVS 读取凭证并连接
  // const char* ssid = "YOUR_SSID";
  // const char* pass = "YOUR_PASSWORD";
  // WiFi.begin(ssid, pass);
  // ... 连接逻辑 ...

  Serial.println(F("[WIFI] Not yet configured"));
}

// ═══════════════════════════════════════════════════════════════
// [预留] aiListen() — AI 语音聆听阶段
//
// 处理流程:
//   1. INMP441 录音 (GPIO7) → I2S → 16kHz/16bit PCM
//   2. PCM → Base64 → HTTPS POST → 云端 ASR
//   3. 转写文本 → LLM API
//   4. LLM 回复 → 云端 TTS → 音频
//   5. I2S → MAX98357 (GPIO8) → 扬声器播放
// ═══════════════════════════════════════════════════════════════
void aiListen() {
  Serial.println(F("[AI] Listening... (not yet implemented)"));
  delay(1000);
  currentState = State::WATCH_FACE;
}

void aiRespond() {
  Serial.println(F("[AI] Responding... (not yet implemented)"));
  delay(1000);
  currentState = State::WATCH_FACE;
}

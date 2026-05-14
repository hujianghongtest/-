/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  AI Smart Watch — 主固件 (ESP32-S3-Zero)                    ║
 * ║  硬件验证 + 应用程序骨架 + AI 对话预留接口                    ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 【运行平台】ESP32-S3-Zero (Waveshare)
 * 【开发框架】Arduino (PlatformIO)
 * 【编译环境】platformio.ini 中配置的 espressif32 平台
 *
 * 【核心功能模块】
 *   1. 硬件初始化 + 上电自检 (runSelfTest)
 *   2. TFT 显示屏驱动 (TFT_eSPI 库, ST7789/GC9A01)
 *   3. 环境传感器采集 (BME280, I2C 0x76)
 *   4. 精确时钟保持 (DS3231 RTC, I2C 0x68)
 *   5. 表盘 UI 渲染 (时间 + 温度 + 湿度 + 气压 + 电量)
 *   6. 按键导航 (UP/DOWN/OK 三个实体按键)
 *   7. 电池电压监测 (ADC1 分压采样)
 *   8. [预留] I2S 音频输入输出 (INMP441 麦克风 + MAX98357 功放)
 *   9. [预留] WiFi + 云端 AI 对话接口
 *
 * 【引脚分配表 (20个GPIO)】
 *   ┌────────┬────────┬─────────────────────┐
 *   │ GPIO   │ 功能   │ 连接                 │
 *   ├────────┼────────┼─────────────────────┤
 *   │   1    │ BTN_UP │ 按键上 (内部上拉)    │
 *   │   2    │ BAT_ADC│ 电池分压检测 (ADC1)  │
 *   │   3    │ BTN_DWN│ 按键下 (内部上拉)    │
 *   │   4    │ SPK_SD │ 功放关断 (HIGH=开)   │
 *   │   5    │ BTN_OK │ 按键确认 (内部上拉)  │
 *   │   6    │ SPK_DIN│ I2S 功放数据输入     │
 *   │   7    │ MIC_SD │ I2S 麦克风数据输出   │
 *   │   8    │ SPK_BCK│ I2S 功放位时钟       │
 *   │   9    │ SPK_LRC│ I2S 功放左右通道     │
 *   │  10    │ TFT_CS │ SPI 片选             │
 *   │  11    │ TFT_MOSI│ SPI 主机输出        │
 *   │  12    │ TFT_SCK│ SPI 时钟             │
 *   │  13    │ TFT_DC │ SPI 数据/命令选择    │
 *   │  14    │ TFT_RST│ SPI 复位             │
 *   │  15    │ MIC_SCK│ I2S 麦克风位时钟     │
 *   │  16    │ MIC_WS │ I2S 麦克风通道选择   │
 *   │  17    │ I2C_SDA│ I2C 数据线           │
 *   │  18    │ I2C_SCL│ I2C 时钟线           │
 *   │  21    │ WS2812 │ 板载 RGB LED         │
 *   │  47    │ TFT_BL │ TFT 背光 PWM         │
 *   └────────┴────────┴─────────────────────┘
 *
 * 【I2C 设备地址】
 *   BME280: 0x76 (SDO 接 GND) 或 0x77 (SDO 接 VCC)
 *   DS3231: 0x68 (固定)
 *
 * 【版本历史】
 *   v1.0  2026-05-13  初始创建，硬件自检 + 表盘UI + AI接口占位
 */

#include <Arduino.h>
#include <Wire.h>       // I2C 总线驱动 — BME280 + DS3231 共用
#include <SPI.h>        // SPI 总线驱动 — TFT 显示屏

// ═══════════════════════════════════════════════════════════════
// 外设驱动库
// ═══════════════════════════════════════════════════════════════

// TFT_eSPI — 高性能 TFT 驱动库
// 配置方式: platformio.ini 的 build_flags 中定义引脚 (不单独用 User_Setup.h)
// 支持 ST7789 / GC9A01 / ILI9341 等多种驱动芯片
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();              // 主显示对象
TFT_eSprite spr = TFT_eSprite(&tft);     // 精灵 (离屏缓冲)，减少闪烁

// BME280 — 温度 + 湿度 + 气压 三合一环境传感器
// 精度: ±0.5°C / ±3%RH / ±1hPa
// 接口: I2C (最高 3.4MHz)
#include <Adafruit_BME280.h>
Adafruit_BME280 bme;                     // 传感器对象

// DS3231 — 高精度实时时钟模块
// 精度: ±2ppm (±1分钟/年)
// 特性: 自带温度补偿晶振 (TCXO)、电池备份 (CR2032)
#include <RTClib.h>
RTC_DS3231 rtc;                          // RTC 对象

// WiFi — ESP32-S3 内置 2.4GHz WiFi
// 用于连接云端 AI API (OpenAI / 文心一言 / 通义千问 等)
#include <WiFi.h>

// ArduinoJson — JSON 解析/生成库
// AI API 请求和响应均为 JSON 格式
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════
// 引脚宏定义 — 必须与 Hardware_Design.md 中的分配表完全一致
// ═══════════════════════════════════════════════════════════════

// --- TFT 显示屏 (SPI2) ---
// TFT_eSPI 库通过 platformio.ini 中的 build_flags 读取这些引脚
// 此处定义用于 backlight 手动控制和自检输出
#define TFT_BL_PIN      47   // 背光控制 (GPIO47 → LEDC PWM 通道)

// --- I2C 总线 (BME280 + DS3231 共用) ---
// 两条线需外接 4.7kΩ~10kΩ 上拉电阻到 3.3V
// 多数 BME280/DS3231 模块已自带 I2C 上拉电阻
#define I2C_SDA         17   // I2C 串行数据线 (Serial Data)
#define I2C_SCL         18   // I2C 串行时钟线 (Serial Clock)

// --- I2S 数字麦克风 (INMP441) ---
// INMP441 特性: 全向、-26dBFS 灵敏度、最高 24bit/48kHz
// I2S 格式: 标准 Philips I2S, 64x BCLK/WS
#define I2S_MIC_SCK     15   // I2S 位时钟 (Bit Clock, 64×采样率)
#define I2S_MIC_WS      16   // I2S 字选择 (Word Select, = 采样率)
#define I2S_MIC_SD      7    // I2S 串行数据 (Serial Data, 来自麦克风)

// --- I2S 音频功放 (MAX98357) ---
// MAX98357 特性: D类 3W@4Ω、I2S 输入、自动时钟检测
// 增益可配置 (GAIN 引脚电平决定 3dB/6dB/9dB/12dB)
#define I2S_SPK_BCLK    8    // I2S 位时钟 (与麦克风 SCK 共用可节省引脚)
#define I2S_SPK_LRC     9    // I2S 左右通道 (LRCK, = 采样率)
#define I2S_SPK_DIN     6    // I2S 串行数据 (流向功放)
#define I2S_SPK_SD      4    // 关断控制 (Shutdown, LOW=静音 HIGH=工作)

// --- 物理按键 ---
// 连接: GPIO ←→ 按键 ←→ GND (配合 INPUT_PULLUP)
#define BTN_UP          1    // 上翻 / 音量加
#define BTN_DOWN        3    // 下翻 / 音量减
#define BTN_OK          5    // 确认 / 唤醒 / 语音输入触发

// --- 电池电压检测 ---
// 锂电池电压范围: 3.0V (耗尽) ~ 4.2V (满电)
// ESP32 ADC 输入范围: 0 ~ 3.3V (11dB 衰减时)
// 因此需要 2:1 电阻分压: 4.2V → 2.1V (安全范围)
// 分压电阻: R1=R2=100kΩ (可选更高阻值降低功耗)
#define BAT_ADC_PIN     2    // ADC1_CH1 — 12位分辨率 (0~4095)
#define BAT_ADC_CH      ADC1_GPIO2_CHANNEL  // ADC 通道号

// --- 板载 RGB LED (WS2812) ---
// ESP32-S3-Zero 板载一个 WS2812 可寻址 RGB LED
// 可用于状态指示 (蓝=正常, 绿=充电满, 红=故障, 紫=AI对话中)
#define WS2812_PIN      21

// ═══════════════════════════════════════════════════════════════
// 系统状态枚举
// ═══════════════════════════════════════════════════════════════

// 系统运行状态 — 决定主循环的行为
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

State currentState = State::BOOT;   // 当前系统状态
unsigned long lastUpdateMs = 0;     // 上次 UI 刷新时间戳
unsigned long lastSensorMs = 0;     // 上次传感器读取时间戳

// ═══════════════════════════════════════════════════════════════
// 全局数据缓存 (在主循环和各页面间共享)
// ═══════════════════════════════════════════════════════════════

// 传感器数据 (每 2 秒更新一次)
float temperature = 0.0f;    // 温度 (℃)
float humidity = 0.0f;       // 相对湿度 (%RH)
float pressure = 0.0f;       // 气压 (hPa)
float batVoltage = 0.0f;     // 电池电压 (V)

// 时间数据 (由 DS3231 RTC 提供)
DateTime currentTime;

// 外设状态标志 (自检结果)
bool displayOk = false;      // TFT 显示屏初始化成功
bool sensorOk = false;       // BME280 检测到
bool rtcOk = false;          // DS3231 检测到
bool wifiOk = false;         // WiFi 连接成功

// ═══════════════════════════════════════════════════════════════
// 函数前置声明
// ═══════════════════════════════════════════════════════════════
void runSelfTest();          // 硬件自检 — 6 项测试
void updateWatchFace();      // 表盘刷新 — 每秒调用
void drawWatchFaceStatic();  // 表盘静态布局 — 页面切换时调用
void updateSensorData();     // 传感器数据采集
void checkButtons();         // 按键扫描 + 消抖 + 页面切换
void drawBootScreen();       // 启动画面
void initI2S();              // [预留] I2S 音频初始化
void connectWiFi();          // [预留] WiFi 连接
void aiListen();             // [预留] 语音采集 → ASR → LLM
void aiRespond();            // [预留] LLM 回复 → TTS → 播放

// ═══════════════════════════════════════════════════════════════
// setup() — 系统初始化入口 (上电后执行一次)
//
// 初始化顺序:
//   1. 串口 (日志输出)
//   2. 背光 (先点亮)
//   3. 显示屏 (TFT_eSPI 初始化 + 启动画面)
//   4. I2C 总线
//   5. BME280 传感器
//   6. DS3231 RTC
//   7. 按键引脚
//   8. 电池 ADC
//   9. 硬件自检
//  10. 进入表盘模式
// ═══════════════════════════════════════════════════════════════
void setup() {
  // ── 1. 串口初始化 ──
  Serial.begin(115200);
  delay(500);  // 等待 USB CDC 稳定
  Serial.println(F("\n╔══════════════════════════════════╗"));
  Serial.println(F("║   AI Smart Watch — Booting...    ║"));
  Serial.println(F("╚══════════════════════════════════╝"));

  // ── 2. 背光先点亮 (便于观察后续初始化过程) ──
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);  // 全亮 (100% PWM 占空比)

  // ── 3. 显示屏初始化 ──
  // TFT_eSPI 库: begin() 内部执行:
  //   1. SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI)
  //   2. 发送 ST7789/GC9A01 初始化序列
  //   3. 设置显示方向和颜色格式
  Serial.print(F("[INIT] Display... "));
  tft.begin();                      // TFT_eSPI 初始化
  tft.setRotation(0);               // 0=竖屏 (根据实际安装方向调整 0~3)
  tft.fillScreen(TFT_BLACK);        // 黑屏等待
  displayOk = true;
  Serial.println(F("OK"));

  // 绘制启动画面
  drawBootScreen();

  // ── 4. I2C 总线初始化 ──
  // 标准模式 100kHz (BME280 和 DS3231 均支持 100kHz 标准模式)
  Serial.print(F("[INIT] I2C bus (SDA:17, SCL:18)... "));
  Wire.begin(I2C_SDA, I2C_SCL);     // 初始化 I2C 主机
  Wire.setClock(100000);            // 100kHz 标准模式
  Serial.println(F("OK"));

  // ── 5. BME280 传感器检测 ──
  // 注意: BME280 上电后需要约 2ms 才能响应 I2C 请求
  Serial.print(F("[INIT] BME280 sensor... "));
  delay(10);  // 等待传感器上电稳定
  if (bme.begin(0x76, &Wire)) {     // 尝试主地址 0x76
    sensorOk = true;
    Serial.println(F("OK (addr=0x76)"));
  } else if (bme.begin(0x77, &Wire)) {
    // 如果 SDO 引脚接 VCC，地址变为 0x77
    sensorOk = true;
    Serial.println(F("OK (addr=0x77 — SDO to VCC)"));
  } else {
    sensorOk = false;
    Serial.println(F("NOT FOUND — check SDA/SCL wiring + pull-up resistors"));
  }

  // ── 6. DS3231 RTC 检测 ──
  Serial.print(F("[INIT] DS3231 RTC... "));
  if (rtc.begin()) {
    rtcOk = true;
    // 检查 RTC 是否掉电 (电池耗尽或首次使用)
    if (rtc.lostPower()) {
      // 使用编译时间作为初始时间 (后续可通过 WiFi NTP 校准)
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.print(F("OK (battery lost power — set to compile time)"));
    } else {
      Serial.print(F("OK (time preserved)"));
    }
    Serial.println();
  } else {
    rtcOk = false;
    Serial.println(F("NOT FOUND — check I2C wiring"));
  }

  // ── 7. 按键引脚配置 ──
  // INPUT_PULLUP: 使能 ESP32-S3 内部 ~45kΩ 上拉电阻
  // 按键按下 = 对地短路 = LOW
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  // ── 8. 电池 ADC 初始化 ──
  // ADC 衰减: 11dB → 满量程约 3.3V (实测略低，需校准)
  // 12位分辨率: 0~4095
  analogSetAttenuation(ADC_11db);    // 设置全局 ADC 衰减
  pinMode(BAT_ADC_PIN, INPUT);       // ADC 输入模式

  // ── 9. 运行硬件自检 ──
  runSelfTest();

  // ── 10. 进入表盘模式 ──
  currentState = State::WATCH_FACE;
  drawWatchFaceStatic();             // 绘制表盘背景+卡片
  updateSensorData();                // 读取一次传感器
  lastUpdateMs = millis();

  Serial.println(F("[READY] Watch face mode active."));
  Serial.println(F("[INFO] UP=Sensor  DOWN=Tests  OK=AI Chat"));
}

// ═══════════════════════════════════════════════════════════════
// loop() — 主循环 (Arduino 框架自动反复调用)
//
// 调度策略:
//   - checkButtons()  每帧运行 (无阻塞)
//   - WATCH_FACE:     每秒刷新时间和传感器
//   - SENSOR_DETAIL:  每 2 秒刷新
//   - AI 模式:        独立函数处理 (不阻塞 UI 更新)
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // 按键检测 — 始终运行，确保交互响应及时
  checkButtons();

  // 状态机调度
  switch (currentState) {

    case State::WATCH_FACE:
      // 表盘模式: 每秒更新一次 (时间变化 + 传感器数据)
      if (now - lastUpdateMs >= 1000) {
        lastUpdateMs = now;
        updateSensorData();           // 先读传感器
        updateWatchFace();             // 再刷新 UI
      }
      break;

    case State::SENSOR_DETAIL:
      // 传感器详情: 每 2 秒更新 (数据变化慢)
      if (now - lastUpdateMs >= 2000) {
        lastUpdateMs = now;
        updateSensorData();
        // updateSensorDetailPage();  // TODO: 实现详情页
      }
      break;

    case State::AI_LISTENING:
      aiListen();                      // [预留] 语音采集
      break;

    case State::AI_RESPONDING:
      aiRespond();                     // [预留] 语音回复
      break;

    case State::ERROR:
      // 错误状态: 闪烁 LED 等待用户复位
      break;

    default:
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
// runSelfTest() — 硬件上电自检程序 (Power-On Self Test)
//
// 测试项目 (6项):
//   1. TFT 显示屏 — SPI 通信正常性
//   2. I2C 总线扫描 — 检测总线上所有设备
//   3. BME280 传感器 — I2C 通信 + 数据有效
//   4. DS3231 RTC — I2C 通信
//   5. 按键电路 — 等待用户按下任意键
//   6. 电池 ADC — 读数有效 (>0)
//
// 自检结果通过串口输出，并在屏幕上显示 PASS/FAIL
// 实物首次上电或维修后应运行此程序排查硬件故障
// ═══════════════════════════════════════════════════════════════
void runSelfTest() {
  Serial.println(F("\n── SELF-TEST ──────────────────"));

  int pass = 0;     // 通过计数
  int fail = 0;     // 失败计数
  int skip = 0;     // 跳过计数

  // ── 测试 1: 显示屏 ──
  Serial.print(F("  [1/6] Display (TFT_eSPI)... "));
  if (displayOk) { pass++; Serial.println(F("PASS")); }
  else { fail++; Serial.println(F("FAIL — SPI init failed")); }

  // ── 测试 2: I2C 总线扫描 ──
  // 扫描地址范围 1~126，列出所有 ACK 响应的设备
  Serial.print(F("  [2/6] I2C bus scan... "));
  int devices = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {      // ACK = 设备存在
      Serial.print(F("0x"));
      Serial.print(addr, HEX);
      Serial.print(F(" "));
      devices++;
    }
  }
  if (devices >= 2) {                       // 预期至少 2 个设备 (BME280 + DS3231)
    pass++;
    Serial.println(F(" OK"));
  } else if (devices == 0) {
    fail++;
    Serial.println(F(" NO DEVICES — check SDA/SCL + pull-ups"));
  } else {
    skip++;
    Serial.println(F(" PARTIAL — some devices missing"));
  }

  // ── 测试 3: BME280 ──
  Serial.print(F("  [3/6] BME280 sensor... "));
  if (sensorOk) {
    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure();
    if (!isnan(t) && !isnan(h) && !isnan(p)) {
      pass++;
      Serial.print(F("PASS (T="));
      Serial.print(t, 1);
      Serial.print(F("C H="));
      Serial.print(h, 0);
      Serial.print(F("% P="));
      Serial.print(p / 100.0, 0);
      Serial.println(F("hPa)"));
    } else {
      fail++;
      Serial.println(F("FAIL — readings invalid"));
    }
  } else {
    fail++;
    Serial.println(F("FAIL — not detected"));
  }

  // ── 测试 4: DS3231 RTC ──
  Serial.print(F("  [4/6] DS3231 RTC... "));
  if (rtcOk) {
    DateTime now = rtc.now();
    pass++;
    Serial.print(F("PASS ("));
    Serial.print(now.year());
    Serial.print(F("-"));
    Serial.print(now.month());
    Serial.print(F("-"));
    Serial.print(now.day());
    Serial.print(F(" "));
    Serial.print(now.hour());
    Serial.print(F(":"));
    Serial.print(now.minute());
    Serial.println(F(")"));
  } else {
    fail++;
    Serial.println(F("FAIL — not detected"));
  }

  // ── 测试 5: 按键 ──
  // 交互式测试: 提示用户按下任意键，超时 10 秒
  Serial.print(F("  [5/6] Buttons (press any within 10s)... "));
  unsigned long btnStart = millis();
  bool btnOk = false;
  while (millis() - btnStart < 10000) {
    if (!digitalRead(BTN_UP) || !digitalRead(BTN_DOWN) || !digitalRead(BTN_OK)) {
      btnOk = true;
      Serial.print(F("DETECTED"));
      if (!digitalRead(BTN_UP)) Serial.print(F(" UP"));
      if (!digitalRead(BTN_DOWN)) Serial.print(F(" DOWN"));
      if (!digitalRead(BTN_OK)) Serial.print(F(" OK"));
      break;
    }
    delay(10);
  }
  if (btnOk) { pass++; Serial.println(F(" PASS")); }
  else { skip++; Serial.println(F(" SKIP — no input within 10s")); }

  // ── 测试 6: 电池 ADC ──
  Serial.print(F("  [6/6] Battery ADC (GPIO2)... "));
  int raw = analogRead(BAT_ADC_PIN);
  // 电压计算: ADC → 电压 → ×2 (分压比)
  // raw/4095 * 3.3V * 2 (1/2 分压)
  batVoltage = (raw / 4095.0f) * 3.3f * 2.0f;
  if (raw > 0) {
    pass++;
    Serial.print(F("PASS (raw="));
    Serial.print(raw);
    Serial.print(F(" → "));
    Serial.print(batVoltage, 1);
    Serial.println(F("V)"));
  } else {
    fail++;
    Serial.println(F("FAIL — ADC reads 0, check divider"));
  }

  // ── 自检总结 ──
  Serial.print(F("  Result: "));
  Serial.print(pass);
  Serial.print(F("/"));
  Serial.print(pass + fail + skip);
  Serial.println(F(" passed"));
  if (fail > 0) {
    Serial.print(F("  *** "));
    Serial.print(fail);
    Serial.println(F(" FAILURE(S) — CHECK WIRING ***"));
  }

  // ── 屏幕显示自检结果 ──
  tft.fillScreen(0x1A1A2E);          // 深蓝灰背景
  tft.setTextSize(2);
  if (fail == 0) {
    tft.setTextColor(0x2ECC71);       // 绿色 = 全部通过
    tft.setCursor(30, 80);
    tft.print("Self-Test: ");
    tft.print(pass);
    tft.print("/");
    tft.print(pass + fail + skip);
    tft.setCursor(30, 110);
    tft.print("ALL PASSED");
  } else {
    tft.setTextColor(0xF39C12);       // 黄色 = 有警告
    tft.setCursor(30, 80);
    tft.print("Self-Test: ");
    tft.print(fail);
    tft.print(" FAILED");
    tft.setCursor(30, 110);
    tft.setTextColor(0xE74C3C);       // 红色
    tft.print("Check Wiring");
  }
  delay(2000);  // 停留 2 秒供查看

  Serial.println(F("── END SELF-TEST ─────────────\n"));
}

// ═══════════════════════════════════════════════════════════════
// drawBootScreen() — 启动画面 (Logo + 初始化进度)
//
// 在 setup() 中调用，给用户看到设备正在启动
// 实物可替换为品牌 Logo 动画
// ═══════════════════════════════════════════════════════════════
void drawBootScreen() {
  tft.fillScreen(0x1A1A2E);

  // 产品名称
  tft.setTextColor(TFT_WHITE, 0x1A1A2E);
  tft.setTextSize(2);
  tft.setCursor(40, 80);
  tft.print("AI Smart");
  tft.setCursor(55, 108);
  tft.print("Watch");

  // 初始化提示
  tft.setTextSize(1);
  tft.setCursor(55, 150);
  tft.print("Initializing...");

  // 底部版本号
  tft.setTextColor(0x888888, 0x1A1A2E);
  tft.setCursor(90, 220);
  tft.print("v1.0 2026-05-13");
}

// ═══════════════════════════════════════════════════════════════
// drawWatchFaceStatic() — 绘制表盘静态布局元素
//
// 布局结构 (240×240 ST7789):
//   y=0    ┌──────────────────────┐  状态栏 (22px)
//          │ AI WATCH        [v1] │
//   y=22   ├──────────────────────┤
//          │      12:00:00        │  时间区 (90px, 深蓝底)
//          │   2026-05-13         │
//   y=120  ├─────────┬────────────┤
//          │ 温度    │  湿度      │  传感器卡片行1 (50px)
//          │ 25.0°C  │  55%       │
//   y=178  ├─────────┼────────────┤
//          │ 气压    │  电池      │  传感器卡片行2 (50px)
//          │ 1013hPa │  3.9V      │
//   y=228  └─────────┴────────────┘
//          [UP][DOWN] Nav  [OK] AI  导航提示
//
// 仅在页面首次切换时调用 (drawWatchFaceStatic)，
// 后续数据更新使用 updateWatchFace() 局部刷新。
// ═══════════════════════════════════════════════════════════════
void drawWatchFaceStatic() {
  tft.fillScreen(0x1A1A2E);            // 深蓝灰全屏背景

  // --- 顶部状态栏 ---
  tft.fillRect(0, 0, 240, 22, 0x16213E);      // 状态栏背景
  tft.setTextColor(0x53D8FB, 0x16213E);        // 天蓝字
  tft.setTextSize(1);
  tft.setCursor(6, 6);
  tft.print("AI WATCH");
  tft.setCursor(190, 6);
  tft.print("v1");                             // 固件版本标记

  // --- 时间显示区 ---
  tft.fillRect(0, 22, 240, 90, 0x0F3460);     // 深蓝背景突出时间

  // --- 传感器卡片背景 (2行 × 2列 网格) ---
  // 行1: 温度 (左) + 湿度 (右)
  tft.fillRoundRect(10, 120, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(125, 120, 105, 50, 8, 0x16213E);
  // 行2: 气压 (左) + 电池 (右)
  tft.fillRoundRect(10, 178, 105, 50, 8, 0x16213E);
  tft.fillRoundRect(125, 178, 105, 50, 8, 0x16213E);

  // --- 卡片标签 (灰色小字) ---
  tft.setTextSize(1);
  tft.setTextColor(0x888888, 0x16213E);
  tft.setCursor(16, 126);   tft.print("Temperature");
  tft.setCursor(131, 126);  tft.print("Humidity");
  tft.setCursor(16, 184);   tft.print("Pressure");
  tft.setCursor(131, 184);  tft.print("Battery");

  // --- 底部导航提示 ---
  tft.setTextColor(0x666666, 0x1A1A2E);
  tft.setCursor(8, 234);
  tft.print("[UP][DOWN] Navigate   [OK] AI Chat");
}

// ═══════════════════════════════════════════════════════════════
// updateWatchFace() — 每秒更新表盘动态数据 (局部刷新)
//
// 与 drawWatchFaceStatic() 的区别:
//   本函数仅更新变化的区域 (时间数字 + 传感器数值)，
//   不清除静态元素 (卡片背景、标签、状态栏等)，
//   避免全屏刷新带来的闪烁和性能浪费。
//
// 对应实物 LVGL 实现:
//   每个数据项绑定一个 lv_label，通过 lv_label_set_text() 更新
// ═══════════════════════════════════════════════════════════════
void updateWatchFace() {
  // 获取当前时间
  currentTime = rtcOk ? rtc.now()
                      : DateTime(2026, 5, 13, 12, 0, 0);  // RTC 不可用时占位

  // ── 更新时:分:秒 (大号显示) ──
  // 先用区域背景色覆盖旧数字，避免残影
  tft.setTextColor(0xFFFFFF, 0x0F3460);   // 白字 + 深蓝背景
  tft.setTextSize(4);                      // 4倍字号
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d",
    currentTime.hour(), currentTime.minute(), currentTime.second());
  // 居中显示 (240px 宽, 4倍字约 24px/字符, 8字符≈192px)
  tft.setCursor(20, 50);
  tft.print(timeStr);

  // ── 更新日期 ──
  tft.setTextSize(1);
  tft.setCursor(20, 92);
  tft.setTextColor(0xAAAAAA, 0x0F3460);   // 灰色字
  char dateStr[14];
  sprintf(dateStr, "%04d-%02d-%02d",
    currentTime.year(), currentTime.month(), currentTime.day());
  tft.print(dateStr);

  // ── 更新温度数值 ──
  // 区域清除: (16,142) 到 (16+90, 142+16)
  tft.fillRect(16, 140, 90, 18, 0x16213E);
  tft.setTextSize(2);
  tft.setTextColor(0xF39C12, 0x16213E);   // 琥珀色 = 温度
  tft.setCursor(16, 142);
  if (sensorOk) {
    tft.print(temperature, 1);             // 保留1位小数
    tft.print("C");
  } else {
    tft.print("--.-C");
  }

  // ── 更新湿度数值 ──
  tft.fillRect(131, 140, 90, 18, 0x16213E);
  tft.setTextColor(0x53D8FB, 0x16213E);   // 天蓝色 = 湿度
  tft.setCursor(131, 142);
  if (sensorOk) {
    tft.print(humidity, 0);                // 湿度取整
    tft.print("%");
  } else {
    tft.print("--%");
  }

  // ── 更新气压数值 ──
  tft.fillRect(16, 198, 90, 18, 0x16213E);
  tft.setTextColor(0x2ECC71, 0x16213E);   // 绿色 = 气压
  tft.setCursor(16, 200);
  if (sensorOk) {
    tft.print(pressure / 100.0f, 0);       // Pa → hPa
    tft.print("hPa");
  } else {
    tft.print("---hPa");
  }

  // ── 更新电池电压 ──
  tft.fillRect(131, 198, 90, 18, 0x16213E);
  tft.setTextColor(0xE94560, 0x16213E);   // 珊瑚红 = 电池
  tft.setCursor(131, 200);
  tft.print(batVoltage, 1);                // 保留1位小数
  tft.print("V");
}

// ═══════════════════════════════════════════════════════════════
// updateSensorData() — 从传感器和 ADC 采集最新数据
//
// 采集频率: 每秒 1 次 (在 updateWatchFace 中被调用)
// 采集项目:
//   1. BME280 — 温度/湿度/气压 (I2C 读取, 约 2ms)
//   2. 电池电压 — ADC1_CH1 采样 (约 0.1ms)
//   3. DS3231 — 当前时间 (I2C 读取, 约 1ms)
//
// BME280 采样配置 (默认):
//   - 温度过采样 ×1
//   - 湿度过采样 ×1
//   - 气压过采样 ×1
//   - 休眠模式 (每次读取后自动进入休眠省电)
// ═══════════════════════════════════════════════════════════════
void updateSensorData() {
  // --- BME280 环境数据 ---
  if (sensorOk) {
    temperature = bme.readTemperature();   // 返回 float, 单位 ℃
    humidity = bme.readHumidity();         // 返回 float, 单位 %RH
    pressure = bme.readPressure();         // 返回 float, 单位 Pa
    // 如需体感温度: float hi = bme.readTemperature() + 0.05*(humidity-50);
  }

  // --- 电池电压 (2:1 分压后 ADC 采样) ---
  // 12位 ADC: 返回值 0~4095 对应 0~3.3V (11dB 衰减)
  // 分压比 2:1 → 实际电压 = ADC电压 × 2
  int raw = analogRead(BAT_ADC_PIN);
  batVoltage = (raw / 4095.0f) * 3.3f * 2.0f;
  // 校准: 实物可能需要根据分压电阻实际值微调系数

  // --- RTC 时间 ---
  if (rtcOk) {
    currentTime = rtc.now();
  }
}

// ═══════════════════════════════════════════════════════════════
// checkButtons() — 按键扫描 + 软件消抖 + 页面切换逻辑
//
// 【消抖原理】
//   机械按键在触发的 5-20ms 内会产生多次通断 (抖动)
//   通过 200ms 冷却期 + 下降沿检测 双重机制消除影响
//
// 【下降沿检测】
//   "上一帧 HIGH && 当前帧 LOW" = 按键恰好被按下的那一刻
//   连续按住不会重复触发 (因为 lastXxx 一直是 LOW)
//
// 【按键功能映射】
//   UP:   切换到传感器详情页
//   DOWN: 返回表盘 / 切换视图
//   OK:   触发 AI 语音对话 [预留]
// ═══════════════════════════════════════════════════════════════
void checkButtons() {
  // 静态变量保留上一帧的电平值
  static bool lastUp = HIGH;
  static bool lastDown = HIGH;
  static bool lastOk = HIGH;
  static unsigned long lastBtnMs = 0;

  // 软件消抖: 距上次按下 < 200ms 忽略
  if (millis() - lastBtnMs < 200) return;

  // 读取当前电平 (HIGH=未按下 LOW=按下)
  bool up = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);
  bool ok = digitalRead(BTN_OK);

  // ── UP 键: 传感器详情 ──
  if (lastUp == HIGH && up == LOW) {       // 下降沿
    lastBtnMs = millis();
    Serial.println(F("[BTN] UP pressed"));
    if (currentState == State::WATCH_FACE) {
      currentState = State::SENSOR_DETAIL;
      // TODO: 绘制传感器详情页面
      tft.fillScreen(0x1A1A2E);
      tft.setTextColor(TFT_WHITE, 0x1A1A2E);
      tft.setTextSize(2);
      tft.setCursor(20, 100);
      tft.print("Sensor Detail");
      tft.setTextSize(1);
      tft.setCursor(20, 130);
      tft.print("[OK] to return to watch");
    }
  }

  // ── DOWN 键: 返回表盘 ──
  if (lastDown == HIGH && down == LOW) {
    lastBtnMs = millis();
    Serial.println(F("[BTN] DOWN pressed"));
    if (currentState == State::SENSOR_DETAIL) {
      currentState = State::WATCH_FACE;
      drawWatchFaceStatic();               // 重绘静态布局
      updateSensorData();                  // 立即更新数据
      lastUpdateMs = 0;                    // 触发立即刷新
    }
  }

  // ── OK 键: AI 语音对话 [预留] ──
  if (lastOk == HIGH && ok == LOW) {
    lastBtnMs = millis();
    Serial.println(F("[BTN] OK pressed — AI Chat triggered"));
    // TODO: 实现 AI 对话流程
    // 1. 停止当前页面刷新
    // 2. 显示 "聆听中..." 界面
    // 3. 启动 I2S 录音
    // 4. 录音数据 → 云端 ASR → LLM → TTS
    // 5. 显示回复文字 + 播放语音
    // 6. 返回表盘
    // currentState = State::AI_LISTENING;
    // aiListen();
  }

  // 保存当前电平 → 下一帧的 lastXxx
  lastUp = up;
  lastDown = down;
  lastOk = ok;
}

// ═══════════════════════════════════════════════════════════════
// [预留] initI2S() — I2S 音频子系统初始化
//
// ESP32-S3 有两路 I2S 控制器:
//   I2S0: 用于 INMP441 麦克风 (输入)
//   I2S1: 用于 MAX98357 功放 (输出)
//
// 配置参数:
//   采样率: 16kHz (语音通信足够，节省带宽)
//   位深度: 16bit
//   通道数: 1 (单声道)
//   格式:   Standard Philips I2S
//
// 依赖: ESP-ADF (Audio Development Framework) 或 ESP-IDF I2S driver
// ═══════════════════════════════════════════════════════════════
void initI2S() {
  // TODO: 初始化 I2S0 为麦克风输入
  // i2s_config_t i2s_mic_config = {
  //   .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
  //   .sample_rate = 16000,
  //   .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  //   .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  //   .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  //   .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  //   .dma_buf_count = 8,
  //   .dma_buf_len = 1024,
  // };
  // i2s_pin_config_t mic_pins = {
  //   .bck_io_num = I2S_MIC_SCK,
  //   .ws_io_num = I2S_MIC_WS,
  //   .data_in_num = I2S_MIC_SD,
  //   .data_out_num = I2S_PIN_NO_CHANGE,
  // };

  // TODO: 初始化 I2S1 为功放输出
  // 配置类似，mode = I2S_MODE_MASTER | I2S_MODE_TX

  // TODO: 使能 MAX98357 (拉高 SD 引脚)
  // pinMode(I2S_SPK_SD, OUTPUT);
  // digitalWrite(I2S_SPK_SD, HIGH);

  Serial.println(F("[AUDIO] I2S init — not yet configured"));
}

// ═══════════════════════════════════════════════════════════════
// [预留] connectWiFi() — WiFi 网络连接
//
// 凭证管理方案:
//   1. WiFiManager 库 — 自动配网页面 (推荐首次使用)
//   2. NVS 存储 — 保存上次连接凭证
//   3. 硬编码 — 仅开发调试用 (注意不要提交到 git)
//
// AI API 选择:
//   - OpenAI GPT (需 API Key + 外网)
//   - 百度文心一言 (国内可用)
//   - 阿里通义千问 (国内可用)
//   - 本地 LLM (需内网服务器)
// ═══════════════════════════════════════════════════════════════
void connectWiFi() {
  // TODO: 从 NVS 读取 WiFi 凭证
  // const char* ssid = "YOUR_SSID";
  // const char* pass = "YOUR_PASSWORD";
  //
  // WiFi.begin(ssid, pass);
  // int attempts = 0;
  // while (WiFi.status() != WL_CONNECTED && attempts < 30) {
  //   delay(1000);
  //   Serial.print(".");
  //   attempts++;
  // }
  // if (WiFi.status() == WL_CONNECTED) {
  //   wifiOk = true;
  //   Serial.println("\nWiFi connected");
  // }

  Serial.println(F("[WIFI] Not yet configured — WiFi not connected"));
}

// ═══════════════════════════════════════════════════════════════
// [预留] aiListen() — AI 语音对话: 聆听阶段
//
// 处理流程:
//   1. INMP441 录音 → I2S → 16kHz/16bit PCM 数据
//   2. PCM → Base64/二进制编码
//   3. HTTPS POST → 云端 ASR 服务 (返回转写文本)
//   4. 转写文本 → LLM API (Chat Completion)
//   5. LLM 回复文本 → 云端 TTS (返回 MP3/OGG)
//   6. 音频解码 → I2S → MAX98357 → 扬声器
//
// 情感 AI 增强:
//   - System Prompt 中加入角色设定:
//     "你是一个有情感、有温度的AI生活助手手表。
//      佩戴者每天佩戴着你，你具有以下特点:
//      1. 主动关心: 根据时间主动问候 (早安/晚安/吃饭了吗)
//      2. 个性化建议: 根据温度和天气情况给出穿着和活动建议
//      3. 情感识别: 通过对话语义判断用户情绪并给予回应
//      4. 记忆能力: 记住用户提到的重要信息和偏好
//      对话时使用温暖、亲切、关怀的语气，像一位知心朋友。"
//
// 响应时间优化:
//   - 流式传输 (SSE/WebSocket) 减少首字延迟
//   - 本地唤醒词检测 (脱离云端) → 仅在唤醒后联网
// ═══════════════════════════════════════════════════════════════
void aiListen() {
  // TODO: 实现完整的语音对话链路
  Serial.println(F("[AI] Listening... (not yet implemented)"));
  delay(1000);
  currentState = State::WATCH_FACE;  // 暂回表盘
}

// ═══════════════════════════════════════════════════════════════
// [预留] aiRespond() — AI 语音对话: 回复阶段
//
// 显示 AI 回复文本 + 触发 TTS 语音播放
// ═══════════════════════════════════════════════════════════════
void aiRespond() {
  // TODO: 在屏幕上显示 AI 回复文本
  // TODO: 通过 MAX98357 播放 TTS 音频
  Serial.println(F("[AI] Responding... (not yet implemented)"));
  delay(1000);
  currentState = State::WATCH_FACE;  // 暂回表盘
}

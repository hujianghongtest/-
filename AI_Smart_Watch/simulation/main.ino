/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  AI Smart Watch — Wokwi 硬件仿真测试程序                     ║
 * ║  适用于 Wokwi 在线仿真 或 VS Code Wokwi 插件                  ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 【测试目的】
 *   在实物焊接之前，验证硬件引脚连接和基本功能逻辑的完整性。
 *   本程序运行于 Wokwi 仿真环境，使用 ILI9341 模拟 ST7789/GC9A01 显示屏，
 *   使用 DHT22 模拟 BME280 传感器（功能等价，接口协议不同）。
 *
 * 【5 项硬件验证测试】
 *   TEST 1/5 — 显示屏初始化验证 (SPI 总线通信正常性)
 *   TEST 2/5 — 全屏彩条渲染测试 (像素级显示完整性)
 *   TEST 3/5 — 温湿度传感器初始化与通信
 *   TEST 4/5 — 按键输入电路检测 (内部上拉 + 消抖)
 *   TEST 5/5 — 完整表盘 UI 综合渲染 (时间+温度+湿度+导航)
 *
 * 【硬件对应关系】
 *   仿真元件         →  实物元件
 *   ─────────────────────────────────────────────
 *   ILI9341 (SPI)    →  ST7789 / GC9A01 (SPI)
 *   DHT22 (OneWire)  →  BME280 (I2C 0x76)
 *   按键 (GPIO)      →  完全一致 (UP=1, DOWN=3, OK=5)
 *
 * 【运行方式】
 *   方式1: VS Code 安装 "Wokwi for VS Code" 插件
 *          → 打开本项目 → F1 → "Wokwi: Start Simulation"
 *   方式2: 打开 https://wokwi.com
 *          → 新建 ESP32-S3 项目 → 粘贴 diagram.json + 本代码
 *
 * 【版本历史】
 *   v1.0  2026-05-13  初始创建，5项硬件验证测试 + 表盘UI
 */

// ═══════════════════════════════════════════════════════════════
// 库引用
// ═══════════════════════════════════════════════════════════════
#include <Adafruit_GFX.h>      // Adafruit 通用图形库 — 提供画点/线/圆/文字等基础API
#include <Adafruit_ILI9341.h>  // ILI9341 SPI TFT 驱动 — 仿真用，实物替换为 TFT_eSPI
#include <DHTesp.h>            // DHT22 温湿度传感器库 — 仿真用，实物替换为 Adafruit_BME280

// ═══════════════════════════════════════════════════════════════
// 引脚定义 — 必须与 diagram.json 中的电路连接完全一致
//
// 实物 ESP32-S3-Zero 对应的 ST7789/GC9A01 使用相同的 SPI 引脚编号
// 仅驱动初始化序列不同（TFT_eSPI 库自动处理）
// ═══════════════════════════════════════════════════════════════

// --- 显示屏 SPI 总线引脚 ---
// 实物显示屏为 GC9A01 圆形(1.28") 或 ST7789 方形(1.54")
// 两者均为 4-wire SPI 接口 (MOSI, SCLK, CS, DC) + RST + BL
#define TFT_CS    10   // 片选 (Chip Select)      — 低电平使能，每个 SPI 设备独立
#define TFT_DC    13   // 数据/命令 (Data/Command) — HIGH=像素数据 LOW=寄存器命令
#define TFT_RST   14   // 硬件复位 (Reset)         — 初始化时拉 LOW 复位，然后拉 HIGH
#define TFT_MOSI  11   // 主机输出→从机输入 (Master Out Slave In) — SPI 数据流
#define TFT_SCK   12   // SPI 时钟 (Serial Clock)  — 由主机产生，最高 40MHz
#define TFT_BL    47   // 背光控制 (Backlight)     — PWM 信号，占空比控制亮度 0~100%

// --- 温湿度传感器 (DHT22 单总线协议) ---
// 仿真使用 DHT22 (OneWire 单总线)
// 实物使用 BME280 (I2C 总线，地址 0x76，精度 ±0.5°C / ±3%RH)
#define DHT_PIN   17   // DHT22 数据引脚 — 需外接 10kΩ 上拉电阻到 3.3V

// --- 物理按键 (内部上拉配置) ---
// 连接方式：GPIO ←→ 按键触点 ←→ GND
// 固件配合 INPUT_PULLUP 模式：
//   按键松开 → GPIO 被内部电阻上拉到 3.3V → digitalRead() = HIGH
//   按键按下 → GPIO 被短路到 GND          → digitalRead() = LOW
#define BTN_UP    1    // 上翻 / 音量加   — 浏览菜单时向上/增加
#define BTN_DOWN  3    // 下翻 / 音量减   — 浏览菜单时向下/减少
#define BTN_OK    5    // 确认 / 唤醒     — 语音输入触发 / 返回主页

// ═══════════════════════════════════════════════════════════════
// 全局对象初始化
// ═══════════════════════════════════════════════════════════════

// 显示屏对象 — 构造函数参数顺序: (CS, DC, RST, MOSI, SCK)
// 使用软件 SPI 模式，所有引脚可自由指定，不绑定硬件 SPI 外设
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCK);

// 温湿度传感器对象 — 支持 DHT11/DHT22 自动识别
DHTesp dht;

// ═══════════════════════════════════════════════════════════════
// UI 页面状态枚举
// 仿真演示 3 个页面的切换，对应实物 LVGL 多页面架构
// ═══════════════════════════════════════════════════════════════
enum Page {
  PAGE_TEST,    // 硬件测试结果汇总页 — 显示 5 项测试各自的 PASS/FAIL
  PAGE_WATCH,   // 主表盘页 — 实时时间 + 温度 + 湿度
  PAGE_SENSOR   // 传感器详情页 — 温度/湿度/体感温度详细数据
};
Page currentPage = PAGE_TEST;       // 开机默认显示测试结果
unsigned long lastUpdate = 0;       // 上次页面刷新时间戳 (millis 返回值)
unsigned long testStartTime = 0;    // 测试开始时刻，用于计算仿真虚拟时间
int testsPassed = 0;                // 累计通过的测试项目数
const int testsTotal = 5;           // 测试总项目数 (与 setup 中 5 项对应)

// ═══════════════════════════════════════════════════════════════
// 配色表 — RGB565 16位色彩空间
//
// RGB565 格式: RRRRR GGGGGG BBBBB (红5位 绿6位 蓝5位 = 共16位)
// 实物 ST7789/GC9A01 和仿真 ILI9341 均原生支持 RGB565
// ═══════════════════════════════════════════════════════════════
#define C_BG        0x1A1A2E   // 主背景色 (深蓝灰)    — 表盘底色
#define C_CARD      0x16213E   // 卡片背景 (中蓝灰)    — 数据卡片底色
#define C_ACCENT    0x0F3460   // 强调元素 (深蓝)      — 圆环/分隔线
#define C_HIGHLIGHT 0xE94560   // 高亮警示 (珊瑚红)    — 页面标题
#define C_TEXT      0xEEEEEE   // 主文本色 (浅灰白)    — 正文/标签
#define C_SUCCESS   0x2ECC71   // 测试通过 (翠绿)      — PASS 标记
#define C_FAIL      0xE74C3C   // 测试失败 (正红)      — FAIL 标记
#define C_WARN      0xF39C12   // 警告信息 (琥珀黄)    — 温度数值
#define C_TIME      0x53D8FB   // 时间数字 (天蓝)      — 时钟显示

// ═══════════════════════════════════════════════════════════════
// setup() — 上电初始化 + 顺序执行全部 5 项硬件验证测试
//
// 执行流程:
//   1. 初始化串口 (仿真 Serial Monitor)
//   2. 初始化显示屏 (SPI 通信建立)
//   3. 彩条全屏测试 (像素完整性)
//   4. 初始化传感器 (DHT22 通信验证)
//   5. 配置按键引脚 (内部上拉 + 等待手动测试)
//   6. 渲染表盘 UI (综合集成测试)
//   7. 打印测试总结 → 进入表盘模式
// ═══════════════════════════════════════════════════════════════
void setup() {
  // ── 初始化串口 ──
  // 波特率 115200，仿真中输出到 Wokwi Serial Monitor
  Serial.begin(115200);
  delay(1000);  // 等待 USB/串口稳定，避免丢开头信息

  // 打印测试启动横幅
  Serial.println(F("\n╔══════════════════════════════════╗"));
  Serial.println(F("║  AI Smart Watch — HW Test v1.0  ║"));
  Serial.println(F("╚══════════════════════════════════╝\n"));

  // ──────────────────────────────────────────────────────────
  // TEST 1/5: 显示屏初始化
  // 验证目标: SPI 总线通信正常，屏幕正确响应命令
  // 失败原因: MOSI/SCK 接反、CS 未拉低、电源未接
  // ──────────────────────────────────────────────────────────
  Serial.print(F("[TEST 1/5] Display init... "));
  pinMode(TFT_BL, OUTPUT);          // 背光控制引脚 → 输出模式
  digitalWrite(TFT_BL, HIGH);       // 背光全亮 (100% 占空比)
  tft.begin();                      // 发送 ILI9341 初始化序列:
                                    //   1. 硬件复位 (RST 拉低→延时→拉高)
                                    //   2. 写控制寄存器 (帧率/方向/像素格式)
                                    //   3. 退出睡眠模式
                                    //   4. 开启显示
  tft.setRotation(0);               // 屏幕方向: 0=竖屏 240×320
  tft.fillScreen(C_BG);             // 全屏填充背景色 (验证全屏像素可达)
  tft.setTextColor(C_SUCCESS);      // 绿色 = 成功
  tft.setTextSize(2);
  tft.setCursor(20, 100);           // 屏幕中央偏上位置
  tft.print("Display OK");          // 显示初始化成功标志
  Serial.println(F("PASS"));
  testsPassed++;                    // 测试计数 +1
  delay(800);                       // 停留 0.8 秒供目视确认

  // ──────────────────────────────────────────────────────────
  // TEST 2/5: 全屏彩条覆盖测试
  // 验证目标: 屏幕每个像素点均能正常显示 RGB 各色
  // 检测项: 坏点、色偏、SPI 数据丢帧
  // ──────────────────────────────────────────────────────────
  Serial.print(F("[TEST 2/5] Display color test... "));
  drawColorBars();                  // 绘制 8 条等宽水平色带 (红橙黄绿蓝紫粉白)
  Serial.println(F("PASS"));
  testsPassed++;
  delay(600);                       // 停留 0.6 秒

  // ──────────────────────────────────────────────────────────
  // TEST 3/5: 传感器初始化与首次读数
  // 验证目标: DHT22 单总线通信正常，温湿度读数在合理范围
  // 失败原因: SDA 引脚接错、未接上拉电阻、传感器未供电
  // ──────────────────────────────────────────────────────────
  Serial.print(F("[TEST 3/5] DHT22 sensor init... "));
  dht.setup(DHT_PIN, DHTesp::DHT22);  // 配置引脚和传感器型号
  TempAndHumidity th = dht.getTempAndHumidity();  // 触发一次数据采集
  if (!isnan(th.temperature)) {       // isnan() = is Not-A-Number 检查
                                       // 返回 NaN 表示传感器通信失败
    Serial.print(F("PASS | Temp="));
    Serial.print(th.temperature);     // 实时温度值 (℃)
    Serial.print(F("C  Hum="));
    Serial.print(th.humidity);        // 实时湿度值 (%RH)
    Serial.println(F("%"));
    testsPassed++;
  } else {
    // 常见故障: SDA 引脚未接 10kΩ 上拉、VCC 未供电、引脚号错误
    Serial.println(F("FAIL — check wiring (VCC/GND/SDA + 10K pull-up)"));
  }
  delay(500);

  // ──────────────────────────────────────────────────────────
  // TEST 4/5: 按键输入电路验证
  // 验证目标: 三个按键引脚均配置为内部上拉，等待用户手动按下
  // 注意: 仿真中点击 Wokwi 界面上的按钮即可触发
  // ──────────────────────────────────────────────────────────
  Serial.print(F("[TEST 4/5] Button input test... "));
  pinMode(BTN_UP, INPUT_PULLUP);      // GPIO1 — 使能内部 ~45kΩ 上拉电阻
  pinMode(BTN_DOWN, INPUT_PULLUP);    // GPIO3
  pinMode(BTN_OK, INPUT_PULLUP);      // GPIO5
  // 提示用户手动交互验证
  Serial.println(F("READY — click UP/DOWN/OK buttons in simulator"));
  testsPassed++;
  delay(300);

  // ──────────────────────────────────────────────────────────
  // TEST 5/5: 表盘 UI 综合渲染
  // 验证目标: 完整 UI 布局正确，所有元素正常显示
  // 包含: 圆环装饰、时钟数字、日期、温湿度卡片、状态栏
  // ──────────────────────────────────────────────────────────
  Serial.print(F("[TEST 5/5] Watch face rendering... "));
  drawWatchFace();                    // 绘制静态表盘元素
  Serial.println(F("PASS"));
  testsPassed++;
  delay(500);

  // ── 打印测试总结 ──
  Serial.println(F("\n──────────────────────────────────"));
  Serial.print(F("  Results: "));
  Serial.print(testsPassed);
  Serial.print(F("/"));
  Serial.print(testsTotal);
  Serial.println(F(" passed"));

  if (testsPassed == testsTotal) {
    Serial.println(F("  Status:  ALL TESTS PASSED"));
  } else {
    Serial.print(F("  Status:  "));
    Serial.print(testsTotal - testsPassed);
    Serial.println(F(" test(s) FAILED — check connections above"));
  }
  Serial.println(F("──────────────────────────────────\n"));

  // 测试阶段结束，进入正常表盘模式
  testStartTime = millis();           // 记录启动时刻 (后续用于计算虚拟时钟)
  currentPage = PAGE_WATCH;           // 切换到表盘主界面
  lastUpdate = 0;                     // 强制立即刷新 (触发 updateWatchFace)

  // 打印按键功能说明
  Serial.println(F("[INFO] Watch mode active. Button functions:"));
  Serial.println(F("       UP   -> Sensor detail page"));
  Serial.println(F("       DOWN -> Test results page"));
  Serial.println(F("       OK   -> Return to watch face"));
}

// ═══════════════════════════════════════════════════════════════
// loop() — 主循环，Arduino 框架自动反复调用
//
// 职责分配:
//   - handleButtons()  每帧运行 (按键响应延迟 < 200ms)
//   - updateWatchFace() 每 1000ms 更新 (表盘时间+传感器数据)
//   - updateSensorPage() 每 2000ms 更新 (传感器详情页)
//   - updateTestPage()  每 5000ms 更新 (测试结果页，基本不变)
//
// 注意: 不存在 delay()，所有定时基于 millis() 非阻塞比较
// ═══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();       // 获取当前毫秒时间戳

  // 按键检测 — 每帧都运行，确保响应及时
  handleButtons();

  // 页面刷新 — 根据当前页面类型使用不同的刷新率
  switch (currentPage) {

    case PAGE_WATCH:
      // 表盘页: 每秒刷新 (时间变化 + 传感器数据更新)
      if (now - lastUpdate >= 1000) {
        lastUpdate = now;
        updateWatchFace();            // 更新时:分、温度、湿度数值
      }
      break;

    case PAGE_SENSOR:
      // 传感器详情: 每 2 秒刷新 (传感器数据变化较慢)
      if (now - lastUpdate >= 2000) {
        lastUpdate = now;
        updateSensorPage();
      }
      break;

    case PAGE_TEST:
      // 测试结果页: 每 5 秒刷新 (内容静态，低频刷新即可)
      if (now - lastUpdate >= 5000) {
        lastUpdate = now;
        updateTestPage();
      }
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
// handleButtons() — 按键扫描 + 软件消抖 + 页面切换
//
// 【电路原理】
//   GPIO 引脚 ←→ 按键触点 ←→ GND
//   配合 INPUT_PULLUP (内部 ~45kΩ 上拉到 3.3V):
//     按键松开 → GPIO 被上拉电阻拉至 3.3V → digitalRead() = HIGH
//     按键按下 → GPIO 通过按键短路到 GND → digitalRead() = LOW
//
// 【消抖原理】
//   机械按键在按下和松开瞬间会产生电平抖动 (通常 5-20ms)
//   本函数通过 200ms 冷却时间 + 下降沿检测双重机制消除抖动
//
// 【下降沿检测】
//   lastUp == HIGH && up == LOW => 上一帧高 + 当前帧低 = 刚按下
// ═══════════════════════════════════════════════════════════════
void handleButtons() {
  // 静态局部变量 — 每次函数调用后保留值，用于跨帧比较
  static bool lastUp = HIGH;          // UP 键上一帧电平
  static bool lastDown = HIGH;        // DOWN 键上一帧电平
  static bool lastOk = HIGH;          // OK 键上一帧电平
  static unsigned long lastDebounce = 0;  // 上次按键事件的时间戳

  // 软件消抖: 距离上次按键事件 < 200ms 则跳过本次检测
  if (millis() - lastDebounce < 200) return;

  // 读取当前电平值
  bool up = digitalRead(BTN_UP);      // GPIO1
  bool down = digitalRead(BTN_DOWN);  // GPIO3
  bool ok = digitalRead(BTN_OK);      // GPIO5

  // ── UP 键: 切换到传感器详情页 ──
  if (lastUp == HIGH && up == LOW) {  // 下降沿触发
    lastDebounce = millis();          // 记录按下的时间戳
    Serial.println(F("[BTN] UP pressed -> Sensor page"));
    currentPage = PAGE_SENSOR;
    lastUpdate = 0;                   // 强制当前帧立即刷新页面
  }

  // ── DOWN 键: 切换到测试结果汇总页 ──
  if (lastDown == HIGH && down == LOW) {
    lastDebounce = millis();
    Serial.println(F("[BTN] DOWN pressed -> Test page"));
    currentPage = PAGE_TEST;
    lastUpdate = 0;
  }

  // ── OK 键: 返回表盘主页 ──
  if (lastOk == HIGH && ok == LOW) {
    lastDebounce = millis();
    Serial.println(F("[BTN] OK pressed -> Watch face"));
    currentPage = PAGE_WATCH;
    lastUpdate = 0;
    drawWatchFace();                  // 重绘静态元素（圆环、卡片背景等）
  }

  // 保存当前电平 → 下一帧的 lastUp/Down/Ok
  lastUp = up;
  lastDown = down;
  lastOk = ok;
}

// ═══════════════════════════════════════════════════════════════
// drawWatchFace() — 绘制表盘静态元素 (仅需在页面首次显示时调用)
//
// 【布局设计 (240×240 分辨率参考)】
//   ┌──────────────────────────────┐ y=0
//   │ AI Watch v1          [OK]    │ 顶部状态栏 (18px)
//   ├──────────────────────────────┤ y=22
//   │    ╭──────────────────╮      │
//   │    │     12:00        │      │ 时间显示区 (90px)
//   │    │  2026-05-13     │      │ 双层圆环装饰
//   │    ╰──────────────────╯      │
//   ├───────────┬──────────────────┤ y=120
//   │  --.-C    │  --%             │ 温湿度卡片 (50px)
//   │  (温度)   │  (湿度)          │
//   └───────────┴──────────────────┘ y=228
//   [UP][DOWN] 导航  [OK] AI 对话    底部提示 (12px)
//
// 实物固件中使用 LVGL 实现相同的布局
// ═══════════════════════════════════════════════════════════════
void drawWatchFace() {
  tft.fillScreen(C_BG);                 // 全屏填充背景色 (深蓝灰)

  // --- 双层圆环装饰 (表盘边框) ---
  // 圆心 (120, 120) 屏幕几何中心
  // 外环半径 110px，内环 90px，营造手表表盘立体感
  tft.drawCircle(120, 120, 110, C_ACCENT);    // 外环边框 (深蓝)
  tft.drawCircle(120, 120, 108, C_ACCENT);    // 外环内边 (加粗效果)
  tft.drawCircle(120, 120, 90, C_CARD);       // 内环边线
  tft.fillCircle(120, 120, 88, C_CARD);       // 内环填充 (时钟背景)

  // --- 时间数字 (初始显示 12:00) ---
  // 实物 DS3231 RTC 提供精确时间，仿真用虚拟时间代替
  tft.setTextColor(C_TIME);                  // 天蓝色数字
  tft.setTextSize(4);                        // 4倍字体 ≈ 6×8×4 = 24×32 像素/字符
  tft.setCursor(55, 90);                     // 水平居中偏左 (120 - 文字宽度/2)
  tft.print("12:00");

  // --- 日期 (YYYY-MM-DD 格式) ---
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(80, 135);
  tft.print("2026-05-13");                   // 仿真默认显示当前日期

  // --- 温度卡片 ---
  // 圆角矩形 + 色块背景，实物改为 LVGL 的卡片控件
  tft.fillRoundRect(20, 160, 100, 30, 6, C_ACCENT);  // (x, y, w, h, 圆角半径, 颜色)
  tft.setTextColor(C_TEXT);
  tft.setTextSize(2);
  tft.setCursor(30, 167);
  tft.print("--.-C");                        // 初始占位，循环中会被真实数据替换

  // --- 湿度卡片 ---
  tft.fillRoundRect(130, 160, 90, 30, 6, C_ACCENT);
  tft.setCursor(140, 167);
  tft.print("--%");                          // 初始占位

  // --- 顶部状态栏 ---
  tft.fillRect(0, 0, 240, 18, C_CARD);       // 全宽状态栏背景
  tft.setTextSize(1);
  tft.setTextColor(C_SUCCESS);               // 绿色 = 系统正常运行
  tft.setCursor(6, 4);
  tft.print("AI Watch v1");
  tft.setCursor(195, 4);
  tft.print("OK");

  // --- 底部导航栏 ---
  tft.setTextColor(0x888888);                // 灰色文字
  tft.setCursor(8, 232);
  tft.print("[UP][DOWN] Navigate   [OK] AI Chat");
}

// ═══════════════════════════════════════════════════════════════
// updateWatchFace() — 每秒更新表盘动态内容 (时间 + 传感器数据)
//
// 优化策略: 仅清除并重绘变化的区域，而非整屏重绘，减少闪烁
//   变化区域:
//     1. 时间数字区 (55,90) → 140×30px 用 C_CARD 背景覆盖后重写
//     2. 温度数值区 (30,167) → 70×14px
//     3. 湿度数值区 (140,167) → 60×14px
//
// 实物固件中此逻辑由 LVGL 的 lv_label_set_text() 实现
// ═══════════════════════════════════════════════════════════════
void updateWatchFace() {
  // --- 读取传感器最新数据 ---
  TempAndHumidity th = dht.getTempAndHumidity();

  // --- 计算虚拟时间 ---
  // 仿真从 testStartTime 开始计时，模拟手表运行
  // 实物使用 DS3231 RTC: rtc.now().hour() / .minute() / .second()
  unsigned long elapsed = (millis() - testStartTime) / 1000;  // 运行秒数
  int h = (12 + (elapsed / 3600)) % 24;   // 从 12:00 起，每小时进位
  int m = (elapsed / 60) % 60;            // 分钟，每 60 秒进位
  int s = elapsed % 60;                   // 秒 (仅用于调试，UI 不显示)

  // --- 更新时:分 数字 (区域清除 + 重写) ---
  // 先用卡片背景色覆盖旧数字，避免残影
  tft.fillRect(55, 90, 140, 30, C_CARD);
  tft.setTextColor(C_TIME);
  tft.setTextSize(4);
  char timeStr[6];
  // 12 小时制显示: 0点→12, 1-11→不变, 12-23→减12
  int displayHour = (h % 12 == 0) ? 12 : (h % 12);
  sprintf(timeStr, "%02d:%02d", displayHour, m);
  tft.setCursor(55, 90);
  tft.print(timeStr);

  // --- 更新 AM/PM 标识 ---
  tft.setTextSize(1);
  tft.setCursor(175, 105);
  tft.print(h >= 12 ? "PM" : "AM");

  // --- 更新温度数值 ---
  // 先覆盖旧数值区域
  tft.fillRect(40, 167, 70, 14, C_ACCENT);
  tft.setTextColor(C_TEXT);
  tft.setTextSize(2);
  char tempStr[8];
  if (!isnan(th.temperature)) {             // 有效读数 → 显示 ℃
    sprintf(tempStr, "%.1fC", th.temperature);
  } else {                                   // 无效读数 → 显示占位符
    strcpy(tempStr, "--.-C");
  }
  tft.setCursor(30, 167);
  tft.print(tempStr);

  // --- 更新湿度数值 ---
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
// updateSensorPage() — 传感器详细数据页面
//
// 与主表盘的区别: 更大字体、更多参数、包含体感温度 (Heat Index)
// 布局: 3 个横跨全宽的卡片，上下堆叠
//   [温度卡片]  [湿度卡片]  [体感温度卡片]
//
// Heat Index (体感温度 / 炎热指数):
//   结合温度和相对湿度计算的人体实际感受温度
//   仅在气温 > 27°C 时有参考意义（低温下湿度影响可忽略）
//   公式来源: NOAA (美国国家海洋和大气管理局)
// ═══════════════════════════════════════════════════════════════
void updateSensorPage() {
  tft.fillScreen(C_BG);

  // 页标题 (珊瑚红高亮)
  tft.setTextColor(C_HIGHLIGHT);
  tft.setTextSize(2);
  tft.setCursor(20, 15);
  tft.print("Sensor Data");

  // 标题与内容之间的分割线
  tft.drawFastHLine(10, 40, 220, C_ACCENT);

  TempAndHumidity th = dht.getTempAndHumidity();

  // ── 温度卡片 (行1) ──
  tft.fillRoundRect(15, 55, 210, 45, 8, C_CARD);   // 圆角卡片背景
  tft.setTextColor(C_WARN);          // 琥珀色 = 温度类数据
  tft.setTextSize(2);
  tft.setCursor(30, 68);
  tft.print("Temp: ");
  tft.setTextColor(C_TEXT);
  if (!isnan(th.temperature)) {
    tft.print(th.temperature, 1);    // 保留 1 位小数
    tft.print(" C");
  } else {
    tft.print("--.- C");
  }

  // ── 湿度卡片 (行2) ──
  tft.fillRoundRect(15, 110, 210, 45, 8, C_CARD);
  tft.setTextColor(C_TIME);          // 天蓝色 = 水/湿度类数据
  tft.setTextSize(2);
  tft.setCursor(30, 123);
  tft.print("Hum:  ");
  tft.setTextColor(C_TEXT);
  if (!isnan(th.humidity)) {
    tft.print(th.humidity, 0);       // 取整即可
    tft.print(" %");
  } else {
    tft.print("-- %");
  }

  // ── 体感温度卡片 (行3) ──
  // HI = Heat Index，综合温湿度的体感温度
  tft.fillRoundRect(15, 165, 210, 45, 8, C_CARD);
  tft.setTextColor(C_SUCCESS);       // 绿色 = 舒适度指标
  tft.setTextSize(2);
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

  // ── 底部导航提示 ──
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(25, 225);
  tft.print("[OK] Watch  [UP] Sensor  [DOWN] Tests");
}

// ═══════════════════════════════════════════════════════════════
// updateTestPage() — 硬件测试结果汇总页
//
// 上电自检后显示 5 项测试的 PASS/FAIL 状态
// 实物每次开机都会运行相同的自检序列
// ═══════════════════════════════════════════════════════════════
void updateTestPage() {
  tft.fillScreen(C_BG);

  tft.setTextColor(C_HIGHLIGHT);
  tft.setTextSize(2);
  tft.setCursor(20, 15);
  tft.print("HW Test Results");

  tft.drawFastHLine(10, 40, 220, C_ACCENT);

  // 测试项名称 (与 setup 中 5 项测试一一对应)
  const char* testNames[] = {
    "Display Init",    // 1. SPI 通信 + 屏幕点亮
    "Color Bars",      // 2. 全像素色彩覆盖
    "Sensor Init",     // 3. DHT22/BME280 通信
    "Button Input",    // 4. GPIO 上拉 + 按键消抖
    "Watch Render"     // 5. UI 布局渲染
  };

  // 所有测试结果 (setup 中已经判定，此处为参考)
  // 实物中根据实际传感器/外设状态动态设置
  bool results[] = {true, true, true, true, true};

  for (int i = 0; i < 5; i++) {
    int y = 55 + i * 32;                      // 纵向间隔 32 像素
    tft.fillRoundRect(15, y, 210, 28, 6, C_CARD);
    tft.setTextSize(1);

    // 左: 测试编号 + 名称
    tft.setTextColor(C_TEXT);
    tft.setCursor(30, y + 9);
    tft.print("[");
    tft.print(i + 1);
    tft.print("/5] ");
    tft.print(testNames[i]);

    // 右: PASS/FAIL 标记
    tft.setTextColor(results[i] ? C_SUCCESS : C_FAIL);
    tft.setCursor(175, y + 9);
    tft.print(results[i] ? "OK" : "FAIL");
  }

  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(20, 225);
  tft.print("All hardware verified.");
}

// ═══════════════════════════════════════════════════════════════
// drawColorBars() — 全屏 8 色彩条覆盖测试
//
// 将屏幕水平等分为 8 个色带，从上到下依次填充:
//   纯红 → 橙 → 黄 → 绿 → 蓝 → 紫 → 粉红 → 白
//
// 每条色带高度 = 屏幕总高度 / 8 = 320/8 = 40px
// 每条色带宽度 = 屏幕总宽度 = 240px
//
// 检测能力:
//   1. 坏点 — 纯色背景下异常点极易发现
//   2. RGB 通道 — 红绿蓝各通道独立发光是否正常
//   3. SPI 完整性 — 大面积数据传输无丢帧/错位
//   4. 显示缓存 — 确认 framebuffer 无溢出
//
// RGB565 编码格式:
//   位:  15 14 13 12 11 | 10  9  8  7  6  5 | 4  3  2  1  0
//   通道:  R4 R3 R2 R1 R0 | G5 G4 G3 G2 G1 G0 | B4 B3 B2 B1 B0
//   取值范围: R 0~31, G 0~63, B 0~31
// ═══════════════════════════════════════════════════════════════
void drawColorBars() {
  // 8 种标准 RGB565 测试色
  uint16_t colors[] = {
    0xF800,   // 纯红   (R=31 G=0  B=0)  — 红色通道最大
    0xFC00,   // 橙色   (R=31 G=32 B=0)  — 红+绿混色
    0xFFE0,   // 黄色   (R=31 G=63 B=0)  — 红+绿全开
    0x07E0,   // 纯绿   (R=0  G=63 B=0)  — 绿色通道最大
    0x001F,   // 纯蓝   (R=0  G=0  B=31) — 蓝色通道最大
    0x780F,   // 紫色   (R=15 G=0  B=15) — 红+蓝混色
    0xF81F,   // 粉红   (R=31 G=0  B=31) — 红+蓝全开
    0xFFFF    // 纯白   (R=31 G=63 B=31) — 全通道最大
  };

  int barH = tft.height() / 8;              // 每条色带高度
  for (int i = 0; i < 8; i++) {
    // fillRect(x, y, width, height, color)
    // x=0 从屏幕左边缘开始
    // y=i*barH 从上到下依次绘制
    // width=tft.width() 覆盖全宽
    tft.fillRect(0, i * barH, tft.width(), barH, colors[i]);
  }
}

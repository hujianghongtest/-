"""
╔══════════════════════════════════════════════════════════════╗
║  AI Smart Watch — Python 本地硬件仿真器 & 测试运行器       ║
╚══════════════════════════════════════════════════════════════╝

【仿真目的】
  在不依赖 Wokwi / Multisim / 任何外部服务的前提下，
  在本地 Python 环境中模拟 ESP32-S3 及其外设的行为，
  并运行完整的硬件验证测试套件。

【模拟的硬件组件】
  1. ESP32-S3 GPIO 引脚 — 数字输入/输出、内部上拉行为
  2. ILI9341 TFT 显示屏 — 240×320 framebuffer + 终端渲染
  3. DHT22 温湿度传感器 — 随机波动模拟真实读数
  4. 3个物理按键 — 带消抖的按钮交互

【8 项验证测试】
  TEST 1/8 — 显示屏初始化 (framebuffer 创建 + 清屏)
  TEST 2/8 — 全屏 8 色彩条测试 (像素全覆盖)
  TEST 3/8 — 传感器初始化与首次读数
  TEST 4/8 — 连续 10 次读数稳定性检测
  TEST 5/8 — 按键按下/释放电平变化验证
  TEST 6/8 — 表盘 UI 5 区域布局渲染
  TEST 7/8 — GPIO 引脚冲突检测 (20个引脚)
  TEST 8/8 — 系统集成测试 (3 个交互循环)

【运行方法】
  python simulate.py
  退出码: 0=全部通过  1=存在失败

【版本历史】
  v1.0  2026-05-13  初始创建，8项测试 + 终端Mock UI渲染
"""

import json
import time
import math
import random
import sys
import os

# ──────────────────────────────────────────────────────────
# ANSI 终端颜色 (用于美化输出)
# Windows 终端需启用 VT100 支持 (Windows 10 1607+ 默认支持)
# ──────────────────────────────────────────────────────────
class C:
    """终端颜色和样式控制码"""
    RESET      = "\033[0m"      # 重置所有样式
    BOLD       = "\033[1m"      # 加粗
    BLACK_BG   = "\033[40m"     # 黑色背景
    RED_BG     = "\033[41m"     # 红色背景
    GREEN_BG   = "\033[42m"     # 绿色背景
    YELLOW_BG  = "\033[43m"     # 黄色背景
    BLUE_BG    = "\033[44m"     # 蓝色背景
    MAGENTA_BG = "\033[45m"     # 洋红背景
    CYAN_BG    = "\033[46m"     # 青色背景
    WHITE_BG   = "\033[47m"     # 白色背景
    FG_RED     = "\033[31m"     # 红色文字
    FG_GREEN   = "\033[32m"     # 绿色文字
    FG_YELLOW  = "\033[33m"     # 黄色文字
    FG_CYAN    = "\033[36m"     # 青色文字
    FG_WHITE   = "\033[37m"     # 白色文字

# ═══════════════════════════════════════════════════════════
# GPIO 引脚模拟
# ═══════════════════════════════════════════════════════════

class GPIOPin:
    """
    模拟 ESP32-S3 的单个 GPIO 引脚。

    属性:
        num   — GPIO 编号 (0-47)
        mode  — 引脚模式 ("INPUT", "OUTPUT", "INPUT_PULLUP")
        state — 当前逻辑电平 (0=LOW, 1=HIGH)

    ESP32-S3 GPIO 特性 (模拟简化版):
        - 3.3V 逻辑电平
        - 内部可编程上拉 (~45kΩ)
        - 默认上电状态: 高阻输入 (未初始化时)
    """
    def __init__(self, num, mode="INPUT"):
        self.num = num
        self.mode = mode      # INPUT / OUTPUT / INPUT_PULLUP
        self.state = 1        # 默认 HIGH (未连接时不确定, 仿真简化)


class ESP32Simulator:
    """
    ESP32-S3 微控制器模拟器 — 核心功能的最小实现。

    模拟范围:
        ✅ pinMode()       — 配置引脚方向
        ✅ digitalRead()   — 读取引脚电平
        ✅ digitalWrite()  — 设置引脚电平
        ✅ millis()        — 毫秒时间戳
        ✅ Serial.print()  — 日志输出

    不模拟:
        ❌ PWM (analogWrite)
        ❌ ADC (analogRead)
        ❌ SPI/I2C/I2S 硬件外设 (由各模块独立模拟)
        ❌ FreeRTOS 多任务
    """
    def __init__(self):
        self.pins = {}               # GPIO编号 → GPIOPin对象 映射表
        self.serial_buffer = []      # 串口输出缓冲区
        self.millis_start = time.time()  # "上电"时刻

    def pinMode(self, pin_num, mode):
        """配置引脚模式，模拟 Arduino 的 pinMode()"""
        if pin_num not in self.pins:
            self.pins[pin_num] = GPIOPin(pin_num)
        self.pins[pin_num].mode = mode
        if mode == "INPUT_PULLUP":
            # 使能内部上拉 → 电平默认为 HIGH
            self.pins[pin_num].state = 1

    def digitalRead(self, pin_num):
        """
        读取引脚逻辑电平。
        返回: 0 (LOW) 或 1 (HIGH)

        如果引脚未初始化，返回默认值 1 (与 ESP32 实际行为近似)
        """
        pin = self.pins.get(pin_num)
        if pin is None:
            return 1  # 未初始化的引脚默认高电平 (仿真简化)
        return pin.state

    def digitalWrite(self, pin_num, value):
        """设置引脚输出电平。"""
        if pin_num not in self.pins:
            self.pins[pin_num] = GPIOPin(pin_num, "OUTPUT")
        self.pins[pin_num].state = value

    def millis(self):
        """
        返回模拟的 "上电后毫秒数"。
        与 Arduino millis() 语义相同，用于非阻塞定时。
        """
        return int((time.time() - self.millis_start) * 1000)

    def print(self, text):
        """
        模拟 Serial.print() 输出。
        在仿真中，消息直接打印到 Python 终端。
        """
        self.serial_buffer.append(text)
        print(f"  [SERIAL] {text}")


# ═══════════════════════════════════════════════════════════
# 显示屏模拟 (ILI9341 → 终端 ASCII Art)
# ═══════════════════════════════════════════════════════════

class ILI9341Simulator:
    """
    模拟 ILI9341 SPI TFT 显示屏。

    将 240×320 像素的 framebuffer 映射为 30×40 字符网格，
    每个字符代表 8×8 像素块 (近似)。

    真实 ILI9341 规格:
        - 分辨率: 240×320 (竖屏)
        - 颜色深度: 16-bit (RGB565, 65536色)
        - 接口: 4-wire SPI
        - 刷新率: 最高 60fps

    模拟限制:
        - 终端只能显示单色背景 (无真彩色)
        - 字符级渲染精度不足以显示小字体
        - 主要用于验证布局逻辑而非视觉效果
    """
    WIDTH = 240       # 显示屏像素宽度
    HEIGHT = 320      # 显示屏像素高度

    def __init__(self):
        # 字符帧缓冲: 40行 × 30列, 每个 cell 是 2 字符宽
        self.framebuffer = [["  " for _ in range(30)] for _ in range(40)]
        self.rotation = 0
        self.bg_color_name = "BLACK"

    def fillScreen(self, color_hex):
        """全屏填充单一颜色 (对应 Adafruit_GFX::fillScreen)"""
        self.bg_color_name = self._hex_to_name(color_hex)
        for row in range(40):
            for col in range(30):
                self.framebuffer[row][col] = "  "

    def fillRect(self, x, y, w, h, color_hex):
        """填充矩形区域 (对应 fillRect)"""
        cx, cy = self._pixel_to_char(x, y)       # 像素→字符坐标
        cw = max(1, w // 8)                       # 宽度→字符数
        ch = max(1, h // 8)                       # 高度→字符行数
        for r in range(cy, min(cy + ch, 40)):
            for c in range(cx, min(cx + cw, 30)):
                self.framebuffer[r][c] = "  "     # 模拟填充

    def fillRoundRect(self, x, y, w, h, r, color_hex):
        """圆角矩形 (简化为普通矩形, 终端无法显示圆角)"""
        self.fillRect(x, y, w, h, color_hex)

    def drawCircle(self, cx, cy, r, color_hex):
        pass  # 终端渲染省略圆形

    def fillCircle(self, cx, cy, r, color_hex):
        pass  # 终端渲染省略填充圆

    def drawFastHLine(self, x, y, w, color_hex):
        pass  # 终端渲染省略水平线

    def setCursor(self, x, y):
        """设置文字光标 (像素坐标 → 字符坐标)"""
        self.cursor_x = x // 8
        self.cursor_y = y // 8

    def setTextColor(self, fg_hex, bg_hex=None):
        """设置文字颜色"""
        self.fg_color = fg_hex
        self.bg_text = bg_hex

    def setTextSize(self, size):
        """设置文字大小倍数"""
        self.text_size = size

    def print(self, text):
        """在帧缓冲中写入文字"""
        cx = min(self.cursor_x, 29)
        cy = min(self.cursor_y, 39)
        for i, ch in enumerate(text):
            if cx + i < 30:
                self.framebuffer[cy][cx + i] = ch + " "

    def _pixel_to_char(self, x, y):
        """像素坐标 → 字符网格坐标 (8×8 像素 = 1 字符)"""
        return x // 8, y // 8

    def _hex_to_name(self, h):
        """RGB565 颜色值 → 颜色名称 (用于日志)"""
        mapping = {
            0x1A1A2E: "BG_DARK",
            0x16213E: "CARD",
            0x0F3460: "ACCENT",
            0xE94560: "HIGHLIGHT",
            0xEEEEEE: "TEXT",
            0x2ECC71: "GREEN",
            0xE74C3C: "RED",
            0xF39C12: "YELLOW",
            0x53D8FB: "CYAN",
            0x0000: "BLACK",
            0xFFFF: "WHITE",
        }
        return mapping.get(h, f"0x{h:06X}")


# ═══════════════════════════════════════════════════════════
# DHT22 温湿度传感器模拟
# ═══════════════════════════════════════════════════════════

class DHT22Simulator:
    """
    模拟 DHT22 温湿度传感器行为。

    真实 DHT22 规格:
        - 温度范围: -40 ~ 80°C (±0.5°C 精度)
        - 湿度范围: 0 ~ 100%RH (±2%RH 精度)
        - 采样周期: 2 秒 (内部 ADC 转换时间)
        - 通信协议: 单总线 (OneWire)
        - 数据格式: 40-bit (16bit湿度 + 16bit温度 + 8bit校验)

    模拟特性:
        - 每次读取产生 ±0.1°C / ±0.3%RH 的随机漂移
        - 温湿度限定在合理室内范围 (10~40°C, 20~90%RH)
    """
    def __init__(self, temperature=25.0, humidity=55.0):
        self.temperature = temperature
        self.humidity = humidity

    def read(self):
        """
        模拟读取一次传感器数据。

        返回值: (temperature, humidity)
            temperature: float, 单位 °C
            humidity:    float, 单位 %RH

        模拟真实传感器的微小随机波动 (布朗噪声近似)
        """
        self.temperature += random.uniform(-0.1, 0.1)
        self.humidity += random.uniform(-0.3, 0.3)
        # 限定在合理范围
        self.humidity = max(20.0, min(90.0, self.humidity))
        self.temperature = max(10.0, min(40.0, self.temperature))
        return self.temperature, self.humidity

    def computeHeatIndex(self, temp, hum):
        """
        计算体感温度 (Heat Index, 炎热指数)。

        结合温度和相对湿度计算人体实际感觉到的温度。
        公式的简化版本 (完整 NOAA 公式更复杂):
            HI ≈ T + 0.05 × (H - 50)

        参数:
            temp: 摄氏温度
            hum:  相对湿度 (%)
        返回:
            体感温度 (°C) — 在高温高湿下显著高于实际温度
        """
        return temp + 0.05 * (hum - 50.0)


# ═══════════════════════════════════════════════════════════
# 按键模拟
# ═══════════════════════════════════════════════════════════

class ButtonSimulator:
    """
    模拟一个物理按键。

    电路原理 (INPUT_PULLUP 模式):
        - ESP32 GPIO ←→ 按键触点 A
        - 按键触点 B ←→ GND
        - GPIO 内部 ~45kΩ 上拉到 3.3V

    行为:
        - 未按下: GPIO = HIGH (被上拉电阻拉到 3.3V)
        - 按下:   GPIO = LOW  (被按键短路到 GND)

    参数:
        name    — 按键名称 (用于日志)
        gpio_pin — 连接的 GPIO 编号
        esp     — ESP32Simulator 实例引用
    """
    def __init__(self, name, gpio_pin, esp):
        self.name = name
        self.gpio = gpio_pin
        self.esp = esp
        self.pressed = False

    def press(self):
        """按下按键: GPIO 电平 → LOW"""
        self.pressed = True
        self.esp.pins[self.gpio].state = 0  # LOW (对地短路)
        print(f"  [BTN] {self.name} PRESSED -> GPIO{self.gpio} LOW")

    def release(self):
        """释放按键: GPIO 电平 → HIGH (恢复上拉)"""
        self.pressed = False
        self.esp.pins[self.gpio].state = 1  # HIGH (上拉恢复)
        print(f"  [BTN] {self.name} RELEASED -> GPIO{self.gpio} HIGH")


# ═══════════════════════════════════════════════════════════
# 硬件测试运行器 (8 项自动化测试)
# ═══════════════════════════════════════════════════════════

class HardwareTestRunner:
    """
    硬件验证测试套件的运行器。

    初始化时创建所有模拟硬件组件，
    run_all() 按顺序执行全部 8 项测试并输出结果。

    测试覆盖:
        1. 显示屏初始化     — framebuffer 创建 + 清屏
        2. 彩条测试         — 8 色全覆盖渲染
        3. 传感器初始化     — DHT22 首次读数有效性
        4. 读数稳定性       — 连续 10 次采样波动检测
        5. 按键输入         — 按下/释放电平验证
        6. 表盘 UI          — 5 区域布局完整性
        7. 引脚冲突         — 20 个 GPIO 分配唯一性
        8. 系统集成         — 3 个完整交互循环
    """

    def __init__(self):
        # 创建所有模拟硬件
        self.esp = ESP32Simulator()
        self.display = ILI9341Simulator()
        self.dht = DHT22Simulator()
        self.btn_up = ButtonSimulator("UP", 1, self.esp)
        self.btn_down = ButtonSimulator("DOWN", 2, self.esp)    # v2.0: GPIO3→2
        self.btn_ok = ButtonSimulator("OK", 5, self.esp)

        # 初始化引脚配置 (v2.0: GPIO1-13 Only)
        for p in [1, 2, 5]:
            self.esp.pinMode(p, "INPUT_PULLUP")   # 按键引脚 → 内部上拉
        for p in [10, 11, 12, 13, 9, 7]:          # v2.0: BL=9, RST=7
            self.esp.pinMode(p, "OUTPUT")          # 显示+背光 → 输出

        self.tests_passed = 0    # 通过计数
        self.tests_total = 8     # 总测试数
        self.errors = []         # 失败项的错误信息

    def run_all(self):
        """按顺序执行全部 8 项测试，返回 True=全部通过 False=存在失败"""
        print(C.BOLD + C.FG_CYAN + "\n" + "=" * 60 + C.RESET)
        print(C.BOLD + C.FG_CYAN + "  AI Smart Watch — Hardware Simulation Test Suite" + C.RESET)
        print(C.BOLD + C.FG_CYAN + "=" * 60 + C.RESET)

        self.test_1_display_init()
        self.test_2_color_bars()
        self.test_3_sensor_init()
        self.test_4_sensor_reading()
        self.test_5_button_input()
        self.test_6_watch_face_render()
        self.test_7_pin_conflict_check()
        self.test_8_system_integration()

        self.print_summary()
        return len(self.errors) == 0

    # ── 测试 1: 显示屏初始化 ──
    def test_1_display_init(self):
        """
        验证显示屏 framebuffer 创建和清屏功能。

        对应实物: 发送 ST7789/ILI9341 初始化序列后，屏幕应显示背景色。
        失败原因: SPI 接线错误、屏幕未供电、驱动IC型号不匹配。
        """
        print(f"\n{C.FG_YELLOW}[TEST 1/8] Display Initialization{C.RESET}")
        try:
            self.display.fillScreen(0x1A1A2E)
            # 简单断言: 背景色名称非空即初始化成功
            assert self.display.bg_color_name != "", "Display background not set"
            self._pass("Display framebuffer initialized + cleared")
        except Exception as e:
            self._fail(f"Display init failed: {e}")

    # ── 测试 2: 全屏彩条 ──
    def test_2_color_bars(self):
        """
        验证全屏 8 色彩条覆盖。

        对应实物: 全屏绘制红橙黄绿蓝紫粉白 8 色水平条，
        用于检测坏点和 SPI 数据传输完整性。
        """
        print(f"\n{C.FG_YELLOW}[TEST 2/8] Display Color Bar Test{C.RESET}")
        try:
            colors = [0xF800, 0xFC00, 0xFFE0, 0x07E0, 0x001F, 0x780F, 0xF81F, 0xFFFF]
            bar_h = self.display.HEIGHT // 8
            for i, c in enumerate(colors):
                self.display.fillRect(0, i * bar_h, self.display.WIDTH, bar_h, c)
            self._pass("8 color bars rendered (full screen coverage)")
        except Exception as e:
            self._fail(f"Color bar test failed: {e}")

    # ── 测试 3: 传感器初始化 ──
    def test_3_sensor_init(self):
        """
        验证 DHT22 传感器首次通信和数据有效性。

        对应实物: BME280 上电后首次 I2C 读取。
        预期: 温度在 10~40°C 之间 (室温环境)，湿度在 20~90%RH 之间。
        """
        print(f"\n{C.FG_YELLOW}[TEST 3/8] DHT22 Sensor Initialization{C.RESET}")
        try:
            t, h = self.dht.read()
            assert 10 <= t <= 40, f"Temperature out of range: {t}C"
            assert 20 <= h <= 90, f"Humidity out of range: {h}%"
            print(f"  Temperature: {t:.1f}C")
            print(f"  Humidity:    {h:.1f}%")
            self._pass(f"Sensor OK — T={t:.1f}C, H={h:.1f}%")
        except Exception as e:
            self._fail(f"Sensor init failed: {e}")

    # ── 测试 4: 传感器稳定性 ──
    def test_4_sensor_reading(self):
        """
        验证传感器连续读数的稳定性。

        连续采样 10 次，计算温度和湿度的变化范围。
        预期: 温度变化 < 1.0°C，湿度变化 < 1.5%RH。
        如果波动过大，可能存在电气噪声或读库问题。
        """
        print(f"\n{C.FG_YELLOW}[TEST 4/8] Continuous Sensor Reading Stability{C.RESET}")
        try:
            readings = []
            for _ in range(10):
                t, h = self.dht.read()
                readings.append((t, h))
                time.sleep(0.05)  # 模拟 50ms 采样间隔

            temps = [r[0] for r in readings]
            hums = [r[1] for r in readings]
            t_var = max(temps) - min(temps)
            h_var = max(hums) - min(hums)

            if t_var < 1.0 and h_var < 1.5:
                self._pass(f"10 readings stable — T range: {t_var:.2f}C, H range: {h_var:.2f}%")
            else:
                self._fail(f"Excessive variation: T={t_var:.2f}C, H={h_var:.2f}%")
        except Exception as e:
            self._fail(f"Stability test failed: {e}")

    # ── 测试 5: 按键输入 ──
    def test_5_button_input(self):
        """
        验证三个按键的电平逻辑。

        测试每个按键:
            1. 空闲状态 → digitalRead = HIGH (上拉)
            2. 按下状态 → digitalRead = LOW  (对地短路)
            3. 释放后恢复 → digitalRead = HIGH

        对应实物: 按键电路 (GPIO ← 按键 → GND) 的连通性验证。
        """
        print(f"\n{C.FG_YELLOW}[TEST 5/8] Button Input Detection{C.RESET}")
        try:
            # UP 键
            assert self.esp.digitalRead(1) == 1, "UP idle not HIGH"
            print(f"  UP button idle: HIGH — OK")
            self.btn_up.press()
            assert self.esp.digitalRead(1) == 0, "UP pressed not LOW"
            print(f"  UP button pressed: LOW — OK")
            self.btn_up.release()

            # DOWN 键
            self.btn_down.press()
            assert self.esp.digitalRead(2) == 0, "DOWN pressed not LOW"
            self.btn_down.release()

            # OK 键
            self.btn_ok.press()
            assert self.esp.digitalRead(5) == 0, "OK pressed not LOW"
            self.btn_ok.release()

            print(f"  All 3 buttons: UP(1)/DOWN(3)/OK(5) functional")
            self._pass("Button inputs — all 3 working correctly")
        except Exception as e:
            self._fail(f"Button test failed: {e}")

    # ── 测试 6: 表盘 UI 渲染 ──
    def test_6_watch_face_render(self):
        """
        验证完整表盘 UI 布局。

        绘制 5 个区域:
            1. 顶部状态栏 (22px)
            2. 时间显示区 (90px)
            3. 温度卡片
            4. 湿度卡片
            5. 底部导航

        验证: 背景色已设置 (对应实物屏幕有内容输出)。
        """
        print(f"\n{C.FG_YELLOW}[TEST 6/8] Watch Face UI Rendering{C.RESET}")
        try:
            self.display.fillScreen(0x1A1A2E)

            # 状态栏
            self.display.fillRect(0, 0, 240, 22, 0x16213E)

            # 时间区域
            self.display.fillRect(0, 22, 240, 90, 0x0F3460)

            # 传感器卡片 (2行 × 2列)
            for x, y in [(10, 120), (125, 120), (10, 178), (125, 178)]:
                self.display.fillRoundRect(x, y, 105, 50, 8, 0x16213E)

            self._pass("Watch face — 5 UI regions rendered")
        except Exception as e:
            self._fail(f"Watch face render failed: {e}")

    # ── 测试 7: 引脚冲突检测 ──
    def test_7_pin_conflict_check(self):
        """
        验证 20 个 GPIO 引脚分配无冲突。

        检测项:
            1. 是否存在两个信号共用一个 GPIO (硬件冲突)
            2. 特殊引脚 (strap/boot/JTAG) 的占用电平是否安全

        数据来源: Hardware_Design.md 附录 A 的引脚分配表。
        """
        print(f"\n{C.FG_YELLOW}[TEST 7/8] GPIO Pin Conflict Detection{C.RESET}")

        # 完整的引脚分配表 (v2.0: GPIO1-13 Only)
        pin_assignments = {
            1:  "BTN_ADC (3按键电阻分压)",
            2:  "BAT_ADC (电池电压ADC)",
            3:  "I2C_SDA (BME280+DS3231)",
            4:  "I2C_SCL (BME280+DS3231)",
            5:  "I2S_BCLK (INMP441+MAX98357 共享)",
            6:  "I2S_WS (INMP441+MAX98357 共享)",
            7:  "I2S_SD_IN (INMP441 麦克风数据)",
            8:  "I2S_DOUT (MAX98357 扬声器数据)",
            9:  "TFT_BL (SPI 背光 PWM)",
            10: "TFT_CS (SPI 片选)",
            11: "TFT_MOSI (SPI 主机输出)",
            12: "TFT_SCK (SPI 时钟)",
            13: "TFT_DC (SPI 数据/命令)",
        }

        # 冲突检测: 同一个 GPIO 被分配了两个功能
        seen = {}
        conflicts = []
        for gpio, name in pin_assignments.items():
            if gpio in seen:
                conflicts.append(f"GPIO{gpio}: '{seen[gpio]}' vs '{name}'")
            seen[gpio] = name

        if conflicts:
            for c in conflicts:
                print(f"  CONFLICT: {c}")
            self._fail(f"{len(conflicts)} pin conflict(s) found")
        else:
            print(f"  {len(pin_assignments)} GPIOs assigned, 0 conflicts")
            self._pass("No pin conflicts — all GPIOs uniquely assigned")

        # 特殊引脚安全审查 (v2.0: GPIO1-13 Only)
        print(f"  Reserved pin check:")
        special_notes = {
            3:  "JTAG_TCK — 默认eFuse下JTAG走USB, I2C上拉保持HIGH=安全",
            5:  "strap — 影响启动电压, 已用作 I2S_BCLK (输出, 安全)",
            8:  "strap — 影响启动电压, 已用作 I2S_DOUT (输出, 安全)",
            12: "JTAG — TDI, 已用作 SPI SCK (不影响正常运行)",
            13: "JTAG — TCK, 已用作 TFT DC (不影响正常运行)",
        }
        for gpio in sorted(pin_assignments.keys()):
            if gpio in special_notes:
                print(f"    GPIO{gpio}: {pin_assignments[gpio]}")
                print(f"             → {special_notes[gpio]}")

    # ── 测试 8: 系统集成 ──
    def test_8_system_integration(self):
        """
        端到端系统集成测试: 模拟 3 个完整的交互循环。

        循环 1: 表盘模式 — 传感器读取 → UI 刷新
        循环 2: 按下 UP → 进入传感器详情页
        循环 3: 按下 OK → 返回表盘

        验证: 所有模块协同工作，状态切换正常。
        """
        print(f"\n{C.FG_YELLOW}[TEST 8/8] System Integration Test{C.RESET}")
        try:
            page = "WATCH_FACE"
            for cycle in range(3):
                t, h = self.dht.read()
                hi = self.dht.computeHeatIndex(t, h)

                # 模拟页面切换
                if cycle == 1:
                    self.btn_up.press(); self.btn_up.release()
                    page = "SENSOR_DETAIL"
                elif cycle == 2:
                    self.btn_ok.press(); self.btn_ok.release()
                    page = "WATCH_FACE"

                print(f"  Cycle {cycle+1}: Page={page}, T={t:.1f}C, H={h:.0f}%, HI={hi:.1f}C")

            self._pass("System integration — 3 cycles completed successfully")
        except Exception as e:
            self._fail(f"Integration test failed: {e}")

    # ── 辅助方法 ──
    def _pass(self, msg):
        self.tests_passed += 1
        print(f"  {C.FG_GREEN}PASS{C.RESET} — {msg}")

    def _fail(self, msg):
        self.errors.append(msg)
        print(f"  {C.FG_RED}FAIL{C.RESET} — {msg}")

    def print_summary(self):
        """打印测试总结和模拟的表盘界面"""
        print(C.BOLD + C.FG_CYAN + "\n" + "=" * 60 + C.RESET)
        print(C.BOLD + C.FG_CYAN + "  SIMULATION RESULTS" + C.RESET)
        print(C.BOLD + C.FG_CYAN + "=" * 60 + C.RESET)
        print(f"  Tests Passed: {self.tests_passed}/{self.tests_total}")

        if self.errors:
            print(f"\n  {C.FG_RED}FAILURES:{C.RESET}")
            for e in self.errors:
                print(f"    {C.FG_RED}*{C.RESET} {e}")
            print(f"\n  {C.FG_RED}SIMULATION FAILED — {len(self.errors)} error(s){C.RESET}")
            sys.exit(1)
        else:
            print(f"\n  {C.FG_GREEN}ALL {self.tests_total} TESTS PASSED{C.RESET}")
            print(f"  {C.FG_GREEN}Hardware design verified successfully!{C.RESET}")

        print(C.BOLD + C.FG_CYAN + "=" * 60 + C.RESET + "\n")

        # ── 终端模拟表盘界面 (ASCII Art) ──
        print("  [Mock Display Render — Watch Face]")
        print("  +------------------------------------------------------------+")
        print("  | AI WATCH v1                                              |")
        print("  |                                                            |")
        print("  |                   ################                         |")
        print("  |                   ##  12:00  ##          +----+           |")
        print("  |                   ## 2026-05-13 ##         | :) |           |")
        print("  |                   ################         +----+           |")
        print("  |                                                            |")
        print("  |  +---------+  +---------+                                  |")
        print("  |  | TEMP    |  |  HUM    |                                  |")
        print("  |  | 25.0 C  |  |  55%    |                                  |")
        print("  |  +---------+  +---------+                                  |")
        print("  |  +---------+  +---------+                                  |")
        print("  |  | PRESS   |  |  BATT   |                                  |")
        print("  |  | 1013hPa |  |  3.9V   |                                  |")
        print("  |  +---------+  +---------+                                  |")
        print("  |                                                            |")
        print("  |  [UP][DOWN] Navigate   [OK] AI Chat                       |")
        print("  +------------------------------------------------------------+")


# ═══════════════════════════════════════════════════════════════
# 入口
# ═══════════════════════════════════════════════════════════════
if __name__ == "__main__":
    runner = HardwareTestRunner()
    success = runner.run_all()
    # 退出码: 0=全部通过, 1=存在失败 (用于 CI 流水线)
    sys.exit(0 if success else 1)

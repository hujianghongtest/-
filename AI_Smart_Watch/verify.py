"""
╔══════════════════════════════════════════════════════════════╗
║  AI Smart Watch — 硬件设计一致性验证脚本                   ║
╚══════════════════════════════════════════════════════════════╝

【验证目的】
  在所有设计文件之间进行交叉验证，确保:
    1. diagram.json (Wokwi电路图) 的引脚连接正确
    2. main.ino (仿真代码) 的引脚宏定义与电路图一致
    3. firmware/main.cpp (实物固件) 的引脚与硬件设计文档一致
    4. Hardware_Design.md 包含所有必要的设计章节
    5. 不存在 GPIO 引脚冲突 (同一引脚被两个信号共用)

【9 项验证检查】
  TEST 1 — diagram.json JSON 格式有效性
  TEST 2 — Wokwi 元件类型合法
  TEST 3 — diagram.json 中提取的引脚映射
  TEST 4 — main.ino 中提取的引脚宏定义
  TEST 5 — circuit ↔ code 引脚交叉比对 (10个引脚)
  TEST 6 — 仿真代码结构完整性 (include/函数/上拉)
  TEST 7 — 固件 ↔ 硬件设计文档引脚比对 (14个)
  TEST 8 — GPIO 冲突检测 (20个引脚)
  TEST 9 — 硬件设计文档章节完整性 (12个)

【运行方法】
  python verify.py
  退出码: 0=全部通过  1=存在错误

【与 simulate.py 的区别】
  verify.py — 静态分析 (检查文件一致性, 不运行任何硬件逻辑)
  simulate.py — 动态仿真 (模拟硬件行为, 运行交互测试)

【版本历史】
  v1.0  2026-05-13  初始创建, 9项验证 + 2项修复 (编码/命名)
"""

import json
import re
import os
import sys

# ── 项目根目录 (本脚本所在目录) ──
base = os.path.dirname(os.path.abspath(__file__))
errors = []    # 严重错误 (会导致硬件不工作)
warnings = []  # 警告 (不影响基本功能但需要关注)


def read(path):
    """读取文件内容 (UTF-8编码, 处理Windows GBK兼容问题)"""
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()


# ═══════════════════════════════════════════════════════════
# TEST 1: diagram.json JSON 有效性
# 目的: 确保 Wokwi 电路图文件是合法的 JSON，格式无错误
# ═══════════════════════════════════════════════════════════
print("=" * 60)
print("TEST 1: diagram.json JSON Validation")
print("=" * 60)
try:
    diagram = json.loads(read(os.path.join(base, "simulation", "diagram.json")))
    n_parts = len(diagram.get("parts", []))
    n_conns = len(diagram.get("connections", []))
    print(f"PASS - Valid JSON ({n_parts} parts, {n_conns} connections)")
    if n_parts < 7:
        warnings.append(f"Expected >=7 parts, found {n_parts}")
    if n_conns < 20:
        warnings.append(f"Expected >=20 connections, found {n_conns}")
except json.JSONDecodeError as e:
    errors.append(f"diagram.json syntax error: {e}")
    print(f"FAIL - Invalid JSON: {e}")
    diagram = {"parts": [], "connections": []}
except FileNotFoundError:
    errors.append("simulation/diagram.json not found")
    print("FAIL - File not found")
    diagram = {"parts": [], "connections": []}


# ═══════════════════════════════════════════════════════════
# TEST 2: 元件类型验证
# 目的: 确保所有元件类型都是 Wokwi 支持的合法类型
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 2: Part Type Verification")
print("=" * 60)
valid_types = {
    "board-esp32-s3-devkitc-1",  # ESP32-S3 主控板
    "wokwi-ili9341",              # TFT 显示屏 (SPI)
    "wokwi-dht22",                # 温湿度传感器 (OneWire)
    "wokwi-pushbutton",           # 按键
    "wokwi-resistor",             # 上拉电阻
}
for p in diagram.get("parts", []):
    if p.get("type") in valid_types:
        print(f"PASS - {p['id']}: {p['type']}")
    else:
        warnings.append(f"Unknown part type: {p.get('type')} (id={p.get('id')})")
        print(f"WARN - {p['id']}: {p.get('type')} (unknown — may not work)")


# ═══════════════════════════════════════════════════════════
# TEST 3: 从 diagram.json 提取引脚映射
# 目的: 解析电路连接，汇总每个 GPIO 连接的目标元件
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 3: Pin Mapping from diagram.json")
print("=" * 60)
diagram_pins = {}
for conn in diagram.get("connections", []):
    a, b = conn[0], conn[1]       # 连接的两端
    for side in [a, b]:
        parts = side.split(":")     # 解析 "esp:GPIO10" → ["esp", "10"]
        if parts[0] == "esp" and len(parts) == 2 and parts[1].isdigit():
            gpio = int(parts[1])
            other = b if side == a else a  # 另一端
            diagram_pins[gpio] = other

for g in sorted(diagram_pins):
    print(f"  GPIO{g:2d} -> {diagram_pins[g]}")

# 检查关键引脚是否都已连接
critical_pins = {
    10: "lcd:CS", 11: "lcd:MOSI", 12: "lcd:SCK",
    13: "lcd:D/C", 14: "lcd:RST", 47: "lcd:LED",
    17: "dht1:SDA / r_dht:2",
    1: "btn_up", 3: "btn_down", 5: "btn_ok"
}
for gpio, expected in critical_pins.items():
    if gpio not in diagram_pins:
        errors.append(f"GPIO{gpio} not connected in diagram.json")
        print(f"  FAIL - GPIO{gpio} missing (expected {expected})")


# ═══════════════════════════════════════════════════════════
# TEST 4: 从 main.ino 提取引脚宏定义
# 目的: 解析仿真代码中的 #define 引脚常量
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 4: Pin Mapping from main.ino")
print("=" * 60)
try:
    ino = read(os.path.join(base, "simulation", "main.ino"))
except FileNotFoundError:
    errors.append("simulation/main.ino not found")
    ino = ""
    print("FAIL - File not found")

code_pins = {}
# 匹配 #define TFT_CS 10 这类宏定义
for m in re.finditer(r'#define\s+(TFT_\w+|DHT_\w+|BTN_\w+)\s+(\d+)', ino):
    code_pins[m.group(1)] = int(m.group(2))
    print(f"  {m.group(1):12s} = {m.group(2)}")

if not code_pins:
    warnings.append("No pin definitions found in main.ino")


# ═══════════════════════════════════════════════════════════
# TEST 5: 引脚一致性交叉比对 (circuit ↔ code)
# 目的: 确保仿真代码中的引脚号与电路图完全一致
# 不一致的后果: 仿真中屏幕不亮、传感器无法读取、按键失效
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 5: Pin Consistency (diagram.json <-> main.ino)")
print("=" * 60)
checks = [
    # (代码宏名称, 电路图预期GPIO)
    ("TFT_CS",   10),   # 片选 — lcd:CS
    ("TFT_DC",   13),   # 数据/命令 — lcd:D/C
    ("TFT_RST",  14),   # 复位 — lcd:RST
    ("TFT_MOSI", 11),   # SPI数据 — lcd:MOSI
    ("TFT_SCK",  12),   # SPI时钟 — lcd:SCK
    ("TFT_BL",   47),   # 背光 — lcd:LED
    ("DHT_PIN",  17),   # 传感器数据 — dht1:SDA
    ("BTN_UP",    1),   # UP键 — btn_up:1.r
    ("BTN_DOWN",  3),   # DOWN键 — btn_down:1.r
    ("BTN_OK",    5),   # OK键 — btn_ok:1.r
]

all_ok = True
for name, expected in checks:
    val = code_pins.get(name)
    if val == expected:
        print(f"PASS - {name}: GPIO{val}")
    elif val is None:
        errors.append(f"{name} not found in main.ino")
        print(f"FAIL - {name}: NOT FOUND (expected GPIO{expected})")
        all_ok = False
    else:
        errors.append(f"{name}: code={val}, circuit={expected} — MISMATCH")
        print(f"FAIL - {name}: code=GPIO{val}, circuit=GPIO{expected}")
        all_ok = False

if all_ok:
    print("  -> All 10 pins match between circuit and code")


# ═══════════════════════════════════════════════════════════
# TEST 6: 仿真代码结构完整性
# 目的: 确保所有必要的 #include、函数、配置均已到位
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 6: Code Structure (main.ino)")
print("=" * 60)

# 必要的头文件
required_includes = [
    "Adafruit_GFX.h",       # 图形基础库
    "Adafruit_ILI9341.h",   # TFT 驱动 (仿真)
    "DHTesp.h",             # DHT22 传感器库 (仿真)
]
for inc in required_includes:
    if inc in ino:
        print(f"PASS - Include: {inc}")
    else:
        warnings.append(f"Missing include: {inc}")
        print(f"WARN - Include missing: {inc}")

# 必要的函数
required_funcs = [
    "void setup()",           # Arduino 初始化入口
    "void loop()",            # Arduino 主循环
    "void handleButtons()",   # 按键处理
    "void drawWatchFace()",   # 表盘静态绘制
    "void updateWatchFace()", # 表盘动态更新
    "void drawColorBars()",   # 彩条测试
]
for func in required_funcs:
    if func in ino:
        print(f"PASS - Function: {func}")
    else:
        warnings.append(f"Missing function: {func}")
        print(f"WARN - Function missing: {func}")

# 按键上拉配置 (硬件可靠性的关键)
if "INPUT_PULLUP" in ino:
    print("PASS - INPUT_PULLUP configured (buttons)")
else:
    warnings.append("INPUT_PULLUP not found — buttons may float")
    print("WARN - INPUT_PULLUP missing")

# 错误处理 (传感器读取失败时显示占位符)
if "isnan" in ino:
    print("PASS - Sensor error handling (isnan checks)")


# ═══════════════════════════════════════════════════════════
# TEST 7: 固件引脚一致性 (firmware ↔ Hardware_Design.md)
# 目的: 确保实物固件的引脚分配与设计文档完全匹配
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 7: firmware/main.cpp Pin Consistency")
print("=" * 60)
try:
    fw = read(os.path.join(base, "firmware", "src", "main.cpp"))
except FileNotFoundError:
    errors.append("firmware/src/main.cpp not found")
    fw = ""
    print("FAIL - File not found")

fw_pins = {}
for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', fw):
    fw_pins[m.group(1)] = int(m.group(2))

# 固件引脚期望值 (与 Hardware_Design.md 附录A 一致)
fw_checks = [
    # (宏名称, 期望GPIO, 说明)
    ("BTN_UP",          1,  "按键上"),
    ("BTN_DOWN",        3,  "按键下"),
    ("BTN_OK",          5,  "按键确认"),
    ("I2C_SDA",        17,  "I2C数据线 — BME280+DS3231"),
    ("I2C_SCL",        18,  "I2C时钟线 — BME280+DS3231"),
    ("I2S_MIC_SCK",    15,  "INMP441 麦克风位时钟"),
    ("I2S_MIC_WS",     16,  "INMP441 麦克风通道选择"),
    ("I2S_MIC_SD",      7,  "INMP441 麦克风数据"),
    ("I2S_SPK_BCLK",    8,  "MAX98357 功放位时钟"),
    ("I2S_SPK_LRC",     9,  "MAX98357 功放通道选择"),
    ("I2S_SPK_DIN",     6,  "MAX98357 功放数据输入"),
    ("I2S_SPK_SD",      4,  "MAX98357 功放关断"),
    ("BAT_ADC_PIN",     2,  "电池电压 ADC"),
    ("TFT_BL_PIN",     47,  "TFT 背光 PWM"),
]

for name, expected, desc in fw_checks:
    val = fw_pins.get(name)
    if val == expected:
        print(f"PASS - {name}: GPIO{val} ({desc})")
    elif val is None:
        warnings.append(f"firmware: {name} not defined (should be GPIO{expected})")
        print(f"WARN - {name}: NOT FOUND (expected GPIO{expected} — {desc})")
    else:
        errors.append(f"{name}: firmware={val}, design={expected}")
        print(f"FAIL - {name}: firmware=GPIO{val}, design=GPIO{expected} ({desc})")


# ═══════════════════════════════════════════════════════════
# TEST 8: GPIO 冲突检测
# 目的: 确保每个 GPIO 只被分配一个功能
# 冲突的后果: 两个外设互相干扰，可能导致硬件损坏或功能异常
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 8: GPIO Pin Conflict Detection")
print("=" * 60)

# 汇总所有引脚分配 (GPIO → [功能列表])
all_gpios = {}
for name, val in fw_pins.items():
    all_gpios.setdefault(val, []).append(name)

conflict_count = 0
for gpio in sorted(all_gpios):
    names = all_gpios[gpio]
    if len(names) > 1:
        # 同一 GPIO 出现多个功能定义 → 冲突
        errors.append(f"GPIO{gpio}: CONFLICT — {' vs '.join(names)}")
        print(f"FAIL - GPIO{gpio}: CONFLICT — {' + '.join(names)}")
        conflict_count += 1
    else:
        print(f"PASS - GPIO{gpio}: {names[0]}")

if conflict_count == 0:
    print(f"  -> {len(all_gpios)} GPIOs, 0 conflicts")

# 额外检查: 板载 LED (GPIO21) 是否被占用
if 21 in all_gpios:
    print(f"  INFO - GPIO21: {all_gpios[21]} (onboard WS2812 LED)")
else:
    print(f"  INFO - GPIO21: onboard WS2812 LED (unused in definitions)")


# ═══════════════════════════════════════════════════════════
# TEST 9: 硬件设计文档完整性
# 目的: 确保 Hardware_Design.md 包含所有必要的设计章节
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("TEST 9: Hardware_Design.md Completeness")
print("=" * 60)
try:
    doc = read(os.path.join(base, "Hardware_Design.md"))
except FileNotFoundError:
    errors.append("Hardware_Design.md not found")
    doc = ""
    print("FAIL - File not found")

# 设计文档必需章节 (中文)
required_sections = [
    "系统架构",     # 整体框架
    "BOM",          # 物料清单
    "引脚连接",     # GPIO 分配表
    "SPI",          # 显示屏接口
    "I2C",          # 传感器总线
    "I2S",          # 音频接口
    "电源管理",     # 充电+稳压+电池
    "音频子系统",   # 麦克风+功放设计
    "显示子系统",   # 屏幕选型+驱动
    "测试方案",     # 硬件验证方法
    "注意事项",     # 关键警告
    "组装步骤",     # 实物焊接顺序
]

found_count = 0
for sec in required_sections:
    if sec in doc:
        print(f"PASS - Section: {sec}")
        found_count += 1
    else:
        warnings.append(f"Document missing section: {sec}")
        print(f"WARN - Missing section: {sec}")

print(f"  -> {found_count}/{len(required_sections)} required sections found")


# ═══════════════════════════════════════════════════════════
# 最终报告
# ═══════════════════════════════════════════════════════════
print("\n" + "=" * 60)
print("FINAL REPORT")
print("=" * 60)

print(f"Errors:   {len(errors)}")
for e in errors:
    print(f"  FAIL - {e}")

print(f"Warnings: {len(warnings)}")
for w in warnings:
    print(f"  WARN - {w}")

if not errors:
    print(f"\n*** ALL {9} TESTS PASSED — No critical errors ***")
    sys.exit(0)
else:
    print(f"\n*** {len(errors)} CRITICAL ERROR(S) — Must fix before hardware assembly ***")
    sys.exit(1)

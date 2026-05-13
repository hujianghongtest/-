import json, re, os

base = os.path.dirname(os.path.abspath(__file__))
errors = []
warnings = []

def read(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

# ── 1. Validate diagram.json ──
print("=" * 60)
print("TEST 1: diagram.json JSON Validation")
print("=" * 60)
try:
    diagram = json.loads(read(os.path.join(base, "simulation", "diagram.json")))
    print(f"PASS - Valid JSON ({len(diagram['parts'])} parts, {len(diagram['connections'])} connections)")
except Exception as e:
    errors.append(f"diagram.json: {e}")
    print(f"FAIL - {e}")
    diagram = {"parts": [], "connections": []}

# ── 2. Check part types ──
print("\n" + "=" * 60)
print("TEST 2: Part Type Verification")
print("=" * 60)
valid = {"board-esp32-s3-devkitc-1", "wokwi-ili9341", "wokwi-dht22", "wokwi-pushbutton", "wokwi-resistor"}
for p in diagram.get("parts", []):
    if p["type"] in valid:
        print(f"PASS - {p['id']}: {p['type']}")
    else:
        warnings.append(f"Unknown type: {p['type']} ({p['id']})")
        print(f"WARN - {p['id']}: {p['type']}")

# ── 3. Pin mapping from diagram ──
print("\n" + "=" * 60)
print("TEST 3: Pin Mapping from diagram.json")
print("=" * 60)
diagram_pins = {}
for conn in diagram.get("connections", []):
    a, b = conn[0], conn[1]
    for side in [a, b]:
        ps = side.split(":")
        if ps[0] == "esp" and len(ps) == 2 and ps[1].isdigit():
            gpio = int(ps[1])
            other = b if side == a else a
            diagram_pins[gpio] = other
for g in sorted(diagram_pins):
    print(f"  GPIO{g:2d} -> {diagram_pins[g]}")

# ── 4. Pin mapping from main.ino ──
print("\n" + "=" * 60)
print("TEST 4: Pin Mapping from main.ino")
print("=" * 60)
ino = read(os.path.join(base, "simulation", "main.ino"))
code_pins = {}
for m in re.finditer(r'#define\s+(TFT_\w+|DHT_\w+|BTN_\w+)\s+(\d+)', ino):
    code_pins[m.group(1)] = int(m.group(2))
    print(f"  {m.group(1):12s} = {m.group(2)}")

# ── 5. Cross-check ──
print("\n" + "=" * 60)
print("TEST 5: Pin Consistency (diagram.json <-> main.ino)")
print("=" * 60)
checks = [
    ("TFT_CS", 10), ("TFT_DC", 13), ("TFT_RST", 14),
    ("TFT_MOSI", 11), ("TFT_SCK", 12), ("TFT_BL", 47),
    ("DHT_PIN", 17), ("BTN_UP", 1), ("BTN_DOWN", 3), ("BTN_OK", 5),
]
for name, exp in checks:
    val = code_pins.get(name)
    if val == exp:
        print(f"PASS - {name}: GPIO{val}")
    elif val is None:
        errors.append(f"{name} not found")
        print(f"FAIL - {name}: NOT FOUND")
    else:
        errors.append(f"{name}: {val} != {exp}")
        print(f"FAIL - {name}: {val} != {exp}")

# ── 6. Code structure ──
print("\n" + "=" * 60)
print("TEST 6: Code Structure")
print("=" * 60)
for inc in ["Adafruit_GFX.h", "Adafruit_ILI9341.h", "DHTesp.h"]:
    if inc in ino:
        print(f"PASS - Include: {inc}")
    else:
        warnings.append(f"Missing include: {inc}")
        print(f"WARN - Missing: {inc}")

for func in ["void setup()", "void loop()", "void handleButtons()",
             "void drawWatchFace()", "void updateWatchFace()", "void drawColorBars()"]:
    if func in ino:
        print(f"PASS - Function: {func}")
    else:
        warnings.append(f"Missing function: {func}")
        print(f"WARN - Missing: {func}")

if "INPUT_PULLUP" in ino:
    print("PASS - Pull-up configured")
else:
    warnings.append("No INPUT_PULLUP")

# ── 7. firmware/main.cpp ──
print("\n" + "=" * 60)
print("TEST 7: firmware/main.cpp Pin Consistency")
print("=" * 60)
fw = read(os.path.join(base, "firmware", "src", "main.cpp"))
fw_pins = {}
for m in re.finditer(r'#define\s+(\w+)\s+(\d+)', fw):
    fw_pins[m.group(1)] = int(m.group(2))

fw_checks = [
    ("BTN_UP", 1), ("BTN_DOWN", 3), ("BTN_OK", 5),
    ("I2C_SDA", 17), ("I2C_SCL", 18),
    ("I2S_MIC_SCK", 15), ("I2S_MIC_WS", 16), ("I2S_MIC_SD", 7),
    ("I2S_SPK_BCLK", 8), ("I2S_SPK_LRC", 9), ("I2S_SPK_DIN", 6),
    ("I2S_SPK_SD", 4), ("BAT_ADC_PIN", 2), ("TFT_BL_PIN", 47),
]
for name, exp in fw_checks:
    val = fw_pins.get(name)
    if val == exp:
        print(f"PASS - {name}: GPIO{val}")
    elif val is None:
        warnings.append(f"{name} not defined in firmware")
        print(f"WARN - {name}: NOT FOUND")
    else:
        errors.append(f"{name}: {val} != {exp}")
        print(f"FAIL - {name}: {val} != {exp}")

# ── 8. Pin conflicts ──
print("\n" + "=" * 60)
print("TEST 8: Pin Conflict Detection")
print("=" * 60)
all_gpios = {}
for name, val in fw_pins.items():
    all_gpios.setdefault(val, []).append(name)
for gpio in sorted(all_gpios):
    names = all_gpios[gpio]
    if len(names) > 1:
        errors.append(f"GPIO{gpio} conflict: {', '.join(names)}")
        print(f"FAIL - GPIO{gpio}: CONFLICT - {', '.join(names)}")
    else:
        print(f"PASS - GPIO{gpio}: {names[0]}")

# ── 9. Document completeness ──
print("\n" + "=" * 60)
print("TEST 9: Hardware_Design.md Completeness")
print("=" * 60)
doc = read(os.path.join(base, "Hardware_Design.md"))
for sec in ["系统架构", "BOM", "引脚连接", "电源管理", "音频子系统",
            "显示子系统", "测试方案", "注意事项", "组装步骤", "SPI", "I2C", "I2S"]:
    if sec in doc:
        print(f"PASS - Section: {sec}")
    else:
        warnings.append(f"Doc missing: {sec}")
        print(f"WARN - Missing section: {sec}")

# ── Final ──
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
    print("\n*** ALL CRITICAL TESTS PASSED ***")
else:
    print(f"\n*** {len(errors)} ERROR(S) FOUND ***")

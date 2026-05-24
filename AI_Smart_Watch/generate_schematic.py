"""
AI Smart Watch — KiCad v6 原理图 (仅用标准库符号)
所有符号使用 KiCad 官方库符号名，确保 EasyEDA Pro 可导入
"""

import uuid as _uuid
import os, math

OUTPUT = r'C:\Users\7\Desktop\jlc project\ai_smart_watch.kicad_sch'

# ============================================================
def _q(s): return f'"{s}"'

def sexp(*args):
    parts = []
    for a in args:
        if isinstance(a, (list, tuple)):
            parts.append(sexp(*a))
        else:
            parts.append(str(a))
    return '(' + ' '.join(parts) + ')'

def uid(): return _uuid.uuid4().hex

F = '(effects (font (size 1.27 1.27)))'
FH = F + ' hide'

# ============================================================
# 标准符号定义 (KiCad v6 格式)
# ============================================================

def sym_connector(n_pins, lib_name=None):
    """Connector_Generic:Conn_01xNN 标准符号"""
    if lib_name is None:
        lib_name = f'Connector_Generic:Conn_01x{n_pins:02d}'
    h = n_pins * 28 + 20  # pin spacing
    y1 = -(h // 2)
    y2 = h // 2
    x1, x2 = -100, 100

    inner = ['symbol', f'{lib_name}_0_1',
        sexp('rectangle', f'(start {x1} {y1})', f'(end {x2} {y2})',
             '(fill (type background))', '(stroke (width 0) (type default))')
    ]
    step = h / n_pins
    for i in range(n_pins):
        py = int(y2 - (i + 0.5) * step)
        inner.append(
            sexp('pin', 'passive', 'line',
                 f'(at {x1} {py} 180)', '(length 100)',
                 sexp('name', _q(f'Pin_{i+1}'), F),
                 sexp('number', _q(str(i+1)), F)))

    return sexp('symbol', _q(lib_name),
                '(pin_names (offset 1.016))',
                '(in_bom yes)', '(on_board yes)',
                sexp(*inner),
                sexp('property', _q("Reference"), _q("J?"), f'(at 0 {y1 - 30} 0)', F),
                sexp('property', _q("Value"), _q(lib_name), f'(at 0 {y2 + 30} 0)', F),
                sexp('property', _q("Footprint"), _q(""), f'(at 0 {y2 + 50} 0)', FH),
                sexp('property', _q("Datasheet"), _q(""), f'(at 0 {y2 + 70} 0)', FH))


def sym_switch_push():
    """Switch:SW_Push 标准符号"""
    lib_name = 'Switch:SW_Push'
    return sexp('symbol', _q(lib_name),
        '(pin_numbers hide)', '(pin_names (offset 1.016))',
        '(in_bom yes)', '(on_board yes)',
        sexp('symbol', f'{lib_name}_0_1',
            sexp('circle', '(center -40 0)', '(radius 10)',
                 '(fill (type none))', '(stroke (width 0.254) (type default))'),
            sexp('circle', '(center 40 0)', '(radius 10)',
                 '(fill (type none))', '(stroke (width 0.254) (type default))'),
            sexp('polyline',
                 '(pts (xy -30 0) (xy 0 0))',
                 '(stroke (width 0) (type default))'),
            sexp('polyline',
                 '(pts (xy 0 -15) (xy 0 0))',
                 '(stroke (width 0) (type default))'),
            sexp('pin', 'passive', 'line',
                 '(at -90 0 0)', '(length 50)',
                 sexp('name', _q("1"), F),
                 sexp('number', _q("1"), F)),
            sexp('pin', 'passive', 'line',
                 '(at 90 0 180)', '(length 50)',
                 sexp('name', _q("2"), F),
                 sexp('number', _q("2"), F)),
        ),
        sexp('property', _q("Reference"), _q("SW?"), f'(at 0 -50 0)', F),
        sexp('property', _q("Value"), _q("SW_Push"), f'(at 0 50 0)', F),
        sexp('property', _q("Footprint"), _q(""), f'(at 0 70 0)', FH),
        sexp('property', _q("Datasheet"), _q(""), f'(at 0 90 0)', FH))


def sym_resistor():
    """Device:R 标准符号"""
    lib_name = 'Device:R'
    return sexp('symbol', _q(lib_name),
        '(pin_numbers hide)', '(pin_names (offset 0.762))',
        '(in_bom yes)', '(on_board yes)',
        sexp('symbol', f'{lib_name}_0_1',
            sexp('rectangle', '(start -2.54 -1.27)', '(end 2.54 1.27)',
                 '(fill (type background))', '(stroke (width 0.254) (type default))'),
            sexp('pin', 'passive', 'line',
                 '(at -5.08 0 0)', '(length 2.54)',
                 sexp('name', _q("1"), F),
                 sexp('number', _q("1"), F)),
            sexp('pin', 'passive', 'line',
                 '(at 5.08 0 180)', '(length 2.54)',
                 sexp('name', _q("2"), F),
                 sexp('number', _q("2"), F)),
        ),
        sexp('property', _q("Reference"), _q("R?"), f'(at 0 1.27 0)', F),
        sexp('property', _q("Value"), _q("R"), f'(at 0 -1.27 0)', F),
        sexp('property', _q("Footprint"), _q(""), f'(at 0 0 0)', FH),
        sexp('property', _q("Datasheet"), _q(""), f'(at 0 0 0)', FH))


def sym_battery():
    """Device:Battery_Cell 标准符号"""
    lib_name = 'Device:Battery_Cell'
    return sexp('symbol', _q(lib_name),
        '(pin_names (offset 0))', '(in_bom yes)', '(on_board yes)',
        sexp('symbol', f'{lib_name}_0_1',
            sexp('polyline',
                 '(pts (xy -2.54 0) (xy -2.54 -2.54) (xy -2.54 2.54))',
                 '(stroke (width 0.254) (type default))'),
            sexp('polyline',
                 '(pts (xy 0 -1.27) (xy 0 -3.81) (xy 0 1.27))',
                 '(stroke (width 0.254) (type default))'),
            sexp('polyline',
                 '(pts (xy 2.54 0) (xy 2.54 -2.54) (xy 2.54 2.54))',
                 '(stroke (width 0.254) (type default))'),
            sexp('pin', 'passive', 'line',
                 '(at -5.08 0 0)', '(length 2.54)',
                 sexp('name', _q("+"), F),
                 sexp('number', _q("1"), F)),
            sexp('pin', 'passive', 'line',
                 '(at 5.08 0 180)', '(length 2.54)',
                 sexp('name', _q("-"), F),
                 sexp('number', _q("2"), F)),
        ),
        sexp('property', _q("Reference"), _q("BT?"), f'(at 0 4 0)', F),
        sexp('property', _q("Value"), _q("Battery_Cell"), f'(at 0 -4 0)', F),
        sexp('property', _q("Footprint"), _q(""), f'(at 0 0 0)', FH),
        sexp('property', _q("Datasheet"), _q(""), f'(at 0 0 0)', FH))


def sym_regulator():
    """Regulator_Linear:AMS1117-3.3 标准符号"""
    lib_name = 'Regulator_Linear:AMS1117-3.3'
    return sexp('symbol', _q(lib_name),
        '(pin_names (offset 0))', '(in_bom yes)', '(on_board yes)',
        sexp('symbol', f'{lib_name}_0_1',
            sexp('rectangle', '(start -5.08 -7.62)', '(end 5.08 7.62)',
                 '(fill (type background))', '(stroke (width 0.254) (type default))'),
            sexp('pin', 'power_in', 'line',
                 '(at -7.62 5.08 0)', '(length 2.54)',
                 sexp('name', _q("VI"), F),
                 sexp('number', _q("3"), F)),
            sexp('pin', 'power_out', 'line',
                 '(at 7.62 0 180)', '(length 2.54)',
                 sexp('name', _q("VO"), F),
                 sexp('number', _q("2"), F)),
            sexp('pin', 'power_in', 'line',
                 '(at 0 -10.16 90)', '(length 2.54)',
                 sexp('name', _q("GND"), F),
                 sexp('number', _q("1"), F)),
        ),
        sexp('property', _q("Reference"), _q("U?"), f'(at 0 10 0)', F),
        sexp('property', _q("Value"), _q("AMS1117-3.3"), f'(at 0 -11 0)', F),
        sexp('property', _q("Footprint"), _q(""), f'(at 0 0 0)', FH),
        sexp('property', _q("Datasheet"), _q(""), f'(at 0 0 0)', FH))


def sym_power(net_name):
    """Power 符号: power:GND, power:+3.3V, power:VBAT 等"""
    lib_name = f'power:{net_name}'
    inner_name = net_name.replace('.', '_').replace('+', '_')

    if net_name == 'GND':
        shape = sexp('polyline',
                     '(pts (xy 0 0) (xy 1.27 0) (xy 0.635 -1.27) (xy 0 -2.54) (xy -0.635 -1.27) (xy -1.27 0))',
                     '(stroke (width 0.254) (type default))')
    else:
        # VCC/VBAT: bar shape
        shape = sexp('polyline',
                     '(pts (xy -1.27 1.27) (xy 1.27 1.27))',
                     '(stroke (width 0) (type default))') + \
                sexp('polyline',
                     '(pts (xy 0 1.27) (xy 0 -1.27))',
                     '(stroke (width 0) (type default))')

    return sexp('symbol', _q(lib_name),
        '(power)', '(pin_names (offset 0) hide)', '(in_bom no)', '(on_board yes)',
        sexp('symbol', f'{lib_name}_0_1',
            shape,
            sexp('pin', 'power_in', 'line',
                 '(at 0 -2.54 90)', '(length 2.54)',
                 sexp('name', _q(net_name), F),
                 sexp('number', _q("1"), F)),
        ),
        sexp('property', _q("Reference"), _q("#PWR?"), f'(at 0 -5.08 0)', F),
        sexp('property', _q("Value"), _q(net_name), f'(at 0 -7.62 0)', F),
        sexp('property', _q("Footprint"), _q(""), f'(at 0 0 0)', FH),
        sexp('property', _q("Datasheet"), _q(""), f'(at 0 0 0)', FH))


# ============================================================
# 原理图布局
# ============================================================

def generate():
    out = []

    # Header
    out.append(sexp('kicad_sch', '(version 20211014)', '(generator "eeschema")'))
    out.append(sexp('paper', '"A4"'))
    out.append(sexp('title_block',
        f'(title "AI Smart Watch Schematic")',
        f'(date "2026-05-19")', f'(rev "1.0")',
        f'(company "")', f'(comment 1 "ESP32-S3-Zero AI Smart Watch — Standard Symbols")'))

    # ========== lib_symbols ==========
    lib = ['lib_symbols']

    # 连接器符号
    for n in [18, 10, 9, 6, 8, 4, 2]:
        lib.append(sym_connector(n))

    # 开关
    lib.append(sym_switch_push())

    # 电阻
    lib.append(sym_resistor())

    # 电池
    lib.append(sym_battery())

    # 稳压器
    lib.append(sym_regulator())

    # 电源
    for pwr in ['GND', '+3.3V', 'VBAT']:
        lib.append(sym_power(pwr))

    out.append(sexp(*lib))

    # ========== 页面 ==========
    sheet = ['sheet', '(at 0 0)', '(size 11693 8268)', '(fields_autoplaced)']

    def add_inst(ref, sym_name, x, y, value=None):
        """添加符号实例"""
        if value is None:
            value = sym_name.split(':')[-1]
        return sexp('symbol', f'(lib_id "{sym_name}")', f'(at {x} {y} 0)',
            '(unit 1)', '(in_bom yes)', '(on_board yes)',
            sexp('property', _q("Reference"), _q(ref), f'(at {x} {y - 60} 0)', F),
            sexp('property', _q("Value"), _q(value), f'(at {x} {y + 60} 0)', F),
            sexp('property', _q("Footprint"), _q(""), f'(at {x} {y + 90} 0)', FH),
            sexp('property', _q("Datasheet"), _q(""), f'(at {x} {y + 120} 0)', FH))

    def add_pwr(ref, net, x, y):
        """添加电源符号实例"""
        sym = f'power:{net}'
        return sexp('symbol', f'(lib_id "{sym}")', f'(at {x} {y} 0)',
            '(unit 1)', '(in_bom no)', '(on_board yes)',
            sexp('property', _q("Reference"), _q(ref), f'(at {x} {y - 30} 0)', F),
            sexp('property', _q("Value"), _q(net), f'(at {x} {y + 30} 0)', F),
            sexp('property', _q("Footprint"), _q(""), f'(at {x} {y + 50} 0)', FH),
            sexp('property', _q("Datasheet"), _q(""), f'(at {x} {y + 70} 0)', FH))

    def add_wire(x1, y1, x2, y2):
        return sexp('wire', f'(pts (xy {x1} {y1}) (xy {x2} {y2}))',
            '(stroke (width 0) (type default))', f'(uuid "{uid()}")')

    def add_label(text, x, y):
        return sexp('label', _q(text), f'(at {x} {y} 0)', F, f'(uuid "{uid()}")')

    def add_global(text, x, y):
        return sexp('global_label', _q(text), f'(at {x} {y} 0)', F, f'(uuid "{uid()}")')

    def add_text(text, x, y, sz=1.5):
        return sexp('text', _q(text), f'(at {x} {y} 0)', f'(effects (font (size {sz} {sz})))', f'(uuid "{uid()}")')

    # ==================== 元件放置 ====================

    # ESP32-S3 左侧 GPIOs (Conn_01x18)
    sheet.append(add_inst('U1A', 'Connector_Generic:Conn_01x18', 5000, 4200, 'ESP32-S3_Left'))

    # ESP32-S3 右侧 GPIOs (Conn_01x10)
    sheet.append(add_inst('U1B', 'Connector_Generic:Conn_01x10', 5600, 4200, 'ESP32-S3_Right'))

    # TFT Display (Conn_01x09)
    sheet.append(add_inst('J1', 'Connector_Generic:Conn_01x09', 1200, 2000, 'TFT_ST7789'))

    # BME280 (Conn_01x06)
    sheet.append(add_inst('U2', 'Connector_Generic:Conn_01x06', 8600, 1400, 'BME280'))

    # DS3231 (Conn_01x06)
    sheet.append(add_inst('U3', 'Connector_Generic:Conn_01x06', 8600, 3200, 'DS3231'))

    # INMP441 (Conn_01x06)
    sheet.append(add_inst('U4', 'Connector_Generic:Conn_01x06', 1200, 5800, 'INMP441'))

    # MAX98357 (Conn_01x08)
    sheet.append(add_inst('U5', 'Connector_Generic:Conn_01x08', 3800, 6000, 'MAX98357'))

    # Buttons ×3 (Switch:SW_Push)
    sheet.append(add_inst('SW1', 'Switch:SW_Push', 2000, 7600, 'BTN_UP'))
    sheet.append(add_inst('SW2', 'Switch:SW_Push', 3200, 7600, 'BTN_DOWN'))
    sheet.append(add_inst('SW3', 'Switch:SW_Push', 4400, 7600, 'BTN_OK'))

    # Battery (Device:Battery_Cell)
    sheet.append(add_inst('BT1', 'Device:Battery_Cell', 8800, 5600, 'LiPo_3.7V'))

    # TP4056 (Conn_01x04)
    sheet.append(add_inst('U6', 'Connector_Generic:Conn_01x04', 8600, 7000, 'TP4056'))

    # AMS1117-3.3 (Regulator_Linear)
    sheet.append(add_inst('U7', 'Regulator_Linear:AMS1117-3.3', 6400, 7000, 'AMS1117-3.3'))

    # Speaker (Conn_01x02)
    sheet.append(add_inst('SP1', 'Connector_Generic:Conn_01x02', 1800, 6000, 'Speaker_8R'))

    # Resistors ×2 (Device:R)
    sheet.append(add_inst('R1', 'Device:R', 6400, 5400, '100k'))
    sheet.append(add_inst('R2', 'Device:R', 6800, 5400, '100k'))

    # Power symbols
    sheet.append(add_pwr('#PWR1', 'GND', 3000, 7950))
    sheet.append(add_pwr('#PWR2', 'GND', 5600, 4800))
    sheet.append(add_pwr('#PWR3', 'GND', 8800, 7300))
    sheet.append(add_pwr('#PWR4', 'GND', 6400, 7300))
    sheet.append(add_pwr('#PWR5', '+3.3V', 5600, 3800))
    sheet.append(add_pwr('#PWR6', '+3.3V', 6400, 6500))
    sheet.append(add_pwr('#PWR7', 'VBAT', 6400, 6200))

    # ==================== 导线 ====================
    wires = [
        # SPI: J1 → U1A
        (1400, 1720, 4800, 1720),   # Pin_1=VCC → +3.3V (handled by labels)
        (1400, 1780, 4800, 1780),   # Pin_2=GND
        (1400, 1840, 4800, 1840),   # Pin_3=CS
        (1400, 1900, 4800, 1900),   # Pin_4=DC
        (1400, 1960, 4800, 1960),   # Pin_5=RST
        (1400, 2020, 4800, 2020),   # Pin_6=MOSI
        (1400, 2080, 4800, 2080),   # Pin_7=SCLK
        (1400, 2140, 4800, 2140),   # Pin_8=BL
        (1400, 2200, 4800, 2200),   # Pin_9=MISO

        # I2C: U2 → U1B, U3 → I2C bus
        (8400, 1580, 5800, 1580),   # U2 SDA → U1B
        (8400, 1520, 5800, 1520),   # U2 SCL → U1B
        (8400, 3380, 8400, 1580),   # U3 SDA vert
        (8400, 3320, 8400, 1520),   # U3 SCL vert

        # I2S Mic: U4 → U1A
        (1400, 5980, 4800, 5980),   # SCK
        (1400, 6040, 4800, 6040),   # WS
        (1400, 6100, 4800, 6100),   # SD

        # I2S Amp: U5 → U1A
        (4000, 6125, 4800, 6125),   # BCLK
        (4000, 6180, 4800, 6180),   # LRC
        (4000, 6240, 4800, 6240),   # DIN
        (4000, 6300, 4800, 6300),   # SD

        # Speaker: SP1 → U5
        (2000, 6050, 3600, 6050),   # SP1+ → U5 OUT+

        # Buttons: SWx → U1A, SWx → GND
        (2200, 7650, 4800, 7650),   # SW1 → GPIO
        (3400, 7650, 4800, 7650),   # SW2 → GPIO
        (4600, 7650, 4800, 7650),   # SW3 → GPIO

        # Battery → TP4056
        (8800, 5750, 8800, 6850),   # BAT+ down

        # TP4056 → LDO
        (8800, 7100, 6600, 7100),   # BAT+ → VIN

        # LDO VOUT → 3.3V
        (6600, 7000, 6600, 6500),   # VO vertical

        # 3.3V to MCU
        (6600, 6500, 5600, 6500),   # 3.3V bus to U1B
        (5600, 6500, 5600, 4400),   # 3.3V bus to U1B area

        # Resistor divider: BAT+ → R1 → R2 → GND
        (8800, 5700, 6200, 5700),   # BAT+ to R1
        (6200, 5700, 6200, 5500),   # BAT+ to R1 pin1
        (6600, 5400, 6200, 5400),   # ADC tap
        (6600, 5400, 6600, 5600),   # ADC to R2
        (6800, 5490, 6800, 5550),   # R2 to GND
    ]
    for w in wires:
        sheet.append(add_wire(*w))

    # ==================== 标签 ====================
    labels = [
        # SPI bus
        ("SPI_CS/GPIO10", 3000, 1840), ("SPI_DC/GPIO13", 3000, 1900),
        ("SPI_RST/GPIO14", 3000, 1960), ("SPI_MOSI/GPIO11", 3000, 2020),
        ("SPI_SCLK/GPIO12", 3000, 2080), ("TFT_BL/GPIO4", 3000, 2140),  // v1.2: GPIO47→GPIO4
        ("TFT_MISO", 3000, 2200),

        # I2C
        ("I2C_SDA/GPIO17", 7000, 1580), ("I2C_SCL/GPIO18", 7000, 1520),

        # I2S Mic
        ("I2S_MIC_SCK/GPIO15", 3000, 5980), ("I2S_MIC_WS/GPIO16", 3000, 6040),
        ("I2S_MIC_SD/GPIO7", 3000, 6100),

        # I2S Amp
        ("I2S_BCLK/GPIO8", 4400, 6125), ("I2S_LRC/GPIO9", 4400, 6180),
        ("I2S_DIN/GPIO6", 4400, 6240), ("SPK_SD/GPIO4", 4400, 6300),

        # Buttons
        ("BTN_UP/GPIO1", 3500, 7650), ("BTN_DOWN/GPIO3", 3500, 7650),
        ("BTN_OK/GPIO5", 3500, 7650),

        # Power
        ("VBAT", 7500, 5700), ("VCC_3V3", 6000, 6500),
        ("BAT_ADC/GPIO2", 6400, 5400),
        ("SPK_OUT+", 3000, 6050),
    ]
    for lbl in labels:
        sheet.append(add_label(*lbl))

    # Global power labels
    sheet.append(add_global('+3.3V', 5600, 6500))
    sheet.append(add_global('GND', 5600, 4800))

    # ==================== 文字注释 ====================
    texts = [
        ("AI Smart Watch — Hardware Schematic v2.0", 5000, 300),
        ("ESP32-S3-Zero + ST7789/GC9A01 + BME280(0x76) + DS3231(0x68) + INMP441 + MAX98357", 5000, 450),
        ("SPI Display Bus", 3000, 1580),
        ("I2C Sensor Bus (shared: SDA=GPIO17, SCL=GPIO18)", 7000, 1300),
        ("I2S Microphone (SCK=GPIO15, WS=GPIO16, SD=GPIO7)", 3000, 5800),
        ("I2S Amplifier (BCLK=GPIO8, LRC=GPIO9, DIN=GPIO6, SD=GPIO4)", 4500, 5950),
        ("User Buttons (UP=GPIO1, DOWN=GPIO3, OK=GPIO5)", 3200, 7450),
        ("Battery + Charger + LDO (3.7V -> TP4056 -> AMS1117-3.3 -> 3.3V)", 7500, 6500),
        ("Battery ADC Divider (100k+100k -> GPIO2)", 6400, 5250),
        ("Rev 2.0 | 2026-05-19 | Sheet 1/1", 5000, 8000),
    ]
    for t in texts:
        sheet.append(add_text(*t))

    # ==================== MCU 引脚功能标注 ====================
    # U1A (Conn_01x18) — 左侧 GPIOs: Pin_1=BTN_UP, Pin_2=BAT_ADC, ...
    u1a_pins = [
        (1, "GPIO1/BTN_UP"), (2, "GPIO2/BAT_ADC"), (3, "GPIO3/BTN_DOWN"),
        (4, "GPIO4/SPK_SD"), (5, "GPIO5/BTN_OK"), (6, "GPIO6/I2S_DIN"),
        (7, "GPIO7/I2S_SD"), (8, "GPIO8/I2S_BCLK"), (9, "GPIO9/I2S_LRC"),
        (10, "GPIO10/TFT_CS"), (11, "GPIO11/TFT_MOSI"), (12, "GPIO12/TFT_SCLK"),
        (13, "GPIO13/TFT_DC"), (14, "GPIO14/TFT_RST"), (15, "GPIO15/I2S_SCK"),
        (16, "GPIO16/I2S_WS"), (17, "GND"), (18, "3V3"),
    ]
    u1a_y_start = 4460
    u1a_y_step = 26
    for i, (pin_num, pin_func) in enumerate(u1a_pins):
        py = u1a_y_start - i * u1a_y_step
        sheet.append(add_text(pin_func, 4600, py, 1.0))

    # U1B (Conn_01x10) — 右侧 GPIOs
    u1b_pins = [
        (1, "GPIO17/I2C_SDA"), (2, "GPIO18/I2C_SCL"), (3, "GPIO21/WS2812"),
        (4, "GPIO4/TFT_BL"), (5, "5V_IN"), (6, "EN/RESET"),  // v1.2: TFT_BL GPIO47→GPIO4
        (7, "IO0/BOOT"), (8, "IO46"), (9, "USB_D+"), (10, "USB_D-"),
    ]
    u1b_y_start = 4330
    u1b_y_step = 23
    for i, (pin_num, pin_func) in enumerate(u1b_pins):
        py = u1b_y_start - i * u1b_y_step
        sheet.append(add_text(pin_func, 5800, py, 1.0))

    # J1 TFT pin labels
    j1_pins = [(1, "VCC"), (2, "GND"), (3, "CS"), (4, "DC"), (5, "RST"),
               (6, "MOSI"), (7, "SCLK"), (8, "BL"), (9, "MISO")]
    j1_y_start = 2150
    j1_y_step = 24
    for i, (pin_num, pin_func) in enumerate(j1_pins):
        py = j1_y_start - i * j1_y_step
        sheet.append(add_text(pin_func, 800, py, 1.0))

    out.append(sexp(*sheet))
    return '\n'.join(out)


if __name__ == '__main__':
    content = generate()
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, 'w', encoding='utf-8') as f:
        f.write(content)
    print(f"Generated: {OUTPUT}")
    print(f"Size: {len(content)} bytes")
    print("All symbols use standard KiCad v6 library names — EasyEDA Pro compatible")

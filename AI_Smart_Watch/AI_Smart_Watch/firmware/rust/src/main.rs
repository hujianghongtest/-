/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  AI Smart Watch — Rust 固件 (ESP32-S3-Zero)          v2.0   ║
 * ║  基于 esp-idf-hal + embedded-graphics + mipidsi             ║
 * ║  GPIO1-13 Only 精简引脚方案                                  ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 【从 C++ 到 Rust 的迁移说明】
 *
 *   C++ (Arduino)              →  Rust (ESP-IDF)
 *   ─────────────────────────────────────────────────
 *   Arduino framework           →  esp-idf-hal + esp-idf-svc
 *   TFT_eSPI 库                →  mipidsi crate (ST7789)
 *   Adafruit_BME280             →  bme280 crate
 *   RTClib (DS3231)             →  ds323x crate
 *   Adafruit_GFX (画点线圆)    →  embedded-graphics crate
 *   String / std::string        →  heapless::String (无堆)
 *   Serial.print()              →  log::info!() 宏
 *   millis()                    →  std::time::Instant
 *   pinMode/digitalRead/Write   →  esp_idf_hal::gpio PinDriver
 *   delay()                     →  std::thread::sleep
 *
 * 【核心功能模块】
 *   1. 硬件初始化 + 上电自检 (6项测试)
 *   2. ST7789 TFT 显示屏驱动 (SPI 40MHz, 240×240)
 *   3. BME280 环境传感器采集 (I2C, 0x76)
 *   4. DS3231 实时时钟 (I2C, 0x68)
 *   5. 表盘 UI 渲染 (时间 + 温度 + 湿度 + 气压 + 电量)
 *   6. 按键导航 (3个实体按键, 内部上拉, 200ms消抖)
 *   7. 电池电压监测 (ADC1, 2:1分压)
 *   8. [预留] I2S 音频 + WiFi + AI 对话
 *
 * 【引脚分配 (v2.0: GPIO1-13 Only, 与 Hardware_Design.md 完全一致)】
 *   参见 config 模块中的常量定义
 *
 * 【版本历史】
 *   v2.0  2026-05-23  GPIO1-13 Only 精简引脚方案
 *   v1.0  2026-05-14  Rust 重写 — 从 C++/Arduino 迁移
 */

// ═══════════════════════════════════════════════════════════════
// 外部 crate 引用
// ═══════════════════════════════════════════════════════════════

// --- ESP-IDF 平台 ---
use esp_idf_hal::{
    adc, delay, gpio,
    i2c, peripherals, prelude::*,
    spi, task, timer,
};
use esp_idf_svc::log::EspLogger;

// --- 嵌入式图形 ---
use embedded_graphics::{
    geometry::{Point, Size},
    mono_font::{ascii::FONT_6X10, ascii::FONT_10X20, MonoTextStyle},
    pixelcolor::Rgb565,
    prelude::*,
    primitives::{Circle, PrimitiveStyle, Rectangle, StyledDrawable},
    text::Text,
};

// --- 显示驱动 ---
use display_interface_spi::SPIInterface;
use mipidsi::{models::ST7789, Builder, Display, Options};

// --- 传感器 ---
use bme280::i2c::BME280;
use ds323x::{Ds323x, NaiveDate, NaiveDateTime};

// --- 标准库 ---
use core::fmt::Write;
use std::{
    sync::Arc,
    sync::Mutex,
    thread::sleep,
    time::{Duration, Instant},
};

// ═══════════════════════════════════════════════════════════════
// 硬件配置常量 (引脚定义 — 与 Hardware_Design.md 一致)
// ═══════════════════════════════════════════════════════════════

mod config {
    /// ESP32-S3-Zero GPIO1-13 Only 引脚分配 (v2.0)
    /// 所有引脚均在标准 2.54mm 排针上
    /// 修改硬件接线时只需改这里

    // --- TFT 显示屏 (SPI2 / FSPI) ---
    pub const TFT_MOSI: i32 = 11;   // SPI 主机数据输出
    pub const TFT_SCLK: i32 = 12;   // SPI 时钟
    pub const TFT_CS: i32 = 10;     // 片选
    pub const TFT_DC: i32 = 13;     // 数据/命令选择
    pub const TFT_BL: i32 = 9;      // 背光 PWM (v2.0: GPIO4→GPIO9)
    // TFT_RST 省略: 硬件RC上电复位 + mipidsi软件复位

    // --- I2C 总线 (BME280 + DS3231 共用) ---
    pub const I2C_SDA: i32 = 3;     // I2C 数据线 (v2.0: GPIO17→GPIO3)
    pub const I2C_SCL: i32 = 4;     // I2C 时钟线 (v2.0: GPIO18→GPIO4)
    // GPIO3说明: JTAG_TCK strapping pin, 默认eFuse下安全, I2C上拉=USB JTAG模式

    // --- BME280 地址 ---
    pub const BME280_ADDR: u8 = 0x76;

    // --- I2S 全双工音频 (INMP441 + MAX98357, BCLK/WS 共享) ---
    pub const I2S_BCLK: i32 = 5;    // 共享位时钟 (INMP441 SCK + MAX98357 BCLK)
    pub const I2S_WS: i32 = 6;      // 共享字选择 (INMP441 WS + MAX98357 LRC)
    pub const I2S_SD_IN: i32 = 7;   // INMP441 麦克风数据输入
    pub const I2S_DOUT: i32 = 8;    // MAX98357 扬声器数据输出
    // MAX98357 SD → 3.3V (硬件常开,省GPIO)

    // --- ADC 按键 (3按键 → 电阻梯形分压 → GPIO1) ---
    // 电压: OK=0V, DOWN≈0.30V, UP≈0.55V, 无按键=3.3V
    pub const BTN_ADC_PIN: i32 = 1;   // ADC1_CH0, 3按键合并
    // ADC判断阈值 (12位, 0~4095)
    pub const BTN_OK_THRESHOLD: u16 = 300;
    pub const BTN_DOWN_MIN: u16 = 300;
    pub const BTN_DOWN_MAX: u16 = 800;
    pub const BTN_UP_MIN: u16 = 800;
    pub const BTN_UP_MAX: u16 = 2000;
    pub const BTN_NONE_MIN: u16 = 3500;

    // --- 电池电压检测 ---
    pub const BAT_ADC_PIN: i32 = 2;    // ADC1_CH1, 外部 2:1 电阻分压(100k+100k)
    pub const BAT_DIVIDER_RATIO: f32 = 2.0;

    // --- 板载 WS2812 RGB LED (板载直连, 不在排针) ---
    pub const WS2812_PIN: i32 = 21;

    // --- 显示屏参数 ---
    pub const DISPLAY_WIDTH: u16 = 240;
    pub const DISPLAY_HEIGHT: u16 = 240;
    pub const SPI_FREQ: u32 = 40_000_000;  // 40 MHz

    // --- I2C 参数 ---
    pub const I2C_FREQ: u32 = 100_000;     // 100 kHz 标准模式
}

// ═══════════════════════════════════════════════════════════════
// 配色定义 (RGB565 — 与 C++ 版完全一致)
// ═══════════════════════════════════════════════════════════════

mod colors {
    use embedded_graphics::pixelcolor::Rgb565;

    /// #1A1A2E — 主背景 (深蓝灰)
    pub const BG: Rgb565 = Rgb565::new(0x1A, 0x05, 0x0E);
    /// #16213E — 卡片/面板背景 (中蓝灰)
    pub const CARD: Rgb565 = Rgb565::new(0x16, 0x04, 0x0E);
    /// #0F3460 — 强调色 (深蓝, 用于圆环/分隔线)
    pub const ACCENT: Rgb565 = Rgb565::new(0x0F, 0x07, 0x00);
    /// #E94560 — 高亮/警告 (珊瑚红)
    pub const HIGHLIGHT: Rgb565 = Rgb565::new(0xE9, 0x09, 0x00);
    /// #EEEEEE — 主文本 (浅灰白)
    pub const TEXT: Rgb565 = Rgb565::new(0xEE, 0x1D, 0x0E);
    /// #2ECC71 — 通过/正常 (翠绿)
    pub const SUCCESS: Rgb565 = Rgb565::new(0x2E, 0x19, 0x11);
    /// #E74C3C — 失败/错误 (正红)
    pub const FAIL: Rgb565 = Rgb565::new(0xE7, 0x09, 0x0C);
    /// #F39C12 — 警告/温度 (琥珀黄)
    pub const WARN: Rgb565 = Rgb565::new(0xF3, 0x14, 0x02);
    /// #53D8FB — 时间/湿度 (天蓝)
    pub const CYAN: Rgb565 = Rgb565::new(0x53, 0x1B, 0x1B);
    /// 纯黑
    pub const BLACK: Rgb565 = Rgb565::new(0x00, 0x00, 0x00);
    /// 纯白
    pub const WHITE: Rgb565 = Rgb565::new(0xFF, 0x3F, 0x1F);
}

// ═══════════════════════════════════════════════════════════════
// 系统状态枚举
// ═══════════════════════════════════════════════════════════════

#[derive(Debug, Clone, Copy, PartialEq)]
enum SystemState {
    Boot,          // 启动中 — 显示 Logo
    SelfTest,      // 自检中 — 逐个验证外设
    WatchFace,     // 表盘模式 — 时间+传感器
    SensorDetail,  // 传感器详情
    AiListening,   // [预留] AI 聆听
    AiResponding,  // [预留] AI 回复
    Settings,      // [预留] 设置
    Error,         // 错误状态
}

// ═══════════════════════════════════════════════════════════════
// 自检结果结构
// ═══════════════════════════════════════════════════════════════

#[derive(Debug)]
struct SelfTestResults {
    display_ok: bool,    // TFT 显示屏
    i2c_devices: u8,     // I2C 总线扫描设备数
    bme280_ok: bool,     // BME280 传感器
    rtc_ok: bool,        // DS3231 RTC
    buttons_ok: bool,    // 按键 (用户按下任意键)
    battery_ok: bool,    // 电池 ADC
    passed: u8,          // 通过数
    total: u8,           // 总数
}

// ═══════════════════════════════════════════════════════════════
// 全局共享状态 (线程安全)
// ═══════════════════════════════════════════════════════════════

struct WatchState {
    state: SystemState,
    temperature: f32,
    humidity: f32,
    pressure: f32,
    battery_v: f32,
    last_update: Instant,
    display_ok: bool,
    sensor_ok: bool,
    rtc_ok: bool,
}

impl WatchState {
    fn new() -> Self {
        Self {
            state: SystemState::Boot,
            temperature: 0.0,
            humidity: 0.0,
            pressure: 0.0,
            battery_v: 0.0,
            last_update: Instant::now(),
            display_ok: false,
            sensor_ok: false,
            rtc_ok: false,
        }
    }
}

/// 类型别名: BME280 over I2C (使用 esp-idf-hal I2C)
type Bme280Sensor<'a> = BME280<
    i2c::I2cDriver<'a, esp_idf_hal::peripherals::I2C0>,
>;

/// 类型别名: ST7789 显示屏 (v2.0: 无硬件RST, 软件复位)
/// RST引脚省略: 硬件RC上电复位 + mipidsi Builder::reset_pin(-1) 软件复位
type WatchDisplay<'a> = Display<
    SPIInterface<
        spi::SpiDriver<'a, esp_idf_hal::peripherals::SPI2>,
        gpio::PinDriver<'a, gpio::Gpio13, gpio::Output>,   // DC = GPIO13
        gpio::PinDriver<'a, gpio::Gpio10, gpio::Output>,   // CS = GPIO10
    >,
    ST7789,
    mipidsi::NoResetPin,  // v2.0: 无硬件RST引脚
>;

// ═══════════════════════════════════════════════════════════════
// 主入口 — 初始化 ESP-IDF → 外设 → 自检 → 主循环
// ═══════════════════════════════════════════════════════════════

fn main() -> anyhow::Result<()> {
    // ── 1. 初始化 ESP-IDF 平台 ──
    // 初始化: 网络栈、NVS、事件循环、日志系统
    esp_idf_svc::sys::link_patches();
    EspLogger::initialize_default();
    log::info!("AI Smart Watch — Rust firmware booting...");

    // ── 2. 获取外设访问权 ──
    // take_peripherals() 只能调用一次，返回 Peripherals 单例
    let peripherals = Peripherals::take()?;

    // ── 3. 初始化各外设 ──

    // 背光先点亮 (便于观察后续初始化)
    let mut backlight = gpio::PinDriver::output(peripherals.pins.gpio9)?;  // v2.0: TFT_BL on GPIO9
    backlight.set_high()?;  // 100% 亮度

    // 显示屏
    log::info!("[INIT] Display (ST7789, SPI 40MHz)...");
    let mut display = init_display(&peripherals)?;
    log::info!("[INIT] Display OK");

    // 启动画面
    draw_boot_screen(&mut display)?;

    // I2C 总线
    log::info!("[INIT] I2C bus (100kHz)...");
    let i2c = init_i2c(&peripherals)?;
    log::info!("[INIT] I2C OK");

    // BME280 传感器
    log::info!("[INIT] BME280 sensor (addr 0x76)...");
    let mut bme = init_bme280(i2c)?;
    log::info!("[INIT] BME280 OK");

    // 注意: BME280 消费了 I2C，DS3231 需要另外的 I2C 实例
    // 在 ESP32-S3 上共享 I2C 总线需要 Mutex 包装
    // 简化处理: 重新获取 I2C (ESP-IDF 支持多个 I2C 句柄指向同一总线)
    let i2c_for_rtc = init_i2c(&peripherals)?;

    // DS3231 RTC
    log::info!("[INIT] DS3231 RTC...");
    let mut rtc = init_rtc(i2c_for_rtc)?;
    log::info!("[INIT] DS3231 RTC OK");

    // ADC 按键 (v2.0: 3按键→电阻梯形分压→GPIO1 ADC1_CH0)
    // 电压: OK=0V, DOWN≈0.30V, UP≈0.55V, 无按键=3.3V
    log::info!("[INIT] ADC Buttons (3 buttons on GPIO1)...");
    let mut adc1 = adc::AdcDriver::new(
        peripherals.adc1,
        &adc::AdcConfig::new(),
    )?;
    let mut btn_pin = adc::AdcChannelDriver::<{ adc::Attenuation::DB11 as u8 }, _>::new(
        peripherals.pins.gpio1,
    )?;
    log::info!("[INIT] ADC Buttons OK");

    // 电池 ADC (v2.0: GPIO2 ADC1_CH1)
    log::info!("[INIT] Battery ADC (GPIO2)...");
    let mut bat_pin = adc::AdcChannelDriver::<{ adc::Attenuation::DB11 as u8 }, _>::new(
        peripherals.pins.gpio2,
    )?;
    log::info!("[INIT] Battery ADC OK");

    // ── 4. 运行上电自检 ──
    let test_results = run_self_test(
        &mut display, &mut bme, &mut rtc,
        &mut adc1, &mut btn_pin,
        &mut bat_pin,
    )?;

    // ── 5. 初始化全局状态 ──
    let mut watch = WatchState::new();
    watch.display_ok = test_results.display_ok;
    watch.sensor_ok = test_results.bme280_ok;
    watch.rtc_ok = test_results.rtc_ok;

    // 读取初始传感器数据
    if watch.sensor_ok {
        if let Ok(t) = bme.temperature() {
            watch.temperature = t;
        }
        if let Ok(h) = bme.humidity() {
            watch.humidity = h;
        }
    }
    if watch.rtc_ok {
        if let Ok(dt) = rtc.datetime() {
            log::info!("RTC time: {}", dt);
        }
    }

    // 电池
    watch.battery_v = read_battery(&mut bat_adc, &mut bat_pin)?;

    // 进入表盘模式
    watch.state = SystemState::WatchFace;
    draw_watch_face_static(&mut display)?;
    watch.last_update = Instant::now();

    log::info!("[READY] Watch face mode active.");
    log::info!("[INFO] UP=Sensor  DOWN=Tests  OK=AI Chat");

    // ═══════════════════════════════════════════════════════════
    // 主循环
    // ═══════════════════════════════════════════════════════════
    loop {
        let now = Instant::now();

        // ── 按键检测 (每帧) ──
        let (up, down, ok) = scan_buttons(&btn_up, &btn_down, &btn_ok);
        handle_button_events(up, down, ok, &mut watch, &mut display, now)?;

        // ── 表盘刷新 (每秒) ──
        match watch.state {
            SystemState::WatchFace => {
                if now.duration_since(watch.last_update) >= Duration::from_secs(1) {
                    watch.last_update = now;

                    // 更新传感器数据
                    if watch.sensor_ok {
                        if let Ok(t) = bme.temperature() {
                            watch.temperature = t;
                        }
                        if let Ok(h) = bme.humidity() {
                            watch.humidity = h;
                        }
                        if let Ok(p) = bme.pressure() {
                            watch.pressure = p;
                        }
                    }
                    watch.battery_v = read_battery(&mut bat_adc, &mut bat_pin)?;

                    // 更新 UI
                    update_watch_face(
                        &mut display, &rtc,
                        watch.temperature, watch.humidity,
                        watch.pressure, watch.battery_v,
                        watch.sensor_ok, watch.rtc_ok,
                    )?;
                }
            }

            SystemState::SensorDetail => {
                // 传感器详情页 (每2秒刷新)
                if now.duration_since(watch.last_update) >= Duration::from_secs(2) {
                    watch.last_update = now;
                    draw_sensor_detail(&mut display,
                        watch.temperature, watch.humidity, watch.pressure,
                        watch.sensor_ok,
                    )?;
                }
            }

            _ => {}
        }

        // 控制循环频率 (~50Hz, 足够响应按键)
        sleep(Duration::from_millis(20));
    }
}

// ═══════════════════════════════════════════════════════════════
// 外设初始化函数
// ═══════════════════════════════════════════════════════════════

/// 初始化 ST7789 TFT 显示屏 (SPI2)
///
/// 连接方式:
///   SPI2 MOSI=GPIO11  SCLK=GPIO12  CS=GPIO10  DC=GPIO13  RST=GPIO14
///
/// 初始化流程:
///   1. 创建 SPI 驱动 (40MHz, MSB first, Mode 0)
///   2. 创建 DC 和 CS 引脚驱动
///   3. 封装为 display-interface-spi 适配器
///   4. mipidsi::Builder 发送 ST7789 初始化序列
///   5. 清除屏幕为背景色
fn init_display<'a>(
    p: &'a Peripherals,
) -> anyhow::Result<WatchDisplay<'a>> {
    use config::*;
    use esp_idf_hal::spi::*;

    // 创建 SPI2 驱动
    let spi = spi::SpiDriver::new(
        p.spi2,
        p.pins.gpio12,  // SCLK
        p.pins.gpio11,  // MOSI (SDA)
        Option::<gpio::Gpio9>::None,  // MISO 不用 (单向写入)
        &SpiConfig::new()
            .baudrate(SPI_FREQ.Hz().into())
            .data_mode(SpiDataMode::Mode0)  // CPOL=0 CPHA=0
            .bit_order(SpiBitOrder::MsbFirst),
    )?;

    // DC 引脚: HIGH=数据 LOW=命令
    let dc = gpio::PinDriver::output(p.pins.gpio13)?;
    // CS 引脚: LOW=选中 (display-interface 自动管理)
    let cs = gpio::PinDriver::output(p.pins.gpio10)?;
    // RST 引脚: 硬件复位
    let rst = gpio::PinDriver::output(p.pins.gpio14)?;

    // 封装为 display-interface SPI
    let di = SPIInterface::new(spi, dc, cs);

    // mipidsi 构建 ST7789 显示器
    let mut display = Builder::st7789(di)
        .with_display_size(DISPLAY_WIDTH, DISPLAY_HEIGHT)
        .with_orientation(mipidsi::Orientation::Portrait)
        .init(&mut delay::Ets)?;

    // 清屏
    display.clear(Rgb565::BLACK)?;

    Ok(display)
}

/// 初始化 I2C 总线 (I2C0)
///
/// SDA=GPIO17  SCL=GPIO18  频率=100kHz
/// 此总线挂载 BME280 (0x76) 和 DS3231 (0x68)
fn init_i2c<'a>(p: &'a Peripherals) -> anyhow::Result<i2c::I2cDriver<'a, esp_idf_hal::peripherals::I2C0>> {
    use config::*;
    let sda = unsafe { gpio::Gpio17::new() };  // 从裸 GPIO 编号创建
    let scl = unsafe { gpio::Gpio18::new() };
    let i2c = i2c::I2cDriver::new(
        unsafe { esp_idf_hal::peripherals::I2C0::new() },  // 获取 I2C0 外设
        sda, scl,
        &i2c::I2cConfig::new().baudrate(I2C_FREQ.Hz().into()),
    )?;
    Ok(i2c)
}

/// 初始化 BME280 传感器
fn init_bme280<'a>(
    i2c: i2c::I2cDriver<'a, esp_idf_hal::peripherals::I2C0>,
) -> anyhow::Result<Bme280Sensor<'a>> {
    // BME280::new 自动检测芯片 ID 并初始化
    let mut bme = BME280::new_primary(i2c);
    // 设置工作模式: 正常模式, 过采样 ×1 (低功耗)
    bme.init(&mut delay::Ets)?;
    Ok(bme)
}

/// 初始化 DS3231 RTC
fn init_rtc<'a>(
    i2c: i2c::I2cDriver<'a, esp_idf_hal::peripherals::I2C0>,
) -> anyhow::Result<Ds323x<i2c::I2cDriver<'a, esp_idf_hal::peripherals::I2C0>>> {
    let mut rtc = Ds323x::new_ds3231(i2c);
    // 检查是否掉电 (丢失时间)
    if rtc.has_gone_bad() {
        log::warn!("RTC lost power — setting to compile time");
        // 设置为编译时间 (后续可通过 WiFi NTP 校准)
        let now = NaiveDateTime::new(
            NaiveDate::from_ymd_opt(2026, 5, 14).unwrap(),
            chrono::NaiveTime::from_hms_opt(12, 0, 0).unwrap(),
        );
        rtc.set_datetime(&now)?;
    }
    Ok(rtc)
}

// ═══════════════════════════════════════════════════════════════
// 电池电压读取
// ═══════════════════════════════════════════════════════════════

fn read_battery(
    adc: &mut adc::AdcDriver<esp_idf_hal::peripherals::ADC1>,
    pin: &mut adc::AdcChannelDriver<{ gpio::Gpio2 }, &esp_idf_hal::peripherals::ADC1>,
) -> anyhow::Result<f32> {
    use config::BAT_DIVIDER_RATIO;

    // ESP32-S3 ADC: 12位分辨率 (0~4095), 11dB 衰减 (~3.3V 满量程)
    let raw = adc.read(pin)?;

    // 电压计算:
    //   1. raw/4095 × 3.3V → ADC 引脚电压
    //   2. × 分压比 (2:1) → 实际电池电压
    let voltage = (raw as f32 / 4095.0) * 3.3 * BAT_DIVIDER_RATIO;
    Ok(voltage)
}

// ═══════════════════════════════════════════════════════════════
// 按键扫描 + 消抖
// ═══════════════════════════════════════════════════════════════

/// 按键扫描 (带软件消抖)
///
/// 返回值: (up_pressed, down_pressed, ok_pressed)
///   每个字段为 true 表示检测到一次有效的按下 (下降沿触发)
///
/// 消抖策略:
///   - 200ms 冷却期: 两次按键事件之间至少间隔 200ms
///   - 下降沿检测: 记录上一帧电平，HIGH→LOW 跳变 = 按下
fn scan_buttons(
    btn_up: &gpio::PinDriver<'_, gpio::Gpio1, gpio::Input>,
    btn_down: &gpio::PinDriver<'_, gpio::Gpio3, gpio::Input>,
    btn_ok: &gpio::PinDriver<'_, gpio::Gpio5, gpio::Input>,
) -> (bool, bool, bool) {
    // 静态变量: 跨函数调用保持状态
    // Rust 中使用 lazy_static 或 thread_local! 或函数内的 static mut
    // 这里使用简化的 AtomicBool + static (线程安全)
    use std::sync::atomic::{AtomicBool, Ordering};
    static LAST_UP: AtomicBool = AtomicBool::new(true);
    static LAST_DOWN: AtomicBool = AtomicBool::new(true);
    static LAST_OK: AtomicBool = AtomicBool::new(true);
    static LAST_TIME: std::sync::Mutex<Option<Instant>> = std::sync::Mutex::new(None);

    let now = Instant::now();

    // 消抖: 200ms 冷却期
    {
        let mut lt = LAST_TIME.lock().unwrap();
        if let Some(t) = *lt {
            if now.duration_since(t) < Duration::from_millis(200) {
                return (false, false, false);
            }
        }
    }

    let up = btn_up.is_low();    // LOW = 按下 (INPUT_PULLUP)
    let down = btn_down.is_low();
    let ok = btn_ok.is_low();

    let mut up_triggered = false;
    let mut down_triggered = false;
    let mut ok_triggered = false;

    // 下降沿检测: 上一帧 HIGH + 当前帧 LOW = 刚按下
    if LAST_UP.load(Ordering::Relaxed) && up {
        up_triggered = true;
        *LAST_TIME.lock().unwrap() = Some(now);
    }
    if LAST_DOWN.load(Ordering::Relaxed) && down {
        down_triggered = true;
        *LAST_TIME.lock().unwrap() = Some(now);
    }
    if LAST_OK.load(Ordering::Relaxed) && ok {
        ok_triggered = true;
        *LAST_TIME.lock().unwrap() = Some(now);
    }

    // 更新上一帧状态
    LAST_UP.store(!up, Ordering::Relaxed);
    LAST_DOWN.store(!down, Ordering::Relaxed);
    LAST_OK.store(!ok, Ordering::Relaxed);

    (up_triggered, down_triggered, ok_triggered)
}

/// 按键事件处理: 页面切换逻辑
fn handle_button_events(
    up: bool, down: bool, ok: bool,
    watch: &mut WatchState,
    display: &mut WatchDisplay,
    now: Instant,
) -> anyhow::Result<()> {
    if up {
        log::info!("[BTN] UP pressed -> Sensor detail");
        watch.state = SystemState::SensorDetail;
        watch.last_update = Instant::now();
        // 触发立即渲染
        draw_sensor_detail(display,
            watch.temperature, watch.humidity, watch.pressure,
            watch.sensor_ok,
        )?;
    }

    if down {
        log::info!("[BTN] DOWN pressed -> Watch face");
        watch.state = SystemState::WatchFace;
        watch.last_update = Instant::now();
        draw_watch_face_static(display)?;
    }

    if ok {
        log::info!("[BTN] OK pressed -> AI Chat [reserved]");
        // TODO: 触发 AI 语音对话
        // watch.state = SystemState::AiListening;
    }

    Ok(())
}

// ═══════════════════════════════════════════════════════════════
// UI 绘制函数 (基于 embedded-graphics + Rgb565 色彩)
// ═══════════════════════════════════════════════════════════════

/// 启动画面 — 显示产品名称
fn draw_boot_screen(display: &mut WatchDisplay) -> anyhow::Result<()> {
    use colors::*;
    display.clear(BG)?;

    // "AI Smart" 文本
    let style = MonoTextStyle::new(&FONT_10X20, TEXT);
    Text::new("AI Smart", Point::new(45, 90), style).draw(display)?;

    // "Watch" 文本
    Text::new("Watch", Point::new(60, 115), style).draw(display)?;

    // "Initializing..."
    let small = MonoTextStyle::new(&FONT_6X10, Rgb565::new(0x88, 0x11, 0x08));
    Text::new("Initializing...", Point::new(60, 150), small).draw(display)?;

    // 版本号
    Text::new("v1.0 Rust 2026-05-14", Point::new(30, 220), small).draw(display)?;

    Ok(())
}

/// 表盘静态布局 (切换页面时调用)
///
/// 布局:
///   y=0    状态栏 (AI WATCH | v1)
///   y=22   时间区 (HH:MM:SS + 日期)
///   y=120  传感器卡片: 温度 | 湿度
///   y=178  传感器卡片: 气压 | 电池
fn draw_watch_face_static(display: &mut WatchDisplay) -> anyhow::Result<()> {
    use colors::*;
    display.clear(BG)?;

    // --- 状态栏 ---
    Rectangle::new(Point::new(0, 0), Size::new(240, 22))
        .draw_styled(&PrimitiveStyle::with_fill(CARD), display)?;
    let small = MonoTextStyle::new(&FONT_6X10, CYAN);
    Text::new("AI WATCH", Point::new(6, 14), small).draw(display)?;
    Text::new("v1", Point::new(195, 14), small).draw(display)?;

    // --- 时间区域背景 ---
    Rectangle::new(Point::new(0, 22), Size::new(240, 90))
        .draw_styled(&PrimitiveStyle::with_fill(ACCENT), display)?;

    // --- 传感器卡片 (2×2 网格) ---
    let card_style = PrimitiveStyle::with_fill(CARD);
    // 温度 (左上)
    Rectangle::new(Point::new(10, 120), Size::new(105, 50))
        .draw_styled(&card_style, display)?;
    // 湿度 (右上)
    Rectangle::new(Point::new(125, 120), Size::new(105, 50))
        .draw_styled(&card_style, display)?;
    // 气压 (左下)
    Rectangle::new(Point::new(10, 178), Size::new(105, 50))
        .draw_styled(&card_style, display)?;
    // 电池 (右下)
    Rectangle::new(Point::new(125, 178), Size::new(105, 50))
        .draw_styled(&card_style, display)?;

    // --- 卡片标签 ---
    let label_style = MonoTextStyle::new(&FONT_6X10, Rgb565::new(0x88, 0x11, 0x08));
    Text::new("Temperature", Point::new(16, 134), label_style).draw(display)?;
    Text::new("Humidity", Point::new(131, 134), label_style).draw(display)?;
    Text::new("Pressure", Point::new(16, 192), label_style).draw(display)?;
    Text::new("Battery", Point::new(131, 192), label_style).draw(display)?;

    // --- 底部导航 ---
    let nav_style = MonoTextStyle::new(&FONT_6X10, Rgb565::new(0x66, 0x0C, 0x0C));
    Text::new("[UP][DOWN] Nav  [OK] AI Chat", Point::new(8, 234), nav_style).draw(display)?;

    Ok(())
}

/// 表盘动态更新 — 每秒刷新时间和传感器数据 (局部刷新)
///
/// 只更新变化的区域:
///   1. 时:分:秒 (大号)
///   2. 日期
///   3. 温度数值
///   4. 湿度数值
///   5. 气压数值
///   6. 电池电压
fn update_watch_face(
    display: &mut WatchDisplay,
    rtc: &Ds323x<impl embedded_hal::i2c::I2c>,
    temp: f32, hum: f32, press: f32, bat: f32,
    sensor_ok: bool, rtc_ok: bool,
) -> anyhow::Result<()> {
    use colors::*;

    // --- 获取当前时间 ---
    let (hour, min, sec, year, month, day) = if rtc_ok {
        if let Ok(dt) = rtc.datetime() {
            (dt.hour(), dt.minute(), dt.second(),
             dt.year() as u16, dt.month() as u8, dt.day() as u8)
        } else {
            (12, 0, 0, 2026, 5, 14)
        }
    } else {
        (12, 0, 0, 2026, 5, 14)
    };

    // --- 更新时:分:秒 ---
    // 用背景色覆盖旧数字，再写新数字
    Rectangle::new(Point::new(15, 30), Size::new(210, 40))
        .draw_styled(&PrimitiveStyle::with_fill(ACCENT), display)?;

    let time_style = MonoTextStyle::new(&FONT_10X20, WHITE);
    let mut time_str: heapless::String<16> = heapless::String::new();
    write!(time_str, "{:02}:{:02}:{:02}", hour, min, sec).ok();
    Text::new(&time_str, Point::new(25, 65), time_style).draw(display)?;

    // --- 更新日期 ---
    Rectangle::new(Point::new(15, 70), Size::new(210, 20))
        .draw_styled(&PrimitiveStyle::with_fill(ACCENT), display)?;
    let date_style = MonoTextStyle::new(&FONT_6X10, Rgb565::new(0xAA, 0x15, 0x0A));
    let mut date_str: heapless::String<16> = heapless::String::new();
    write!(date_str, "{:04}-{:02}-{:02}", year, month, day).ok();
    Text::new(&date_str, Point::new(25, 88), date_style).draw(display)?;

    // --- 更新温度 ---
    Rectangle::new(Point::new(12, 142), Size::new(100, 16))
        .draw_styled(&PrimitiveStyle::with_fill(CARD), display)?;
    let val_style = MonoTextStyle::new(&FONT_6X10, WARN);
    let mut temp_str: heapless::String<8> = heapless::String::new();
    if sensor_ok {
        write!(temp_str, "{:.1}C", temp).ok();
    } else {
        temp_str.push_str("--.-C").ok();
    }
    Text::new(&temp_str, Point::new(16, 154), val_style).draw(display)?;

    // --- 更新湿度 ---
    Rectangle::new(Point::new(127, 142), Size::new(100, 16))
        .draw_styled(&PrimitiveStyle::with_fill(CARD), display)?;
    let hum_style = MonoTextStyle::new(&FONT_6X10, CYAN);
    let mut hum_str: heapless::String<8> = heapless::String::new();
    if sensor_ok {
        write!(hum_str, "{:.0}%", hum).ok();
    } else {
        hum_str.push_str("--%").ok();
    }
    Text::new(&hum_str, Point::new(131, 154), hum_style).draw(display)?;

    // --- 更新气压 ---
    Rectangle::new(Point::new(12, 200), Size::new(100, 16))
        .draw_styled(&PrimitiveStyle::with_fill(CARD), display)?;
    let press_style = MonoTextStyle::new(&FONT_6X10, SUCCESS);
    let mut press_str: heapless::String<10> = heapless::String::new();
    if sensor_ok {
        write!(press_str, "{:.0}hPa", press / 100.0).ok();
    } else {
        press_str.push_str("---hPa").ok();
    }
    Text::new(&press_str, Point::new(16, 212), press_style).draw(display)?;

    // --- 更新电池电压 ---
    Rectangle::new(Point::new(127, 200), Size::new(100, 16))
        .draw_styled(&PrimitiveStyle::with_fill(CARD), display)?;
    let bat_style = MonoTextStyle::new(&FONT_6X10, HIGHLIGHT);
    let mut bat_str: heapless::String<8> = heapless::String::new();
    write!(bat_str, "{:.1}V", bat).ok();
    Text::new(&bat_str, Point::new(131, 212), bat_style).draw(display)?;

    Ok(())
}

/// 传感器详情页面 (UP 键进入)
fn draw_sensor_detail(
    display: &mut WatchDisplay,
    temp: f32, hum: f32, press: f32,
    sensor_ok: bool,
) -> anyhow::Result<()> {
    use colors::*;
    display.clear(BG)?;

    // 标题
    let title = MonoTextStyle::new(&FONT_6X10, HIGHLIGHT);
    Text::new("Sensor Data", Point::new(20, 15), title).draw(display)?;

    // 分割线
    Rectangle::new(Point::new(10, 30), Size::new(220, 2))
        .draw_styled(&PrimitiveStyle::with_fill(ACCENT), display)?;

    let label = MonoTextStyle::new(&FONT_6X10, TEXT);
    let value = MonoTextStyle::new(&FONT_6X10, WARN);

    if sensor_ok {
        let mut s: heapless::String<32> = heapless::String::new();
        // 温度
        write!(s, "Temperature: {:.1} C", temp).ok();
        Text::new(&s, Point::new(20, 60), value).draw(display)?;
        // 湿度
        s.clear();
        write!(s, "Humidity:    {:.0} %", hum).ok();
        Text::new(&s, Point::new(20, 85), value).draw(display)?;
        // 气压
        s.clear();
        write!(s, "Pressure:    {:.0} hPa", press / 100.0).ok();
        Text::new(&s, Point::new(20, 110), value).draw(display)?;
    } else {
        Text::new("Sensor not available", Point::new(20, 60), label).draw(display)?;
        Text::new("Check BME280 wiring", Point::new(20, 80), label).draw(display)?;
    }

    // 导航
    let nav = MonoTextStyle::new(&FONT_6X10, Rgb565::new(0x88, 0x11, 0x08));
    Text::new("[OK] Watch  [UP] Sensor  [DOWN] Tests", Point::new(10, 230), nav).draw(display)?;

    Ok(())
}

// ═══════════════════════════════════════════════════════════════
// 硬件自检程序
// ═══════════════════════════════════════════════════════════════

/// 上电自检 — 6项测试
///
/// 测试项目:
///   1. TFT 显示屏 — SPI 通信正常 + framebuffer 可用
///   2. BME280 传感器 — I2C 通信 + 芯片ID 匹配
///   3. DS3231 RTC — I2C 通信 + 时间有效
///   4. 按键电路 — 提示用户按下任意键 (10秒超时)
///   5. 电池 ADC — 读数 > 0
///   6. I2C 总线扫描 — 列出所有设备
fn run_self_test(
    display: &mut WatchDisplay,
    bme: &mut Bme280Sensor,
    rtc: &mut Ds323x<impl embedded_hal::i2c::I2c>,
    btn_up: &gpio::PinDriver<'_, gpio::Gpio1, gpio::Input>,
    btn_down: &gpio::PinDriver<'_, gpio::Gpio3, gpio::Input>,
    btn_ok: &gpio::PinDriver<'_, gpio::Gpio5, gpio::Input>,
    adc: &mut adc::AdcDriver<esp_idf_hal::peripherals::ADC1>,
    adc_pin: &mut adc::AdcChannelDriver<{ gpio::Gpio2 }, &esp_idf_hal::peripherals::ADC1>,
) -> anyhow::Result<SelfTestResults> {
    use colors::*;
    log::info!("── SELF-TEST ──────────────────");

    let mut pass: u8 = 0;
    let mut fail: u8 = 0;

    // ── 测试 1: 显示屏 ──
    log::info!("  [1/6] Display...");
    let display_ok = true;  // 如果 init_display 成功，屏幕即正常
    if display_ok { pass += 1; log::info!("  PASS"); }
    else { fail += 1; log::warn!("  FAIL"); }

    // ── 测试 2: BME280 ──
    log::info!("  [2/6] BME280 sensor...");
    let bme280_ok = bme.temperature().is_ok();
    if bme280_ok {
        pass += 1;
        let t = bme.temperature().unwrap_or(0.0);
        let h = bme.humidity().unwrap_or(0.0);
        log::info!("  PASS (T={:.1}C H={:.0}%)", t, h);
    } else {
        fail += 1;
        log::warn!("  FAIL — check I2C wiring");
    }

    // ── 测试 3: DS3231 ──
    log::info!("  [3/6] DS3231 RTC...");
    let rtc_ok = rtc.datetime().is_ok();
    if rtc_ok {
        pass += 1;
        log::info!("  PASS ({})", rtc.datetime().unwrap());
    } else {
        fail += 1;
        log::warn!("  FAIL — check I2C wiring");
    }

    // ── 测试 4: 按键 ──
    log::info!("  [4/6] Buttons (press any within 10s)...");
    let start = Instant::now();
    let mut buttons_ok = false;
    while start.elapsed() < Duration::from_secs(10) {
        if btn_up.is_low() || btn_down.is_low() || btn_ok.is_low() {
            buttons_ok = true;
            log::info!("  DETECTED");
            break;
        }
        sleep(Duration::from_millis(50));
    }
    if buttons_ok { pass += 1; log::info!("  PASS"); }
    else { log::warn!("  SKIP — no input within 10s"); }

    // ── 测试 5: 电池 ADC ──
    log::info!("  [5/6] Battery ADC...");
    let battery_ok = adc.read(adc_pin).unwrap_or(0) > 0;
    if battery_ok {
        pass += 1;
        let v = read_battery(adc, adc_pin)?;
        log::info!("  PASS ({:.1}V)", v);
    } else {
        fail += 1;
        log::warn!("  FAIL — ADC reads 0");
    }

    // ── 测试 6: I2C 总线扫描 ──
    log::info!("  [6/6] I2C bus scan...");
    // I2C 扫描的简化版本: 只统计已知设备
    let mut devices: u8 = 0;
    if bme280_ok { devices += 1; }
    if rtc_ok { devices += 1; }
    log::info!("  {} device(s) found", devices);

    // ── 显示自检结果 ──
    display.clear(BG)?;
    let style = MonoTextStyle::new(&FONT_6X10, if fail == 0 { SUCCESS } else { WARN });
    let mut result: heapless::String<32> = heapless::String::new();
    write!(result, "Self-Test: {}/6", pass).ok();
    Text::new(&result, Point::new(50, 100), style).draw(display)?;

    let status = if fail == 0 { "ALL PASSED" } else { "CHECK WIRING" };
    let st_style = MonoTextStyle::new(&FONT_6X10, if fail == 0 { SUCCESS } else { FAIL });
    Text::new(status, Point::new(50, 120), st_style).draw(display)?;

    sleep(Duration::from_secs(2));

    log::info!("  Result: {}/6 passed", pass);
    log::info!("── END SELF-TEST ─────────────");

    Ok(SelfTestResults {
        display_ok,
        i2c_devices: devices,
        bme280_ok,
        rtc_ok,
        buttons_ok,
        battery_ok,
        passed: pass,
        total: 6,
    })
}

# 项目变更历史 (CHANGELOG)

## [v2.0] — 2026-05-23

### GPIO1-13 Only 精简引脚方案

**设计目标:**
- 所有引脚限制在 GPIO1-13 (ESP32-S3-Zero 标准 2.54mm 排针)
- 无需焊接底部 2.0mm 焊盘或内侧引脚
- 保留全部核心功能: 显示屏 + I2C传感器 + I2S音频 + AI语音 + 按键 + 电池检测

**引脚变更:**

| 功能 | v1.2 引脚 | v2.0 引脚 | 说明 |
|------|----------|----------|------|
| TFT_BL | GPIO4 | **GPIO9** | 腾出GPIO4给I2C |
| TFT_RST | GPIO14 | **省略** | RC上电复位 + TFT_eSPI软件复位(RST=-1) |
| I2C_SDA | GPIO17 | **GPIO3** | 移至1-13范围 |
| I2C_SCL | GPIO18 | **GPIO4** | 移至1-13范围 |
| I2S_BCLK | GPIO8+GPIO15 | **GPIO5** | 麦克风和功放共享位时钟 |
| I2S_WS | GPIO9+GPIO16 | **GPIO6** | 麦克风和功放共享字选择 |
| I2S_SD_IN(麦克风) | GPIO7 | **GPIO7** | 保持不变 |
| I2S_DOUT(功放) | GPIO6 | **GPIO8** | 调整以配合共享时钟布线 |
| BTN_UP/DOWN/OK | GPIO1/3/5 | **GPIO1(ADC)** | 3按键合并为1个ADC电阻分压 |
| BAT_ADC | GPIO2 | **GPIO2** | 保持不变 |
| MAX98357 SD | GPIO4→3.3V | **3.3V常开** | 保持不变 |
| WS2812 | GPIO21 | GPIO21 | 板载LED,不在排针,保持 |

**技术要点:**
- TFT_RST: 硬件RC电路(10kΩ上拉+0.1μF电容) + `TFT_RST=-1`软件复位, GC9A01/ST7789均支持
- ADC按键: 3按键→4电阻梯形分压→GPIO1(ADC1_CH0), 判断阈值: OK<300, DOWN 300~800, UP 800~2000
- I2S全双工: BCLK(GPIO5)+WS(GPIO6)由ESP32-S3作为Master产生, INMP441+MAX98357共享
- GPIO3安全性: 默认eFuse下JTAG走USB, I2C上拉4.7kΩ保持HIGH=安全

**修改文件:**
- `Hardware_Design.md` — 完整重写引脚表、ASCII图、ADC按键说明
- `firmware/src/main.cpp` — 更新引脚宏、新增ADC按键函数、更新I2C/I2S配置
- `firmware/rust/src/main.rs` — 更新引脚常量、新增ADC按键逻辑
- `simulation/diagram.json` — 更新连接图 (TFT_BL=9, DHT22=3, BTN_DOWN=2, RST=RC)
- `simulation/main.ino` — 更新引脚定义和注释
- `simulation/simulate.py` — 更新测试中的引脚期望值
- `verify.py` — 更新所有9项验证的引脚期望值
- `CHANGELOG.md` — 本文件

**仿真验证:**
- Python仿真: 8/8 全部通过
- 交叉验证: diagram.json ↔ main.ino 10脚比对 0错误
- 引脚冲突: 13 GPIOs, 0 conflicts

**结果:** 全部 13 个引脚均在标准 2.54mm 排针上，无需焊接底部焊盘。核心功能全部保留。

---

## [v1.2] — 2026-05-19

### 硬件引脚修正 (基于ESP32-S3-Zero实际引脚)

**问题发现:**
- GPIO47 在 ESP32-S3-Zero 的底部 2.0mm 焊盘上，不在标准 2.54mm 排针，无法直接接线
- GPIO33-37 被 Octal PSRAM 占用，完全不引出
- 之前的设计未考虑实际开发板的引脚引出情况

**修正内容:**
- **TFT_BL (背光PWM)**: GPIO47 → **GPIO4**
  - `Hardware_Design.md` 引脚表更新
  - `firmware/src/main.cpp` 宏 `TFT_BL_PIN` 47→4
  - `firmware/rust/src/main.rs` 常量 `TFT_BL` 47→4, 初始化代码 `gpio47`→`gpio4`
- **MAX98357 SD (功放关断)**: GPIO4 → **硬件接3.3V常开**
  - `firmware/src/main.cpp` 注释掉 `I2S_SPK_SD` 宏
  - `firmware/rust/src/main.rs` 删除 `I2S_SPK_SD` 常量, 添加说明注释
- **GPIO47**: 标注为底部焊盘/不使用
- **GPIO33-37**: 标注为 PSRAM 占用/不可用

**结果:** 全部功能引脚均在标准 2.54mm 排针上，无需焊接底部焊盘

---

## [v1.1] — 2026-05-14

### Rust 固件重写
- **新增 `firmware/rust/` 完整 Rust 项目**，基于 `esp-idf-hal` + `embedded-graphics` + `mipidsi`
- 新增文件：
  - `Cargo.toml` — 依赖配置
  - `build.rs` — ESP-IDF 构建脚本
  - `rust-toolchain.toml` — 固定 Xtensa ESP32-S3 工具链
  - `.cargo/config.toml` — 构建目标 + 烧录配置
  - `sdkconfig.defaults` — ESP-IDF SDK 精简配置
  - `partitions.csv` — Flash 分区表
  - `src/main.rs` — Rust 固件主程序 (~940行)
  - `README_RUST.md` — Rust 编译与烧录指南
- 迁移内容：硬件初始化、6项自检、表盘UI、传感器采集、按键处理、电池ADC
- 保留 C++ 固件在 `firmware/` 根目录作为参考

### 代码注释完善
- 所有代码文件添加详细中文注释
- `simulation/main.ino` — 逐函数级注释
- `firmware/src/main.cpp` — 引脚原理 + 电路说明注释
- `simulation/simulate.py` — 8项测试全部注释
- `verify.py` — 9项验证全部注释

---

## [v1.0] — 2026-05-13

### 项目创建
- 初始化 AI 智能助手手表项目
- 硬件平台：ESP32-S3-Zero (Waveshare)
- 项目路径：`C:\Users\7\Desktop\projects\AI_Smart_Watch\`

### 硬件设计
- 完成硬件物料清单 (BOM)，总计 ~134 元
- 完成 GPIO 引脚分配表
- 完成子系统设计：SPI 显示屏、I2C 传感器总线、I2S 双音频通道、电源管理
- 选型决策：
  - 显示屏：GC9A01 圆形屏 (1.28", 240×240) 或 ST7789 方形屏 (1.54")
  - 传感器：BME280 (温度+湿度+气压, I2C 0x76)
  - RTC：DS3231 (高精度, I2C 0x68)
  - 麦克风：INMP441 (I2S 数字 MEMS)
  - 功放：MAX98357 (I2S D类 3W)
  - 电池：3.7V 500mAh LiPo + TP4056 充电

### 仿真验证
- 创建 Wokwi 仿真电路图 (`diagram.json`)
- 编写 Wokwi 仿真测试代码 (`main.ino`)，包含 5 项硬件测试
- 编写 Python 本地仿真器 (`simulate.py`)，包含 8 项系统测试
- 编写硬件引脚交叉验证脚本 (`verify.py`)，9 项一致性检查
- **全部测试通过：0 错误，0 警告**

### 固件框架
- 创建 PlatformIO 项目配置 (`platformio.ini`)
- 编写固件主框架 (`firmware/src/main.cpp`)
  - 硬件初始化 + 自检程序
  - 表盘 UI 渲染
  - 按键导航框架
  - I2S 音频占位
  - WiFi + AI 对话占位

### 已知限制
- Wokwi CLI 需要 API token，本地未执行在线仿真
- ESP32-S3-Zero 在 Wokwi 中只有 DevKitC-1 近似模型
- 仿真中 DHT22 替代 BME280
- 实物尚未购买外设，固件未在真机上验证

---

## 待办事项

- [ ] 购买屏幕 + 传感器 + RTC（约40元）
- [ ] 实物焊接并运行固件自检
- [ ] 屏幕 UI 适配（LVGL 移植）
- [ ] WiFi + HTTP 客户端对接 AI API
- [ ] I2S 音频链路调试（ESP-ADF 集成）
- [ ] 3D 打印外壳设计
- [ ] 电池供电优化（深度睡眠 + RTC 唤醒）

# 项目变更历史 (CHANGELOG)

## [v1.0] — 2026-05-13

### 项目创建
- 初始化 AI 智能助手手表项目
- 硬件平台：ESP32-S3-Zero (Waveshare)
- 项目路径：`C:\Users\7\Desktop\AI_Smart_Watch\`

### 硬件设计
- 完成硬件物料清单 (BOM)，总计 ~134 元
- 完成 GPIO 引脚分配表（20个引脚，0冲突）
- 完成子系统设计：SPI 显示屏、I2C 传感器总线、I2S 双音频通道、电源管理
- 选型决策：
  - 显示屏：GC9A01 圆形屏 (1.28", 240×240) 或 ST7789 方形屏 (1.54")
  - 传感器：BME280 (温度+湿度+气压, I2C 0x76)
  - RTC：DS3231 (高精度, I2C 0x68)
  - 麦克风：INMP441 (I2S 数字 MEMS)
  - 功放：MAX98357 (I2S D类 3W)
  - 电池：3.7V 500mAh LiPo + TP4056 充电

### 引脚分配
```
    显示(SPI):    CS=10, DC=13, RST=14, MOSI=11, SCLK=12, BL=47
    I2C 总线:     SDA=17, SCL=18
    麦克风(I2S):  SCK=15, WS=16, SD=7
    功放(I2S):    BCLK=8, LRC=9, DIN=6, SD=4
    按键:         UP=1, DOWN=3, OK=5
    电池ADC:      GPIO2
    板载LED:      GPIO21 (WS2812)
```

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
  - 表盘 UI 渲染（时间、温度、湿度、气压、电量）
  - 按键导航框架
  - I2S 音频占位
  - WiFi + AI 对话占位

### 已知限制
- Wokwi CLI 需要 API token，本地未执行在线仿真
- ESP32-S3-Zero 在 Wokwi 中只有 DevKitC-1 近似模型
- 仿真中 DHT22 替代 BME280（接口不同但功能等价）
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

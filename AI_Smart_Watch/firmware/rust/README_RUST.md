# AI Smart Watch — Rust 固件编译指南

## 前提条件

### 1. 安装 ESP Rust 工具链

```bash
# 安装 espup (ESP Rust 工具链管理器)
cargo install espup

# 安装 Xtensa Rust 编译器 + ESP-IDF
espup install

# 安装烧录工具
cargo install cargo-espflash
cargo install cargo-espmonitor
```

### 2. 验证工具链

```bash
rustup show
# 应显示: esp (default)
# 目标: xtensa-esp32s3-espidf

espflash --version
# 烧录工具版本
```

## 编译

```bash
cd firmware/rust

# 开发构建 (快速编译, 带调试信息)
cargo build

# 发布构建 (体积优化, LTO 启用)
cargo build --release
```

## 烧录与运行

```bash
# 烧录并打开串口监视器
cargo espflash flash --release --monitor

# 仅烧录
cargo espflash flash --release

# 仅监视串口
cargo espflash monitor
```

## 项目结构

```
firmware/rust/
├── Cargo.toml              # 依赖 + 项目配置
├── build.rs                # ESP-IDF 构建脚本
├── rust-toolchain.toml     # Rust 工具链固定
├── sdkconfig.defaults      # ESP-IDF SDK 配置
├── partitions.csv          # Flash 分区表
├── .cargo/
│   └── config.toml         # Cargo 构建目标 + 烧录配置
└── src/
    └── main.rs             # 固件主程序 (~940行)
        ├── config 模块     # 引脚定义 + 硬件常量
        ├── colors 模块     # RGB565 配色表
        └── 主逻辑          # 初始化/自检/主循环/UI/按键
```

## 从 C++ 到 Rust 的关键变化

| 方面 | C++ (Arduino) | Rust (ESP-IDF) |
|------|--------------|----------------|
| 内存安全 | 手动管理, 易越界 | 编译期检查, 无 UB |
| 并发安全 | 无保护 | Send/Sync trait + Mutex |
| 错误处理 | 返回值/异常 | Result<T, E> 类型 |
| 依赖管理 | PlatformIO lib_deps | Cargo.toml (语义化版本) |
| 类型系统 | 隐式转换 | 严格类型, 泛型约束 |
| 构建系统 | CMake + Python | Cargo + embuild |

## 已知问题

1. **embedded-hal 版本兼容性**: esp-idf-hal 0.44 实现 embedded-hal 1.0,
   部分外设驱动 crate (ds323x, bme280) 可能需要 embedded-hal 0.2.x。
   解决方案: 使用 `embedded-hal-0-2` 适配层 crate 或使用更新版本的驱动。

2. **首次编译时间**: ESP-IDF 首次编译需要下载完整的 ESP-IDF (~500MB)
   并编译其所有组件, 耗时 5-15 分钟。后续增量编译仅需数十秒。

3. **I2C 总线共享**: BME280 和 DS3231 共享 I2C0 总线。
   在 Rust 中，I2C 驱动是独占所有权的。
   解决方案: 使用 `shared-bus` crate 或 ESP-IDF 的多句柄特性。

## 性能对比 (预估)

| 指标 | C++/Arduino | Rust/ESP-IDF |
|------|------------|--------------|
| 固件体积 | ~800KB | ~600KB (LTO) |
| 内存占用 | ~150KB | ~120KB |
| SPI 刷新率 | 40MHz | 40MHz (相同) |
| 启动时间 | ~1.2s | ~0.8s |
| 编译时间 (增量) | ~5s | ~3s |
| 编译时间 (首次) | ~30s | ~10min |

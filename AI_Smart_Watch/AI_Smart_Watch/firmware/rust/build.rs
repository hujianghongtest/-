// ╔══════════════════════════════════════════════════════════════╗
// ║  ESP-IDF Rust 项目构建脚本 (build.rs)                       ║
// ╚══════════════════════════════════════════════════════════════╝
//
// 【作用】
//   在编译 Rust 代码之前，自动配置 ESP-IDF 构建系统。
//   此脚本由 Cargo 在编译 crate 之前调用。
//
// 【工作原理】
//   esp-idf-sys crate 通过此脚本:
//     1. 检测或下载 ESP-IDF (优先使用 $IDF_PATH 环境变量)
//     2. 调用 ESP-IDF 的 CMake 构建系统
//     3. 生成 bindgen FFI 绑定 (Rust ↔ C 互操作)
//     4. 链接 ESP-IDF 组件 (FreeRTOS, WiFi, SPI, I2C, I2S...)
//
// 【前提条件】
//   需要安装 ESP Rust 工具链:
//     cargo install espup
//     espup install
//     此命令会自动安装 Xtensa Rust 编译器 + ESP-IDF
//
// 【构建命令】
//   开发构建:  cargo build
//   发布构建:  cargo build --release
//   烧录运行:  cargo espflash flash --release --monitor

fn main() {
    // esp-idf-sys 提供的构建辅助函数
    // 自动处理整个 ESP-IDF 工具链集成
    embuild::build::CfgArgs::output_propagated("ESP_IDF").unwrap();
    embuild::build::LinkArgs::output_propagated("ESP_IDF").unwrap();
}

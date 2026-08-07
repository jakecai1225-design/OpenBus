---
kind: logging_system
name: 基于 spdlog 的 SIN_LOG_* 结构化日志系统
category: logging_system
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/main.cpp
---

## 1. 使用的系统与框架

本项目采用 **spdlog**（位于 `third_party/spdlog/`）作为底层日志库，通过自定义封装提供统一的 `SIN_LOG_*` 宏接口。日志输出同时写入控制台与滚动文件两个 sink，形成“控制台 + 文件”双通道输出。

- 格式化引擎：使用 `fmt::format`（spdlog 依赖的 fmt 库）进行参数化消息格式化。
- 线程模型：控制台 sink 使用 `stdout_color_sink_mt`（多线程安全），文件 sink 使用 `rotating_file_sink_mt`（多线程安全）。

## 2. 核心文件与位置

| 文件 | 作用 |
|---|---|
| `src/core/logging.h` | 定义 `logging` 命名空间、`init()` / `shutdown()` / `logger()` 接口及 `SIN_LOG_DEBUG/INFO/WARN/ERROR` 四个宏 |
| `src/core/logging.cpp` | 实现 spdlog logger 初始化、sink 配置、日志目录解析与回退逻辑 |
| `src/utils/logging.h` | 重包含 `core/logging.h`，作为统一入口供 UI 层等模块引用 |
| `src/main.cpp` | 在 `QApplication` 启动后调用 `logging::init()`，应用退出前调用 `logging::shutdown()` |

## 3. 架构与设计约定

### 3.1 初始化流程
- 调用 `logging::init(logDir)`：若未指定目录，则使用 Qt 的 `QStandardPaths::AppDataLocation/logs`；创建目录失败时回退到当前工作目录下的 `./logs`。
- 创建两个 sink：
  - 控制台：`stdout_color_sink_mt`，级别设为 `debug`。
  - 文件：`rotating_file_sink_mt`，单文件上限 5MB，保留 3 个滚动文件，级别设为 `trace`（比默认 logger 更细粒度）。
- 默认 logger 名称为 `sin`，全局级别 `debug`，flush 策略为 `info` 及以上级别自动 flush。
- 日志格式模式：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，即 `[时间戳] [级别] 消息`。

### 3.2 调用约定
- 所有模块通过 `#include "core/logging.h"`（或 `utils/logging.h`）引入，使用 `SIN_LOG_*` 宏而非直接调用 spdlog。
- 每个日志调用必须传入第一个参数 `tag` 标识模块名（如 `CanDeviceKvaser`、`CanDevicePEAK`、`CanDeviceZLG`、`ICanDevice` 等），便于按模块筛选日志。
- 消息体使用 `fmt::format` 风格的 `{}` 占位符，支持类型安全的参数拼接。

### 3.3 生命周期管理
- 在 `main.cpp` 中，`QApplication` 构造完成后立即调用 `logging::init()`，确保 Qt 元对象系统已就绪。
- 应用退出时 `app.exec()` 返回后调用 `logging::shutdown()`，刷新缓冲并释放资源。

## 4. 约定与约束

- **统一入口**：禁止各模块自行创建 spdlog logger，必须通过 `logging::init()` 初始化的全局 logger 和 `SIN_LOG_*` 宏输出。
- **模块标签**：所有日志调用必须携带 `tag` 参数用于区分来源模块，这是宏签名强制要求的。
- **级别策略**：控制台输出最低显示 `debug`，文件记录最低显示 `trace`，错误及以上级别会触发 flush 保证落盘。
- **日志路径回退**：当用户可写目录不可用时自动降级到 `./logs`，避免程序因无法写日志而崩溃。
- **Qt 集成**：日志目录借助 `QStandardPaths` 定位跨平台应用数据目录，体现 Qt 项目风格。
- **不直接使用 qDebug**：代码注释明确说明用 `SIN_LOG_*` 替代 `qDebug`，保持日志体系一致。

## 5. 使用示例（来自实际代码）

```cpp
SIN_LOG_INFO("CanDeviceKvaser", "canlib32.dll loaded successfully");
SIN_LOG_ERROR("CanDevicePEAK", "CAN_Initialize failed: status=0x{:08X}", status);
SIN_LOG_WARN("ICanDevice", "brand {} not implemented yet", brand);
SIN_LOG_DEBUG("CanDeviceKvaser", "canReadWait: {}", result);
```

## 6. 第三方依赖

- `third_party/spdlog/`：spdlog 源码，提供高性能异步/同步日志能力。
- `third_party/nlohmann_json/`：JSON 库（非日志相关，但随 spdlog 一同引入）。
- 项目通过 CMake 将 spdlog 作为静态/头文件依赖集成。

---
kind: logging_system
name: 基于 spdlog 的 SIN 日志系统（控制台 + 滚动文件双 sink）
category: logging_system
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/main.cpp
    - src/core/candevice_kvaser.cpp
    - src/core/candevice_peak.cpp
    - src/core/candevice_zlg.cpp
---

## 1. 使用的系统与框架

- **底层库**：`spdlog`（位于 `third_party/spdlog/`），通过 CMake 作为第三方依赖引入。
- **格式化**：使用 `fmt::format`（C++23-like 风格占位符 `{}`）对消息进行格式化，而非 Qt 的 `%1` 语法。
- **Qt 集成**：通过 `QStandardPaths::AppDataLocation` 定位跨平台用户数据目录；日志目录为 `<AppData>/logs/sin.log`；若不可写则回退到当前工作目录下的 `./logs/sin.log`。
- **线程模型**：控制台 sink 使用 `stdout_color_sink_mt`（多线程安全），文件 sink 使用 `rotating_file_sink_mt`（多线程安全）。全局 logger 为单例式共享指针。

## 2. 核心文件与入口

| 文件 | 职责 |
|---|---|
| `src/core/logging.h` | 定义 `logging` 命名空间、`init()` / `shutdown()` / `logger()` API，以及 `SIN_LOG_*` 宏 |
| `src/core/logging.cpp` | 实现双 sink 初始化、默认 logger 创建、模式设置、错误回退 |
| `src/utils/logging.h` | 薄包装头，重新包含 `core/logging.h`，作为统一入口 |
| `src/main.cpp` | 在 `QApplication` 构造后调用 `logging::init()`，退出前调用 `logging::shutdown()` |

## 3. 架构与约定

### 3.1 初始化流程
1. `main()` 中先创建 `QApplication`，再调用 `logging::init()`。
2. `init()` 确定日志目录：优先使用 `QStandardPaths::writableLocation(AppDataLocation) + "/logs"`；为空或不可写时回退到 `./logs`。
3. 创建两个 sink：
   - **控制台 sink**：`stdout_color_sink_mt`，级别设为 `debug`。
   - **文件 sink**：`rotating_file_sink_mt`，轮转策略为 5MB × 3 个文件，级别设为 `trace`。
4. 创建名为 `sin` 的全局 logger，设置全局级别为 `debug`，并在 `info` 及以上级别自动 flush。
5. 通过 `spdlog::set_default_logger` 注册为默认 logger，使 `SPDLOG_*` 宏可直接使用。
6. 启动时输出一条 INFO 日志记录日志目录路径。

### 3.2 输出格式
默认 pattern：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`
即：`[2026-07-29 14:30:00.123] [info] message`。

### 3.3 模块标签约定
所有 `SIN_LOG_*` 宏的第一个参数是 **tag**（字符串字面量），用于标识模块来源，例如：
- `SIN_LOG_INFO("CanDeviceKvaser", "...")`
- `SIN_LOG_ERROR("CanDevicePEAK", "...")`
- `SIN_LOG_WARN("CanDeviceZLG", "...")`
- `SIN_LOG_DEBUG("DbcManager", "...")`

该 tag 会被写入日志消息的 `[{}]` 位置，便于按模块过滤。

### 3.4 日志级别策略
- **全局级别**：`debug`（由 `s_logger->set_level` 和 `spdlog::set_level` 双重设置）。
- **控制台级别**：`debug`。
- **文件级别**：`trace`（文件记录更详细，便于离线分析）。
- 应用关闭时调用 `logging::shutdown()` 确保缓冲刷新并释放资源。

## 4. 使用方式与约束

### 4.1 推荐用法
```cpp
#include "core/logging.h"
// 或 #include "utils/logging.h"（等价）

SIN_LOG_DEBUG("ModuleTag", "解析文件: {}", fileName.toStdString());
SIN_LOG_INFO("ModuleTag", "设备已打开: {}, baud={}", name, baud);
SIN_LOG_WARN("ModuleTag", "品牌 {} 尚未实现", brand);
SIN_LOG_ERROR("ModuleTag", "canOpenChannel failed: {}", h);
```

### 4.2 约束与约定
- **必须在 `QApplication` 之后初始化**：`main.cpp` 注释明确要求“需在 QApplication 设置名称之后”调用 `logging::init()`。
- **必须配对调用 shutdown**：`main()` 在 `app.exec()` 返回后调用 `logging::shutdown()`，避免析构顺序问题。
- **禁止直接使用 `qDebug()`**：代码注释明确说明“提供控制台 + 滚动文件双 sink。使用 SIN_LOG_* 系列宏替代 qDebug”，且各模块均通过 `SIN_LOG_*` 输出。
- **字符串参数需转为 std::string**：由于底层走 `spdlog` + `fmt`，Qt 字符串需调用 `.toStdString()` 传入（如 `fileName.toStdString()`）。
- **文件写入失败会静默降级**：当 AppData 目录不可写时，自动回退到 `./logs`；若仍失败，仅保留控制台输出，不抛异常。
- **日志轮转固定**：文件大小上限 5MB，最多保留 3 个历史文件，无配置开关。

### 4.3 当前覆盖范围
经搜索，`SIN_LOG_*` 宏已在以下核心模块中使用：`candevice_kvaser.cpp`、`candevice_peak.cpp`、`candevice_zlg.cpp`、`candevice.cpp` 等 CAN 设备驱动层，用于记录 DLL 加载、通道打开/关闭、读取状态等关键事件。UI 层暂未发现直接使用该日志系统的调用。
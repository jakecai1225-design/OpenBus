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

项目采用 **spdlog**（`third_party/spdlog/`）作为底层日志库，通过 `src/core/logging.h/.cpp` 暴露统一的 `logging::init()` / `logging::shutdown()` / `logging::logger()` API，并以 `SIN_LOG_DEBUG/INFO/WARN/ERROR(tag, ...)` 四个宏作为全仓统一入口。格式化使用 `fmt::format`（C++23-like 风格），模块标识以第一个参数 `tag` 字符串形式传入。

## 2. 核心文件与职责

- `src/core/logging.h`：声明 `logging` 命名空间、初始化/关闭接口、默认 logger 获取函数；定义 `SIN_LOG_*` 四个级别宏，格式为 `[{}] {}`，即 `[模块标签] 消息`。
- `src/core/logging.cpp`：实现双 sink 初始化——控制台彩色输出 (`stdout_color_sink_mt`) + 滚动文件 (`rotating_file_sink_mt`)；设置全局默认 logger 并写入 `sin.log`。
- `src/utils/logging.h`：仅重新 include `core/logging.h`，作为 utils 层统一入口。
- `src/main.cpp`：在 `QApplication` 构造后调用 `logging::init()`，程序退出前调用 `logging::shutdown()`。

## 3. 架构与设计决策

### 3.1 Sink 策略
- **控制台 sink**：级别设为 `debug`，用于开发期实时观察。
- **文件 sink**：`rotating_file_sink_mt`，单文件最大 5MB，最多保留 3 个轮转文件（`sin.log`, `sin.1.log`, `sin.2.log`），级别设为 `trace`（记录最细粒度）。
- **目录选择**：优先使用 `QStandardPaths::AppDataLocation/logs`；若不可写则回退到当前目录 `./logs`；若仍失败则仅保留控制台输出。
- **刷新策略**：`flush_on(info)`，info 及以上级别立即落盘。
- **日志模式**：`[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v`，输出形如 `[2026-07-29 14:30:00.123] [info] [DbcManager] message`。

### 3.2 模块标签约定
所有 `SIN_LOG_*` 调用均带第一个参数 `tag`，典型取值如 `CanDeviceKvaser`、`CanDevicePEAK`、`CanDeviceZLG`、`ICanDevice`、`DbcManager` 等，用于区分来源模块。该 tag 是纯字符串，非类名或命名空间推导，由调用方显式传入。

### 3.3 生命周期管理
- 启动顺序：`QApplication` → `logging::init()` → `AppConfig` / `SessionManager` / `ThemeManager` → `MainWindow`。
- 关闭顺序：`app.exec()` 返回后 → `logging::shutdown()`（flush + `spdlog::shutdown`）→ 返回。

## 4. 使用规范与约束

- **禁止直接使用 `qDebug`**：代码注释明确说明“使用 SIN_LOG_* 系列宏替代 qDebug”，且仓库中未发现 `qDebug` 调用，全部通过 `SIN_LOG_*` 输出。
- **必须提供 tag**：四个宏的第一个参数固定为模块标签字符串，调用点需自行维护一致的命名（如 `CanDeviceKvaser`）。
- **必须显式初始化/关闭**：`main.cpp` 在应用生命周期两端调用 `logging::init()` / `logging::shutdown()`，其他模块不得重复初始化。
- **级别使用约定**：
  - `DEBUG`：设备驱动内部调试（如 `canReadWait`、`CAN_ReadFD status=...`）。
  - `INFO`：关键流程节点（设备打开/关闭、DLL 加载成功、解析完成）。
  - `WARN`：可恢复异常（如品牌未实现、功能降级）。
  - `ERROR`：致命错误（DLL 缺失、API 调用失败、无法打开设备）。
- **线程安全**：两个 sink 均为 `_mt` 多线程版本，适合跨线程日志输出。
- **依赖第三方库**：依赖 `spdlog` 和 `fmt`（通过 `third_party/Dependencies.cmake` 引入），编译时需确保这两个子模块可用。

## 5. 覆盖范围

日志系统被广泛集成于 CAN 设备驱动层（`candevice_kvaser.cpp`、`candevice_peak.cpp`、`candevice_zlg.cpp`）、核心模块（`candevice.cpp`、`dbcmanager` 等），形成统一的诊断输出通道，便于定位 DBC 导入、信号发送、设备连接等问题。
---
kind: error_handling
name: 基于信号/日志的 Qt 错误处理体系
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/utils/logging.h
    - src/core/candevicemanager.cpp
    - src/ui/mainwindow.cpp
    - src/core/candevice_zlg.cpp
    - src/core/canfileio/blf.cpp
    - src/core/file_import/asc_importer.cpp
    - src/core/file_import/blf_importer.cpp
    - src/core/file_import/csv_importer.cpp
    - src/ui/deviceconnectiontab.cpp
    - src/ui/dbcimportdialog.cpp
---

## 1. 整体方法

本仓库是一个 Qt6 桌面应用，没有定义统一的 C++ 异常类型或全局 `setErrorHandler`。错误处理采用 **分层、混合式** 策略：
- 核心层（设备驱动、文件 I/O）通过 **返回值 + 结构化日志** 上报错误；
- UI 层通过 **Qt 信号槽** 将底层错误向上传播到主窗口，再写入底部输出面板或弹出 `QMessageBox`；
- 诊断信息统一通过基于 spdlog 的 `SIN_LOG_*` 宏输出到控制台与滚动日志文件。

该方案避免了在 Qt GUI 线程中抛 C++ 异常的惯例，同时保留了人类可读的诊断输出。

## 2. 关键文件与组件

| 文件 | 职责 |
|---|---|
| `src/core/logging.h` / `.cpp` | 基于 spdlog 的日志系统初始化、`SIN_LOG_DEBUG/INFO/WARN/ERROR` 宏，双 sink（控制台 + 滚动文件） |
| `src/utils/logging.h` | 对 `core/logging.h` 的重新包含，作为 utils 层的统一入口 |
| `src/core/candevicemanager.cpp` | 设备连接生命周期管理，失败时 `emit errorOccurred(...)` |
| `src/ui/mainwindow.cpp` | 顶层信号处理器，将 `errorOccurred` 写入底部输出面板 |
| `src/core/candevice_zlg.cpp` | ZLG 硬件驱动封装，DLL 加载/函数解析失败用 `SIN_LOG_ERROR` 记录并返回 false |
| `src/core/canfileio/blf.cpp` | BLF 格式解析，使用 `qDebug`/`qWarning` 报告魔数校验、解压失败等错误 |
| `src/core/file_import/*.cpp` | ASC/BLF/CSV 导入器，打开失败返回空结果并用 `qWarning` 提示 |
| `src/ui/deviceconnectiontab.cpp` | 设备连接 UI，连接失败弹 `QMessageBox::warning` |
| `src/ui/dbcimportdialog.cpp` | DBC 导入对话框，加载失败弹 `QMessageBox::warning` |

## 3. 架构与约定

### 3.1 核心层：返回值 + 结构化日志
- 所有 I/O 和驱动操作以 **bool/int 返回值** 表达成功/失败（如 `open()` 返回 bool，`send()` 返回发送帧数），调用方据此分支。
- 每个失败路径都伴随 `SIN_LOG_ERROR` / `SIN_LOG_WARN`，tag 标识模块名（如 `"CanDeviceZLG"`、`"ICanDevice"`），便于日志过滤。
- 示例：`candevicemanager.cpp` 中设备创建失败 `emit errorOccurred(...)` 后 `m_device.reset()`；`candevice_zlg.cpp` 中 DLL 缺失或符号缺失直接 `return false` 并写日志。

### 3.2 跨层传播：Qt 信号槽
- `CanDeviceManager` 暴露 `errorOccurred(const QString&)` 信号，`MainWindow` 构造函数中将其连接到 lambda，追加到 `m_bottomPanel->appendOutput(...)`。
- 这种设计使底层错误不依赖上层 UI 细节，但又能被主窗口统一消费。

### 3.3 UI 层：用户可见的错误
- 对用户可恢复的错误（如 DBC 文件无法加载、设备连接失败），使用 `QMessageBox::warning(this, "...", msg)` 弹窗。
- 对运行期诊断（设备断开、录制开始/结束等），写入底部输出面板而非弹窗，避免打断工作流。

### 3.4 第三方库错误：静默降级
- BLF 写入器 `BlfWriter::open` 直接 `qWarning("暂不支持 BLF 写入")` 并返回 `false`，调用方应回退到其他格式。
- ZLG CAN FD 函数为可选导出，缺失时仅 `SIN_LOG_INFO` 记录，不影响 Classic CAN 模式。

## 4. 约定与约束

- **不使用 C++ 异常进行控制流**：代码中未见 `throw`/`catch` 用于业务逻辑，仅在少数地方使用 `std::exception` 相关头文件（来自第三方库）。错误通过返回值传播。
- **日志级别有明确分工**：`SIN_LOG_ERROR` 用于不可恢复错误（DLL 缺失、SDK 函数缺失）；`SIN_LOG_WARN` 用于可恢复异常（未实现品牌、32 位 DLL 跳过）；`SIN_LOG_INFO` 用于正常流程（DLL 发现、CAN FD 不可用说明）。
- **调试输出保留 `qDebug`/`qWarning`**：部分旧代码（如 `blf.cpp`）仍直接使用 Qt 原生日志，尚未迁移到 `SIN_LOG_*` 宏，属于遗留风格。
- **无全局 panic/recover**：Windows 平台下 DLL 加载失败不会触发系统级崩溃，而是通过 `QLibrary::isLoaded()` 检查并优雅降级。
- **UI 错误必须非阻塞**：弹窗仅用于需要用户确认的错误；常规状态变化走信号槽 + 底部面板，保证长时间运行的录制/回放不被中断。

## 5. 观察到的不足

- 日志风格不统一：核心层已迁移到 `SIN_LOG_*`，但文件解析层仍混用 `qDebug`/`qWarning`。
- 缺少统一的错误码枚举：不同模块用字符串描述错误，不利于程序化判断错误类型。
- 没有集中错误处理中间件：每个模块各自决定是弹窗、写日志还是返回 false，缺乏横切规则。
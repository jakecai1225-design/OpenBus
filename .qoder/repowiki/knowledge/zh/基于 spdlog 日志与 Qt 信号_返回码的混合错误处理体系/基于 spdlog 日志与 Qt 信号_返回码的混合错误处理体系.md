---
kind: error_handling
name: 基于 spdlog 日志与 Qt 信号/返回码的混合错误处理体系
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/core/candevice.cpp
    - src/core/candevice_zlg.cpp
    - src/core/candevice_peak.cpp
    - src/core/candevice_kvaser.cpp
    - src/core/candevicemanager.cpp
    - src/core/dbcmanager.cpp
    - src/core/appconfig.cpp
    - src/core/projectmanager.cpp
    - src/core/sessionmanager.cpp
    - src/core/file_import/blf_importer.cpp
    - src/core/file_import/asc_importer.cpp
    - src/core/file_import/csv_importer.cpp
    - src/core/canfileio/blf.cpp
    - src/ui/mainwindow.cpp
    - src/ui/dbcimportdialog.cpp
---

## 1. 整体方案

该工程是一个 C++/Qt6 桌面应用，**没有统一的自定义异常类型或错误码枚举**。错误处理采用“分层混合”策略：
- **核心 I/O、设备驱动层**：通过 `bool` 返回值 + `SIN_LOG_*` 宏（基于 spdlog）记录错误；调用方根据返回值决定后续流程。
- **配置/JSON 解析层**：使用 `nlohmann::json::parse_error` 等标准异常，在 catch 块中降级为默认值并记录日志。
- **UI 交互层**：通过 Qt 信号 `errorOccurred(...)` 向主窗口上报错误，并以 `QMessageBox` 弹窗提示用户。
- **调试输出**：部分文件解析器仍使用 `qDebug()` / `qWarning()` 直接输出诊断信息（作为临时调试手段）。

日志系统由 `src/core/logging.h/.cpp` 提供，封装了 spdlog 的 console + rolling file sink，并通过 `SIN_LOG_DEBUG/INFO/WARN/ERROR(tag, fmt...)` 宏统一输出，格式形如 `[时间] [level] [模块标签] 消息`。`src/utils/logging.h` 仅重新 include 该头，作为跨模块的统一入口。

## 2. 关键文件与位置

| 层次 | 文件 | 作用 |
|---|---|---|
| 日志基础设施 | `src/core/logging.h`, `src/core/logging.cpp` | 初始化 spdlog、定义 `SIN_LOG_*` 宏 |
| CAN 设备抽象 | `src/core/candevice*.cpp` | 各品牌 DLL 动态加载失败时 `SIN_LOG_ERROR` 并返回 false |
| 设备管理器 | `src/core/candevicemanager.cpp` | 将底层错误转换为 `emit errorOccurred(...)` 信号 |
| DBC 管理 | `src/core/dbcmanager.cpp` | `loadDbc` 返回 bool，失败时不抛异常 |
| 配置文件 | `src/core/appconfig.cpp` | 捕获 `json::parse_error`，回退到 `defaultConfig()` |
| 项目/会话 | `src/core/projectmanager.cpp`, `sessionmanager.cpp` | 同上，JSON 解析失败时记录并回退 |
| 文件导入 | `src/core/file_import/{asc,blf,csv}_importer.cpp` | 打开失败 `qWarning()`，解析失败返回空结果 |
| BLF 解析器 | `src/core/canfileio/blf.cpp` | 魔数/长度校验失败 `qWarning()`，继续尝试解压 |
| UI 错误上报 | `src/ui/mainwindow.cpp` | 顶层 `catch (...)` 兜底，避免崩溃；`QMessageBox` 弹窗 |
| DBC 导入对话框 | `src/ui/dbcimportdialog.cpp` | 失败时 `QMessageBox::warning` |

## 3. 架构与约定

### 3.1 设备驱动层：返回码 + 日志
`ICanDevice::open()` / `CanDeviceZLG::open()` / `CanDevicePEAK::...` / `CanDeviceKvaser::...` 均返回 `bool`。DLL 未找到、函数符号缺失、`OpenDevice`/`InitCAN`/`StartCAN` 失败时，先 `SIN_LOG_ERROR` 记录具体原因，再 `return false`。`CanDeviceManager::start()` 检查返回值后 `emit errorOccurred(...)` 通知 UI。

### 3.2 配置/数据层：异常捕获 + 降级
`AppConfig`、`ProjectManager`、`SessionManager` 在 `json::parse(...)` 周围包裹 `try { ... } catch (const json::parse_error &e) { spdlog::error(...); 使用默认值; }`，保证单份配置损坏不会导致应用启动失败。

### 3.3 UI 层：信号 + 弹窗
`CanDeviceManager` 暴露 `errorOccurred(QString)` 信号，由 MainWindow 连接并显示 `QMessageBox`。对于非致命错误（如 DBC 导入失败），直接在对话框内 `QMessageBox::warning` 提示。

### 3.4 文件解析层：宽松容错
BLF/ASC/CSV 导入器在文件无法打开时 `qWarning()` 并返回空容器；解析过程中遇到未知字段或压缩失败时记录警告但继续读取剩余内容，尽可能多地提取有效帧。

### 3.5 顶层兜底
`MainWindow` 中对 JSON 预览等可能抛异常的代码使用 `catch (...) {}` 吞掉异常，确保 UI 操作不会因第三方库异常而崩溃。

## 4. 约定与约束

- **禁止在核心 I/O 路径上抛出异常**：设备打开、文件读取、DBC 解析等关键路径一律返回 `bool`/空容器，异常仅在配置加载等可安全降级的场景中使用。
- **所有错误必须带模块标签**：通过 `SIN_LOG_*` 宏的第一个参数（如 `"CanDeviceZLG"`、`"ICanDevice"`、`"AppConfig"`）标识来源，便于日志检索。
- **UI 层不直接 throw**：UI 组件只消费信号和返回值，不向上抛异常；对不可恢复错误通过 `QMessageBox` 告知用户。
- **调试输出与正式日志分离**：开发期用 `qDebug()/qWarning()` 快速定位问题（如 BLF 解析细节），生产路径用 `SIN_LOG_*` 写入滚动日志文件。
- **无全局错误码枚举**：错误语义由返回值 + 日志消息共同表达，新增错误需同时更新日志文案。
- **线程安全**：接收线程中不抛异常，仅入队帧；主线程定时器消费队列，异常隔离在 UI 层。

## 5. 待改进点（观察到的不一致）

- 文件导入层混用 `qWarning()` 与 `SIN_LOG_*`，建议统一到 `SIN_LOG_WARN`。
- `canfileio` 子模块尚未引入 `logging.h`，仍依赖 Qt 调试输出。
- 缺少统一的错误类型（如 `Result<T, E>`），调用方需记忆每个 API 的错误语义（false vs 空容器 vs 异常）。
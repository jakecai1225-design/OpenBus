---
kind: error_handling
name: Qt 信号 + spdlog 日志的错误处理体系
category: error_handling
scope:
    - '**'
source_files:
    - src/core/logging.h
    - src/core/logging.cpp
    - src/utils/logging.h
    - src/core/candevicemanager.h
    - src/core/candevicemanager.cpp
    - src/core/candevice.h
    - src/ui/mainwindow.cpp
    - src/ui/dbcimportdialog.cpp
---

## 1. 整体方案

本项目采用 Qt 信号/槽作为跨层错误传播机制，配合基于 spdlog 的结构化日志系统（SIN_LOG_* 宏）进行诊断记录；UI 层通过 QMessageBox 向用户呈现关键错误。代码中几乎不使用 C++ 异常（throw/catch），也不使用 qDebug/qWarning/qCritical 等 Qt 内置日志，而是统一走 logging.h 暴露的宏。

## 2. 核心文件与职责

- src/core/logging.h / src/core/logging.cpp：定义 logging::init()、shutdown()、logger() 以及 SIN_LOG_DEBUG/INFO/WARN/ERROR 四个级别宏；初始化时创建控制台彩色 sink + 滚动文件 sink（5MB × 3 个文件，目录为 QStandardPaths::AppDataLocation/logs，不可写时回退到 ./logs）。
- src/utils/logging.h：仅重新 include core/logging.h，提供统一入口。
- src/core/candevicemanager.h：声明 errorOccurred(const QString&) 信号，作为设备层向上报告的统一通道。
- src/ui/mainwindow.cpp：在构造函数中连接 m_deviceManager->errorOccurred 到 m_bottomPanel->appendOutput(...)，将底层错误以文本形式追加到底部输出面板。
- src/ui/dbcimportdialog.cpp：调用 DbcManager::loadDbc() 失败时直接弹出 QMessageBox::warning(this, "导入失败", ...) 提示用户。

## 3. 架构与约定

### 3.1 设备层 → 管理层 → UI 层的错误传播链

ICanDevice (ZLG/PEAK/Kvaser …) 返回 bool/int 或内部日志，CanDeviceManager::start() 判断后 emit errorOccurred("无法打开设备: …")，MainWindow 接收并 appendOutput 到底部面板。

- ICanDevice::open() 返回 bool，send() 返回发送帧数（0=失败），由 CanDeviceManager 判断后 emit errorOccurred(...)
- DbcManager::loadDbc() 返回 bool，调用方根据返回值决定是否弹窗
- 所有硬件访问路径都通过 SIN_LOG_WARN/INFO/ERROR 记录上下文信息（如 DLL 路径、设备名），便于离线排查

### 3.2 日志级别约定

- SIN_LOG_INFO：正常流程事件（线程启动、DLL 找到、连接建立）
- SIN_LOG_WARN：可恢复异常（DLL 存在但位数不匹配、文件不可写）
- SIN_LOG_ERROR：严重错误（日志写入失败、设备打开失败）
- SIN_LOG_DEBUG：调试细节（默认开启 trace 级文件日志）

### 3.3 资源加载失败的容错

logging::init() 对日志文件写入做了 try-catch 保护：若 QStandardPaths::writableLocation 不可写，自动降级到当前目录 ./logs/sin.log；若仍失败则仅保留控制台 sink，保证应用不会因日志初始化失败而崩溃。

## 4. 约束与模式总结

- 禁止抛异常：整个 src/ 下未出现 throw 语句，错误一律通过返回值（bool/int）+ 信号 + 日志传递
- 统一日志入口：禁止直接使用 qDebug/qWarning/qFatal，必须通过 SIN_LOG_* 宏，确保格式一致且带模块 tag
- UI 层错误展示分层：可恢复的用户操作错误用 QMessageBox 即时反馈（如 DBC 导入失败）；底层/异步错误通过 errorOccurred 信号进入底部输出面板，避免阻塞主线程
- 无全局错误码枚举：错误信息以 QString 字符串形式经信号传递，由上层决定如何解释和展示
- 第三方库异常隔离：spdlog 的 spdlog_ex 被捕获并降级处理，确保日志子系统故障不影响主程序运行

## 5. 适用边界

该模式覆盖 CAN 设备管理、DBC 导入、日志系统等核心路径；对于纯计算型函数（如 DbcSignal::encode/decode）目前直接返回结果而不做参数校验报错，属于可接受的简化策略。
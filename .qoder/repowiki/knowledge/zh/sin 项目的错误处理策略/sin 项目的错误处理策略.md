---
kind: error_handling
name: sin 项目的错误处理策略
category: error_handling
scope:
    - '**'
source_files:
    - src/main.cpp
    - src/core/canframe.h
    - src/utils/canutils.cpp
    - src/ui/mainwindow.cpp
    - src/ui/panels/sidebarpanels.cpp
---

该 CAN/CAN FD 报文分析工具 sin 采用**轻量级、UI 直接反馈的错误处理方式**，没有统一的异常框架或错误类型系统。核心特点如下：

## 1. 无统一异常机制
- 项目未使用 C++ 异常（`throw/catch`）、Qt 异常或自定义错误码枚举
- 所有 I/O 操作和解析逻辑通过返回值（布尔/空值）表示成功或失败
- 例如 `parseHex()` 在解析失败时返回 `0xFFFFFFFF` 作为哨兵值

## 2. UI 层直接弹窗反馈
- 所有用户可见的错误都通过 `QMessageBox` 直接弹出对话框
- 主要使用 `QMessageBox::warning()` 显示错误信息，`QMessageBox::question()` 获取确认
- 典型场景：文件创建失败、文件加载失败、删除工程确认等
- 示例：`QMessageBox::warning(this, "录制", "无法创建文件: " + path)`

## 3. 状态字段标记错误帧
- `CanFrame` 结构体包含 `errorState` 布尔字段标识 CAN FD ESI 错误状态
- `isErrorFrame()` 方法通过检查 `CAN_ERR_FLAG` 位判断是否为错误帧
- 这些是数据层面的错误标记，不是程序执行错误

## 4. 解析器内部状态标记
- 过滤器解析器使用 `m_ok` 布尔成员跟踪解析状态
- 解析失败时返回空的 `FilterPredicate`，调用方通过 `ok()` 方法检查
- 这种设计避免了异常传播，保持解析逻辑简洁

## 5. 底部面板记录问题
- `BottomPanel` 的 `addProblem()` 方法用于记录非致命问题
- 与 `QMessageBox` 不同，这是静默记录，不打断用户操作
- 适用于需要记录但不立即提示用户的错误场景

## 6. 关键约束
- **禁止使用异常**：整个代码库未发现任何 `try/catch/throw` 语句
- **UI 错误必须弹窗**：用户可感知的错误都应通过 `QMessageBox` 明确告知
- **I/O 失败需检查返回值**：文件操作后必须检查打开/写入是否成功
- **解析失败返回空对象**：解析函数失败时返回默认构造的空对象而非抛出异常
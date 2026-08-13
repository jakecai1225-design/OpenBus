# Python SDK开发指南

<cite>
**本文引用的文件**
- [sdk/sin/__init__.py](file://sdk/sin/__init__.py)
- [sdk/sin/_transport.py](file://sdk/sin/_transport.py)
- [sdk/sin/frames.py](file://sdk/sin/frames.py)
- [sdk/sin/commands.py](file://sdk/sin/commands.py)
- [sdk/sin/output.py](file://sdk/sin/output.py)
- [sdk/sin/workspace.py](file://sdk/sin/workspace.py)
- [sdk/sin/signals.py](file://sdk/sin/signals.py)
- [plugins/frame-counter/main.py](file://plugins/frame-counter/main.py)
- [plugins/hello-world/main.py](file://plugins/hello-world/main.py)
- [plugins/ui-demo/main.py](file://plugins/ui-demo/main.py)
- [plugins/frame-counter/plugin.json](file://plugins/frame-counter/plugin.json)
- [plugins/hello-world/plugin.json](file://plugins/hello-world/plugin.json)
- [README.md](file://README.md)
</cite>

## 目录
1. [简介](#简介)
2. [项目结构](#项目结构)
3. [核心组件](#核心组件)
4. [架构总览](#架构总览)
5. [详细组件分析](#详细组件分析)
6. [依赖关系分析](#依赖关系分析)
7. [性能与可靠性](#性能与可靠性)
8. [故障排查](#故障排查)
9. [结论](#结论)
10. [附录：插件示例与最佳实践](#附录插件示例与最佳实践)

## 简介
本指南面向希望基于 sin 主程序扩展功能的开发者，系统讲解 Python SDK 的接口设计、通信机制、数据模型以及插件开发流程。通过该 SDK，插件可以：
- 接收总线帧事件并统计、处理
- 发送 CAN/CAN FD 报文
- 调用主程序命令
- 读取工程上下文（工程目录、DBC 列表、设置）
- 使用 DBC 进行信号解码/编码
- 可选地创建独立 PyQt6 窗口进行交互

SDK 采用 JSON-RPC 通过标准输入输出与主进程通信，保证插件与宿主解耦且易于调试。

## 项目结构
Python SDK 位于 sdk/sin 目录，提供统一的 API 入口；插件位于 plugins 目录，每个插件包含 main.py 和 plugin.json。

```mermaid
graph TB
A["插件代码<br/>plugins/*/main.py"] --> B["sin 模块入口<br/>sdk/sin/__init__.py"]
B --> C["传输层<br/>_transport.py"]
B --> D["帧API<br/>frames.py"]
B --> E["命令API<br/>commands.py"]
B --> F["输出API<br/>output.py"]
B --> G["工作区API<br/>workspace.py"]
B --> H["信号API<br/>signals.py"]
C --> I["主程序JSON-RPC服务"]
```

图表来源
- [sdk/sin/__init__.py:1-31](file://sdk/sin/__init__.py#L1-L31)
- [sdk/sin/_transport.py:1-72](file://sdk/sin/_transport.py#L1-L72)
- [sdk/sin/frames.py:1-92](file://sdk/sin/frames.py#L1-L92)
- [sdk/sin/commands.py:1-23](file://sdk/sin/commands.py#L1-L23)
- [sdk/sin/output.py:1-26](file://sdk/sin/output.py#L1-L26)
- [sdk/sin/workspace.py:1-51](file://sdk/sin/workspace.py#L1-L51)
- [sdk/sin/signals.py:1-56](file://sdk/sin/signals.py#L1-L56)

章节来源
- [sdk/sin/__init__.py:1-31](file://sdk/sin/__init__.py#L1-L31)
- [README.md:1-800](file://README.md#L1-L800)

## 核心组件
- 传输层 _transport：封装 JSON-RPC 通知与请求，维护请求队列与超时，线程安全写入 stdout。
- frames：CAN/CAN FD 帧对象与操作（获取选中帧、最近帧、发送帧）。
- commands：执行主程序注册的命令。
- output：向主程序输出面板追加或清空文本。
- workspace：获取工程目录、已加载 DBC 列表与应用设置。
- signals：基于 DBC 对 CAN 帧进行信号级解码与编码。
- ui（可选）：当存在 PyQt6 时暴露 UI 能力，用于创建独立窗口。

章节来源
- [sdk/sin/_transport.py:1-72](file://sdk/sin/_transport.py#L1-L72)
- [sdk/sin/frames.py:1-92](file://sdk/sin/frames.py#L1-L92)
- [sdk/sin/commands.py:1-23](file://sdk/sin/commands.py#L1-L23)
- [sdk/sin/output.py:1-26](file://sdk/sin/output.py#L1-L26)
- [sdk/sin/workspace.py:1-51](file://sdk/sin/workspace.py#L1-L51)
- [sdk/sin/signals.py:1-56](file://sdk/sin/signals.py#L1-L56)
- [sdk/sin/__init__.py:1-31](file://sdk/sin/__init__.py#L1-L31)

## 架构总览
插件通过 sin 模块提供的 API 与主程序通信。所有跨进程调用均经由 _transport 层以 JSON-RPC 形式完成，支持同步请求（带超时）与异步通知。

```mermaid
sequenceDiagram
participant P as "插件"
participant S as "sin 模块"
participant T as "_transport"
participant H as "主程序"
P->>S : frames.get_selected()
S->>T : send_request("frames.getSelected")
T-->>H : JSON-RPC 请求
H-->>T : JSON-RPC 响应
T-->>S : deliver_response()
S-->>P : Frame[] / []
P->>S : frames.send(id, data, extended, fd)
S->>T : send_notification("sendFrame", {...})
T-->>H : 通知
```

图表来源
- [sdk/sin/frames.py:42-88](file://sdk/sin/frames.py#L42-L88)
- [sdk/sin/_transport.py:39-72](file://sdk/sin/_transport.py#L39-L72)

## 详细组件分析

### 传输层 _transport
- 职责：统一发送通知与请求，管理请求 ID、等待队列与超时，线程安全写 stdout。
- 关键方法：
  - send_notification(method, params=None)：无返回的通知。
  - send_request(method, params=None, timeout=5.0)：阻塞等待响应，超时返回 None。
  - deliver_response(msg_id, result, error=None)：由宿主在主循环中调用，将响应投递给对应请求。
- 并发与健壮性：
  - 使用锁保护 pending_requests 与 stdout 写入。
  - 请求超时后清理队列，避免内存泄漏。

```mermaid
flowchart TD
Start(["进入 send_request"]) --> GenId["生成唯一请求ID"]
GenId --> Enqueue["创建队列并注册到pending_requests"]
Enqueue --> SendMsg["序列化并写入stdout"]
SendMsg --> Wait{"等待响应"}
Wait --> |收到| Deliver["deliver_response投递结果"]
Wait --> |超时| Cleanup["移除pending并返回None"]
Deliver --> End(["返回{result,error}"])
Cleanup --> End
```

图表来源
- [sdk/sin/_transport.py:23-72](file://sdk/sin/_transport.py#L23-L72)

章节来源
- [sdk/sin/_transport.py:1-72](file://sdk/sin/_transport.py#L1-L72)

### 帧操作 frames
- Frame 数据模型：id、extended、fd、dlc、data、timestamp、channel、direction。
- 主要能力：
  - get_selected()：获取 Trace 中选中的帧列表。
  - get_recent(count=100)：获取最近 N 帧。
  - send(id, data, extended=False, fd=False)：发送一帧（支持 bytes 或 hex 字符串）。
- 复杂度与注意事项：
  - 获取帧为 O(N) 构造 Frame 对象，N 为返回帧数。
  - 发送帧为异步通知，不阻塞。

```mermaid
classDiagram
class Frame {
+int id
+bool extended
+bool fd
+int dlc
+bytes data
+float timestamp
+int channel
+string direction
}
class _Frames {
+get_selected() Frame[]
+get_recent(count) Frame[]
+send(id, data, extended, fd) void
}
_Frames --> Frame : "创建/返回"
```

图表来源
- [sdk/sin/frames.py:9-92](file://sdk/sin/frames.py#L9-L92)

章节来源
- [sdk/sin/frames.py:1-92](file://sdk/sin/frames.py#L1-L92)

### 命令操作 commands
- 能力：execute(command_id, *args) 触发主程序已注册的命令。
- 特点：仅发送通知，不等待结果。

章节来源
- [sdk/sin/commands.py:1-23](file://sdk/sin/commands.py#L1-L23)

### 输出面板 output
- 能力：append(text)、clear()。
- 用途：在底部“插件输出”标签页显示日志或状态信息。

章节来源
- [sdk/sin/output.py:1-26](file://sdk/sin/output.py#L1-L26)

### 工作区 workspace
- 能力：
  - get_project_dir()：当前工程目录路径。
  - get_dbc_files()：已加载 DBC 文件列表。
  - get_setting(key, default=None)：读取应用设置。
- 返回值：均为空值或默认值时的安全降级。

章节来源
- [sdk/sin/workspace.py:1-51](file://sdk/sin/workspace.py#L1-L51)

### 信号解码/编码 signals
- 能力：
  - decode(can_id, data)：根据 DBC 将原始数据解码为信号名→值的映射。
  - encode(can_id, signal_values)：将信号值编码为 bytes。
- 注意：若无匹配 DBC 或编码失败，返回空字典或 None。

章节来源
- [sdk/sin/signals.py:1-56](file://sdk/sin/signals.py#L1-L56)

### UI 能力（可选）
- 当安装 PyQt6 时，sin.ui 可用，可创建独立窗口、控件与事件绑定。
- 未安装时自动跳过，不影响基础功能。

章节来源
- [sdk/sin/__init__.py:20-31](file://sdk/sin/__init__.py#L20-L31)

## 依赖关系分析
- 模块耦合：
  - frames、commands、output、workspace、signals 均依赖 _transport。
  - __init__ 聚合导出各模块，供插件 import sin 直接访问。
- 外部依赖：
  - PyQt6 为可选依赖，仅在 UI 相关场景需要。
- 插件与 SDK：
  - 插件通过 context.on_frame 与 context.register_command 与宿主交互（见插件示例）。
  - 插件通过 sin.output、sin.frames、sin.commands、sin.workspace、sin.signals 调用 SDK。

```mermaid
graph LR
subgraph "插件"
PC["frame-counter/main.py"]
PH["hello-world/main.py"]
PU["ui-demo/main.py"]
end
subgraph "SDK"
SIN["sin.__init__"]
TR["_transport"]
FR["frames"]
CM["commands"]
OP["output"]
WS["workspace"]
SG["signals"]
end
PC --> SIN
PH --> SIN
PU --> SIN
SIN --> TR
SIN --> FR
SIN --> CM
SIN --> OP
SIN --> WS
SIN --> SG
```

图表来源
- [sdk/sin/__init__.py:14-31](file://sdk/sin/__init__.py#L14-L31)
- [plugins/frame-counter/main.py:16-21](file://plugins/frame-counter/main.py#L16-L21)
- [plugins/hello-world/main.py:11-15](file://plugins/hello-world/main.py#L11-L15)
- [plugins/ui-demo/main.py:15-23](file://plugins/ui-demo/main.py#L15-L23)

章节来源
- [sdk/sin/__init__.py:14-31](file://sdk/sin/__init__.py#L14-L31)
- [plugins/frame-counter/main.py:16-21](file://plugins/frame-counter/main.py#L16-L21)
- [plugins/hello-world/main.py:11-15](file://plugins/hello-world/main.py#L11-L15)
- [plugins/ui-demo/main.py:15-23](file://plugins/ui-demo/main.py#L15-L23)

## 性能与可靠性
- 传输层性能：
  - 请求-响应为同步阻塞，建议合理设置 timeout，避免长时间占用线程。
  - stdout 写入加锁，确保多并发下消息顺序正确。
- 帧处理：
  - get_recent/get_selected 会构造多个 Frame 对象，批量处理时注意内存与 CPU 开销。
  - send 为异步通知，适合高频发送场景。
- 错误恢复：
  - 请求超时返回 None，调用方需判空处理。
  - 解码/编码失败返回空字典/None，应做防御式编程。

[本节为通用指导，不直接分析具体文件]

## 故障排查
- 无法收到响应：
  - 检查主程序是否正常运行并已实现对应 JSON-RPC 方法。
  - 确认 _transport.deliver_response 被宿主正确调用。
- 输出面板无内容：
  - 确认 output.append 的参数为字符串类型（内部会自动转换）。
- 帧发送无效：
  - 检查 CAN ID、数据格式（bytes 或 hex）、extended/fd 标志是否符合设备能力。
- DBC 解码为空：
  - 确认已加载匹配的 DBC 文件，且 can_id 与信号定义一致。
- UI 不可用：
  - 未安装 PyQt6 时，UI 模块将被跳过，需 pip install PyQt6。

章节来源
- [sdk/sin/_transport.py:44-72](file://sdk/sin/_transport.py#L44-L72)
- [sdk/sin/output.py:12-22](file://sdk/sin/output.py#L12-L22)
- [sdk/sin/frames.py:69-88](file://sdk/sin/frames.py#L69-L88)
- [sdk/sin/signals.py:12-52](file://sdk/sin/signals.py#L12-L52)
- [sdk/sin/__init__.py:20-31](file://sdk/sin/__init__.py#L20-L31)

## 结论
sin Python SDK 提供了简洁稳定的插件扩展能力，通过 JSON-RPC 与主程序解耦通信。开发者可快速实现帧监听、报文发送、工程上下文读取、DBC 信号级处理以及可选的独立 UI。结合示例插件，可在短时间内构建实用的分析工具与自动化脚本。

[本节为总结性内容，不直接分析具体文件]

## 附录：插件示例与最佳实践

### 插件生命周期与事件
- activate(context)：插件激活时初始化，注册命令与事件回调。
- deactivate()：插件卸载时清理资源。
- on_frame(frame)：每收到一帧触发，适合统计、过滤、转发等逻辑。
- register_command(id, handler, title)：注册命令，便于从 UI 或脚本触发。

章节来源
- [plugins/frame-counter/main.py:16-21](file://plugins/frame-counter/main.py#L16-L21)
- [plugins/hello-world/main.py:11-15](file://plugins/hello-world/main.py#L11-L15)

### 插件配置 plugin.json
- name/version/author/description：插件元信息。
- main：入口脚本。
- activationEvents：激活事件，如 onStartup、onFrame、onCommand:...。
- contributes.commands：声明命令以便 UI 展示。

章节来源
- [plugins/frame-counter/plugin.json:1-18](file://plugins/frame-counter/plugin.json#L1-L18)
- [plugins/hello-world/plugin.json:1-17](file://plugins/hello-world/plugin.json#L1-L17)

### 常用模式与示例路径
- 帧统计：参考 frame-counter 插件，按 ID 分类计数并输出报告。
- 简单问候：参考 hello-world 插件，演示命令注册与输出。
- 独立 UI：参考 ui-demo 插件，展示 PyQt6 窗口、按钮点击发送帧、实时显示接收帧。

章节来源
- [plugins/frame-counter/main.py:23-46](file://plugins/frame-counter/main.py#L23-L46)
- [plugins/hello-world/main.py:17-24](file://plugins/hello-world/main.py#L17-L24)
- [plugins/ui-demo/main.py:15-106](file://plugins/ui-demo/main.py#L15-L106)

### 最佳实践
- 使用 frames.get_recent 控制批量大小，避免一次性拉取过多帧导致卡顿。
- 发送帧时使用 bytes 或规范化的 hex 字符串，减少解析错误。
- 对 signals.decode/encode 的结果做空值判断，增强鲁棒性。
- 在 activate 中注册必要命令与回调，在 deactivate 中释放资源。
- 如需 UI，确保 PyQt6 已安装，并在导入失败时给出友好提示。

[本节为通用指导，不直接分析具体文件]
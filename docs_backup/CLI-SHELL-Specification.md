# OpenBUS CLI Shell Specification v2.0

## 📋 概述

OpenBUS CLI Shell 是一个基于 **JSON-RPC 2.0** 的远程过程调用接口，允许通过 TCP/IP 网络控制 OpenBUS 的所有功能。设计目标包括：

- **标准化协议**: JSON-RPC 2.0 + TCP 传输
- **全功能覆盖**: 对标 VSCode API，开放所有核心功能
- **AI Agent 友好**: 结构化指令集，支持自动化控制
- **调试工具兼容**: Postman/curl/Python SDK 任意选择

---

## 🏗️ 架构设计

```
┌─────────────────────────────────────┐
│         External Clients            │
│  (Python/Node.js/Postman/CURL)      │
└──────────────┬──────────────────────┘
               │ JSON-RPC over TCP
┌──────────────▼──────────────────────┐
│   TcpRpcServer (Port 5555)          │
│  ├─ Request Router                  │
│  └─ Auth & Rate Limiter             │
└──────────┬──────────────────────────┘
           │ Command Dispatching
┌──────────▼──────────────────────────┐
│   Core Function Modules             │
│  ├─ CanTraceController              │
│  ├─ DbcManager                      │
│  ├─ FileIOController                │
│  ├─ RecordingController             │
│  ├─ PlaybackController              │
│  └─ SettingsController              │
└─────────────────────────────────────┘
```

---

## 🔧 技术栈

| 组件 | 选择 | 理由 |
|------|------|------|
| **传输层** | QTcpServer (Qt6) | 无 AUTOMOC 问题，稳定成熟 |
| **协议格式** | JSON-RPC 2.0 | 国际标准 RFC，工具生态丰富 |
| **JSON 库** | nlohmann/json | 项目已集成，单头文件轻量 |
| **鉴权** | Token-based (可选) | 保护本地端口不被外部访问 |
| **速率限制** | Token Bucket | 防止高频请求阻塞 UI 线程 |

---

## 📦 可用 RPC 方法清单

### 1. **CanTrace API** - 帧数据操作

#### `trace.list(params)` - 列出所有帧
```json
{
  "jsonrpc": "2.0",
  "method": "trace.list",
  "params": {
    "skip": 0,
    "limit": 100,
    "channel": 1,
    "filter": "id > 500"
  },
  "id": 1
}
```
响应:
```json
{
  "jsonrpc": "2.0",
  "result": {
    "total": 1000,
    "frames": [
      {"no": 1, "time": 1234567.89, "channel": 1, "id": 513, "dir": "R", "dlc": 8, "data": "ABCD1234", "flags": 0},
      ...
    ]
  },
  "id": 1
}
```

#### `trace.count(params)` - 统计帧数
- `channel`: 通道过滤
- `id_range`: 报文 ID 范围 `[start, end]`
- `from_time`, `to_time`: 时间范围

#### `trace.clear()` - 清空缓冲区

#### `trace.export(path, format)` - 导出文件
- `format`: `asc` / `blf` / `csv` / `json`
- `filter`: SQL-like 表达式

#### `trace.import(path, format)` - 导入文件
- 支持 BLF/ASC/CSV/TRC 批量加载

#### `trace.subscribe(callback_uri?)` - 订阅实时流
- WebSocket/回调 URL 推送新帧

---

### 2. **DBC API** - 数据库管理

#### `dbc.list()` - 列出所有已加载 DBC
- 返回 `[{name, version, messages: [], signals: []}]`

#### `dbc.load(path)` - 加载 DBC 文件
- 解析并构建信号树

#### `dbc.unlink(name)` - 卸载 DBC

#### `dbc.signals(message_name)` - 获取某报文的所有信号
- 用于信号级解码查询

---

### 3. **File I/O API** - 工程与数据管理

#### `file.open(project_path)` - 打开工程
- `.obp` 文件格式

#### `file.save()` / `file.save_as(path)` - 保存工程

#### `file.new()` - 新建空白工程

#### `file.info()` - 获取当前工程信息

#### `file.recent_list()` - 最近打开记录

---

### 4. **Recording API** - 录制控制

#### `record.start(device_id)` - 开始录制
- 自动保存到配置路径

#### `record.stop()` - 停止录制

#### `record.pause()` / `record.resume()` - 暂停/恢复

#### `record.status()` - 查询状态 (`recording`, `paused`, `idle`)

#### `record.config(path, max_size_mb, rotate_count)` - 配置录制参数

---

### 5. **Playback API** - 回放控制

#### `playback.load(file_path)` - 加载回放文件
- 支持 BLF/ASC/CSV/TRC

#### `playback.play(rate=1.0)` - 开始回放
- `rate`: 倍速 (0.1 ~ 10.0)

#### `playback.pause()` / `playback.resume()` - 暂停/继续

#### `playback.stop()` - 停止回放

#### `playback.seek(time_offset)` - 跳转到指定时间

#### `playback.config(loop, stop_at_end)` - 配置行为

---

### 6. **Device API** - 硬件设备管理

#### `device.list()` - 列出所有连接的设备
- 返回 `[{"id": "zlg_1", "vendor": "ZLG", "status": "connected"}, ...]`

#### `device.connect(id)` - 连接设备

#### `device.disconnect(id)` - 断开设备

#### `device.send_can(id, frame)` - 发送 CAN 帧

#### `device.send_can_fd(id, frame)` - 发送 CAN FD 帧

---

### 7. **Settings API** - 配置管理

#### `settings.get(key_path)` - 获取单个设置值
- 嵌套路径：`"ui.theme"`, `"trace.max_frames"`

#### `settings.set(key_path, value)` - 设置值

#### `settings.reset()` - 重置所有为默认

#### `settings.export(json_path)` - 导出配置

#### `settings.import(json_path)` - 导入配置

---

### 8. **UI Layout API** - 界面布局控制

#### `ui.toggle_sidebar(side: "left"|"right"|"bottom")` - 切换侧边栏可见性
- 参考 VSCode 折叠面板

#### `ui.show_panel(panel: "output"|"terminal"|"debug")` - 显示面板

#### `ui.hide_panel(panel)` - 隐藏面板

#### `ui.get_layout()` - 获取当前布局状态

#### `ui.save_layout(name)` - 保存自定义布局

#### `ui.load_layout(name)` - 加载自定义布局

---

### 9. **Market API** - 市场插件管理

#### `market.search(query)` - 搜索插件
- 支持关键词、分类筛选

#### `market.install(plugin_id)` - 安装插件
- 下载并解压 .opk 包

#### `market.uninstall(plugin_id)` - 卸载插件

#### `market.update(plugin_id)` - 检查更新

#### `market.enabled_plugins()` - 列出已启用插件

---

### 10. **Driver Market API** - 驱动市场

#### `driver.list()` - 列出所有已安装驱动
- ZLG/PEAK/KVASER/CANDLE/SLCAN

#### `driver.search(query)` - 搜索驱动

#### `driver.install(zip_file)` - 安装驱动包 (.odp)

#### `driver.uninstall(driver_id)` - 卸载驱动

#### `driver.refresh()` - 刷新市场索引

---

### 11. **System API** - 系统信息

#### `system.version()` - 获取版本信息
- `{openbus: "0.1.0", qt: "6.8.3", os: "Windows 11"}`

#### `system.memory()` - 内存使用情况

#### `system.cpu()` - CPU 使用率

#### `system.log(level="info")` - 获取日志
- `level`: trace/debug/info/warn/error

#### `system.shutdown()` - 关闭程序
- 安全退出，保存状态

---

## 🚀 实施路线图

### Phase 1: 基础框架 (✅ 已完成)
- [x] TcpRpcServer + JSON-RPC Handler
- [x] TCP Server 监听端口 5555
- [x] Lambda 模式无 MOC 依赖

### Phase 2: 核心命令注册 (进行中)
- [ ] `trace.list/count/clear`
- [ ] `dbc.load/list/unlink`
- [ ] `file.open/save`
- [ ] `record.start/stop/status`

### Phase 3: 完整功能覆盖 (Week 2)
- [ ] Playback/Device/Settings API
- [ ] Market/Driver 市场接口
- [ ] System API

### Phase 4: UI 集成 (Week 3)
- [ ] 右上角折叠/展开按钮
- [ ] `ui.toggle_*` RPC 命令实现
- [ ] Python SDK + 示例脚本

### Phase 5: 测试与文档 (Week 4)
- [ ] L1 单元测试 (gtest)
- [ ] L2 端到端集成测试
- [ ] Python/Node.js SDK
- [ ] API 文档 + 交互式测试页

---

## 🛠️ UI 集成设计

### VSCode 风格折叠按钮

在 `MainWindow`右上角添加 3 个图标按钮：

```cpp
QToolButton *leftBtn = new QToolButton();
leftBtn->setIcon(QIcon(":/icons/sidebar_left"));
leftBtn->setToolTip("Toggle Left Sidebar");
connect(leftBtn, &QToolButton::clicked, this, [=]() {
    emit toggleSidebar("left");
});

QToolButton *bottomBtn = new QToolButton();
bottomBtn->setIcon(QIcon(":/icons/sidebar_bottom"));
bottomBtn->setToolTip("Toggle Bottom Panel");

QToolButton *rightBtn = new QToolButton();
rightBtn->setIcon(QIcon(":/icons/sidebar_right"));
rightBtn->setToolTip("Toggle Right Sidebar");
```

**RPC 命令**:
```json
{
  "jsonrpc": "2.0",
  "method": "ui.toggle_sidebar",
  "params": {"side": "right"},
  "id": 1
}
```

---

## 📚 参考资源

- JSON-RPC 2.0 Spec: https://www.jsonrpc.org/specification
- VSCode Extension API: https://code.visualstudio.com/api
- Qt Network Docs: https://doc.qt.io/qt-6/qnetworkaccessmanager.html

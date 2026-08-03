# sin — CAN/CAN FD 报文分析工具

<p align="center">
  <strong>专业的 CAN/CAN FD 总线报文录制、回放、解析与分析桌面软件</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Qt-6.8.3-green" alt="Qt 6.8.3">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17">
  <img src="https://img.shields.io/badge/CMake-3.21+-orange" alt="CMake">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey" alt="Platform">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
</p>

---

## 简介

**sin** 是一款灵感来源于 [Wireshark](https://www.wireshark.org/)、[CANoe](https://www.vector.com/canoe)、[Ozone](https://www.segger.com/products/development-tools/ozone-debugger/) 等优秀软件的 CAN/CAN FD 总线报文分析工具。采用 VS Code 风格的现代化 UI 设计，提供从报文录制到信号级解析的完整工作流，适用于汽车电子开发、总线调试、协议逆向等场景。

## 功能特性

### 报文录制与回放
- **实时录制** — 连接 CAN 设备实时捕获总线报文，支持 CAN 2.0A/B 与 CAN FD
- **文件回放** — 加载 BLF/ASC/CSV/PCAP/TRC 报文文件，按原始时间戳精准回放，支持变速控制（0.1x ~ 10x）
- **进度拖拽** — 回放进度条可任意拖拽定位，快速跳转到关键时间点
- **文件导入** — 支持从 `.blf`（Vector 二进制日志）、`.asc`（Vector ASCII 日志）、`.csv` 文件导入报文到 Trace
- **文件导出** — 支持将 Trace 报文导出为 `.asc`、`.csv` 格式

### Trace 追踪
- **帧编号列 (No.)** — 参考 Wireshark，首列为帧序号，标识当前帧在捕获序列中的位置
- **绝对/相对时间** — 除绝对时间戳外，支持显示与上一帧的时间增量 (Delta)，方便排查周期不稳定等问题
- **Wireshark 风格列表** — No. / Time / Delta / Ch / Dir / ID / DLC / Data / Flags / Count 多列显示
- **按列排序** — 点击列标题升序/降序排序，支持数值与时间类型智能比较
- **表头漏斗过滤** — 鼠标悬停列标题时显示漏斗图标，点击即可快速设置该列筛选条件；已激活筛选的列持续显示高亮漏斗
- **按列筛选** — 每列独立筛选条件，ID 列支持 `>`、`<`、`!=` 运算符，快速过滤目标报文
- **快速筛选** — 右键列标题一键筛选仅 Rx/Tx、唯一 ID 列表
- **帧信息面板** — 选中报文显示完整解码字段，集成 DBC 信号级解析
- **十六进制转储** — 选中报文以 Offset + Hex + ASCII 格式展示原始数据
- **覆盖模式** — 同 CAN ID 只保留最新一帧，实时刷新数据值和帧计数
- **表达式过滤** — 类 Wireshark 过滤语法 `id == 0x123 && dlc > 4`，自写递归下降解析器
- **行标记与着色** — 过滤后可对结果行手动标记、自定义着色，方便分析对比；参考 Wireshark/CANoe 的标记着色机制
- **自动行着色** — 按 CAN ID / 方向 / 错误帧自动着色，视觉区分不同报文类型
- **搜索定位** — 按 ID、数据内容（Hex 模式）、时间范围搜索并跳转到目标帧
- **标记书签** — 手动标记重要帧，书签列表快速跳转
- **统计概览** — 总线负载率、各 ID 帧数/频率、错误帧统计
- **文件导入** — 支持 BLF / ASC / CSV 格式导入，自动解析时间戳与数据
- **文件导出** — 导出 Trace 为 ASC / CSV 格式，支持导出过滤后子集

### DBC 信号解析
- **DBC 加载** — 支持加载标准 `.dbc` 文件，自动解析报文与信号定义
- **信号树浏览** — 侧边栏 DBC 面板以树形结构展示 Message → Signal 层级
- **信号解码** — 选中 Trace 报文自动解码所有信号值（支持有符号/无符号、大小端）
- **信号双击追踪** — 双击 DBC 信号自动添加到 Graphic 图形视图实时绘制

### Graphic 图形视图
- **实时波形** — 以时间轴为横坐标绘制信号值变化曲线
- **多信号叠加** — 同时监控多个信号，支持通道独立配置
- **交互缩放** — 鼠标滚轮缩放、拖拽平移，支持波形测量

### 工程上下文管理
- **多工程并行** — 创建多个分析工程，每个工程独立管理 CAN 配置、DBC 文件、布局
- **快速切换** — 一键切换工程上下文，无需重新加载文件
- **工程持久化** — 工程配置保存为 `.sinproj` 文件，下次打开即恢复工作状态

### VS Code 风格 UI
- **无边框窗口** — 去除原生标题栏，菜单栏直接置顶，最小化/最大化/关闭按钮位于菜单栏右上角
- **全局侧边栏** — ActivityBar + 可折叠 SideBar，按功能分组面板
- **可拆分编辑器** — 主区域标签页支持右键"向右拆分"/"向下拆分"，并排对比视图
- **全面板可停靠** — 左侧、右侧、底部、帧信息、HexDump 面板均可关闭、拖拽、停靠
- **Aero Snap** — 无边框窗口仍支持 Windows 原生窗口贴靠、边缘缩放

### 其他功能
- **命令行终端** — 内置命令行界面，支持 `help`、`clear`、`sim on/off`、`record`、`play`、`filter` 等命令
- **CAN 模拟器** — 内置报文模拟器，可生成测试报文用于功能验证
- **统计面板** — 实时显示总帧数、Rx/Tx 分布、CAN FD / Extended 统计
- **主题样式** — QSS 驱动的暗色菜单栏 + 亮色内容区，可自定义主题

---

## Trace 页面详细规划

> Trace 是总线分析工具的核心视图，对标 CANoe Trace 窗口 + Wireshark 报文列表。
> 以下按功能模块细化，明确已实现/待实现状态，并引入必要的开源组件减少重复造轮子。

### 一、已实现功能清单

| 功能 | 状态 | 实现位置 |
|------|------|----------|
| Wireshark 风格报文列表 | ✅ 已实现 | `TraceView` + `CanTraceModel` |
| 按列排序（数值/时间智能比较） | ✅ 已实现 | `CanFilterProxyModel::lessThan` |
| 按列筛选（ID 列支持运算符） | ✅ 已实现 | `CanFilterProxyModel::setColumnFilter` |
| 快速筛选（Rx/Tx、唯一 ID） | ✅ 已实现 | `TraceView::contextMenuEvent` |
| 帧信息面板（DBC 信号解码） | ✅ 已实现 | `FrameInfoWidget` + `SignalDecodeWidget` |
| 十六进制转储（Offset+Hex+ASCII） | ✅ 已实现 | `TraceTab` 底部面板 |
| 覆盖模式（同 ID 只保留最新帧） | ✅ 已实现 | `CanTraceModel::setOverwriteMode` |
| 表达式过滤引擎 | ✅ 已实现 | `FilterEngine`（自写递归下降解析器） |
| 多 Trace 标签页 | ✅ 已实现 | `SplitEditorArea` 多 Tab |

### 二、待实现功能规划

#### 1. 文件导入（高优先级）

支持从第三方日志文件导入报文到 Trace，复用现有 `CanFrame` 数据结构和 `CanTraceModel`。

| 格式 | 方案 | 协议 | 说明 |
|------|------|------|------|
| **BLF** | [vector_blf](https://github.com/Technica-Engineering/vector_blf) | LGPL | C++ 库，兼容 binlog API 7.1.0，支持 CAN/CAN FD/LIN 等多种对象类型 |
| **ASC** | 自写解析器 | — | 纯文本格式，每行一条报文记录，约 300 行代码即可实现 |
| **CSV** | 自写解析器 | — | 通用表格格式，约 200 行代码即可实现 |

**架构设计**：

```
core/file_import/
├── file_importer.h        # 统一导入接口（纯虚基类）
├── blf_importer.h/cpp     # BLF 格式导入（依赖 vector_blf）
├── asc_importer.h/cpp     # ASC 格式导入（自写文本解析）
└── csv_importer.h/cpp     # CSV 格式导入（自写文本解析）
```

- 所有导入器实现统一接口 `bool importFile(const QString &path, CanTraceModel *model)`
- 导入过程支持进度回调，大文件异步导入不阻塞 UI
- 导入后自动合并到当前 Trace 的 `CanTraceModel`，按时间戳排序

**ASC 格式说明**（Vector ASCII Logging Format）：

```
; comment
begin Triggerblock
   0.001  CAN  1  Rx  0123  8  01 02 03 04 05 06 07 08
   0.005  CAN  1  Tx  0456  4  AA BB CC DD
end Triggerblock
```

每行字段：时间戳(s) | 总线类型 | 通道 | 方向 | ID(hex) | DLC | 数据字节(hex)

#### 2. 文件导出（中优先级）

| 格式 | 方案 | 说明 |
|------|------|------|
| **ASC** | 自写导出器 | 按 Vector ASC 标准格式输出，可被 CANoe/CANalyzer 直接打开 |
| **CSV** | 自写导出器 | 通用 CSV 格式，Excel/Python 可直接分析 |
| **过滤子集导出** | 基于 FilterProxyModel | 仅导出当前过滤后可见行 |

#### 3. 行着色规则（中优先级）

通过 `CanTraceModel::data()` 的 `Qt::BackgroundRole` 实现行级背景色：

| 规则 | 颜色 | 说明 |
|------|------|------|
| 按 CAN ID 着色 | 哈希调色板（~20 色循环） | 不同 ID 不同背景色，快速识别报文类型 |
| 方向区分 | Rx: 无底色 / Tx: 浅绿底 | 收发方向一目了然 |
| 错误帧 | 浅红底 | CAN_ERR_FLAG 标记的帧高亮 |
| CAN FD 帧 | 浅蓝底标记 | 区分经典帧和 FD 帧 |
| 用户标记 | 黄色高亮 | 手动标记的重要帧 |

#### 4. 搜索与书签（中优先级）

- **搜索栏**：在 FilterBar 右侧增加搜索按钮，支持：
  - 按 ID 搜索：`0x123`
  - 按数据内容搜索：`data contains AA BB`
  - 按时间范围：`time > 10.5 && time < 20.0`
- **书签**：
  - 右键报文行 → 「添加书签」/「移除书签」
  - 书签列表在右侧面板显示，点击跳转
  - 书签数据保存在 `.sin` 录制文件中

#### 5. Hex Dump 增强（低优先级）

当前 Hex Dump 使用 `QPlainTextEdit` 纯文本显示，后续引入 **QHexEdit2** 替换：

| 特性 | 当前 | QHexEdit2 |
|------|------|----------|
| 查看 | ✅ 文本 | ✅ 高亮文本 |
| 选中/复制 | ❌ | ✅ 区域选中、右键复制 |
| 查找 | ❌ | ✅ Hex/ASCII 双向查找 |
| 信号高亮 | ❌ | ✅ DBC 信号在 Hex 数据中着色标注 |

> **QHexEdit2**：BSD 协议，商用友好。GitHub: https://github.com/SilkierNet/HexEdit

#### 6. 统计面板增强（低优先级）

当前仅显示总帧数/Rx/Tx 分布，后续增加：

- **总线负载率**：基于 CAN 波特率和实际数据量计算
- **各 ID 帧数/频率**：表格展示每个 CAN ID 的累计帧数和每秒帧数
- **错误帧统计**：按错误类型分类统计
- **周期抖动分析**：计算各 ID 的报文周期均值、最大/最小/标准差

### 三、Trace 专用开源组件引入计划

| 组件 | 协议 | 用途 | 集成方式 | 优先级 |
|------|------|------|---------|--------|
| **[vector_blf](https://github.com/Technica-Engineering/vector_blf)** | LGPL | BLF 文件解析 | 源码引入 `third_party/vector_blf/` | 🔴 高 |
| **自写 ASC 解析器** | — | ASC 文件导入/导出 | 自研（~300 行） | 🔴 高 |
| **自写 CSV 解析器** | — | CSV 文件导入/导出 | 自研（~200 行） | 🟡 中 |
| **[QHexEdit2](https://github.com/SilkierNet/HexEdit)** | BSD | 十六进制查看控件 | 源码引入 `third_party/qhexedit2/` | 🟢 低 |

### 四、实现分阶段计划

**Phase 1 — 文件导入（核心）**
1. 实现 ASC 解析器（文本格式，最简单）
2. 引入 vector_blf，实现 BLF 导入器
3. 实现 CSV 导入器
4. 统一导入接口，集成到 Trace 菜单「文件 → 导入」

**Phase 2 — 行着色 + 搜索**
1. 实现行着色规则（`data()` 的 `BackgroundRole`）
2. 实现搜索定位功能
3. 实现书签标记

**Phase 3 — 文件导出 + Hex 增强**
1. 实现 ASC/CSV 导出器
2. 引入 QHexEdit2 替换当前 Hex 面板
3. 实现 DBC 信号在 Hex 数据中高亮

**Phase 4 — 统计增强**
1. 总线负载率计算
2. 各 ID 帧数/频率统计表格
3. 周期抖动分析

---

## 软件架构

```
sin/
├── CMakeLists.txt              # 顶层 CMake 构建配置
├── src/
│   ├── main.cpp                # 程序入口
│   ├── core/                   # 核心层 — 数据结构与引擎
│   │   ├── canframe.h          #   CAN/CAN FD 帧结构
│   │   ├── recorder            #   报文录制器
│   │   ├── player              #   报文回放器
│   │   ├── cansimulator        #   CAN 模拟器
│   │   ├── dbcdata.h           #   DBC 数据结构
│   │   ├── dbcmanager          #   DBC 文件管理器
│   │   └── canfileio/          #   文件格式 I/O 层
│   │       ├── canfileio       #   读写器接口 + 格式枚举
│   │       ├── blf             #   BLF 格式读写 (Vector 二进制)
│   │       ├── asc             #   ASC 格式读写 (Vector ASCII)
│   │       ├── csv             #   CSV 格式读写 (通用文本)
│   │       ├── pcap_reader     #   PCAP 格式读取 (libpcap 网络捕获)
│   │       └── trc_reader      #   TRC 格式读取 (Vector 旧格式)
│   ├── models/                 # 数据模型层
│   │   ├── cantracemodel       #   Trace 表格模型
│   │   └── canfilterproxymodel #   过滤代理模型（排序+筛选）
│   ├── ui/                     # 界面层
│   │   ├── mainwindow          #   主窗口（无边框 + Dock 布局）
│   │   ├── spliteditorarea     #   可拆分编辑器区域
│   │   ├── traceview           #   Trace 列表 + 帧信息 + HexDump
│   │   ├── graphicview         #   信号图形视图
│   │   ├── filterbar           #   过滤栏
│   │   ├── activitybar         #   左侧活动栏
│   │   ├── bottompanel         #   底部面板（终端/输出/问题）
│   │   ├── rightpanel          #   右侧属性面板
│   │   └── panels/
│   │       └── sidebarpanels   #   侧边栏面板（工程/DBC/配置/设备）
│   └── utils/
│       └── canutils            #   格式化与工具函数
├── resources/
│   ├── resources.qrc           # Qt 资源集合
│   └── styles/
│       └── default.qss         # 全局样式表（VS Code 风格）
└── scripts/
    └── build.py                # Python 构建脚本
```

## 安装教程

### 依赖环境

- [Qt 6.8+](https://www.qt.io/download-open-source)（安装时勾选 MinGW 组件）
- [CMake 3.21+](https://cmake.org/download/)
- [MinGW 13+](https://www.mingw-w64.org/) 或 MSVC 2022

### 构建步骤

```bash
# 1. 配置（指定 Qt6 路径和编译器）
cmake -B build -S . -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64" \
  -DCMAKE_CXX_COMPILER="D:/Qt/Tools/mingw1310_64/bin/g++.exe" \
  -DCMAKE_C_COMPILER="D:/Qt/Tools/mingw1310_64/bin/gcc.exe"

# 2. 编译
cmake --build build

# 3. 部署（Windows）
D:/Qt/6.8.3/mingw_64/bin/windeployqt.exe build/bin/sin.exe

# 4. 运行
./build/bin/sin.exe
```

> 也可使用项目内置的 Python 构建脚本：`python scripts/build.py all`

## 使用说明

1. **启动程序** — 打开 sin，界面分为左侧边栏、中央编辑区、右侧属性面板、底部输出面板
2. **加载 DBC** — 通过「文件 → 打开文件」加载 `.dbc` 信号定义文件
3. **加载报文文件** — 打开 BLF/ASC/CSV/PCAP/TRC 等格式的报文文件，报文自动填充到 Trace 列表
4. **回放分析** — 在左侧「回放控制」折叠栏点击播放，使用速度下拉框调节回放速率
5. **筛选报文** — 在过滤栏输入表达式（如 `id == 0x123`），或右键列标题按列筛选
6. **查看信号** — 选中 Trace 报文，底部帧信息面板自动显示解码后的信号值
7. **图形监控** — 双击 DBC 信号树中的信号项，自动添加到 Graphic 视图绘制波形
8. **工程管理** — 在左侧「工程」面板创建多个分析工程，快速切换工作上下文

## 快捷操作

| 操作 | 方式 |
|------|------|
| 拖拽窗口 | 按住菜单栏空白区域拖动 |
| 最大化/还原 | 双击菜单栏空白区域 |
| 拆分标签页 | 右键标签栏 → 「向右拆分」/「向下拆分」 |
| 按列排序 | 点击列标题 |
| 按列筛选 | 右键列标题 → 「筛选...」 |
| 快速过滤 ID | 右键 ID 列标题 → 选择唯一 ID |
| 清空 Trace | 工具菜单 → 「清空 Trace」或命令行输入 `clear` |

## 技术栈

| 组件 | 版本/说明 |
|------|-----------|
| Qt | 6.8.3 (Widgets) |
| C++ | 17 |
| CMake | 3.21+ |
| 编译器 | MinGW 13.1.0 / MSVC 2022 |
| 构建系统 | CMake + MinGW Makefiles |
| UI 框架 | QMainWindow + QDockWidget + QSplitter |
| 样式 | QSS (VS Code 风格暗色主题) |

## 参与贡献

1. Fork 本仓库
2. 新建 `Feat_xxx` 分支
3. 提交代码
4. 新建 Pull Request

## 作者

**蔡可杰 (Jake.cai)**

- GitHub: [https://github.com/JakeCai](https://github.com/JakeCai)
- 项目地址: [https://github.com/JakeCai/sin](https://github.com/JakeCai/sin)
- 邮箱: 929168503@qq.com

## 商业合作

如需商业授权、定制开发、技术支持或业务合作，请通过以下方式联系：

- **邮箱**: 929168503@qq.com
- **微信**: 13368295840
- **GitHub Issues**: [https://github.com/JakeCai/sin/issues](https://github.com/JakeCai/sin/issues)

## 开源协议

本项目基于 [MIT License](LICENSE) 开源，商业使用请联系作者获取授权。

---

## 技术栈演进路线（逐步引入）

**原则**：每引入一个组件都要保证构建成功、功能正常后再引入下一个，避免大规模重构导致系统不稳定。

### ✅ 第一阶段：已实现的核心依赖（v1.0-当前版本）

| 功能模块 | 组件 | 协议 | 集成状态 | 说明 |
|---------|------|------|---------|------|
| **DBC 解析** | [dbcppp](https://github.com/eclipse/dbcppp) | MIT | ✅ 已内嵌源码集成 | 已集成到 `src/core/dbcpppp_includes/`，在 CMakeLists.txt 中直接编译 |
| **图形绘制** | Qt6 Charts | LGPL-3.0 | ✅ 已安装并配置 | 使用标准模块，无需额外依赖 |
| **日志输出** | qDebug | - | ✅ 默认可用 | Qt 内置调试输出 |

---

### 🔄 第二批引入：日志系统与数据结构增强

#### 1. spdlog（高性能 C++ 日志库）

**协议**: MIT  
**GitHub**: https://github.com/gabime/spdlog  
**用途**: 替换 qDebug，提供更强大的日志分级、异步写入、文件轮转、彩色终端支持。

**集成计划**:
- [x] 源码引入 spdlog v1.14.1（header-only 模式）→ `third_party/spdlog/`
- [x] 创建 `src/core/logging.h/cpp` 封装 spdlog API
- [x] 定义宏 `SIN_LOG_DEBUG/INFO/WARN/ERROR`
- [ ] 迁移关键模块（DBC 解析、Trace 过滤、回放引擎）
- [ ] 移除调试代码中的 `qDebug()`

**预期收益**:
- 更细粒度的日志级别控制（DEBUG/INFO/WARN/ERROR/FATAL）
- 日志自动保存到 `logs/sin_YYYYMMDD.log`
- 跨线程安全，避免 UI 阻塞
- 支持日志筛选和动态调整级别

---

#### 2. nlohmann/json（JSON 解析库）

**协议**: MIT  
**GitHub**: https://github.com/nlohmann/json  
**用途**: 项目配置文件、工程文件 (.sinproj)、UI 布局配置保存与加载。

**集成计划**:
- [ ] 单头文件引入（无需编译）
- [ ] 创建 `src/utils/config.cpp` 序列化/反序列化 API
- [ ] 实现 `.sinproj` 工程文件 JSON 格式转换（原纯文本解析 → JSON）
- [ ] UI 布局配置 JSON 化（Tab 位置、大小、Dock 窗口状态）

**预期收益**:
- 配置文件可读性提升
- 类型安全的序列化/反序列化
- 更容易扩展新字段（向后兼容）

---

### 📋 第三批引入：数据流与表达式处理

#### 3. moodycamel::ConcurrentQueue（无锁队列）

**协议**: BSD-2-Clause  
**GitHub**: https://github.com/cameron314/concurrentqueue  
**用途**: Trace 接收、回放、模拟三端数据流传递，替代 `QQueue` + `QMutex`。

**集成计划**:
- [x] 单头文件引入 → `third_party/concurrentqueue/`（concurrentqueue.h + blockingconcurrentqueue.h）
- [x] 创建 `src/utils/message_queue.h` 封装 `FrameQueue` 类
- [x] CanSimulator 改为后台线程生成帧 → 无锁队列 → 主线程批量消费
- [ ] 测试高负载下无丢包、零等待时间

**预期收益**:
- 无锁设计，性能比 `QMutex+QQueue` 高 3-5 倍
- 生产者 - 消费者模型天然适合 CAN 报文流
- CPU 占用更低，适合高频报文场景（10k+ Hz）

---

#### 4. 自写过滤表达式引擎（替代 exprtk）

**协议**: MIT（项目自有代码，零外部依赖）
**用途**: Trace 页面「过滤器」输入 `id == 0x50 && dlc > 8` 实时求值。

**实现方案**:
- [x] 自写递归下降解析器（Tokenizer → Parser → AST → Evaluator）
- [x] 支持变量: `id, dlc, ch, time, fd, ext, rx, tx, std`
- [x] 支持运算符: `== != > < >= <= && || !`
- [x] 支持语法糖: 裸十六进制 (`0x123` → `id==0x123`)、`id in`、`data contains`
- [x] 集成到 `FilterEngine`（pimpl 模式封装）

**收益**:
- 零外部依赖，编译极快（exprtk 头文件 ~1MB 导致链接超时）
- 完全掌控语法和错误提示
- 无协议风险（exprtk 为 LGPL/GPL，商用受限）

---

### 🎨 第四批引入：绘图与可视化增强

#### 5. QCustomPlot（Qt 图表库）

**协议**: GPL-2.0 / Commercial  ⚠️ **注意商用协议**
**官方站点**: https://www.qcustomplot.com/  
**用途**: 替代 Qt6Charts，提供更高的可定制性和丰富的图表面板（波形图、频谱图、示波器等）。

**集成计划**:
- [ ] 下载源码 `qcustomplot.h/cpp`
- [ ] 集成到 `src/ui/charts/qcustomplot/`
- [ ] 评估 GPL 协议：如用于商业软件需购买许可证（~€300）
- [ ] 如不想付费：可继续使用 Qt6Charts，或改用 Apache-2.0 协议的 `ScottPlot.Cpp`（新兴）
- [ ] 创建 `GraphicViewV2` 继承 `QCustomPlot`，保留现有 `Signal` 接口兼容

**预期收益**:
- 更美观的波形渲染效果
- 支持缩放、平移、多 Y 轴、光标读数等高级交互
- 可轻松添加网格线、图例、轴标签

**⚠️ 风险提示**: 若不想购买 GPL 商业许可，请跳过 QCustomPlot，改用以下免费选项：
- **替代 1**: 自定义 `QWidget` 绘制（推荐长期维护）
- **替代 2**: `ScottPlot.Cpp`（Apache-2.0，新兴库）

---

#### 6. FastTable / QtAdvancedTableView（高性能表格）

**协议**: LGPL / Commercial  
**GitHub**: https://github.com/fastfloat/fast-table  
**用途**: Trace 页面大量报文数据的快速展示（万行级流畅滚动）。

**集成计划**:
- [ ] 评估 FastTable 是否满足需求（主要是性能和可扩展性）
- [ ] 当前 `QTableWidget` 在 1 万行以下表现良好，暂不急进
- [ ] 如后续发现性能瓶颈，再考虑替换

---

### 🌳 第五批引入：树形控件增强

#### 7. QTreeWidgetEx（树形控件增强）

**协议**: LGPL-2.1  
**GitHub**: https://github.com/JoseMaQue/QTreeWidgetEx  
**用途**: DBC 详情标签页的信号浏览器，支持分组、折叠、筛选、搜索信号。

**集成计划**:
- [ ] 下载源码并集成
- [ ] 替换 `DbcDetailTab` 内部左侧的 `QTreeWidget` 为 `QTreeWidgetEx`
- [ ] 增加「按报文 ID 分组」「按收发节点分类」「关键字高亮」等功能

---

### 🪟 第六批引入：高级窗口管理

#### 8. Qt Advanced Docking System (QtADS)

**协议**: MIT  
**GitHub**: https://github.com/aqtads/qtads  
**用途**: 拖拽拆分、浮动子窗口、标签化面板、一键保存/加载界面布局。

**集成计划**:
- [ ] 评估 QtADS 是否能稳定工作（文档有限）
- [ ] 创建 `MainWindow::saveLayout()` / `loadLayout()` 基于 QtADS
- [ ] 支持用户自定义 Tab 布局（如：左侧 DBC 详情 + 右侧 Trace + 上部波形图）
- [ ] 退出时自动保存布局，下次启动恢复

**预期收益**:
- VS Code 级别的自由布局体验
- 用户可以分屏同时看 Trace、Graphic、DBC 详情
- 支持多显示器拖动独立窗口

---

### 🧩 可选扩展：脚本语言嵌入

#### 9. sol2 + Lua / pybind11 + Python（二选一）

**用途**: 允许用户使用脚本编写自动化测试用例、自定义数据处理逻辑。

**集成计划**:
- [ ] Lua（轻量）: sol2 + lua.hpp（单头文件）
- [ ] Python（强大但重量）: pybind11 + 独立 Python 解释器 DLL
- [ ] 创建 `src/utils/script_engine.h` 提供统一的脚本执行接口
- [ ] 示例功能：
  ```lua
  -- 自动触发条件
  if signal("DoorLock") == "Locked" then
     send(0x123, "ButtonPress", 1)
  end
  ```

**优先级**: 低，适合后期扩展

---

### 📦 其他硬件相关库

| 功能 | 推荐方案 | 备注 |
|------|---------|------|
| **SocketCAN (Linux)** | libsocketcan (MIT) | Linux 原生支持，`can-utils` 已打包 |
| **Kvaser USB 设备** | Kvaser C API SDK (商用) | 需购买授权，Windows/Linux/macOS |
| **PEAK-Systems PCAN** | PEAK SDK (商用) | 需购买授权，仅 Windows/Linux |
| **BLF/ASC/CSV/PCAP/TRC 日志** | 自研解析 | ✅ 已实现，支持读写 BLF/ASC/CSV，读取 PCAP/TRC |

**集成顺序建议**:
1. 先用内置模拟器完成所有逻辑开发
2. 优先集成 **Kvaser**（中国市场占有率最高）
3. 再根据用户需求补充 SocketCAN / PEAK

---

## 总结：渐进式演进策略

✅ **已完成**: DBC 解析（dbcppp）、图形绘制（Qt6Charts）  
🔄 **近期目标** (1-2 周):
   - 引入 spdlog（日志系统）
   - 引入 nlohmann/json（配置管理）
   
📅 **中期目标** (1-2 月):
   - 引入 moodycamel::ConcurrentQueue（高性能消息队列）
   - 引入 exprtk/re2（表达式过滤）
   - 评估 QCustomPlot（替换或升级绘图库）
   - 引入 QtADS（高级窗口管理）

🚀 **长期规划**:
   - 引入 Lua/Python（脚本扩展）
   - 支持真实 CAN 硬件（Kvaser/PEAK/SocketCAN）
   - BLF/ASC/CSV/PCAP/TRC 报文文件分析（✅ 已实现）

💡 **核心原则**：每次只改一个模块，确保构建通过、功能正确、回归测试无误，再继续下一步.

---

## 整体架构设计

> 对标 CANoe 中心化架构，融合 Wireshark 优势；目标支持 CAN/CAN FD/EtherCAT，具备 Trace、Graphic、报文录制回放、DBC 解析、多总线时间对齐；技术底座：Qt6 + C++17，商用友好开源组件。

### 总体架构思想

**中心化发布订阅架构**：全局唯一数据流中心（DataCore），所有数据源汇入中心，统一时间处理后分发至各个业务模块；模块之间零直接依赖。

分层自上而下：UI 交互层 → 业务服务层 → 核心数据内核层 → 硬件/文件抽象层

```
┌─────────────────────────────────────────────────────┐
│ UI 交互层（Qt）                                       │
│ Trace 窗口｜Graphic 曲线｜信号树｜十六进制面板｜命令行｜AI 侧边栏│
│ 依赖组件：Qt Advanced Docking System、QCustomPlot、QHexEdit2│
└───────────────────────────┬─────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────┐
│ 业务服务层（松耦合服务，订阅 DataCore 数据）             │
│ 过滤服务｜统计服务｜DBC 协议解析服务｜日志服务｜回放服务    │
└───────────────────────────┬─────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────┐
│ 核心内核层 DataCore（对标 CANoe Measurement Core）     │
│ 统一时间对齐模块｜报文分发器｜发布订阅管理器｜数据缓存      │
└───────────────────────────┬─────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────┐
│ 设备抽象层 HAL（对标 CANoe VTI）                       │
│ 采集硬件适配器｜离线日志适配器｜仿真报文源                  │
└─────────────────────────────────────────────────────┘
```

### 各层级详细设计

#### 一、设备抽象层 HAL

**目标**：隔离硬件设备与上层业务，实现在线采集 / 离线回放 / 仿真模式无缝切换

1. **统一抽象接口 `IBusSource`**
   - 开启/关闭通道
   - 报文接收回调
   - 发送报文接口

2. **三类实现**
   - **硬件采集适配器**：对接自研 Zynq 采集器 USB 数据流，接收带硬件时间戳的 CAN/CAN FD/EtherCAT 原始报文
   - **离线文件适配器**：加载 BLF/ASC/自定义录制文件，读取报文时序
   - **仿真源适配器**：软件内部生成测试报文

3. **输出标准统一报文结构体**

```cpp
struct BusMessage {
    uint64_t timestamp_ns;   // 统一纳秒时间戳
    uint8_t bus_type;        // CAN/CAN FD/ETHERCAT
    uint8_t channel;
    uint32_t id;
    std::vector<uint8_t> data;
    bool is_error_frame;
    // 原始协议信息
};
```

#### 二、核心内核层 DataCore【整个软件心脏】

1. **时间对齐模块**（差异化重点，CANoe 短板）
   - 硬件报文携带硬件时间戳优先使用
   - 多总线（CAN + EtherCAT）时间偏移校准，形成全局唯一时间坐标系
   - 离线回放严格按照原始时间戳调度

2. **报文分发中心**（发布订阅模型）
   - HAL 向上推送报文 → DataCore 完成时间标准化 → 分发给所有订阅服务
   - 订阅者：日志服务、统计服务、UI 数据代理、过滤服务

3. **两级缓存**
   - **环形实时缓存**：供给 Trace 实时展示
   - **持久化缓存**：交由日志服务落盘

**关键设计**：UI 缓存与持久化存储解耦，界面卡顿不会造成录制丢帧（复刻 CANoe 优势）

#### 三、业务服务层（独立后台服务，Qt 多线程运行）

全部运行在后台工作线程，禁止直接操作 UI

1. **DBC 解析服务**（全局单例）
   - 加载 DBC，提供报文→信号双向转换；所有 UI 模块共享解析实例
   - 依赖：dbcppp

2. **日志录制服务**
   - 支持持续录制、条件触发录制、文件分片；写入自定义二进制格式 + 兼容 BLF 导入导出

3. **回放服务**
   - 读取日志文件，匀速/倍速/单步推送报文至 DataCore；在线、回放共用一套上层逻辑

4. **高级过滤服务**
   - 两层过滤：后台二进制预过滤（减少数据流转）+ 上层类 Wireshark 表达式过滤器

5. **总线统计服务**
   - 总线负载、报文周期、抖动统计、错误帧统计

#### 四、UI 交互层（Qt6）

所有 UI 组件只订阅业务服务推送的数据，不直接访问底层硬件

**基础依赖组件**：

1. **Qt Advanced Docking System**：自由拖拽分栏、多标签、自定义布局（对标 VS Code）
2. **QCustomPlot**：Graphic 信号曲线，实现多坐标轴、游标、同步时间轴
3. **虚拟 Table Model**：Trace 报文列表，百万行流畅滚动（禁止 QTableWidget）
4. **QHexEdit2**：原始报文十六进制查看
5. **QSortFilterProxyModel 扩展**：表格实时筛选

**UI 模块清单**：

1. **Trace 窗口**：报文列表、行着色、标记点、检索、详情面板
2. **Graphic 窗口**：曲线绘图；支持信号拖拽创建曲线，多窗口时间游标联动
3. **SignalTree 信号浏览器**：DBC 报文、信号树形结构
4. **十六进制详情面板**
5. **底部命令行窗口**：输入过滤表达式，历史记录
6. **AI 辅助侧边栏**：信号分析、异常识别（可选扩展）

#### 五、线程模型设计（规避 Qt 常见坑）

1. **Qt 主线程**：只负责 UI 渲染
2. **DataCore 独立线程**：报文接收、分发、时间处理（高优先级）
3. **日志写入独立线程**：磁盘 IO 不阻塞数据流
4. **回放引擎独立线程**

✅ **规则**：所有跨线程数据传递使用 `Qt::QueuedConnection` + 只读报文结构体，避免内存竞争；大量报文采用对象池减少内存频繁申请释放。

#### 六、数据流两条核心路径

**路径 1：硬件在线采集**

```
采集硬件 → HAL 适配器 → DataCore（时间统一）
→ 分发：
  → 日志服务（持久存储）
  → 过滤服务 → Trace UI
  → DBC 解析服务 → Graphic 信号曲线
  → 统计服务
```

**路径 2：离线日志回放**

```
日志文件 → 回放服务 → DataCore
后续分发链路和在线模式完全一致
```

**巨大优势**：上层 UI 不需要区分在线/离线，大幅减少重复代码（借鉴 CANoe 经典设计）

#### 七、与 CANoe 架构对比 & 差异化设计

✅ **继承 CANoe 优秀点**

1. 中心化数据流，在线/离线逻辑复用
2. 持久化录制与界面解耦，防止界面卡顿丢数据
3. 全局统一信号数据库（DBC）

✅ **新增差异化**（弥补 CANoe 短板）

1. 原生支持 CAN FD + EtherCAT 多总线全局时间对齐
2. 引入 Wireshark 风格表达式过滤引擎
3. VS Code 式自由可停靠布局
4. 预留 AI 分析扩展接口
5. 架构不绑定 Windows，Qt 天然支持后续移植 Linux

#### 八、风险与工程优化要点

1. **海量报文性能**：Trace 必须使用虚拟 Model；曲线控件实现视口降采样
2. **内存控制**：采用环形缓冲区，限制最大缓存报文数量，防止内存持续上涨
3. **协议扩展**：`IBusMessage` 统一结构体，后续新增总线协议无需重构上层 UI
4. **授权风险**：组件优先选用 MIT/BSD 协议（QADS、QHexEdit2），谨慎使用 GPL 组件

#### 九、可选长期扩展模块（对标 CAPL）

预留脚本服务模块，后期嵌入 Lua 脚本引擎，实现自定义报文生成、自动化测试，充当自研版"CAPL"。

---

## 开源组件选型清单

> 区分三大模块：Trace 报文表格组件、Graphic 实时曲线组件、协议辅助工具组件，附带授权、适用场景、优缺点，适配 CAN/CAN FD/EtherCAT 分析仪上位机。

### 一、Graphic 曲线绘图（最高优先级，核心刚需）

#### 1. QCustomPlot（首推）

- **协议**：GPLv2 / 商业授权可购买
- **能力**：二维曲线、多 Y 轴、游标标记、区间选取、缩放平移、实时数据流、多条曲线、自定义坐标轴、导出图片
- **适配场景**：信号波形绘制，对标 CANoe Graphics
- **优势**：轻量、纯 Qt、无第三方依赖；文档丰富，工业上位机大量在用；支持大数据分片渲染
- **短板**：超大点数（百万级）直接渲染卡顿，需要自己实现数据抽稀、视口采样；不内置多通道同步游标

**工程建议**：实现视口数据降采样，配合环形缓冲区，完美适配总线录制回放场景

#### 2. Qt Charts（Qt 官方）

- **协议**：GPL / Qt 商业许可
- **能力**：折线、散点，内置坐标轴管理
- **优势**：官方原生，接口规范
- **短板**：性能弱，海量实时数据延迟高；定制化样式麻烦；商用需要购买 Qt License，不建议独立工具产品商用

#### 3. Qwt

- **协议**：Qwt License（GPL 兼容，商用需留意条款）
- **能力**：科学绘图，游标、标尺、多轴，实时数据流支持优秀
- **优势**：性能优于 QtCharts，工业老牌组件
- **短板**：UI 风格老旧；学习成本高于 QCustomPlot；新版本对 Qt6 适配存在少量兼容坑

#### 4. ImGui-Qt（备选）

- 嵌入式/工具软件高频使用，超高渲染性能；适合想要高性能曲线、自定义面板
- 代价：界面需要大量手写，无法 Qt Designer 拖拽

### 二、Trace 报文表格组件（报文列表、筛选、高亮）

Qt 原生 QTableWidget/QTreeWidget 基础上扩展，没有开箱即用的工业 Trace 控件，以下是可复用扩展库：

#### 1. QAdvancedTableView

- **功能**：虚拟表格（最重要！），支持百万行数据不卡顿、列过滤、排序、自定义行着色
- **原理**：虚拟 Model，不在内存创建全部 Item，对标 CANoe Trace 海量报文滚动
- **适用**：Trace 窗口核心表格底座

**重点**：原生 QTableWidget 加载 10 万行直接卡死，必须使用 QAbstractItemModel 虚拟表格

#### 2. QtFilterProxyModel（Qt 官方扩展）

QSortFilterProxyModel 增强版本，支持多条件、正则、多列联合过滤；实现 Trace 多维度报文筛选（ID、信号值、原始数据）。

#### 3. QtTreePropertyBrowser

- **用途**：实现 DBC 信号树、报文详情面板（Detail View）
- 可以直接展示报文内所有信号、数值、单位，构建信号浏览器侧边栏

### 三、辅助通用组件（高亮、日志、命令行、DBC 解析）

#### 1. Syntax Highlight 代码/十六进制编辑器

- **QHexEdit2** ✅【强烈推荐】
  - 十六进制查看控件，用于展示 CAN/EtherCAT 原始帧 Data，支持选中、高亮、查找；BSD 协议，宽松商用
- **QScintilla**：语法高亮，可用来实现底部命令输入窗口（类似 Wireshark 过滤表达式输入框）

#### 2. DBC 解析库（C++，无 Qt 依赖）

- **libdbc / dbccpp**：开源 DBC 文件解析，解析报文、信号、换算公式
- **CANdb++**：老牌解析库

⚠️ **注意**：大多开源 DBC 库仅基础解析，需要自行扩展信号字节序、缩放偏移计算

#### 3. BLF/ASC 日志读写

- **[vector_blf](https://github.com/Technica-Engineering/vector_blf)**（首推）
  - LGPL 协议，C++ 实现，兼容 binlog API 7.1.0
  - 支持 CAN/CAN FD/LIN 等多种对象类型，支持读写
  - 源码引入 `third_party/vector_blf/`，CMake 集成
- **自写 ASC 解析器**（~300 行）
  - ASC 为纯文本格式，每行一条报文记录，自写解析器即可
  - 同时支持导入和导出
- **自写 CSV 解析器**（~200 行）
  - 通用表格格式，支持导入和导出

### 四、EtherCAT、CAN FD 协议辅助

- **soem**（Simple Open EtherCAT Master）：开源 EtherCAT 协议栈；可用于解析 EtherCAT 报文结构
- **can-utils** 源码内协议解析逻辑，可移植用于原始帧解析

### 五、UI 布局、可停靠窗口（对标 VS Code、CANoe 多窗口拖拽）

#### Qt Advanced Docking System (QADS) ⭐关键组件

- **协议**：MIT（商用友好）
- **能力**：窗口自由拖拽、悬浮、分栏、标签化、保存布局配置文件
- 完美实现需求：可自由拖动 Trace、Graphic、信号树、统计面板，布局记忆，对标 VS Code 布局体系

**必备组件**，原生 QDockWidget 功能简陋，不要使用。

### 六、推荐最终技术组合（商用友好优先）

1. **绘图**：QCustomPlot
2. **可停靠自由布局**：Qt Advanced Docking System
3. **Trace 表格**：Qt 虚拟 QAbstractItemModel + QAdvancedTableView
4. **原始数据查看**：QHexEdit2
5. **信号树**：QtTreePropertyBrowser
6. **过滤层**：增强版 QSortFilterProxyModel
7. **DBC 解析**：dbcppp
8. **BLF 日志**：vector_blf（LGPL）
9. **ASC/CSV 日志**：自写解析器（文本格式，简单可靠）

### 七、关键避坑

1. 优先选择 MIT / BSD 协议组件，规避 GPL 传染风险（产品售卖非常重要）
2. 曲线控件一定要提前规划数据降采样，回放几十上百万帧时，直接渲染必然卡顿
3. Trace 表格强制使用虚拟 Model，绝对不要使用 QTableWidget 加载大量报文
4. QCustomPlot 默认单线程渲染；实时高流速报文，需要分离数据缓存线程与 UI 线程

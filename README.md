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
- **文件回放** — 加载 `.sin` 录制文件，按原始时间戳精准回放，支持变速控制（0.1x ~ 10x）
- **进度拖拽** — 回放进度条可任意拖拽定位，快速跳转到关键时间点

### Trace 追踪
- **Wireshark 风格列表** — 时间戳、通道、方向、ID、DLC、数据、Flags 多列显示
- **按列排序** — 点击列标题升序/降序排序，支持数值与时间类型智能比较
- **按列筛选** — 每列独立筛选条件，ID 列支持 `>`、`<`、`!=` 运算符，快速过滤目标报文
- **快速筛选** — 右键列标题一键筛选仅 Rx/Tx、唯一 ID 列表
- **帧信息面板** — 选中报文显示完整解码字段，集成 DBC 信号级解析
- **十六进制转储** — 选中报文以 Offset + Hex + ASCII 格式展示原始数据

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
│   │   └── dbcmanager          #   DBC 文件管理器
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
3. **加载录制** — 打开 `.sin` 录制文件，报文自动填充到 Trace 列表
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
| **BLF 日志播放** | libblf (LGPL) | 梅赛德斯 - 奔驰开源，需解决 Qt 适配 |
| **ASC/PCAP 日志** | 自研解析 | 先不做，后续按需扩展 |

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
   - BLF 日志播放

💡 **核心原则**：每次只改一个模块，确保构建通过、功能正确、回归测试无误，再继续下一步。
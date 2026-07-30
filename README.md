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

引入这些开源组件，能去重构或者重写原来的一些模块，最小稳定技术栈（Qt6 + 商用无风险协议）
DBC 解析：dbcppp（MIT）
CAN 硬件：libsocketcan (Linux) + Kvaser/PEAK 官方 SDK (Windows)
BLF 日志：libblf
绘图波形：QCustomPlot
多窗口布局：Qt Advanced Docking System
线程队列：moodycamel ConcurrentQueue
表达式过滤：exprtk
脚本扩展：sol2 + Lua（可选）
XML/JSON：pugixml + nlohmann/json
日志打印：spdlog

FastTable / QtAdvancedTableView（高性能表格）

3. Qt TreeView 增强：QTreeWidgetEx
DBC 信号树（信号分组、报文列表），支持折叠、筛选、搜索信号，对标 Canoe 信号浏览器

【窗口 / 布局 / 多视图管理组件】多波形窗口、分屏、停靠面板（对标 Canoe 多视图）
1. QDockWidget 增强：Qt Advanced Docking System（QtADS，必装！）
绝对刚需，你的上位机离不开
原生 QDockWidget 缺陷：拖拽分屏、浮动窗口、标签分组、保存布局非常难写。
QtADS 开源 MIT，完美解决：
窗口自由拖拽拆分、左右 / 上下分屏、多波形独立子窗口；
面板标签化堆叠（波形面板 + 报文表格 + 信号树同区域切换）；
一键保存 / 加载界面布局（用户自定义视图布局重启不丢失）；
浮动独立窗口、最小化面板、锁定布局；
兼容 QCustomPlot 绘图面板嵌入 dock。
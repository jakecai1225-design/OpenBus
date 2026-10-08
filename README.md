# OpenBus

<p align="center">
  <strong>专业、免费、开源的总线 · 协议 · 网络综合分析桌面软件</strong><br/>
  开放架构 · 可扩展驱动与插件 · 面向工程现场的稳定工作台
</p>

<p align="center">
  <a href="README.en.md">English</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/">官网 / App Store</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/docs">文档</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/download">下载</a>
  &nbsp;·&nbsp;
  <a href="http://sin.org.cn/market">扩展市场</a>
  &nbsp;·&nbsp;
  <a href="https://gitee.com/jake_cai/sin">Gitee</a>
  &nbsp;·&nbsp;
  <a href="https://github.com/jakecai1225-design/OpenBus">GitHub</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/version-1.10.4-blue" alt="Version">
  <img src="https://img.shields.io/badge/Qt-6-green" alt="Qt 6">
  <img src="https://img.shields.io/badge/C%2B%2B-17-blue" alt="C++17">
  <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
  <img src="https://img.shields.io/badge/platform-Windows%20x64-lightgrey" alt="Windows">
</p>

<p align="center">
  <img src="http://sin.org.cn/screenshots/07-trace.png" alt="OpenBus Trace" width="880">
</p>

---

## 简介

**OpenBus** 是一款面向总线、协议与现场网络分析的桌面工作台。你可以用它完成报文录制与回放、Trace 浏览、信号波形、DBC 解码、多厂商设备接入，以及诊断、标定、日志转换等上层能力——后一类能力通过开放的驱动与插件机制按需扩展。

软件以 **MIT** 许可开源，个人、团队与商业集成均可使用。我们希望它既足够专业，能支撑日常联调与日志分析，也足够开放，让硬件厂商、工具作者和爱好者一起完善生态。

适用场景包括汽车与新能源电子、工控与机器人、教学实验、协议研究等凡是需要 CAN / CAN FD 及上层协议分析的工作。当前正式发行面向 **Windows 10/11 x64**；源码中亦包含 SocketCAN 等后端，便于在 Linux 等环境自行构建。

### 我们看重的产品特质

| | |
|--|--|
| **专业** | Trace、Graphic、过滤、解码与日志 I/O 遵循业界熟悉的分析习惯，面向真实工程负载 |
| **稳定可靠** | 主机以 Qt / C++ 实现；录制、回放与视图刷新按长时间运行与大日志场景设计 |
| **开放免费** | 源码、文档与官方扩展索引公开可取；MIT 许可允许学习、改造与商用集成 |
| **可扩展** | 驱动（`.odp`）与工具插件（`.opk`）可热安装；协议与行业能力以领域套件方式演进 |
| **多厂商友好** | 内置多种常见后端，并可通过市场持续接入新的接口卡与适配器 |

工作流可以概括为：连接设备或打开日志 → 在 Trace 中筛选与定位 → 用 DBC 解码信号 → 在 Graphic 中对照波形 → 按需安装诊断 / 标定等扩展。

---

## 谁适合使用

- **汽车 / 新能源 / 零部件**：CAN / CAN FD 联调、台架与路试日志、DBC 信号核对  
- **工控与机器人**：现场抓包、周期与抖动分析、设备对线  
- **教学与科研**：内置模拟器即可完成完整演示；源码适合课程与二次开发  
- **协议分析**：过滤、书签、十六进制转储、多日志对照  
- **生态伙伴**：希望发布自有驱动或协议工具，并接入统一市场的团队  

---

## 核心功能

### 录制、回放与日志

- **实时录制**：连接 CAN 设备捕获总线流量，支持 CAN 2.0A/B 与 CAN FD  
- **日志回放**：按原始时间戳回放，支持变速（约 0.1×～10×）与进度定位  
- **导入**：BLF / ASC / CSV / PCAP / TRC 等常见格式  
- **导出**：ASC / CSV / BLF，便于与报告或下游工具衔接  
- **内置模拟器**：无硬件时也可生成测试帧，验证过滤、解码与视图  

### Trace 报文视图

- Wireshark 风格多列：序号、时间、Delta、通道、方向、ID、DLC、数据、标志、计数等  
- 列排序、表头漏斗筛选、右键快速筛选（仅 Rx / 仅 Tx / 唯一 ID）  
- 表达式过滤（如 `id == 0x123 && dlc > 4`）  
- 覆盖模式、自动 / 手动行着色、书签跳转  
- 帧信息面板与十六进制转储（Offset / Hex / ASCII）  
- 搜索定位与统计概览（帧数、频率、错误帧、总线负载等）  

### DBC 与信号分析

- 加载标准 `.dbc`，侧边栏 Message → Signal 树浏览  
- 选中 Trace 帧即可解码（有符号 / 无符号、大小端）  
- 双击信号加入 Graphic，进行实时或回放波形观察  

### Graphic 波形

- 多信号时间轴叠加，可按通道配置显示  
- 滚轮缩放、拖拽平移、卡尺 / 游标测量  
- 与 Trace 联动，便于列表与波形对照  

### 工程与界面

- VS Code 风格布局：活动栏、可折叠侧边栏、可拆分编辑区、可停靠面板  
- 工程文件 `.openbusproj` 保存通道、DBC、布局等上下文，支持多工程切换  
- Data Window、I/O Graph、着色规则等内置分析工具  
- 命令行终端（`help` / `sim` / `record` / `play` / `filter` 等）  
- 深色 / 浅色主题；无边框窗口仍支持 Windows 贴靠与缩放  

---

## 硬件与驱动

分析能力集中在主机；设备差异由驱动层承载，便于同时支持多家接口卡与开源适配器。

### 内置后端

启动时注册下列后端（本机能否枚举到设备，取决于是否已安装对应厂商运行库）：

| 驱动 ID | 说明 |
|---------|------|
| **simulator** | OpenBus 模拟器（始终可用） |
| **zlg** | 致远 ZLG USBCAN / USBCANFD 等 |
| **peak** | PEAK System PCAN |
| **kvaser** | Kvaser CANLIB |
| **vector** | Vector XL |
| **tongxing** | 同星 / TOSUN |
| **ixxat** | IXXAT VCI |
| **intrepid** | Intrepid |
| **candle** | Candle / GS_USB（CANable、candleLight 等） |
| **busmust** | BUSMUST USB-CAN(FD) |
| **slcan** | SLCAN 串口协议（市售适配器 / CANable / USBtin / ESP32 等） |
| **socketcan** | Linux SocketCAN（源码构建场景） |

### 市场驱动包（`.odp`）

- 以 `.odp`（ZIP）分发：`driver.json` + 原生库 + `CHECKSUMS.sha256`  
- 在客户端 **插件市场** 中下载、校验、安装并热加载（多数情况无需重启）  
- 支持禁用、卸载，以及离线「从文件安装」  
- 官方索引：[`http://sin.org.cn/market/market.json`](http://sin.org.cn/market/market.json)  

开发新驱动时，可在 `drivers/<id>/` 实现后端并接入 CMake，再用 `scripts/driver_tool.py` 打包。细则见 `doc/` 中的驱动说明。

---

## 插件与协议扩展

主机提供通用分析与设备管理；诊断、标定、日志转换、辅助分析等能力以插件形式扩展。

| 项目 | 说明 |
|------|------|
| 运行方式 | 工具插件运行于独立进程（Python + PyQt6），经宿主桥接交换数据 |
| 包格式 | `.opk`（`plugin.json` + 脚本与资源） |
| 安装入口 | 与驱动共用插件市场：搜索、详情、启停、卸载 |
| 产品目录 | 领域套件由 allowlist 管理，保持默认产品面清晰 |

当前已上架的领域能力包括：UDS、OBD、J1939、CANopen、DBC Studio、EDS Studio、A2L Studio、XCP Studio、AUTOSAR Suite、EtherCAT Suite、Log Converter、AI Agent 等。

本地开发可使用：

```bash
python scripts/plugin_tool.py pack-suites
python scripts/make_market.py
```

设计说明见 `doc/插件系统方案.md`、`doc/Plugin_Domain_Suites.md`。

---

## 官网与 App Store

OpenBus 的产品站点与扩展目录托管在 **[sin.org.cn](http://sin.org.cn/)**（OpenBus App Store）。桌面客户端「插件市场」与网站扩展页使用同一套市场数据。

| 页面 | 地址 | 说明 |
|------|------|------|
| 首页 | [http://sin.org.cn/](http://sin.org.cn/) | 产品介绍与入口导航 |
| 下载 | [http://sin.org.cn/download](http://sin.org.cn/download) | Windows 安装包 |
| 文档 | [http://sin.org.cn/docs](http://sin.org.cn/docs) | 在线用户手册 |
| 手册 PDF | [中文](http://sin.org.cn/docs/downloads/openbus-user-manual.pdf) · [English](http://sin.org.cn/docs/downloads/openbus-user-manual.en.pdf) | 离线阅读 |
| 更新说明 | [http://sin.org.cn/updates](http://sin.org.cn/updates) | 版本发布记录 |
| 扩展市场 | [http://sin.org.cn/market](http://sin.org.cn/market) | 浏览驱动与插件 |
| 硬件一览 | [http://sin.org.cn/hardwares](http://sin.org.cn/hardwares) | 常见设备与兼容信息 |
| 提交扩展 | [http://sin.org.cn/market/submit](http://sin.org.cn/market/submit) | 驱动 / 插件上架指引 |
| 社区 | [http://sin.org.cn/community](http://sin.org.cn/community) | 交流与贡献入口 |
| 反馈 | [http://sin.org.cn/feedback](http://sin.org.cn/feedback) | 问题与建议表单 |
| 博客 | [http://sin.org.cn/blog](http://sin.org.cn/blog) | 使用与开发文章 |
| 市场索引（JSON） | [http://sin.org.cn/market/market.json](http://sin.org.cn/market/market.json) | 客户端可配置的目录源 |

安装客户端后，也可直接在侧边栏打开 **插件市场**，体验与网站一致。

---

## 获取与开始使用

1. 从 [下载页](http://sin.org.cn/download) 获取安装包，或从源码自行构建  
2. 运行 `openbus.exe`，从欢迎页进入，或通过「文件 → 打开」加载 DBC / 日志  
3. 配置通道、连接设备，或启用内置模拟器熟悉界面  
4. 在 Trace / Graphic 中完成分析；需要时到插件市场安装驱动或领域套件  

更完整的操作说明见 [在线文档](http://sin.org.cn/docs)。

---

## 开源、反馈与参与

OpenBus 欢迎使用、反馈与贡献。你可以通过下列途径参与：

### 反馈问题与建议

| 途径 | 地址 |
|------|------|
| Gitee Issue（推荐，与主仓一致） | [gitee.com/jake_cai/sin/issues](https://gitee.com/jake_cai/sin/issues) |
| GitHub Issue | [github.com/jakecai1225-design/OpenBus/issues](https://github.com/jakecai1225-design/OpenBus/issues) |
| 网站反馈表单 | [sin.org.cn/feedback](http://sin.org.cn/feedback) |
| 社区页 | [sin.org.cn/community](http://sin.org.cn/community) |
| 邮件 | 929168503@qq.com |

提交缺陷时，请尽量附上：OpenBus 版本、操作系统、能否复现的步骤、相关日志格式或截图。功能建议可直接在 Issue 中描述场景与期望行为。

### 获取源码与提交代码

| 托管 | 仓库 |
|------|------|
| Gitee（日常协作主仓） | [gitee.com/jake_cai/sin](https://gitee.com/jake_cai/sin) |
| GitHub（同步镜像） | [github.com/jakecai1225-design/OpenBus](https://github.com/jakecai1225-design/OpenBus) |

贡献流程建议：

1. Fork 仓库并创建分支  
2. 在 MSYS2 环境下完成构建与本地验证  
3. 通过 Pull Request 说明改动动机与测试情况  
4. 较大架构或行为变更，请先开 Issue 讨论  

用户手册源稿位于 `doc/user_manual/`；面向开发者的设计说明在 `doc/`。

### 开发并发布驱动 / 插件

1. 阅读 `doc/` 中的驱动与插件方案，以及现有 `drivers/*`、`plugins/*` 示例  
2. 使用 `scripts/driver_tool.py` / `scripts/plugin_tool.py` 打包为 `.odp` / `.opk`  
3. 本地用 `scripts/make_market.py` 生成索引并在客户端验证安装  
4. 按 [提交扩展](http://sin.org.cn/market/submit) 页面的说明准备材料，或通过 Issue / 邮件与维护者联系上架  
5. 上架后，用户可从 [扩展市场](http://sin.org.cn/market) 与客户端插件市场获取  

---

## 从源码编译

### 环境要求

请使用 **MSYS2 UCRT64** 工具链。不建议混用 Qt 官方在线安装包或其他 MinGW 发行版。

| 组件 | 要求 |
|------|------|
| 系统 | Windows 10/11 x64（正式发行与主验证平台） |
| Shell | [MSYS2](https://www.msys2.org/) UCRT64 |
| 构建 | CMake ≥ 3.21、Ninja、C++17 |
| Qt | Qt 6（`mingw-w64-ucrt-x86_64-qt6-*`） |
| 其他 | zlib、ZeroMQ / cppzmq、libusb；插件开发另需 Python、PyQt6、PyZMQ |

### 安装依赖

在 UCRT64 终端进入仓库根目录：

```bash
bash scripts/setup_msys2.sh
python scripts/build.py status
```

可选环境变量（bash 路径）：

```bash
export SIN_MSYS2_ROOT=/c/msys64
export SIN_MSYS2_ENV=ucrt64
export SIN_QT_DIR=/ucrt64
export SIN_MINGW_DIR=/ucrt64
```

### 配置、编译与运行

```bash
python scripts/build.py configure --clean --build-type Release
python scripts/build.py build -j8
python scripts/build.py deploy
python scripts/build.py run
```

一键流水线：`python scripts/build.py all`

日常调试可使用独立目录：

```bash
python scripts/build.py configure --build-type Dev --build-dir build-dev
python scripts/build.py build --build-dir build-dev -j8
python scripts/build.py run --build-dir build-dev
```

请以 `scripts/build.py` 为准；本工程不建议启用 ccache，也不建议随意改用 LLD。

### 仓库结构（简要）

```
sin/
├── src/           # 主机（core / ui / models …）
├── drivers/       # 厂商与协议驱动
├── plugins/       # Python 领域套件
├── market/        # 本地市场索引与包
├── resources/     # 资源与样式
├── scripts/       # 构建、打包与市场工具
├── doc/           # 开发者文档与用户手册源稿
└── dist/          # 打包输出
```

---

## 打包与部署

### 便携包

```bash
python scripts/build.py package --version 1.10.4
```

产物在 `dist/`。解压后保持目录完整，直接运行 `openbus.exe`。

### Windows 安装程序（NSIS）

需安装 [NSIS 3](https://nsis.sourceforge.io/)，或设置 `NSIS` / `MAKENSIS` 指向 `makensis.exe`：

```bash
python scripts/build.py package --version 1.10.4 --installer
```

安装向导支持多语言选择。更多参数见 `python scripts/build.py package -h`。

### 市场与发布

```bash
python scripts/make_market.py
python scripts/driver_tool.py pack --help
python scripts/plugin_tool.py pack-suites
```

客户端默认可使用官方 [`market.json`](http://sin.org.cn/market/market.json)；开发阶段可指向本地 `market/`。

发布前建议确认：环境 `status` 正常、Release 冒烟通过、版本号与 About 一致（当前 **1.10.4**）、市场包校验和正确。

---

## 作者

**蔡可杰（Jake Cai）** · 929168503@qq.com  

更多入口见 [社区页](http://sin.org.cn/community) 与文首源码链接。

---

## 许可证

本项目以 [MIT License](LICENSE) 发布。在保留版权与许可声明的前提下，你可以自由使用、修改、分发，并用于商业场景。

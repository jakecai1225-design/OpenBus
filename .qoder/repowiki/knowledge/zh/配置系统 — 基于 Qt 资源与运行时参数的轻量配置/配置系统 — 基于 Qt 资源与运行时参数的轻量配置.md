---
kind: configuration_system
name: 配置系统 — 基于 Qt 资源与运行时参数的轻量配置
category: configuration_system
scope:
    - '**'
source_files:
    - src/main.cpp
    - resources/resources.qrc
    - resources/styles/default.qss
    - CMakeLists.txt
    - src/ui/signalconfigdialog.h
    - src/ui/signalconfigdialog.cpp
---

本仓库未实现独立的配置文件加载机制（如 .yaml、.toml、.ini、.env 等），而是采用 Qt 框架自带的资源系统与运行时参数来管理应用配置。具体表现为：

1. **样式与静态资源通过 Qt Resource System (.qrc) 打包**
   - `resources/resources.qrc` 将 `styles/default.qss` 以 `:/styles/default.qss` 路径嵌入可执行文件。
   - `src/main.cpp` 在启动时通过 `QFile` 读取该内嵌 QSS 并调用 `app.setStyleSheet()` 应用全局样式。

2. **应用元信息通过 QApplication 设置**
   - `main.cpp` 中通过 `setApplicationName("sin")`、`setOrganizationName("sin")`、`setApplicationVersion("0.1.0")` 设置应用标识，这些值会被 Qt 的 `QSettings` 用作默认组织/应用名前缀，但当前代码并未实际使用 `QSettings` 读写持久化配置。

3. **用户交互配置通过对话框即时生效**
   - `SignalConfigDialog` 提供信号监控的配置界面（名称、CAN ID、扩展帧、字节偏移、位长、端序），配置值作为局部对象字段在内存中传递，无持久化存储逻辑。
   - `GraphicView::signalConfigs()` 返回当前图形视图中的信号配置集合，同样仅存在于运行期内存。

4. **构建期配置由 CMake 管理**
   - 顶层 `CMakeLists.txt` 定义项目版本、Qt6 依赖、C++17 标准、输出目录等；`src/CMakeLists.txt` 负责编译源文件。构建产物输出到 `${CMAKE_BINARY_DIR}/bin`。

5. **脚本与资源**
   - `scripts/build.py` 为构建辅助脚本。
   - `resources/styles/default.qss` 是唯一的样式配置文件，随应用一起打包。

**约束与约定**：
- 所有静态资源必须通过 `.qrc` 注册并以 `:/` 前缀访问。
- 应用名称、组织名、版本号统一在 `main.cpp` 中集中设置。
- 用户级配置（如信号监控）目前仅保存在内存中，重启后丢失，未见任何 `QSettings`、JSON、INI 或外部配置文件的读写实现。
- DBC 文件、录制文件（`.sin`）属于数据文件而非应用配置，通过文件对话框动态选择，不预置默认路径。

综上，该项目处于“无独立配置系统”的状态，配置能力仅限于 Qt 资源系统和运行时参数，尚未引入持久化配置管理机制。
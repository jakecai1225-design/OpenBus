---
kind: configuration_system
name: 基于 JSON 的应用配置系统
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/ui/settingsdialog.h
    - src/ui/settingsdialog.cpp
---

该应用使用自研的 `AppConfig` 单例类作为统一的配置管理核心，采用 nlohmann/json 库将配置持久化为 JSON 文件，路径位于 `%APPDATA%/sin/sin/settings.json`（通过 Qt 的 `QStandardPaths::AppDataLocation` 动态解析）。

**加载与分层策略**：程序启动时调用 `AppConfig::load()`，若配置文件不存在则自动生成默认配置并写入；若文件存在但解析失败，则回退到 `defaultConfig()` 返回的内置默认值。所有 getter（`getString`/`getInt`/`getBool`/`getDouble`）均支持传入默认值，确保缺失键或类型不匹配时不会崩溃。

**配置项结构**：默认配置按命名空间前缀分组，包括 `theme`、`font.*`、`window.*`、`trace.*`、`graphic.*`、`record.*`、`filter.*`、`log.*` 等，覆盖界面主题、字体、窗口几何、Trace 缓冲区、图形渲染、录制格式、过滤器和日志轮转等全部运行时参数。

**编辑与持久化**：提供 `SettingsDialog` 对话框，支持两种编辑模式——可视化设置列表（按分类树 + 搜索过滤，根据类型自动选择 QCheckBox/QSpinBox/QComboBox 等控件）和直接编辑 raw JSON（类似 VS Code settings.json）。修改通过 `set()` 写入内存后触发 `changed(key)` 信号，保存时调用 `save()` 以缩进 4 空格格式化输出到磁盘。

**生命周期管理**：`AppConfig` 采用静态单例模式（`instance()`），析构时不会自动保存，需显式调用 `save()`；JSON 批量导入通过 `fromJsonString()` 完成，错误时记录 spdlog 错误日志并返回 false。

**约束与约定**：所有配置键必须为字符串形式（Qt QString → std::string 转换），不支持嵌套对象或数组结构；新增配置项需在 `defaultConfig()` 中声明默认值，并在 `SettingsDialog::setupMetas()` 中补充元数据（key、label、category、type、desc、comboChoices）才能出现在 UI 中。
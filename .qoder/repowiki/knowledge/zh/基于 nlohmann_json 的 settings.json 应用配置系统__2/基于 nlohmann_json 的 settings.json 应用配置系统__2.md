---
kind: configuration_system
name: 基于 nlohmann/json 的 settings.json 应用配置系统
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/ui/settingsdialog.cpp
    - src/main.cpp
---

本项目的配置系统采用单文件 JSON 持久化方案，核心由 `src/core/appconfig.h/.cpp` 中的 `AppConfig` 单例类实现，配置文件路径为 `%APPDATA%/sin/sin/settings.json`（通过 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` 解析）。

**加载与默认值**：应用启动时 `main.cpp` 调用 `AppConfig::instance()->load()`，若 `settings.json` 不存在则自动生成并写入 `defaultConfig()` 返回的默认 JSON；若文件存在但解析失败则回退到默认值。所有 getter（`getString/getInt/getBool/getDouble`）均支持传入默认值作为回退。

**读写接口**：提供类型安全的 typed setter（`set(key, value)` 重载 string/int/bool/double），每次修改后发射 `changed(key)` 信号；`save()` 将内存中的 `nlohmann::json` 对象以 4 空格缩进格式序列化写入磁盘。

**配置项分类与元数据**：`SettingsDialog` 通过 `setupMetas()` 集中定义全部设置项的键名、显示标签、所属分类（通用/Trace/Graphic/Record/日志）、数据类型（string/int/bool/double/combo）及描述文本，UI 根据类型动态生成 QCheckBox/QSpinBox/QDoubleSpinBox/QComboBox/QLineEdit 编辑器，实现“设置列表”和“JSON 编辑”双视图。

**使用位置**：除 SettingsDialog 直接操作外，`ThemeManager` 通过该配置读取主题名称，`logging::init()` 依据 `log.level/log.maxFileSize/log.maxFiles` 初始化 spdlog 输出。

**约束与约定**：
- 配置键采用点号分隔的层级命名（如 `trace.maxFrames`、`window.width`），与 VS Code `settings.json` 风格一致。
- 所有配置项必须在 `defaultConfig()` 中声明默认值，否则运行时取不到对应 key 时会返回空/0/false。
- 配置文件仅存放用户可覆盖的设置，不存储运行时状态（如窗口几何由 `window.rememberGeometry` 控制是否持久化）。
- 不支持环境变量或命令行参数覆盖配置，也不支持多环境/多用户配置切换。
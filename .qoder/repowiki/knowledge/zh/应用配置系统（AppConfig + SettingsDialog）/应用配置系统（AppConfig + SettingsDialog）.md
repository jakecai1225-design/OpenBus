---
kind: configuration_system
name: 应用配置系统（AppConfig + SettingsDialog）
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/ui/settingsdialog.h
    - src/ui/settingsdialog.cpp
    - src/main.cpp
---

本仓库采用基于 nlohmann/json 的轻量级应用配置系统，配置文件为 JSON 格式，默认路径位于 `%APPDATA%/sin/sin/settings.json`（通过 Qt `QStandardPaths::AppDataLocation` 解析）。配置由单例 `AppConfig` 管理，提供类型安全的 getter/setter、默认值回退、JSON 序列化/反序列化以及变更信号通知。

**核心组件与职责**
- `src/core/appconfig.h/.cpp`：配置持久化核心。启动时调用 `load()` 读取或创建默认配置；`save()` 写入磁盘；提供 `getString/getInt/getBool/getDouble` 等 typed 访问器，以及 `set/reset/toJsonString/fromJsonString` 等批量操作；析构不自动保存，需显式调用 `save()`。
- `src/ui/settingsdialog.h/.cpp`：VS Code 风格的设置对话框，左侧分类树 + 右侧设置项列表，支持搜索过滤和“编辑 JSON”模式。所有设置项通过 `SettingMeta` 元数据声明（key、label、category、type、desc、comboChoices），运行时动态生成 UI 控件（QCheckBox/QSpinBox/QDoubleSpinBox/QComboBox/QLineEdit）并双向绑定到 `AppConfig`。
- `src/main.cpp`：应用入口，在 `QApplication` 初始化后调用 `logging::init()`，再加载 `AppConfig::instance()->load()`，最后应用主题并显示主窗口。

**架构与约定**
- 单例模式：`AppConfig::instance()` 使用静态局部变量保证全局唯一实例。
- 默认配置集中定义：`defaultConfig()` 返回包含 theme、font、window、trace、graphic、record、filter、log、project 等分组的初始 JSON。
- 键命名约定：使用点号分隔的层级键名（如 `trace.maxFrames`、`log.level`），便于分类展示。
- 变更通知：每个 `set()` 调用后 emit `changed(key)` 信号，供 UI 响应式更新。
- 错误回退：JSON 解析失败时记录日志并回退到默认配置，保证程序健壮性。
- 文件编码：UTF-8 文本文件，缩进 4 空格。

**使用流程**
1. 首次运行：`load()` 检测到文件不存在则写入默认配置。
2. 运行时修改：通过 `SettingsDialog` 的图形界面或直接调用 `AppConfig::set()` 修改。
3. 持久化：点击“保存”按钮调用 `save()`，或在 JSON 编辑器中修改后保存。
4. 重置：`onReset()` 将配置恢复为 `defaultConfig()` 的 JSON 字符串。

**约束与限制**
- 不支持环境变量覆盖或命令行参数注入。
- 不支持多环境配置（开发/测试/生产）。
- 不支持配置热重载（需重启或手动刷新 UI）。
- 不支持配置迁移或版本升级逻辑。
- 仅支持基本数据类型（string/int/bool/double）及组合选择（combo）。
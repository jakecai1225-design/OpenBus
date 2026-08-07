---
kind: configuration_system
name: 基于 nlohmann/json 的 settings.json 应用配置系统
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/ui/settingsdialog.h
    - src/ui/settingsdialog.cpp
    - src/main.cpp
    - src/core/projectmanager.cpp
---

## 1. 系统概览

本项目采用自研的 `AppConfig` 单例作为唯一的应用级配置管理入口，使用 **nlohmann/json** 库将配置持久化为 JSON 文件（`settings.json`），并通过 Qt 的 `QStandardPaths::AppDataLocation` 定位到 `%APPDATA%/sin/sin/settings.json`。该设计在注释中明确描述为“类似 VS Code settings.json”，并提供 typed getter/setter、默认值回退、JSON 文本导入导出以及变更信号通知。

## 2. 核心文件与职责

- `src/core/appconfig.h` / `src/core/appconfig.cpp`：配置系统的核心实现。提供 `instance()` 单例、`load()`/`save()` 生命周期方法、`getString/getInt/getBool/getDouble` 类型安全读取、对应的 `set(key, value)` 重载、`reset(key)` 删除键、`toJsonString()`/`fromJsonString()` 批量导入导出、`defaultConfig()` 定义所有默认项。
- `src/ui/settingsdialog.h` / `src/ui/settingsdialog.cpp`：VS Code 风格的设置 UI。通过 `SettingMeta` 元数据（key、label、category、type、desc、comboChoices）声明式地注册每个设置项，动态生成分类树、搜索过滤、表单控件（QCheckBox/QSpinBox/QDoubleSpinBox/QComboBox/QLineEdit）和 JSON 纯文本编辑器两种编辑模式。
- `src/main.cpp`：在应用启动时调用 `AppConfig::instance()->load()` 完成首次加载。
- `src/core/projectmanager.cpp`、`src/ui/mainwindow.cpp`、`src/ui/panels/sidebarpanels.cpp`：业务模块通过 `AppConfig::instance()->get/set` 读写工程相关配置（如 `project.lastPath`、`project.recent`、`project.autoSaveOnClose`）。

## 3. 架构与设计约定

### 3.1 存储格式与位置
- 配置文件路径固定为 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.json"`。
- 首次运行或 JSON 解析失败时，自动写入 `defaultConfig()` 返回的默认 JSON，并记录日志。
- 保存时使用 `json.dump(4)` 输出带 4 空格缩进的格式化 JSON。

### 3.2 配置项命名空间
所有 key 采用点号分隔的层级命名（如 `trace.maxFrames`、`graphic.timeWindow`、`log.level`、`project.recentMax`），UI 层据此划分“通用”、“Trace”、“Graphic”、“Record”、“日志”、“工程”等分类。

### 3.3 默认值与回退机制
- `AppConfig` 构造时即初始化 `m_data = defaultConfig()`。
- 所有 typed getter 在 key 不存在或类型不匹配时返回传入的 `def` 参数，保证调用方无需判空。
- `SettingsDialog::onReset()` 通过 `fromJsonString(defaultConfig().dump(4))` 一键恢复全部默认值。

### 3.4 运行时修改与持久化
- 每次 `set(key, value)` 都会 `emit changed(key)` 信号，供观察者响应。
- 配置修改后需显式调用 `save()` 才会落盘；`SettingsDialog` 的“保存”按钮触发 `AppConfig::instance()->save()`。
- 项目最近列表等关键状态在写入后立即调用 `save()` 确保一致性。

### 3.5 双模编辑
- **表单模式**：根据 `SettingMeta.type`（string/int/bool/double/combo）动态创建对应 Qt 控件，实时写回 `AppConfig`。
- **JSON 模式**：顶部切换按钮可进入纯文本编辑页，直接编辑 `settings.json` 内容，点击保存时通过 `fromJsonString()` 解析并校验语法。

## 4. 约束与规则

- **单一配置源**：整个应用仅通过 `AppConfig::instance()` 访问配置，无其他全局配置入口。
- **key 必须存在且类型正确**：typed getter 会严格检查 `is_string()/is_number_integer()/is_boolean()/is_number()`，类型不符则回退默认值。
- **JSON 必须合法**：`fromJsonString()` 捕获 `json::parse_error` 并记录错误日志，解析失败时返回 false，阻止脏数据写入。
- **目录自动创建**：`load()`/`save()` 均先调用 `QDir().mkpath(...)` 确保配置目录存在。
- **未使用 QSettings**：尽管 CMakeLists 中列出了 `<QSettings>` 头文件，但实际配置系统完全基于 nlohmann/json 自建，未启用 Qt 原生注册表/ini 后端。
- **配置项扩展方式**：新增设置需在 `defaultConfig()` 中添加默认值，并在 `SettingsDialog::setupMetas()` 中注册 `SettingMeta` 以出现在 UI 中。
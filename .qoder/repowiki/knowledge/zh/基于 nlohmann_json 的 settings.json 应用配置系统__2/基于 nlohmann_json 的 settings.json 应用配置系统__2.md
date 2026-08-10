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
    - src/core/sessionmanager.cpp
    - src/ui/mainwindow.cpp
---

## 1. 采用的系统与方案

本仓库实现了一个自研的、单文件 JSON 格式的应用级配置系统，核心是 `src/core/appconfig.h/.cpp` 中的 `AppConfig` 类。它使用第三方库 `nlohmann/json`（位于 `third_party/nlohmann_json/`）作为底层序列化/反序列化工具，通过 Qt 的 `QStandardPaths::AppDataLocation` 定位用户数据目录，将配置文件持久化为 `%APPDATA%/sin/sin/settings.json`（Windows）或对应平台的 AppData 子目录下的 `settings.json`。

该设计在头文件注释中明确说明“类似 VS Code settings.json”，并提供 typed getter/setter（`getString` / `getInt` / `getBool` / `getDouble`）、默认值回退、`changed` 信号通知以及 `toJsonString` / `fromJsonString` 双向转换能力。

## 2. 关键文件与包

- **配置核心**：`src/core/appconfig.h`、`src/core/appconfig.cpp` —— 单例 `AppConfig`，负责加载、保存、读写 JSON。
- **设置 UI**：`src/ui/settingsdialog.h`、`src/ui/settingsdialog.cpp` —— VS Code 风格的设置对话框，提供分类树 + 搜索 + 直接编辑 JSON 两种模式。
- **启动入口**：`src/main.cpp` 在应用初始化时调用 `AppConfig::instance()->load()`。
- **消费者**：`src/core/projectmanager.cpp`、`src/core/sessionmanager.cpp`、`src/ui/mainwindow.cpp` 等模块通过 `AppConfig::instance()->get*` / `set*` 读取/写入配置项。
- **主题相关**：`src/ui/thememanager.*` 配合 `theme` 配置项切换样式表。
- **依赖**：`third_party/nlohmann_json/`（JSON 解析）、`third_party/spdlog/`（用于记录配置加载/保存日志）。

## 3. 架构与设计约定

- **单例生命周期**：`AppConfig::instance()` 使用函数内静态变量实现线程安全的单例；构造时即调用 `defaultConfig()` 填充内存中的 `m_data`（`nlohmann::json`），确保始终有可用默认值。
- **首次运行处理**：`load()` 检查 `settings.json` 是否存在，不存在则创建目录并写入默认配置，同时用 spdlog 记录信息日志。
- **容错策略**：JSON 解析失败时捕获 `json::parse_error`，降级为默认配置并记录错误日志；无法打开文件时同样回退到默认值。
- **键空间命名**：所有配置键采用点号分隔的层级命名（如 `trace.maxFrames`、`project.lastPath`、`log.level`），由 `defaultConfig()` 集中声明，形成一份“单一事实来源”的 schema。
- **UI 与配置解耦**：`SettingsDialog` 不直接硬编码业务逻辑，而是通过 `SettingMeta` 结构体（key、label、category、type、desc、comboChoices）描述每个设置项，再动态生成左侧分类树和右侧编辑器（bool → QCheckBox、int → QSpinBox、double → QDoubleSpinBox、string → QLineEdit、combo → QComboBox）。
- **双模编辑**：设置对话框支持“可视化列表页”和“JSON 编辑页”切换；后者直接将当前配置以格式化 JSON 文本展示，保存时先 `fromJsonString` 校验语法再落盘。
- **变更传播**：所有 setter 在修改 `m_data` 后 emit `changed(key)` 信号，供监听者响应（目前主要被 SettingsDialog 自身消费）。未实现自动持久化，需显式调用 `save()`。

## 4. 约定与约束

- **配置文件位置固定**：路径由 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.json"` 计算得出，不可通过环境变量或命令行参数覆盖。
- **新增配置项必须三处同步**：
  1. `AppConfig::defaultConfig()` 中声明默认值；
  2. `SettingsDialog::setupMetas()` 中注册元数据（key、label、category、type、desc、可选 comboChoices）；
  3. 业务代码通过对应的 typed getter/setter 访问。
- **类型安全**：getter 会校验 JSON 值类型（`is_string` / `is_number_integer` / `is_boolean` / `is_number`），类型不符时返回传入的默认值，不会抛出异常。
- **无环境/命令行配置层**：当前实现仅支持 `settings.json` 一种来源，没有合并 `.env`、命令行参数或系统环境变量的机制。
- **无增量保存**：每次 `save()` 都会将整个 `m_data` 重新 dump 到磁盘；`reset(key)` 仅删除 key 而不写盘，需后续 `save()` 生效。
- **项目最近工程列表限制**：README 文档明确指出 `project.recent` 仅存储文件路径字符串数组，无元数据（修改时间、设备类型、备注），因此无法搜索/筛选——这是已知的设计约束而非 bug。
- **会话状态独立于配置**：`sessionmanager.cpp` 中会话文件（`sessions.json`）与 `settings.json` 同目录但独立管理，避免会话数据污染用户偏好配置。

### 已验证的使用点

| 模块 | 使用的配置键 | 用途 |
|---|---|---|
| `mainwindow.cpp` | `project.lastPath`、`project.autoSaveOnClose` | 恢复上次工程路径、关闭时自动保存 |
| `projectmanager.cpp` | `project.lastPath` | 更新最近工程路径 |
| `sessionmanager.cpp` | `project.recent`、`project.recentMax` | 维护最近工程历史列表 |
| `thememanager.*` | `theme` | 切换界面主题 |
| `trace/graphic/recorder/log` 各组件 | `trace.*`、`graphic.*`、`record.*`、`log.*` | 控制行为与输出 |

## 5. 总结

这是一个轻量、自包含的 JSON 配置系统：以 `AppConfig` 单例为中心，以 `settings.json` 为唯一持久化载体，通过 typed API 和 VS Code 风格设置对话框暴露给用户。其优点是实现简单、无需外部依赖（除已引入的 nlohmann/json），缺点是没有多源合并、没有 schema 校验（运行时仅做基本类型检查）、没有热重载（需重启或手动刷新 UI）。对于本 CAN 调试工具而言，该方案足以满足用户偏好和项目状态的持久化需求。
---
kind: configuration_system
name: 基于 nlohmann/json 的用户配置系统（settings.json）与驱动设备配置
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/ui/settingsdialog.cpp
    - driver/kerneldlls/dll_cfg.ini
    - driver/kerneldlls/devices_property/default/candtu.ini
---

## 1. 使用的系统与框架

本仓库包含两类配置：
- **用户应用配置**：通过自实现的 `AppConfig` 单例，使用 `nlohmann::json` 将用户偏好持久化到 `%APPDATA%/sin/sin/settings.json`，提供类型安全的 getter/setter、默认值回退、JSON 文本导入/导出以及 Qt 信号 `changed(key)`。
- **CAN 驱动设备配置**：ZLG 驱动库自带的 INI 配置文件，位于 `driver/kerneldlls/dll_cfg.ini`（驱动 DLL 注册表）和 `driver/kerneldlls/devices_property/default/*.ini`（各硬件通道波特率、滤波等参数），由底层驱动直接读取。

没有发现 `.env`、`.yaml`、`.toml`、`application.properties` 或环境变量加载逻辑；所有运行时用户配置均集中在 `settings.json`。

## 2. 关键文件

| 文件 | 作用 |
|---|---|
| `src/core/appconfig.h` / `src/core/appconfig.cpp` | 应用配置核心：单例 `AppConfig`、默认配置 `defaultConfig()`、JSON 读写、路径解析、类型存取 |
| `src/ui/settingsdialog.cpp` | 设置 UI：声明式元数据 `m_metas`（key/label/category/type/desc/choices），动态生成树形编辑器，支持“编辑 JSON”模式与“重置为默认” |
| `driver/kerneldlls/dll_cfg.ini` | ZLG 驱动 DLL 映射表，定义内核层 CAN 驱动数量、起始 ID、DLL 名、位宽等 |
| `driver/kerneldlls/devices_property/default/candtu.ini` | 具体设备通道（CAN0/CAN1）的波特率、采样点、滤波器、Mode 等参数 |
| `resources/styles/theme.qss` / `resources/styles/default.qss` | 主题样式资源，配合 `theme` 配置项在运行时切换 |

## 3. 架构与设计约定

### 3.1 用户配置（AppConfig）

- **存储格式**：扁平键名的 JSON 对象，键按业务域分段，如 `window.*`、`trace.*`、`graphic.*`、`record.*`、`log.*`、`project.*`、`font.*`、`theme`。
- **默认值集中管理**：`AppConfig::defaultConfig()` 返回一个内联 `nlohmann::json`，首次运行自动写入磁盘；UI 的“重置为默认”通过 `fromJsonString(defaultConfig().dump(4))` 恢复。
- **类型安全访问**：提供 `getString/getInt/getBool/getDouble`，缺失键或类型不匹配时返回调用方传入的默认值，不会抛错。
- **变更通知**：每个 `set(key, value)` 都会 `emit changed(key)`，便于 UI 监听并刷新。
- **持久化策略**：`save()` 显式调用；析构时未自动保存（注释中“或由析构时自动保存”仅为文档描述，实际实现未覆盖）。UI 在 SettingsDialog 保存按钮处调用 `save()`。
- **路径定位**：使用 Qt `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` 拼接 `/settings.json`，不存在目录会自动创建。
- **错误处理**：JSON 解析失败会记录 `spdlog::error` 并回退到 `defaultConfig()`，保证程序可启动。

### 3.2 设置 UI（SettingsDialog）

- **声明式元数据驱动**：`setupMetas()` 中以 `add(key, label, category, type, desc, choices?)` 形式注册所有设置项，UI 据此自动生成分类树、搜索过滤、编辑器控件（bool→QCheckBox、int→QSpinBox、double→QDoubleSpinBox、string→QLineEdit、combo→QComboBox）。
- **双视图**：左侧“设置列表”视图与右侧“JSON 编辑”视图可切换；JSON 模式下直接编辑原始 `settings.json` 内容，点击保存时调用 `fromJsonString` 校验语法后再 `save()`。
- **实时生效**：控件值变化立即写入 `AppConfig`，无需等待保存即可被其他模块读取（如字体、主题、日志级别）。

### 3.3 驱动设备配置

- **dll_cfg.ini**：以 `[KERNELDLL_VCI_CAN]`、`[KERNELDLL_ZLG_CAN]`、`[KERNELDLL_CANSCOPE]` 三个段分别登记不同厂商/类型的驱动 DLL，每行格式为 `序号=DLL名 通道ID 位宽 标志`，由 ZLG 驱动库内部解析。
- **devices_property/default/*.ini**：每个硬件型号一个 INI 文件，包含 `[CAN0]`、`[CAN1]`、`[GEN]` 等段，字段包括 `BpsBRP/BpsSWJ/BpsSeg1/BpsSeg2/BpsSmp`（波特率/采样点）、`FltCNT/FltF*`（过滤器）、`Mode`、`NChan` 等，由驱动根据设备型号选择对应文件。

## 4. 约定与约束

- 新增用户配置项必须同时完成三件事：
  1. 在 `AppConfig::defaultConfig()` 中添加默认值；
  2. 在 `SettingsDialog::setupMetas()` 中用 `add(...)` 注册元数据（key、label、category、type、desc、可选 choices）；
  3. 若该配置影响 UI，需订阅 `AppConfig::changed(key)` 或在读取处直接取值。
- 配置键采用 `domain.key` 的分段命名（如 `trace.maxFrames`、`log.level`），避免键冲突。
- 所有配置值都允许缺失并使用默认值，禁止因缺少 key 导致崩溃。
- 驱动相关配置（DLL 注册、设备通道参数）**不由应用代码修改**，仅作为只读资源随安装包分发；应用侧不暴露对这些 INI 的编辑入口。
- 主题名称来源于 `ThemeManager::instance()->themeNames()`，并通过 `theme` 配置项选择，样式文件位于 `resources/styles/`。

## 5. 未发现的机制

- 未发现命令行参数配置、环境变量覆盖、多环境（dev/prod）配置分支、远程配置拉取、配置版本迁移脚本等高级特性。
- 未发现对 `settings.json` 的 schema 校验或 lint 规则，仅依赖 `nlohmann::json` 的运行时解析错误捕获。
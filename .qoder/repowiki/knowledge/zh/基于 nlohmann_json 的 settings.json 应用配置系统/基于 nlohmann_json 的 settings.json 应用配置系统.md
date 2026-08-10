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

## 1. 系统/方案概述

本仓库采用自实现的 `AppConfig` 单例作为统一配置中心，以 VS Code 风格的 `settings.json` 文件持久化用户设置。核心思路：
- 使用 `nlohmann::json` 作为底层存储格式（键值对 JSON）。
- 通过 `QStandardPaths::AppDataLocation` 定位配置文件路径 `%APPDATA%/sin/sin/settings.json`。
- 提供 typed getter/setter（`getString/getInt/getBool/getDouble` / `set(key, value)`），缺失键时回退到 `defaultConfig()` 中的默认值。
- 启动时调用 `load()` 加载；修改后调用 `save()` 持久化；也支持析构前自动保存（由调用方显式 `save()` 触发）。
- 暴露 `changed(key)` Qt 信号，供 UI 监听单项变更。
- 提供 `toJsonString()` / `fromJsonString()` 用于 Settings 对话框的“JSON 编辑器”模式。

该方案不依赖 Qt 的 `QSettings` 进行持久化（CMakeLists 中仅声明了 `<QSettings>` 头文件，但实际代码未使用），而是完全基于 JSON 文件 + Qt 文件系统 API。

## 2. 关键文件与包

| 文件 | 作用 |
|---|---|
| `src/core/appconfig.h` / `src/core/appconfig.cpp` | 配置单例、默认值、JSON 读写、typed 存取 |
| `src/ui/settingsdialog.h` / `src/ui/settingsdialog.cpp` | VS Code 风格设置界面（分类树 + 搜索 + JSON 编辑器） |
| `src/main.cpp` | 应用启动时调用 `AppConfig::instance()->load()` |
| `src/core/projectmanager.cpp` | 写入 `project.lastPath`、`project.recentMax` 等工程相关配置 |
| `src/core/sessionmanager.cpp` | 读取/维护 `project.recent` 最近项目列表 |
| `src/ui/mainwindow.cpp` | 读取 `project.autoSaveOnClose`、`project.lastPath` 等 |
| `third_party/nlohmann_json` | 第三方 JSON 库（已 vendored） |

## 3. 架构与约定

### 3.1 配置项命名空间约定
所有配置键采用 **点号分隔的命名空间** 形式，在 `defaultConfig()` 和 `SettingsDialog::setupMetas()` 中保持一致：
- `theme`, `font.family`, `font.size`, `window.*` — 通用/窗口
- `trace.*` — Trace 视图行为
- `graphic.*` — 波形图渲染参数
- `record.*` — 录制行为
- `filter.*` — 过滤器相关
- `log.*` — spdlog 日志级别与轮转
- `project.*` — 工程最近打开记录、自动保存开关

这种命名方式既是文档（通过 key 前缀表达类别），也是 Settings 对话框分类树的数据源（从 `SettingMeta.category` 聚合去重生成左侧分类）。

### 3.2 默认值集中管理
`AppConfig::defaultConfig()` 是唯一默认值来源。首次运行或解析失败时回退到此 JSON。`SettingsDialog::onReset()` 通过重新 `fromJsonString(defaultConfig().dump(4))` 实现“重置为默认”。新增配置项必须同时出现在 `defaultConfig()` 和 `setupMetas()` 中，否则无法在 UI 中编辑。

### 3.3 加载/保存流程
1. 应用启动 → `main.cpp` 调用 `AppConfig::instance()->load()`。
2. `load()` 确保目录存在；若文件不存在则写入默认配置并返回；若存在则解析 JSON，解析失败则降级到默认值并记录错误日志。
3. 运行时通过 `set()` 修改内存中的 `m_data`，并发出 `changed(key)`。
4. 通过 `save()` 将 `m_data` dump 为带缩进的 JSON 写入磁盘。
5. Settings 对话框支持两种编辑模式：
   - “设置列表”页：按类型动态创建控件（bool→QCheckBox、int→QSpinBox、double→QDoubleSpinBox、combo→QComboBox、string→QLineEdit），实时调用 `cfg->set(key, value)`。
   - “编辑 JSON”页：直接编辑原始 JSON 文本，点击“保存”时调用 `fromJsonString()` 再 `save()`。

### 3.4 与主题系统的集成
`theme` 配置项通过 `ThemeManager::instance()->themeNames()` 提供下拉选项，UI 层负责把主题名写回 AppConfig，再由主题管理器读取生效。

### 3.5 与工程/会话管理的集成
- `ProjectManager` 在打开/保存工程时更新 `project.lastPath`。
- `SessionManager` 维护 `project.recent` 数组（限制长度由 `project.recentMax` 控制），并在清理过期条目后调用 `save()` 持久化。
- `MainWindow` 在关闭时根据 `project.autoSaveOnClose` 决定是否自动保存工程。

## 4. 约定与约束

- **配置文件位置固定**：`%APPDATA%/sin/sin/settings.json`，由 `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/settings.json"` 计算，不可由外部覆盖。
- **JSON 格式要求**：必须是合法 JSON；非法 JSON 会被捕获并降级到默认值，同时记录 `spdlog::error`。
- **键类型安全**：getter 会检查值的 JSON 类型（`is_string/is_number_integer/is_boolean/is_number`），类型不符时返回传入的默认值，不会抛异常。
- **新增配置项的契约**：必须在 `defaultConfig()` 定义默认值，并在 `SettingsDialog::setupMetas()` 注册元数据（key、label、category、type、desc、可选 comboChoices），否则不会出现在设置界面。
- **线程模型**：`AppConfig` 继承 `QObject` 并通过 Qt 信号 `changed(key)` 通知变更；当前所有访问均发生在 GUI 线程（Qt 事件循环内），未见跨线程并发访问保护。
- **不使用环境变量或命令行参数注入配置**：整个应用配置仅来源于 `settings.json` 文件，未发现从 `QCoreApplication::arguments()` 或环境变量读取配置的逻辑。
- **未使用 Qt 的 QSettings**：尽管 CMakeLists 包含 `<QSettings>` 头，但全部配置读写走的是自定义 JSON 路径，`QSettings` 未被调用。

## 5. 适用范围说明

该配置系统覆盖应用级偏好（主题、字体、窗口尺寸）、功能开关（Trace/Graphic/Record/Filter/Log/Project），但不涉及设备驱动配置（如 `driver/kerneldlls/dll_cfg.ini` 属于 ZLG CAN 驱动的外部配置）、DBC 导入时的临时参数（这些由 `DbcImportDialog` 内部状态管理，不持久化）。
---
kind: configuration_system
name: 应用配置系统（settings.json + AppConfig 单例）
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/ui/settingsdialog.cpp
    - src/main.cpp
    - src/ui/thememanager.cpp
    - src/core/projectmanager.cpp
---

## 1. 系统与框架
- 基于 nlohmann/json 的 JSON 配置文件系统，文件名为 `settings.json`。
- 通过自定义 `AppConfig` 单例类提供类型安全的 getter/setter 与信号通知，不依赖 Qt 的 QSettings。
- 配置文件路径使用 Qt 标准路径 API：`QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)`，在 Windows 上等价于 `%APPDATA%/sin/sin/settings.json`。

## 2. 核心文件与包
- `src/core/appconfig.h` / `src/core/appconfig.cpp`：配置加载、保存、默认值、JSON 序列化/反序列化的核心实现。
- `src/ui/settingsdialog.cpp`：图形化设置编辑器，支持“设置列表”和“直接编辑 settings.json”两种模式。
- `src/main.cpp`：应用启动时调用 `AppConfig::instance()->load()` 完成配置初始化。
- `src/ui/thememanager.cpp`：主题管理，主题名由配置项 `theme` 驱动。
- `src/core/projectmanager.cpp`：工程最近路径等工程相关配置读写。

## 3. 架构与设计约定
- **单例访问**：所有模块通过 `AppConfig::instance()` 获取全局配置实例。
- **分层加载**：
  - 启动时先 `load()`，若文件不存在则写入 `defaultConfig()` 生成的默认 JSON。
  - 读取失败或解析异常时回退到默认值并记录日志。
- **类型安全存取**：提供 `getString/getInt/getBool/getDouble` 与对应 `set` 重载，缺失 key 或类型不匹配时返回默认值。
- **变更通知**：每次 `set` 后发射 `changed(key)` 信号，便于 UI 响应式更新。
- **持久化策略**：显式调用 `save()` 写盘；析构时不会自动保存，需由调用方控制时机。
- **批量导入导出**：`toJsonString()` / `fromJsonString()` 用于设置对话框中整份 JSON 的导入导出。
- **默认配置集中定义**：`defaultConfig()` 以键值对形式集中声明所有可配置项及其默认值，形成事实上的配置 schema。

## 4. 配置项分类与命名约定
配置键采用点号分层的命名空间风格，分为以下类别：
- `theme`, `font.family`, `font.size`, `window.*` — 通用界面与窗口行为
- `trace.*` — Trace 表格显示与缓冲策略
- `graphic.*` — 波形图渲染参数
- `record.*` — 录制格式与自动保存
- `filter.recentMax` — 过滤器历史数量
- `log.level`, `log.maxFileSize`, `log.maxFiles` — spdlog 日志轮转
- `project.lastPath`, `project.recent`, `project.recentMax`, `project.autoSaveOnClose` — 工程状态

## 5. 运行时行为与约束
- **首次运行**：自动创建目录并写入默认配置，同时输出日志。
- **解析失败回退**：JSON 解析异常时打印错误日志并使用默认配置，保证程序健壮性。
- **路径自动创建**：`save()` 前确保父目录存在。
- **线程模型**：未加锁保护，假设单线程 GUI 环境访问；多线程并发读写需外部同步。
- **环境变量**：构建脚本中使用 `SIN_QT_DIR`、`SIN_MINGW_DIR`、`SIN_CMAKE_DIR` 等环境变量，但这些属于构建期配置，不属于运行时应用配置。
- **主题切换**：通过 `ThemeManager::applyTheme(name)` 动态应用，主题名来源于配置的 `theme` 字段。

## 6. 与其他系统的集成
- 日志系统 (`logging`)：在 `AppConfig::load()` 之前初始化，日志级别由 `log.level` 控制。
- 主题系统 (`ThemeManager`)：默认主题为 `Light`，可通过配置改为 `Dark`、`VS Code Dark+` 等。
- 工程管理器 (`ProjectManager`)：读写 `project.*` 相关配置项。
- CMake 编译期：`src/CMakeLists.txt` 中包含 `<QSettings>` 引用，但实际运行时并未使用 QSettings，而是完全由自定义 AppConfig 接管。
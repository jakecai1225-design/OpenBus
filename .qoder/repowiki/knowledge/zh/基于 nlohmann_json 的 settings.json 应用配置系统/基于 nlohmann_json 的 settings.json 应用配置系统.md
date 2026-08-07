---
kind: configuration_system
name: 基于 nlohmann/json 的 settings.json 应用配置系统
category: configuration_system
scope:
    - '**'
source_files:
    - src/core/appconfig.h
    - src/core/appconfig.cpp
    - src/core/sessionmanager.h
    - src/core/sessionmanager.cpp
    - src/ui/settingsdialog.h
    - src/ui/settingsdialog.cpp
---

## 1. 使用的系统与框架

- **持久化格式**：纯 JSON 文件，使用 `nlohmann::json`（third_party 引入）进行读写。
- **存储位置**：通过 Qt `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` 定位用户 AppData 目录，再拼接子路径：
  - 全局应用设置：`settings.json`
  - 个人会话状态：`sessions.json`
- **单例模式**：`AppConfig`、`SessionManager` 均以静态 `instance()` 暴露全局访问点。
- **UI 层**：`SettingsDialog` 提供 VS Code 风格的图形化设置界面，同时支持直接编辑底层 `settings.json` 文本。

## 2. 核心文件与职责

| 文件 | 职责 |
|---|---|
| `src/core/appconfig.h/.cpp` | 全局应用配置：加载/保存 `settings.json`，typed getter/setter，默认值注入，`changed` 信号 |
| `src/core/sessionmanager.h/.cpp` | 个人会话状态：管理 `sessions.json`（最近工程、UI 几何、侧边栏状态等），首次启动时从 `AppConfig` 迁移旧数据 |
| `src/ui/settingsdialog.h/.cpp` | 设置对话框：按元数据驱动 UI，支持分类树、搜索、JSON 直编、重置为默认 |
| `resources/styles/theme.qss` / `styles/theme.qss` | 主题样式资源（由 `theme` 配置项驱动） |

## 3. 架构与设计约定

### 3.1 配置分层

- **全局应用设置（`AppConfig`）**：跨会话生效的用户偏好，如主题、字体、Trace 行为、录制格式、日志级别、工程最近列表上限等。位于 `AppData/sin/settings.json`。
- **个人会话状态（`SessionManager`）**：仅当前用户会话内有效的状态，如窗口 geometry、活跃面板、最近打开记录、固定标记等。位于 `AppData/sessions.json`。
- 两者解耦：`SessionManager` 在 `load()` 中检测 `sessions.json` 不存在时，会读取 `AppConfig` 中的 `project.recent` 并迁移到新的 `recent` 数组结构，然后删除旧键。

### 3.2 默认值与回退机制

- `AppConfig::defaultConfig()` 集中声明所有键及其默认值（字符串、整数、布尔、浮点、数组），覆盖 theme/font/window/trace/graphic/record/filter/log/project 等分组。
- 首次运行或 JSON 解析失败时，自动写入默认配置并记录日志。
- typed getter（`getString/getInt/getBool/getDouble`）在 key 不存在或类型不匹配时返回传入的默认值，保证调用方无需判空。

### 3.3 键命名约定

- 采用 `category.key` 形式的点号分隔命名空间（如 `window.width`、`trace.maxFrames`、`log.level`、`project.lastPath`），便于在 SettingsDialog 中按 category 分组展示。
- 新增配置项需同时在 `defaultConfig()` 和 `SettingsDialog::setupMetas()` 中注册元数据（label、category、type、desc、可选 comboChoices），否则不会出现在图形界面中。

### 3.4 变更通知

- 每次 `set(key, value)` 后 emit `changed(key)` 信号，供订阅者响应配置变化（例如 ThemeManager 可监听 `theme` 键切换主题）。
- `SessionManager` 修改后直接 `save()` 并 emit `recentChanged()` 等语义化信号。

### 3.5 迁移策略

- `SessionManager::migrateFromAppConfig()` 将旧版 `project.recent`（字符串数组）迁移为新版 `recent`（对象数组，含 path/type/name/modified/pinned 字段），同时将 `project.lastPath` 迁移到 `lastOpened`，最后清理 AppConfig 中的旧键并保存。
- 迁移过程对异常容错：解析失败时跳过并继续。

## 4. 约定与约束

- **配置文件位置不可自定义**：路径由 `QStandardPaths::AppDataLocation` + 文件名硬编码生成，无命令行参数或环境变量覆盖机制。
- **JSON 必须合法**：读写均依赖 `nlohmann::json::parse/dump`，解析失败会记录错误并使用默认值；SettingsDialog 的 JSON 编辑页保存时会校验语法，非法则提示“JSON 解析失败”。
- **目录自动创建**：`QDir().mkpath(...)` 确保父目录存在后再写文件。
- **线程模型**：代码未显式加锁，`AppConfig`/`SessionManager` 以单例形式被多处调用，应假设调用方串行访问或在 Qt 事件循环上下文中使用。
- **配置项扩展方式**：新增一个配置项需要三处改动——`defaultConfig()` 添加默认值、`SettingsDialog::setupMetas()` 注册元数据、必要时在业务逻辑中添加 typed getter/setter 调用。
- **无环境变量/命令行配置**：未发现 `.env`、`.ini`、`--config` 等外部配置入口，所有运行时配置均来自 `settings.json` 及内存默认值。
- **无加密/密钥管理**：配置文件中不包含敏感信息（密码、令牌等），如需扩展应自行增加加密层。
- **日志级别受配置驱动**：`log.level` 对应 spdlog 的输出级别（trace/debug/info/warn/error/critical），由 SettingsDialog 的 combo 限定枚举值。

## 5. 与其他子系统交互

- `ThemeManager` 通过读取 `theme` 配置项切换 QSS 样式。
- `SessionManager` 依赖 `AppConfig` 读取 `project.recentMax` 限制最近列表长度。
- 录制模块使用 `record.defaultFormat` 决定新文件的默认导出格式（sin/asc/blf）。
- Trace/Graphic/Filter 等 UI 行为均通过 `AppConfig` 的 typed getter 获取运行时开关。
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

本仓库使用基于 nlohmann/json 的轻量级应用配置系统，通过单例 `AppConfig` 管理用户设置，并以 VS Code 风格的 `SettingsDialog` 提供可视化编辑与 JSON 直编两种交互方式。配置文件采用扁平键名（如 `font.size`、`trace.maxFrames`），默认存储于 `%APPDATA%/sin/sin/settings.json`，首次运行自动写入默认值。

**核心架构**
- `AppConfig` 单例：封装 JSON 内存模型与文件 I/O，提供 typed getter/setter（string/int/bool/double）、`reset()` 删除 key、`toJsonString()/fromJsonString()` 批量导入导出、`defaultConfig()` 返回内置默认值。所有 set 操作通过 Qt 信号 `changed(key)` 通知监听者。
- `SettingsDialog`：左侧分类树 + 右侧设置项列表，支持按类别/文本搜索；同时提供“编辑 JSON”页面直接修改 settings.json 内容。设置项元数据由 `SettingMeta` 结构体描述（key、label、category、type、desc、comboChoices），在 `setupMetas()` 中集中定义。
- 启动流程：`main.cpp` 中先初始化日志，再调用 `AppConfig::instance()->load()` 加载配置，随后应用主题并创建主窗口。

**配置分层与默认值**
- 默认配置集中在 `AppConfig::defaultConfig()` 中，覆盖通用（theme/font/window）、Trace、Graphic、Record、Filter、Log、Project 等类别。
- 读取时若文件不存在或解析失败，回退到默认配置并记录日志；保存时自动创建目录并格式化输出（缩进 4）。
---
kind: frontend_style
name: Qt6 桌面应用样式系统（QSS + 主题引擎）
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - src/ui/thememanager.cpp
    - src/ui/thememanager.h
    - UI/css/ui-prototype.css
---

本项目为基于 Qt6 的 CAN 报文分析工具，前端样式采用 **Qt StyleSheet (QSS)** 与自定义主题引擎相结合的双层架构：静态默认样式通过 `resources/styles/default.qss` 提供，运行时主题切换由 C++ 端的 `ThemeManager` 动态生成并应用。同时保留了一份 HTML/CSS 原型文件 `UI/css/ui-prototype.css`，用于界面原型的快速验证。
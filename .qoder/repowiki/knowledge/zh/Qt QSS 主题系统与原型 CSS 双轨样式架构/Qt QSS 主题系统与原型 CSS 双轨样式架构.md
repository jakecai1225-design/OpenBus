---
kind: frontend_style
name: Qt QSS 主题系统与原型 CSS 双轨样式架构
category: frontend_style
scope:
    - '**'
source_files:
    - resources/styles/default.qss
    - src/ui/thememanager.h
    - src/ui/thememanager.cpp
    - UI/css/ui-prototype.css
    - UI/ui-prototype.html
    - UI/js/ui-loader.js
    - UI/js/ui-prototype.js
---

本项目的 UI 样式采用「Qt QSS + 独立 HTML/CSS 原型」双轨体系：运行时通过 Qt 的 QSS（Qt Style Sheets）实现主题化，原型阶段使用独立的 HTML+CSS 文件进行界面设计验证。
# 用户手册截图指南

手册 Markdown 中通过 `![说明](images/文件名.png)` 引用截图。  
当前仓库可先用同名 **SVG 占位图** 预览版式；正式发版前请用实机截图替换为 **PNG**（建议宽度 1280–1600 px）。

## 拍摄约定

1. Windows 缩放 100% 或 125%，窗口最大化。
2. 使用 Light 主题（与当前产品默认一致）。
3. 尽量裁掉无关桌面；保留 OpenBus 标题栏与活动栏。
4. 文件名与下表 **完全一致**（便于替换占位图）。
5. 若导出 PDF，PNG 优于 JPEG。

## 清单

| 文件名 | 对应章节 | 拍摄内容 |
|--------|----------|----------|
| `01-welcome.png` | 介绍 | 首次启动 Welcome 全貌 |
| `02-project-sidebar.png` | 快速上手 | Project 侧栏 EXPLORER |
| `02-device-connect.png` | 快速上手 | Device 配置 + Connect |
| `02-flow-start.png` | 快速上手 | Flow 画布与 Start |
| `02-trace-filter.png` | 快速上手 | Trace + 过滤栏有数据 |
| `02-dbc-graphic.png` | 快速上手 | DBC 树与 Graphic 波形 |
| `03-workbench-overview.png` | 工作台 | 全界面，可手绘分区标注 |
| `03-command-palette.png` | 工作台 | Ctrl+Shift+P 命令面板 |
| `04-project-explorer.png` | 工程 | Project 侧栏 Recent |
| `05-device-page.png` | 设备 | 设备页完整表单 |
| `06-flow-canvas.png` | Flow | 测量拓扑画布 |
| `07-trace-main.png` | Trace | Trace 主列表 |
| `07-filter-bar.png` | Trace | 过滤栏特写 |
| `08-graphic-main.png` | Graphic | 多信号波形 |
| `09-dbc-detail.png` | Database | 左树右表 |
| `10-send.png` | 收发 | Send 列表 + 编辑区 |
| `10-playback.png` | 收发 | Playback 文件列表 |
| `10-record.png` | 收发 | Record 表单 |
| `10-offline.png` | 收发 | Offline 文件列表 |
| `11-market.png` | 扩展 | 市场卡片网格 |
| `12-watcher.png` | 工具 | Watcher 双页之一 |

## 替换步骤

1. 按上表截取 PNG，放入本目录。  
2. 删除或保留同名 `.svg` 占位（Markdown 默认引用 `.png`）。  
3. 用支持 Mermaid 的阅读器（如 VS Code / Cursor）预览各章链接是否正常。

## 从 Markdown 导出

可选工具：

- [Pandoc](https://pandoc.org/)：`pandoc README.md 0*.md -o openbus-user-manual.pdf`
- Typora / Obsidian / VS Code Markdown PDF 插件

导出前确认 `images/` 下 PNG 已齐。

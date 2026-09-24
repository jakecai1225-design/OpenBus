# 14. 常见问题（FAQ）

## 14.1 连接相关

**Q：点击 Connect 无反应或失败？**  
A：确认驱动已安装且设备被系统识别；查看底部 **Output**；尝试更换 USB 口 / 关闭占用该设备的其他软件；确认通道已勾选、波特率有效。

**Q：为什么 Connect 成功了 Trace 仍无数据？**  
A：还需在 **Flow** 中 **Start** 测量；确认过滤栏没有过滤掉全部帧；确认总线确实有报文（可用模拟器或对端发送验证）。

**Q：CAN FD 数据段配置是灰的？**  
A：将 **CAN mode** 设为 CAN FD；若设备为仅 Classic 的驱动（如部分 SLCAN），FD 会被禁用。

## 14.2 Trace / Graphic

**Q：过滤语法报错？**  
A：点击过滤栏帮助查看语法；表达式为空时表示不过滤。

**Q：Graphic 没有波形？**  
A：确认已加载 DBC、已添加信号、测量正在运行、时间轴范围内有采样。

**Q：列表卡顿？**  
A：收紧过滤；减少同时打开的 Graphic 曲线；结束不需要的测量窗口。

## 14.3 工程与文件

**Q：打开旧工程部分窗口没了？**  
A：使用 **View > Reset Layout**；检查工程文件是否完整；重新从侧栏 New Trace / New Graphic。

**Q：录制文件在哪？**  
A：Record 页中的目录与前缀；可用 **打开目录** 在资源管理器中查看。

## 14.4 扩展

**Q：市场安装失败？**  
A：检查网络、磁盘权限与包校验；查看 **Extensions** 输出页。

**Q：插件启动后找不到窗口？**  
A：部分套件为独立窗口，检查任务栏；或从侧栏 Running / Installed 再次激活。

## 14.5 获取支持

- **Help > Report Issue**
- **Help > Documentation** / 官方网站（以菜单链接为准）
- 反馈时请附带：软件版本（About）、系统版本、设备型号、Output 关键日志摘要

---

## 附录 A. 术语

| 术语 | 说明 |
|------|------|
| CAN FD | CAN with Flexible Data-Rate |
| DBC | 报文 / 信号定义数据库文件 |
| Trace | 报文追踪列表 |
| Graphic | 信号波形图 |
| Flow | 测量配置 / 测量启停 |
| Panel | 底部共享输出区 |

## 附录 B. 截图清单

完整拍摄说明见 [images/README.md](images/README.md)。

---

← [快捷键](13-shortcuts.md) · [手册首页](README.md)

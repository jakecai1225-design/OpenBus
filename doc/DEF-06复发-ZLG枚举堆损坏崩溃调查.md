# DEF-06 复发：启动/枚举期间 ZLG 厂商线程踩堆导致 c0000374 崩溃

- 日期：2026-08-24
- 状态：已定位（gdb 现场捕获），待实施修复
- 关联：DEF-06（历史：外置驱动枚举堆损坏，已做缓解未根治）

## 1. 现象

- 用户场景：接入 ZLG USBCANFD-200U 硬件，打开 Trace + Graphic，静置数分钟后程序退出。
- Windows 事件日志：`openbus.exe` APPCRASH，异常码 **0xc0000374（STATUS_HEAP_CORRUPTION）**，错误模块 ntdll.dll。
- spdlog 滚动日志在崩溃前丢失最后一段（缓冲未刷盘），仅存启动序列。

## 2. gdb 现场捕获（第一轮，无人操作自动复现）

```
#11 msvcrt!free
#12 Qt6Core.dll!QObject::~QObject()
#13 BusStatistics::~BusStatistics          ← 崩溃点（受害块）
#15 Qt6Core.dll!QObjectPrivate::deleteChildren()
#16 Qt6Widgets.dll!QWidget::~QWidget()
#17 MainWindow::~MainWindow (mainwindow.cpp:146)
#18 qMain (main.cpp:65)                    ← 正常退出路径
```

要点：
1. 崩溃发生在**程序退出析构阶段**（`app.exec()` 已返回）—— free BusStatistics 堆块时检测到堆元数据损坏。
2. 崆溃受害者（BusStatistics）不是凶手：**堆在运行期已被越界写**，退出时首次 free 触发检测。
3. gdb 无人值守复现：程序启动后**自动退出**（无用户操作），复现窗口 = 启动后数秒~2 分钟。

## 3. 根因（代码注释自证的已知问题）

`src/core/candevice_zlg.cpp::enumerate()`（DEF-06 复现缓解注释）：

```
L780: 厂商接收线程在 ZCAN_CloseDevice 后仍短暂活动（崩溃现场曾观察到
      close 后继续打印 CCanDeviceRecvDataThread 日志，堆块被踩即此窗口）
```

gdb 运行输出证实：**每次枚举扫描期间，ZLG SDK 每 ~100ms 创建一组厂商线程**
（CCanDeviceRecvDataThread / CCanDeviceRecvDataCmdThread，tid 持续变化），与
7 设备类型 × 4 索引 = 28 次 `ZCAN_OpenDevice` 探测的线程创建模式吻合。

### 崩溃链

```
DevicePanel 构造 / setDeviceManager / 扫描按钮 / driversChanged
  → ICanDevice::enumerateAll() → DriverRegistry::enumerateDevices()
  → CanDeviceZLG::enumerate()
  → 28 × ZCAN_OpenDevice（多数失败路径同样触发 SDK 线程创建）
  → ZLG 厂商线程在 open/close 后短暂活动（SDK 内部缺陷）
  → 厂商线程越界写踩坏用户堆块（随机受害者：本次为 BusStatistics）
  → 程序退出（exec 返回）析构阶段 free 损坏块 → c0000374
```

### 现有缓解为何失效

| 缓解措施 | 失效原因 |
|---|---|
| 探测阶段只收集 POD（无堆分配） | 只保护"枚举循环内新分配"，**已存在的堆块**（BusStatistics 等）照样被踩 |
| `found` 非空才 msleep(150) | 无设备（`found` 空）时不等待；且 150ms 不足以保证 SDK 线程完全静默 |
| 静态查表取名 / move 转移 | 缩小活动面，但厂商线程踩堆是**外部写**，无法靠自身代码风格规避 |

## 4. 触发面（每次启动至少 2 次枚举）

- `DevicePanel` 构造 → `populateTree()` → 枚举
- `setDeviceManager()` → `refreshDevices()` → 枚举
- 用户点「扫描设备」按钮 → 枚举
- 驱动安装/卸载 `driversChanged` → 枚举
- 连接硬件流程的设备确认 → 枚举

## 5. 修复方案（待评审）

### 方案 A：探测进程隔离（推荐，根治）

新增 `openbus_probe.exe` 独立小进程执行 ZLG OpenDevice/CloseDevice 扫描：

- probe 进程：加载 zlgcan.dll → 7×4 扫描 → stdout 输出 JSON（设备列表）→ 退出
- `CanDeviceZLG::enumerate()` 改为 `QProcess` 启动 probe + 解析 JSON
- 堆被踩只影响 probe 子进程；probe 崩溃/超时按"未发现设备"处理，主程序毫发无损
- 附带收益：SDK 线程日志（CCanDeviceRecvDataThread 刷屏）隔离在子进程
- 成本：新增构建目标（~100 行）、部署一份 exe；单次枚举增加 ~100ms 进程开销

### 方案 B：无条件静默等待（缓解加强，不根治）

- 扫描结束后**无条件** msleep(300~500ms) 等 SDK 线程静默（含 found 为空路径）
- OpenDevice 失败路径同样计入静默等待
- 局限：SDK 线程静默前踩堆窗口依然存在，只能降低概率

### 方案 C：禁止启动自动枚举（回避）

- 启动不枚举，用户手动扫描
- 局限：扫描动作本身仍触发踩堆窗口，仅降低暴露频率

## 6. 建议决策点

1. 是否采用方案 A（进程隔离）？—— 唯一根治路径
2. probe 超时阈值（建议 3s）与崩溃兜底策略（返回空列表 + 日志告警）
3. 外置驱动插件（市场 .odp）路径是否同步进程化（当前仅内置 ZLG 可用，可暂缓）

## 7. 证据附件

- gdb 第一轮全线程栈：见会话终端输出（BusStatistics 析构 free 崩溃）
- WER 事件：AppCrash openbus.exe P7=c0000374（2026-08-24 10:14:20）
- SDK 线程日志模式：gdb 捕获 CCanDeviceRecvDataThread tid 序列（启动后 2s 内 12 条）

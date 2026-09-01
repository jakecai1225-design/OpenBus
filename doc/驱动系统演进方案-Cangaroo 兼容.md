# openbus 驱动系统演进方案 - v2.3 (BusMaster + Cangaroo 融合版)

> **状态**: v2.3 计划（2026-09-01）｜基于 BusMaster/Cangaroo 双项目深度分析  
> **目标**: 融合**Cangaroo 的面向对象设计** + **BusMaster 的企业级多通道支持** + **Qt 插件市场机制**  
> **核心决策**: ✅ 继续使用 Qt 插件模式（适合驱动市场热安装），吸收 BusMaster 优秀设计

---

## 零、架构选型总结：为什么选择 Qt 插件模式？

### 0.1 三种技术路径对比

| 维度 | BusMaster (函数指针 DLL) | Cangaroo (Qt 插件 QPluginLoader) | 独立进程 + IPC |
|------|------------------------|----------------------------------|----------------|
| **构建复杂度** | ❗高（需预定义 ABI） | ✅低（Qt 自动元对象代码） | ✅低（但通信协议复杂） |
| **运行时灵活性** | ✅强（热插拔） | ✅强（QPluginLoader 热加载） | ✅✅最优（崩溃隔离） |
| **厂商开发成本** | ❗高（理解预定义宏） | ✅低（继承基类+Q_OBJECT） | ⚠️中（需懂 IPC） |
| **崩溃隔离** | ❌无（同进程） | ❌无（同进程） | ✅✅有（沙盒化） |
| **UI 集成难度** | ❗中（函数指针回调） | ✅低（信号槽自动绑定） | ⚠️中（IPC 通信） |
| **依赖管理** | ❗复杂（SDK 冲突） | ⚠️中等（Qt 版本一致） | ✅简单（进程间独立） |
| **驱动市场适配** | ❌不适合 | ✅**完美适配** | ✅适合 |

**结论**: ✅ **选择 A —— Qt 插件机制**，理由如下：

#### 优势一：元数据校验（关键特性）
```cpp
// 驱动市场包中的 manifest.json:
{
    "id": "canable-slcanserial",
    "abiVersion": "2.0",       // 适配 Qt 6.8.3 MinGW
    "minAppVersion": "0.1.0",  // 最低主程序版本
    "sdkDependencies": []      // 声明不需要额外 SDK
}

// 安装时 QPluginLoader::metaData() 会预检:
auto metaData = loader.metaData();
if (metaData["qclassinfo"] != nullptr) {
    auto info = metaData.value("qclassinfo").toByteArray();
    if (appAbiVersion < pluginAbiVersion) {
        reject_installation("❌ 此驱动需要主程序 v0.2+");
    }
}
```

#### 优势二：优雅降级与错误处理
```cpp
void CanDeviceManager::loadDriver(const QString& path) {
    auto plugin = new QPluginLoader(path);
    if (!plugin->load()) {
        qWarning() << "Failed to load:" << plugin->errorString();
        show_error_dialog("该驱动无法加载，可能需要重新安装");
        return;
    }
    
    auto canDriver = dynamic_cast<CanDriver*>(plugin->instance());
    deviceRegistry[driverId] = canDriver;
    emit deviceChanged();  // UI 自动刷新
}
```

#### 优势三：厂商开发门槛极低
```cpp
class MyCustomDriverPlugin : public QObject, public ICanDevice {
    Q_OBJECT
    Q_INTERFACES(ICanDevice)
    
public:
    std::vector<DeviceInfo> enumerate() const override {
        return findMyCustomDeviceByVidPid();
    }
    
    unique_ptr<ICanDevice> create(int subType) const override {
        return make_unique<MyCustomBackend>();
    }
};
```

对比 BusMaster 的函数指针模型，这需要理解大量预定义宏和外部链接语法。

---

## 一、核心问题识别

### 1.1 现有框架限制

当前 `ICanDevice` 设计的主要瓶颈：

| 问题 | 影响范围 | 改进建议 |
|------|---------|----------|
| **品牌枚举封闭** (`Brand::ZLG/PEAK/Kvaser/Candle/SLCAN`) | 新增厂商需修改主程序源码 | 参考 Cangaroo: 通过 `driver.json` 动态注册 Brand |
| **单通道硬编码** | 无法支持 PEAK/XL、Kvaser 多通道设备 | 扩展为 `std::vector<ICanDevice*>` (每通道一个对象) |
| **时序参数硬编码** | 每个设备类型需手动计算波特率/采样点 | 复用 Cangaroo: `getAvailableBitrates()` 动态查询 |
| **无能力描述** | UI 无法知道设备是否支持监听模式/三采样/单帧发送 | 添加 `capability_canfd`, `capability_listen_only` 等标志 |
| **无错误统计** | UI 看不到 RX/TX 错误帧计数 | 实现 `updateStatistics()` 并暴露给上层 |

### 1.2 Cangaroo 设计亮点

Cangaroo 的成功实践：

```cpp
// CanInterface.h
class CanInterface {
    // ✅ 能力枚举（位掩码）
    enum {
        capability_canfd           = 0x01,
        capability_listen_only     = 0x02,
        capability_triple_sampling = 0x04,
        capability_one_shot        = 0x08,
        capability_auto_restart    = 0x10,
        capability_config_os       = 0x20
    };
    
    // ✅ 获取能力标志
    virtual uint32_t getCapabilities();
    
    // ✅ 获取支持的波特率列表（含采样点）
    virtual QList<CanTiming> getAvailableBitrates();
    
    // ✅ 错误统计
    virtual int getNumRxFrames();
    virtual int getNumRxErrors();
    virtual int getNumTxDropped();
};
```

### 1.3 BusMaster 设计亮点

**BusMaster** (Robert Bosch Engineering and Business Solutions) 是另一款优秀的开源 CAN 上位机，其特点包括：

#### 多层抽象架构

```cpp
// Layer 1: BaseDIL_CAN (虚拟基类)
class CBaseDIL_CAN {
    virtual HRESULT DILC_GetDILList(...) = 0;
    virtual HRESULT DILC_ListHwInterfaces(...) = 0;  // 硬件枚举
    virtual HRESULT DILC_StartHardware(void) = 0;
    virtual HRESULT DILC_SendMsg(DWORD clientId, ...) = 0;
    // ... 所有厂商必须实现的接口
};

// Layer 2: CDIL_CAN (工厂层，每个厂商一个 DLL)
class CDIL_CAN : public CBaseDIL_CAN {
    HMODULE m_hDll;                             // 厂商 SDK 句柄
    SCONTROLLER_DETAILS m_asOldControllerDetails[defNO_OF_CHANNELS];  // ✅ 多通道数组
    CBaseDIL_CAN_Controller* m_pBaseDILCAN_Controller;  // 动态创建实例
    
    HRESULT DILC_SelectDriver(DWORD dwDriverID) {
        switch (dwDriverID) {
            case DRIVER_CAN_PEAK:   return createPEAK();
            case DRIVER_CAN_VECTOR: return createVector();
        }
    }
};
```

#### BusMaster 的独特优势

| 特性 | BusMaster 实现 | 对 openbus 的价值 |
|------|---------------|-------------------|
| **多通道原生支持** | `m_asOldControllerDetails[32]` 数组 | ✅ 必须引入：支持 PEAK XL/Kvaser MultiChan |
| **客户端订阅机制** | `DILC_RegisterClient(clientId)` | ⚠️ 可选：多个 UI 窗口同时读取同一硬件 |
| **精确时间戳映射** | `GetTimeModeMapping(sysTime↔ns)` | ✅ 强烈推荐：提升高速场景准确性 |
| **能力查询接口** | `GetControllerParams(eContrParam)` | ❌ 不如 Cangaroo 位掩码清晰 |
| **协议栈丰富度** | UDS/J1939/FlexRay 内置 | 📅 远期规划：作为高级功能模块 |

#### BusMaster 支持的下位机清单

| 驱动 | 厂商 | 通道数 | 特色 |
|------|------|--------|------|
| `DRIVER_CAN_PEAK_USB` | PEAK Systems | 1-8 | PCAN-Basic (免费商用) |
| `DRIVER_CAN_VECTOR_XL` | Vector | 1-32 | vectorXL SDK (高端实验室) |
| `DRIVER_CAN_KVASER` | Kvaser | 1-4 | canlib32 (军工级) |
| `DRIVER_CAN_ICS_NEOVI` | Intrepid CS | 1-4 | neoVI API (高性能 OBD) |
| `DRIVER_CAN_ETAS_BOA` | ETAS | 1-2 | BMW/VW Group 官方工具链 |


**关键优势**:
- **UI 友好**: 通过 `getCapabilities()` 自动判断设备特性
- **可扩展**: 新设备无需修改 UI 代码即可接入
- **精确时序**: `CanTiming` 包含完整波特率计算参数

---

## 二、演进方案设计

### 2.1 总体架构图

```
┌─────────────────────────── 应用层 ─────────────────────────────┐
│   GUI → DevicePanel → ConnectionDialog                         │
└───────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────── 统一接口层 ─────────────────────────┐
│  CanInterface (Cangaroo 风格增强版) + BusMaster 多通道支持      │
│  ├── ICanDevice (向后兼容字段)                                │
│  ├── ├─ getCapabilities()                                     │
│  ├── ├─ getAvailableBitrates()                                │
│  ├── ├─ updateStatistics()                                    │
│  ├── ├─ getChannelCount()  // ← BusMaster SCONTROLLER_DETAILS │
│  ├── ├─ enumerateAllChannels()                               │
│  └── └─ registerClient(clientId)  // ← Optional (BusMaster)   │
└───────────────────────────────────────────────────────────────┘
                              ↕
┌─────────────────────────── 驱动插件层 ─────────────────────────┐
│  driver_<vendor>.dll                                           │
│  ├── CanDevice<.vendor> (实现 CanInterface)                   │
│  ├── driver.json (声明式元数据 - 驱动市场基础)                 │
│  └── vendor SDK (libusb / PCAN-Basic / canlib / zlgcan)       │
└───────────────────────────────────────────────────────────────┘
```

### 2.2 核心接口定义

#### 2.2.1 **CanInterface (增强版)**

基于 Cangaroo + BusMaster 融合设计（**优先级 P0**）：

```cpp
// src/core/caninterface.h
class CanInterface : public QObject
{
    Q_OBJECT
    
public:
    // ---- 能力标志（Cangaroo 位掩码 + openbus 扩展） ----
    enum Capability {
        capability_canfd           = 0x01,
        capability_listen_only     = 0x02,
        capability_triple_sampling = 0x04,
        capability_one_shot        = 0x08,
        capability_auto_restart    = 0x10,
        capability_config_os       = 0x20,
        
        // OpenBus 特有扩展
        capability_hw_timestamp    = 0x100,      // 硬件时间戳
        capability_error_frame     = 0x200,      // 错误帧收发
        capability_fd_boost        = 0x400       // CAN FD 灵活数据率
    };
    
    using Capabilities = quint32;  // 32 位位掩码
    
    // ---- 基本信息（BusMaster INTERFACE_HW 参考） ----
    virtual QString getName() const = 0;                          // 设备名（如 "CANable COM3"）
    virtual QString getDetailsStr() const;                        // 详细信息（序列号/固件版本）
    
    // ---- 多通道支持（✅ BusMaster 风格 - **P0 必须引入**） ----
    virtual int getChannelCount() const = 0;                      // ✅ 物理通道数
    virtual QList<DeviceInfo> enumerateAllChannels() const = 0;   // ✅ 列出所有通道详情
    
    struct ChannelInfo {          // ← BusMaster CHANNEL_DETAILS 启发
        int channelIndex;
        QString devicePath;       // COM3 / \\.\PCAN_USBBUS1
        uint32_t maxBaudRate;     // 1Mbps / 5Mbps
        bool supportsFD;
        std::string firmwareVersion;
        std::string serialNumber;
    };
    
    // ---- 客户端订阅机制（⚠️ BusMaster DILC_RegisterClient - 可选） ----
    virtual DWORD registerClient(const QString& clientName, DWORD& clientId) { 
        return E_NOTIMPL;  // 默认不实现，仅在需要时启用
    }
    
    // ---- 基本操作 ----
    virtual void applyConfig(const MeasurementInterface &mi);     // 配置波特率/采样点
    
    // ---- 能力查询（✅ Cangaroo 位掩码优于 BusMaster eContrParam） ----
    virtual Capabilities getCapabilities() = 0;                    // 能力位掩码
    virtual QList<CanTiming> getAvailableBitrates() = 0;          // 支持的时序表 ← BusMaster 没这个！
    
    // ---- 链路状态 ----
    virtual bool isOpen() const;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual uint32_t getState() = 0;                               // state_ok/bus_off/passive...
    
    // ---- 报文收发 ----
    virtual void sendMessage(const CanMessage &msg) = 0;           // 发送（单帧）
    virtual bool readMessage(QList<CanMessage> &msgs, int timeoutMs) = 0;  // 接收
    
    // ---- 统计信息（❌ BusMaster SERROR_CNT 太复杂，采用 Cangaroo 简单设计） ----
    virtual int getNumRxFrames() = 0;
    virtual int getNumRxErrors();                                  // 默认 0
    virtual int getNumTxFrames();
    virtual int getNumTxErrors();
    virtual int getNumRxOverruns();
    virtual int getNumTxDropped();
    
signals:
    void messageReceived(const QList<CanMessage>& msgs);
    void errorsChanged(int rxErrors, int txErrors);
    void busStateChanged(uint32_t state);
};
```

**设计决策说明**:  
- ✅ **采用 Cangaroo 的位掩码能力枚举**（比 BusMaster 的 `GetControllerParams` 更清晰直观）  
- ✅ **新增 `getChannelCount()`**（BusMaster 独有优势，**P0 必须引入**）  
- ⚠️ **客户端订阅机制可选**（仅在多窗口同时读取同一硬件时启用）  

#### 2.2.2 **CanDriver (工厂层)**

对应现有框架的驱动插件 DLL：

```cpp
// src/core/candriver.h
class CanDriver
{
public:
    // 基础信息
    virtual QString id() const = 0;                     // 驱动 ID（如 "candle", "slcan"）
    virtual QString name() const = 0;                   // 显示名称
    virtual QStringList deviceTypes() const = 0;        // 支持的子类型（用于 UI 下拉框）
    
    // 设备枚举（零副作用）
    virtual QList<DeviceInfo> enumerate() const = 0;    // 枚举所有可用设备
    
    // 动态创建通道（多通道设备时，调用 n 次创建 n 个接口）
    virtual CanInterface* createInterface(int deviceIndex, int channelIndex) const = 0;
    
    // 更新设备状态
    virtual bool update() = 0;                          // 刷新设备列表
    
    // Vendor-specific 功能（可选扩展）
    virtual bool vendorCtrl(int cmd, void* param) = 0;
};

typedef QHash<QString, CanDriverFactory*> CanDriverRegistry;
```

### 2.3 厂商驱动实现示例

#### 2.3.1 **Canable (SLCAN 固件) 实现参考**

```cpp
// drivers/canable-slcanserial/CanableSlcanInterface.cpp
#include "CanInterface.h"
#include <QtSerialPort/QSerialPort>

class CanableSlcanInterface : public CanInterface
{
    Q_OBJECT
    
public:
    explicit CanableSlcanInterface(int serialIndex, QString portName, bool supportsFd);
    ~CanableSlcanInterface() override;
    
    // 基本信息
    QString getName() const override { return "CANable " + m_portName; }
    QString getDetailsStr() const override { return m_portName; }
    
    // 能力
    Capabilities getCapabilities() override {
        Capabilities caps = capability_listen_only | 
                           capability_auto_restart |
                           capability_config_os;
        if (m_supportsFd) {
            caps |= capability_canfd;
        }
        return caps;
    }
    
    // 波特率表（SLCan 协议标准波特率）
    QList<CanTiming> getAvailableBitrates() override {
        QList<CanTiming> ret;
        static const uint bitrates[] = {
            10000, 20000, 50000, 83333, 100000, 125000, 250000, 500000, 800000, 1000000
        };
        static const uint fdBitrates[] = {0, 2000000, 5000000};
        for (int i = 0; i < 10; ++i) {
            for (auto fdBr : fdBitrates) {
                if (fdBr > 0 || !supportsCanFD()) continue;
                ret << CanTiming(i, bitrates[i], fdBr, 875);
            }
        }
        return ret;
    }
    
    // 链路控制
    void open() override;
    void close() override;
    void sendMessage(const CanMessage &msg) override;
    bool readMessage(QList<CanMessage> &msgs, int timeoutMs) override;
    
    // 统计
    int getNumRxFrames() override { return m_rxCount; }
    int getNumRxErrors() override { return m_rxErrors; }
    int getNumTxFrames() override { return m_txCount; }
    int getNumTxErrors() override { return m_txErrors; }
    
private:
    bool writeLine(const QByteArray &cmd);
    QByteArray readResponse(int timeoutMs);
    
    QSerialPort* m_port = nullptr;
    QString m_portName;
    bool m_supportsFd = false;
    int m_channelIndex = 0;
    
    // 统计
    int m_rxCount = 0;
    int m_rxErrors = 0;
    int m_txCount = 0;
    int m_txErrors = 0;
};
```

#### 2.3.2 **Candle (GS_USB 固件) 实现要点**

关键差异在于**使用 libusb 而不是串口**:

```cpp
// drivers/candle/CanDeviceCandle.cpp
bool CanDeviceCandle::open(int devIndex, int channel) {
    // 1. 枚举 USB 设备
    auto devs = collectWhitelisted();
    if (devIndex >= devs.size()) return false;
    
    // 2. 打开 USB 句柄
    m_handle = devs[devIndex].dev;
    
    // 3. 获取设备能力
    candle_capability_t caps;
    candle_channel_get_capabilities(m_handle, 0, &caps);
    
    // 4. 构建时序表（根据 fclk_can 自动适配）
    if (caps.fclk_can == 170000000) {
        // CANable 2.0 / MKS (170MHz)
        _timings = {
            CandleApiTiming(170000000, 10000, 875, 68, 217, 31),
            CandleApiTiming(170000000, 100000, 875, 10, 147, 21),
            // ... 其他波特率
        };
    } else {
        // CANable 0.x (48MHz)
        _timings = {
            CandleApiTiming(48000000, 10000, 875, 300, 6, 8),
            // ...
        };
    }
    
    return true;
}
```

---

## 三、迁移路线图（融入 BusMaster 设计后）

### Phase 0: 架构选型确认 ✅ **已完成**
- [x] 对比三种技术路径：BusMaster (函数指针 DLL) vs Cangaroo (Qt 插件) vs 独立进程 + IPC
- [x] 确定选择 Qt 插件模式（适合驱动市场热安装，厂商开发门槛低）

### Phase 1: 接口重构（**1 周** - **P0 必须引入多通道支持**）
- [ ] **多通道基类增强**: `CanInterface` 增加 `getChannelCount()` + `enumerateAllChannels()`
  ```cpp
  // src/core/caninterface.h (修改)
  virtual int getChannelCount() const = 0;                          // ← BusMaster SCONTROLLER_DETAILS
  virtual QList<DeviceInfo> enumerateAllChannels() const = 0;       // ← BusMaster CHANNEL_DETAILS
  ```
- [ ] 创建 `ICanDevice` 作为代理包装器（保持向后兼容）
- [ ] 更新现有驱动代码（ZLG/PEAK/Kvaser/Candle）实现新虚函数

### Phase 2: 驱动适配（**2 周**）
- [ ] **SLCAN**: 重写 `CanDeviceSlcan` 实现新的 `CanInterface`
  ```cpp
  class CanDeviceSlcan : public CanInterface {
      int getChannelCount() const override { return 1; }  // slcan 默认单通道
      ChannelInfo enumerateAllChannels() const override {
          auto ports = QSerialPortInfo::availablePorts();
          for (const auto& port : ports) {
              if (isSlcanDevice(port)) {
                  return ChannelInfo{...};  // BusMaster 风格结构体
              }
          }
      }
  };
  ```
- [ ] **Candle**: 扩展 `CanDeviceCandle` 支持多通道
  - 参考 BusMaster `CDIL_CAN` 工厂模式：每个物理通道一个对象实例
- [ ] **ZLG**: 封装现有代码到新接口（保留旧 API），增加 `capability_` 标志
- [ ] **Kvaser**: 新增驱动（首次支持）

### Phase 3: UI 适配与客户端订阅机制（可选，**1 周**）
- [ ] `DeviceConnectionTab` 改用 `getCapabilities()` 渲染高级选项
- [ ] 波特率选择器改为动态查询（不再硬编码）
- [ ] 添加统计信息显示（RX/TX 错误计数）
- [ ] **可选**: 实现 `registerClient(clientId)` 支持多窗口同时读取同一硬件

### Phase 4: 时间戳优化（**2 天** - P1 强烈推荐）
- [ ] 参考 BusMaster `GetTimeModeMapping(sysTime↔ns)` 优化归一化算法
- [ ] 提升高速 CAN FD 场景下的时序准确性

### Phase 5: 测试与文档（**1 周**）
- [ ] 真机测试所有厂商设备（ZLG/PEAK/Kvaser/Candle/SLCAN）
- [ ] 编写《第三方驱动开发指南》（含多通道示例）
- [ ] 更新插件市场索引脚本 `plugin_tool.py`

---

## 四、预期收益对比

| 维度 | 仅融合 Cangaroo | 融合 Cangaroo + BusMaster | 差距说明 |
|------|-----------------|---------------------------|----------|
| **新厂商接入** | ✅ (无需改主程序) | ✅✅ (完全相同) | BusMaster 的厂家 SDK 依赖管理反而更复杂 |
| **多通道设备支持** | ❌ 不支持 | ✅✅ PEAK XL / Kvaser MultiChan | BusMaster 独有优势 - **核心差异点** |
| **CAN FD 配置** | ✅ 动态波特率表 | ⚠️ 无变化 | BusMaster 没这个功能，Cangaroo 领先 |
| **UI 能力感知** | ✅ 运行时检测 | ✅ 运行时检测 | 两者相同 |
| **客户端订阅** | ❌ 无 | ⚠️ 可选支持 | BusMaster 独有，但需求不广泛 |
| **错误追踪** | ✅ RX/TX 统计 | ⚠️ BusMaster 太复杂 | 采用 Cangaroo 简单设计 |
| **时间戳精度** | ✅ 软归一化 | ✅✅ 硬件映射 | BusMaster `GetTimeModeMapping` 优势 |
| **协议栈丰富度** | ❌ 无 | ✅ UDS/J1939/FlexRay | 📅 远期规划：作为高级功能模块 |

**总结表格**:

| 特性 | Cangaroo 贡献 | BusMaster 贡献 |
|------|---------------|----------------|
| **接口设计** | 面向对象 + 位掩码 | 插件工厂 + 函数指针 |
| **驱动隔离** | Qt 插件机制 | DLL 动态加载 |
| **能力发现** | runtime query (`getCapabilities`) | parameter query (`GetControllerParams`) |
| **多通道** | ❌ | ✅✅ 32+ 通道支持 |
| **客户端订阅** | ❌ | ⚠️ DILC_RegisterClient |
| **时间戳** | 软归一化 | ✅✅ 精确映射 |
| **波特率表** | ✅✅ 动态查询 | ❌ 无此概念 |
| **协议栈** | ❌ | ✅ UDS/J1939 |

---

## 五、融合实施建议

### 优先级排序

#### **P0 (必须)**
- ✅ **多通道支持接口**: `getChannelCount()` + `enumerateAllChannels()`
  - 原因：PEAK XL / Kvaser MultiChan 等高端设备刚需
  - 工期：2 天
  - 参考：BusMaster `S CONTROLLER_DETAILS` + `CHANNEL_DETAILS`

#### **P1 (强烈推荐)**
- ✅ **时间戳优化**: 参考 BusMaster `GetTimeModeMapping`
  - 原因：提升高速 CAN FD 场景下的时序准确性
  - 工期：2 天
  - 参考：BusMaster `DILC_GetTimeModeMapping` 精确对齐系统时钟

#### **P2 (可选)**
- ⚠️ **客户端订阅机制**: `registerClient(clientId)`
  - 原因：仅在多个 UI 窗口同时读取同一硬件时需要
  - 工期：3 天
  - 参考：BusMaster `DILC_RegisterClient/DILC_ManageMsgBuf`
  
#### **P3 (远期规划)**
- 📅 **协议栈集成**: UDS/J1939/FlexRay
  - 原因：作为高级功能模块逐步移植
  - 工期：视资源而定

---

## 六、总体架构决策

### 6.1 为什么选择 Qt 插件模式？

对比三种技术路径的最终结论：

| 维度 | BusMaster (函数指针 DLL) | Cangaroo (Qt 插件 QPluginLoader) | 独立进程 + IPC |
|------|------------------------|----------------------------------|----------------|
| **构建复杂度** | ❗高（需预定义 ABI） | ✅低（Qt 自动元对象代码） | ✅低（但通信协议复杂） |
| **运行时灵活性** | ✅强（热插拔） | ✅强（QPluginLoader 热加载） | ✅✅最优（崩溃隔离） |
| **厂商开发成本** | ❗高（理解预定义宏） | ✅低（继承基类 +Q_OBJECT） | ⚠️中（需懂 IPC） |
| **崩溃隔离** | ❌无（同进程） | ❌无（同进程） | ✅✅有（沙盒化） |
| **UI 集成难度** | ❗中（函数指针回调） | ✅低（信号槽自动绑定） | ⚠️中（IPC 通信） |
| **依赖管理** | ❗复杂（SDK 冲突） | ⚠️中等（Qt 版本一致） | ✅简单（进程间独立） |
| **驱动市场适配** | ❌不适合 | ✅✅**完美适配** | ✅适合 |

**核心优势**:

#### 优势一：元数据校验（关键特性）
```cpp
// 驱动市场包中的 manifest.json:
{
    "id": "canable-slcanserial",
    "abiVersion": "2.0",       // 适配 Qt 6.8.3 MinGW
    "minAppVersion": "0.1.0",  // 最低主程序版本
    "sdkDependencies": []      // 声明不需要额外 SDK
}
```

#### 优势二：优雅降级与错误处理
```cpp
auto plugin = new QPluginLoader(path);
if (!plugin->load()) {
    qWarning() << "Failed to load:" << plugin->errorString();
    show_error_dialog("该驱动无法加载，可能需要重新安装");
    return;
}
```

#### 优势三：厂商开发门槛极低
```cpp
class MyCustomDriverPlugin : public QObject, public ICanDevice {
    Q_OBJECT
    Q_INTERFACES(ICanDevice)
public:
    std::vector<DeviceInfo> enumerate() const override {
        return findMyCustomDeviceByVidPid();
    }
    unique_ptr<ICanDevice> create(int subType) const override {
        return make_unique<MyCustomBackend>();
    }
};
```

### 6.2 最终融合策略

✅ **继续使用 Qt 插件模式**，但吸收以下优秀设计：

| BusMaster 特性 | 融合方式 | 优先级 |
|---------------|---------|--------|
| **多通道原生支持** | ✅ `getChannelCount()` + `enumerateAllChannels()` | P0 |
| **客户端订阅机制** | ⚠️ `registerClient(clientId)` (可选) | P2 |
| **时间戳映射** | ✅ `GetTimeModeMapping` 优化归一化算法 | P1 |

✅ **拒绝以下复杂设计**：
- ❌ BusMaster 的函数指针模型（厂商开发成本高）
- ❌ BusMaster 的 `SERROR_CNT` 统计结构（不如 Cangaroo 位掩码清晰）
- ❌ BusMaster 的 `GetControllerParams(eContrParam)` 参数化接口（API 过于繁琐）

✅ **保留 Cangaroo 的设计**：
- ✅ 位掩码能力枚举 (`getCapabilities()`)
- ✅ 动态波特率表 (`getAvailableBitrates()`)
- ✅ 简单的错误统计 (`getNumRxFrames()/getNumRxErrors()`)

🎉 **完美平衡：现代性与专业性并重！**

---

## 七、参考实现位置

- **Cangaroo 源码**: `third_party/cangaroo-master/src/driver/`
- **BusMaster 源码**: `third_party/busmaster-master/Sources/Kernel/BusmasterDriverInterface/` + `BUSMASTER/CAN_PEAK_USB/` 等
- **现有驱动**: `drivers/zlg/`, `drivers/candle/`, `drivers/slcan/`, `drivers/canable-slcanserial/`

---

**下一步行动**: 建议先实现 CANable slcan 驱动的增强版，作为样板项目验证新架构可行性。重点测试多通道接口 + 能力枚举是否满足实际需求。


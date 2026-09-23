#ifndef OPENBUS_CORE_MODULE_IMODULE_H
#define OPENBUS_CORE_MODULE_IMODULE_H

#include <functional>

#include <QIcon>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QWidget>

/**
 * @file imodule.h
 * @brief 业务模块接口 — 壳（openbus.exe）与业务模块（openbus_*.dll）的唯一契约
 *
 * 设计原则（doc/拆分应用实施方案.md §4.2）：
 *  - 业务 DLL 只导出一个 C 工厂函数（openbus_create<Xxx>Module）返回本接口，
 *    模块内部所有类均为 DLL 私有，不跨边界导出类/符号
 *  - 接口只依赖 Qt 基础类型与 data 层（openbus_data.dll）类型，
 *    禁止出现壳的具体类型（SplitEditorArea / ActivityBar 等）
 *  - 接口只增不改：新增能力以新的虚函数（带默认实现）或 ShellContext
 *    新字段追加，不修改既有签名
 */

class Player;
class Recorder;
class CanDeviceManager;
class CanSimulator;
class DbcManager;

/**
 * @brief 业务模块上下文 — 壳提供给业务模块的全部服务入口
 *
 * 壳（MainWindow）每次创建页面时构造并传入；模块保存拷贝使用。
 * 数据层对象实例由壳创建、全进程唯一，此处只传指针（前向声明），
 * 模块 cpp 按需 include data 层完整头。
 */
struct ShellContext {
    QWidget *mainWindow = nullptr;   ///< 主窗口（对话框 parent、居中定位等）

    // ---- B2 增：数据层服务（实例由壳装配，代码均在 openbus_data.dll）----
    Player *player = nullptr;             ///< 回放器（core/player.h）
    Recorder *recorder = nullptr;         ///< 录制器（core/recorder.h）
    CanDeviceManager *deviceManager = nullptr;  ///< 设备管理（core/candevicemanager.h）
    CanSimulator *simulator = nullptr;    ///< 模拟器（core/cansimulator.h）
    DbcManager *dbcManager = nullptr;     ///< DBC 管理（core/dbcmanager.h）

    // ---- B2 增：壳服务回调（底部输出 / 问题面板）----
    std::function<void(const QString &)> appendOutput;
    std::function<void(int level, const QString &source, const QString &message)> addProblem;

    /**
     * @brief 模块 → 壳反向动作（与 IBusinessModule::invoke 镜像）
     *
     * 已约定动作（壳侧分发见 MainWindow::makeShellContext）：
     *  - "play" / "pause" / "stop"        回放控制（走壳的完整播放链路）
     *  - "setSpeed"   (arg = double)      回放倍速
     *  - "seek"       (arg = double 秒)   回放跳转
     *  - "setAutoScroll" (arg = bool)     Trace 自动滚动
     *  - "clearTraceGraphic"              清空全部 Trace/Graphic 视图数据
     *  - "updateActions"                  刷新菜单/工具栏动作状态
     *  - "statusMessage" (arg = QString)  状态栏消息
     *  - "signalDoubleClicked" (arg = QVariantList{canId, signalName})
     *  - "signalAddToTrace"    (arg = QVariantList{canId, signalName})
     *      DBC 详情页信号联动（B3）：双击/加到 Graphic 走壳的信号→Graphic 编排，
     *      加到 Trace 走壳的信号→Trace 编排
     *  - "openOfflineAnalysis" / "openDevicePage"
     *      Flow 页跳转请求（B4）→ 壳打开对应标签页
     *  - "measurementToggled" (arg = bool)
     *  - "measurementReplay"  (no arg) — clear Trace/Graphic and restart from beginning
     *      Flow 测量启停（B4）→ 壳离线加载 + Trace/Graphic 实例门控
     *  - "moduleToggled" (arg = QVariantList{blockId, name, enabled})
     *  - "moduleOpened"   (arg = QVariantList{moduleId, instanceId})
     *  - "moduleInstanceClosed" (arg = QVariantList{moduleId, instanceId})
     *      Flow 画布块操作（B4）→ 壳实例编排
     *  - "dbcRemoveRequested" (arg = QString 文件名) → 壳关关联页 + unloadDbc
     *  - "connMessage" (arg = QString) / "deviceDisconnected"
     *      设备连接页状态反馈（B4）→ 状态栏/测量状态/Trace 门控
     *  - "frameDoubleClicked" (arg = CanFrame)
     *      Trace 双击帧（B5）→ 壳的帧→过滤 + 帧信号→Graphic 编排
     *  - "frameAddToGraphic" (arg = CanFrame)
     *      Trace 右键"添加信号到 Graphic"（B5）→ 壳查 DBC 加全部信号
     *  - "traceSelectionChanged" (arg = int rows)
     *      Trace 选中行数变化（B5）→ 状态栏"选中N行"
     *  - "traceFileLoaded" (arg = int frameCount)
     *      Trace 文件拖放加载完成（B5）→ 底部输出 + 状态栏帧数
     */
    std::function<void(const QString &action, const QVariant &arg)> shellInvoke;
};

/**
 * @brief 业务模块接口
 */
class IBusinessModule {
public:
    virtual ~IBusinessModule() = default;

    /// 模块标识（"trace" / "graphic" / "market" ...，全局唯一）
    virtual QString id() const = 0;
    /// 标签页默认标题（本地化文案，可被用户改名）
    virtual QString title() const = 0;
    /// 模块图标（活动栏 / 标签图标，资源路径 :/icons/... 由壳的 qrc 提供）
    virtual QIcon icon() const = 0;

    /**
     * @brief 创建模块主标签页内容 widget
     *
     * 壳负责放入编辑区并接管生命周期：标签页关闭即 widget 销毁，
     * 再次打开时壳重新调用本函数。模块内部只保存裸指针并监听
     * QObject::destroyed 置空。
     */
    virtual QWidget *createWidget(ShellContext &ctx) = 0;

    /**
     * @brief 模块通用动作（字符串约定，接口只增不改）
     *
     * 已约定动作：
     *  - market: "refreshInstalled"（重取已装列表）
     *            "focusSearch"（聚焦搜索框，＋新增设备跳转联动）
     *            "revealItem"（arg = MarketItem，定位展示条目）
     *            "installLocalFile"（arg = QString 路径，离线安装包）
     *  - transceive: "setRecording"（arg = bool）
     *                "setFileInfo"（arg = QVariantList {name, total, totalTime}）
     *                "setProgress"（arg = QVariantList {cur, total, curTime, totalTime}）
     *                "setPlayerLoaded"（arg = QVariantList {loaded, playing}）
     *                "loadRecordConfig" / "loadSendEntries" / "loadPlaybackConfig"
     *                "addOfflineFiles"
     *  - trace（B5）: "onFrame"(CanFrame) / "setAutoScroll"(bool)
     *                / "setRunning"(QVariantList{id, bool}) / "setRunningAll"(bool)
     *                / "clearTraceAll" / "clearAll"
     *                / "appendFrames"(QVariantList{QWidget* target, QVariantList frames})
     *                / "setFilterExpression"(QVariantList{QWidget*, expr, report})
     *                / "jumpToFrame"(QVariantList{QWidget*, int}) / "editColorRules"
     *                / "setColorRules"(QVariantList{QWidget*, QVariantList rules})
     *  - graphic（B5）: "onFrame"(CanFrame) / "setFlowEnabled"(QVariantList{QWidget*, bool})
     *                / "clearDataAll"
     *                / "addSignal"(QVariantList{QWidget*, sigMap}) — sigMap 字段：
     *                  {name, canId, extended, color?, dbcSig?}，dbcSig 用
     *                  core/dbcdata.h 的 dbcSignalToMap() 序列化
     *                / "addSignals"(QVariantList{QWidget*, QVariantList<sigMap>})
     *                / "loadSignalConfigs"(QVariantList{QWidget*, QVariantList<sigMap>})
     *                  — sigMap 追加 displayMode 字段
     *
     * 默认实现为空操作；不支持的动作静默忽略。
     */
    virtual void invoke(const QString &action, const QVariant &arg = {})
    {
        Q_UNUSED(action);
        Q_UNUSED(arg);
    }

    // ---- B2 增：多页面模块支持（transceive = 发送/回放/离线分析/录制 4 页）----

    /// 模块全部页面 id（默认空 = 单页面模块，仅 createWidget）
    virtual QStringList pages() const { return {}; }

    /**
     * @brief 创建指定页面（多页面模块实现；未知 pageId 返回 nullptr）
     *
     * 壳按用户操作调用（打开"录制"标签 → createPage("record", ctx)）。
     * 单页面模块无需覆写。
     */
    virtual QWidget *createPage(const QString &pageId, ShellContext &ctx)
    {
        Q_UNUSED(pageId);
        Q_UNUSED(ctx);
        return nullptr;
    }

    /**
     * @brief 创建带参数页面（B3 增；只增不改原则下的参数化扩展）
     *
     * param 语义由 pageId 约定：
     *  - dbc  "detail"  : param = QString（DBC 文件名，多实例页，每次调用新建）
     *  - flow "device"  : param = QVariantList{deviceKind, devIndex, deviceName, deviceType}
     *
     * 默认转发到无参版本，旧模块不受影响。
     */
    virtual QWidget *createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
    {
        Q_UNUSED(param);
        return createPage(pageId, ctx);
    }

    /**
     * @brief 模块状态查询（返回无效 QVariant = 不支持该查询）
     *
     * 已约定查询：
     *  - transceive: "offlineFiles"（离线分析页文件列表 → QStringList）
     *                / "recordConfig"（录制页 UI → QVariantMap）
     *                / "sendEntries"（发送页列表 → QVariantList）
     *                / "playbackConfig"（回放页 UI → QVariantMap）
     *  - trace: "isTrace"(QWidget*→bool) / "instance"(id→QWidget*)
     *           / "filterExpression"(id→QString) / "frameCount"(QWidget*→int)
     *           / "activeInstance"(→QWidget* 首个存活实例)
     *           / "colorRules"(id|QWidget*→QVariantList)
     *  - graphic: "isGraphic"(QWidget*→bool) / "instance"(id→QWidget*)
     *             / "lastInstance"(→QWidget*) / "signalConfigs"(QWidget*→
     *             QVariantList{name, canId, extended, displayMode, dbcSig, color})
     *             / "dataWindow"(→QWidget*)
     */
    virtual QVariant query(const QString &what, const QVariant &arg = {})
    {
        Q_UNUSED(what);
        Q_UNUSED(arg);
        return {};
    }
};

#endif // OPENBUS_CORE_MODULE_IMODULE_H

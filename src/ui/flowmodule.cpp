#include "flowmodule.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接

#include "measurementsetupview.h"
#include "deviceconnectiontab.h"
#include "core/canframe.h"
#include "core/dbcmanager.h"
#include "core/candevicemanager.h"
#include "core/player.h"
#include "core/cansimulator.h"
#include "core/protocol/protocolregistry.h"     // M3：注册表管道（doc/flow.md §13.4）
#include "core/protocol/parserregistry.h"
#include "core/protocol/busdefinitionstore.h"
#include "utils/logging.h"

#include <QPointer>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

// openbus_flow.dll 唯一显式导出的符号（拆分方案 §4.2 决策 1）。
// 工厂按模块唯一命名（openbus_create<Xxx>Module），避免多 DLL 链接期同名冲突。
extern "C" IBusinessModule *openbus_createFlowModule()
{
    return new FlowModule;
}

QString FlowModule::id() const
{
    return QStringLiteral("flow");
}

QString FlowModule::title() const
{
    return QStringLiteral("Flow");
}

QIcon FlowModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/flow.svg"));
}

QStringList FlowModule::pages() const
{
    return { QStringLiteral("setup"), QStringLiteral("device") };
}

QWidget *FlowModule::createWidget(ShellContext &ctx)
{
    return createSetupPage(ctx);
}

QWidget *FlowModule::createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
{
    m_ctx = ctx;
    if (pageId == QStringLiteral("setup"))
        return createSetupPage(ctx);
    if (pageId == QStringLiteral("device"))
        return createDevicePage(param, ctx);
    return nullptr;
}

QWidget *FlowModule::createPage(const QString &pageId, ShellContext &ctx)
{
    // 两参重载转发（DEF-10）：接口默认实现返回 nullptr，壳侧无参调用
    // （onOpenMeasurementSetup 的 "setup" / onOpenDeviceTab 的 "device"）
    // 会拿到空页导致 Flow 页打不开
    return createPage(pageId, QVariant(), ctx);
}

void FlowModule::cachePage(const QString &pageId, QWidget *page)
{
    m_pages.insert(pageId, page);
    // 标签页关闭即析构：同步移除缓存，下次请求重建
    QObject::connect(page, &QObject::destroyed, page, [this, pageId](QObject *) {
        m_pages.remove(pageId);
    });
}

QWidget *FlowModule::createSetupPage(ShellContext &ctx)
{
    if (QWidget *cached = m_pages.value(QStringLiteral("setup")))
        return cached;
    m_ctx = ctx;

    auto *view = new MeasurementSetupView(ctx.mainWindow);

    // 已加载 DBC 列表同步（数据层操作，模块侧完成）
    QStringList dbcFiles;
    for (const auto &f : ctx.dbcManager->files())
        dbcFiles << f.fileName;
    view->setDbcFiles(dbcFiles);
    // DEF-08 字符串信号：DbcManager 定义于 data.dll，本模块在 openbus_flow.dll
    auto *dbcLoadedRelay = new SignalRelay(view);
    dbcLoadedRelay->fire0 = [view, ctx]() {
        QStringList files;
        for (const auto &f : ctx.dbcManager->files())
            files << f.fileName;
        view->setDbcFiles(files);
    };
    QObject::connect(ctx.dbcManager, SIGNAL(dbcLoaded(QString)),
                     dbcLoadedRelay, SLOT(fire()));
    auto *dbcUnloadedRelay = new SignalRelay(view);
    dbcUnloadedRelay->fire0 = [view, ctx]() {
        QStringList files;
        for (const auto &f : ctx.dbcManager->files())
            files << f.fileName;
        view->setDbcFiles(files);
    };
    QObject::connect(ctx.dbcManager, SIGNAL(dbcUnloaded(QString)),
                     dbcUnloadedRelay, SLOT(fire()));

    // 数据源切换：直接操作数据层（停另一侧数据源）
    QObject::connect(view, &MeasurementSetupView::sourceChanged, view, [ctx](int src) {
        if (src == static_cast<int>(MeasurementSetupView::Source::File)) {
            ctx.simulator->stop();
            ctx.deviceManager->stop();
            ctx.appendOutput(QStringLiteral("数据源切换：离线分析"));
        } else {
            ctx.player->stop();
            ctx.appendOutput(QStringLiteral("数据源切换：硬件实时"));
        }
    });

    // 跨模块编排 → 壳
    QObject::connect(view, &MeasurementSetupView::fileBrowseRequested, view, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("openOfflineAnalysis"), {});
    });
    QObject::connect(view, &MeasurementSetupView::realBlockClicked, view, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("openDevicePage"), {});
    });
    QObject::connect(view, &MeasurementSetupView::fileBlockClicked, view, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("openOfflineAnalysis"), {});
    });
    QObject::connect(view, &MeasurementSetupView::sendPageOpened, view, [this]() {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("sendPageOpened"), {});
    });
    QObject::connect(view, &MeasurementSetupView::playbackPageOpened, view, [this]() {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("openPlaybackTab"), {});
    });
    QObject::connect(view, &MeasurementSetupView::measurementToggled, view,
                     [this](bool running) {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("measurementToggled"), running);
    });
    QObject::connect(view, &MeasurementSetupView::moduleToggled, view,
                     [this](const QString &blockId, const QString &name, bool enabled) {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("moduleToggled"),
                              QVariantList{ blockId, name, enabled });
    });
    QObject::connect(view, &MeasurementSetupView::moduleOpened, view,
                     [this](const QString &moduleId, const QString &instanceId) {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("moduleOpened"),
                              QVariantList{ moduleId, instanceId });
    });
    QObject::connect(view, &MeasurementSetupView::moduleInstanceClosed, view,
                     [this](const QString &moduleId, const QString &instanceId) {
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("moduleInstanceClosed"),
                              QVariantList{ moduleId, instanceId });
    });
    QObject::connect(view, &MeasurementSetupView::dbcRemoveRequested, view,
                     [this](const QString &fileName) {
        // 卸载涉及关闭壳标签页 → 委托壳（壳内再调 DbcManager::unloadDbc）
        if (m_ctx.shellInvoke)
            m_ctx.shellInvoke(QStringLiteral("dbcRemoveRequested"), fileName);
    });

    // 解析器选择：模块侧完成（对话框 parent = 壳窗口，加载走数据层）
    // M3 冷路径试点（doc/flow.md §13.4）：文件过滤器由 acceptedParsers()
    // 生成，加载动作经 ParserRegistry → DbcParser → DbcManager 直通
    // ——「UI → 注册表 → 适配器/解析器 → 既有管理器」样板代码路径，
    // 后续 EtherCAT / 通用 Flow / 第三方协议接入照抄此模式
    QObject::connect(view, &MeasurementSetupView::dbcSelectRequested, view, [this]() {
        // database 块异常标记：加载失败红闪、成功后熄灭（加载结果就地可见）
        auto *setupView = qobject_cast<MeasurementSetupView *>(
            m_pages.value(QStringLiteral("setup")));
        // 1) 过滤器：CAN 适配器声明的解析器扩展名聚合（当前 *.dbc，行为一致）
        QStringList nameFilters;
        auto *can = ProtocolRegistry::instance()->findAdapter(QStringLiteral("can"));
        if (can) {
            for (const QString &pid : can->acceptedParsers()) {
                if (auto *p = ParserRegistry::instance()->findParser(pid)) {
                    for (const QString &ext : p->fileExtensions())
                        nameFilters << QStringLiteral("*.") + ext.toLower();
                }
            }
        }
        if (nameFilters.isEmpty())
            nameFilters << QStringLiteral("*.dbc");   // 注册表缺项兜底
        const QString filter = QStringLiteral("协议描述文件 (%1);;所有文件 (*.*)")
                                   .arg(nameFilters.join(QLatin1Char(' ')));

        const QString path = QFileDialog::getOpenFileName(
            m_ctx.mainWindow, QStringLiteral("导入协议描述文件"), {}, filter);
        if (path.isEmpty())
            return;

        // 2) 管道：按扩展名路由解析器 → 定义集入 store（统一建模）
        IBusParser *parser = ParserRegistry::instance()->findParserForExtension(
            QFileInfo(path).suffix());
        if (!parser) {
            m_ctx.addProblem(1, QStringLiteral("DBC"),
                             QStringLiteral("无匹配解析器: ") + path);
            if (setupView)
                setupView->setBlockError(QStringLiteral("database"), true);
            return;
        }
        QString parseError;
        const BusDefinitionSet set = parser->parse(path, &parseError);
        if (set.isEmpty()) {
            m_ctx.addProblem(1, QStringLiteral("DBC"), parseError);
            if (setupView)
                setupView->setBlockError(QStringLiteral("database"), true);
            return;
        }
        BusDefinitionStore::instance()->addDefinitionSet(set);

        // 3) 直通：DbcManager 保持原样加载（外部行为不变，双入口并存，§6.4）
        if (m_ctx.dbcManager->loadDbc(path)) {
            m_ctx.appendOutput(QStringLiteral("已加载 DBC: ")
                               + QFileInfo(path).fileName());
            // 加载成功：清除 database 块历史错误标记（红闪熄灭）
            if (setupView)
                setupView->setBlockError(QStringLiteral("database"), false);
        } else {
            m_ctx.addProblem(1, QStringLiteral("DBC"),
                             QStringLiteral("加载失败: ") + path);
            if (setupView)
                setupView->setBlockError(QStringLiteral("database"), true);
        }
    });

    // Filter 块过滤规则变更提示（v1.6：多 CAN 通道块收编为单 Filter 块）
    QObject::connect(view, &MeasurementSetupView::filterRulesChanged, view,
                     [this](const QStringList &rules) {
        m_ctx.appendOutput(
            QStringLiteral("Flow 过滤规则已更新（%1 条）").arg(rules.size()));
    });

    cachePage(QStringLiteral("setup"), view);
    return view;
}

QWidget *FlowModule::createDevicePage(const QVariant &param, ShellContext &ctx)
{
    m_ctx = ctx;
    auto *tab = qobject_cast<DeviceConnectionTab *>(
        m_pages.value(QStringLiteral("device")));
    if (!tab) {
        tab = new DeviceConnectionTab(ctx.mainWindow);
        tab->setSimulator(ctx.simulator);
        tab->setDeviceManager(ctx.deviceManager);

        // V2 信号 — 真实硬件连接（数据层操作，模块侧完成）
        QObject::connect(tab, &DeviceConnectionTab::deviceConnectRequestedV2, tab,
                         [ctx](int devKind, int devIndex, int channel,
                               int arbBaud, int dataBaud, bool canFd, int devSubType) {
            auto kind = static_cast<CanDeviceManager::DeviceKind>(devKind);
            ctx.deviceManager->configure(kind, devIndex, channel,
                                         arbBaud, dataBaud, canFd, devSubType);
            ctx.deviceManager->start();
        });

        // 连接成功 — 仅连接设备，不启动数据流
        QObject::connect(tab, &DeviceConnectionTab::deviceConnectRequested, tab,
                         [this, ctx](const QString &name, int) {
            if (ctx.deviceManager->isRealDevice() && !ctx.deviceManager->isRunning()) {
                ctx.appendOutput(QStringLiteral("设备连接失败: %1").arg(name));
                return;
            }
            if (!ctx.deviceManager->isRealDevice()) {
                if (ctx.shellInvoke)
                    ctx.shellInvoke(QStringLiteral("connMessage"),
                                    QStringLiteral("已连接"));
            }
            // 不自动启动数据流 — 需在 Flow 页面点击"开始"后才向 Trace/Graphic 分发数据
            ctx.appendOutput(QStringLiteral("设备已连接: %1 (请在 Flow 页面点击开始启动数据流)")
                                 .arg(name));
        });

        // 断开 — 停止数据源 + 壳侧状态/实例门控
        QObject::connect(tab, &DeviceConnectionTab::deviceDisconnectRequested, tab,
                         [this, ctx]() {
            ctx.simulator->stop();
            ctx.deviceManager->stop();
            if (ctx.shellInvoke)
                ctx.shellInvoke(QStringLiteral("deviceDisconnected"), {});
        });

        cachePage(QStringLiteral("device"), tab);
    }

    // 应用设备参数（onOpenDeviceTab 携带 DevicePanel 选中设备）
    const QVariantList l = param.toList();
    if (l.size() == 4)
        tab->setDevice(l.at(0).toInt(), l.at(1).toInt(),
                       l.at(2).toString(), l.at(3).toInt());
    return tab;
}

void FlowModule::invoke(const QString &action, const QVariant &arg)
{
    if (action == QStringLiteral("setDevice")) {
        const QVariantList l = arg.toList();
        if (auto *tab = qobject_cast<DeviceConnectionTab *>(
                m_pages.value(QStringLiteral("device")))) {
            if (l.size() == 4)
                tab->setDevice(l.at(0).toInt(), l.at(1).toInt(),
                               l.at(2).toString(), l.at(3).toInt());
        }
    } else if (action == QStringLiteral("onFrames")) {
        const QVector<CanFrame> frames = arg.value<QVector<CanFrame>>();
        if (frames.isEmpty())
            return;
        for (auto it = m_pages.constBegin(); it != m_pages.constEnd(); ++it) {
            if (auto *msv = qobject_cast<MeasurementSetupView *>(it.value()))
                msv->onFrame(frames.last());
        }
    } else if (action == QStringLiteral("onFrame")) {
        const CanFrame frame = arg.value<CanFrame>();
        for (auto it = m_pages.constBegin(); it != m_pages.constEnd(); ++it) {
            if (auto *msv = qobject_cast<MeasurementSetupView *>(it.value()))
                msv->onFrame(frame);
        }
    } else if (action == QStringLiteral("addModuleInstance")) {
        const QVariantList l = arg.toList();
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup")))) {
            if (l.size() == 3)
                msv->addModuleInstance(l.at(0).toString(), l.at(1).toString(),
                                       l.at(2).toString());
        }
    } else if (action == QStringLiteral("removeModuleInstance")) {
        const QVariantList l = arg.toList();
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup")))) {
            if (l.size() == 2)
                msv->removeModuleInstance(l.at(0).toString(), l.at(1).toString());
        }
    } else if (action == QStringLiteral("clearTraceGraphicInstances")) {
        // 工程状态恢复前清空旧实例块（B4：applyProjectState → 模块）
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            msv->clearTraceGraphicInstances();
    } else if (action == QStringLiteral("rebuildScene")) {
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            msv->rebuildScene();
    } else if (action == QStringLiteral("setSource")) {
        // 工程状态恢复（B4：applyProjectState → 模块）
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            msv->setSource(static_cast<MeasurementSetupView::Source>(arg.toInt()));
    } else if (action == QStringLiteral("setFilePath")) {
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            msv->setFilePath(arg.toString());
    } else if (action == QStringLiteral("setMeasurementRunning")) {
        // 离线回放结束/取消：复位 Flow 页启停按钮（壳 onPlayerFinished /
        // onMeasurementToggled 早退路径回调，「开始」恢复可点）
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            msv->setRunning(arg.toBool());
    } else if (action == QStringLiteral("setBlockError")) {
        // 块运行异常标记（壳侧设备错误/掉线转发：true=红闪，false=恢复熄灭）
        // Arg: QVariantList{ blockId, on }
        const QVariantList l = arg.toList();
        if (l.size() >= 2) {
            if (auto *msv = qobject_cast<MeasurementSetupView *>(
                    m_pages.value(QStringLiteral("setup"))))
                msv->setBlockError(l.at(0).toString(), l.at(1).toBool());
        }
    } else if (action == QStringLiteral("setDeviceConfig")) {
        // Restore device connection UI (does not connect)
        const QVariantMap cfg = arg.toMap();
        if (auto *tab = qobject_cast<DeviceConnectionTab *>(
                m_pages.value(QStringLiteral("device")))) {
            if (cfg.contains(QStringLiteral("deviceKind"))) {
                tab->setDevice(
                    cfg.value(QStringLiteral("deviceKind")).toInt(),
                    cfg.value(QStringLiteral("deviceIndex")).toInt(),
                    cfg.value(QStringLiteral("deviceName")).toString(),
                    cfg.value(QStringLiteral("deviceSubType")).toInt());
            }
            if (cfg.contains(QStringLiteral("baudrate")))
                tab->setBaudrate(cfg.value(QStringLiteral("baudrate")).toInt());
            if (cfg.contains(QStringLiteral("channel")))
                tab->setChannel(cfg.value(QStringLiteral("channel")).toInt());
            if (cfg.contains(QStringLiteral("canFd")))
                tab->setCanFd(cfg.value(QStringLiteral("canFd")).toBool());
            if (cfg.value(QStringLiteral("fdBaudrate")).toInt() > 0)
                tab->setDataBaudrate(cfg.value(QStringLiteral("fdBaudrate")).toInt());
        }
    } else if (action == QStringLiteral("setBlockEnabledMap")) {
        const QVariantMap en = arg.toMap();
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup")))) {
            for (auto it = en.constBegin(); it != en.constEnd(); ++it)
                msv->setBlockEnabled(it.key(), it.value().toBool());
        }
    } else if (action == QStringLiteral("setFilterRules")) {
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            msv->setFilterRules(arg.toStringList());
    }
}

QVariant FlowModule::query(const QString &what, const QVariant &arg)
{
    if (what == QStringLiteral("currentSource")) {
        // 返回 "hardware"/"file"（壳侧不再可见 MeasurementSetupView::Source 枚举）
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            return msv->currentSource() == MeasurementSetupView::Source::Hardware
                       ? QStringLiteral("hardware") : QStringLiteral("file");
        return {};
    }
    if (what == QStringLiteral("isBlockEnabled")) {
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            return msv->isBlockEnabled(arg.toString());
        return {};
    }
    if (what == QStringLiteral("devicePage"))
        return QVariant::fromValue<QWidget *>(m_pages.value(QStringLiteral("device")));
    if (what == QStringLiteral("projectState")) {
        // 工程状态采集（B4：captureProjectState → 模块）
        QVariantMap st;
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup")))) {
            st.insert(QStringLiteral("sourceMode"),
                      static_cast<int>(msv->currentSource()));
            st.insert(QStringLiteral("filePath"), msv->filePath());
        }
        return st;
    }
    if (what == QStringLiteral("deviceConfig")) {
        QVariantMap cfg;
        if (auto *tab = qobject_cast<DeviceConnectionTab *>(
                m_pages.value(QStringLiteral("device")))) {
            cfg.insert(QStringLiteral("canFd"), tab->isCanFd());
            cfg.insert(QStringLiteral("fdBaudrate"), tab->dataBaudrate());
            cfg.insert(QStringLiteral("deviceKind"), tab->deviceKind());
            cfg.insert(QStringLiteral("deviceIndex"), tab->deviceIndex());
            cfg.insert(QStringLiteral("deviceSubType"), tab->deviceSubType());
            cfg.insert(QStringLiteral("deviceName"), tab->deviceName());
            cfg.insert(QStringLiteral("baudrate"), tab->baudrate());
            cfg.insert(QStringLiteral("channel"), tab->channel());
        }
        return cfg;
    }
    if (what == QStringLiteral("blockEnabledMap")) {
        QVariantMap en;
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup")))) {
            const auto map = msv->blockEnabledMap();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it)
                en.insert(it.key(), it.value());
        }
        return en;
    }
    if (what == QStringLiteral("filterRules")) {
        if (auto *msv = qobject_cast<MeasurementSetupView *>(
                m_pages.value(QStringLiteral("setup"))))
            return msv->filterRules();
        return QStringList();
    }
    return {};
}

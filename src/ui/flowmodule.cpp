#include "flowmodule.h"

#include "measurementsetupview.h"
#include "deviceconnectiontab.h"
#include "core/canframe.h"
#include "core/dbcmanager.h"
#include "core/candevicemanager.h"
#include "core/player.h"
#include "core/cansimulator.h"

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
    QObject::connect(ctx.dbcManager, &DbcManager::dbcLoaded, view,
                     [view, ctx](const QString &) {
        QStringList files;
        for (const auto &f : ctx.dbcManager->files())
            files << f.fileName;
        view->setDbcFiles(files);
    });
    QObject::connect(ctx.dbcManager, &DbcManager::dbcUnloaded, view,
                     [view, ctx](const QString &) {
        QStringList files;
        for (const auto &f : ctx.dbcManager->files())
            files << f.fileName;
        view->setDbcFiles(files);
    });

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
    QObject::connect(view, &MeasurementSetupView::fileBlockClicked, view, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("openOfflineAnalysis"), {});
    });
    QObject::connect(view, &MeasurementSetupView::realBlockClicked, view, [this]() {
        if (m_ctx.shellInvoke) m_ctx.shellInvoke(QStringLiteral("openDevicePage"), {});
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

    // DBC 选择：模块侧完成（对话框 parent = 壳窗口，加载走数据层）
    QObject::connect(view, &MeasurementSetupView::dbcSelectRequested, view, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            m_ctx.mainWindow, QStringLiteral("导入 DBC 文件"), {},
            QStringLiteral("DBC 文件 (*.dbc);;所有文件 (*.*)"));
        if (path.isEmpty())
            return;
        if (m_ctx.dbcManager->loadDbc(path))
            m_ctx.appendOutput(QStringLiteral("已加载 DBC: ")
                               + QFileInfo(path).fileName());
        else
            m_ctx.addProblem(1, QStringLiteral("DBC"),
                             QStringLiteral("加载失败: ") + path);
    });

    // 通道过滤提示
    QObject::connect(view, &MeasurementSetupView::channelFilterRequested, view,
                     [this](const QString &channelId) {
        m_ctx.appendOutput(
            QStringLiteral("通道 %1 过滤条件已配置").arg(channelId));
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
    } else if (action == QStringLiteral("onFrame")) {
        // 帧分发（壳 onFrameReceived → 模块页内部消化）
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
    } else if (action == QStringLiteral("setDeviceConfig")) {
        // 恢复设备连接页界面配置（不连接设备，仅恢复参数）
        const QVariantMap cfg = arg.toMap();
        if (auto *tab = qobject_cast<DeviceConnectionTab *>(
                m_pages.value(QStringLiteral("device")))) {
            if (cfg.contains(QStringLiteral("baudrate")))
                tab->setBaudrate(cfg.value(QStringLiteral("baudrate")).toInt());
            if (cfg.contains(QStringLiteral("channel")))
                tab->setChannel(cfg.value(QStringLiteral("channel")).toInt());
            if (cfg.contains(QStringLiteral("canFd")))
                tab->setCanFd(cfg.value(QStringLiteral("canFd")).toBool());
            if (cfg.value(QStringLiteral("fdBaudrate")).toInt() > 0)
                tab->setDataBaudrate(cfg.value(QStringLiteral("fdBaudrate")).toInt());
        }
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
            cfg.insert(QStringLiteral("baudrate"), tab->baudrate());
            cfg.insert(QStringLiteral("channel"), tab->channel());
        }
        return cfg;
    }
    return {};
}

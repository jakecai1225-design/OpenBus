#include "graphicmodule.h"
#include "graphicview.h"
#include "datawindow.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"
#include "core/player.h"
#include "core/samplestore.h"
#include "core/capturelog.h"
#include "core/signalrelay.h"

#include <QFileIconProvider>
#include <QFileDialog>
#include <QMessageBox>

// openbus_graphic.dll 唯一导出的 C 工厂函数（拆分方案 §4.2）
extern "C" IBusinessModule *openbus_createGraphicModule()
{
    return new GraphicModule;
}

// ============================================================
//  Constructor / Destructor
// ============================================================

GraphicModule::GraphicModule()
{
    // m_dataWindow created lazily when requested
}

QIcon GraphicModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/graphic.svg")); // Reuse existing icon if exists
}

// ============================================================
//  Interface Implementation
// ============================================================

QWidget *GraphicModule::createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
{
    if (pageId == QStringLiteral("graphic")) {
        auto *gv = new GraphicView(ctx.mainWindow);

        // P1 G14: 注入 DBC 管理器引用（用于添加信号对话框）
        gv->setDbcManager(ctx.dbcManager);

        // Store context for later use
        if (!m_ctx.mainWindow) {
            m_ctx.mainWindow = ctx.mainWindow;
            m_ctx.player = ctx.player;
            m_ctx.shellInvoke = ctx.shellInvoke;
            m_ctx.appendOutput = ctx.appendOutput;
            m_ctx.addProblem = ctx.addProblem;
        }

        // Register instance（param 为壳/flow 页传入的 instanceId）
        QString id = param.userType() == QMetaType::QString
                         ? param.toString() : QString();
        if (!id.isEmpty()) {
            m_instances.insert(id, gv);
        }
        m_viewList.append(gv);

        // Forward Graphic status line to shell status bar (visible views only).
        if (ctx.shellInvoke) {
            auto *relay = new SignalRelay(gv);
            auto shellInvoke = ctx.shellInvoke;
            relay->fnString = [gv, shellInvoke](const QString &text) {
                if (gv->isVisible())
                    shellInvoke(QStringLiteral("statusMessage"), text);
            };
            QObject::connect(gv, SIGNAL(statusInfoChanged(QString)),
                             relay, SLOT(fireQString(QString)));
        }

        // Cursor linkage: bidirectional sync with all existing views
        // (UniqueConnection dedupes; first view has no peer)
        for (const auto &otherPtr : qAsConst(m_viewList)) {
            GraphicView *other = otherPtr.data();
            if (!other || other == gv)
                continue;
            QObject::connect(gv, &GraphicView::cursorMoved, other, &GraphicView::onSyncCursor,
                    Qt::UniqueConnection);
            QObject::connect(other, &GraphicView::cursorMoved, gv, &GraphicView::onSyncCursor,
                    Qt::UniqueConnection);
        }

        QObject::connect(gv, &QObject::destroyed, gv, [this, id, gv]() {
            // strongref 归零先于 destroyed 发射——QPointer::data() 已为空，
            // 按捕获的 id 移除（指针比较永远失配）；m_viewList 里只有本条目
            // 的 data() 为空，removeAll 依"空指针等值"恰好只移除本条目
            m_instances.remove(id);
            m_viewList.removeAll(gv);
        });

        return gv;

    } else if (pageId == QStringLiteral("datawindow")) {
        // Data Window is a cached single-instance auxiliary page
        // （标签页关闭即 widget 销毁 → 置空缓存，下次调用重建）
        if (!m_dataWindow) {
            m_dataWindow = new DataWindow(ctx.mainWindow);
            m_dataWindow->setDbcManager(ctx.dbcManager);
            // 模块非 QObject：以 DataWindow 自身为接收上下文
            QObject::connect(m_dataWindow, &QObject::destroyed, m_dataWindow, [this]() {
                m_dataWindow = nullptr;
            });
        }
        return m_dataWindow;
    }

    return nullptr;
}

void GraphicModule::invoke(const QString &action, const QVariant &arg)
{
    if (action == QStringLiteral("onFrames")) {
        // Phase B3: GraphicView pulls SampleStore; only DataWindow still needs frames
        const QVector<CanFrame> frames = arg.value<QVector<CanFrame>>();
        if (frames.isEmpty())
            return;
        if (m_dataWindow && m_dataWindow->isVisible()) {
            for (const auto &frame : frames)
                m_dataWindow->onFrame(frame);
        }
    } else if (action == QStringLiteral("onFrame")) {
        const CanFrame frame = arg.value<CanFrame>();
        if (m_dataWindow && m_dataWindow->isVisible())
            m_dataWindow->onFrame(frame);
    } else if (action == QStringLiteral("setFlowEnabled")) {
        // Set flow gate on specific instance
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                auto w = l[0].value<QWidget *>();
                bool enabled = l[1].toBool();
                if (auto *gv = qobject_cast<GraphicView *>(w)) {
                    gv->setProperty("flowEnabled", enabled);
                }
            }
        }
    } else if (action == QStringLiteral("clearDataAll")) {
        SampleStore::instance()->clear();
        const auto views = m_viewList;
        for (const auto &gvPtr : views) {
            GraphicView *gv = gvPtr.data();
            if (gv)
                gv->clearData();
        }
    } else if (action == QStringLiteral("addSignal")) {
        // Add single signal to target view (from shell forwarding onFrameDoubleClicked/onSignalDoubleClicked)
        // Arg: variant map with "target"(QWidget*) + "sig"(QVariantMap) or standalone sig + resolve target
        if (arg.canConvert(QVariant::Map)) {
            auto sigMap = arg.toMap();
            auto w = sigMap.value("target").value<QWidget *>();
            auto sigData = sigMap.value("sig");

            if (w && sigData.canConvert(QVariant::Map)) {
                // Direct signal add
                GraphicView::Signal gsig;
                auto smap = sigData.toMap();
                gsig.name = smap.value("name").toString();
                gsig.canId = smap.value("canId").toUInt();
                gsig.extended = smap.value("extended").toBool();
                gsig.color = smap.value("color", QColor()).value<QColor>();
                if (smap.contains("dbcSig"))
                    gsig.dbcSig = dbcSignalFromMap(smap.value("dbcSig").toMap());
                // Other fields: displayMode, yAxisMode etc. optional defaults

                if (auto *gv = qobject_cast<GraphicView *>(w)) {
                    int histCount = -1;
                    const auto *hist = replayHistory(&histCount);
                    gv->addSignal(gsig, hist, histCount);
                }
            } else if (!w && sigData.canConvert(QVariant::Map)) {
                // No target: shell should have resolved target; skip
                qDebug() << "addSignal: no target widget provided";
            }
        } else if (arg.canConvert(QVariant::List)) {
            // Old-style: QVariantList{QVariant::fromValue<QWidget*>(target), QVariantMap{...}}
            auto l = arg.toList();
            if (l.size() >= 2) {
                auto w = l[0].value<QWidget *>();
                if (auto *gv = qobject_cast<GraphicView *>(w)) {
                    auto smap = l[1].toMap();
                    GraphicView::Signal gsig;
                    gsig.name = smap.value("name").toString();
                    gsig.canId = smap.value("canId").toUInt();
                    gsig.extended = smap.value("extended").toBool();
                    gsig.color = smap.value("color", QColor()).value<QColor>();
                    if (smap.contains("dbcSig"))
                        gsig.dbcSig = dbcSignalFromMap(smap.value("dbcSig").toMap());
                    int histCount = -1;
                    const auto *hist = replayHistory(&histCount);
                    gv->addSignal(gsig, hist, histCount);
                }
            }
        }
    } else if (action == QStringLiteral("addSignals")) {
        // Add multiple signals (message signals from frameAddToGraphic handling)
        // Arg: QVariantList{QWidget* target, QVariantList<sigs>} where sigs are maps
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                auto w = l[0].value<QWidget *>();
                if (auto *gv = qobject_cast<GraphicView *>(w)) {
                    auto sigMaps = l[1].toList();
                    int histCount = -1;
                    const auto *hist = replayHistory(&histCount);
                    for (const auto &sigVar : sigMaps) {
                        if (sigVar.canConvert(QVariant::Map)) {
                            auto smap = sigVar.toMap();
                            GraphicView::Signal gsig;
                            gsig.name = smap.value("name").toString();
                            gsig.canId = smap.value("canId").toUInt();
                            gsig.extended = smap.value("extended").toBool();
                            gsig.color = smap.value("color", QColor()).value<QColor>();
                            if (smap.contains("dbcSig"))
                                gsig.dbcSig = dbcSignalFromMap(smap.value("dbcSig").toMap());
                            gv->addSignal(gsig, hist, histCount);
                        }
                    }
                }
            }
        }
    } else if (action == QStringLiteral("loadSignalConfigs")) {
        // Load batch config: QVariantList{QWidget*, QVariantList<sigMap>}
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                auto w = l[0].value<QWidget *>();
                auto sigListVar = l[1];
                if (auto *gv = qobject_cast<GraphicView *>(w)) {
                    QVector<GraphicView::Signal> sigs;
                    if (sigListVar.canConvert(QVariant::List)) {
                        for (const auto &v : sigListVar.toList()) {
                            if (v.canConvert(QVariant::Map)) {
                                auto smap = v.toMap();
                                GraphicView::Signal gs;
                                gs.name = smap.value("name").toString();
                                gs.canId = smap.value("canId").toUInt();
                                gs.extended = smap.value("extended").toBool();
                                gs.displayMode = smap.value("displayMode", 1).toInt();
                                if (smap.contains(QStringLiteral("color"))) {
                                    const QColor c(smap.value(QStringLiteral("color")).toString());
                                    if (c.isValid())
                                        gs.color = c;
                                }
                                if (smap.contains("dbcSig"))
                                    gs.dbcSig = dbcSignalFromMap(smap.value("dbcSig").toMap());
                                sigs.append(gs);
                            }
                        }
                    }
                    gv->loadSignalConfigs(sigs);
                }
            }
        }
    } else if (action == QStringLiteral("removeSelectedSignals")) {
        if (auto *gv = qobject_cast<GraphicView *>(arg.value<QWidget *>()))
            gv->removeSelectedSignals();
    }
}

QVariant GraphicModule::query(const QString &what, const QVariant &arg)
{
    if (what == QStringLiteral("isGraphic")) {
        auto w = arg.value<QWidget *>();
        return QVariant(static_cast<bool>(qobject_cast<GraphicView *>(w)));
    } else if (what == QStringLiteral("instance")) {
        return QVariant::fromValue<QWidget*>(m_instances.value(arg.toString()));
    } else if (what == QStringLiteral("lastInstance")) {
        // Return most recently created live GraphicView (for target resolution)
        // （Qt6 QMap 无 rbegin/rend，且 QMap 按键排序非创建序——倒序遍历
        //  创建序列表 m_viewList 才是"最近创建"语义）
        for (int i = m_viewList.size() - 1; i >= 0; --i) {
            if (GraphicView *gv = m_viewList.at(i).data())
                return QVariant::fromValue<QWidget*>(gv);
        }
    } else if (what == QStringLiteral("signalConfigs")) {
        // Get signal configs from a specific GraphicView (for captureProjectState)
        // Arg: QWidget* view
        auto gv = qobject_cast<GraphicView*>(arg.value<QWidget*>());
        if (gv) {
            QVariantList sigList;
            for (const auto &sig : gv->signalConfigs()) {
                QVariantMap m;
                m["name"] = sig.name;
                m["canId"] = sig.canId;
                m["extended"] = sig.extended;
                m["displayMode"] = sig.displayMode;
                m["dbcSig"] = dbcSignalToMap(sig.dbcSig);
                m["color"] = sig.color.name();
                sigList.append(m);
            }
            return QVariant(sigList);
        }
    } else if (what == QStringLiteral("removeSelectedSignals")) {
        if (auto *gv = qobject_cast<GraphicView *>(arg.value<QWidget *>()))
            return gv->removeSelectedSignals();
    } else if (what == QStringLiteral("dataWindow")) {
        // Return cached DataWindow widget (QWidget*)
        return QVariant::fromValue<QWidget*>(m_dataWindow);
    } else if (what == QStringLiteral("activeInstance")) {
        // Return first live instance
        if (!m_viewList.isEmpty())
            return QVariant::fromValue<QWidget*>(m_viewList.first().data());
    }

    return {};
}

const QVector<CanFrame> *GraphicModule::replayHistory(int *count) const
{
    if (count)
        *count = -1;
    // Prefer Player (file timestamps, never PC-now). Prefix = already played.
    if (m_ctx.player && m_ctx.player->isLoaded()) {
        if (count)
            *count = m_ctx.player->currentFrameIndex();
        return &m_ctx.player->frames();
    }
    // Measurement path: CaptureLog is the same source Trace cameras read —
    // backfill Graphic with identical timestamps.
    CaptureLog *log = CaptureLog::instance();
    const int n = log->size();
    if (n <= 0)
        return nullptr;
    m_captureHistoryCache.clear();
    log->copyRange(0, n, &m_captureHistoryCache);
    if (m_captureHistoryCache.isEmpty())
        return nullptr;
    if (count)
        *count = m_captureHistoryCache.size();
    return &m_captureHistoryCache;
}

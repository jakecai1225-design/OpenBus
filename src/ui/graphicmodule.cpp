#include "graphicmodule.h"
#include "graphicview.h"
#include "datawindow.h"
#include "core/dbcmanager.h"
#include "core/dbcdata.h"

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

QVariantMap GraphicModule::dbcSignalToVariantMap(const DbcSignal &sig)
{
    QVariantMap map;
    map["name"] = sig.name;
    map["startBit"] = sig.startBit;
    map["bitLength"] = sig.bitLength;
    map["littleEndian"] = sig.littleEndian;
    map["isSigned"] = sig.isSigned;
    map["factor"] = sig.factor;
    map["offset"] = sig.offset;
    map["minimum"] = sig.minimum;
    map["maximum"] = sig.maximum;
    map["unit"] = sig.unit;
    map["receiver"] = sig.receiver;
    // Optional fields: muxType, valueTable etc. omitted for brevity (not used by GraphicView currently)
    return map;
}

DbcSignal GraphicModule::variantMapToDbcSignal(const QVariantMap &map)
{
    DbcSignal sig;
    sig.name = map.value("name").toString();
    sig.startBit = map.value("startBit").toInt();
    sig.bitLength = map.value("bitLength").toInt();
    sig.littleEndian = map.value("littleEndian").toBool();
    sig.isSigned = map.value("isSigned").toBool();
    sig.factor = map.value("factor").toDouble();
    sig.offset = map.value("offset").toDouble();
    sig.minimum = map.value("minimum").toDouble();
    sig.maximum = map.value("maximum").toDouble();
    sig.unit = map.value("unit").toString();
    sig.receiver = map.value("receiver").toString();
    return sig;
}

QVariantMap GraphicModule::buildSignalMap(qint32 canId, bool extended, const QString &name,
                                           const QVariantMap &dbcSigOverride)
{
    QVariantMap sig;
    sig["name"] = name;
    sig["canId"] = static_cast<uint>(canId);
    sig["extended"] = extended;
    if (dbcSigOverride.isEmpty()) {
        // Default values when no DBC signal provided (e.g., frame double-click without DBC)
        sig["dbcSig"] = dbcSignalToVariantMap(DbcSignal()); // default-constructed DbcSignal
    } else {
        sig["dbcSig"] = dbcSigOverride;
    }
    return sig;
}

// ============================================================
//  Interface Implementation
// ============================================================

QWidget *GraphicModule::createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
{
    if (pageId == QStringLiteral("graphic")) {
        auto *gv = new GraphicView(ctx.mainWindow);

        // Store context for later use
        if (!m_ctx.mainWindow) {
            m_ctx.mainWindow = ctx.mainWindow;
            m_ctx.shellInvoke = ctx.shellInvoke;
            m_ctx.appendOutput = ctx.appendOutput;
            m_ctx.addProblem = ctx.addProblem;
        }

        // Register instance
        QString id = param.canConvert(QString::metaType()) ? param.toString() : QString();
        if (!id.isEmpty()) {
            m_instances.insert(id, gv);
        }
        m_viewList.append(gv);

        // Cursor linkage: connect to ALL already-created views (bidirectional sync)
        for (auto *other : qAsConst(m_viewList)) {
            if (other != gv && !m_cursorLinkedViews.contains(other)) {
                connect(gv, &GraphicView::cursorMoved, other, &GraphicView::onSyncCursor, Qt::UniqueConnection);
                connect(other, &GraphicView::cursorMoved, gv, &GraphicView::onSyncCursor, Qt::UniqueConnection);
                m_cursorLinkedViews.append(other);
            }
        }
        m_cursorLinkedViews.append(gv);

        QObject::connect(gv, &QObject::destroyed, this, [this, gv]() {
            // Remove from cursor linked list
            int idx = m_cursorLinkedViews.indexOf(gv);
            if (idx >= 0)
                m_cursorLinkedViews.removeAt(idx);
            // Remove from maps (using pointer value)
            QString idToRemove;
            for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
                if (it.value().data() == gv) {
                    idToRemove = it.key();
                    break;
                }
            }
            if (!idToRemove.isEmpty())
                m_instances.remove(idToRemove);
            m_viewList.removeAll(gv);
        });

        return gv;

    } else if (pageId == QStringLiteral("datawindow")) {
        // Data Window is a cached single-instance auxiliary page
        if (!m_dataWindow) {
            m_dataWindow = new DataWindow(ctx.mainWindow);
            m_dataWindow->setDbcManager(ctx.dbcManager);
        }
        return m_dataWindow;
    }

    return nullptr;
}

void GraphicModule::invoke(const QString &action, const QVariant &arg)
{
    if (action == QStringLiteral("onFrame")) {
        // Dispatch frame to all live GraphicViews that have flowEnabled=true
        const CanFrame frame = arg.value<CanFrame>();
        for (auto *gv : qAsConst(m_viewList)) {
            if (!gv)
                continue;
            // Check flowEnabled property (default true if not set)
            bool flowEnabled = gv->property("flowEnabled").toBool();
            if (!gv->property("flowEnabled").isValid() || flowEnabled) {
                gv->onFrame(frame);
            }
        }
        // Also update DataWindow (cached in module)
        if (m_dataWindow)
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
        // Clear all GraphicViews data (does NOT clear DataWindow per original semantics)
        for (auto *gv : qAsConst(m_viewList)) {
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
                    gsig.dbcSig = variantMapToDbcSignal(smap.value("dbcSig").toMap());
                // Other fields: displayMode, yAxisMode etc. optional defaults

                if (auto *gv = qobject_cast<GraphicView *>(w))
                    gv->addSignal(gsig);
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
                        gsig.dbcSig = variantMapToDbcSignal(smap.value("dbcSig").toMap());
                    gv->addSignal(gsig);
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
                    for (const auto &sigVar : sigMaps) {
                        if (sigVar.canConvert(QVariant::Map)) {
                            auto smap = sigVar.toMap();
                            GraphicView::Signal gsig;
                            gsig.name = smap.value("name").toString();
                            gsig.canId = smap.value("canId").toUInt();
                            gsig.extended = smap.value("extended").toBool();
                            gsig.color = smap.value("color", QColor()).value<QColor>();
                            if (smap.contains("dbcSig"))
                                gsig.dbcSig = variantMapToDbcSignal(smap.value("dbcSig").toMap());
                            gv->addSignal(gsig);
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
                                if (smap.contains("dbcSig"))
                                    gs.dbcSig = variantMapToDbcSignal(smap.value("dbcSig").toMap());
                                sigs.append(gs);
                            }
                        }
                    }
                    gv->loadSignalConfigs(sigs);
                }
            }
        }
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
        for (auto it = m_instances.rbegin(); it != m_instances.rend(); ++it) {
            if (it.value())
                return QVariant::fromValue<QWidget*>(it.value().data());
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
                m["dbcSig"] = dbcSignalToVariantMap(sig.dbcSig);
                m["color"] = sig.color.name();
                sigList.append(m);
            }
            return QVariant(sigList);
        }
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

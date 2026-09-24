#include "tracemodule.h"
#include "traceview.h"
#include "filterbar.h"
#include "colorruleeditor.h"
#include "models/cantracemodel.h"
#include "models/cantraceproxymodel.h"
#include "models/viewportproxy.h"
#include "core/dbcmanager.h"
#include "core/filterpresetmanager.h"
#include "core/canframe.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDebug>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainterPath>

// openbus_trace.dll 唯一导出的 C 工厂函数（拆分方案 §4.2）
extern "C" IBusinessModule *openbus_createTraceModule()
{
    return new TraceModule;
}

// ============================================================
//  Constructor / Destructor
// ============================================================

TraceModule::TraceModule()
{
    m_filterPresets = new FilterPresetManager(nullptr); // module-owned instance for all tabs
}

QIcon TraceModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/list.svg")); // reuse generic list icon if exists; else :/icons/database.svg
}

// ============================================================
//  Interface Implementation
// ============================================================

QWidget *TraceModule::createWidget(ShellContext &ctx)
{
    Q_UNUSED(ctx);
    return createPage("trace", {}, ctx);
}

QWidget *TraceModule::createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
{
    if (pageId != QStringLiteral("trace"))
        return nullptr;

    auto *tab = new TraceTab(ctx.mainWindow);
    tab->setDbcManager(ctx.dbcManager);

    // --- Wiring setup (equivalent to MainWindow::setupTraceTab) ---

    auto *filterBar = tab->filterBar();
    filterBar->setPresetManager(m_filterPresets); // shared preset manager per instance

    // Store context from first createPage (for later dialog invocations)
    if (!m_ctx.mainWindow) {
        m_ctx.mainWindow = ctx.mainWindow;
        m_ctx.shellInvoke = ctx.shellInvoke;
        m_ctx.appendOutput = ctx.appendOutput;
        m_ctx.addProblem = ctx.addProblem;
    }

    // filterApplied → set filter + error reporting。模块非 QObject，一律以页面
    // widget 为接收上下文（连接随页面销毁自动断开；模块 this 仅作裸捕获——
    // 模块生命周期长于所有页面，与 flowmodule 约定一致）；ctx 为引用参数，
    // 值捕获拷贝的是 ShellContext 本体而非引用，闭包内可安全使用
    QObject::connect(filterBar, &FilterBar::filterApplied, tab, [this, tab](const QString &filter) {
        if (!tab->setFilterExpression(filter))
            m_ctx.addProblem(0, QStringLiteral("Filter"), "语法错误：" + filter);
        // Also sync the line edit text
        if (auto *edit = tab->filterBar()->findChild<QLineEdit *>())
            edit->setText(filter);
    });

    QObject::connect(filterBar, &FilterBar::filterCleared, tab, [tab]() {
        tab->clearFilter();
    });

    auto *traceView = tab->traceView();
    QObject::connect(traceView, &TraceView::frameDoubleClicked, tab,
            [ctx](const CanFrame &frame) {
                ctx.shellInvoke("frameDoubleClicked", QVariant::fromValue(frame));
            });
    QObject::connect(traceView, &TraceView::frameSelected, tab, [ctx, tab](const CanFrame &frame) {
        bool hasSelection = static_cast<bool>(tab->traceView()->selectedFrame());
        int rows = hasSelection ? 1 : 0;
        ctx.shellInvoke(QStringLiteral("traceSelectionChanged"), rows);
        if (hasSelection)
            ctx.shellInvoke(QStringLiteral("inspectFrame"), QVariant::fromValue(frame));
        else
            ctx.shellInvoke(QStringLiteral("clearInspection"), {});
    });

    // Add to Graphic: send raw frame to shell (shell does DBC lookup + adds all signals)
    QObject::connect(traceView, &TraceView::frameAddToGraphic, tab, [ctx](const CanFrame &frame) {
        ctx.shellInvoke("frameAddToGraphic", QVariant::fromValue(frame));
    });

    // Add to AI Chat: selected frames → PluginManager.attachFramesToAi
    QObject::connect(traceView, &TraceView::framesAddToAi, tab,
                     [ctx](const QVector<CanFrame> &frames) {
        ctx.shellInvoke(QStringLiteral("framesAddToAi"), QVariant::fromValue(frames));
    });

    // Signal list: add one selected signal to Graphic (same shell path as DBC detail)
    QObject::connect(tab->signalDecode(), &SignalDecodeWidget::signalAddToGraphic, tab,
                     [ctx](quint32 canId, const QString &signalName) {
        ctx.shellInvoke(QStringLiteral("signalDoubleClicked"),
                        QVariantList{ canId, signalName });
    });

    // Clear filter requested
    QObject::connect(traceView, &TraceView::clearFilterRequested, tab, [tab]() {
        tab->clearAllFilters();
    });

    // File loaded callback
    QObject::connect(tab, &TraceTab::fileLoaded, tab, [ctx](int count) {
        ctx.shellInvoke("traceFileLoaded", count);
    });

    // Trace status → shell status bar (no in-tab status strip)
    QObject::connect(tab, &TraceTab::statusInfoChanged, tab, [ctx](const QVariantMap &info) {
        ctx.shellInvoke(QStringLiteral("traceStatus"), info);
    });

    // Register instance by id（param 为壳/flow 页传入的 instanceId；
    // 空串键留给 createWidget 无参路径）
    const QString id = param.userType() == QMetaType::QString
                           ? param.toString() : QString();
    m_instances.insert(id, tab);
    m_tabList.append(tab);
    tab->setProperty("__traceInstanceId", id); // mark widget with id for debugging aid

    QObject::connect(tab, &QObject::destroyed, tab, [this, id, tab]() {
        // strongref 归零先于 destroyed 发射——此刻 QPointer::data() 已为空，
        // 按指针比较永远失配，必须按捕获的 id 移除；m_tabList 里只有本条目
        // 的 data() 为空，removeAll 依"空指针等值"恰好只移除本条目
        m_instances.remove(id);
        m_tabList.removeAll(tab);
    });

    return tab;
}

void TraceModule::invoke(const QString &action, const QVariant &arg)
{
    // Frame reception: live path is CaptureLog camera sync (Phase B5).
    // Keep onFrames/onFrame for import / compat callers (local ring).
    if (action == QStringLiteral("onFrames")) {
        const QVector<CanFrame> frames = arg.value<QVector<CanFrame>>();
        if (frames.isEmpty())
            return;
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            TraceTab *tab = tabPtr.data();
            if (!tab || !tab->isRunning())
                continue;
            // Only local-ring tabs (overwrite / non-camera) accept shell fan-out
            if (tab->traceModel() && !tab->traceModel()->isCaptureLogCamera())
                tab->traceModel()->enqueueFrames(frames);
        }
    } else if (action == QStringLiteral("onFrame")) {
        const CanFrame frame = arg.value<CanFrame>();
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            TraceTab *tab = tabPtr.data();
            if (!tab || !tab->isRunning())
                continue;
            if (tab->traceModel() && !tab->traceModel()->isCaptureLogCamera())
                tab->appendFrame(frame);
        }
    } else if (action == QStringLiteral("resetCaptureCursor")) {
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            if (TraceTab *tab = tabPtr.data())
                tab->resetCaptureCursor();
        }
    } else if (action == QStringLiteral("setAutoScroll")) {
        m_autoScroll = arg.toBool();
        // Also sync each tab's internal autoscroll state
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            TraceTab *tab = tabPtr.data();
            if (tab) {
                auto *tv = tab->traceView();
                if (tv)
                    tv->setAutoScrollEnabled(m_autoScroll);
            }
        }
    } else if (action == QStringLiteral("setRunning")) {
        // set running state on a specific instance by id
        // Arg: QVariantList{id, running}
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                QString id = l[0].toString();
                bool running = l[1].toBool();
                if (!id.isEmpty()) {
                    auto tab = m_instances.value(id);
                    if (tab)
                        tab->setRunning(running);
                }
            }
        }
    } else if (action == QStringLiteral("setRunningAll")) {
        bool running = arg.toBool();
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            TraceTab *tab = tabPtr.data();
            if (tab)
                tab->setRunning(running);
        }
    } else if (action == QStringLiteral("clearTraceAll")) {
        // Just clear traces (not info panels)
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            TraceTab *tab = tabPtr.data();
            if (tab) {
                tab->resetCaptureCursor();
                tab->clearTrace();
            }
        }
    } else if (action == QStringLiteral("clearAll")) {
        // Full clear: trace + info panels
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            TraceTab *tab = tabPtr.data();
            if (tab) {
                tab->resetCaptureCursor();
                tab->clearTrace();
                tab->frameInfo()->clear();
                tab->signalDecode()->clear();
            }
        }
    } else if (action == QStringLiteral("appendFrames")) {
        // Batch import (onImportLog)：Arg = QVariantList{QWidget* target, QVariantList frames}
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                if (auto *tab = qobject_cast<TraceTab *>(l[0].value<QWidget *>())) {
                    QVector<CanFrame> frames;
                    const auto frameVars = l[1].toList();
                    frames.reserve(frameVars.size());
                    for (const auto &v : frameVars)
                        frames.append(v.value<CanFrame>());
                    tab->appendFrames(frames);
                    if (m_autoScroll)
                        tab->traceView()->scrollToBottom();
                }
            }
        }
    } else if (action == QStringLiteral("setFilterExpression") || action == QStringLiteral("applyFilter")) {
        // Set filter on target widget
        // Arg: QVariantList{QWidget* tabWidget, QString expr, bool reportProblem=false}
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                auto w = l[0].value<QWidget *>();
                QString expr = l[1].toString();
                bool report = l.size() > 2 ? l[2].toBool() : false;
                if (auto *tab = qobject_cast<TraceTab *>(w)) {
                    if (auto *edit = tab->filterBar()->findChild<QLineEdit *>())
                        edit->setText(expr);
                    bool ok = tab->setFilterExpression(expr);
                    if (report && !ok)
                        m_ctx.addProblem(0, QStringLiteral("Filter"),
                                         QStringLiteral("语法错误：") + expr);
                }
            }
        }
    } else if (action == QStringLiteral("jumpToFrame")) {
        // Bookmark jump: scroll to index + select
        // Arg: QVariantList{QWidget* tabWidget, int frameIndex}
        if (arg.canConvert(QVariant::List)) {
            auto l = arg.toList();
            if (l.size() >= 2) {
                auto w = l[0].value<QWidget *>();
                int idx = l[1].toInt();
                if (auto *tab = qobject_cast<TraceTab *>(w)) {
                    auto *tv = tab->traceView();
                    if (tv) {
                        QModelIndex index = tv->model()->index(idx, 0);
                        if (index.isValid()) {
                            tv->scrollTo(index, QAbstractItemView::PositionAtCenter);
                            tv->selectRow(idx);
                        }
                    }
                }
            }
        }
    } else if (action == QStringLiteral("editColorRules")) {
        // Open ColorRuleEditor dialog inside module, load rules from first/live trace tab, apply to all
        TraceTab *firstTab = m_tabList.isEmpty() ? nullptr : m_tabList.first().data();
        QVector<ColorRuleEditor::ColorRule> editorRules;
        if (firstTab && firstTab->traceModel()) {
            for (const auto &r : firstTab->traceModel()->colorRules()) {
                ColorRuleEditor::ColorRule cr;
                cr.expr = r.expr;
                cr.background = r.background;
                cr.foreground = r.foreground;
                cr.enabled = r.enabled;
                editorRules.append(cr);
            }
        }

        ColorRuleEditor dlg(m_ctx.mainWindow); // Use stored context
        dlg.setRules(editorRules);

        if (dlg.exec() == QDialog::Accepted) {
            auto rules = dlg.rules();
            // Apply to all instances
            const auto tabs = m_tabList;
            for (const auto &tabPtr : tabs) {
                TraceTab *tab = tabPtr.data();
                if (tab && tab->traceModel()) {
                    QVector<CanTraceModel::ColorRule> modelRules;
                    for (const auto &cr : rules) {
                        CanTraceModel::ColorRule mr;
                        mr.expr = cr.expr;
                        mr.background = cr.background;
                        mr.foreground = cr.foreground;
                        mr.enabled = cr.enabled;
                        modelRules.append(mr);
                    }
                    tab->traceModel()->setColorRules(modelRules);
                }
            }
        }
    } else if (action == QStringLiteral("setColorRules")) {
        // Project restore: QVariantList{QWidget* tab, QVariantList rules}
        if (arg.canConvert(QVariant::List)) {
            const auto l = arg.toList();
            if (l.size() >= 2) {
                auto *tab = qobject_cast<TraceTab *>(l[0].value<QWidget *>());
                if (tab && tab->traceModel()) {
                    QVector<CanTraceModel::ColorRule> modelRules;
                    for (const auto &rv : l[1].toList()) {
                        const QVariantMap m = rv.toMap();
                        CanTraceModel::ColorRule mr;
                        mr.expr = m.value(QStringLiteral("expr")).toString();
                        mr.background = QColor(m.value(QStringLiteral("background")).toString());
                        mr.foreground = QColor(m.value(QStringLiteral("foreground")).toString());
                        mr.enabled = m.value(QStringLiteral("enabled"), true).toBool();
                        modelRules.append(mr);
                    }
                    tab->traceModel()->setColorRules(modelRules);
                }
            }
        }
    } else if (action == QStringLiteral("retranslate")) {
        const auto tabs = m_tabList;
        for (const auto &tabPtr : tabs) {
            if (TraceTab *tab = tabPtr.data())
                tab->retranslateUi();
        }
    }
}

QVariant TraceModule::query(const QString &what, const QVariant &arg)
{
    if (what == QStringLiteral("isTrace")) {
        // Check if QWidget* is a TraceTab instance
        auto w = arg.value<QWidget *>();
        return QVariant(static_cast<bool>(qobject_cast<TraceTab *>(w)));
    } else if (what == QStringLiteral("instance")) {
        // Get TraceTab by id (return QWidget*)
        return QVariant::fromValue<QWidget*>(m_instances.value(arg.toString()));
    } else if (what == QStringLiteral("filterExpression")) {
        // Get current filter expression by id
        auto tab = m_instances.value(arg.toString());
        if (tab)
            return tab->filterExpression();
    } else if (what == QStringLiteral("frameCount")) {
        // Get frame count from a specific TraceTab instance
        auto tab = qobject_cast<TraceTab*>(arg.value<QWidget*>());
        if (tab)
            return tab->frameCount();
    } else if (what == QStringLiteral("activeInstance")) {
        // Return first live instance (for backward compatibility with m_traceTab usage)
        if (!m_tabList.isEmpty())
            return QVariant::fromValue<QWidget*>(m_tabList.first().data());
    } else if (what == QStringLiteral("selectedFrames")) {
        // 插件系统请求：提取指定 Trace 页选中帧（原 onPluginRequestSelectedFrames
        // 的 View → ViewportProxy → CanFilterProxy → CanTraceModel 代理链映射）
        auto *tab = qobject_cast<TraceTab*>(arg.value<QWidget*>());
        if (tab) {
            QVariantList out;
            auto *tv = tab->traceView();
            auto *traceModel = tab->traceModel();
            auto *filterProxy = tab->proxyModel();
            auto *viewportProxy = tab->viewportProxy();
            if (tv && traceModel) {
                auto *sel = tv->selectionModel();
                if (sel) {
                    for (const auto &idx : sel->selectedRows()) {
                        QModelIndex sourceIdx = idx;
                        if (viewportProxy)
                            sourceIdx = viewportProxy->mapToSource(sourceIdx);
                        if (filterProxy)
                            sourceIdx = filterProxy->mapToSource(sourceIdx);
                        int row = sourceIdx.row();
                        if (row >= 0 && row < traceModel->frameCount())
                            out.append(QVariant::fromValue(traceModel->frameAt(row)));
                    }
                }
            }
            return out;
        }
    } else if (what == QStringLiteral("recentFrames")) {
        // 插件系统请求（frames.getRecent）：arg = [TraceTab*, count]，
        // 返回最近 N 帧（时间正序，最新在后；不足 N 帧时返回现有全部）
        const QVariantList args = arg.toList();
        auto *tab = qobject_cast<TraceTab*>(args.value(0).value<QWidget*>());
        const int count = args.value(1, QVariant(100)).toInt();
        if (tab && tab->traceModel()) {
            QVariantList out;
            const int total = tab->traceModel()->frameCount();
            const int first = qMax(0, total - count);
            for (int row = first; row < total; ++row)
                out.append(QVariant::fromValue(tab->traceModel()->frameAt(row)));
            return out;
        }
    } else if (what == QStringLiteral("colorRules")) {
        // Arg: QWidget* tab OR instance id (QString)
        TraceTab *tab = qobject_cast<TraceTab *>(arg.value<QWidget *>());
        if (!tab)
            tab = m_instances.value(arg.toString());
        if (tab && tab->traceModel()) {
            QVariantList rules;
            for (const auto &r : tab->traceModel()->colorRules()) {
                QVariantMap m;
                m.insert(QStringLiteral("expr"), r.expr);
                m.insert(QStringLiteral("background"), r.background.name());
                m.insert(QStringLiteral("foreground"), r.foreground.name());
                m.insert(QStringLiteral("enabled"), r.enabled);
                rules.append(m);
            }
            return rules;
        }
    }
    return {};
}

#include "tracemodule.h"
#include "traceview.h"
#include "filterbar.h"
#include "colorruleeditor.h"
#include "core/dbcmanager.h"
#include "core/filterpresetmanager.h"

#include <QFileDialog>
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

    // filterApplied → set filter + error reporting via shellInvoke
    connect(filterBar, &FilterBar::filterApplied, this, [this, tab](const QString &filter) {
        if (!tab->setFilterExpression(filter))
            ctx.addProblem(0, QStringLiteral("Filter"), "语法错误：" + filter);
        // Also sync the line edit text
        if (auto *edit = filterBar->findChild<QLineEdit *>())
            edit->setText(filter);
    });

    connect(filterBar, &FilterBar::filterCleared, this, [tab]() {
        tab->clearFilter();
    });

    auto *traceView = tab->traceView();
    connect(traceView, &TraceView::frameDoubleClicked, this,
            [ctx](const CanFrame &frame) {
                ctx.shellInvoke("frameDoubleClicked", QVariant::fromValue(frame));
            });
    connect(traceView, &TraceView::frameSelected, this, [ctx, tab](const CanFrame &) {
        // Frame selected -> tell shell to update "选中 X 行" label
        bool hasSelection = static_cast<bool>(traceView->selectedFrame());
        int rows = hasSelection ? 1 : 0;
        ctx.shellInvoke("traceSelectionChanged", rows);
    });

    // Add to Graphic: send raw frame to shell (shell does DBC lookup + adds all signals)
    connect(traceView, &TraceView::frameAddToGraphic, this, [ctx](const CanFrame &frame) {
        ctx.shellInvoke("frameAddToGraphic", QVariant::fromValue(frame));
    });

    // Clear filter requested
    connect(traceView, &TraceView::clearFilterRequested, this, [tab]() {
        tab->clearAllFilters();
    });

    // File loaded callback
    connect(tab, &TraceTab::fileLoaded, this, [ctx](int count) {
        ctx.shellInvoke("traceFileLoaded", count);
    });

    // Register instance by id (param can be instanceId from flow page creation or generated number)
    if (param.isValid() && param.canConvert(QString::metaType())) {
        QString id = param.toString();
        m_instances.insert(id, tab);
        m_tabList.append(tab);
        tab->setProperty("__traceInstanceId", id); // mark widget with id for debugging/debugging aid
    } else {
        // Generate default "traceX" id (shell counts m_traceCount; module tracks by pointer)
        m_instances[""] = tab; // empty-key entry for single-instance scenarios
        m_tabList.append(tab);
    }

    QObject::connect(tab, &QObject::destroyed, this, [this, tab]() {
        // Find and remove from maps using pointer value
        QString idToRemove;
        for (auto it = m_instances.begin(); it != m_instances.end(); ++it) {
            if (it.value().data() == tab) {
                idToRemove = it.key();
                break;
            }
        }
        if (!idToRemove.isEmpty())
            m_instances.remove(idToRemove);
        m_tabList.removeAll(tab);
    });

    return tab;
}

void TraceModule::invoke(const QString &action, const QVariant &arg)
{
    // Frame reception: hot path, dispatch to all running tabs
    if (action == QStringLiteral("onFrame")) {
        const CanFrame frame = arg.value<CanFrame>();
        for (auto *tab : qAsConst(m_tabList)) {
            if (!tab || !tab->isRunning())
                continue;
            tab->appendFrame(frame);
            if (m_autoScroll && !tab->isOverwriteMode()) {
                auto *tv = tab->traceView();
                if (tv)
                    tv->scrollToBottom();
            }
        }
    } else if (action == QStringLiteral("setAutoScroll")) {
        m_autoScroll = arg.toBool();
        // Also sync each tab's internal autoscroll state
        for (auto *tab : qAsConst(m_tabList)) {
            if (tab) {
                auto *tv = tab->traceView();
                if (tv)
                    tv->setAutoScrollEnabled(m_autoScroll);
            }
        }
    } else if (action == QStringLiteral("setRunning")) {
        // set running state on a specific instance by id or QWidget*
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
        for (auto *tab : qAsConst(m_tabList)) {
            if (tab)
                tab->setRunning(running);
        }
    } else if (action == QStringLiteral("clearTraceAll")) {
        // Just clear traces (not info panels)
        for (auto *tab : qAsConst(m_tabList)) {
            if (tab)
                tab->clearTrace();
        }
    } else if (action == QStringLiteral("clearAll")) {
        // Full clear: trace + info panels
        for (auto *tab : qAsConst(m_tabList)) {
            if (tab) {
                tab->clearTrace();
                tab->frameInfo()->clear();
                tab->signalDecode()->clear();
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
                    if (report && !ok) {
                        // This invoke context may not have ctx - skip problem reporting or pass as arg
                        // For safety: just log in console
                        qDebug() << "Filter syntax error:" << expr;
                    }
                }
            }
        }
    } else if (action == QStringLiteral("jumpToFrame")) {
        // Bookmark jump: scroll to index + select
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
        TraceTab *firstTab = m_tabList.isEmpty() ? nullptr : qobject_cast<TraceTab *>(m_tabList.first().data());
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
            for (auto *tab : qAsConst(m_tabList)) {
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
    } else if (what == QStringLiteral("colorRules")) {
        // Return current color rules from any instance (or all?) as QVariantList of maps
        auto tab = qobject_cast<TraceTab*>(arg.value<QWidget*>());
        if (tab && tab->traceModel()) {
            QVariantList rules;
            for (const auto &r : tab->traceModel()->colorRules()) {
                QVariantMap m;
                m["expr"] = r.expr;
                m["background"] = r.background.name();
                m["foreground"] = r.foreground.name();
                m["enabled"] = r.enabled;
                rules.append(m);
            }
            return rules;
        }
    }
    return {};
}

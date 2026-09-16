#ifndef TRACE_MODULE_H
#define TRACE_MODULE_H

#include "core/module/imodule.h"

#include <QList>
#include <QMap>
#include <QPointer>

class TraceTab;
class FilterPresetManager;

/**
 * @brief TraceModule - CANoe 风格 Trace 页面模块（openbus_trace.dll）
 *
 * 功能：
 *   - TraceTab 页面创建/管理（多实例，每用户新建一页 Tab）
 *   - Frame 分发 + 过滤 + 着色规则（traceview/filterbar/tracemodel）
 *   - 书签跳转 + 颜色规则编辑器 + 统计显示
 *   - 文件加载反馈 + 流控（running/offline/realtime gate）
 *
 * 依赖：核心服务（DbcManager from ShellContext）+ Qt Widgets
 */
class TraceModule : public IBusinessModule
{
public:
    explicit TraceModule();
    ~TraceModule() override = default;

    QString id() const override { return QStringLiteral("trace"); }
    QString title() const override { return QStringLiteral("Trace"); }
    QIcon icon() const override; // :/icons/list.svg or database.svg
    QStringList pages() const override { return { QStringLiteral("trace") }; }

    QWidget *createWidget(ShellContext &ctx) override;
    QWidget *createPage(const QString &pageId, const QVariant &param, ShellContext &ctx) override;

    /**
     * @brief Invoke actions
     * @param action: "onFrame"(CanFrame), "setAutoScroll"(bool),
     *                "setRunning"(QVariantList{id, bool}), "setRunningAll"(bool),
     *                "clearTraceAll", "clearAll",
     *                "appendFrames"(QVariantList{QWidget* target, QVariantList frames}),
     *                "setFilterExpression"(QVariantList{QWidget*, expr, report}),
     *                "jumpToFrame"(QVariantList{QWidget*, int}), "editColorRules",
     *                "setColorRules"(QVariantList{QWidget*, QVariantList rules})
     * @param arg: can vary per action
     */
    void invoke(const QString &action, const QVariant &arg) override;

    /**
     * @brief Query handlers
     * @param what: "isTrace"(QWidget*→bool), "instance"(id→QWidget*),
     *              "filterExpression"(id→QString), "frameCount"(QWidget*→int),
     *              "selectedFrames"(QWidget*→QVariantList<CanFrame>),
     *              "recentFrames"([QWidget*,count]→QVariantList<CanFrame>),
     *              "colorRules"(id|QWidget*→QVariantList of maps)
     */
    QVariant query(const QString &what, const QVariant &arg) override;

private:
    struct SignalRule {
        QVariantMap rule;           // variant of CanTraceModel::ColorRule
        bool enabled = true;
    };
    QList<SignalRule> m_colorRules;                          ///< 当前着色规则列表（内存副本）
    QMap<QString, QPointer<TraceTab>> m_instances;            ///< instance id → TraceTab* (tracked)
    QList<QPointer<TraceTab>> m_tabList;                      ///< All created tabs (for iteration onFrame/clear)
    FilterPresetManager *m_filterPresets = nullptr;           ///< Per-instance preset manager (shared)
    bool m_autoScroll = true;                                 ///< Autoscroll state (synced via shellInvoke/onAutoScrollToggled)
    ShellContext m_ctx;                                       ///< Stored context for dialog parents (from first createPage)
};

#endif // TRACE_MODULE_H

#ifndef GRAPHIC_MODULE_H
#define GRAPHIC_MODULE_H

#include "core/module/imodule.h"
#include "core/canframe.h"

#include <QList>
#include <QMap>
#include <QPointer>
#include <QVector>
#include <QVariantMap>

class GraphicView;
class DataWindow;

/**
 * @brief GraphicModule - CANoe 风格 Graphic 页面模块（openbus_graphic.dll）
 *
 * 功能：
 *   - GraphicView 页面创建/管理（多实例，每用户新建一页 Graph）
 *   - Frame 分发（仅启用 flowEnabled 的视图）
 *   - 信号配置加载/保存（loadSignalConfigs/signalConfigs）
 *   - Signal add/apply（from DBC double click or frameAddToGraphic message signals）
 *   - DataWindow 辅助窗口（实时信号表格）
 *
 * 依赖：核心服务（DbcManager from ShellContext）+ qcustomplot + Qt Widgets
 */
class GraphicModule : public IBusinessModule
{
public:
    explicit GraphicModule();
    ~GraphicModule() override = default;

    QString id() const override { return QStringLiteral("graphic"); }
    QString title() const override { return QStringLiteral("Graphic"); }
    QIcon icon() const override; // :/icons/graphic.svg or database.svg
    QStringList pages() const override { return { QStringLiteral("graphic"), QStringLiteral("datawindow") }; }

    QWidget *createWidget(ShellContext &ctx) override { return createPage("graphic", {}, ctx); }
    QWidget *createPage(const QString &pageId, const QVariant &param, ShellContext &ctx) override;

    void invoke(const QString &action, const QVariant &arg) override;
    QVariant query(const QString &what, const QVariant &arg) override;

private:
    QMap<QString, QPointer<GraphicView>> m_instances;             ///< instance id → GraphicView* (tracked)
    QList<QPointer<GraphicView>> m_viewList;                      ///< All created views (for iteration onFrame/clearDataAll)
    DataWindow *m_dataWindow = nullptr;                            ///< Cached DataWindow instance (single)
    ShellContext m_ctx;                                           ///< Stored context for dialog/dataWindow parent

    /// Offline Player frames, else CaptureLog snapshot (same timestamps as Trace).
    const QVector<CanFrame> *replayHistory(int *count) const;

    mutable QVector<CanFrame> m_captureHistoryCache;  ///< CaptureLog snapshot for Graphic backfill
};

#endif // GRAPHIC_MODULE_H

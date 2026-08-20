#ifndef GRAPHIC_MODULE_H
#define GRAPHIC_MODULE_H

#include "core/module/imodule.h"
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
    QList<QPointer<GraphicView>> m_cursorLinkedViews;              ///< Views with cursor linkage established
    DataWindow *m_dataWindow = nullptr;                            ///< Cached DataWindow instance (single)
    ShellContext m_ctx;                                           ///< Stored context for dialog/dataWindow parent

    /**
     * @brief Convert DbcSignal struct to QVariantMap (serializable across DLL boundaries)
     */
    static QVariantMap dbcSignalToVariantMap(const DbcSignal &sig);

    /**
     * @brief Convert QVariantMap back to DbcSignal
     */
    static DbcSignal variantMapToDbcSignal(const QVariantMap &map);

    /**
     * @brief Build full GraphicView::Signal from name/canId/extended + DbcSignal lookup
     * Used by shell when forwarding onFrameDoubleClicked/onSignalDoubleClicked results
     */
    static QVariantMap buildSignalMap(qint32 canId, bool extended, const QString &name,
                                       const QVariantMap &dbcSigOverride);
};

#endif // GRAPHIC_MODULE_H

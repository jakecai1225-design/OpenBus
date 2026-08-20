#include "dbcmodule.h"

#include "dbcdetailtab.h"
#include "tools/dbcsignallistview.h"

// openbus_dbc.dll 唯一显式导出的符号（拆分方案 §4.2 决策 1）。
// 工厂按模块唯一命名（openbus_create<Xxx>Module），避免多 DLL 链接期同名冲突。
extern "C" IBusinessModule *openbus_createDbcModule()
{
    return new DbcModule;
}

QString DbcModule::id() const
{
    return QStringLiteral("dbc");
}

QString DbcModule::title() const
{
    return QStringLiteral("DBC");
}

QIcon DbcModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/database.svg"));
}

QStringList DbcModule::pages() const
{
    return { QStringLiteral("detail"), QStringLiteral("signallist") };
}

QWidget *DbcModule::createWidget(ShellContext &ctx)
{
    // 模块默认落地页：信号清单导出工具（detail 页需文件名参数，由壳带参调用）
    return createPage(QStringLiteral("signallist"), {}, ctx);
}

QWidget *DbcModule::createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
{
    m_ctx = ctx;

    if (pageId == QStringLiteral("detail")) {
        // DBC 详情页：多实例（每个 DBC 文件一页），不做缓存
        auto *tab = new DbcDetailTab(param.toString(), ctx.dbcManager, ctx.mainWindow);

        // 信号联动经 shellInvoke 反向委托壳编排（拆分方案 B3）：
        // 双击/加到 Graphic → 壳的信号→Graphic；加到 Trace → 壳的信号→Trace
        QObject::connect(tab, &DbcDetailTab::signalDoubleClicked, tab,
                         [this](quint32 canId, const QString &signalName) {
            if (m_ctx.shellInvoke)
                m_ctx.shellInvoke(QStringLiteral("signalDoubleClicked"),
                                  QVariantList{ canId, signalName });
        });
        QObject::connect(tab, &DbcDetailTab::signalAddToGraphic, tab,
                         [this](quint32 canId, const QString &signalName) {
            if (m_ctx.shellInvoke)
                m_ctx.shellInvoke(QStringLiteral("signalDoubleClicked"),
                                  QVariantList{ canId, signalName });
        });
        QObject::connect(tab, &DbcDetailTab::signalAddToTrace, tab,
                         [this](quint32 canId, const QString &signalName) {
            if (m_ctx.shellInvoke)
                m_ctx.shellInvoke(QStringLiteral("signalAddToTrace"),
                                  QVariantList{ canId, signalName });
        });
        return tab;
    }
    if (pageId == QStringLiteral("signallist"))
        return new DbcSignalListView(ctx.mainWindow);

    return nullptr;
}

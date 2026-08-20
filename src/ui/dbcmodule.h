#ifndef DBCMODULE_H
#define DBCMODULE_H

#include "core/module/imodule.h"

/**
 * @brief DBC 业务模块 — DBC 详情页 + 信号清单工具（doc/拆分应用实施方案.md B3）
 *
 * openbus_dbc.dll 的适配器。页面经 createPage 创建：
 *  - "detail"    (param = QString 文件名) DBC 详情页，多实例（每个文件一页），
 *                原 MainWindow::onDbcFileClicked 的装配迁入；
 *                信号联动（双击/加 Graphic/加 Trace）经 ctx.shellInvoke 反向委托壳
 *  - "signallist" 信号清单导出工具（DbcSignalListView，原工具集）
 *
 * 注：dbcimportdialog（从 DBC 导入报文）唯一使用者是发送页，
 * 已随 B2 落在 openbus_transceive.dll（见构建基线.md B2 备注）。
 */
class DbcModule : public IBusinessModule {
public:
    QString id() const override;
    QString title() const override;
    QIcon icon() const override;
    QWidget *createWidget(ShellContext &ctx) override;

    QStringList pages() const override;
    QWidget *createPage(const QString &pageId, const QVariant &param, ShellContext &ctx) override;

private:
    ShellContext m_ctx;   ///< 最近一次 createPage 的上下文拷贝
};

#endif // DBCMODULE_H

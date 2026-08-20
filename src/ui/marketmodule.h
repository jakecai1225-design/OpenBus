#ifndef MARKETMODULE_H
#define MARKETMODULE_H

#include "core/module/imodule.h"

class MarketTab;

/**
 * @brief 插件市场业务模块 — IBusinessModule 适配器（doc/拆分应用实施方案.md B0）
 *
 * B0：与 MarketTab 一起静态链接进主 exe，验证模块接口设计；
 * B1：随 markettab/marketmodel 迁入 openbus_market.dll。
 *
 * 壳对市场页的全部操作经 invoke() 的字符串动作完成（见 imodule.h），
 * 壳不再 include markettab.h。MarketTab 的插件操作请求在模块内直接
 * 转发 PluginManager（data 层单例，无需经壳中转）。
 */
class MarketModule : public IBusinessModule {
public:
    QString id() const override;
    QString title() const override;
    QIcon icon() const override;
    QWidget *createWidget(ShellContext &ctx) override;
    void invoke(const QString &action, const QVariant &arg) override;

private:
    MarketTab *m_tab = nullptr;   ///< 最近创建的页面（标签页关闭销毁后置空）
};

#endif // MARKETMODULE_H

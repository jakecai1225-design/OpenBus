#include "marketmodule.h"

#include <QObject>   // connect/emit（PCH 已含，显式声明保证独立可编译）
#include <QWidget>

#include "marketmodel.h"
#include "markettab.h"
#include "core/plugin/pluginmanager.h"

// openbus_market.dll 唯一显式导出的符号（拆分方案 §4.2 决策 1：
// C 接口零 ABI 面；MinGW 本就全符号导出，extern "C" 保证名修饰稳定）。
// 工厂按模块唯一命名（openbus_create<Xxx>Module），避免多 DLL 链接期同名冲突。
// main() 经此工厂注册进 ModuleRegistry，壳不 include 任何模块头。
extern "C" IBusinessModule *openbus_createMarketModule()
{
    return new MarketModule;
}

QString MarketModule::id() const
{
    return QStringLiteral("market");
}

QString MarketModule::title() const
{
    return QStringLiteral("插件市场");
}

QIcon MarketModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/extensions.svg"));
}

QWidget *MarketModule::createWidget(ShellContext &ctx)
{
    m_tab = new MarketTab(ctx.mainWindow);

    // 插件操作请求 → PluginManager（迁移自 MainWindow::setupMarketTab；
    // PluginManager 为 data 层单例，模块可直接访问。
    // MarketModule 非 QObject 派生 → connect 用完整限定名）
    QObject::connect(m_tab, &MarketTab::pluginActivateRequested,
            m_tab, [](const QString &name) {
        PluginManager::instance()->reactivatePlugin(name);
    });
    QObject::connect(m_tab, &MarketTab::pluginDeactivateRequested,
            m_tab, [](const QString &name) {
        PluginManager::instance()->deactivatePlugin(name);
        emit PluginManager::instance()->pluginListChanged();
    });
    QObject::connect(m_tab, &MarketTab::pluginToggleRequested,
            m_tab, [](const QString &name, bool enable) {
        PluginManager::instance()->setPluginEnabled(name, enable);
    });

    // 标签页被关闭后 widget 被销毁 → 置空指针，避免悬空引用
    QObject::connect(m_tab, &QObject::destroyed, m_tab, [this]() {
        m_tab = nullptr;
    });
    return m_tab;
}

void MarketModule::invoke(const QString &action, const QVariant &arg)
{
    if (!m_tab)
        return;
    if (action == QStringLiteral("refreshInstalled")) {
        m_tab->refreshInstalled();
    } else if (action == QStringLiteral("focusSearch")) {
        m_tab->focusSearch();
    } else if (action == QStringLiteral("installLocalFile")) {
        m_tab->installLocalFile(arg.toString());
    } else if (action == QStringLiteral("revealItem")) {
        m_tab->revealItem(arg.value<MarketItem>());
    }
}

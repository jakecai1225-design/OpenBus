#include "sendflowmodule.h"

#include <QIcon>
#include <QVBoxLayout>

#include "sendflowview.h"
#include "ui/thememanager.h"

// openbus_sendflow.dll 唯一显式导出的符号（拆分方案 §4.2 决策 1）。
// 工厂按模块唯一命名，避免多 DLL 链接期同名冲突。
extern "C" IBusinessModule *openbus_createSendFlowModule()
{
    return new SendFlowModule;
}

QString SendFlowModule::id() const
{
    return QStringLiteral("sendflow");
}

QString SendFlowModule::title() const
{
    return QStringLiteral("CAN 数据发送");
}

QIcon SendFlowModule::icon() const
{
    return QIcon(QStringLiteral(":/icons/signal.svg"));
}

QWidget *SendFlowModule::createWidget(ShellContext &ctx)
{
    return createPage(QStringLiteral("setup"), ctx);
}

QStringList SendFlowModule::pages() const
{
    return { QStringLiteral("setup") };
}

QWidget *SendFlowModule::createPage(const QString &pageId, const QVariant &param, ShellContext &ctx)
{
    Q_UNUSED(param);
    if (QWidget *existing = m_pages.value(pageId))
        return existing;

    m_ctx = ctx;
    QWidget *page = nullptr;

    if (pageId == QStringLiteral("setup"))
        page = createSetupPage(ctx);

    if (!page)
        return nullptr;

    cachePage(pageId, page);

    // 标签页关闭 → widget 销毁 → 从页面表移除（下次 createPage 重新创建）
    QObject::connect(page, &QObject::destroyed, page, [this, pageId]() {
        m_pages.remove(pageId);
    });

    return page;
}

QWidget *SendFlowModule::createPage(const QString &pageId, ShellContext &ctx)
{
    // 两参重载必须显式转发（DEF-10）
    return createPage(pageId, QVariant(), ctx);
}

void SendFlowModule::invoke(const QString &action, const QVariant &arg)
{
    Q_UNUSED(action)
    Q_UNUSED(arg)
    // TODO: 后续根据需求添加动作处理
}

QVariant SendFlowModule::query(const QString &what, const QVariant &arg)
{
    Q_UNUSED(what)
    Q_UNUSED(arg)
    return {};
}

QWidget *SendFlowModule::createSetupPage(ShellContext &ctx)
{
    auto *view = new SendFlowView(ctx.mainWindow);
    view->setDbcManager(ctx.dbcManager);
    view->setDeviceManager(ctx.deviceManager);

    // 建立与收发模块的信号发送 Tab 的连接
    QObject::connect(view, &SendFlowView::openSendTabRequested, [this]() {
        if (m_ctx.shellInvoke) {
            m_ctx.shellInvoke(QStringLiteral("openPage"), QVariantList()
                << QStringLiteral("transceive") 
                << QStringLiteral("signalsend"));
        }
    });
    
    // M1: 连接设备运行状态变化，更新流指示器
    if (ctx.deviceManager) {
        connect(view, &SendFlowView::flowActiveChanged,
                this, [this](bool active) {
            if (m_ctx.appendOutput) {
                m_ctx.appendOutput(QString("数据流 %1")
                    .arg(active ? QStringLiteral("开始") : QStringLiteral("停止")));
            }
        });
        
        connect(view, &SendFlowView::deviceStatusChanged,
                this, [this](bool connected, QString deviceName) {
            if (m_ctx.appendOutput && connected) {
                m_ctx.appendOutput(QString("设备已连接：%1").arg(deviceName));
            } else if (m_ctx.appendOutput && !connected) {
                m_ctx.appendOutput("设备已断开");
            }
        });
    }

    // 初始化构建拓扑图
    view->buildTopology();
    
    // M1: 初始刷新一次所有块的状态灯
    view->updateBlockLamps();

    return view;
}

void SendFlowModule::cachePage(const QString &pageId, QWidget *page)
{
    m_pages.insert(pageId, page);
}

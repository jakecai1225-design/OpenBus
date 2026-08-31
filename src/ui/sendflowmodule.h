#ifndef SEND_FLOW_MODULE_H
#define SEND_FLOW_MODULE_H

#include "core/module/imodule.h"

class SendFlowView;
class DeviceConnectionTab;

/**
 * @brief CAN Data Send Flow 业务模块 — 发送流可视化配置页（新增 Flow 类型）
 *
 * openbus_sendflow.dll 的适配器。页面经 createPage 创建：
 *   - "setup"  发送流配置页（SendFlowView），单实例缓存
 *
 * 职责划分（基于 FlowModule B4 拆分经验）：
 *   - 模块侧：纯数据层操作（设备 configure/start/stop、DBC 列表同步等）经 ShellContext
 *     指针直接完成；
 *   - 壳侧：跨模块编排（标签页切换、状态同步等）经 ctx.shellInvoke 回调壳。
 *
 * 三层级可视化拓扑图：
 *   Layer 1: SignalGenerator（单个信号发生器）
 *                │
 *   Layer 2: Trace + Graphic + Record（并行接收端）
 *                │
 *   Layer 3: Real（实际连接的设备显示）
 */
class SendFlowModule : public IBusinessModule {
public:
    QString id() const override;
    QString title() const override;
    QIcon icon() const override;
    QWidget *createWidget(ShellContext &ctx) override;

    QStringList pages() const override;
    QWidget *createPage(const QString &pageId, const QVariant &param, ShellContext &ctx) override;
    QWidget *createPage(const QString &pageId, ShellContext &ctx) override;

    void invoke(const QString &action, const QVariant &arg) override;
    QVariant query(const QString &what, const QVariant &arg) override;

private:
    QWidget *createSetupPage(ShellContext &ctx);
    void cachePage(const QString &pageId, QWidget *page);

    ShellContext m_ctx;                       ///< 最近一次 createPage 的上下文拷贝（包含 deviceManager）
    QMap<QString, QPointer<QWidget>> m_pages; ///< 页面缓存（"setup" 单实例）
};

#endif // SEND_FLOW_MODULE_H

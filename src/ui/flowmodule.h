#ifndef FLOWMODULE_H
#define FLOWMODULE_H

#include "core/module/imodule.h"

class MeasurementSetupView;
class DeviceConnectionTab;

/**
 * @brief 测量流程业务模块 — Flow 页 + 设备连接页（doc/拆分应用实施方案.md B4）
 *
 * openbus_flow.dll 的适配器。页面经 createPage 创建：
 *  - "setup"  测量配置页（MeasurementSetupView），单实例缓存
 *  - "device" 设备连接页（DeviceConnectionTab），单实例缓存，
 *             param = QVariantList{deviceKind, devIndex, deviceName, deviceType}
 *
 * 职责划分（B4 约定）：
 *  - 模块侧：纯数据层操作（数据源切换停 player/simulator/deviceManager、
 *    设备 configure/start/stop、DBC 列表同步、DBC 选择/加载）经 ShellContext
 *    指针直接完成；
 *  - 壳侧：跨模块编排（测量启停的离线加载/Trace 实例门控、实例打开/关闭、
 *    标签页切换、DBC 卸载关页）经 ctx.shellInvoke 回调壳。
 */
class FlowModule : public IBusinessModule {
public:
    QString id() const override;
    QString title() const override;
    QIcon icon() const override;
    QWidget *createWidget(ShellContext &ctx) override;

    QStringList pages() const override;
    QWidget *createPage(const QString &pageId, const QVariant &param, ShellContext &ctx) override;

    void invoke(const QString &action, const QVariant &arg) override;
    QVariant query(const QString &what, const QVariant &arg) override;

private:
    QWidget *createSetupPage(ShellContext &ctx);
    QWidget *createDevicePage(const QVariant &param, ShellContext &ctx);
    void cachePage(const QString &pageId, QWidget *page);

    ShellContext m_ctx;                       ///< 最近一次 createPage 的上下文拷贝
    QMap<QString, QPointer<QWidget>> m_pages; ///< 页面缓存（"setup"/"device" 单实例）
};

#endif // FLOWMODULE_H

#ifndef TRANSCEIVEMODULE_H
#define TRANSCEIVEMODULE_H

#include <QHash>

#include "core/module/imodule.h"

class SignalSendTab;
class PlaybackTab;
class OfflineAnalysisTab;
class RecordTab;
class QTimer;
class TriggerRecorder;

/**
 * @brief 收发业务模块 — 发送/回放/离线分析/录制 四页面（doc/拆分应用实施方案.md B2）
 *
 * openbus_transceive.dll 的适配器。原 MainWindow::setupSendTab /
 * setupPlaybackTab / setupOfflineAnalysisTab / setupRecordTab 的装配逻辑
 * 全部迁入本模块（§4.5：模块自己连接自己的信号槽）：
 *  - 数据层服务经 ShellContext 指针（player/recorder/deviceManager/dbcManager）
 *  - 底部输出/问题面板经 ctx.appendOutput / ctx.addProblem 回调
 *  - 壳编排类动作（回放链路、清视图、状态栏）经 ctx.shellInvoke 反向委托
 *
 * 壳对页面的操控经 invoke()（setRecording/setFileInfo/setProgress/
 * setPlayerLoaded），离线文件查询经 query("offlineFiles")。
 */
class TransceiveModule : public IBusinessModule {
public:
    QString id() const override;
    QString title() const override;
    QIcon icon() const override;
    QWidget *createWidget(ShellContext &ctx) override;

    // 多页面模块（B2 接口扩展）
    QStringList pages() const override;
    QWidget *createPage(const QString &pageId, ShellContext &ctx) override;
    void invoke(const QString &action, const QVariant &arg) override;
    QVariant query(const QString &what, const QVariant &arg = {}) override;

private:
    QWidget *createSendPage(ShellContext &ctx);
    QWidget *createPlaybackPage(ShellContext &ctx);
    QWidget *createOfflinePage(ShellContext &ctx);
    QWidget *createRecordPage(ShellContext &ctx);

    ShellContext m_ctx;                       ///< 最近一次 createPage 的上下文拷贝
    QHash<QString, QWidget *> m_pages;        ///< pageId → 页面（销毁后移除）
    QHash<int, QTimer *> m_periodicSenders;   ///< 发送行号 → 周期发送定时器
    TriggerRecorder *m_triggerRecorder = nullptr;  ///< 触发录制器（随录制页创建）
};

#endif // TRANSCEIVEMODULE_H

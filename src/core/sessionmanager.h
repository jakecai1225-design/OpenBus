#ifndef SESSIONMANAGER_H
#define SESSIONMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QByteArray>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief 会话管理器 — 管理 sessions.json（个人会话状态）
 *
 * 存储位置: AppDataLocation/sin/sessions.json
 *
 * 职责：
 *   - 最近打开的工程/工作区列表（含元数据：名称、修改时间、固定标记）
 *   - 最后打开的工作区/工程路径
 *   - UI 状态（窗口几何、侧边栏可见性、活跃面板）
 *
 * 与 AppConfig 解耦：AppConfig 管理全局应用设置，
 * SessionManager 管理个人会话状态。
 *
 * 迁移策略：首次加载时若 sessions.json 不存在但 AppConfig 有
 * project.recent，自动迁移并标记已迁移。
 */
class SessionManager : public QObject
{
    Q_OBJECT

public:
    static SessionManager *instance();

    /// 加载 sessions.json（启动时调用）
    void load();

    /// 保存到 sessions.json
    void save();

    /// sessions.json 文件路径
    QString sessionPath() const;

    // ---- 最近列表 ----

    /// 最近打开项列表（含元数据），每项为 JSON object
    /// 字段: path, type("project"/"workspace"), name, modified, pinned
    QVariantList recentItems() const;

    /// 最近打开项路径列表（纯路径，兼容旧接口）
    QStringList recentPaths() const;

    /// 添加到最近列表（去重 + 置顶 + 限量）
    void addRecent(const QString &path, const QString &type,
                   const QString &name = QString());

    /// 从最近列表移除指定路径
    void removeRecent(const QString &path);

    /// 固定/取消固定
    void pinRecent(const QString &path, bool pinned);

    /// 清空最近列表
    void clearRecent();

    // ---- 最后打开 ----

    QString lastOpenedPath() const;
    QString lastOpenedType() const;
    void setLastOpened(const QString &path, const QString &type);

    // ---- UI 状态 ----

    void saveUiState(const QByteArray &geometry, const QByteArray &windowState);
    QByteArray uiGeometry() const;
    QByteArray uiWindowState() const;

    /// 侧边栏可见性
    bool sidebarVisible() const;
    void setSidebarVisible(bool visible);

    /// 活跃面板名称
    QString activePanel() const;
    void setActivePanel(const QString &panel);

signals:
    void recentChanged();

private:
    SessionManager(QObject *parent = nullptr);

    QString m_path;
    json m_data;

    /// 从 AppConfig 迁移 project.recent（仅首次执行）
    void migrateFromAppConfig();

    /// 确保目录存在
    void ensureDir();

    /// 裁剪最近列表到最大数量
    void trimRecent(int maxCount);

    /// 从文件路径推导显示名称
    static QString deriveName(const QString &path);

    /// 获取文件修改时间（ISO 8601），失败返回空
    static QString fileModifiedTime(const QString &path);
};

#endif // SESSIONMANAGER_H

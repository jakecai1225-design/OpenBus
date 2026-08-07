#include "sessionmanager.h"
#include "appconfig.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariant>

// ============================================================
//  单例
// ============================================================
SessionManager *SessionManager::instance()
{
    static SessionManager inst;
    return &inst;
}

SessionManager::SessionManager(QObject *parent)
    : QObject(parent)
{
}

// ============================================================
//  路径
// ============================================================
QString SessionManager::sessionPath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/sessions.json";
}

void SessionManager::ensureDir()
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
}

// ============================================================
//  加载（含从 AppConfig 迁移）
// ============================================================
void SessionManager::load()
{
    m_path = sessionPath();
    ensureDir();

    QFile file(m_path);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            spdlog::warn("SessionManager: 无法读取 sessions.json，使用空会话");
            m_data = json::object();
            return;
        }
        QByteArray raw = file.readAll();
        try {
            m_data = json::parse(raw.toStdString());
            spdlog::info("SessionManager: 会话加载成功 -> {}", m_path.toStdString());
        } catch (const json::parse_error &e) {
            spdlog::error("SessionManager: JSON 解析失败: {}，使用空会话", e.what());
            m_data = json::object();
        }
        return;
    }

    // sessions.json 不存在 — 尝试从 AppConfig 迁移
    spdlog::info("SessionManager: sessions.json 不存在，尝试迁移");
    m_data = json::object();
    migrateFromAppConfig();
}

void SessionManager::migrateFromAppConfig()
{
    QString raw = AppConfig::instance()->getString("project.recent", "");
    if (raw.isEmpty()) {
        spdlog::info("SessionManager: AppConfig 无 project.recent，无需迁移");
        save();
        return;
    }

    try {
        json oldRecent = json::parse(raw.toStdString());
        if (oldRecent.is_array()) {
            json recentArr = json::array();
            for (const auto &item : oldRecent) {
                if (item.is_string()) {
                    QString path = QString::fromStdString(item.get<std::string>());
                    json entry;
                    entry["path"] = path.toStdString();
                    entry["type"] = "project";
                    entry["name"] = deriveName(path).toStdString();
                    entry["modified"] = fileModifiedTime(path).toStdString();
                    entry["pinned"] = false;
                    recentArr.push_back(entry);
                }
            }
            m_data["recent"] = recentArr;

            // 迁移 lastOpened
            QString lastPath = AppConfig::instance()->getString("project.lastPath", "");
            if (!lastPath.isEmpty()) {
                m_data["lastOpened"]["path"] = lastPath.toStdString();
                m_data["lastOpened"]["type"] = "project";
            }

            // 标记已迁移，清理 AppConfig 中的旧数据
            AppConfig::instance()->reset("project.recent");
            AppConfig::instance()->save();

            spdlog::info("SessionManager: 已从 AppConfig 迁移 {} 条最近记录",
                         recentArr.size());
        }
    } catch (...) {
        spdlog::warn("SessionManager: AppConfig project.recent 解析失败，跳过迁移");
    }

    save();
}

// ============================================================
//  保存
// ============================================================
void SessionManager::save()
{
    if (m_path.isEmpty())
        m_path = sessionPath();

    ensureDir();

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::error("SessionManager: 无法写入 sessions.json -> {}", m_path.toStdString());
        return;
    }

    std::string dump = m_data.dump(2);
    file.write(dump.data(), static_cast<qint64>(dump.size()));
}

// ============================================================
//  最近列表 — 查询
// ============================================================
QVariantList SessionManager::recentItems() const
{
    QVariantList result;
    if (!m_data.contains("recent") || !m_data["recent"].is_array())
        return result;

    for (const auto &item : m_data["recent"]) {
        QVariantMap map;
        if (item.contains("path") && item["path"].is_string())
            map["path"] = QString::fromStdString(item["path"].get<std::string>());
        if (item.contains("type") && item["type"].is_string())
            map["type"] = QString::fromStdString(item["type"].get<std::string>());
        if (item.contains("name") && item["name"].is_string())
            map["name"] = QString::fromStdString(item["name"].get<std::string>());
        if (item.contains("modified") && item["modified"].is_string())
            map["modified"] = QString::fromStdString(item["modified"].get<std::string>());
        if (item.contains("pinned"))
            map["pinned"] = item["pinned"].get<bool>();
        else
            map["pinned"] = false;
        result.append(map);
    }
    return result;
}

QStringList SessionManager::recentPaths() const
{
    QStringList result;
    if (!m_data.contains("recent") || !m_data["recent"].is_array())
        return result;

    for (const auto &item : m_data["recent"]) {
        if (item.contains("path") && item["path"].is_string())
            result << QString::fromStdString(item["path"].get<std::string>());
    }
    return result;
}

// ============================================================
//  最近列表 — 修改
// ============================================================
void SessionManager::addRecent(const QString &path, const QString &type,
                               const QString &name)
{
    if (path.isEmpty())
        return;

    if (!m_data.contains("recent") || !m_data["recent"].is_array())
        m_data["recent"] = json::array();

    json &recent = m_data["recent"];

    // 去重：移除已存在的同路径项
    for (auto it = recent.begin(); it != recent.end(); ) {
        if (it->contains("path") && (*it)["path"].is_string() &&
            QString::fromStdString((*it)["path"].get<std::string>()) == path) {
            it = recent.erase(it);
        } else {
            ++it;
        }
    }

    // 构造新条目
    json entry;
    entry["path"] = path.toStdString();
    entry["type"] = type.toStdString();
    entry["name"] = (name.isEmpty() ? deriveName(path) : name).toStdString();
    entry["modified"] = fileModifiedTime(path).toStdString();
    entry["pinned"] = false;

    // 插入到开头
    recent.insert(recent.begin(), entry);

    // 裁剪
    int maxCount = AppConfig::instance()->getInt("project.recentMax", 10);
    trimRecent(maxCount);

    // 更新 lastOpened
    m_data["lastOpened"]["path"] = path.toStdString();
    m_data["lastOpened"]["type"] = type.toStdString();

    save();
    emit recentChanged();
}

void SessionManager::removeRecent(const QString &path)
{
    if (!m_data.contains("recent") || !m_data["recent"].is_array())
        return;

    json &recent = m_data["recent"];
    for (auto it = recent.begin(); it != recent.end(); ) {
        if (it->contains("path") && (*it)["path"].is_string() &&
            QString::fromStdString((*it)["path"].get<std::string>()) == path) {
            it = recent.erase(it);
        } else {
            ++it;
        }
    }

    save();
    emit recentChanged();
}

void SessionManager::pinRecent(const QString &path, bool pinned)
{
    if (!m_data.contains("recent") || !m_data["recent"].is_array())
        return;

    for (auto &item : m_data["recent"]) {
        if (item.contains("path") && item["path"].is_string() &&
            QString::fromStdString(item["path"].get<std::string>()) == path) {
            item["pinned"] = pinned;
            break;
        }
    }

    save();
    emit recentChanged();
}

void SessionManager::clearRecent()
{
    m_data["recent"] = json::array();
    save();
    emit recentChanged();
}

void SessionManager::trimRecent(int maxCount)
{
    if (!m_data.contains("recent") || !m_data["recent"].is_array())
        return;

    json &recent = m_data["recent"];

    // 从末尾删除非固定项，直到总数不超过 maxCount
    while (recent.size() > static_cast<size_t>(maxCount)) {
        bool removed = false;
        for (int i = static_cast<int>(recent.size()) - 1; i >= 0; --i) {
            bool pinned = recent[i].contains("pinned") && recent[i]["pinned"].get<bool>();
            if (!pinned) {
                recent.erase(recent.begin() + i);
                removed = true;
                break;
            }
        }
        if (!removed)
            break;  // 全部是固定项，不再裁剪
    }
}

// ============================================================
//  最后打开
// ============================================================
QString SessionManager::lastOpenedPath() const
{
    if (m_data.contains("lastOpened") && m_data["lastOpened"].contains("path") &&
        m_data["lastOpened"]["path"].is_string())
        return QString::fromStdString(m_data["lastOpened"]["path"].get<std::string>());
    return {};
}

QString SessionManager::lastOpenedType() const
{
    if (m_data.contains("lastOpened") && m_data["lastOpened"].contains("type") &&
        m_data["lastOpened"]["type"].is_string())
        return QString::fromStdString(m_data["lastOpened"]["type"].get<std::string>());
    return {};
}

void SessionManager::setLastOpened(const QString &path, const QString &type)
{
    m_data["lastOpened"]["path"] = path.toStdString();
    m_data["lastOpened"]["type"] = type.toStdString();
    save();
}

// ============================================================
//  UI 状态
// ============================================================
void SessionManager::saveUiState(const QByteArray &geometry, const QByteArray &windowState)
{
    m_data["ui"]["geometry"] = QString::fromLatin1(geometry).toStdString();
    m_data["ui"]["windowState"] = QString::fromLatin1(windowState).toStdString();
    save();
}

QByteArray SessionManager::uiGeometry() const
{
    if (m_data.contains("ui") && m_data["ui"].contains("geometry") &&
        m_data["ui"]["geometry"].is_string()) {
        QString s = QString::fromStdString(m_data["ui"]["geometry"].get<std::string>());
        return s.toLatin1();
    }
    return {};
}

QByteArray SessionManager::uiWindowState() const
{
    if (m_data.contains("ui") && m_data["ui"].contains("windowState") &&
        m_data["ui"]["windowState"].is_string()) {
        QString s = QString::fromStdString(m_data["ui"]["windowState"].get<std::string>());
        return s.toLatin1();
    }
    return {};
}

bool SessionManager::sidebarVisible() const
{
    if (m_data.contains("ui") && m_data["ui"].contains("sidebarVisible"))
        return m_data["ui"]["sidebarVisible"].get<bool>();
    return true;
}

void SessionManager::setSidebarVisible(bool visible)
{
    m_data["ui"]["sidebarVisible"] = visible;
    save();
}

QString SessionManager::activePanel() const
{
    if (m_data.contains("ui") && m_data["ui"].contains("activePanel") &&
        m_data["ui"]["activePanel"].is_string())
        return QString::fromStdString(m_data["ui"]["activePanel"].get<std::string>());
    return {};
}

void SessionManager::setActivePanel(const QString &panel)
{
    m_data["ui"]["activePanel"] = panel.toStdString();
    save();
}

// ============================================================
//  辅助
// ============================================================
QString SessionManager::deriveName(const QString &path)
{
    QFileInfo fi(path);
    QString base = fi.baseName();
    if (base.isEmpty())
        return path;
    return base;
}

QString SessionManager::fileModifiedTime(const QString &path)
{
    QFileInfo fi(path);
    if (!fi.exists())
        return {};
    return fi.lastModified().toString(Qt::ISODate);
}

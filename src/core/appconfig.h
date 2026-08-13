#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <nlohmann/json.hpp>
#include <memory>

using json = nlohmann::json;

/**
 * @brief 基于 nlohmann/json 的应用配置管理（类似 VS Code settings.json）
 *
 * 配置文件路径: %APPDATA%/openbus/openbus/settings.json
 * 提供 typed getter/setter，支持默认值回退。
 * 修改后调用 save() 持久化，或由析构时自动保存。
 */
class AppConfig : public QObject
{
    Q_OBJECT

public:
    static AppConfig *instance();

    /// 加载配置文件（启动时调用）
    void load();

    /// 保存配置到文件
    void save();

    /// 配置文件路径
    QString configPath() const;

    // ---- typed getters ----
    QString getString(const QString &key, const QString &def = QString()) const;
    int getInt(const QString &key, int def = 0) const;
    bool getBool(const QString &key, bool def = false) const;
    double getDouble(const QString &key, double def = 0.0) const;

    // ---- typed setters ----
    void set(const QString &key, const QString &val);
    void set(const QString &key, int val);
    void set(const QString &key, bool val);
    void set(const QString &key, double val);

    /// 重置某项为默认（删除 key）
    void reset(const QString &key);

    /// 返回全部配置（JSON 文本，供编辑器使用）
    QString toJsonString() const;

    /// 从 JSON 文本批量加载（编辑器保存时调用）
    bool fromJsonString(const QString &jsonStr);

    /// 返回默认配置 JSON
    static json defaultConfig();

signals:
    void changed(const QString &key);

private:
    AppConfig(QObject *parent = nullptr);
    json m_data;
    QString m_path;

    // Qt <-> nlohmann 转换辅助
    static std::string toStd(const QString &s) { return s.toStdString(); }
    static QString fromStd(const std::string &s) { return QString::fromStdString(s); }
};

#endif // APPCONFIG_H

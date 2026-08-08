#include "filterpresetmanager.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

FilterPresetManager::FilterPresetManager(QObject *parent)
    : QObject(parent)
{
    initDefaultPath();
    loadDefault();
}

void FilterPresetManager::initDefaultPath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir dir(configDir);
    if (!dir.exists()) dir.mkpath(".");
    m_defaultPath = dir.filePath("filters.sfilter");
}

bool FilterPresetManager::loadFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    int version = root.value("version").toInt(1);
    QJsonArray filters = root.value("filters").toArray();

    m_presets.clear();
    for (const auto &item : filters) {
        QJsonObject obj = item.toObject();
        Preset p;
        p.name = obj.value("name").toString();
        p.expr = obj.value("expr").toString();
        if (!p.name.isEmpty())
            m_presets.append(p);
    }

    return true;
}

bool FilterPresetManager::saveToFile(const QString &filePath) const
{
    QJsonObject root;
    root["version"] = 1;

    QJsonArray filters;
    for (const auto &p : m_presets) {
        QJsonObject obj;
        obj["name"] = p.name;
        obj["expr"] = p.expr;
        filters.append(obj);
    }
    root["filters"] = filters;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void FilterPresetManager::loadDefault()
{
    if (QFile::exists(m_defaultPath)) {
        loadFromFile(m_defaultPath);
    } else {
        // 初始化内置默认预设
        m_presets.clear();
        m_presets.append({"仅 Rx 帧", "rx"});
        m_presets.append({"仅 Tx 帧", "tx"});
        m_presets.append({"仅 CAN FD", "fd"});
        m_presets.append({"仅扩展帧", "ext"});
        m_presets.append({"错误帧", "error"});
        m_presets.append({"标准帧", "std"});
        saveDefault();
    }
}

bool FilterPresetManager::saveDefault() const
{
    return saveToFile(m_defaultPath);
}

void FilterPresetManager::addPreset(const QString &name, const QString &expr)
{
    // 如果同名已存在，更新
    for (auto &p : m_presets) {
        if (p.name == name) {
            p.expr = expr;
            return;
        }
    }
    m_presets.append({name, expr});
}

void FilterPresetManager::removePreset(int index)
{
    if (index >= 0 && index < m_presets.size())
        m_presets.removeAt(index);
}

QString FilterPresetManager::findExpr(const QString &name) const
{
    for (const auto &p : m_presets)
        if (p.name == name) return p.expr;
    return {};
}

QStringList FilterPresetManager::presetNames() const
{
    QStringList names;
    for (const auto &p : m_presets)
        names << p.name;
    return names;
}

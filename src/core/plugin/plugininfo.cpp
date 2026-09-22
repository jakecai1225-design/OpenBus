#include "plugininfo.h"
#include "core/logging.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

bool PluginInfo::hasActivationEvent(const QString &event) const
{
    return activationEvents.contains(event);
}

bool PluginInfo::activatesOnFrame() const
{
    return hasActivationEvent("onFrame");
}

bool PluginInfo::activatesOnStartup() const
{
    return hasActivationEvent("onStartup");
}

QString PluginInfo::title() const
{
    if (!displayName.isEmpty())
        return displayName;
    if (!commands.isEmpty() && !commands.first().title.isEmpty())
        return commands.first().title;
    return name;
}

bool PluginInfo::loadFromDirectory(const QString &dir)
{
    QString manifestPath = dir + "/plugin.json";
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("PluginInfo: 无法读取 {}", manifestPath.toStdString());
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
        spdlog::warn("PluginInfo: {} JSON 解析错误: {}",
                     manifestPath.toStdString(),
                     parseError.errorString().toStdString());
        return false;
    }

    QJsonObject obj = doc.object();

    name = obj.value("name").toString();
    displayName = obj.value("displayName").toString();
    version = obj.value("version").toString();
    author = obj.value("author").toString();
    description = obj.value("description").toString();
    mainScript = obj.value("main").toString("main.py");
    icon = obj.value("icon").toString();
    directory = dir;

    if (name.isEmpty()) {
        spdlog::warn("PluginInfo: {} 缺少 name 字段", manifestPath.toStdString());
        return false;
    }

    // 激活事件
    QJsonArray events = obj.value("activationEvents").toArray();
    for (const auto &e : events)
        activationEvents.append(e.toString());

    // 贡献点 — commands
    QJsonObject contributes = obj.value("contributes").toObject();
    QJsonArray cmds = contributes.value("commands").toArray();
    for (const auto &c : cmds) {
        QJsonObject cmd = c.toObject();
        commands.append({cmd.value("id").toString(),
                         cmd.value("title").toString()});
    }

    // 贡献点 — fileFormats
    QJsonArray formats = contributes.value("fileFormats").toArray();
    for (const auto &f : formats) {
        QJsonObject fmt = f.toObject();
        fileFormats.append({fmt.value("extension").toString(),
                            fmt.value("name").toString()});
    }

    spdlog::info("PluginInfo: 已加载插件 '{}' v{} ({})", name.toStdString(),
                 version.toStdString(), dir.toStdString());
    return true;
}

QString PluginInfo::iconFilePath() const
{
    if (icon.isEmpty() || directory.isEmpty())
        return QString();
    const QString path = QDir(directory).filePath(icon);
    return QFileInfo::exists(path) ? path : QString();
}

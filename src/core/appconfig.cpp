#include "appconfig.h"
#include "logging.h"

#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>

AppConfig *AppConfig::instance()
{
    static AppConfig inst;
    return &inst;
}

AppConfig::AppConfig(QObject *parent)
    : QObject(parent)
{
    m_data = defaultConfig();
}

QString AppConfig::configPath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + "/settings.json";
}

void AppConfig::load()
{
    m_path = configPath();

    // 确保目录存在
    QDir().mkpath(QFileInfo(m_path).absolutePath());

    QFile file(m_path);
    if (!file.exists()) {
        // 首次运行：写入默认配置
        m_data = defaultConfig();
        save();
        spdlog::info("AppConfig: 首次初始化，已写入默认配置 -> {}", m_path.toStdString());
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("AppConfig: 无法读取配置文件，使用默认值");
        m_data = defaultConfig();
        return;
    }

    QByteArray raw = file.readAll();
    try {
        m_data = json::parse(raw.toStdString());
        spdlog::info("AppConfig: 配置加载成功 -> {}", m_path.toStdString());
    } catch (const json::parse_error &e) {
        spdlog::error("AppConfig: JSON 解析失败: {}，使用默认值", e.what());
        m_data = defaultConfig();
    }
}

void AppConfig::save()
{
    if (m_path.isEmpty())
        m_path = configPath();

    QDir().mkpath(QFileInfo(m_path).absolutePath());

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::error("AppConfig: 无法写入配置文件 -> {}", m_path.toStdString());
        return;
    }

    std::string dump = m_data.dump(4);
    file.write(dump.data(), static_cast<qint64>(dump.size()));
    spdlog::info("AppConfig: 配置已保存 -> {}", m_path.toStdString());
}

// ---- getters ----

QString AppConfig::getString(const QString &key, const QString &def) const
{
    auto it = m_data.find(toStd(key));
    if (it == m_data.end() || !it->is_string())
        return def;
    return fromStd(it->get<std::string>());
}

int AppConfig::getInt(const QString &key, int def) const
{
    auto it = m_data.find(toStd(key));
    if (it == m_data.end() || !it->is_number_integer())
        return def;
    return it->get<int>();
}

bool AppConfig::getBool(const QString &key, bool def) const
{
    auto it = m_data.find(toStd(key));
    if (it == m_data.end() || !it->is_boolean())
        return def;
    return it->get<bool>();
}

double AppConfig::getDouble(const QString &key, double def) const
{
    auto it = m_data.find(toStd(key));
    if (it == m_data.end() || !it->is_number())
        return def;
    return it->get<double>();
}

// ---- setters ----

void AppConfig::set(const QString &key, const QString &val)
{
    m_data[toStd(key)] = val.toStdString();
    emit changed(key);
}

void AppConfig::set(const QString &key, int val)
{
    m_data[toStd(key)] = val;
    emit changed(key);
}

void AppConfig::set(const QString &key, bool val)
{
    m_data[toStd(key)] = val;
    emit changed(key);
}

void AppConfig::set(const QString &key, double val)
{
    m_data[toStd(key)] = val;
    emit changed(key);
}

void AppConfig::reset(const QString &key)
{
    m_data.erase(toStd(key));
    emit changed(key);
}

QString AppConfig::toJsonString() const
{
    return fromStd(m_data.dump(4));
}

bool AppConfig::fromJsonString(const QString &jsonStr)
{
    try {
        m_data = json::parse(jsonStr.toStdString());
        return true;
    } catch (const json::parse_error &e) {
        spdlog::error("AppConfig: JSON 解析失败: {}", e.what());
        return false;
    }
}

json AppConfig::defaultConfig()
{
    return {
        // ---- 通用 ----
        {"theme", "Dark+ (default dark)"},
        {"font.family", "Consolas"},
        {"font.size", 10},
        {"window.rememberGeometry", true},
        {"window.width", 1400},
        {"window.height", 900},

        // ---- Trace ----
        {"trace.maxFrames", 10000},
        {"trace.overwriteMode", false},
        {"trace.autoScroll", true},
        {"trace.showGrid", true},
        {"trace alternatingRowColors", true},

        // ---- Graphic ----
        {"graphic.timeWindow", 30.0},
        {"graphic.antialiasing", true},
        {"graphic.fps", 30},

        // ---- Record ----
        {"record.defaultFormat", "sin"},
        {"record.autoSave", false},

        // ---- Filter ----
        {"filter.recentMax", 10},

        // ---- 日志 ----
        {"log.level", "info"},
        {"log.maxFileSize", 1048576},
        {"log.maxFiles", 5},

        // ---- 工程 ----
        {"project.lastPath", ""},
        {"project.recent", "[]"},
        {"project.recentMax", 10},
        {"project.autoSaveOnClose", true}
    };
}

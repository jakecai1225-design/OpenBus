#include "projectmanager.h"
#include "appconfig.h"
#include "logging.h"

#include <QFile>
#include <QFileInfo>

// ============================================================
//  单例
// ============================================================
ProjectManager *ProjectManager::instance()
{
    static ProjectManager inst;
    return &inst;
}

ProjectManager::ProjectManager(QObject *parent)
    : QObject(parent)
{
}

// ============================================================
//  新建工程
// ============================================================
void ProjectManager::newProject(const QString &name)
{
    m_state = ProjectState{};
    m_state.name = name;
    m_filePath.clear();
    m_modified = true;
    emit projectLoaded(name);
    spdlog::info("ProjectManager: 新建工程 '{}'", name.toStdString());
}

// ============================================================
//  加载工程
// ============================================================
bool ProjectManager::loadProject(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::error("ProjectManager: 无法打开工程文件 {}", filePath.toStdString());
        return false;
    }

    QByteArray raw = file.readAll();
    try {
        json j = json::parse(raw.toStdString());
        m_state = jsonToState(j);
        m_filePath = filePath;
        m_modified = false;
        addRecentProject(filePath);
        AppConfig::instance()->set("project.lastPath", filePath);
        emit projectLoaded(m_state.name);
        spdlog::info("ProjectManager: 工程加载成功 '{}' <- {}", m_state.name.toStdString(), filePath.toStdString());
        return true;
    } catch (const json::parse_error &e) {
        spdlog::error("ProjectManager: JSON 解析失败: {}", e.what());
        return false;
    }
}

// ============================================================
//  保存工程
// ============================================================
bool ProjectManager::saveProject(const QString &filePath)
{
    QString path = filePath.isEmpty() ? m_filePath : filePath;
    if (path.isEmpty()) {
        spdlog::warn("ProjectManager: 保存路径为空");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::error("ProjectManager: 无法写入工程文件 {}", path.toStdString());
        return false;
    }

    json j = stateToJson(m_state);
    std::string dump = j.dump(2);
    file.write(dump.data(), static_cast<qint64>(dump.size()));
    file.close();

    m_filePath = path;
    m_modified = false;
    addRecentProject(path);
    AppConfig::instance()->set("project.lastPath", path);
    emit projectSaved(path);
    spdlog::info("ProjectManager: 工程已保存 '{}' -> {}", m_state.name.toStdString(), path.toStdString());
    return true;
}

bool ProjectManager::saveAs(const QString &filePath)
{
    return saveProject(filePath);
}

// ============================================================
//  最近工程列表
// ============================================================
QStringList ProjectManager::recentProjects() const
{
    QStringList result;
    QString raw = AppConfig::instance()->getString("project.recent", "");
    if (raw.isEmpty()) return result;

    try {
        json j = json::parse(raw.toStdString());
        if (j.is_array()) {
            for (const auto &item : j) {
                if (item.is_string())
                    result << QString::fromStdString(item.get<std::string>());
            }
        }
    } catch (...) {}

    return result;
}

void ProjectManager::addRecentProject(const QString &path)
{
    QStringList recent = recentProjects();
    recent.removeAll(path);
    recent.prepend(path);

    int maxCount = AppConfig::instance()->getInt("project.recentMax", 10);
    while (recent.size() > maxCount)
        recent.removeLast();

    json j = json::array();
    for (const auto &p : recent)
        j.push_back(p.toStdString());

    AppConfig::instance()->set("project.recent", QString::fromStdString(j.dump()));
    AppConfig::instance()->save();
}

void ProjectManager::clearRecent()
{
    AppConfig::instance()->set("project.recent", "[]");
    AppConfig::instance()->save();
}

// ============================================================
//  序列化
// ============================================================
QString ProjectManager::toJsonString() const
{
    return QString::fromStdString(stateToJson(m_state).dump(2));
}

bool ProjectManager::fromJsonString(const QString &jsonStr)
{
    try {
        json j = json::parse(jsonStr.toStdString());
        m_state = jsonToState(j);
        m_modified = true;
        return true;
    } catch (const json::parse_error &e) {
        spdlog::error("ProjectManager: JSON 解析失败: {}", e.what());
        return false;
    }
}

// ============================================================
//  JSON <-> ProjectState 转换
// ============================================================
json ProjectManager::stateToJson(const ProjectState &st)
{
    json j;
    j["name"] = st.name.toStdString();
    j["version"] = 1;

    j["source"]["mode"] = st.sourceMode;
    j["source"]["filePath"] = st.filePath.toStdString();
    j["source"]["baudrate"] = st.baudrate;
    j["source"]["channel"] = st.channel;

    json dbcArr = json::array();
    for (const auto &f : st.dbcFiles)
        dbcArr.push_back(f.toStdString());
    j["dbc"]["files"] = dbcArr;

    json traceArr = json::array();
    for (const auto &t : st.traces) {
        json tj;
        tj["id"] = t.id.toStdString();
        tj["title"] = t.title.toStdString();
        tj["filter"] = t.filterExpression.toStdString();
        traceArr.push_back(tj);
    }
    j["traces"] = traceArr;

    json graphicArr = json::array();
    for (const auto &g : st.graphics) {
        json gj;
        gj["id"] = g.id.toStdString();
        gj["title"] = g.title.toStdString();
        json sigArr = json::array();
        for (const auto &s : g.sigList) {
            json sj;
            sj["canId"] = s.canId;
            sj["name"] = s.name.toStdString();
            sj["extended"] = s.extended;
            sigArr.push_back(sj);
        }
        gj["signals"] = sigArr;
        graphicArr.push_back(gj);
    }
    j["graphics"] = graphicArr;

    json recArr = json::array();
    for (const auto &f : st.recordFiles)
        recArr.push_back(f.toStdString());
    j["record"]["files"] = recArr;

    json tabArr = json::array();
    for (const auto &t : st.openTabs)
        tabArr.push_back(t.toStdString());
    j["tabs"]["open"] = tabArr;
    j["tabs"]["active"] = st.activeTab.toStdString();

    return j;
}

ProjectState ProjectManager::jsonToState(const json &j)
{
    ProjectState st;

    if (j.contains("name") && j["name"].is_string())
        st.name = QString::fromStdString(j["name"].get<std::string>());

    if (j.contains("source")) {
        const auto &src = j["source"];
        if (src.contains("mode")) st.sourceMode = src["mode"].get<int>();
        if (src.contains("filePath") && src["filePath"].is_string())
            st.filePath = QString::fromStdString(src["filePath"].get<std::string>());
        if (src.contains("baudrate")) st.baudrate = src["baudrate"].get<int>();
        if (src.contains("channel")) st.channel = src["channel"].get<int>();
    }

    if (j.contains("dbc") && j["dbc"].contains("files")) {
        for (const auto &f : j["dbc"]["files"])
            if (f.is_string())
                st.dbcFiles << QString::fromStdString(f.get<std::string>());
    }

    if (j.contains("traces") && j["traces"].is_array()) {
        for (const auto &t : j["traces"]) {
            ProjectTraceInstance ti;
            if (t.contains("id")) ti.id = QString::fromStdString(t["id"].get<std::string>());
            if (t.contains("title")) ti.title = QString::fromStdString(t["title"].get<std::string>());
            if (t.contains("filter") && t["filter"].is_string())
                ti.filterExpression = QString::fromStdString(t["filter"].get<std::string>());
            st.traces.append(ti);
        }
    }

    if (j.contains("graphics") && j["graphics"].is_array()) {
        for (const auto &g : j["graphics"]) {
            ProjectGraphicInstance gi;
            if (g.contains("id")) gi.id = QString::fromStdString(g["id"].get<std::string>());
            if (g.contains("title")) gi.title = QString::fromStdString(g["title"].get<std::string>());
            if (g.contains("signals") && g["signals"].is_array()) {
                for (const auto &s : g["signals"]) {
                    ProjectSigCfg sc;
                    if (s.contains("canId")) sc.canId = s["canId"].get<quint32>();
                    if (s.contains("name") && s["name"].is_string())
                        sc.name = QString::fromStdString(s["name"].get<std::string>());
                    if (s.contains("extended")) sc.extended = s["extended"].get<bool>();
                    gi.sigList.append(sc);
                }
            }
            st.graphics.append(gi);
        }
    }

    if (j.contains("record") && j["record"].contains("files")) {
        for (const auto &f : j["record"]["files"])
            if (f.is_string())
                st.recordFiles << QString::fromStdString(f.get<std::string>());
    }

    if (j.contains("tabs")) {
        const auto &tabs = j["tabs"];
        if (tabs.contains("open") && tabs["open"].is_array()) {
            for (const auto &t : tabs["open"])
                if (t.is_string())
                    st.openTabs << QString::fromStdString(t.get<std::string>());
        }
        if (tabs.contains("active") && tabs["active"].is_string())
            st.activeTab = QString::fromStdString(tabs["active"].get<std::string>());
    }

    return st;
}

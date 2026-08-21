#include "projectmanager.h"
#include "appconfig.h"
#include "sessionmanager.h"
#include "logging.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>

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
    QString now = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    m_state.meta.created = now;
    m_state.meta.modified = now;
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
        ResourceResolver resolver(filePath);
        m_state = jsonToState(j, resolver);
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

    // 更新修改时间戳
    m_state.meta.modified = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

    // 如果是新建工程且 created 为空，补上
    if (m_state.meta.created.isEmpty())
        m_state.meta.created = m_state.meta.modified;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::error("ProjectManager: 无法写入工程文件 {}", path.toStdString());
        return false;
    }

    ResourceResolver resolver(path);
    json j = stateToJson(m_state, resolver);
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
//  最近工程列表（委托给 SessionManager）
// ============================================================
QStringList ProjectManager::recentProjects() const
{
    return SessionManager::instance()->recentPaths();
}

void ProjectManager::addRecentProject(const QString &path)
{
    SessionManager::instance()->addRecent(path, "project");
    // 保留 AppConfig lastPath 设置，向后兼容
    AppConfig::instance()->set("project.lastPath", path);
}

void ProjectManager::clearRecent()
{
    SessionManager::instance()->clearRecent();
}

// ============================================================
//  序列化
// ============================================================
QString ProjectManager::toJsonString() const
{
    ResourceResolver resolver(m_filePath);
    return QString::fromStdString(stateToJson(m_state, resolver).dump(2));
}

bool ProjectManager::fromJsonString(const QString &jsonStr)
{
    try {
        json j = json::parse(jsonStr.toStdString());
        ResourceResolver resolver(m_filePath);
        m_state = jsonToState(j, resolver);
        m_modified = true;
        return true;
    } catch (const json::parse_error &e) {
        spdlog::error("ProjectManager: JSON 解析失败: {}", e.what());
        return false;
    }
}

// ============================================================
//  JSON <-> ProjectState 转换（v2 格式 + v1 向后兼容）
// ============================================================
json ProjectManager::stateToJson(const ProjectState &st,
                                const ResourceResolver &resolver)
{
    json j;
    j["name"] = st.name.toStdString();
    j["version"] = 2;

    // ---- meta（v2 新增）----
    j["meta"]["name"] = st.name.toStdString();
    j["meta"]["created"] = st.meta.created.toStdString();
    j["meta"]["modified"] = st.meta.modified.toStdString();
    j["meta"]["author"] = st.meta.author.toStdString();
    json tagArr = json::array();
    for (const auto &t : st.meta.tags)
        tagArr.push_back(t.toStdString());
    j["meta"]["tags"] = tagArr;
    j["meta"]["notes"] = st.meta.notes.toStdString();

    // ---- resources（v2 新增，相对路径）----
    json dbcRel = json::array();
    for (const auto &f : st.dbcFiles)
        dbcRel.push_back(resolver.relativize(f).toStdString());
    j["resources"]["dbc"] = dbcRel;

    json logRel = json::array();
    for (const auto &f : st.recordFiles)
        logRel.push_back(resolver.relativize(f).toStdString());
    j["resources"]["logs"] = logRel;

    json offRel = json::array();
    for (const auto &f : st.offlineFiles)
        offRel.push_back(resolver.relativize(f).toStdString());
    j["resources"]["offline"] = offRel;

    // ---- device（v2 新增）----
    j["device"]["type"] = st.deviceConfig.type.toStdString();
    j["device"]["channel"] = st.channel;
    j["device"]["baudrate"] = st.baudrate;
    j["device"]["fd"] = st.deviceConfig.fd;
    j["device"]["fdBaudrate"] = st.deviceConfig.fdBaudrate;

    // ---- 以下为 v1 兼容字段（绝对路径），旧版 openbus 仍可读取 ----
    j["source"]["mode"] = st.sourceMode;
    j["source"]["filePath"] = st.filePath.toStdString();
    j["source"]["baudrate"] = st.baudrate;
    j["source"]["channel"] = st.channel;

    json dbcArr = json::array();
    for (const auto &f : st.dbcFiles)
        dbcArr.push_back(f.toStdString());
    j["dbc"]["files"] = dbcArr;

    // ---- Trace / Graphic 实例（格式不变；M1 新增 protocolId/formId 身份字段）----
    json traceArr = json::array();
    for (const auto &t : st.traces) {
        json tj;
        tj["id"] = t.id.toStdString();
        tj["title"] = t.title.toStdString();
        tj["filter"] = t.filterExpression.toStdString();
        tj["protocolId"] = t.protocolId.toStdString();
        tj["formId"] = t.formId.toStdString();
        traceArr.push_back(tj);
    }
    j["traces"] = traceArr;

    json graphicArr = json::array();
    for (const auto &g : st.graphics) {
        json gj;
        gj["id"] = g.id.toStdString();
        gj["title"] = g.title.toStdString();
        gj["protocolId"] = g.protocolId.toStdString();
        gj["formId"] = g.formId.toStdString();
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

    // ---- 录制文件（v1 兼容，绝对路径）----
    json recArr = json::array();
    for (const auto &f : st.recordFiles)
        recArr.push_back(f.toStdString());
    j["record"]["files"] = recArr;

    // ---- 标签页 ----
    json tabArr = json::array();
    for (const auto &t : st.openTabs)
        tabArr.push_back(t.toStdString());
    j["tabs"]["open"] = tabArr;
    j["tabs"]["active"] = st.activeTab.toStdString();

    return j;
}

ProjectState ProjectManager::jsonToState(const json &j,
                                    const ResourceResolver &resolver)
{
    ProjectState st;

    // ---- 名称 ----
    if (j.contains("name") && j["name"].is_string())
        st.name = QString::fromStdString(j["name"].get<std::string>());

    // ---- 版本号 ----
    int version = 1;
    if (j.contains("version") && j["version"].is_number_integer())
        version = j["version"].get<int>();

    // ---- meta（v2 新增）----
    if (j.contains("meta")) {
        const auto &m = j["meta"];
        if (m.contains("created") && m["created"].is_string())
            st.meta.created = QString::fromStdString(m["created"].get<std::string>());
        if (m.contains("modified") && m["modified"].is_string())
            st.meta.modified = QString::fromStdString(m["modified"].get<std::string>());
        if (m.contains("author") && m["author"].is_string())
            st.meta.author = QString::fromStdString(m["author"].get<std::string>());
        if (m.contains("tags") && m["tags"].is_array()) {
            for (const auto &t : m["tags"])
                if (t.is_string())
                    st.meta.tags << QString::fromStdString(t.get<std::string>());
        }
        if (m.contains("notes") && m["notes"].is_string())
            st.meta.notes = QString::fromStdString(m["notes"].get<std::string>());
    }

    // ---- resources（v2 新增，相对路径 → 绝对路径）----
    if (version >= 2 && j.contains("resources")) {
        const auto &r = j["resources"];
        if (r.contains("dbc") && r["dbc"].is_array()) {
            for (const auto &f : r["dbc"])
                if (f.is_string())
                    st.dbcFiles << resolver.resolve(QString::fromStdString(f.get<std::string>()));
        }
        if (r.contains("logs") && r["logs"].is_array()) {
            for (const auto &f : r["logs"])
                if (f.is_string())
                    st.recordFiles << resolver.resolve(QString::fromStdString(f.get<std::string>()));
        }
        if (r.contains("offline") && r["offline"].is_array()) {
            for (const auto &f : r["offline"])
                if (f.is_string())
                    st.offlineFiles << resolver.resolve(QString::fromStdString(f.get<std::string>()));
        }
    }

    // ---- device（v2 新增）----
    if (version >= 2 && j.contains("device")) {
        const auto &d = j["device"];
        if (d.contains("type") && d["type"].is_string())
            st.deviceConfig.type = QString::fromStdString(d["type"].get<std::string>());
        if (d.contains("fd")) st.deviceConfig.fd = d["fd"].get<bool>();
        if (d.contains("fdBaudrate")) st.deviceConfig.fdBaudrate = d["fdBaudrate"].get<int>();
    }

    // ---- source（v1/v2 兼容，始终读取）----
    if (j.contains("source")) {
        const auto &src = j["source"];
        if (src.contains("mode")) st.sourceMode = src["mode"].get<int>();
        if (src.contains("filePath") && src["filePath"].is_string())
            st.filePath = QString::fromStdString(src["filePath"].get<std::string>());
        if (src.contains("baudrate")) st.baudrate = src["baudrate"].get<int>();
        if (src.contains("channel")) st.channel = src["channel"].get<int>();
    }

    // ---- v1 向后兼容：无 resources 时从旧字段读取绝对路径 ----
    if (st.dbcFiles.isEmpty() && j.contains("dbc") && j["dbc"].contains("files")) {
        for (const auto &f : j["dbc"]["files"])
            if (f.is_string())
                st.dbcFiles << QString::fromStdString(f.get<std::string>());
    }

    if (st.recordFiles.isEmpty() && j.contains("record") && j["record"].contains("files")) {
        for (const auto &f : j["record"]["files"])
            if (f.is_string())
                st.recordFiles << QString::fromStdString(f.get<std::string>());
    }

    // ---- Trace 实例 ----
    if (j.contains("traces") && j["traces"].is_array()) {
        for (const auto &t : j["traces"]) {
            ProjectTraceInstance ti;
            if (t.contains("id")) ti.id = QString::fromStdString(t["id"].get<std::string>());
            if (t.contains("title")) ti.title = QString::fromStdString(t["title"].get<std::string>());
            if (t.contains("filter") && t["filter"].is_string())
                ti.filterExpression = QString::fromStdString(t["filter"].get<std::string>());
            // M1 身份字段：旧工程缺省时经结构体默认值回填 can/framelist
            if (t.contains("protocolId") && t["protocolId"].is_string())
                ti.protocolId = QString::fromStdString(t["protocolId"].get<std::string>());
            if (t.contains("formId") && t["formId"].is_string())
                ti.formId = QString::fromStdString(t["formId"].get<std::string>());
            st.traces.append(ti);
        }
    }

    // ---- Graphic 实例 ----
    if (j.contains("graphics") && j["graphics"].is_array()) {
        for (const auto &g : j["graphics"]) {
            ProjectGraphicInstance gi;
            if (g.contains("id")) gi.id = QString::fromStdString(g["id"].get<std::string>());
            if (g.contains("title")) gi.title = QString::fromStdString(g["title"].get<std::string>());
            // M1 身份字段：旧工程缺省时经结构体默认值回填 can/waveform
            if (g.contains("protocolId") && g["protocolId"].is_string())
                gi.protocolId = QString::fromStdString(g["protocolId"].get<std::string>());
            if (g.contains("formId") && g["formId"].is_string())
                gi.formId = QString::fromStdString(g["formId"].get<std::string>());
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

    // ---- 标签页 ----
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

#include "projectmanager.h"
#include "appconfig.h"
#include "sessionmanager.h"
#include "logging.h"

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QMetaType>
#include <QVariantList>
#include <QByteArray>

namespace {

json variantToJson(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong:
        return v.toLongLong();
    case QMetaType::Double:
    case QMetaType::Float:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString().toStdString();
    case QMetaType::QStringList: {
        json arr = json::array();
        for (const QString &s : v.toStringList())
            arr.push_back(s.toStdString());
        return arr;
    }
    case QMetaType::QVariantList: {
        json arr = json::array();
        for (const QVariant &item : v.toList())
            arr.push_back(variantToJson(item));
        return arr;
    }
    case QMetaType::QVariantMap: {
        json obj = json::object();
        const QVariantMap m = v.toMap();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it)
            obj[it.key().toStdString()] = variantToJson(it.value());
        return obj;
    }
    default:
        return v.toString().toStdString();
    }
}

QVariant jsonToVariant(const json &j)
{
    if (j.is_boolean())
        return j.get<bool>();
    if (j.is_number_integer())
        return static_cast<qint64>(j.get<long long>());
    if (j.is_number_float())
        return j.get<double>();
    if (j.is_string())
        return QString::fromStdString(j.get<std::string>());
    if (j.is_array()) {
        QVariantList list;
        for (const auto &el : j)
            list << jsonToVariant(el);
        return list;
    }
    if (j.is_object()) {
        QVariantMap map;
        for (auto it = j.begin(); it != j.end(); ++it)
            map.insert(QString::fromStdString(it.key()), jsonToVariant(it.value()));
        return map;
    }
    return {};
}

} // namespace

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
    j["version"] = 4;

    // ---- meta ----
    j["meta"]["name"] = st.name.toStdString();
    j["meta"]["created"] = st.meta.created.toStdString();
    j["meta"]["modified"] = st.meta.modified.toStdString();
    j["meta"]["author"] = st.meta.author.toStdString();
    json tagArr = json::array();
    for (const auto &t : st.meta.tags)
        tagArr.push_back(t.toStdString());
    j["meta"]["tags"] = tagArr;
    j["meta"]["notes"] = st.meta.notes.toStdString();

    // ---- resources (relative paths) ----
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

    if (!st.filePath.isEmpty())
        j["resources"]["playback"] = resolver.relativize(st.filePath).toStdString();

    // ---- device ----
    j["device"]["type"] = st.deviceConfig.type.toStdString();
    j["device"]["kind"] = st.deviceConfig.kind;
    j["device"]["index"] = st.deviceConfig.index;
    j["device"]["subType"] = st.deviceConfig.subType;
    j["device"]["name"] = st.deviceConfig.name.toStdString();
    j["device"]["channel"] = st.channel;
    j["device"]["baudrate"] = st.baudrate;
    j["device"]["fd"] = st.deviceConfig.fd;
    j["device"]["fdBaudrate"] = st.deviceConfig.fdBaudrate;

    // ---- v1-compat source (absolute filePath kept for older readers) ----
    j["source"]["mode"] = st.sourceMode;
    j["source"]["filePath"] = st.filePath.toStdString();
    j["source"]["baudrate"] = st.baudrate;
    j["source"]["channel"] = st.channel;

    json dbcArr = json::array();
    for (const auto &f : st.dbcFiles)
        dbcArr.push_back(f.toStdString());
    j["dbc"]["files"] = dbcArr;

    // ---- Trace / Graphic ----
    json traceArr = json::array();
    for (const auto &t : st.traces) {
        json tj;
        tj["id"] = t.id.toStdString();
        tj["title"] = t.title.toStdString();
        tj["filter"] = t.filterExpression.toStdString();
        tj["protocolId"] = t.protocolId.toStdString();
        tj["formId"] = t.formId.toStdString();
        json rulesArr = json::array();
        for (const auto &rv : t.colorRules) {
            const QVariantMap r = rv.toMap();
            json rj;
            rj["expr"] = r.value(QStringLiteral("expr")).toString().toStdString();
            rj["background"] = r.value(QStringLiteral("background")).toString().toStdString();
            rj["foreground"] = r.value(QStringLiteral("foreground")).toString().toStdString();
            rj["enabled"] = r.value(QStringLiteral("enabled"), true).toBool();
            rulesArr.push_back(rj);
        }
        tj["colorRules"] = rulesArr;
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
            if (!s.color.isEmpty())
                sj["color"] = s.color.toStdString();
            sj["displayMode"] = s.displayMode;
            sigArr.push_back(sj);
        }
        gj["signals"] = sigArr;
        graphicArr.push_back(gj);
    }
    j["graphics"] = graphicArr;

    // ---- Watcher ----
    json watchArr = json::array();
    for (const auto &w : st.watchers) {
        json wj;
        wj["canId"] = w.canId;
        wj["name"] = w.name.toStdString();
        wj["messageName"] = w.messageName.toStdString();
        wj["extended"] = w.extended;
        watchArr.push_back(wj);
    }
    j["watchers"] = watchArr;

    // ---- Flow block enables ----
    json flowEn = json::object();
    for (auto it = st.flowBlockEnabled.constBegin();
         it != st.flowBlockEnabled.constEnd(); ++it)
        flowEn[it.key().toStdString()] = it.value();
    j["flow"]["blockEnabled"] = flowEn;
    json filterArr = json::array();
    for (const auto &r : st.flowFilterRules)
        filterArr.push_back(r.toStdString());
    j["flow"]["filterRules"] = filterArr;

    // ---- Record files (v1 absolute) + UI config ----
    json recArr = json::array();
    for (const auto &f : st.recordFiles)
        recArr.push_back(f.toStdString());
    j["record"]["files"] = recArr;
    if (!st.recordConfig.isEmpty()) {
        json cfg = json::object();
        for (auto it = st.recordConfig.constBegin();
             it != st.recordConfig.constEnd(); ++it) {
            const QVariant &v = it.value();
            switch (v.typeId()) {
            case QMetaType::Bool:
                cfg[it.key().toStdString()] = v.toBool();
                break;
            case QMetaType::Int:
            case QMetaType::LongLong:
                cfg[it.key().toStdString()] = v.toLongLong();
                break;
            case QMetaType::Double:
                cfg[it.key().toStdString()] = v.toDouble();
                break;
            default:
                cfg[it.key().toStdString()] = v.toString().toStdString();
                break;
            }
        }
        j["record"]["config"] = cfg;
    }

    // ---- Send entries ----
    json sendArr = json::array();
    for (const auto &ev : st.sendEntries) {
        const QVariantMap e = ev.toMap();
        json sj;
        sj["enabled"] = e.value(QStringLiteral("enabled")).toBool();
        sj["id"] = e.value(QStringLiteral("id")).toString().toStdString();
        sj["name"] = e.value(QStringLiteral("name")).toString().toStdString();
        sj["dlc"] = e.value(QStringLiteral("dlc")).toInt();
        sj["data"] = e.value(QStringLiteral("data")).toString().toStdString();
        sj["period"] = e.value(QStringLiteral("period")).toInt();
        sj["count"] = e.value(QStringLiteral("count")).toInt();
        sendArr.push_back(sj);
    }
    j["send"]["entries"] = sendArr;

    // ---- Playback UI config ----
    if (!st.playbackConfig.isEmpty()) {
        json cfg = json::object();
        for (auto it = st.playbackConfig.constBegin();
             it != st.playbackConfig.constEnd(); ++it) {
            const QString key = it.key();
            const QVariant &v = it.value();
            if (key == QStringLiteral("files") && v.canConvert<QStringList>()) {
                json files = json::array();
                for (const auto &f : v.toStringList())
                    files.push_back(resolver.relativize(f).toStdString());
                cfg["files"] = files;
            } else if (v.typeId() == QMetaType::Bool) {
                cfg[key.toStdString()] = v.toBool();
            } else if (v.typeId() == QMetaType::Int
                       || v.typeId() == QMetaType::LongLong) {
                cfg[key.toStdString()] = v.toLongLong();
            } else if (v.typeId() == QMetaType::Double
                       || v.typeId() == QMetaType::Float) {
                cfg[key.toStdString()] = v.toDouble();
            } else {
                cfg[key.toStdString()] = v.toString().toStdString();
            }
        }
        j["playback"]["config"] = cfg;
    }

    // ---- Tabs ----
    json tabArr = json::array();
    for (const auto &t : st.openTabs)
        tabArr.push_back(t.toStdString());
    j["tabs"]["open"] = tabArr;
    j["tabs"]["active"] = st.activeTab.toStdString();
    if (!st.editorLayout.isEmpty())
        j["tabs"]["layout"] = variantToJson(st.editorLayout);

    // ---- Window chrome (geometry + dock state) ----
    if (!st.windowGeometry.isEmpty())
        j["window"]["geometry"] = QString::fromLatin1(st.windowGeometry.toBase64()).toStdString();
    if (!st.windowState.isEmpty())
        j["window"]["state"] = QString::fromLatin1(st.windowState.toBase64()).toStdString();

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
        if (r.contains("playback") && r["playback"].is_string())
            st.filePath = resolver.resolve(
                QString::fromStdString(r["playback"].get<std::string>()));
    }

    // ---- device (v2+) ----
    if (version >= 2 && j.contains("device")) {
        const auto &d = j["device"];
        if (d.contains("type") && d["type"].is_string())
            st.deviceConfig.type = QString::fromStdString(d["type"].get<std::string>());
        if (d.contains("kind") && d["kind"].is_number_integer())
            st.deviceConfig.kind = d["kind"].get<int>();
        if (d.contains("index") && d["index"].is_number_integer())
            st.deviceConfig.index = d["index"].get<int>();
        if (d.contains("subType") && d["subType"].is_number_integer())
            st.deviceConfig.subType = d["subType"].get<int>();
        if (d.contains("name") && d["name"].is_string())
            st.deviceConfig.name = QString::fromStdString(d["name"].get<std::string>());
        if (d.contains("fd")) st.deviceConfig.fd = d["fd"].get<bool>();
        if (d.contains("fdBaudrate")) st.deviceConfig.fdBaudrate = d["fdBaudrate"].get<int>();
        // Legacy: type "devKindN" when kind missing
        if (st.deviceConfig.kind == 0 && st.deviceConfig.type.startsWith(QStringLiteral("devKind")))
            st.deviceConfig.kind = st.deviceConfig.type.mid(7).toInt();
    }

    // ---- source (v1/v2; filePath overridden by resources.playback when set) ----
    if (j.contains("source")) {
        const auto &src = j["source"];
        if (src.contains("mode")) st.sourceMode = src["mode"].get<int>();
        if (st.filePath.isEmpty() && src.contains("filePath") && src["filePath"].is_string()) {
            const QString fp = QString::fromStdString(src["filePath"].get<std::string>());
            // Prefer resolve when relative; absolute paths pass through
            st.filePath = QFileInfo(fp).isAbsolute() ? fp : resolver.resolve(fp);
        }
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

    // ---- Record UI config ----
    if (j.contains("record") && j["record"].contains("config")
        && j["record"]["config"].is_object()) {
        for (auto it = j["record"]["config"].begin();
             it != j["record"]["config"].end(); ++it) {
            const QString key = QString::fromStdString(it.key());
            const auto &v = it.value();
            if (v.is_boolean())
                st.recordConfig.insert(key, v.get<bool>());
            else if (v.is_number_integer())
                st.recordConfig.insert(key, static_cast<qint64>(v.get<long long>()));
            else if (v.is_number_float())
                st.recordConfig.insert(key, v.get<double>());
            else if (v.is_string())
                st.recordConfig.insert(key, QString::fromStdString(v.get<std::string>()));
        }
    }

    // ---- Send entries ----
    if (j.contains("send") && j["send"].contains("entries")
        && j["send"]["entries"].is_array()) {
        for (const auto &s : j["send"]["entries"]) {
            QVariantMap e;
            if (s.contains("enabled")) e.insert(QStringLiteral("enabled"), s["enabled"].get<bool>());
            if (s.contains("id") && s["id"].is_string())
                e.insert(QStringLiteral("id"),
                         QString::fromStdString(s["id"].get<std::string>()));
            if (s.contains("name") && s["name"].is_string())
                e.insert(QStringLiteral("name"),
                         QString::fromStdString(s["name"].get<std::string>()));
            if (s.contains("dlc")) e.insert(QStringLiteral("dlc"), s["dlc"].get<int>());
            if (s.contains("data") && s["data"].is_string())
                e.insert(QStringLiteral("data"),
                         QString::fromStdString(s["data"].get<std::string>()));
            if (s.contains("period")) e.insert(QStringLiteral("period"), s["period"].get<int>());
            if (s.contains("count")) e.insert(QStringLiteral("count"), s["count"].get<int>());
            st.sendEntries.append(e);
        }
    }

    // ---- Playback UI config ----
    if (j.contains("playback") && j["playback"].contains("config")
        && j["playback"]["config"].is_object()) {
        const auto &cfg = j["playback"]["config"];
        for (auto it = cfg.begin(); it != cfg.end(); ++it) {
            const QString key = QString::fromStdString(it.key());
            const auto &v = it.value();
            if (key == QStringLiteral("files") && v.is_array()) {
                QStringList files;
                for (const auto &f : v)
                    if (f.is_string())
                        files << resolver.resolve(
                            QString::fromStdString(f.get<std::string>()));
                st.playbackConfig.insert(key, files);
            } else if (v.is_boolean()) {
                st.playbackConfig.insert(key, v.get<bool>());
            } else if (v.is_number_integer()) {
                st.playbackConfig.insert(key, static_cast<qint64>(v.get<long long>()));
            } else if (v.is_number_float()) {
                st.playbackConfig.insert(key, v.get<double>());
            } else if (v.is_string()) {
                st.playbackConfig.insert(key,
                    QString::fromStdString(v.get<std::string>()));
            }
        }
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
            if (t.contains("colorRules") && t["colorRules"].is_array()) {
                ti.hasColorRules = true;
                for (const auto &r : t["colorRules"]) {
                    QVariantMap rm;
                    if (r.contains("expr") && r["expr"].is_string())
                        rm.insert(QStringLiteral("expr"),
                                  QString::fromStdString(r["expr"].get<std::string>()));
                    if (r.contains("background") && r["background"].is_string())
                        rm.insert(QStringLiteral("background"),
                                  QString::fromStdString(r["background"].get<std::string>()));
                    if (r.contains("foreground") && r["foreground"].is_string())
                        rm.insert(QStringLiteral("foreground"),
                                  QString::fromStdString(r["foreground"].get<std::string>()));
                    if (r.contains("enabled"))
                        rm.insert(QStringLiteral("enabled"), r["enabled"].get<bool>());
                    else
                        rm.insert(QStringLiteral("enabled"), true);
                    ti.colorRules.append(rm);
                }
            }
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
                    if (s.contains("color") && s["color"].is_string())
                        sc.color = QString::fromStdString(s["color"].get<std::string>());
                    if (s.contains("displayMode") && s["displayMode"].is_number_integer())
                        sc.displayMode = s["displayMode"].get<int>();
                    gi.sigList.append(sc);
                }
            }
            st.graphics.append(gi);
        }
    }

    // ---- Watcher ----
    if (j.contains("watchers") && j["watchers"].is_array()) {
        for (const auto &w : j["watchers"]) {
            ProjectWatcherEntry we;
            if (w.contains("canId")) we.canId = w["canId"].get<quint32>();
            if (w.contains("name") && w["name"].is_string())
                we.name = QString::fromStdString(w["name"].get<std::string>());
            if (w.contains("messageName") && w["messageName"].is_string())
                we.messageName = QString::fromStdString(w["messageName"].get<std::string>());
            if (w.contains("extended")) we.extended = w["extended"].get<bool>();
            st.watchers.append(we);
        }
    }

    // ---- Flow block enables ----
    if (j.contains("flow") && j["flow"].contains("blockEnabled")
        && j["flow"]["blockEnabled"].is_object()) {
        for (auto it = j["flow"]["blockEnabled"].begin();
             it != j["flow"]["blockEnabled"].end(); ++it) {
            if (it.value().is_boolean())
                st.flowBlockEnabled.insert(
                    QString::fromStdString(it.key()), it.value().get<bool>());
        }
    }
    if (j.contains("flow") && j["flow"].contains("filterRules")
        && j["flow"]["filterRules"].is_array()) {
        for (const auto &r : j["flow"]["filterRules"])
            if (r.is_string())
                st.flowFilterRules << QString::fromStdString(r.get<std::string>());
    }

    // ---- Tabs ----
    if (j.contains("tabs")) {
        const auto &tabs = j["tabs"];
        if (tabs.contains("open") && tabs["open"].is_array()) {
            for (const auto &t : tabs["open"])
                if (t.is_string())
                    st.openTabs << QString::fromStdString(t.get<std::string>());
        }
        if (tabs.contains("active") && tabs["active"].is_string())
            st.activeTab = QString::fromStdString(tabs["active"].get<std::string>());
        if (tabs.contains("layout") && tabs["layout"].is_object())
            st.editorLayout = jsonToVariant(tabs["layout"]).toMap();
    }

    if (j.contains("window")) {
        const auto &w = j["window"];
        if (w.contains("geometry") && w["geometry"].is_string())
            st.windowGeometry = QByteArray::fromBase64(
                QByteArray::fromStdString(w["geometry"].get<std::string>()));
        if (w.contains("state") && w["state"].is_string())
            st.windowState = QByteArray::fromBase64(
                QByteArray::fromStdString(w["state"].get<std::string>()));
    }

    return st;
}

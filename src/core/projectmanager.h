#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QVariantMap>
#include <QVariantList>
#include <QByteArray>
#include <nlohmann/json.hpp>
#include "resourceresolver.h"

using json = nlohmann::json;

/**
 * @brief Trace 实例配置
 *
 * M1 预埋（doc/flow.md §十三）：protocolId / formId 身份字段 —
 * 多协议 / 多形态（TR1-TR6）就绪前，旧工程反序列化经默认值
 * 自动回填为 CAN 帧列表。
 */
struct ProjectTraceInstance {
    QString id;
    QString title;
    QString filterExpression;
    /// Color rules: list of {expr, background, foreground, enabled}
    QVariantList colorRules;
    bool hasColorRules = false;  ///< true if project JSON contained colorRules
    QString protocolId = QStringLiteral("can");     ///< 协议身份（M2 ProtocolRegistry 键）
    QString formId = QStringLiteral("framelist");   ///< 形态身份（TR1；后续 TraceFormRegistry 键）
};

/**
 * @brief Graphic 信号配置
 */
struct ProjectSigCfg {
    quint32 canId = 0;
    QString name;
    bool extended = false;
    QString color;           // optional "#RRGGBB"
    int displayMode = 1;     // GraphicView::DisplayMode (default Step)
};

/**
 * @brief Graphic 实例配置
 *
 * M1 预埋（doc/flow.md §十三）：protocolId / formId 身份字段 —
 * 默认 CAN 时序波形，旧工程反序列化经默认值自动回填。
 */
struct ProjectGraphicInstance {
    QString id;
    QString title;
    QList<ProjectSigCfg> sigList;
    QString protocolId = QStringLiteral("can");     ///< 协议身份（M2 ProtocolRegistry 键）
    QString formId = QStringLiteral("waveform");    ///< 形态身份（GV1；后续 GraphicFormRegistry 键）
};

/**
 * @brief 工程元数据 — 标签、备注、时间戳（v2 新增）
 *
 * 存储在 .openbusproj 的 meta 节点中，不包含运行时路径信息。
 */
struct ProjectMeta {
    QString created;           // ISO 8601 创建时间
    QString modified;          // ISO 8601 最后修改时间
    QString author;
    QStringList tags;
    QString notes;
};

/**
 * @brief Device config — structured device parameters (v2+)
 */
struct ProjectDeviceConfig {
    QString type;              // legacy string e.g. "devKind1"
    int kind = 0;              // 0=simulator, 1=ZLG, 2=PEAK, ...
    int index = 0;             // device ordinal
    int subType = 0;           // vendor subtype (e.g. ZLG USBCANFD_200U)
    QString name;              // display name
    bool fd = false;
    int fdBaudrate = 2000000;
};

/**
 * @brief Watcher variable entry (project snapshot)
 */
struct ProjectWatcherEntry {
    quint32 canId = 0;
    QString name;
    QString messageName;
    bool extended = false;
};

/**
 * @brief Full project snapshot — restore desk so Start Measurement needs no re-setup
 *
 * v2: meta / deviceConfig. v3: watchers, flow block enables, richer device identity,
 * relativized playback path under resources.playback, record UI config, send entries.
 */
struct ProjectState {
    QString name;

    // Data source
    int sourceMode = 0;       // 0=Hardware, 1=File
    QString filePath;          // playback / Flow file path
    int baudrate = 500000;
    int channel = 1;

    // DBC
    QStringList dbcFiles;

    // Trace / Graphic instances
    QList<ProjectTraceInstance> traces;
    QList<ProjectGraphicInstance> graphics;

    // Watcher variables
    QList<ProjectWatcherEntry> watchers;

    // Flow block enable map (blockId -> enabled); empty = defaults
    QHash<QString, bool> flowBlockEnabled;
    // Filter block rule summaries (human-readable titles on the canvas)
    QStringList flowFilterRules;

    // Record / offline analysis files
    QStringList recordFiles;
    QStringList offlineFiles;

    // Record tab UI settings (directory/prefix/split/filter/trigger)
    QVariantMap recordConfig;
    // Signal-send table rows (enabled/id/name/dlc/data/period/count)
    QVariantList sendEntries;
    // Playback tab UI (speed/loop/files/channel/filter)
    QVariantMap playbackConfig;

    // Open tab order
    QStringList openTabs;
    QString activeTab;

    // Editor split / float layout (v4); window chrome geometry
    QVariantMap editorLayout;
    QByteArray windowGeometry;
    QByteArray windowState;

    ProjectMeta meta;
    ProjectDeviceConfig deviceConfig;
};

/**
 * @brief 工程管理器 — 单例，管理工程的新建/加载/保存/切换
 *
 * 仅负责 JSON 序列化和文件 I/O。
 * 状态收集与恢复由 MainWindow 负责，通过 currentState() 读写。
 */
class ProjectManager : public QObject
{
    Q_OBJECT

public:
    static ProjectManager *instance();

    const ProjectState &currentState() const { return m_state; }
    ProjectState &currentStateRef() { return m_state; }
    bool isModified() const { return m_modified; }
    QString currentFilePath() const { return m_filePath; }
    QString currentProjectName() const { return m_state.name; }

    void newProject(const QString &name);
    bool loadProject(const QString &filePath);
    bool saveProject(const QString &filePath = QString());
    bool saveAs(const QString &filePath);

    QStringList recentProjects() const;
    void addRecentProject(const QString &path);
    void clearRecent();

    QString toJsonString() const;
    bool fromJsonString(const QString &jsonStr);

signals:
    void projectLoaded(const QString &name);
    void projectSaved(const QString &filePath);
    void stateModified();

private:
    ProjectManager(QObject *parent = nullptr);

    ProjectState m_state;
    QString m_filePath;
    bool m_modified = false;

    static json stateToJson(const ProjectState &st,
                            const ResourceResolver &resolver = ResourceResolver(QString()));
    static ProjectState jsonToState(const json &j,
                                    const ResourceResolver &resolver = ResourceResolver(QString()));
};

#endif // PROJECTMANAGER_H

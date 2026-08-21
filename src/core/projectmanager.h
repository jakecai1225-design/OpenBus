#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
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
 * @brief 设备配置 — 结构化设备参数（v2 新增）
 *
 * channel / baudrate 仍保留在 ProjectState 顶层，此处仅存放新增字段。
 */
struct ProjectDeviceConfig {
    QString type;              // "USBCANFD_200U" 等
    bool fd = false;
    int fdBaudrate = 2000000;
};

/**
 * @brief 工程状态数据结构 — 描述一个工程的完整现场
 *
 * v2 新增 meta / deviceConfig 字段，其余字段保持兼容。
 * 外部资源路径在运行时使用绝对路径；序列化时通过 ResourceResolver
 * 转为相对路径写入 JSON resources 节点。
 */
struct ProjectState {
    QString name;

    // 数据源
    int sourceMode = 0;       // 0=Hardware, 1=File
    QString filePath;          // 回放文件路径
    int baudrate = 500000;
    int channel = 1;

    // DBC
    QStringList dbcFiles;

    // Trace / Graphic 实例
    QList<ProjectTraceInstance> traces;
    QList<ProjectGraphicInstance> graphics;

    // 录制文件
    QStringList recordFiles;

    // 离线分析文件（离线分析页加载的报文文件列表）
    QStringList offlineFiles;

    // 打开的标签页顺序
    QStringList openTabs;
    QString activeTab;

    // ---- v2 新增字段 ----
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

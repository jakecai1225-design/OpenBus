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
 */
struct ProjectTraceInstance {
    QString id;
    QString title;
    QString filterExpression;
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
 */
struct ProjectGraphicInstance {
    QString id;
    QString title;
    QList<ProjectSigCfg> sigList;
};

/**
 * @brief 工程元数据 — 标签、备注、时间戳（v2 新增）
 *
 * 存储在 .sinproj 的 meta 节点中，不包含运行时路径信息。
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

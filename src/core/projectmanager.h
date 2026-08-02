#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <nlohmann/json.hpp>

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
 * @brief 工程状态数据结构 — 描述一个工程的完整现场
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

    static json stateToJson(const ProjectState &st);
    static ProjectState jsonToState(const json &j);
};

#endif // PROJECTMANAGER_H

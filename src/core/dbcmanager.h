#ifndef DBCMANAGER_H
#define DBCMANAGER_H

#include <QObject>
#include <QList>
#include "dbcdata.h"

/**
 * @brief DBC 文件管理器
 *
 * 管理已加载的 DBC 文件，提供信号查询。
 * 完整解析器，支持 BO_/SG_/BU_/CM_/BA_/VAL_/VAL_TABLE_ 等 DBC 段。
 */
class DbcManager : public QObject
{
    Q_OBJECT

public:
    explicit DbcManager(QObject *parent = nullptr);

    /// 加载 DBC 文件
    bool loadDbc(const QString &filePath);

    /// 卸载 DBC 文件
    void unloadDbc(const QString &filePath);

    /// 获取所有已加载的 DBC 文件
    const QList<DbcFile> &files() const { return m_files; }

    /// 根据 CAN ID 查找报文定义（跨所有 DBC 文件）
    const DbcMessage *findMessage(quint32 id) const;

    /// 根据 CAN ID + 信号名查找信号定义
    const DbcSignal *findSignal(quint32 id, const QString &signalName) const;

    /// 获取所有报文（跨所有 DBC）
    QList<const DbcMessage *> allMessages() const;

    /// 查找指定文件名的 DbcFile
    const DbcFile *findFile(const QString &fileName) const;

signals:
    void dbcLoaded(const QString &fileName);
    void dbcUnloaded(const QString &fileName);

private:
    QList<DbcFile> m_files;

    /// 完整 DBC 文件解析（支持 BU_/CM_/BA_/VAL_/VAL_TABLE_ 等全部段）
    bool parseDbc(const QString &filePath, DbcFile &out);

    /// 解析后处理：关联节点收发关系、应用属性值
    void postProcess(DbcFile &file);
};

#endif // DBCMANAGER_H

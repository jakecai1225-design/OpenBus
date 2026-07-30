#ifndef DBCMANAGER_H
#define DBCMANAGER_H

#include <QObject>
#include <QList>
#include <QHash>
#include "dbcdata.h"

/**
 * @brief DBC 文件管理器
 *
 * 管理已加载的 DBC 文件，提供信号查询。
 * 完整解析器，支持 BO_/SG_/BU_/CM_/BA_/VAL_/VAL_TABLE_ 等 DBC 段。
 * 内部维护 CAN ID → Message 哈希索引，O(1) 查找。
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

    /// 根据 CAN ID 查找报文定义（跨所有 DBC 文件，O(1) 哈希查找）
    const DbcMessage *findMessage(quint32 id) const;

    /// 根据 CAN ID + 信号名查找信号定义
    const DbcSignal *findSignal(quint32 id, const QString &signalName) const;

    /// 获取所有报文（跨所有 DBC）
    QList<const DbcMessage *> allMessages() const;

    /// 查找指定文件名的 DbcFile
    const DbcFile *findFile(const QString &fileName) const;

    /// 解码单帧所有信号 — 批量返回信号名+物理值+单位+值描述
    struct DecodedSignal {
        QString name;
        double physValue = 0.0;
        QString unit;
        QString valueDesc;   // 值表描述（如有）
        QString comment;      // 信号注释（如有）
    };
    QVector<DecodedSignal> decodeFrame(quint32 canId, const QByteArray &data) const;

    /// 解码单个信号
    bool decodeSignal(quint32 canId, const QString &sigName,
                      const QByteArray &data, double &outValue) const;

signals:
    void dbcLoaded(const QString &fileName);
    void dbcUnloaded(const QString &fileName);

private:
    QList<DbcFile> m_files;

    /// CAN ID → DbcMessage* 哈希索引（O(1) 查找）
    QHash<quint32, const DbcMessage *> m_msgIndex;

    /// 重建索引（loadDbc / unloadDbc 后调用）
    void rebuildIndex();
};

#endif // DBCMANAGER_H

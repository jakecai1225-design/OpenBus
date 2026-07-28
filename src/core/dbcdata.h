#ifndef DBCDATA_H
#define DBCDATA_H

#include <QString>
#include <QList>
#include <QHash>
#include <QMetaType>

/**
 * @brief DBC 信号定义
 */
struct DbcSignal
{
    QString name;
    int startBit = 0;
    int bitLength = 1;
    bool littleEndian = true;   // Intel = true, Motorola = false
    bool isSigned = false;
    double factor = 1.0;
    double offset = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;
    QString receiver;
    QString comment;

    /// 从原始数据中解码信号值
    double decode(const QByteArray &data) const;
};

/**
 * @brief DBC 报文定义
 */
struct DbcMessage
{
    quint32 id = 0;
    QString name;
    int dlc = 8;
    QString sender;
    QList<DbcSignal> signalList;
    QString comment;

    const DbcSignal *findSignal(const QString &name) const
    {
        for (const auto &s : signalList)
            if (s.name == name) return &s;
        return nullptr;
    }
};

/**
 * @brief DBC 文件（一个 DBC 可包含多个报文）
 */
struct DbcFile
{
    QString filePath;
    QString fileName;
    QString version;
    QList<DbcMessage> messages;

    const DbcMessage *findMessage(quint32 id) const
    {
        for (const auto &m : messages)
            if (m.id == id) return &m;
        return nullptr;
    }
};

#endif // DBCDATA_H

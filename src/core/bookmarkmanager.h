#ifndef BOOKMARKMANAGER_H
#define BOOKMARKMANAGER_H

#include <QObject>
#include <QVector>
#include <QColor>

/**
 * @brief 书签管理器（对标 CANoe Trace Bookmarks）
 *
 * 管理报文行书签：{frameIndex, note, timestamp, color}
 * 书签保存在 .sbm 文件中（JSON 格式），也可嵌入 .sinproj 工程文件。
 */
class BookmarkManager : public QObject
{
    Q_OBJECT

public:
    struct Bookmark {
        int frameIndex = 0;       ///< 帧序号
        QString note;             ///< 备注
        double timestamp = 0.0;   ///< 帧时间戳
        QColor color;             ///< 书签颜色
    };

    explicit BookmarkManager(QObject *parent = nullptr);

    bool loadFromFile(const QString &filePath);
    bool saveToFile(const QString &filePath) const;

    void addBookmark(int frameIndex, const QString &note, double timestamp,
                     const QColor &color = QColor(0xFF, 0xEB, 0x3B));
    void removeBookmark(int frameIndex);
    void clear();

    const QVector<Bookmark> &bookmarks() const { return m_bookmarks; }

    /// 查找指定帧序号的书签
    int findBookmark(int frameIndex) const;

signals:
    void bookmarkAdded(const Bookmark &bm);
    void bookmarkRemoved(int frameIndex);
    void cleared();

private:
    QVector<Bookmark> m_bookmarks;
};

#endif // BOOKMARKMANAGER_H

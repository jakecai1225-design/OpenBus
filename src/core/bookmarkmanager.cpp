#include "bookmarkmanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

BookmarkManager::BookmarkManager(QObject *parent)
    : QObject(parent)
{
}

bool BookmarkManager::loadFromFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray arr = root.value("bookmarks").toArray();

    m_bookmarks.clear();
    for (const auto &item : arr) {
        QJsonObject obj = item.toObject();
        Bookmark bm;
        bm.frameIndex = obj.value("frameIndex").toInt();
        bm.note = obj.value("note").toString();
        bm.timestamp = obj.value("timestamp").toDouble();
        QString colorName = obj.value("color").toString();
        if (!colorName.isEmpty())
            bm.color = QColor(colorName);
        m_bookmarks.append(bm);
    }

    return true;
}

bool BookmarkManager::saveToFile(const QString &filePath) const
{
    QJsonObject root;
    root["version"] = 1;

    QJsonArray arr;
    for (const auto &bm : m_bookmarks) {
        QJsonObject obj;
        obj["frameIndex"] = bm.frameIndex;
        obj["note"] = bm.note;
        obj["timestamp"] = bm.timestamp;
        obj["color"] = bm.color.name();
        arr.append(obj);
    }
    root["bookmarks"] = arr;

    QJsonDocument doc(root);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void BookmarkManager::addBookmark(int frameIndex, const QString &note,
                                   double timestamp, const QColor &color)
{
    // 如果已存在同帧序号的书签，更新
    for (auto &bm : m_bookmarks) {
        if (bm.frameIndex == frameIndex) {
            bm.note = note;
            bm.timestamp = timestamp;
            bm.color = color;
            emit bookmarkAdded(bm);
            return;
        }
    }

    Bookmark bm;
    bm.frameIndex = frameIndex;
    bm.note = note;
    bm.timestamp = timestamp;
    bm.color = color;
    m_bookmarks.append(bm);
    emit bookmarkAdded(bm);
}

void BookmarkManager::removeBookmark(int frameIndex)
{
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks[i].frameIndex == frameIndex) {
            m_bookmarks.removeAt(i);
            emit bookmarkRemoved(frameIndex);
            return;
        }
    }
}

void BookmarkManager::clear()
{
    m_bookmarks.clear();
    emit cleared();
}

int BookmarkManager::findBookmark(int frameIndex) const
{
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks[i].frameIndex == frameIndex)
            return i;
    }
    return -1;
}

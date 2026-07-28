#ifndef RIGHTPANEL_H
#define RIGHTPANEL_H

#include <QTabWidget>

class QTableWidget;
class QPlainTextEdit;
class QLabel;

/**
 * @brief 右侧辅助面板
 *
 * 标签页：Properties（属性）/ Statistics（统计）/ Bookmarks（书签）
 */
class RightPanel : public QTabWidget
{
    Q_OBJECT

public:
    explicit RightPanel(QWidget *parent = nullptr);

public slots:
    /// 设置选中帧的属性
    void setFrameProperties(const QString &time, const QString &channel,
                            const QString &direction, const QString &id,
                            const QString &dlc, const QString &data,
                            const QString &flags);

    /// 更新统计信息
    void updateStatistics(int totalFrames, int rxCount, int txCount,
                          int canFdCount, int extCount, double busLoad);

    /// 清除所有
    void clearAll();

private:
    QTableWidget *m_propTable;
    QTableWidget *m_statTable;
    QPlainTextEdit *m_bookmarks;
};

#endif // RIGHTPANEL_H

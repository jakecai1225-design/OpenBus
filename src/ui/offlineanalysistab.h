#ifndef OFFLINEANALYSISTAB_H
#define OFFLINEANALYSISTAB_H

#include <QWidget>
#include <QQueue>
#include <QTimer>
#include <QStringList>

class QPushButton;
class QLabel;
class QTableWidget;
class QDragEnterEvent;
class QDropEvent;

/**
 * @brief 离线分析标签页 — 纯文件列表管理 + 信息解析
 *
 * 只负责：添加/删除文件、上移/下移排序、异步解析帧数/时长/大小。
 * 不负责加载文件到 Player — 由 Flow 界面点击“开始”时统一加载所有文件。
 */
class OfflineAnalysisTab : public QWidget
{
    Q_OBJECT

public:
    explicit OfflineAnalysisTab(QWidget *parent = nullptr);

    /// 返回列表中所有文件的完整路径（按顺序）
    QStringList filePaths() const;
    /// 列表是否为空
    bool isEmpty() const { return m_fileList->rowCount() == 0; }

    /// 追加文件到列表（不经文件对话框；工程恢复用）
    void addFiles(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onAddFile();
    void onRemoveFile();
    void onMoveUp();
    void onMoveDown();
    void onParseTimer();

private:
    void parseFileInfo(int row);
    void renumberRows();

    QTableWidget *m_fileList;
    QPushButton *m_addFileBtn;
    QPushButton *m_removeFileBtn;
    QPushButton *m_moveUpBtn;
    QPushButton *m_moveDownBtn;
    QLabel *m_statusLabel;
    QLabel *m_dropHint;  // 💡 拖放提示标签

    QTimer *m_parseTimer;
    QQueue<int> m_parseQueue;
};

#endif // OFFLINEANALYSISTAB_H

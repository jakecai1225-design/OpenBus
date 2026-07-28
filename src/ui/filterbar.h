#ifndef FILTERBAR_H
#define FILTERBAR_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QToolButton;
class QLabel;

/**
 * @brief Wireshark 风格显示过滤栏
 *
 * 左侧有 Start/Stop 按钮控制本标签页的数据采集，
 * 右侧为过滤输入框。
 */
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);

    QString filterText() const;
    bool filterActive() const;

    void setRunning(bool running);

signals:
    void filterApplied(const QString &filter);
    void filterCleared();
    void startRequested();
    void stopRequested();

private slots:
    void onApply();
    void onClear();
    void showHelp();
    void onTextChanged();
    void onStart();
    void onStop();

private:
    QLineEdit *m_edit;
    QPushButton *m_applyBtn;
    QPushButton *m_clearBtn;
    QToolButton *m_helpBtn;
    QLabel *m_statusIcon;
    QPushButton *m_startBtn;
    QPushButton *m_stopBtn;
};

#endif // FILTERBAR_H

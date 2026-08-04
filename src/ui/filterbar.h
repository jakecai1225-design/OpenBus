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
 * 左侧为覆盖模式切换，右侧为过滤输入框。
 * 开始/停止由 flow 标签页全局控制，本组件不再包含独立按钮。
 */
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);

    QString filterText() const;
    bool filterActive() const;

    void setOverwriteMode(bool enabled);

signals:
    void filterApplied(const QString &filter);
    void filterCleared();
    void overwriteModeToggled(bool enabled);

private slots:
    void onApply();
    void onClear();
    void showHelp();
    void onTextChanged();

private:
    QLineEdit *m_edit;
    QPushButton *m_applyBtn;
    QPushButton *m_clearBtn;
    QToolButton *m_helpBtn;
    QLabel *m_statusIcon;
    QPushButton *m_overwriteBtn;
};

#endif // FILTERBAR_H

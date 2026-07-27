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
 * 输入过滤表达式后按 Enter 或点击 Apply 应用过滤，
 * 支持语法检查、错误提示、帮助。
 */
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);

    QString filterText() const;
    bool filterActive() const;

signals:
    void filterApplied(const QString &filter);
    void filterCleared();

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
};

#endif // FILTERBAR_H

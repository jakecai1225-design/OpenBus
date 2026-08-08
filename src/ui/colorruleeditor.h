#ifndef COLORRULEEDITOR_H
#define COLORRULEEDITOR_H

#include <QDialog>
#include <QVector>
#include <QColor>

class QListWidget;
class QLineEdit;
class QPushButton;
class QCheckBox;

/**
 * @brief 着色规则编辑器（对标 CANoe Trace 着色规则）
 *
 * 规则列表：每条规则 = 条件表达式 + 背景色 + 前景色
 * 条件复用 FilterEngine（如 id == 0x123 → 黄色背景）
 * 规则优先级：从上到下匹配，首个命中规则的着色生效
 */
class ColorRuleEditor : public QDialog
{
    Q_OBJECT

public:
    struct ColorRule {
        QString expr;         ///< 条件表达式 (FilterEngine 语法)
        QColor background;    ///< 背景色
        QColor foreground;    ///< 前景色
        bool enabled = true;  ///< 是否启用
    };

    explicit ColorRuleEditor(QWidget *parent = nullptr);

    void setRules(const QVector<ColorRule> &rules);
    QVector<ColorRule> rules() const;

private slots:
    void onAddRule();
    void onRemoveRule();
    void onMoveUp();
    void onMoveDown();
    void onSelectionChanged();

private:
    QListWidget *m_listWidget;
    QLineEdit *m_exprEdit;
    QPushButton *m_bgColorBtn;
    QPushButton *m_fgColorBtn;
    QCheckBox *m_enabledChk;

    QColor m_currentBg;
    QColor m_currentFg;

    void updateListWidget();
    void applyCurrentEdit();
};

#endif // COLORRULEEDITOR_H

#ifndef COMMANDCENTER_H
#define COMMANDCENTER_H

#include <QFrame>

class QLabel;
class QToolButton;

/**
 * @brief VS Code title-bar Command Center pill (centered in the menu bar).
 * Click opens the CommandPalette.
 */
class CommandCenter : public QFrame
{
    Q_OBJECT

public:
    explicit CommandCenter(QWidget *parent = nullptr);

    void setPlaceholder(const QString &text);
    void setQueryPreview(const QString &text);

signals:
    void activated();
    void navigateBack();
    void navigateForward();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_fwdBtn = nullptr;
    QLabel *m_label = nullptr;
    QString m_placeholder;
};

#endif // COMMANDCENTER_H

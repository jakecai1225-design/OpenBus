#ifndef ACTIVITYBAR_H
#define ACTIVITYBAR_H

#include <QWidget>
#include <QList>

class QToolButton;

/**
 * @brief VS Code 风格左侧活动栏
 *
 * 窄竖条（48px），放置功能图标按钮。
 * 点击切换 SideBar 显示的面板，再次点击同一按钮可隐藏 SideBar。
 */
class ActivityBar : public QWidget
{
    Q_OBJECT

public:
    enum Activity {
        None = -1,
        Project = 0,
        Trace,
        Graphic,
        Dbc,
        Playback,
        Record,
        Device,
        Settings
    };

    explicit ActivityBar(QWidget *parent = nullptr);

    Activity currentActivity() const { return m_current; }

signals:
    void activityChanged(int activity);
    void activityToggled(int activity); // 同一按钮再次点击

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onButtonClicked();

private:
    struct BtnInfo {
        QToolButton *btn;
        Activity activity;
        QString text;
        QString tooltip;
    };

    QList<BtnInfo> m_buttons;
    Activity m_current = None;

    QToolButton *createButton(const QString &text, const QString &tooltip,
                              Activity act, bool atBottom = false);
};

#endif // ACTIVITYBAR_H

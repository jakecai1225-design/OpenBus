#ifndef WELCOMEPAGE_H
#define WELCOMEPAGE_H

#include <QPixmap>
#include <QWidget>

class QLabel;
class QPaintEvent;
class QVBoxLayout;
class QFrame;
class QPushButton;

/**
 * @brief VS Code-style Welcome editor tab
 *
 * Sections: hero, Start actions, Recent projects, Help links, walkthrough tips.
 * Shell owns a single instance; closed tab destroys the widget (like ShortcutsPage).
 */
class WelcomePage : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomePage(QWidget *parent = nullptr);

    /// Reload recent project list from SessionManager
    void refreshRecent();

signals:
    void newProjectRequested();
    void openProjectRequested();
    void openRecentRequested(const QString &path);
    void openDeviceRequested();
    void openFlowRequested();
    void openMarketRequested();
    void openTraceRequested();
    void openGraphicRequested();
    void openDbcPanelRequested();
    void openShortcutsRequested();
    void openAboutRequested();
    void openReleaseNotesRequested();
    void openDocsRequested();
    void clearRecentRequested();

private:
    void setupUi();
    void applyTheme();
    QFrame *makeSection(const QString &title, QWidget *body);
    QPushButton *makeLinkButton(const QString &text, const QString &tip = {});

    QVBoxLayout *m_recentLayout = nullptr;
    QLabel *m_recentEmpty = nullptr;
    QLabel *m_heroTitle = nullptr;
    QLabel *m_heroSub = nullptr;
    QLabel *m_versionLabel = nullptr;
    QPixmap m_watermark;

    void rebuildWatermark();

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // WELCOMEPAGE_H

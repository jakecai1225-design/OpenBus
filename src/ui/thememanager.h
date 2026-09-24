#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QStringList>

struct Theme
{
    QString name;

    // Core backgrounds
    QString windowBg;    // Main window / dialog background
    QString contentBg;   // Editor / table / content area
    QString sidebarBg;   // Side / bottom / right chrome — match windowBg
    QString panelBg;     // Idle panel chrome — match windowBg

    // Menu & Activity bar
    QString barBg;       // Menu bar background
    QString barFg;       // Menu bar text
    QString barHover;    // Menu bar hover
    QString barBorder;   // Menu border

    // Activity bar
    QString activityBarBg;     // Activity bar background
    QString activityBarFg;     // Activity bar icon/text color
    QString activityBarHover;  // Activity bar hover

    // Text
    QString text;
    QString textDim;

    // Accent
    QString accent;
    QString accentHover;
    QString accentBorder;

    // Borders
    QString border;
    QString borderDim;

    // Selection & hover
    QString selectionBg;
    QString hoverBg;

    // Buttons
    QString buttonBg;
    QString buttonHover;
    QString buttonPress;
    QString buttonDisabledBg;
    QString buttonDisabledText;

    // Status bar
    QString statusBg;
    QString statusFg;

    // Terminal
    QString terminalBg;
    QString terminalFg;

    // Tabs
    QString tabBg;
    QString tabActiveBg;
    QString tabHoverBg;

    // Scrollbar
    QString scrollBg;
    QString scrollHandle;
    QString scrollHandleHover;

    // Close button
    QString closeBtnHover;
    QString closeBtnPress;

    // Table header
    QString headerBg;
    QString headerHover;

    // Alternate row
    QString altRowBg;
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager *instance();

    QStringList themeNames() const;
    QString currentThemeName() const { return m_currentName; }
    const Theme &currentTheme() const;
    void applyTheme(const QString &name);

    QString generateQss(const Theme &t) const;

signals:
    void themeChanged(const QString &name);

private:
    ThemeManager(QObject *parent = nullptr);

    QList<QPair<QString, Theme>> m_themes;
    QString m_currentName;

    void initThemes();
};

#endif // THEMEMANAGER_H

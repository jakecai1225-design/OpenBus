#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QObject>
#include <QStringList>

struct Theme
{
    QString name;

    // Core backgrounds
    QString windowBg;        // Main window / dialog background (#1e1e1e / #ffffff)
    QString contentBg;       // Editor / table / content area (#1e1e1e / #ffffff)
    QString sidebarBg;       // Sidebar list / tree background (#252526 / #f3f3f3)
    QString panelBg;         // Panel title / toolbar / tab bar background (#1e1e1e / #ffffff)

    // Menu & Title bar (integrated title bar)
    QString titleBarActiveBg;      // Active title bar (#007acc / #007acc)
    QString titleBarInactiveBg;    // Inactive title bar (#454545 / #dddddd)
    QString titleBarActiveFg;      // Active foreground (#ffffff / #ffffff)
    QString titleBarInactiveFg;    // Inactive foreground (#bbbbbb / #666666)
    QString titleBarFocusBorder;   // Focus border (#007acc / #007acc)
    QString barBg;                 // Menu bar background (same as titleBar)
    QString barFg;                 // Menu bar text
    QString barHover;              // Menu bar hover
    QString barBorder;             // Menu border

    // Activity bar (48px left dock)
    QString activityBarBg;     // Activity bar background (#333333 / #272727)
    QString activityBarFg;     // Activity bar icon/text color (#ffffff / #ffffff)
    QString activityBarBorder; // Left/right border (#333333 / #272727)
    QString activityBarHover;  // Activity bar hover (#ffffff / #ffffff)
    QString activityBarBadgeBg;// Activity bar notification badge (#007acc / #007acc)
    QString activityBarBadgeFg;// Badge foreground (#ffffff / #ffffff)
    QString activityBarDropBorder; // Drag drop border (#d7ba7d)

    // SideBar (collapsible panel)
    QString sideBarForeground; // SideBar text (#cccccc / #5f6368)
    QString sideBarTitleFg;    // SideBar title (#bbbbbb / #5f6368)
    QString sideBarBorder;     // SideBar right border (#2b2b2b / #e7e7e7)
    QString sideBarDropBorder; // Drag drop border (#d7ba7d)

    // Text
    QString text;          // Main text (#cccccc / #333333)
    QString textDim;       // Dimmed text (#858585 / #6c6c6c)
    QString errorFg;       // Error foreground (#f48771 / #e81721)
    QString linkFg;        // Link text color (#3794ff / #0066b8)
    QString iconFg;        // Icon color (#cccccc / #333333)

    // Accent
    QString accent;        // Primary accent (#007acc / #007acc)
    QString accentHover;   // Hover accent (#005fa3 / #005fa3)
    QString accentBorder;  // Accent border (#007acc / #0066b8)

    // Borders
    QString border;        // Default border (#2b2b2b / #e4e4e4)
    QString borderDim;     // Dimmed border (#424242 / #e7e7e7)

    // Selection & hover
    QString selectionBg;   // Selected items (#264f78 / #add6ff80)
    QString hoverBg;       // Hover background (#2a2d2d / #f0f0f0)
    QString altRowBg;      // Alternate row in tables (#1a1a1a / #fafafa)

    // Buttons
    QString buttonBg;        // Default button (#0e63bb / #0e63bb)
    QString buttonHover;     // Button hover (#0d5fae / #0b5a8f)
    QString buttonPress;     // Button pressed
    QString buttonSecondaryBg;   // Secondary button (#333333 / #f0f0f0)
    QString buttonSecondaryFg;   // Secondary button fg (#cccccc / #333333)
    QString buttonDisabledBg;  // Disabled button (#3c3c3c / #dadada)
    QString buttonDisabledText;// Disabled text (#858585 / #858585)

    // Checkbox & Dropdown
    QString checkboxBg;        // Checkbox background (#2b2b2b / #fafafa)
    QString checkboxFg;        // Checkbox foreground (#cccccc / #333333)
    QString checkboxBorder;    // Checkbox border (#1b1b1b / #c8c8c8)
    QString dropdownBg;        // Dropdown background (#2b2b2b / #fafafa)
    QString dropdownFg;        // Dropdown foreground (#cccccc / #333333)
    QString dropdownBorder;    // Dropdown border (#1b1b1b / #c8c8c8)

    // Input
    QString inputBg;           // Input background (#3c3c3c / #fcfcfc)
    QString inputFg;           // Input foreground (#cccccc / #333333)
    QString inputPlaceholderFg;// Placeholder text (#aaaaaa / #999999)
    QString inputBorder;       // Input border (#3c3c3c / #c8c8c8)

    // Status bar
    QString statusBg;          // Status bar background (#007acc / #007acc)
    QString statusFg;          // Status bar foreground (#ffffff / #ffffff)
    QString statusBarBorder;   // Status bar border (#007acc / #007acc)
    QString statusBarItemHoverBg;// Item hover (#005a9e80 / #005fa380)
    QString statusBarItemRemoteBg;// Remote indicator (#ce9178 / #ce9178)

    // Terminal
    QString terminalBg;        // Terminal background (#1e1e1e / #ffffff)
    QString terminalFg;        // Terminal foreground (#cccccc / #333333)
    QString terminalCursorFg;  // Cursor foreground (#ffffff / #000000)
    QString terminalCursorBg;  // Cursor background (#aca1ee / #aca1ee)

    // Tabs
    QString tabBg;             // Tab bar background (#1e1e1e / #ffffff)
    QString tabActiveBg;       // Active tab (#1e1e1e / #ffffff)
    QString tabInactiveBg;     // Inactive tab (#2d2d2d / #ececec)
    QString tabActiveFg;       // Active tab text (#ffffff / #333333)
    QString tabInactiveFg;     // Inactive tab text (#9c9c9c / #666666)
    QString tabHoverBg;        // Hover background (#2d2d2d / #ececec)
    QString tabBorder;         // Tab separator (#1e1e1e / #e7e7e7)
    QString tabActiveBorderTop;// Active top border (#007acc / #007acc)
    QString editorGroupHeaderBg;// Group header (#1e1e1e / #f3f3f3)
    QString editorGroupBorder; // Group border (#444444 / #e7e7e7)
    QString editorGroupDropBg; // Drop background (#515d8a80 / #007acc33)

    // Scrollbar
    QString scrollBg;          // Scrollbar track (#80808033 / #adadad66)
    QString scrollHandle;      // Scrollbar handle (#80808055 / #adadad66)
    QString scrollHandleHover; // Handle hover (#808080aa / #adadad99)
    QString scrollHandleActive;// Handle active drag (#808080dd / #adadadcc)

    // List & Tree
    QString listActiveSelectionBg;   // Active selection in list (#094771 / #eff4fb)
    QString listActiveSelectionFg;   // Selection text (#ffffff / #333333)
    QString listHoverBackground;     // Hover row (#2a2d2e / #f0f0f0)
    QString listDropBackground;      // Drag drop background (#37373d / #e8e8e8)
    QString listFocusOutline;        // Focus outline (#094771 / #007acc)
    QString listFocusBackground;     // Focus background (#062f4a / #e8ebf1)
    QString listInactiveSelectionBg; // Inactive selection (#37373d / #f0f0f0)

    // Widget shadow
    QString widgetShadow;        // Popup shadow (#00000050 / #00000020)

    // Quick Open & Picker
    QString quickInputBg;        // Quick open background (#252526 / #ffffff)
    QString quickInputListFocusBg; // Focus in quick pick (#094771 / #eff4fb)
    QString quickInputListFocusFg; // Text in focus (#ffffff / #333333)
    QString pickerGroupFg;       // Picker group label (#3794ff / #007acc)
    QString pickerGroupBorder;   // Picker group separator (#3794ff / #007acc)

    // Breadcrumb
    QString breadcrumbBg;        // Breadcrumb background (#252526 / #f3f3f3)
    QString breadcrumbFg;        // Breadcrumb text (#cccccc / #5f6368)

    // Badge
    QString badgeBg;             // Notification badge (#007acc / #007acc)
    QString badgeFg;             // Badge foreground (#ffffff / #ffffff)

    // Close button
    QString closeBtnHover;       // Close button hover (#4a4a4a / #d4d4d4)
    QString closeBtnPress;       // Close button press (#5a5a5a / #c8c8c8)

    // Table header
    QString headerBg;            // Table header background (#252526 / #f3f3f3)
    QString headerHover;         // Header hover (#2a2a2a / #ececec)

    // Git decoration colors
    QString gitDecorationAddedResourceFg;     // Added files (#4ecf50 / #4ecf50)
    QString gitDecorationModifiedResourceFg;  // Modified files (#cca700 / #895503)
    QString gitDecorationDeletedResourceFg;   // Deleted files (#f14c4c / #cf2b2b)
    QString gitDecorationUntrackedResourceFg; // Untracked files (#4ecf50 / #4ecf50)
    QString gitDecorationIgnoredResourceFg;   // Ignored files (#848484 / #b0b0b0)
    QString gitDecorationSubmoduleResourceFg; // Submodules (#8be1fd / #8be1fd)

    // Extension buttons
    QString extensionButtonProminentBg;    // Prominent button (#0e63bb / #0e63bb)
    QString extensionButtonProminentHoverBg; // Hover (#0d5fae / #0b5a8f)

    // Diff editor
    QString diffEditorInsertedTextBg;  // Inserted text background (#00ff0033 / #00ff0033)
    QString diffEditorRemovedTextBg;   // Removed text background (#ff000033 / #ff000033)

    // Terminal ANSI colors (Dark+)
    QString terminalAnsiBlack;
    QString terminalAnsiRed;
    QString terminalAnsiGreen;
    QString terminalAnsiYellow;
    QString terminalAnsiBlue;
    QString terminalAnsiMagenta;
    QString terminalAnsiCyan;
    QString terminalAnsiWhite;
    QString terminalAnsiBrightBlack;
    QString terminalAnsiBrightRed;
    QString terminalAnsiBrightGreen;
    QString terminalAnsiBrightYellow;
    QString terminalAnsiBrightBlue;
    QString terminalAnsiBrightMagenta;
    QString terminalAnsiBrightCyan;
    QString terminalAnsiBrightWhite;
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

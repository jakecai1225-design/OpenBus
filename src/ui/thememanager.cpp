#include "thememanager.h"
#include "utils/svg_icon.h"
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QDir>
#include <QPalette>
#include <algorithm>

ThemeManager *ThemeManager::instance()
{
    static ThemeManager *inst = nullptr;
    if (!inst)
        inst = new ThemeManager(qApp);
    return inst;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    initThemes();
}

void ThemeManager::initThemes()
{
    // ===== Dark+ (VSCode Default Dark) =====
    Theme dark;
    dark.name = "Dark+";
    
    // Core backgrounds
    dark.windowBg = "#1e1e1e";      dark.contentBg = "#1e1e1e";     dark.sidebarBg = "#252526";   dark.panelBg = "#1e1e1e";
    
    // Title bar (integrated)
    dark.titleBarActiveBg = "#007acc"; dark.titleBarInactiveBg = "#454545";
    dark.titleBarActiveFg = "#ffffff"; dark.titleBarInactiveFg = "#bbbbbb";
    dark.titleBarFocusBorder = "#007acc";
    dark.barBg = "#007acc";         dark.barFg = "#ffffff";          dark.barHover = "#005fa3";   dark.barBorder = "#007acc";
    
    // Activity bar (48px left dock)
    dark.activityBarBg = "#333333";  dark.activityBarFg = "#ffffff";  dark.activityBarBorder = "#333333";
    dark.activityBarHover = "#ffffff";
    dark.activityBarBadgeBg = "#007acc"; dark.activityBarBadgeFg = "#ffffff";
    dark.activityBarDropBorder = "#d7ba7d";
    
    // SideBar
    dark.sideBarForeground = "#cccccc"; dark.sideBarTitleFg = "#bbbbbb";
    dark.sideBarBorder = "#2b2b2b";    dark.sideBarDropBorder = "#d7ba7d";
    
    // Text
    dark.text = "#cccccc";            dark.textDim = "#858585";        dark.errorFg = "#f48771";
    dark.linkFg = "#3794ff";          dark.iconFg = "#cccccc";
    
    // Accent
    dark.accent = "#007acc";          dark.accentHover = "#005fa3";    dark.accentBorder = "#007acc";
    
    // Borders
    dark.border = "#2b2b2b";          dark.borderDim = "#424242";
    
    // Selection & hover
    dark.selectionBg = "#264f78";     dark.hoverBg = "#2a2d2d";        dark.altRowBg = "#1a1a1a";
    
    // Buttons
    dark.buttonBg = "#0e63bb";        dark.buttonHover = "#0d5fae";    dark.buttonPress = "#0b5a8f";
    dark.buttonSecondaryBg = "#333333"; dark.buttonSecondaryFg = "#cccccc";
    dark.buttonDisabledBg = "#3c3c3c"; dark.buttonDisabledText = "#858585";
    
    // Checkbox & Dropdown
    dark.checkboxBg = "#2b2b2b";      dark.checkboxFg = "#cccccc";     dark.checkboxBorder = "#1b1b1b";
    dark.dropdownBg = "#2b2b2b";      dark.dropdownFg = "#cccccc";     dark.dropdownBorder = "#1b1b1b";
    
    // Input
    dark.inputBg = "#3c3c3c";         dark.inputFg = "#cccccc";        dark.inputPlaceholderFg = "#aaaaaa";
    dark.inputBorder = "#3c3c3c";
    
    // Status bar
    dark.statusBg = "#007acc";        dark.statusFg = "#ffffff";       dark.statusBarBorder = "#007acc";
    dark.statusBarItemHoverBg = "#005a9e80"; dark.statusBarItemRemoteBg = "#ce9178";
    
    // Terminal
    dark.terminalBg = "#1e1e1e";      dark.terminalFg = "#cccccc";
    dark.terminalCursorFg = "#ffffff"; dark.terminalCursorBg = "#aca1ee";
    
    // Tabs
    dark.tabBg = "#1e1e1e";           dark.tabActiveBg = "#1e1e1e";    dark.tabInactiveBg = "#2d2d2d";
    dark.tabActiveFg = "#ffffff";     dark.tabInactiveFg = "#9c9c9c";  dark.tabHoverBg = "#2d2d2d";
    dark.tabBorder = "#1e1e1e";       dark.tabActiveBorderTop = "#007acc";
    dark.editorGroupHeaderBg = "#1e1e1e"; dark.editorGroupBorder = "#444444";
    dark.editorGroupDropBg = "#515d8a80";
    
    // Scrollbar
    dark.scrollBg = "#80808033";      dark.scrollHandle = "#80808055"; dark.scrollHandleHover = "#808080aa";
    dark.scrollHandleActive = "#808080dd";
    
    // List & Tree
    dark.listActiveSelectionBg = "#094771"; dark.listActiveSelectionFg = "#ffffff";
    dark.listHoverBackground = "#2a2d2e";  dark.listDropBackground = "#37373d";
    dark.listFocusOutline = "#094771";     dark.listFocusBackground = "#062f4a";
    dark.listInactiveSelectionBg = "#37373d";
    
    // Widget shadow
    dark.widgetShadow = "#00000050";
    
    // Quick Open & Picker
    dark.quickInputBg = "#252526";    dark.quickInputListFocusBg = "#094771";
    dark.quickInputListFocusFg = "#ffffff";
    dark.pickerGroupFg = "#3794ff";   dark.pickerGroupBorder = "#3794ff";
    
    // Breadcrumb
    dark.breadcrumbBg = "#252526";    dark.breadcrumbFg = "#cccccc";
    
    // Badge
    dark.badgeBg = "#007acc";         dark.badgeFg = "#ffffff";
    
    // Close button
    dark.closeBtnHover = "#4a4a4a";   dark.closeBtnPress = "#5a5a5a";
    
    // Table header
    dark.headerBg = "#252526";        dark.headerHover = "#2a2a2a";
    
    // Git decoration colors
    dark.gitDecorationAddedResourceFg = "#4ecf50";
    dark.gitDecorationModifiedResourceFg = "#cca700";
    dark.gitDecorationDeletedResourceFg = "#f14c4c";
    dark.gitDecorationUntrackedResourceFg = "#4ecf50";
    dark.gitDecorationIgnoredResourceFg = "#848484";
    dark.gitDecorationSubmoduleResourceFg = "#8be1fd";
    
    // Extension buttons
    dark.extensionButtonProminentBg = "#0e63bb";
    dark.extensionButtonProminentHoverBg = "#0d5fae";
    
    // Diff editor
    dark.diffEditorInsertedTextBg = "#00ff0033";
    dark.diffEditorRemovedTextBg = "#ff000033";
    
    // Terminal ANSI colors (Dark+)
    dark.terminalAnsiBlack = "#323232";
    dark.terminalAnsiRed = "#f44e48";
    dark.terminalAnsiGreen = "#4caf74";
    dark.terminalAnsiYellow = "#bbca36";
    dark.terminalAnsiBlue = "#2e88d6";
    dark.terminalAnsiMagenta = "#b557ba";
    dark.terminalAnsiCyan = "#3fb4e2";
    dark.terminalAnsiWhite = "#dfdfdf";
    dark.terminalAnsiBrightBlack = "#545454";
    dark.terminalAnsiBrightRed = "#f44e48";
    dark.terminalAnsiBrightGreen = "#4caf74";
    dark.terminalAnsiBrightYellow = "#bbca36";
    dark.terminalAnsiBrightBlue = "#2e88d6";
    dark.terminalAnsiBrightMagenta = "#b557ba";
    dark.terminalAnsiBrightCyan = "#3fb4e2";
    dark.terminalAnsiBrightWhite = "#ffffff";
    
    m_themes.append({dark.name, dark});
    
    // ===== Light (VSCode Default Light) =====
    Theme light;
    light.name = "Light";
    
    // Core backgrounds
    light.windowBg = "#ffffff";       light.contentBg = "#ffffff";      light.sidebarBg = "#f3f3f3";   light.panelBg = "#ffffff";
    
    // Title bar (integrated)
    light.titleBarActiveBg = "#007acc"; light.titleBarInactiveBg = "#dddddd";
    light.titleBarActiveFg = "#ffffff"; light.titleBarInactiveFg = "#666666";
    light.titleBarFocusBorder = "#007acc";
    light.barBg = "#007acc";          light.barFg = "#ffffff";           light.barHover = "#005fa3";  light.barBorder = "#007acc";
    
    // Activity bar (48px left dock - still dark in Light theme)
    light.activityBarBg = "#272727";  light.activityBarFg = "#ffffff";   light.activityBarBorder = "#272727";
    light.activityBarHover = "#ffffff";
    light.activityBarBadgeBg = "#007acc"; light.activityBarBadgeFg = "#ffffff";
    light.activityBarDropBorder = "#d7ba7d";
    
    // SideBar
    light.sideBarForeground = "#5f6368"; light.sideBarTitleFg = "#5f6368";
    light.sideBarBorder = "#e7e7e7";    light.sideBarDropBorder = "#d7ba7d";
    
    // Text
    light.text = "#333333";            light.textDim = "#6c6c6c";         light.errorFg = "#e81721";
    light.linkFg = "#0066b8";          light.iconFg = "#333333";
    
    // Accent
    light.accent = "#007acc";          light.accentHover = "#005fa3";     light.accentBorder = "#0066b8";
    
    // Borders
    light.border = "#e4e4e4";          light.borderDim = "#e7e7e7";
    
    // Selection & hover
    light.selectionBg = "#add6ff80";   light.hoverBg = "#f0f0f0";         light.altRowBg = "#fafafa";
    
    // Buttons
    light.buttonBg = "#0e63bb";        light.buttonHover = "#0b5a8f";     light.buttonPress = "#094d7b";
    light.buttonSecondaryBg = "#f0f0f0"; light.buttonSecondaryFg = "#333333";
    light.buttonDisabledBg = "#dadada"; light.buttonDisabledText = "#858585";
    
    // Checkbox & Dropdown
    light.checkboxBg = "#fafafa";      light.checkboxFg = "#333333";      light.checkboxBorder = "#c8c8c8";
    light.dropdownBg = "#fafafa";      light.dropdownFg = "#333333";      light.dropdownBorder = "#c8c8c8";
    
    // Input
    light.inputBg = "#fcfcfc";         light.inputFg = "#333333";         light.inputPlaceholderFg = "#999999";
    light.inputBorder = "#c8c8c8";
    
    // Status bar
    light.statusBg = "#007acc";        light.statusFg = "#ffffff";        light.statusBarBorder = "#007acc";
    light.statusBarItemHoverBg = "#005fa380"; light.statusBarItemRemoteBg = "#ce9178";
    
    // Terminal
    light.terminalBg = "#ffffff";      light.terminalFg = "#333333";
    light.terminalCursorFg = "#000000"; light.terminalCursorBg = "#aca1ee";
    
    // Tabs
    light.tabBg = "#ffffff";           light.tabActiveBg = "#ffffff";     light.tabInactiveBg = "#ececec";
    light.tabActiveFg = "#333333";     light.tabInactiveFg = "#666666";   light.tabHoverBg = "#ececec";
    light.tabBorder = "#e7e7e7";       light.tabActiveBorderTop = "#007acc";
    light.editorGroupHeaderBg = "#f3f3f3"; light.editorGroupBorder = "#e7e7e7";
    light.editorGroupDropBg = "#007acc33";
    
    // Scrollbar
    light.scrollBg = "#adadad66";      light.scrollHandle = "#adadad66";  light.scrollHandleHover = "#adadad99";
    light.scrollHandleActive = "#adadadcc";
    
    // List & Tree
    light.listActiveSelectionBg = "#eff4fb"; light.listActiveSelectionFg = "#333333";
    light.listHoverBackground = "#f0f0f0";   light.listDropBackground = "#e8e8e8";
    light.listFocusOutline = "#007acc";        light.listFocusBackground = "#e8ebf1";
    light.listInactiveSelectionBg = "#f0f0f0";
    
    // Widget shadow
    light.widgetShadow = "#00000020";
    
    // Quick Open & Picker
    light.quickInputBg = "#ffffff";    light.quickInputListFocusBg = "#eff4fb";
    light.quickInputListFocusFg = "#333333";
    light.pickerGroupFg = "#007acc";   light.pickerGroupBorder = "#007acc";
    
    // Breadcrumb
    light.breadcrumbBg = "#f3f3f3";    light.breadcrumbFg = "#5f6368";
    
    // Badge
    light.badgeBg = "#007acc";         light.badgeFg = "#ffffff";
    
    // Close button
    light.closeBtnHover = "#d4d4d4";   light.closeBtnPress = "#c8c8c8";
    
    // Table header
    light.headerBg = "#f3f3f3";        light.headerHover = "#ececec";
    
    // Git decoration colors
    light.gitDecorationAddedResourceFg = "#4ecf50";
    light.gitDecorationModifiedResourceFg = "#895503";
    light.gitDecorationDeletedResourceFg = "#cf2b2b";
    light.gitDecorationUntrackedResourceFg = "#4ecf50";
    light.gitDecorationIgnoredResourceFg = "#b0b0b0";
    light.gitDecorationSubmoduleResourceFg = "#8be1fd";
    
    // Extension buttons
    light.extensionButtonProminentBg = "#0e63bb";
    light.extensionButtonProminentHoverBg = "#0b5a8f";
    
    // Diff editor
    light.diffEditorInsertedTextBg = "#00ff0033";
    light.diffEditorRemovedTextBg = "#ff000033";
    
    // Terminal ANSI colors (Light)
    light.terminalAnsiBlack = "#323232";
    light.terminalAnsiRed = "#cd3131";
    light.terminalAnsiGreen = "#0dbc79";
    light.terminalAnsiYellow = "#e5c07b";
    light.terminalAnsiBlue = "#5bc0de";
    light.terminalAnsiMagenta = "#8f55a6";
    light.terminalAnsiCyan = "#1f719d";
    light.terminalAnsiWhite = "#5a646c";
    light.terminalAnsiBrightBlack = "#686868";
    light.terminalAnsiBrightRed = "#a00000";
    light.terminalAnsiBrightGreen = "#009f13";
    light.terminalAnsiBrightYellow = "#b89618";
    light.terminalAnsiBrightBlue = "#4088b8";
    light.terminalAnsiBrightMagenta = "#8f55a6";
    light.terminalAnsiBrightCyan = "#1f719d";
    light.terminalAnsiBrightWhite = "#9ea6a8";
    
    m_themes.append({light.name, light});
}

QStringList ThemeManager::themeNames() const
{
    QStringList names;
    for (const auto &p : m_themes)
        names << p.first;
    return names;
}

const Theme &ThemeManager::currentTheme() const
{
    static Theme fallback;
    for (const auto &p : m_themes) {
        if (p.first == m_currentName)
            return p.second;
    }
    return fallback;
}

void ThemeManager::applyTheme(const QString &name)
{
    // 仅保留 Light 一套配色；name 未命中时回退到首个（唯一）主题，
    // 避免历史调用传入已删除的主题名导致界面无样式
    const Theme *sel = nullptr;
    for (const auto &p : m_themes) {
        if (p.first == name) {
            m_currentName = p.first;
            sel = &p.second;
            break;
        }
    }
    if (!sel) {
        if (m_themes.isEmpty())
            return;
        m_currentName = m_themes.first().first;
        sel = &m_themes.first().second;
    }
    const Theme &t = *sel;

    qApp->setStyleSheet(generateQss(t));

    // 同步 QPalette — QSS 未覆盖的原生绘制部件（输入框清除按钮回退、
    // 消息框、原生弹窗等）也能跟随主题，避免配色残留
    QPalette pal;
    pal.setColor(QPalette::Window, QColor(t.windowBg));
    pal.setColor(QPalette::WindowText, QColor(t.text));
    pal.setColor(QPalette::Base, QColor(t.contentBg));
    pal.setColor(QPalette::AlternateBase, QColor(t.altRowBg));
    pal.setColor(QPalette::ToolTipBase, QColor(t.contentBg));
    pal.setColor(QPalette::ToolTipText, QColor(t.text));
    pal.setColor(QPalette::Text, QColor(t.text));
    pal.setColor(QPalette::Button, QColor(t.buttonBg));
    pal.setColor(QPalette::ButtonText, QColor(t.text));
    pal.setColor(QPalette::BrightText, QColor("#ffffff"));
    pal.setColor(QPalette::Highlight, QColor(t.accent));
    pal.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    pal.setColor(QPalette::Link, QColor(t.accentHover));
    pal.setColor(QPalette::PlaceholderText, QColor(t.textDim));
    const QColor dimText(t.buttonDisabledText);
    pal.setColor(QPalette::Disabled, QPalette::Text, dimText);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, dimText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, dimText);
    qApp->setPalette(pal);

    // 已存在的 QLineEdit 清除按钮统一换主题色图标
    // （原生 × 图标不随主题，部分底色下不可见）
    for (QWidget *w : qApp->allWidgets()) {
        auto *edit = qobject_cast<QLineEdit *>(w);
        if (edit && edit->property("clearButtonEnabled").toBool())
            applyClearButtonIcon(edit, t.text);
    }

    emit themeChanged(m_currentName);
}

// 生成主题色小图标（树形分支箭头 + 缩进参考线 + SpinBox/ComboBox/表头排序上下箭头
// + 标签关闭 × + 单选圆点 + 复选半选横线）到临时目录，供 QSS image: 引用。
// QSS 的 image: 只能引用真实文件路径，无法使用 qrc 内的 currentColor 占位 SVG，
// 故每次换主题时按当前配色写出小尺寸 SVG。
// 树图标 20x24 视口匹配 Qt 分支元素（缩进 20 × 行高 24），避免 image: 拉伸变形。
// 其余图标 10x10 视口（边框三角法在 Qt QSS 中渲染不可靠，改用真实图片）。
static QString writeTreeIcons(const Theme &t)
{
    QString dir = QDir::tempPath() + "/openbus_theme_icons";
    if (!QDir().mkpath(dir))
        return {};
    dir.replace('\\', '/');   // QSS url() 内使用正斜杠

    const auto write = [&dir](const QString &name, const QString &svg) {
        QFile f(dir + '/' + name);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(svg.toUtf8());
        return true;
    };

    const QString chevronRight = QStringLiteral(
        "<svg width='20' height='24' xmlns='http://www.w3.org/2000/svg'>"
        "<path d='M8 9 L13 12 L8 15' fill='none' stroke='%1' stroke-width='1.4' "
        "stroke-linecap='round' stroke-linejoin='round'/></svg>").arg(t.textDim);
    const QString chevronDown = QStringLiteral(
        "<svg width='20' height='24' xmlns='http://www.w3.org/2000/svg'>"
        "<path d='M7 10 L10 13 L13 10' fill='none' stroke='%1' stroke-width='1.4' "
        "stroke-linecap='round' stroke-linejoin='round'/></svg>").arg(t.textDim);
    const QString indentGuide = QStringLiteral(
        "<svg width='20' height='24' xmlns='http://www.w3.org/2000/svg'>"
        "<rect x='9' y='0' width='1' height='24' fill='%1' fill-opacity='0.5'/></svg>").arg(t.textDim);
    const auto smallChevron = [](const char *d, const QString &color) {
        return QStringLiteral(
            "<svg width='10' height='10' xmlns='http://www.w3.org/2000/svg'>"
            "<path d='%1' fill='none' stroke='%2' stroke-width='1.4' "
            "stroke-linecap='round' stroke-linejoin='round'/></svg>")
            .arg(QString::fromLatin1(d), color);
    };
    // 上箭头 ^ 与下箭头 v（SpinBox/ComboBox/表头排序共用），常规 textDim / 悬停 accent
    const QString spinUp      = smallChevron("M2.5 6.5 L5 4 L7.5 6.5", t.textDim);
    const QString spinDown    = smallChevron("M2.5 4 L5 6.5 L7.5 4",   t.textDim);
    const QString spinUpHov   = smallChevron("M2.5 6.5 L5 4 L7.5 6.5", t.accent);
    const QString spinDownHov = smallChevron("M2.5 4 L5 6.5 L7.5 4",   t.accent);

    // 标签页关闭 ×（QTabBar::close-button），常规 textDim / 悬停 text
    const auto smallX = [](const QString &color) {
        return QStringLiteral(
            "<svg width='10' height='10' xmlns='http://www.w3.org/2000/svg'>"
            "<path d='M3 3 L7 7 M3 7 L7 3' fill='none' stroke='%1' stroke-width='1.4' "
            "stroke-linecap='round' stroke-linejoin='round'/></svg>").arg(color);
    };
    const QString tabClose    = smallX(t.textDim);
    const QString tabCloseHov = smallX(t.text);

    // 单选钮选中圆点（accent）
    const QString radioDot = QStringLiteral(
        "<svg width='10' height='10' xmlns='http://www.w3.org/2000/svg'>"
        "<circle cx='5' cy='5' r='2.5' fill='%1'/></svg>").arg(t.accent);

    // 复选框半选横线（白 — 铺在 accent 底色上）
    const QString checkIndet = QStringLiteral(
        "<svg width='10' height='10' xmlns='http://www.w3.org/2000/svg'>"
        "<rect x='2' y='4' width='6' height='2' rx='1' fill='#ffffff'/></svg>");

    if (!write(QStringLiteral("chevron-right.svg"), chevronRight)
        || !write(QStringLiteral("chevron-down.svg"), chevronDown)
        || !write(QStringLiteral("indent-guide.svg"), indentGuide)
        || !write(QStringLiteral("spin-up.svg"), spinUp)
        || !write(QStringLiteral("spin-down.svg"), spinDown)
        || !write(QStringLiteral("spin-up-hover.svg"), spinUpHov)
        || !write(QStringLiteral("spin-down-hover.svg"), spinDownHov)
        || !write(QStringLiteral("tab-close.svg"), tabClose)
        || !write(QStringLiteral("tab-close-hover.svg"), tabCloseHov)
        || !write(QStringLiteral("radio-dot.svg"), radioDot)
        || !write(QStringLiteral("check-indeterminate.svg"), checkIndet))
        return {};
    return dir;
}

QString ThemeManager::generateQss(const Theme &t) const
{
    // 1. 读取 QSS 模板 — 优先从文件系统 (开发模式, 改完无需编译), 回退到 qrc
    QString rawQss;
    QString fsPath = QDir::currentPath() + "/styles/theme.qss";
    if (QFile::exists(fsPath)) {
        QFile f(fsPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            rawQss = QString::fromUtf8(f.readAll());
    }
    if (rawQss.isEmpty()) {
        QFile f(":/styles/theme.qss");
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            rawQss = QString::fromUtf8(f.readAll());
    }
    if (rawQss.isEmpty())
        return {};

    // 2. 构建 @变量 → 颜色值 映射（覆盖完整的 VSCode UX 配色）
    static const QHash<QString, QString Theme::*> varMap = {
        // Core backgrounds
        {"@windowBg",          &Theme::windowBg},
        {"@contentBg",         &Theme::contentBg},
        {"@sidebarBg",         &Theme::sidebarBg},
        {"@panelBg",           &Theme::panelBg},
        
        // Title bar
        {"@titleBarActiveBg",  &Theme::titleBarActiveBg},
        {"@titleBarInactiveBg",&Theme::titleBarInactiveBg},
        {"@titleBarActiveFg",  &Theme::titleBarActiveFg},
        {"@titleBarInactiveFg",&Theme::titleBarInactiveFg},
        {"@titleBarFocusBorder",&Theme::titleBarFocusBorder},
        
        // Menu & Activity bar
        {"@barBg",             &Theme::barBg},
        {"@barFg",             &Theme::barFg},
        {"@barHover",          &Theme::barHover},
        {"@barBorder",         &Theme::barBorder},
        {"@activityBarBg",     &Theme::activityBarBg},
        {"@activityBarFg",     &Theme::activityBarFg},
        {"@activityBarBorder", &Theme::activityBarBorder},
        {"@activityBarHover",  &Theme::activityBarHover},
        {"@activityBarBadgeBg",&Theme::activityBarBadgeBg},
        {"@activityBarBadgeFg",&Theme::activityBarBadgeFg},
        {"@activityBarDropBorder",&Theme::activityBarDropBorder},
        
        // SideBar
        {"@sideBarForeground", &Theme::sideBarForeground},
        {"@sideBarTitleFg",    &Theme::sideBarTitleFg},
        {"@sideBarBorder",     &Theme::sideBarBorder},
        {"@sideBarDropBorder", &Theme::sideBarDropBorder},
        
        // Text
        {"@text",              &Theme::text},
        {"@textDim",           &Theme::textDim},
        {"@errorFg",           &Theme::errorFg},
        {"@linkFg",            &Theme::linkFg},
        {"@iconFg",            &Theme::iconFg},
        
        // Accent
        {"@accent",            &Theme::accent},
        {"@accentHover",       &Theme::accentHover},
        {"@accentBorder",      &Theme::accentBorder},
        
        // Borders
        {"@border",            &Theme::border},
        {"@borderDim",         &Theme::borderDim},
        
        // Selection & hover
        {"@selectionBg",       &Theme::selectionBg},
        {"@hoverBg",           &Theme::hoverBg},
        {"@altRowBg",          &Theme::altRowBg},
        
        // Buttons
        {"@buttonBg",          &Theme::buttonBg},
        {"@buttonHover",       &Theme::buttonHover},
        {"@buttonPress",       &Theme::buttonPress},
        {"@buttonSecondaryBg", &Theme::buttonSecondaryBg},
        {"@buttonSecondaryFg", &Theme::buttonSecondaryFg},
        {"@buttonDisabledBg",  &Theme::buttonDisabledBg},
        {"@buttonDisabledText",&Theme::buttonDisabledText},
        
        // Checkbox & Dropdown
        {"@checkboxBg",        &Theme::checkboxBg},
        {"@checkboxFg",        &Theme::checkboxFg},
        {"@checkboxBorder",    &Theme::checkboxBorder},
        {"@dropdownBg",        &Theme::dropdownBg},
        {"@dropdownFg",        &Theme::dropdownFg},
        {"@dropdownBorder",    &Theme::dropdownBorder},
        
        // Input
        {"@inputBg",           &Theme::inputBg},
        {"@inputFg",           &Theme::inputFg},
        {"@inputPlaceholderFg",&Theme::inputPlaceholderFg},
        {"@inputBorder",       &Theme::inputBorder},
        
        // Status bar
        {"@statusBg",          &Theme::statusBg},
        {"@statusFg",          &Theme::statusFg},
        {"@statusBarBorder",   &Theme::statusBarBorder},
        {"@statusBarItemHoverBg",&Theme::statusBarItemHoverBg},
        {"@statusBarItemRemoteBg",&Theme::statusBarItemRemoteBg},
        
        // Terminal
        {"@terminalBg",        &Theme::terminalBg},
        {"@terminalFg",        &Theme::terminalFg},
        {"@terminalCursorFg",  &Theme::terminalCursorFg},
        {"@terminalCursorBg",  &Theme::terminalCursorBg},
        
        // Tabs
        {"@tabBg",             &Theme::tabBg},
        {"@tabActiveBg",       &Theme::tabActiveBg},
        {"@tabInactiveBg",     &Theme::tabInactiveBg},
        {"@tabActiveFg",       &Theme::tabActiveFg},
        {"@tabInactiveFg",     &Theme::tabInactiveFg},
        {"@tabHoverBg",        &Theme::tabHoverBg},
        {"@tabBorder",         &Theme::tabBorder},
        {"@tabActiveBorderTop",&Theme::tabActiveBorderTop},
        {"@editorGroupHeaderBg",&Theme::editorGroupHeaderBg},
        {"@editorGroupBorder", &Theme::editorGroupBorder},
        {"@editorGroupDropBg", &Theme::editorGroupDropBg},
        
        // Scrollbar
        {"@scrollBg",          &Theme::scrollBg},
        {"@scrollHandle",      &Theme::scrollHandle},
        {"@scrollHandleHover", &Theme::scrollHandleHover},
        {"@scrollHandleActive",&Theme::scrollHandleActive},
        
        // List & Tree
        {"@listActiveSelectionBg",&Theme::listActiveSelectionBg},
        {"@listActiveSelectionFg",&Theme::listActiveSelectionFg},
        {"@listHoverBackground", &Theme::listHoverBackground},
        {"@listDropBackground",  &Theme::listDropBackground},
        {"@listFocusOutline",    &Theme::listFocusOutline},
        {"@listFocusBackground", &Theme::listFocusBackground},
        {"@listInactiveSelectionBg",&Theme::listInactiveSelectionBg},
        
        // Widget shadow
        {"@widgetShadow",      &Theme::widgetShadow},
        
        // Quick Open & Picker
        {"@quickInputBg",      &Theme::quickInputBg},
        {"@quickInputListFocusBg",&Theme::quickInputListFocusBg},
        {"@quickInputListFocusFg",&Theme::quickInputListFocusFg},
        {"@pickerGroupFg",     &Theme::pickerGroupFg},
        {"@pickerGroupBorder", &Theme::pickerGroupBorder},
        
        // Breadcrumb
        {"@breadcrumbBg",      &Theme::breadcrumbBg},
        {"@breadcrumbFg",      &Theme::breadcrumbFg},
        
        // Badge
        {"@badgeBg",           &Theme::badgeBg},
        {"@badgeFg",           &Theme::badgeFg},
        
        // Close button
        {"@closeBtnHover",     &Theme::closeBtnHover},
        {"@closeBtnPress",     &Theme::closeBtnPress},
        
        // Table header
        {"@headerBg",          &Theme::headerBg},
        {"@headerHover",       &Theme::headerHover},
        
        // Git decoration colors
        {"@gitDecorationAddedResourceFg", &Theme::gitDecorationAddedResourceFg},
        {"@gitDecorationModifiedResourceFg", &Theme::gitDecorationModifiedResourceFg},
        {"@gitDecorationDeletedResourceFg", &Theme::gitDecorationDeletedResourceFg},
        {"@gitDecorationUntrackedResourceFg", &Theme::gitDecorationUntrackedResourceFg},
        {"@gitDecorationIgnoredResourceFg", &Theme::gitDecorationIgnoredResourceFg},
        {"@gitDecorationSubmoduleResourceFg", &Theme::gitDecorationSubmoduleResourceFg},
        
        // Extension buttons
        {"@extensionButtonProminentBg", &Theme::extensionButtonProminentBg},
        {"@extensionButtonProminentHoverBg", &Theme::extensionButtonProminentHoverBg},
        
        // Diff editor
        {"@diffEditorInsertedTextBg", &Theme::diffEditorInsertedTextBg},
        {"@diffEditorRemovedTextBg", &Theme::diffEditorRemovedTextBg},
    };

    // 3. 替换所有 @变量 — 按键长降序，避免前缀键破坏长键
    //    （如 @text 先替换会把 @textDim 变成 "#3b3b3bDim"，@border/@borderDim、
    //    @accent/@accentHover、@scrollHandle/@scrollHandleHover 同理；QHash 遍历
    //    顺序不确定，不能依赖插入顺序碰巧正确）
    QList<QPair<QString, QString>> vars;
    for (auto it = varMap.constBegin(); it != varMap.constEnd(); ++it)
        vars.append({it.key(), t.*(it.value())});
    std::sort(vars.begin(), vars.end(),
              [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                  return a.first.size() > b.first.size();
              });
    QString qss = rawQss;
    for (const auto &v : vars)
        qss.replace(v.first, v.second);

    // 4. 注入主题色小图标路径（树分支箭头 / 缩进参考线 / SpinBox-ComboBox 上下
    //    箭头 / 表头排序箭头 / 标签关闭 × / 单选圆点 / 复选半选横线）
    const QString iconDir = writeTreeIcons(t);
    if (!iconDir.isEmpty()) {
        QList<QPair<QString, QString>> iconVars = {
            {"@treeChevronRight",  "/chevron-right.svg"},
            {"@treeChevronDown",   "/chevron-down.svg"},
            {"@treeGuide",         "/indent-guide.svg"},
            {"@spinUpHover",       "/spin-up-hover.svg"},
            {"@spinDownHover",     "/spin-down-hover.svg"},
            {"@spinUp",            "/spin-up.svg"},
            {"@spinDown",          "/spin-down.svg"},
            {"@tabCloseHover",     "/tab-close-hover.svg"},
            {"@tabClose",          "/tab-close.svg"},
            {"@radioDot",          "/radio-dot.svg"},
            {"@checkIndet",        "/check-indeterminate.svg"},
        };
        std::sort(iconVars.begin(), iconVars.end(),
                  [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                      return a.first.size() > b.first.size();
                  });
        for (const auto &v : iconVars)
            qss.replace(v.first, iconDir + v.second);
    }

    return qss;
}

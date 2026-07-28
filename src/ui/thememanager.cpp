#include "thememanager.h"
#include <QApplication>

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
    // ===== Light =====
    Theme light;
    light.name = "Light";
    light.windowBg = "#f0f0f0";  light.contentBg = "#ffffff";  light.sidebarBg = "#f5f5f5";
    light.panelBg = "#e0e0e0";
    light.barBg = "#2d2d2d";      light.barFg = "#e0e0e0";      light.barHover = "#3d3d3d";
    light.barBorder = "#1a1a1a";
    light.text = "#333333";      light.textDim = "#666666";
    light.accent = "#4a90d9";     light.accentHover = "#5a9ee8"; light.accentBorder = "#3a7fc9";
    light.border = "#b0b0b0";    light.borderDim = "#c0c0c0";
    light.selectionBg = "#c5d9f1"; light.hoverBg = "#e0e0e0";
    light.buttonBg = "#e0e0e0";  light.buttonHover = "#d0d0d0"; light.buttonPress = "#c0c0c0";
    light.buttonDisabledBg = "#eeeeee"; light.buttonDisabledText = "#aaaaaa";
    light.statusBg = "#4a90d9";  light.statusFg = "#ffffff";
    light.terminalBg = "#1e1e1e"; light.terminalFg = "#d4d4d4";
    light.tabBg = "#e0e0e0";     light.tabActiveBg = "#ffffff"; light.tabHoverBg = "#d8d8d8";
    light.scrollBg = "#f0f0f0";  light.scrollHandle = "#c0c0c0"; light.scrollHandleHover = "#a0a0a0";
    light.closeBtnHover = "#e81123"; light.closeBtnPress = "#f1707a";
    light.headerBg = "#d8d8d8";  light.headerHover = "#c8c8c8";
    light.altRowBg = "#f7f7f7";
    m_themes.append({light.name, light});

    // ===== Dark =====
    Theme dark;
    dark.name = "Dark";
    dark.windowBg = "#1e1e1e";   dark.contentBg = "#252526";   dark.sidebarBg = "#252526";
    dark.panelBg = "#2d2d2d";
    dark.barBg = "#1e1e1e";      dark.barFg = "#cccccc";       dark.barHover = "#3d3d3d";
    dark.barBorder = "#0a0a0a";
    dark.text = "#d4d4d4";       dark.textDim = "#808080";
    dark.accent = "#0e639c";     dark.accentHover = "#1177bb"; dark.accentBorder = "#0a5680";
    dark.border = "#3c3c3c";     dark.borderDim = "#2d2d2d";
    dark.selectionBg = "#264f78"; dark.hoverBg = "#2a2d2e";
    dark.buttonBg = "#3c3c3c";   dark.buttonHover = "#4c4c4c"; dark.buttonPress = "#2c2c2c";
    dark.buttonDisabledBg = "#2d2d2d"; dark.buttonDisabledText = "#5a5a5a";
    dark.statusBg = "#0e639c";   dark.statusFg = "#ffffff";
    dark.terminalBg = "#1e1e1e"; dark.terminalFg = "#d4d4d4";
    dark.tabBg = "#2d2d2d";      dark.tabActiveBg = "#1e1e1e"; dark.tabHoverBg = "#3d3d3d";
    dark.scrollBg = "#1e1e1e";   dark.scrollHandle = "#424242"; dark.scrollHandleHover = "#5a5a5a";
    dark.closeBtnHover = "#e81123"; dark.closeBtnPress = "#f1707a";
    dark.headerBg = "#2d2d2d";  dark.headerHover = "#3d3d3d";
    dark.altRowBg = "#2a2a2b";
    m_themes.append({dark.name, dark});

    // ===== VS Code Dark+ =====
    Theme vscDark;
    vscDark.name = "VS Code Dark+";
    vscDark.windowBg = "#1e1e1e"; vscDark.contentBg = "#1e1e1e"; vscDark.sidebarBg = "#252526";
    vscDark.panelBg = "#333333";
    vscDark.barBg = "#333333";    vscDark.barFg = "#cccccc";     vscDark.barHover = "#454545";
    vscDark.barBorder = "#1a1a1a";
    vscDark.text = "#d4d4d4";     vscDark.textDim = "#858585";
    vscDark.accent = "#007acc";   vscDark.accentHover = "#1f8ad3"; vscDark.accentBorder = "#0066b8";
    vscDark.border = "#3c3c3c";  vscDark.borderDim = "#2d2d2d";
    vscDark.selectionBg = "#264f78"; vscDark.hoverBg = "#2a2d2e";
    vscDark.buttonBg = "#3c3c3c"; vscDark.buttonHover = "#4c4c4c"; vscDark.buttonPress = "#2c2c2c";
    vscDark.buttonDisabledBg = "#2d2d2d"; vscDark.buttonDisabledText = "#5a5a5a";
    vscDark.statusBg = "#007acc"; vscDark.statusFg = "#ffffff";
    vscDark.terminalBg = "#1e1e1e"; vscDark.terminalFg = "#d4d4d4";
    vscDark.tabBg = "#2d2d2d";   vscDark.tabActiveBg = "#1e1e1e"; vscDark.tabHoverBg = "#3d3d3d";
    vscDark.scrollBg = "#1e1e1e"; vscDark.scrollHandle = "#424242"; vscDark.scrollHandleHover = "#5a5a5a";
    vscDark.closeBtnHover = "#e81123"; vscDark.closeBtnPress = "#f1707a";
    vscDark.headerBg = "#333333"; vscDark.headerHover = "#404040";
    vscDark.altRowBg = "#2a2a2b";
    m_themes.append({vscDark.name, vscDark});

    // ===== VS Code Light+ =====
    Theme vscLight;
    vscLight.name = "VS Code Light+";
    vscLight.windowBg = "#f3f3f3"; vscLight.contentBg = "#ffffff"; vscLight.sidebarBg = "#f3f3f3";
    vscLight.panelBg = "#e8e8e8";
    vscLight.barBg = "#2c2c2c";   vscLight.barFg = "#cccccc";    vscLight.barHover = "#3c3c3c";
    vscLight.barBorder = "#1a1a1a";
    vscLight.text = "#333333";    vscLight.textDim = "#6c6c6c";
    vscLight.accent = "#0066b8";  vscLight.accentHover = "#1f7ad3"; vscLight.accentBorder = "#005a9e";
    vscLight.border = "#c4c4c4"; vscLight.borderDim = "#d4d4d4";
    vscLight.selectionBg = "#add6ff"; vscLight.hoverBg = "#e8e8e8";
    vscLight.buttonBg = "#e8e8e8"; vscLight.buttonHover = "#d8d8d8"; vscLight.buttonPress = "#c8c8c8";
    vscLight.buttonDisabledBg = "#f0f0f0"; vscLight.buttonDisabledText = "#b0b0b0";
    vscLight.statusBg = "#0066b8"; vscLight.statusFg = "#ffffff";
    vscLight.terminalBg = "#1e1e1e"; vscLight.terminalFg = "#d4d4d4";
    vscLight.tabBg = "#ececec";  vscLight.tabActiveBg = "#ffffff"; vscLight.tabHoverBg = "#dcdcdc";
    vscLight.scrollBg = "#f3f3f3"; vscLight.scrollHandle = "#c0c0c0"; vscLight.scrollHandleHover = "#a0a0a0";
    vscLight.closeBtnHover = "#e81123"; vscLight.closeBtnPress = "#f1707a";
    vscLight.headerBg = "#e8e8e8"; vscLight.headerHover = "#d8d8d8";
    vscLight.altRowBg = "#f7f7f7";
    m_themes.append({vscLight.name, vscLight});

    // ===== Monokai =====
    Theme monokai;
    monokai.name = "Monokai";
    monokai.windowBg = "#272822"; monokai.contentBg = "#272822"; monokai.sidebarBg = "#1e1f1c";
    monokai.panelBg = "#3e3d32";
    monokai.barBg = "#1e1f1c";   monokai.barFg = "#f8f8f2";    monokai.barHover = "#3e3d32";
    monokai.barBorder = "#0c0c0a";
    monokai.text = "#f8f8f2";    monokai.textDim = "#75715e";
    monokai.accent = "#a6e22e"; monokai.accentHover = "#b6f23e"; monokai.accentBorder = "#86c20e";
    monokai.border = "#3e3d32"; monokai.borderDim = "#2d2c28";
    monokai.selectionBg = "#49483e"; monokai.hoverBg = "#3e3d32";
    monokai.buttonBg = "#3e3d32"; monokai.buttonHover = "#4e4d42"; monokai.buttonPress = "#2e2d22";
    monokai.buttonDisabledBg = "#2d2c28"; monokai.buttonDisabledText = "#5a5a4e";
    monokai.statusBg = "#a6e22e"; monokai.statusFg = "#272822";
    monokai.terminalBg = "#272822"; monokai.terminalFg = "#f8f8f2";
    monokai.tabBg = "#1e1f1c";   monokai.tabActiveBg = "#272822"; monokai.tabHoverBg = "#3e3d32";
    monokai.scrollBg = "#1e1f1c"; monokai.scrollHandle = "#3e3d32"; monokai.scrollHandleHover = "#5a5a4e";
    monokai.closeBtnHover = "#f92672"; monokai.closeBtnPress = "#fc5a96";
    monokai.headerBg = "#3e3d32"; monokai.headerHover = "#4e4d42";
    monokai.altRowBg = "#2d2e28";
    m_themes.append({monokai.name, monokai});

    // ===== Solarized Light =====
    Theme solLight;
    solLight.name = "Solarized Light";
    solLight.windowBg = "#eee8d5"; solLight.contentBg = "#fdf6e3"; solLight.sidebarBg = "#eee8d5";
    solLight.panelBg = "#ddd6c1";
    solLight.barBg = "#073642";   solLight.barFg = "#93a1a1";    solLight.barHover = "#0a4858";
    solLight.barBorder = "#05232e";
    solLight.text = "#586e75";    solLight.textDim = "#93a1a1";
    solLight.accent = "#268bd2";  solLight.accentHover = "#3a9ee3"; solLight.accentBorder = "#1a6da8";
    solLight.border = "#c8c0a8"; solLight.borderDim = "#d8d2c0";
    solLight.selectionBg = "#eee8d5"; solLight.hoverBg = "#ddd6c1";
    solLight.buttonBg = "#ddd6c1"; solLight.buttonHover = "#cdc6b1"; solLight.buttonPress = "#bdb6a1";
    solLight.buttonDisabledBg = "#e8e0cc"; solLight.buttonDisabledText = "#b0a890";
    solLight.statusBg = "#268bd2"; solLight.statusFg = "#fdf6e3";
    solLight.terminalBg = "#073642"; solLight.terminalFg = "#93a1a1";
    solLight.tabBg = "#ddd6c1";  solLight.tabActiveBg = "#fdf6e3"; solLight.tabHoverBg = "#cdc6b1";
    solLight.scrollBg = "#eee8d5"; solLight.scrollHandle = "#c8c0a8"; solLight.scrollHandleHover = "#a8a088";
    solLight.closeBtnHover = "#dc322f"; solLight.closeBtnPress = "#ec504f";
    solLight.headerBg = "#ddd6c1"; solLight.headerHover = "#cdc6b1";
    solLight.altRowBg = "#f5efdc";
    m_themes.append({solLight.name, solLight});

    // ===== Solarized Dark =====
    Theme solDark;
    solDark.name = "Solarized Dark";
    solDark.windowBg = "#002b36"; solDark.contentBg = "#073642"; solDark.sidebarBg = "#073642";
    solDark.panelBg = "#073642";
    solDark.barBg = "#002b36";   solDark.barFg = "#93a1a1";    solDark.barHover = "#0a3b46";
    solDark.barBorder = "#001a22";
    solDark.text = "#839496";    solDark.textDim = "#586e75";
    solDark.accent = "#268bd2";  solDark.accentHover = "#3a9ee3"; solDark.accentBorder = "#1a6da8";
    solDark.border = "#0a4252";  solDark.borderDim = "#073642";
    solDark.selectionBg = "#073d4a"; solDark.hoverBg = "#0a4252";
    solDark.buttonBg = "#073642"; solDark.buttonHover = "#0a4858"; solDark.buttonPress = "#002b36";
    solDark.buttonDisabledBg = "#073642"; solDark.buttonDisabledText = "#4a6068";
    solDark.statusBg = "#268bd2"; solDark.statusFg = "#fdf6e3";
    solDark.terminalBg = "#002b36"; solDark.terminalFg = "#839496";
    solDark.tabBg = "#002b36";   solDark.tabActiveBg = "#073642"; solDark.tabHoverBg = "#0a4252";
    solDark.scrollBg = "#002b36"; solDark.scrollHandle = "#0a4252"; solDark.scrollHandleHover = "#1a5262";
    solDark.closeBtnHover = "#dc322f"; solDark.closeBtnPress = "#ec504f";
    solDark.headerBg = "#073642"; solDark.headerHover = "#0a4858";
    solDark.altRowBg = "#0a3040";
    m_themes.append({solDark.name, solDark});

    m_currentName = "Light";
}

QStringList ThemeManager::themeNames() const
{
    QStringList names;
    for (const auto &p : m_themes)
        names << p.first;
    return names;
}

void ThemeManager::applyTheme(const QString &name)
{
    for (const auto &p : m_themes) {
        if (p.first == name) {
            m_currentName = name;
            qApp->setStyleSheet(generateQss(p.second));
            return;
        }
    }
}

QString ThemeManager::generateQss(const Theme &t) const
{
    return QString(
        /* ---- Global ---- */
        "QMainWindow { background-color: %1; }"
        "QWidget { font-size: 12px; }"

        /* ---- Menu bar ---- */
        "QMenuBar { background-color: %2; color: %3; border: none; padding: 1px; min-height: 28px; }"
        "QMenuBar::item { background-color: transparent; padding: 4px 12px; border-radius: 2px; }"
        "QMenuBar::item:selected { background-color: %4; }"
        "QMenuBar::item:pressed { background-color: %5; }"

        "QMenu { background-color: %2; color: %3; border: 1px solid %6; padding: 4px; }"
        "QMenu::item { padding: 5px 24px 5px 20px; border-radius: 2px; }"
        "QMenu::item:selected { background-color: %5; color: #ffffff; }"
        "QMenu::separator { height: 1px; background-color: %7; margin: 4px 8px; }"

        /* ---- DockWidget ---- */
        "QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; background-color: %13; }"
        "QDockWidget::title { background-color: %8; padding: 2px 4px; border: none; border-bottom: 1px solid %9; }"
        "#LeftContainer { background-color: %13; }"
        "#LeftDock { background-color: %13; border: none; }"
        "#BottomDock { background-color: %13; border: none; }"
        "#RightDock { background-color: %13; border: none; }"

        /* ---- Panel titles ---- */
        "#CollapsibleTitle { background-color: %10; color: %11; font-weight: bold; font-size: 11px; padding: 4px 8px; border: none; border-bottom: 1px solid %9; }"
        "#CollapsibleTitle:hover { background-color: %8; }"
        "#DockPanelTitle { background-color: %10; color: %12; font-weight: bold; font-size: 11px; padding: 4px 6px; border: none; border-bottom: 1px solid %9; }"
        "#SidePanelTitle { background-color: %10; color: %11; font-weight: bold; font-size: 11px; padding: 6px 10px; border-bottom: 1px solid %9; }"
        "#DbcDetailTitle { background-color: %10; color: %11; font-weight: bold; font-size: 12px; padding: 6px 8px; border-bottom: 1px solid %9; }"

        /* ---- Lists ---- */
        "#ProjectList { background-color: %13; border: none; outline: none; font-size: 12px; }"
        "#ProjectList::item { padding: 4px 8px; min-height: 22px; }"
        "#ProjectList::item:hover { background-color: %14; }"
        "#ProjectList::item:selected { background-color: %15; color: %11; }"

        "QTreeWidget { background-color: %13; border: none; outline: none; font-size: 12px; }"
        "QTreeWidget::item { padding: 2px 4px; min-height: 20px; }"
        "QTreeWidget::item:hover { background-color: %14; }"
        "QTreeWidget::item:selected { background-color: %15; color: %11; }"
        "QTreeWidget::branch:has-siblings:!adjoins-item { background: transparent; }"

        "QListWidget { background-color: %13; border: none; outline: none; font-size: 12px; }"
        "QListWidget::item { padding: 3px 6px; min-height: 20px; }"
        "QListWidget::item:hover { background-color: %14; }"
        "QListWidget::item:selected { background-color: %15; }"

        /* ---- ActivityBar ---- */
        "#ActivityBar { background-color: %2; }"
        "#ActivityBar QToolButton { background-color: transparent; border: none; color: %3; font-size: 18px; padding: 0; margin: 0; }"
        "#ActivityBar QToolButton:hover { background-color: %4; }"
        "#ActivityBar QToolButton:checked { border-left: 2px solid %5; color: #ffffff; }"

        /* ---- Toolbar ---- */
        "QToolBar { background-color: %10; border: none; border-bottom: 1px solid %9; padding: 3px; spacing: 3px; }"
        "QToolBar QToolButton { padding: 4px 8px; margin: 1px; border-radius: 3px; background-color: transparent; color: %11; font-size: 12px; }"
        "QToolBar QToolButton:hover { background-color: %8; }"
        "QToolBar QToolButton:checked { background-color: %9; border: 1px solid %7; }"
        "QToolBar QLabel { color: %12; font-size: 11px; padding: 0 4px; }"

        /* ---- Tabs ---- */
        "QTabWidget::pane { border: none; background-color: %16; }"
        "QTabBar::tab { background-color: %17; color: %18; padding: 6px 14px; border: none; border-right: 1px solid %9; border-bottom: 1px solid %9; font-size: 12px; }"
        "QTabBar::tab:selected { background-color: %19; color: %11; border-bottom: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { background-color: %20; }"

        "#BottomPanel { background-color: %13; }"
        "#BottomPanel::pane { border-top: 1px solid %9; background-color: %13; }"
        "#BottomPanel QTabBar::tab { padding: 4px 12px; font-size: 11px; border-bottom: none; border-top: 2px solid transparent; }"
        "#BottomPanel QTabBar::tab:selected { border-top: 2px solid %5; border-bottom: none; }"

        "#RightPanel::pane { border: none; border-left: 1px solid %9; background-color: %13; }"
        "#RightPanel QTabBar::tab { padding: 4px 10px; font-size: 11px; }"

        /* ---- Inputs ---- */
        "QLineEdit { border: 1px solid %9; border-radius: 3px; padding: 3px 6px; background-color: %16; font-size: 12px; selection-background-color: %5; }"
        "QLineEdit:focus { border: 1px solid %5; }"

        "QPushButton { background-color: %21; color: %11; border: 1px solid %9; border-radius: 3px; padding: 4px 10px; font-size: 12px; }"
        "QPushButton:hover { background-color: %22; border: 1px solid %7; }"
        "QPushButton:pressed { background-color: %23; }"
        "QPushButton:disabled { background-color: %24; color: %25; border: 1px solid %9; }"

        /* ---- Table ---- */
        "QTableView { background-color: %16; alternate-background-color: %26; gridline-color: %9; selection-background-color: %15; selection-color: %11; border: none; font-size: 12px; }"
        "QTableView::item { padding: 1px 4px; min-height: 18px; }"
        "QTableView::item:selected { background-color: %15; }"

        "QHeaderView::section { background-color: %27; color: %11; padding: 3px 6px; border: none; border-right: 1px solid %9; border-bottom: 1px solid %9; font-size: 12px; font-weight: bold; }"
        "QHeaderView::section:hover { background-color: %28; }"

        /* ---- Splitter ---- */
        "QSplitter::handle { background-color: %8; }"
        "QSplitter::handle:vertical { height: 2px; }"
        "QSplitter::handle:horizontal { width: 2px; }"
        "QSplitter::handle:hover { background-color: %5; }"

        /* ---- Slider ---- */
        "QSlider::groove:horizontal { border: 1px solid %9; height: 4px; background: %10; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: %5; border: 1px solid %29; width: 12px; margin: -5px 0; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { background: %30; }"

        /* ---- ComboBox ---- */
        "QComboBox { border: 1px solid %9; border-radius: 3px; padding: 2px 6px; background-color: %16; font-size: 12px; }"
        "QComboBox:hover { border: 1px solid %7; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { border: 1px solid %9; background-color: %16; selection-background-color: %15; selection-color: %11; }"

        /* ---- Status bar ---- */
        "QStatusBar { background-color: %31; color: %32; border-top: 1px solid %29; font-size: 11px; }"
        "QStatusBar QLabel { padding: 1px 8px; }"

        /* ---- CheckBox ---- */
        "QCheckBox { color: %11; font-size: 12px; spacing: 6px; }"

        /* ---- GroupBox ---- */
        "QGroupBox { border: 1px solid %9; border-radius: 4px; margin-top: 10px; padding-top: 6px; font-size: 12px; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"

        /* ---- Dialog ---- */
        "QDialog { background-color: %1; }"
        "QTextBrowser { background-color: %16; color: %11; border: 1px solid %9; font-size: 12px; }"
        "QDialogButtonBox QPushButton { min-width: 70px; }"

        /* ---- SpinBox ---- */
        "QSpinBox { border: 1px solid %9; border-radius: 3px; padding: 2px 4px; background-color: %16; font-size: 12px; }"

        /* ---- Text edit ---- */
        "QPlainTextEdit { background-color: %13; color: %11; border: none; font-family: Consolas, \"Courier New\", monospace; font-size: 12px; }"
        "#TerminalOutput { background-color: %33; color: %34; }"
        "#TerminalPrompt { font-family: monospace; font-weight: bold; color: %5; }"

        /* ---- Table widget ---- */
        "QTableWidget { background-color: %13; alternate-background-color: %8; border: none; gridline-color: %9; font-size: 12px; }"
        "QTableWidget::item { padding: 2px 6px; }"
        "QTableWidget::item:selected { background-color: %15; }"

        /* ---- Scrollbar ---- */
        "QScrollBar:vertical { border: none; background: %35; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %36; min-height: 20px; border-radius: 5px; }"
        "QScrollBar::handle:vertical:hover { background: %37; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { border: none; background: %35; height: 10px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %36; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal:hover { background: %37; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"

        /* ---- Window buttons ---- */
        "#WindowButtons { background-color: transparent; }"
        "#WinMinBtn, #WinMaxBtn { background-color: transparent; color: %3; border: none; font-size: 13px; }"
        "#WinMinBtn:hover, #WinMaxBtn:hover { background-color: %4; }"
        "#WinMinBtn:pressed, #WinMaxBtn:pressed { background-color: %5; }"
        "#WinCloseBtn { background-color: transparent; color: %3; border: none; font-size: 13px; }"
        "#WinCloseBtn:hover { background-color: %38; color: #ffffff; }"
        "#WinCloseBtn:pressed { background-color: %39; color: #ffffff; }"
    )
    .arg(t.windowBg)      // %1
    .arg(t.barBg)         // %2
    .arg(t.barFg)         // %3
    .arg(t.barHover)      // %4
    .arg(t.accent)        // %5
    .arg(t.barBorder)     // %6
    .arg(t.border)        // %7
    .arg(t.panelBg)       // %8
    .arg(t.borderDim)     // %9
    .arg(t.panelBg)       // %10 (same as %8)
    .arg(t.text)          // %11
    .arg(t.textDim)       // %12
    .arg(t.sidebarBg)     // %13
    .arg(t.hoverBg)       // %14
    .arg(t.selectionBg)   // %15
    .arg(t.contentBg)     // %16
    .arg(t.tabBg)         // %17
    .arg(t.textDim)       // %18
    .arg(t.tabActiveBg)   // %19
    .arg(t.tabHoverBg)    // %20
    .arg(t.buttonBg)      // %21
    .arg(t.buttonHover)   // %22
    .arg(t.buttonPress)   // %23
    .arg(t.buttonDisabledBg)  // %24
    .arg(t.buttonDisabledText) // %25
    .arg(t.altRowBg)      // %26
    .arg(t.headerBg)      // %27
    .arg(t.headerHover)   // %28
    .arg(t.accentBorder)  // %29
    .arg(t.accentHover)   // %30
    .arg(t.statusBg)      // %31
    .arg(t.statusFg)      // %32
    .arg(t.terminalBg)    // %33
    .arg(t.terminalFg)    // %34
    .arg(t.scrollBg)      // %35
    .arg(t.scrollHandle)  // %36
    .arg(t.scrollHandleHover) // %37
    .arg(t.closeBtnHover) // %38
    .arg(t.closeBtnPress) // %39
    ;
}

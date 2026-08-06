#include "thememanager.h"
#include <QApplication>
#include <QFile>
#include <QHash>
#include <QDir>

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
    light.windowBg = "#f8f8f8";  light.contentBg = "#ffffff";  light.sidebarBg = "#f3f3f3";
    light.panelBg = "#ececec";
    light.barBg = "#dddddd";      light.barFg = "#333333";      light.barHover = "#d0d0d0";
    light.barBorder = "#c4c4c4";
    light.activityBarBg = "#2c2c2c";  light.activityBarFg = "#cccccc";  light.activityBarHover = "#3c3c3c";
    light.text = "#3b3b3b";      light.textDim = "#6c6c6c";
    light.accent = "#0066b8";     light.accentHover = "#1f7ad3"; light.accentBorder = "#005a9e";
    light.border = "#d0d0d0";    light.borderDim = "#e4e4e4";
    light.selectionBg = "#d6ebff"; light.hoverBg = "#eaeaea";
    light.buttonBg = "#ececec";  light.buttonHover = "#dcdcdc"; light.buttonPress = "#cccccc";
    light.buttonDisabledBg = "#f0f0f0"; light.buttonDisabledText = "#b0b0b0";
    light.statusBg = "#0066b8";  light.statusFg = "#ffffff";
    light.terminalBg = "#1e1e1e"; light.terminalFg = "#d4d4d4";
    light.tabBg = "#ececec";     light.tabActiveBg = "#ffffff"; light.tabHoverBg = "#dcdcdc";
    light.scrollBg = "#f8f8f8";  light.scrollHandle = "#c8c8c8"; light.scrollHandleHover = "#a0a0a0";
    light.closeBtnHover = "#e81123"; light.closeBtnPress = "#f1707a";
    light.headerBg = "#f0f0f0";  light.headerHover = "#e8e8e8";
    light.altRowBg = "#fafafa";
    m_themes.append({light.name, light});

    // ===== Dark =====
    Theme dark;
    dark.name = "Dark";
    dark.windowBg = "#1e1e1e";   dark.contentBg = "#252526";   dark.sidebarBg = "#252526";
    dark.panelBg = "#2d2d2d";
    dark.barBg = "#1e1e1e";      dark.barFg = "#cccccc";       dark.barHover = "#3d3d3d";
    dark.barBorder = "#0a0a0a";
    dark.activityBarBg = "#333333";  dark.activityBarFg = "#cccccc";  dark.activityBarHover = "#454545";
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
    vscDark.activityBarBg = "#333333";  vscDark.activityBarFg = "#cccccc";  vscDark.activityBarHover = "#454545";
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
    vscLight.barBg = "#dddddd";   vscLight.barFg = "#333333";    vscLight.barHover = "#d0d0d0";
    vscLight.barBorder = "#c4c4c4";
    vscLight.activityBarBg = "#2c2c2c";  vscLight.activityBarFg = "#cccccc";  vscLight.activityBarHover = "#3c3c3c";
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
    monokai.activityBarBg = "#1e1f1c";  monokai.activityBarFg = "#f8f8f2";  monokai.activityBarHover = "#3e3d32";
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
    solLight.barBg = "#ddd6c1";   solLight.barFg = "#586e75";    solLight.barHover = "#cdc6b1";
    solLight.barBorder = "#b8b098";
    solLight.activityBarBg = "#073642";  solLight.activityBarFg = "#93a1a1";  solLight.activityBarHover = "#0a4858";
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
    solDark.activityBarBg = "#002b36";  solDark.activityBarFg = "#93a1a1";  solDark.activityBarHover = "#0a3b46";
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

    // 2. 构建 @变量 → 颜色值 映射
    static const QHash<QString, QString Theme::*> varMap = {
        {"@windowBg",          &Theme::windowBg},
        {"@contentBg",         &Theme::contentBg},
        {"@sidebarBg",         &Theme::sidebarBg},
        {"@panelBg",           &Theme::panelBg},
        {"@barBg",             &Theme::barBg},
        {"@barFg",             &Theme::barFg},
        {"@barHover",          &Theme::barHover},
        {"@barBorder",         &Theme::barBorder},
        {"@activityBarBg",     &Theme::activityBarBg},
        {"@activityBarFg",     &Theme::activityBarFg},
        {"@activityBarHover",  &Theme::activityBarHover},
        {"@text",              &Theme::text},
        {"@textDim",           &Theme::textDim},
        {"@accent",            &Theme::accent},
        {"@accentHover",       &Theme::accentHover},
        {"@accentBorder",      &Theme::accentBorder},
        {"@border",            &Theme::border},
        {"@borderDim",         &Theme::borderDim},
        {"@selectionBg",       &Theme::selectionBg},
        {"@hoverBg",           &Theme::hoverBg},
        {"@buttonBg",          &Theme::buttonBg},
        {"@buttonHover",       &Theme::buttonHover},
        {"@buttonPress",       &Theme::buttonPress},
        {"@buttonDisabledBg",  &Theme::buttonDisabledBg},
        {"@buttonDisabledText",&Theme::buttonDisabledText},
        {"@statusBg",          &Theme::statusBg},
        {"@statusFg",          &Theme::statusFg},
        {"@terminalBg",        &Theme::terminalBg},
        {"@terminalFg",        &Theme::terminalFg},
        {"@tabBg",             &Theme::tabBg},
        {"@tabActiveBg",       &Theme::tabActiveBg},
        {"@tabHoverBg",        &Theme::tabHoverBg},
        {"@scrollBg",          &Theme::scrollBg},
        {"@scrollHandle",      &Theme::scrollHandle},
        {"@scrollHandleHover", &Theme::scrollHandleHover},
        {"@closeBtnHover",     &Theme::closeBtnHover},
        {"@closeBtnPress",     &Theme::closeBtnPress},
        {"@headerBg",          &Theme::headerBg},
        {"@headerHover",       &Theme::headerHover},
        {"@altRowBg",          &Theme::altRowBg},
    };

    // 3. 替换所有 @变量
    QString qss = rawQss;
    for (auto it = varMap.constBegin(); it != varMap.constEnd(); ++it)
        qss.replace(it.key(), t.*(it.value()));

    return qss;
}

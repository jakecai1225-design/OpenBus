#include "thememanager.h"
#include "utils/svg_icon.h"
#include "utils/gray_branch_style.h"
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
    // Light — flat workbench: every region matches the main window.
    // Only selection (and brief hover) may tint; idle chrome stays window white.
    Theme light;
    light.name = "Light";
    light.windowBg = "#ffffff";
    light.contentBg = "#ffffff";
    light.sidebarBg = "#ffffff";
    light.panelBg = "#ffffff";
    light.barBg = "#ffffff";
    light.barFg = "#3b3b3b";
    light.barHover = "#f0f0f0";
    light.barBorder = "#d0d0d0";
    light.activityBarBg = "#ffffff";
    light.activityBarFg = "#616161";
    light.activityBarHover = "#f0f0f0";
    light.text = "#3b3b3b";
    light.textDim = "#6c6c6c";
    light.accent = "#005fb8";
    light.accentHover = "#1f7ad3";
    light.accentBorder = "#005a9e";
    light.border = "#d0d0d0";
    light.borderDim = "#e0e0e0";
    light.selectionBg = "#cce8ff";
    light.hoverBg = "#f0f0f0";
    light.buttonBg = "#ffffff";
    light.buttonHover = "#f0f0f0";
    light.buttonPress = "#e8e8e8";
    light.buttonDisabledBg = "#ffffff";
    light.buttonDisabledText = "#b0b0b0";
    light.statusBg = "#ffffff";
    light.statusFg = "#3b3b3b";
    light.terminalBg = "#ffffff";
    light.terminalFg = "#3b3b3b";
    light.tabBg = "#ffffff";
    light.tabActiveBg = "#ffffff";
    light.tabHoverBg = "#f5f5f5";
    light.scrollBg = "#ffffff";
    light.scrollHandle = "#c8c8c8";
    light.scrollHandleHover = "#a0a0a0";
    light.closeBtnHover = "#e81123";
    light.closeBtnPress = "#f1707a";
    light.headerBg = "#ffffff";
    light.headerHover = "#f5f5f5";
    light.altRowBg = "#ffffff";
    m_themes.append({light.name, light});

    m_currentName = "Light";
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
    // Light-only: any name (incl. legacy "Dark Modern") maps to Light
    Q_UNUSED(name);
    if (m_themes.isEmpty())
        return;
    m_currentName = m_themes.first().first;
    const Theme &t = m_themes.first().second;

    // Install before stylesheet so QStyleSheetStyle wraps GrayBranchStyle.
    GrayBranchStyle::installOnApp();

    qApp->setStyleSheet(generateQss(t));

    // Sync QPalette — native fallbacks follow theme. Highlight must be the soft
    // selection color (not accent): Windows PE_IndicatorBranch uses Highlight
    // for selected-row branch lines and would otherwise paint thick blue bars.
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
    pal.setColor(QPalette::Highlight, QColor(t.selectionBg));
    pal.setColor(QPalette::HighlightedText, QColor(t.text));
    pal.setColor(QPalette::Link, QColor(t.accentHover));
    pal.setColor(QPalette::Mid, QColor(t.textDim));
    pal.setColor(QPalette::Dark, QColor(QStringLiteral("#b0b0b0")));
    pal.setColor(QPalette::Light, QColor(t.panelBg));
    pal.setColor(QPalette::Midlight, QColor(t.hoverBg));
    pal.setColor(QPalette::Shadow, QColor(t.border));
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
// QSS image: can only reference real files, not qrc SVGs with currentColor,
// so small SVGs are written on each theme apply.
// Tree icons: 12x22 matches ExplorerTree indent x row height (no image stretch).
// Other glyphs: 10x10 (CSS border triangles are unreliable in Qt QSS).
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

    // 12x22 matches ExplorerTree indentation (VS Code explorer density)
    const QString chevronRight = QStringLiteral(
        "<svg width='12' height='22' xmlns='http://www.w3.org/2000/svg'>"
        "<path d='M4.5 8 L8.5 11 L4.5 14' fill='none' stroke='%1' stroke-width='1.3' "
        "stroke-linecap='round' stroke-linejoin='round'/></svg>").arg(t.textDim);
    const QString chevronDown = QStringLiteral(
        "<svg width='12' height='22' xmlns='http://www.w3.org/2000/svg'>"
        "<path d='M3.5 9 L6 12.5 L8.5 9' fill='none' stroke='%1' stroke-width='1.3' "
        "stroke-linecap='round' stroke-linejoin='round'/></svg>").arg(t.textDim);
    // Thin gray indent guide (VS Code explorer) — 1px centered, very muted
    const QString indentGuide = QStringLiteral(
        "<svg width='12' height='22' xmlns='http://www.w3.org/2000/svg'>"
        "<rect x='5.5' y='0' width='1' height='22' fill='%1' fill-opacity='0.28'/>"
        "</svg>").arg(t.textDim);
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

    // Tab close X (QTabBar::close-button) — textDim idle / text on hover
    const auto smallX = [](const QString &color) {
        return QStringLiteral(
            "<svg width='12' height='12' xmlns='http://www.w3.org/2000/svg'>"
            "<path d='M3.2 3.2 L8.8 8.8 M8.8 3.2 L3.2 8.8' fill='none' stroke='%1' "
            "stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/>"
            "</svg>").arg(color);
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

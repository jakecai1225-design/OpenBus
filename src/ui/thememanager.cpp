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
    // ===== Light =====
    Theme light;
    light.name = "Light";
    light.windowBg = "#f8f8f8";  light.contentBg = "#ffffff";  light.sidebarBg = "#f3f3f3";
    light.panelBg = "#ececec";
    light.barBg = "#dddddd";      light.barFg = "#333333";      light.barHover = "#d0d0d0";
    light.barBorder = "#c4c4c4";
    light.activityBarBg = "#dcdcdc";  light.activityBarFg = "#5c5c5c";  light.activityBarHover = "#cfcfcf";
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

    // 仅保留 Light 一套配色 — 其余主题存在大量未覆盖的硬编码浅色区域
    // （黑一块白一块），在全面适配前不再提供
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

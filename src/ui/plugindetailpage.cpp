#include "plugindetailpage.h"
#include "core/plugin/pluginmanager.h"
#include "core/plugin/plugininfo.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QMessageBox>
#include <QIcon>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QSvgRenderer>
#include <QGuiApplication>

#include "utils/svg_icon.h"

// ============================================================
//  PluginUi::pluginIconPixmap — 图标加载 + 首字母头像兜底
// ============================================================

QPixmap PluginUi::pluginIconPixmap(const QString &iconPath, const QString &name, int size)
{
    if (!iconPath.isEmpty()) {
        if (iconPath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
            // SVG：手动经 QSvgRenderer 渲染（不依赖 imageformats 插件），按 DPR 放大保证清晰
            QFile f(iconPath);
            if (f.open(QIODevice::ReadOnly)) {
                QSvgRenderer renderer(f.readAll());
                if (renderer.isValid()) {
                    const qreal dpr = qApp->devicePixelRatio();
                    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
                    pm.setDevicePixelRatio(dpr);
                    pm.fill(Qt::transparent);
                    QPainter pt(&pm);
                    pt.setRenderHint(QPainter::Antialiasing);
                    renderer.render(&pt);
                    return pm;
                }
            }
        } else {
            QIcon icon(iconPath);
            if (!icon.isNull())
                return icon.pixmap(size, size);
        }
    }

    // 按名称哈希取色，绘制圆角首字母头像
    static const QColor palette[] = {
        QColor("#4ec9b0"), QColor("#569cd6"), QColor("#c586c0"), QColor("#dcdcaa"),
        QColor("#ce9178"), QColor("#6a9955"), QColor("#d7ba7d"), QColor("#9cdcfe"),
    };
    const int n = sizeof(palette) / sizeof(palette[0]);
    const QColor color = palette[int(qHash(name) % n)];

    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(0, 0, size, size, size * 0.22, size * 0.22);

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(10, int(size * 0.5)));
    p.setFont(f);
    p.setPen(Qt::white);
    const QString letter = name.isEmpty() ? QStringLiteral("?")
                                          : QString(name.at(0)).toUpper();
    p.drawText(pm.rect(), Qt::AlignCenter, letter);
    return pm;
}

// ============================================================
//  PluginDetailPage
// ============================================================

namespace {
// 清空布局：widget 走 hide + deleteLater；子布局递归清空后删除。
// 只删 QLayoutItem 包装不删子布局里的 widget，会导致 widget 残留在父容器里叠层显示。
void clearLayout(QLayout *layout)
{
    while (layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        } else if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *lbl = new QLabel(text, parent);
    QFont f = lbl->font();
    f.setBold(true);
    lbl->setFont(f);
    lbl->setStyleSheet("color: #ccc; padding-top: 8px;");
    return lbl;
}

void addInfoRow(QVBoxLayout *parentLay, const QString &key, const QString &value)
{
    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    auto *k = new QLabel(key);
    k->setStyleSheet("color: #888;");
    k->setMinimumWidth(64);
    auto *v = new QLabel(value.isEmpty() ? QStringLiteral("-") : value);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(k);
    row->addWidget(v, 1);
    parentLay->addLayout(row);
}

// VS Code "More Info" 风格：暗色小标签在上，值在下（用于右侧信息栏）
void addInfoField(QVBoxLayout *parentLay, QWidget *parent, const QString &key, const QString &value)
{
    auto *k = new QLabel(key, parent);
    k->setStyleSheet("color: #777; font-size: 11px;");
    auto *v = new QLabel(value.isEmpty() ? QStringLiteral("-") : value, parent);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    parentLay->addWidget(k);
    parentLay->addWidget(v);
    parentLay->addSpacing(8);
}
} // namespace

PluginDetailPage::PluginDetailPage(QWidget *parent)
    : QWidget(parent)
    , m_pm(PluginManager::instance())
{
    setObjectName("PluginDetailPage");
    buildUi();
}

void PluginDetailPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 头部：图标 + 名称/版本 + 作者 + 描述 + 操作按钮 ----
    auto *header = new QWidget(this);
    header->setObjectName("PluginDetailHeader");
    auto *hl = new QVBoxLayout(header);
    hl->setContentsMargins(16, 16, 16, 12);
    hl->setSpacing(6);

    auto *hRow = new QHBoxLayout;
    hRow->setSpacing(12);
    m_iconLabel = new QLabel(header);
    m_iconLabel->setFixedSize(64, 64);
    hRow->addWidget(m_iconLabel);

    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    m_titleLabel = new QLabel(header);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_titleLabel->setFont(titleFont);
    titleCol->addWidget(m_titleLabel);
    m_authorLabel = new QLabel(header);
    m_authorLabel->setStyleSheet("color: #888;");
    titleCol->addWidget(m_authorLabel);
    hRow->addLayout(titleCol, 1);
    hl->addLayout(hRow);

    m_descLabel = new QLabel(header);
    m_descLabel->setWordWrap(true);
    m_descLabel->setStyleSheet("color: #aaa;");
    hl->addWidget(m_descLabel);

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);
    m_activateBtn = new QPushButton(header);
    m_enableBtn = new QPushButton(header);
    m_uninstallBtn = new QPushButton(header);
    btnRow->addWidget(m_activateBtn);
    btnRow->addWidget(m_enableBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_uninstallBtn);
    hl->addLayout(btnRow);

    root->addWidget(header);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color: #333;");
    root->addWidget(sep);

    // ---- 正文：可滚动区 ----
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_body = new QWidget(scroll);
    m_body->setObjectName("PluginDetailBody");
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(16, 8, 16, 16);
    m_bodyLayout->setSpacing(4);
    m_bodyLayout->addStretch(0);
    scroll->setWidget(m_body);
    root->addWidget(scroll, 1);

    // ---- 信号 ----
    connect(m_pm, &PluginManager::pluginListChanged,
            this, &PluginDetailPage::onPluginListChanged);

    connect(m_activateBtn, &QPushButton::clicked, this, [this]() {
        if (m_name.isEmpty()) return;
        if (m_pm->isPluginActivated(m_name))
            m_pm->deactivatePlugin(m_name);
        else
            m_pm->activatePlugin(m_name);
    });

    connect(m_enableBtn, &QPushButton::clicked, this, [this]() {
        if (m_name.isEmpty()) return;
        m_pm->setPluginEnabled(m_name, !m_pm->isPluginEnabled(m_name));
    });

    connect(m_uninstallBtn, &QPushButton::clicked, this, [this]() {
        if (m_name.isEmpty()) return;
        const auto ret = QMessageBox::question(
            this, QStringLiteral("卸载插件"),
            QStringLiteral("确定卸载插件 “%1” 吗？插件目录将被删除。").arg(m_name));
        if (ret != QMessageBox::Yes)
            return;
        const QString err = m_pm->uninstallPlugin(m_name);
        if (!err.isEmpty())
            QMessageBox::warning(this, QStringLiteral("卸载失败"), err);
    });
}

void PluginDetailPage::showPlugin(const QString &name)
{
    if (m_name == name) {
        refresh();
        return;
    }
    m_name = name;
    refresh();
}

void PluginDetailPage::onPluginListChanged()
{
    refresh();
}

void PluginDetailPage::refresh()
{
    const QList<PluginInfo> plugins = m_pm->discoveredPlugins();
    const PluginInfo *found = nullptr;
    for (const auto &info : plugins) {
        if (info.name == m_name) {
            found = &info;
            break;
        }
    }

    // 插件不存在（未选择或已卸载）
    if (!found) {
        m_iconLabel->setPixmap(PluginUi::pluginIconPixmap(QString(), m_name, 64));
        m_titleLabel->setText(m_name.isEmpty() ? QStringLiteral("未选择插件")
                                               : m_name);
        m_authorLabel->clear();
        m_descLabel->setText(m_name.isEmpty()
                                 ? QStringLiteral("在扩展面板中点击插件查看详情")
                                 : QStringLiteral("插件不存在或已卸载"));
        m_activateBtn->setEnabled(false);
        m_enableBtn->setEnabled(false);
        m_uninstallBtn->setEnabled(false);
        rebuildBody();
        return;
    }

    const bool enabled = m_pm->isPluginEnabled(found->name);
    const bool activated = m_pm->isPluginActivated(found->name);

    m_iconLabel->setPixmap(PluginUi::pluginIconPixmap(found->iconFilePath(),
                                                      found->name, 64));
    m_titleLabel->setText(QStringLiteral("%1  %2").arg(found->name, found->version));
    m_authorLabel->setText(found->author.isEmpty()
                               ? QStringLiteral("未知作者")
                               : found->author);
    m_descLabel->setText(found->description);

    m_activateBtn->setEnabled(enabled);
    m_activateBtn->setText(activated ? QStringLiteral("停止") : QStringLiteral("启动"));
    m_enableBtn->setText(enabled ? QStringLiteral("禁用") : QStringLiteral("启用"));
    m_uninstallBtn->setEnabled(true);

    rebuildBody();
}

void PluginDetailPage::rebuildBody()
{
    clearLayout(m_bodyLayout);

    if (m_name.isEmpty()) {
        m_bodyLayout->addStretch();
        return;
    }

    const QList<PluginInfo> plugins = m_pm->discoveredPlugins();
    const PluginInfo *found = nullptr;
    for (const auto &info : plugins) {
        if (info.name == m_name) {
            found = &info;
            break;
        }
    }
    if (!found) {
        m_bodyLayout->addStretch();
        return;
    }

    const bool enabled = m_pm->isPluginEnabled(found->name);
    const bool activated = m_pm->isPluginActivated(found->name);

    // ---- VS Code 双栏：左主列（说明/命令/文件格式）+ 右信息栏（详细信息） ----
    auto *columns = new QHBoxLayout;
    columns->setSpacing(24);

    // 左主列
    auto *mainCol = new QVBoxLayout;
    mainCol->setSpacing(4);

    if (!found->description.isEmpty()) {
        mainCol->addWidget(makeSectionLabel(QStringLiteral("说明"), m_body));
        auto *desc = new QLabel(found->description, m_body);
        desc->setWordWrap(true);
        desc->setTextInteractionFlags(Qt::TextSelectableByMouse);
        desc->setStyleSheet("color: #bbb; padding-bottom: 8px;");
        mainCol->addWidget(desc);
    }

    // 命令
    if (!found->commands.isEmpty()) {
        mainCol->addWidget(makeSectionLabel(
            QStringLiteral("命令 (%1)").arg(found->commands.size()), m_body));
        for (const auto &cmd : found->commands) {
            auto *btn = new QPushButton(cmd.title, m_body);
            btn->setFlat(true);
            btn->setToolTip(cmd.id);
            btn->setIcon(svgIcon(QStringLiteral(":/icons/play.svg"),
                                 QStringLiteral("#4ec9b0"), 14));
            btn->setIconSize(QSize(14, 14));
            btn->setStyleSheet("text-align: left; padding: 2px 0; color: #4ec9b0;");
            const QString id = cmd.id;
            connect(btn, &QPushButton::clicked, this, [this, id]() {
                m_pm->executeCommand(id);
            });
            mainCol->addWidget(btn);
        }
    }

    // 文件格式
    if (!found->fileFormats.isEmpty()) {
        mainCol->addWidget(makeSectionLabel(
            QStringLiteral("支持的文件格式 (%1)").arg(found->fileFormats.size()), m_body));
        for (const auto &fmt : found->fileFormats) {
            addInfoRow(mainCol, fmt.extension,
                       fmt.name.isEmpty() ? QStringLiteral("-") : fmt.name);
        }
    }
    mainCol->addStretch();

    // 右信息栏（More Info）
    auto *sideWrap = new QWidget(m_body);
    sideWrap->setFixedWidth(280);
    auto *sideCol = new QVBoxLayout(sideWrap);
    sideCol->setContentsMargins(0, 0, 0, 0);
    sideCol->setSpacing(4);
    sideCol->addWidget(makeSectionLabel(QStringLiteral("详细信息"), sideWrap));
    addInfoField(sideCol, sideWrap, QStringLiteral("标识符"), found->name);
    addInfoField(sideCol, sideWrap, QStringLiteral("版本"), found->version);
    addInfoField(sideCol, sideWrap, QStringLiteral("作者"),
                 found->author.isEmpty() ? QString() : found->author);
    addInfoField(sideCol, sideWrap, QStringLiteral("状态"),
                 !enabled ? QStringLiteral("已禁用")
                          : activated ? QStringLiteral("已启用 · 运行中")
                                      : QStringLiteral("已启用 · 未运行"));
    if (!found->activationEvents.isEmpty())
        addInfoField(sideCol, sideWrap, QStringLiteral("激活事件"),
                     found->activationEvents.join(", "));
    addInfoField(sideCol, sideWrap, QStringLiteral("目录"),
                 QDir::toNativeSeparators(found->directory));
    sideCol->addStretch();

    // 竖分隔线
    auto *vline = new QFrame(m_body);
    vline->setFrameShape(QFrame::VLine);
    vline->setFrameShadow(QFrame::Plain);
    vline->setStyleSheet("color: #333;");
    vline->setFixedWidth(1);

    columns->addLayout(mainCol, 1);
    columns->addWidget(vline);
    columns->addWidget(sideWrap);
    m_bodyLayout->addLayout(columns);
    m_bodyLayout->addStretch();
}

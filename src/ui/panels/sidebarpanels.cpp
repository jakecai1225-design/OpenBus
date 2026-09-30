#include "sidebarpanels.h"
#include "core/signalrelay.h"   // DEF-08：字符串信号 → lambda 桥接
#include "core/protocol/protocolregistry.h"   // M2：Flow 模板行注册表枚举（doc/flow.md §13.3）
#include "core/protocol/iprotocoladapter.h"   // M2：适配器接口完整类型（枚举访问）
#include "utils/svg_icon.h"
#include "core/dbcmanager.h"
#include "core/cansimulator.h"
#include "core/candevicemanager.h"
#include "core/candevice.h"
#include "core/driver/driverregistry.h"
#include "core/appconfig.h"
#include "core/sessionmanager.h"
// ui/graphicview.h 已移除 — Graphic 视图经 ModuleRegistry "graphic" 模块操控（B5-5）
#include "ui/thememanager.h"
#include "core/marketmodel.h"
#include "core/driver/marketindex.h"
#include "core/plugin/pluginmanager.h"

#include <nlohmann/json.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QTreeWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QStyle>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QToolButton>
#include <QPointer>
#include <QScrollArea>
#include <QProcess>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QSizePolicy>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QIcon>
#include <QCursor>
#include <QShowEvent>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QBrush>
#include <QFont>

// ============================================================
//  SidePanel 基类
// ============================================================

SidePanel::SidePanel(const QString &title, QWidget *parent)
    : QWidget(parent), m_contentLayout(nullptr)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setupTitle(title);
}

void SidePanel::setupTitle(const QString &title)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *titleBar = new QLabel(title, this);
    titleBar->setObjectName("SidePanelTitle");
    titleBar->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(titleBar);

    auto *contentWidget = new QWidget(this);
    m_contentLayout = new QVBoxLayout(contentWidget);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);
    layout->addWidget(contentWidget, 1);
}

// ============================================================
//  ExplorerItemRow / ExplorerItemActions — VS Code item actions
// ============================================================

ExplorerItemRow::ExplorerItemRow(const QString &text, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ExplorerItemRow"));
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(22);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 0, 4, 0);
    lay->setSpacing(4);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(16, 16);
    m_iconLabel->setVisible(false);
    lay->addWidget(m_iconLabel, 0, Qt::AlignVCenter);

    m_textLabel = new QLabel(text, this);
    m_textLabel->setObjectName(QStringLiteral("ExplorerItemLabel"));
    m_textLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    lay->addWidget(m_textLabel, 1);

    m_actionsHost = new QWidget(this);
    m_actionsHost->setObjectName(QStringLiteral("ExplorerItemActions"));
    m_actionsLay = new QHBoxLayout(m_actionsHost);
    m_actionsLay->setContentsMargins(0, 0, 0, 0);
    m_actionsLay->setSpacing(0);
    lay->addWidget(m_actionsHost, 0, Qt::AlignVCenter);

    setActionsVisible(false);
}

void ExplorerItemRow::setText(const QString &text)
{
    m_textLabel->setText(text);
}

void ExplorerItemRow::setLeadingIcon(const QIcon &icon)
{
    if (icon.isNull()) {
        m_iconLabel->clear();
        m_iconLabel->setVisible(false);
        return;
    }
    m_iconLabel->setPixmap(icon.pixmap(16, 16));
    m_iconLabel->setVisible(true);
}

QToolButton *ExplorerItemRow::addAction(const QString &iconPath,
                                        const QString &tooltip,
                                        int iconSize)
{
    auto *btn = new QToolButton(m_actionsHost);
    btn->setObjectName(QStringLiteral("ExplorerSectionAction"));
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFixedSize(16, 16);
    btn->setIconSize(QSize(iconSize > 0 ? qMin(iconSize, 12) : 12,
                           iconSize > 0 ? qMin(iconSize, 12) : 12));
    btn->setIcon(svgIcon(iconPath, ThemeManager::instance()->currentTheme().text,
                         iconSize > 0 ? qMin(iconSize, 12) : 12));
    m_actionsLay->addWidget(btn, 0, Qt::AlignVCenter);
    m_actions.append(btn);
    m_actionIconPaths.append(iconPath);
    updateActionVisibility();
    return btn;
}

void ExplorerItemRow::setActionsVisible(bool on)
{
    m_hovered = on;
    updateActionVisibility();
}

void ExplorerItemRow::refreshTheme()
{
    const QString c = ThemeManager::instance()->currentTheme().text;
    for (int i = 0; i < m_actions.size(); ++i) {
        const int sz = m_actions[i]->iconSize().width();
        m_actions[i]->setIcon(svgIcon(m_actionIconPaths.value(i), c, sz > 0 ? sz : 12));
    }
}

void ExplorerItemRow::updateActionVisibility()
{
    bool force = false;
    for (auto *b : m_actions) {
        if (b->isDown()) {
            force = true;
            break;
        }
    }
    const bool show = m_hovered || force;
    for (auto *b : m_actions)
        b->setVisible(show);
}

void ExplorerItemRow::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    setActionsVisible(true);
}

void ExplorerItemRow::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    const QPoint g = QCursor::pos();
    if (!rect().contains(mapFromGlobal(g)))
        setActionsVisible(false);
}

void ExplorerItemRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        && !m_actionsHost->geometry().contains(event->pos())) {
        emit activated();
    }
    QWidget::mouseReleaseEvent(event);
}

ExplorerItemActions::ExplorerItemActions(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ExplorerItemActions"));
    setAttribute(Qt::WA_Hover, true);
    m_lay = new QHBoxLayout(this);
    m_lay->setContentsMargins(0, 0, 2, 0);
    m_lay->setSpacing(0);
    setActionsVisible(false);
}

QToolButton *ExplorerItemActions::addAction(const QString &iconPath,
                                            const QString &tooltip,
                                            int iconSize)
{
    auto *btn = new QToolButton(this);
    btn->setObjectName(QStringLiteral("ExplorerSectionAction"));
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFixedSize(20, 20);
    btn->setIconSize(QSize(iconSize, iconSize));
    btn->setIcon(svgIcon(iconPath, ThemeManager::instance()->currentTheme().text, iconSize));
    m_lay->addWidget(btn, 0, Qt::AlignVCenter);
    m_actions.append(btn);
    m_actionIconPaths.append(iconPath);
    setFixedWidth(qMax(20, m_actions.size() * 20));
    setActionsVisible(m_hovered);
    return btn;
}

void ExplorerItemActions::setActionsVisible(bool on)
{
    m_hovered = on;
    bool force = false;
    for (auto *b : m_actions) {
        if (b->isDown()) {
            force = true;
            break;
        }
    }
    const bool show = on || force;
    for (auto *b : m_actions)
        b->setVisible(show);
}

void ExplorerItemActions::refreshTheme()
{
    const QString c = ThemeManager::instance()->currentTheme().text;
    for (int i = 0; i < m_actions.size(); ++i) {
        const int sz = m_actions[i]->iconSize().width();
        m_actions[i]->setIcon(svgIcon(m_actionIconPaths.value(i), c, sz > 0 ? sz : 12));
    }
}

void ExplorerItemActions::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    setActionsVisible(true);
}

void ExplorerItemActions::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    setActionsVisible(false);
}

// ============================================================
//  ExplorerSection — VS Code twistie section
// ============================================================

ExplorerSection::ExplorerSection(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ExplorerSection"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_headerRow = new QWidget(this);
    m_headerRow->setObjectName(QStringLiteral("ExplorerSectionHeaderRow"));
    m_headerRow->setFixedHeight(22);
    m_headerLay = new QHBoxLayout(m_headerRow);
    m_headerLay->setContentsMargins(0, 0, 4, 0);
    m_headerLay->setSpacing(2);

    m_header = new QToolButton(m_headerRow);
    m_header->setObjectName(QStringLiteral("ExplorerSectionHeader"));
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setAutoRaise(true);
    m_header->setFixedHeight(22);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setFocusPolicy(Qt::NoFocus);
    connect(m_header, &QToolButton::clicked, this, &ExplorerSection::toggle);
    m_headerLay->addWidget(m_header, 1);

    m_statusDot = new QLabel(QStringLiteral("●"), m_headerRow);
    m_statusDot->setObjectName(QStringLiteral("ExplorerSectionStatusDot"));
    m_statusDot->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_statusDot->setStyleSheet(QStringLiteral(
        "QLabel#ExplorerSectionStatusDot { color: #22C55E; font-size: 11px; }"));
    m_statusDot->setVisible(false);
    m_headerLay->addWidget(m_statusDot, 0, Qt::AlignVCenter);

    m_actionsHost = new QWidget(m_headerRow);
    m_actionsHost->setObjectName(QStringLiteral("ExplorerSectionActions"));
    m_actionsLay = new QHBoxLayout(m_actionsHost);
    m_actionsLay->setContentsMargins(0, 0, 0, 0);
    m_actionsLay->setSpacing(0);
    m_headerLay->addWidget(m_actionsHost, 0, Qt::AlignVCenter);

    lay->addWidget(m_headerRow, 0);

    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("ExplorerSectionBody"));
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(0);
    lay->addWidget(m_body, 1);

    setTitle(title);
    applyExpandPolicy();
    updateHeaderChrome();
    updateHeaderActionsVisibility();
    // Parent layout may not be ready yet; rebalance once shown / next tick.
    QTimer::singleShot(0, this, &ExplorerSection::rebalanceSiblings);
}

void ExplorerSection::setTitle(const QString &title)
{
    m_header->setText(title.toUpper());
    m_header->setToolTip(title);
}

QString ExplorerSection::title() const
{
    return m_header->text();
}

void ExplorerSection::setExpanded(bool expanded)
{
    if (m_expanded == expanded)
        return;
    m_expanded = expanded;
    m_body->setVisible(m_expanded);
    applyExpandPolicy();
    updateHeaderChrome();
    updateHeaderActionsVisibility();
    rebalanceSiblings();
    emit expandedChanged(m_expanded);
}

void ExplorerSection::toggle()
{
    setExpanded(!m_expanded);
}

void ExplorerSection::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    rebalanceSiblings();
}

void ExplorerSection::rebalanceSiblings()
{
    QWidget *p = parentWidget();
    if (!p)
        return;
    auto *lay = qobject_cast<QVBoxLayout *>(p->layout());
    if (!lay)
        return;

    QList<ExplorerSection *> sections;
    QWidget *tail = nullptr;
    for (int i = 0; i < lay->count(); ++i) {
        QLayoutItem *it = lay->itemAt(i);
        if (!it)
            continue;
        if (auto *s = qobject_cast<ExplorerSection *>(it->widget()))
            sections.append(s);
        else if (auto *w = it->widget()) {
            if (w->objectName() == QLatin1String("ExplorerSectionTailSpacer"))
                tail = w;
        }
    }
    if (sections.isEmpty())
        return;

    int expandedCount = 0;
    for (auto *s : sections) {
        if (s->isExpanded())
            ++expandedCount;
    }

    // Expanded sections share leftover height equally (stretch 1).
    // Collapsed keep fixed header height (stretch 0).
    // Leading collapsed stay top; trailing collapsed are pushed to the bottom
    // by the expanded stretch — VS Code view-container packing.
    for (auto *s : sections) {
        const int idx = lay->indexOf(s);
        if (idx < 0)
            continue;
        lay->setStretch(idx, s->isExpanded() ? 1 : 0);
        s->applyExpandPolicy();
    }

    if (expandedCount == 0) {
        // All collapsed → pack to the top; absorb leftover below.
        if (!tail) {
            tail = new QWidget(p);
            tail->setObjectName(QStringLiteral("ExplorerSectionTailSpacer"));
            tail->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
            tail->setMinimumHeight(0);
            lay->addWidget(tail, 1);
        } else {
            tail->setVisible(true);
            tail->setMaximumHeight(QWIDGETSIZE_MAX);
            lay->setStretch(lay->indexOf(tail), 1);
        }
    } else if (tail) {
        // Expanded views own the leftover space; hide the tail spacer.
        const int idx = lay->indexOf(tail);
        if (idx >= 0)
            lay->setStretch(idx, 0);
        tail->setMaximumHeight(0);
        tail->setVisible(false);
    }

    p->updateGeometry();
}

void ExplorerSection::refreshTheme()
{
    updateHeaderChrome();
    refreshActionIcons();
}

void ExplorerSection::setStatusDotVisible(bool on)
{
    if (m_statusDot)
        m_statusDot->setVisible(on);
}

QToolButton *ExplorerSection::addHeaderAction(const QString &iconPath,
                                             const QString &tooltip,
                                             int iconSize)
{
    auto *btn = new QToolButton(m_actionsHost);
    btn->setObjectName(QStringLiteral("ExplorerSectionAction"));
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setToolTip(tooltip);
    btn->setFixedSize(16, 16);
    btn->setIconSize(QSize(iconSize > 0 ? qMin(iconSize, 12) : 12,
                           iconSize > 0 ? qMin(iconSize, 12) : 12));
    btn->setIcon(svgIcon(iconPath, ThemeManager::instance()->currentTheme().text,
                         iconSize > 0 ? qMin(iconSize, 12) : 12));
    m_actionsLay->addWidget(btn, 0, Qt::AlignVCenter);
    m_actions.append(btn);
    m_actionIconPaths.append(iconPath);
    updateHeaderActionsVisibility();
    return btn;
}

void ExplorerSection::applyExpandPolicy()
{
    if (m_expanded) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumHeight(0);
    } else {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        const int h = m_headerRow ? m_headerRow->sizeHint().height() : 22;
        setMaximumHeight(qMax(22, h));
        setMinimumHeight(0);
    }
    updateGeometry();
    if (parentWidget())
        parentWidget()->updateGeometry();
}

void ExplorerSection::updateHeaderActionsVisibility()
{
    // Expanded section = active: keep trailing actions visible.
    // Collapsed: hide them.
    for (auto *b : m_actions)
        b->setVisible(m_expanded);
    if (m_actionsHost)
        m_actionsHost->setVisible(m_expanded && !m_actions.isEmpty());
}

void ExplorerSection::refreshActionIcons()
{
    const QString c = ThemeManager::instance()->currentTheme().text;
    for (int i = 0; i < m_actions.size(); ++i) {
        const int sz = m_actions[i]->iconSize().width();
        m_actions[i]->setIcon(svgIcon(m_actionIconPaths.value(i), c, sz > 0 ? sz : 14));
    }
}

void ExplorerSection::updateHeaderChrome()
{
    const auto &th = ThemeManager::instance()->currentTheme();
    const QString icon = m_expanded ? QStringLiteral(":/icons/chevron-down.svg")
                                    : QStringLiteral(":/icons/chevron-right.svg");
    m_header->setIcon(svgIcon(icon, th.textDim, kIconSm));
    m_header->setStyleSheet(QString());
}

namespace {

/// Show ExplorerItemActions in column 1 while the mouse is over that tree row.
class TreeRowActionHoverFilter : public QObject
{
public:
    explicit TreeRowActionHoverFilter(QTreeWidget *tree)
        : QObject(tree), m_tree(tree)
    {
        tree->setMouseTracking(true);
        if (tree->viewport()) {
            tree->viewport()->setMouseTracking(true);
            tree->viewport()->installEventFilter(this);
        }
        tree->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_tree->viewport()) {
            if (event->type() == QEvent::MouseMove) {
                auto *me = static_cast<QMouseEvent *>(event);
                setHoverItem(m_tree->itemAt(me->pos()));
            } else if (event->type() == QEvent::Leave) {
                setHoverItem(nullptr);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void setHoverItem(QTreeWidgetItem *item)
    {
        if (item == m_last)
            return;
        if (m_last) {
            if (auto *w = qobject_cast<ExplorerItemActions *>(
                    m_tree->itemWidget(m_last, 1)))
                w->setActionsVisible(false);
        }
        m_last = item;
        if (m_last) {
            if (auto *w = qobject_cast<ExplorerItemActions *>(
                    m_tree->itemWidget(m_last, 1)))
                w->setActionsVisible(true);
        }
    }

    QTreeWidget *m_tree = nullptr;
    QTreeWidgetItem *m_last = nullptr;
};

void installTreeRowActionHover(QTreeWidget *tree)
{
    if (!tree || tree->property("_explorerActionHover").toBool())
        return;
    tree->setProperty("_explorerActionHover", true);
    new TreeRowActionHoverFilter(tree);
}

} // namespace

// ============================================================
//  ProjectPanel
// ============================================================

ProjectPanel::ProjectPanel(QWidget *parent)
    : SidePanel("EXPLORER", parent)
{
    auto *cl = contentLayout();

    m_projectSection = new ExplorerSection(QStringLiteral("Open Project"), this);
    auto *newProjBtn = m_projectSection->addHeaderAction(
        QStringLiteral(":/icons/plus.svg"),
        QStringLiteral("New Project"));
    auto *openProjBtn = m_projectSection->addHeaderAction(
        QStringLiteral(":/icons/folder.svg"),
        QStringLiteral("Open Project"));
    connect(newProjBtn, &QToolButton::clicked, this, &ProjectPanel::onNewProject);
    connect(openProjBtn, &QToolButton::clicked, this, &ProjectPanel::onOpenProject);

    m_projectTree = new QTreeWidget(m_projectSection->bodyWidget());
    applyExplorerTree(m_projectTree, QStringLiteral("ProjectTree"));
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setColumnCount(2);
    m_projectTree->setRootIsDecorated(true);
    m_projectTree->header()->setStretchLastSection(false);
    m_projectTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_projectTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_projectTree->setColumnWidth(1, 44);
    m_projectTree->setExpandsOnDoubleClick(false);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    installTreeRowActionHover(m_projectTree);
    m_projectSection->bodyLayout()->addWidget(m_projectTree, 1);
    cl->addWidget(m_projectSection, 1);

    m_recentSection = new ExplorerSection(QStringLiteral("Recent"), this);
    auto *clearRecentBtn = m_recentSection->addHeaderAction(
        QStringLiteral(":/icons/clear-all.svg"),
        QStringLiteral("Clear Recent"));
    connect(clearRecentBtn, &QToolButton::clicked, this, [this]() {
        SessionManager::instance()->clearRecent();
        refreshRecentList();
    });
    m_recentList = new QListWidget(m_recentSection->bodyWidget());
    m_recentList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_recentList->setFrameShape(QFrame::NoFrame);
    m_recentList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_recentList->setMouseTracking(true);
    m_recentSection->bodyLayout()->addWidget(m_recentList, 1);
    cl->addWidget(m_recentSection, 1);

    ProjectContext defaultProj;
    defaultProj.name = QStringLiteral("Default Project");
    m_projects.append(defaultProj);
    m_currentIndex = 0;
    refreshList();
    refreshRecentList();

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        if (m_projectSection)
            m_projectSection->refreshTheme();
        if (m_recentSection)
            m_recentSection->refreshTheme();
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));

    connect(m_projectTree, &QTreeWidget::itemClicked,
            this, &ProjectPanel::onProjectItemClicked);
    connect(m_projectTree, &QTreeWidget::itemDoubleClicked,
            this, &ProjectPanel::onProjectItemDoubleClicked);
    connect(m_projectTree, &QWidget::customContextMenuRequested,
            this, &ProjectPanel::onProjectTreeContextMenu);
    connect(m_recentList, &QWidget::customContextMenuRequested,
            this, &ProjectPanel::onRecentListContextMenu);
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            emit openProjectRequested(path);
    });
}

void ProjectPanel::updateProjectSectionTitle()
{
    if (!m_projectSection)
        return;
    m_projectSection->setTitle(QStringLiteral("Open Project"));
    const bool hasActive = (m_currentIndex >= 0 && m_currentIndex < m_projects.size());
    m_projectSection->setStatusDotVisible(hasActive);
}

void ProjectPanel::activateProject(const QString &filePath, const QString &name)
{
    if (filePath.isEmpty())
        return;

    const QString resolvedName = name.isEmpty()
                                     ? QFileInfo(filePath).completeBaseName()
                                     : name;

    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects[i].filePath == filePath) {
            m_currentIndex = i;
            m_projects[i].name = resolvedName;
            refreshList();
            return;
        }
    }

    // Reuse lone unsaved placeholder instead of stacking another root
    if (m_projects.size() == 1 && m_projects[0].filePath.isEmpty()) {
        m_projects[0].filePath = filePath;
        m_projects[0].name = resolvedName;
        m_currentIndex = 0;
        refreshList();
        return;
    }

    ProjectContext proj;
    proj.filePath = filePath;
    proj.name = resolvedName;
    m_projects.append(proj);
    m_currentIndex = m_projects.size() - 1;
    refreshList();
}

void ProjectPanel::refreshList()
{
    m_projectTree->blockSignals(true);
    m_projectTree->clear();

    const auto &th = ThemeManager::instance()->currentTheme();
    const QIcon iconFolder = svgIcon(QStringLiteral(":/icons/folder.svg"), th.textDim, 16);
    const QIcon iconFile   = svgIcon(QStringLiteral(":/icons/file.svg"), th.textDim, 16);
    const QIcon iconDb     = svgIcon(QStringLiteral(":/icons/database.svg"), th.text, 16);

    updateProjectSectionTitle();

    for (int i = 0; i < m_projects.size(); ++i) {
        const auto &proj = m_projects[i];
        QString label = proj.name.isEmpty() ? QStringLiteral("Untitled") : proj.name;
        const bool active = (i == m_currentIndex);

        auto *projItem = new QTreeWidgetItem(m_projectTree, {label});
        projItem->setIcon(0, iconFolder);
        projItem->setData(0, Qt::UserRole, i);
        projItem->setExpanded(active);
        if (active) {
            // Trailing green ● only (col 1); name stays plain
            projItem->setText(1, QStringLiteral("●"));
            projItem->setForeground(1, QBrush(QColor(0x22, 0xC5, 0x5E)));
            projItem->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            projItem->setToolTip(0, QStringLiteral("Active project"));
        }

        if (!proj.filePath.isEmpty()) {
            auto *fItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("[Project] ") + QFileInfo(proj.filePath).fileName()});
            fItem->setIcon(0, iconFile);
            fItem->setData(0, Qt::UserRole, proj.filePath);
            fItem->setToolTip(0, proj.filePath);
            auto *acts = new ExplorerItemActions(m_projectTree);
            auto *revealBtn = acts->addAction(QStringLiteral(":/icons/folder.svg"),
                                              QStringLiteral("Reveal in File Explorer"));
            auto *openBtn = acts->addAction(QStringLiteral(":/icons/goto.svg"),
                                            QStringLiteral("Open project file"));
            const QString path = proj.filePath;
            connect(revealBtn, &QToolButton::clicked, this, [path]() {
                revealInFileManager(path);
            });
            connect(openBtn, &QToolButton::clicked, this, [this, path]() {
                emit openProjectRequested(path);
            });
            m_projectTree->setItemWidget(fItem, 1, acts);
        }

        QString playback = extractPlaybackFile(proj.stateJson);
        if (!playback.isEmpty()) {
            auto *fItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("[Playback] ") + QFileInfo(playback).fileName()});
            fItem->setIcon(0, iconFile);
            fItem->setData(0, Qt::UserRole, playback);
            fItem->setToolTip(0, playback);
            auto *acts = new ExplorerItemActions(m_projectTree);
            auto *revealBtn = acts->addAction(QStringLiteral(":/icons/folder.svg"),
                                              QStringLiteral("Reveal in File Explorer"));
            auto *previewBtn = acts->addAction(QStringLiteral(":/icons/goto.svg"),
                                               QStringLiteral("Preview"));
            connect(revealBtn, &QToolButton::clicked, this, [playback]() {
                revealInFileManager(playback);
            });
            connect(previewBtn, &QToolButton::clicked, this, [this, playback]() {
                emit filePreviewRequested(playback);
            });
            m_projectTree->setItemWidget(fItem, 1, acts);
        }

        auto dbcFiles = extractDbcFiles(proj.stateJson);
        if (!dbcFiles.isEmpty()) {
            auto *catItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("DBC (") + QString::number(dbcFiles.size()) + QStringLiteral(")")});
            catItem->setIcon(0, iconFolder);
            catItem->setExpanded(i == m_currentIndex);
            for (const auto &f : dbcFiles) {
                auto *fItem = new QTreeWidgetItem(catItem, {QFileInfo(f).fileName()});
                fItem->setIcon(0, iconDb);
                fItem->setData(0, Qt::UserRole, f);
                fItem->setToolTip(0, f);
                auto *acts = new ExplorerItemActions(m_projectTree);
                auto *revealBtn = acts->addAction(QStringLiteral(":/icons/folder.svg"),
                                                  QStringLiteral("Reveal in File Explorer"));
                auto *previewBtn = acts->addAction(QStringLiteral(":/icons/goto.svg"),
                                                   QStringLiteral("Preview"));
                connect(revealBtn, &QToolButton::clicked, this, [f]() {
                    revealInFileManager(f);
                });
                connect(previewBtn, &QToolButton::clicked, this, [this, f]() {
                    emit filePreviewRequested(f);
                });
                m_projectTree->setItemWidget(fItem, 1, acts);
            }
        }

        auto recFiles = extractRecordFiles(proj.stateJson);
        for (const auto &f : proj.recordFiles) {
            if (!recFiles.contains(f))
                recFiles << f;
        }
        if (!recFiles.isEmpty()) {
            auto *catItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("Recordings (") + QString::number(recFiles.size())
                 + QStringLiteral(")")});
            catItem->setIcon(0, iconFolder);
            catItem->setExpanded(i == m_currentIndex);
            for (const auto &f : recFiles) {
                auto *fItem = new QTreeWidgetItem(catItem, {QFileInfo(f).fileName()});
                fItem->setIcon(0, iconFile);
                fItem->setData(0, Qt::UserRole, f);
                fItem->setToolTip(0, f);
                auto *acts = new ExplorerItemActions(m_projectTree);
                auto *revealBtn = acts->addAction(QStringLiteral(":/icons/folder.svg"),
                                                  QStringLiteral("Reveal in File Explorer"));
                auto *previewBtn = acts->addAction(QStringLiteral(":/icons/goto.svg"),
                                                   QStringLiteral("Preview"));
                connect(revealBtn, &QToolButton::clicked, this, [f]() {
                    revealInFileManager(f);
                });
                connect(previewBtn, &QToolButton::clicked, this, [this, f]() {
                    emit filePreviewRequested(f);
                });
                m_projectTree->setItemWidget(fItem, 1, acts);
            }
        }

        auto offlineFiles = extractOfflineFiles(proj.stateJson);
        if (!offlineFiles.isEmpty()) {
            auto *catItem = new QTreeWidgetItem(projItem,
                {QStringLiteral("Offline (") + QString::number(offlineFiles.size())
                 + QStringLiteral(")")});
            catItem->setIcon(0, iconFolder);
            catItem->setExpanded(i == m_currentIndex);
            for (const auto &f : offlineFiles) {
                auto *fItem = new QTreeWidgetItem(catItem, {QFileInfo(f).fileName()});
                fItem->setIcon(0, iconFile);
                fItem->setData(0, Qt::UserRole, f);
                fItem->setToolTip(0, f);
                auto *acts = new ExplorerItemActions(m_projectTree);
                auto *revealBtn = acts->addAction(QStringLiteral(":/icons/folder.svg"),
                                                  QStringLiteral("Reveal in File Explorer"));
                auto *previewBtn = acts->addAction(QStringLiteral(":/icons/goto.svg"),
                                                   QStringLiteral("Preview"));
                connect(revealBtn, &QToolButton::clicked, this, [f]() {
                    revealInFileManager(f);
                });
                connect(previewBtn, &QToolButton::clicked, this, [this, f]() {
                    emit filePreviewRequested(f);
                });
                m_projectTree->setItemWidget(fItem, 1, acts);
            }
        }

        if (proj.filePath.isEmpty() && playback.isEmpty() &&
            dbcFiles.isEmpty() && recFiles.isEmpty() && offlineFiles.isEmpty()) {
            auto *empty = new QTreeWidgetItem(projItem,
                {QStringLiteral("(no linked files)")});
            empty->setFlags(Qt::NoItemFlags);
        }
    }

    if (m_currentIndex >= 0 && m_currentIndex < m_projectTree->topLevelItemCount())
        m_projectTree->setCurrentItem(m_projectTree->topLevelItem(m_currentIndex));
    m_projectTree->blockSignals(false);
}

void ProjectPanel::onNewProject()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "新建工程",
        "工程名称:", QLineEdit::Normal,
        QString("工程 %1").arg(m_projects.size() + 1), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    ProjectContext proj;
    proj.name = name.trimmed();
    m_projects.append(proj);
    m_currentIndex = m_projects.size() - 1;
    refreshList();
    emit projectCreated(proj.name);
    // 不再 emit projectSwitched — onProjectCreated 已处理全部逻辑
    // 避免 newProject 被重复调用导致工程状态反复重置
}

void ProjectPanel::onSaveProject()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_projects.size()) return;

    QString path = m_projects[m_currentIndex].filePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
            this, "保存工程", m_projects[m_currentIndex].name + ".openbusproj",
            "openbus 工程文件 (*.openbusproj);;所有文件 (*.*)");
        if (path.isEmpty()) return;
    }

    m_projects[m_currentIndex].filePath = path;
    emit saveProjectRequested(path);
    refreshRecentList();
}

void ProjectPanel::onOpenProject()
{
    QString path = QFileDialog::getOpenFileName(
        this, "打开工程", {},
        "openbus 工程文件 (*.openbusproj);;所有文件 (*.*)");
    if (path.isEmpty()) return;

    // 添加到工程列表
    QFileInfo fi(path);
    ProjectContext proj;
    proj.name = fi.baseName();
    proj.filePath = path;

    // 检查是否已存在
    for (int i = 0; i < m_projects.size(); ++i) {
        if (m_projects[i].filePath == path) {
            m_currentIndex = i;
            refreshList();
            emit projectSwitched(m_currentIndex);
            return;
        }
    }

    m_projects.append(proj);
    m_currentIndex = m_projects.size() - 1;
    refreshList();
    emit openProjectRequested(path);
}

void ProjectPanel::onOpenRecent()
{
    // 由 m_recentList 的 itemDoubleClicked 直接处理
}

void ProjectPanel::refreshRecentList()
{
    if (!m_recentList) return;
    m_recentList->clear();

    auto items = SessionManager::instance()->recentItems();
    for (const auto &var : items) {
        auto map = var.toMap();
        QString p = map.value("path").toString();
        if (p.isEmpty()) continue;
        QString name = map.value("name").toString();
        if (name.isEmpty())
            name = QFileInfo(p).completeBaseName();
        if (map.value("pinned").toBool())
            name = QStringLiteral("[Pinned] ") + name;

        auto *listItem = new QListWidgetItem(m_recentList);
        listItem->setData(Qt::UserRole, p);
        listItem->setToolTip(p);

        auto *row = new ExplorerItemRow(name, m_recentList);
        row->setLeadingIcon(svgIcon(QStringLiteral(":/icons/file.svg"),
                                    ThemeManager::instance()->currentTheme().textDim, 14));
        auto *openBtn = row->addAction(QStringLiteral(":/icons/goto.svg"),
                                       QStringLiteral("Open"));
        auto *revealBtn = row->addAction(QStringLiteral(":/icons/folder.svg"),
                                         QStringLiteral("Reveal in File Explorer"));
        auto *removeBtn = row->addAction(QStringLiteral(":/icons/close.svg"),
                                         QStringLiteral("Remove from Recent"));
        connect(openBtn, &QToolButton::clicked, this, [this, p]() {
            emit openProjectRequested(p);
        });
        connect(revealBtn, &QToolButton::clicked, this, [p]() {
            revealInFileManager(p);
        });
        connect(removeBtn, &QToolButton::clicked, this, [this, p]() {
            SessionManager::instance()->removeRecent(p);
            refreshRecentList();
        });
        connect(row, &ExplorerItemRow::activated, this, [this, listItem]() {
            m_recentList->setCurrentItem(listItem);
            const QString path = listItem->data(Qt::UserRole).toString();
            if (!path.isEmpty())
                emit openProjectRequested(path);
        });
        listItem->setSizeHint(row->sizeHint().expandedTo(QSize(0, 22)));
        m_recentList->setItemWidget(listItem, row);
    }
}

void ProjectPanel::onDeleteProject()
{
    if (m_projects.size() <= 1) {
        QMessageBox::information(this, QStringLiteral("Delete Project"),
                                 QStringLiteral("Keep at least one project."));
        return;
    }
    if (m_currentIndex < 0) return;

    auto reply = QMessageBox::question(
        this, QStringLiteral("Delete Project"),
        QStringLiteral("Remove project \"%1\" from the list?")
            .arg(m_projects[m_currentIndex].name));
    if (reply != QMessageBox::Yes) return;

    m_projects.removeAt(m_currentIndex);
    m_currentIndex = qMax(0, m_currentIndex - 1);
    refreshList();
    emit projectSwitched(m_currentIndex);
}

void ProjectPanel::onProjectItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (!item) return;
    if (item->parent()) return;
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_projects.size()) return;
    m_currentIndex = idx;
}

void ProjectPanel::onProjectItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column)
    if (!item) return;

    if (item->parent()) {
        QString filePath = item->data(0, Qt::UserRole).toString();
        if (!filePath.isEmpty())
            emit filePreviewRequested(filePath);
        return;
    }

    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_projects.size()) return;
    m_currentIndex = idx;
    emit projectSwitched(m_currentIndex);
}

QString ProjectPanel::pathForTreeItem(QTreeWidgetItem *item) const
{
    if (!item)
        return {};

    if (!item->parent()) {
        const int idx = item->data(0, Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_projects.size())
            return m_projects[idx].filePath;
        return {};
    }

    return item->data(0, Qt::UserRole).toString();
}

void ProjectPanel::revealInFileManager(const QString &path)
{
    if (path.isEmpty())
        return;

    const QFileInfo fi(path);
    if (!fi.exists())
        return;

#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,%1").arg(native)});
#else
    const QString dir = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#endif
}

void ProjectPanel::onProjectTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_projectTree->itemAt(pos);
    if (item)
        m_projectTree->setCurrentItem(item);

    QMenu menu(this);

    QAction *newAct = menu.addAction(QStringLiteral("New Project..."));
    QAction *openAct = menu.addAction(QStringLiteral("Open Project..."));
    menu.addSeparator();

    const bool isFileLeaf = item && item->parent()
                            && !item->data(0, Qt::UserRole).toString().isEmpty();
    int projIdx = -1;
    if (item && !item->parent()) {
        projIdx = item->data(0, Qt::UserRole).toInt();
    } else if (item) {
        QTreeWidgetItem *root = item;
        while (root->parent())
            root = root->parent();
        projIdx = root->data(0, Qt::UserRole).toInt();
    }

    QAction *saveAct = nullptr;
    QAction *deleteAct = nullptr;
    QAction *switchAct = nullptr;
    QAction *revealAct = nullptr;
    QAction *previewAct = nullptr;

    if (projIdx >= 0 && projIdx < m_projects.size()) {
        if (projIdx != m_currentIndex)
            switchAct = menu.addAction(QStringLiteral("Switch to Project"));
        saveAct = menu.addAction(QStringLiteral("Save Project"));
        deleteAct = menu.addAction(QStringLiteral("Delete Project"));
        menu.addSeparator();
    }

    const QString path = pathForTreeItem(item);
    if (!path.isEmpty()) {
        revealAct = menu.addAction(QStringLiteral("Reveal in File Explorer"));
        revealAct->setEnabled(QFileInfo::exists(path));
        if (isFileLeaf) {
            previewAct = menu.addAction(QStringLiteral("Open Preview"));
            previewAct->setEnabled(QFileInfo::exists(path));
        }
    }

    QAction *chosen = menu.exec(m_projectTree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == newAct) {
        onNewProject();
    } else if (chosen == openAct) {
        onOpenProject();
    } else if (chosen == switchAct && projIdx >= 0) {
        m_currentIndex = projIdx;
        refreshList();
        emit projectSwitched(m_currentIndex);
    } else if (chosen == saveAct && projIdx >= 0) {
        m_currentIndex = projIdx;
        onSaveProject();
    } else if (chosen == deleteAct && projIdx >= 0) {
        m_currentIndex = projIdx;
        onDeleteProject();
    } else if (chosen == revealAct) {
        revealInFileManager(path);
    } else if (chosen == previewAct) {
        emit filePreviewRequested(path);
    }
}

void ProjectPanel::onRecentListContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_recentList->itemAt(pos);

    QMenu menu(this);
    QAction *newAct = menu.addAction(QStringLiteral("New Project..."));
    QAction *openAct = menu.addAction(QStringLiteral("Open Project..."));

    QAction *openRecentAct = nullptr;
    QAction *revealAct = nullptr;
    QAction *removeRecentAct = nullptr;

    QString path;
    if (item) {
        m_recentList->setCurrentItem(item);
        path = item->data(Qt::UserRole).toString();
        menu.addSeparator();
        openRecentAct = menu.addAction(QStringLiteral("Open"));
        openRecentAct->setEnabled(!path.isEmpty() && QFileInfo::exists(path));
        revealAct = menu.addAction(QStringLiteral("Reveal in File Explorer"));
        revealAct->setEnabled(!path.isEmpty() && QFileInfo::exists(path));
        menu.addSeparator();
        removeRecentAct = menu.addAction(QStringLiteral("Remove from Recent"));
        removeRecentAct->setEnabled(!path.isEmpty());
    }

    QAction *chosen = menu.exec(m_recentList->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == newAct) {
        onNewProject();
    } else if (chosen == openAct) {
        onOpenProject();
    } else if (chosen == openRecentAct && !path.isEmpty()) {
        emit openProjectRequested(path);
    } else if (chosen == revealAct) {
        revealInFileManager(path);
    } else if (chosen == removeRecentAct && !path.isEmpty()) {
        SessionManager::instance()->removeRecent(path);
        refreshRecentList();
    }
}

// ============================================================
//  ProjectPanel — project file info helpers
// ============================================================

QStringList ProjectPanel::extractDbcFiles(const QString &stateJson) const
{
    QStringList result;
    if (stateJson.isEmpty()) return result;
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: resources.dbc
        if (j.contains("resources") && j["resources"].contains("dbc") &&
            j["resources"]["dbc"].is_array()) {
            for (const auto &f : j["resources"]["dbc"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
        // v1 格式: dbc.files
        if (result.isEmpty() && j.contains("dbc") && j["dbc"].contains("files") &&
            j["dbc"]["files"].is_array()) {
            for (const auto &f : j["dbc"]["files"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
    } catch (...) {}
    return result;
}

QStringList ProjectPanel::extractRecordFiles(const QString &stateJson) const
{
    QStringList result;
    if (stateJson.isEmpty()) return result;
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: resources.logs
        if (j.contains("resources") && j["resources"].contains("logs") &&
            j["resources"]["logs"].is_array()) {
            for (const auto &f : j["resources"]["logs"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
        // v1 格式: record.files
        if (result.isEmpty() && j.contains("record") && j["record"].contains("files") &&
            j["record"]["files"].is_array()) {
            for (const auto &f : j["record"]["files"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
    } catch (...) {}
    return result;
}

QString ProjectPanel::extractPlaybackFile(const QString &stateJson) const
{
    if (stateJson.isEmpty()) return {};
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: source.filePath
        if (j.contains("source") && j["source"].contains("filePath") &&
            j["source"]["filePath"].is_string())
            return QString::fromStdString(j["source"]["filePath"].get<std::string>());
        // v1 格式: filePath
        if (j.contains("filePath") && j["filePath"].is_string())
            return QString::fromStdString(j["filePath"].get<std::string>());
    } catch (...) {}
    return {};
}

QStringList ProjectPanel::extractOfflineFiles(const QString &stateJson) const
{
    QStringList result;
    if (stateJson.isEmpty()) return result;
    try {
        auto j = nlohmann::json::parse(stateJson.toStdString());
        // v2 格式: resources.offline
        if (j.contains("resources") && j["resources"].contains("offline") &&
            j["resources"]["offline"].is_array()) {
            for (const auto &f : j["resources"]["offline"])
                if (f.is_string())
                    result << QString::fromStdString(f.get<std::string>());
        }
    } catch (...) {}
    return result;
}

// ============================================================
//  DbcPanel — 数据库面板（多协议树形分类）
// ============================================================

DbcPanel::DbcPanel(QWidget *parent)
    : SidePanel(QStringLiteral("DATABASES"), parent)
{
    auto *cl = contentLayout();

    m_dbSection = new ExplorerSection(QStringLiteral("Databases"), this);
    m_importBtn = m_dbSection->addHeaderAction(
        QStringLiteral(":/icons/plus.svg"),
        QStringLiteral("Load database file"));
    m_removeBtn = m_dbSection->addHeaderAction(
        QStringLiteral(":/icons/dash.svg"),
        QStringLiteral("Remove selected database"));

    m_tree = new QTreeWidget(m_dbSection->bodyWidget());
    applyExplorerTree(m_tree);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->setColumnWidth(1, 24);
    m_tree->setExpandsOnDoubleClick(false);
    installTreeRowActionHover(m_tree);
    m_dbSection->bodyLayout()->addWidget(m_tree, 1);
    cl->addWidget(m_dbSection, 1);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        if (m_dbSection)
            m_dbSection->refreshTheme();
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));

    initCategoryNodes();

    connect(m_importBtn, &QToolButton::clicked, this, &DbcPanel::onImportDatabase);
    connect(m_removeBtn, &QToolButton::clicked, this, &DbcPanel::onRemoveDatabase);
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &DbcPanel::onItemClicked);
}

void DbcPanel::initCategoryNodes()
{
    const QString iconColor = ThemeManager::instance()->currentTheme().textDim;
    const QIcon folderIcon = svgIcon(QStringLiteral(":/icons/folder.svg"), iconColor, 16);

    m_catCanFd    = new QTreeWidgetItem(m_tree, {QStringLiteral("CAN / CANFD")});
    m_catCanFd->setIcon(0, folderIcon);

    m_catCanopen  = new QTreeWidgetItem(m_tree, {QStringLiteral("CANopen")});
    m_catCanopen->setIcon(0, folderIcon);

    m_catEthercat = new QTreeWidgetItem(m_tree, {QStringLiteral("EtherCAT")});
    m_catEthercat->setIcon(0, folderIcon);

    m_catLin      = new QTreeWidgetItem(m_tree, {QStringLiteral("LIN")});
    m_catLin->setIcon(0, folderIcon);

    m_catJ1939    = new QTreeWidgetItem(m_tree, {QStringLiteral("J1939")});
    m_catJ1939->setIcon(0, folderIcon);

    m_catAutosar  = new QTreeWidgetItem(m_tree, {QStringLiteral("AUTOSAR")});
    m_catAutosar->setIcon(0, folderIcon);

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto *cat = m_tree->topLevelItem(i);
        cat->setFlags(Qt::ItemIsEnabled);  // 分类节点不可选中，仅可展开
    }
}

QString DbcPanel::categoryForFile(const QString &fileName)
{
    QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == "dbc")
        return "CAN/CANFD";
    if (ext == "eds" || ext == "dcf" || ext == "xdd")
        return "CANopen";
    if (ext == "xml")
        return "EtherCAT";
    if (ext == "ldf" || ext == "ncf")
        return "LIN";
    if (ext == "dpf")
        return "J1939";
    if (ext == "arxml")
        return "AUTOSAR";
    return {};
}

void DbcPanel::setDbcManager(DbcManager *mgr)
{
    m_dbcMgr = mgr;
    if (m_dbcMgr) {
        // DEF-08 字符串信号：DbcManager 定义于 data.dll，跨 DLL PMF connect 断连
        auto *dbcRelay = new SignalRelay(this);
        dbcRelay->fire0 = [this]() { refreshTree(); };
        connect(m_dbcMgr, SIGNAL(dbcLoaded(QString)), dbcRelay, SLOT(fire()));
        connect(m_dbcMgr, SIGNAL(dbcUnloaded(QString)), dbcRelay, SLOT(fire()));
    }
    refreshTree();
}

void DbcPanel::onImportDatabase()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load database file"), {},
        QStringLiteral(
            "CAN/CANFD DBC (*.dbc);;"
            "CANopen EDS/DCF/XDD (*.eds *.dcf *.xdd);;"
            "EtherCAT ESI (*.xml);;"
            "LIN LDF/NCF (*.ldf *.ncf);;"
            "J1939 DPF (*.dpf);;"
            "AUTOSAR ARXML (*.arxml);;"
            "All files (*.*)"));
    if (path.isEmpty())
        return;

    QString category = categoryForFile(path);
    if (category.isEmpty()) {
        QMessageBox::warning(this, "不支持的格式",
            QString("无法识别文件类型: %1\n支持: DBC / EDS / DCF / XDD / XML / LDF / NCF / DPF / ARXML")
                .arg(QFileInfo(path).fileName()));
        return;
    }

    // DBC 文件交给 DbcManager 解析
    if (category == "CAN/CANFD" && m_dbcMgr) {
        if (!m_dbcMgr->loadDbc(path))
            QMessageBox::warning(this, "加载失败", "无法加载 DBC 文件: " + path);
        return;
    }

    // 其他协议文件加入本地列表
    DatabaseEntry entry;
    entry.fileName = QFileInfo(path).fileName();
    entry.filePath = path;
    entry.category = category;

    // 避免重复加载
    for (const auto &e : m_otherDbs) {
        if (e.filePath == path) {
            QMessageBox::information(this, "已加载", "该文件已在列表中");
            return;
        }
    }

    m_otherDbs.append(entry);
    refreshTree();
}

void DbcPanel::onRemoveDatabase()
{
    auto *item = m_tree->currentItem();
    if (!item || item->childCount() > 0) {
        QMessageBox::information(this, QStringLiteral("Remove"),
                                 QStringLiteral("Select a database file first."));
        return;
    }
    removeDatabaseItem(item);
}

void DbcPanel::removeDatabaseItem(QTreeWidgetItem *item)
{
    if (!item || item->childCount() > 0)
        return;

    QString category = item->data(0, Qt::UserRole).toString();
    QString filePath = item->data(0, Qt::UserRole + 1).toString();
    QString fileName = item->text(0);

    if (filePath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Remove"),
                                 QStringLiteral("Cannot resolve file path."));
        return;
    }

    auto reply = QMessageBox::question(
        this, QStringLiteral("Remove database"),
        QStringLiteral("Remove \"%1\"?").arg(fileName));
    if (reply != QMessageBox::Yes)
        return;

    if (category == "CAN/CANFD" && m_dbcMgr) {
        emit dbcRemoveRequested(filePath);
        m_dbcMgr->unloadDbc(filePath);
    } else {
        for (int i = 0; i < m_otherDbs.size(); ++i) {
            if (m_otherDbs[i].filePath == filePath) {
                m_otherDbs.removeAt(i);
                break;
            }
        }
        refreshTree();
    }
}

void DbcPanel::refreshTree()
{
    auto clearChildren = [](QTreeWidgetItem *cat) {
        while (cat->childCount() > 0)
            delete cat->takeChild(0);
    };
    clearChildren(m_catCanFd);
    clearChildren(m_catCanopen);
    clearChildren(m_catEthercat);
    clearChildren(m_catLin);
    clearChildren(m_catJ1939);
    clearChildren(m_catAutosar);

    const QString iconCol = ThemeManager::instance()->currentTheme().text;

    auto attachRemove = [this](QTreeWidgetItem *item) {
        auto *acts = new ExplorerItemActions(m_tree);
        auto *rm = acts->addAction(QStringLiteral(":/icons/close.svg"),
                                   QStringLiteral("Remove"));
        connect(rm, &QToolButton::clicked, this, [this, item]() {
            m_tree->setCurrentItem(item);
            removeDatabaseItem(item);
        });
        m_tree->setItemWidget(item, 1, acts);
    };

    if (m_dbcMgr) {
        for (const auto &file : m_dbcMgr->files()) {
            auto *item = new QTreeWidgetItem(m_catCanFd, {file.fileName});
            item->setIcon(0, svgIcon(":/icons/file.svg", iconCol));
            item->setData(0, Qt::UserRole, "CAN/CANFD");
            item->setData(0, Qt::UserRole + 1, file.filePath);
            attachRemove(item);
        }
    }

    for (const auto &entry : m_otherDbs) {
        QTreeWidgetItem *parent = nullptr;
        if (entry.category == "CAN/CANFD")       parent = m_catCanFd;
        else if (entry.category == "CANopen")    parent = m_catCanopen;
        else if (entry.category == "EtherCAT")   parent = m_catEthercat;
        else if (entry.category == "LIN")        parent = m_catLin;
        else if (entry.category == "J1939")      parent = m_catJ1939;
        else if (entry.category == "AUTOSAR")    parent = m_catAutosar;
        if (!parent) continue;

        auto *item = new QTreeWidgetItem(parent, {entry.fileName});
        item->setIcon(0, svgIcon(":/icons/file.svg", iconCol));
        item->setData(0, Qt::UserRole, entry.category);
        item->setData(0, Qt::UserRole + 1, entry.filePath);
        attachRemove(item);
    }

    auto updateVisibility = [](QTreeWidgetItem *cat) {
        bool hasChildren = cat->childCount() > 0;
        cat->setHidden(!hasChildren);
        if (hasChildren)
            cat->setExpanded(true);
    };
    updateVisibility(m_catCanFd);
    updateVisibility(m_catCanopen);
    updateVisibility(m_catEthercat);
    updateVisibility(m_catLin);
    updateVisibility(m_catJ1939);
    updateVisibility(m_catAutosar);

    auto setCount = [](QTreeWidgetItem *cat, const QString &label) {
        int n = cat->childCount();
        cat->setText(0, QString("%1 %2").arg(label).arg(n > 0 ? QString("(%1)").arg(n) : ""));
    };
    setCount(m_catCanFd,    "CAN / CANFD");
    setCount(m_catCanopen,  "CANopen");
    setCount(m_catEthercat, "EtherCAT");
    setCount(m_catLin,      "LIN");
    setCount(m_catJ1939,    "J1939");
    setCount(m_catAutosar,  "AUTOSAR");
}

void DbcPanel::onItemClicked(QTreeWidgetItem *item, int)
{
    if (!item || item->flags() == Qt::NoItemFlags)
        return;

    // 分类节点 → 展开/折叠
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    // 叶子节点 → 发出信号
    QString category = item->data(0, Qt::UserRole).toString();
    if (category.isEmpty())
        return;

    if (category == "CAN/CANFD")
        emit dbcFileClicked(item->text(0));
    else
        emit databaseFileClicked(category, item->text(0));
}

// ============================================================
//  TracePanel — 形态模板平铺 + 已打开实例列表
// ============================================================

TracePanel::TracePanel(QWidget *parent)
    : SidePanel("Trace", parent)
{
    auto *cl = contentLayout();

    m_openedSection = new ExplorerSection(QStringLiteral("Open Editors"), this);
    m_delBtn = m_openedSection->addHeaderAction(
        QStringLiteral(":/icons/dash.svg"),
        QStringLiteral("Close selected Trace"));
    m_traceList = new QListWidget(m_openedSection->bodyWidget());
    m_traceList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_traceList->setFrameShape(QFrame::NoFrame);
    m_traceList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_traceList->setMouseTracking(true);
    m_openedSection->bodyLayout()->addWidget(m_traceList, 1);
    cl->addWidget(m_openedSection, 1);

    m_newSection = new ExplorerSection(QStringLiteral("New Trace"), this);
    m_templateList = new QListWidget(m_newSection->bodyWidget());
    m_templateList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_templateList->setFrameShape(QFrame::NoFrame);
    const QString tmplIconCol = ThemeManager::instance()->currentTheme().text;
    auto addTemplate = [this, tmplIconCol](const QString &name,
                                           const QString &formId,
                                           const QString &tip, bool enabled) {
        auto *row = new QListWidgetItem(m_templateList);
        row->setText(name);
        row->setData(Qt::UserRole, formId);
        row->setToolTip(tip);
        if (enabled)
            row->setIcon(svgIcon(":/icons/plus.svg", tmplIconCol, 14));
        else {
            row->setFlags(Qt::NoItemFlags);
            row->setIcon(svgIcon(":/icons/plus.svg", "#6c6c6c", 14));
        }
    };
    addTemplate(QStringLiteral("Frame List"), QStringLiteral("framelist"),
                QStringLiteral("New frame-list Trace (current form)"), true);
    addTemplate(QStringLiteral("Transaction"), QStringLiteral("transaction"),
                QStringLiteral("UDS / CANopen SDO request-response (planned)"), false);
    addTemplate(QStringLiteral("Aggregate Watch"), QStringLiteral("aggregwatch"),
                QStringLiteral("Latest frame / signal values (planned)"), false);
    addTemplate(QStringLiteral("Text Log"), QStringLiteral("textlog"),
                QStringLiteral("Serial ASCII / plugin / system events (planned)"), false);
    addTemplate(QStringLiteral("Byte Stream"), QStringLiteral("bytestream"),
                QStringLiteral("Binary / HEX stream (planned)"), false);
    addTemplate(QStringLiteral("Timeline"), QStringLiteral("timeline"),
                QStringLiteral("LIN schedule / FlexRay cycle timeline (planned)"), false);
    m_newSection->bodyLayout()->addWidget(m_templateList, 1);
    cl->addWidget(m_newSection, 1);

    connect(m_templateList, &QListWidget::itemClicked,
            this, &TracePanel::onTemplateClicked);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        if (m_openedSection)
            m_openedSection->refreshTheme();
        if (m_newSection)
            m_newSection->refreshTheme();
        const QString c = ThemeManager::instance()->currentTheme().text;
        for (int i = 0; i < m_templateList->count(); ++i) {
            auto *row = m_templateList->item(i);
            if (row->flags().testFlag(Qt::ItemIsEnabled))
                row->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        }
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));

    connect(m_delBtn, &QToolButton::clicked, this, &TracePanel::onDeleteTrace);
    connect(m_traceList, &QListWidget::currentRowChanged,
            this, &TracePanel::onPageSelected);
    connect(m_traceList, &QWidget::customContextMenuRequested,
            this, &TracePanel::onContextMenu);
}

void TracePanel::refreshList(const QStringList &names)
{
    m_traceList->blockSignals(true);
    int prevRow = m_traceList->currentRow();
    m_traceList->clear();
    for (const auto &n : names) {
        auto *item = new QListWidgetItem(m_traceList);
        item->setData(Qt::UserRole, n);
        auto *row = new ExplorerItemRow(n, m_traceList);
        row->setLeadingIcon(svgIcon(QStringLiteral(":/icons/trace.svg"),
                                    ThemeManager::instance()->currentTheme().text, 14));
        auto *closeBtn = row->addAction(QStringLiteral(":/icons/close.svg"),
                                        QStringLiteral("Close"));
        connect(closeBtn, &QToolButton::clicked, this, [this, item]() {
            const int r = m_traceList->row(item);
            if (r >= 0)
                emit traceDeleteRequested(r);
        });
        connect(row, &ExplorerItemRow::activated, this, [this, item]() {
            m_traceList->setCurrentItem(item);
            const int r = m_traceList->row(item);
            if (r >= 0)
                emit tracePageSelected(r);
        });
        item->setSizeHint(row->sizeHint().expandedTo(QSize(0, 22)));
        m_traceList->setItemWidget(item, row);
    }
    if (prevRow >= 0 && prevRow < m_traceList->count())
        m_traceList->setCurrentRow(prevRow);
    m_traceList->blockSignals(false);
}

void TracePanel::onTemplateClicked(QListWidgetItem *item)
{
    // 模板行点击：仅已实现形态可新建（当前仅帧列表）；置灰占位行不响应。
    // TR1 落地 TraceFormRegistry 后按 formId 分发到对应形态工厂
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
        return;
    emit openTraceRequested();
}

void TracePanel::onPageSelected(int row)
{
    if (row >= 0)
        emit tracePageSelected(row);
}

void TracePanel::onDeleteTrace()
{
    int row = m_traceList->currentRow();
    if (row >= 0)
        emit traceDeleteRequested(row);
}

void TracePanel::onContextMenu(const QPoint &pos)
{
    auto *item = m_traceList->itemAt(pos);
    if (!item) return;
    int row = m_traceList->row(item);

    QMenu menu(this);
    auto *actJump = menu.addAction(QStringLiteral("Jump to this tab"));
    auto *actDel = menu.addAction(QStringLiteral("Close this Trace"));
    QAction *chosen = menu.exec(m_traceList->viewport()->mapToGlobal(pos));
    if (chosen == actJump) {
        emit tracePageSelected(row);
    } else if (chosen == actDel) {
        emit traceDeleteRequested(row);
    }
}

// ============================================================
//  GraphicConfigPanel — 形态模板平铺 + 已打开页面列表
// ============================================================

GraphicConfigPanel::GraphicConfigPanel(QWidget *parent)
    : SidePanel("Graphic", parent)
{
    auto *cl = contentLayout();

    m_openedSection = new ExplorerSection(QStringLiteral("Open Editors"), this);
    m_delBtn = m_openedSection->addHeaderAction(
        QStringLiteral(":/icons/dash.svg"),
        QStringLiteral("Close selected Graphic"));
    m_pageList = new QListWidget(m_openedSection->bodyWidget());
    m_pageList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_pageList->setFrameShape(QFrame::NoFrame);
    m_pageList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pageList->setMouseTracking(true);
    m_openedSection->bodyLayout()->addWidget(m_pageList, 1);
    cl->addWidget(m_openedSection, 1);

    m_newSection = new ExplorerSection(QStringLiteral("New Graphic"), this);
    m_templateList = new QListWidget(m_newSection->bodyWidget());
    m_templateList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_templateList->setFrameShape(QFrame::NoFrame);
    const QString tmplIconCol = ThemeManager::instance()->currentTheme().text;
    auto addTemplate = [this, tmplIconCol](const QString &name,
                                           const QString &formId,
                                           const QString &tip, bool enabled) {
        auto *row = new QListWidgetItem(m_templateList);
        row->setText(name);
        row->setData(Qt::UserRole, formId);
        row->setToolTip(tip);
        if (enabled)
            row->setIcon(svgIcon(":/icons/plus.svg", tmplIconCol, 14));
        else {
            row->setFlags(Qt::NoItemFlags);
            row->setIcon(svgIcon(":/icons/plus.svg", "#6c6c6c", 14));
        }
    };
    addTemplate(QStringLiteral("Waveform"), QStringLiteral("waveform"),
                QStringLiteral("New waveform Graphic (current form)"), true);
    addTemplate(QStringLiteral("XY Plot"), QStringLiteral("xyplot"),
                QStringLiteral("X/Y signal correlation (planned)"), false);
    addTemplate(QStringLiteral("Digital Bus"), QStringLiteral("digital"),
                QStringLiteral("Bit-level digital lanes (planned)"), false);
    addTemplate(QStringLiteral("State Timeline"), QStringLiteral("statetimeline"),
                QStringLiteral("Enum color bands (planned)"), false);
    addTemplate(QStringLiteral("Gauge"), QStringLiteral("gauge"),
                QStringLiteral("Gauges / bars / LEDs (planned)"), false);
    addTemplate(QStringLiteral("Bar Stats"), QStringLiteral("barstats"),
                QStringLiteral("Time-bucketed stats (planned)"), false);
    m_newSection->bodyLayout()->addWidget(m_templateList, 1);
    cl->addWidget(m_newSection, 1);

    connect(m_templateList, &QListWidget::itemClicked,
            this, &GraphicConfigPanel::onTemplateClicked);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        if (m_openedSection)
            m_openedSection->refreshTheme();
        if (m_newSection)
            m_newSection->refreshTheme();
        const QString c = ThemeManager::instance()->currentTheme().text;
        for (int i = 0; i < m_templateList->count(); ++i) {
            auto *row = m_templateList->item(i);
            if (row->flags().testFlag(Qt::ItemIsEnabled))
                row->setIcon(svgIcon(":/icons/plus.svg", c, 14));
        }
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));

    connect(m_delBtn, &QToolButton::clicked, this, [this]() {
        const int row = m_pageList->currentRow();
        if (row >= 0)
            emit graphicDeleteRequested(row);
    });
    connect(m_pageList, &QListWidget::currentRowChanged,
            this, &GraphicConfigPanel::onPageSelected);
    connect(m_pageList, &QWidget::customContextMenuRequested,
            this, &GraphicConfigPanel::onContextMenu);
}

// setGraphicView 已随 B5-5 移除 — 面板不再持有 GraphicView 指针，
// Graphic 实例编排统一经壳 → ModuleRegistry "graphic" 模块

void GraphicConfigPanel::onTemplateClicked(QListWidgetItem *item)
{
    // 模板行点击：仅已实现形态可新建（当前仅时序波形）；置灰占位行不响应。
    // GV1 落地 GraphicFormRegistry 后按 formId 分发到对应形态工厂
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
        return;
    emit newGraphicRequested();
}

void GraphicConfigPanel::onPageSelected(int row)
{
    if (row >= 0)
        emit graphicPageSelected(row);
}

void GraphicConfigPanel::onContextMenu(const QPoint &pos)
{
    auto *item = m_pageList->itemAt(pos);
    if (!item) return;
    int row = m_pageList->row(item);

    QMenu menu(this);
    auto *actJump = menu.addAction(QStringLiteral("Jump to this tab"));
    auto *actDel = menu.addAction(QStringLiteral("Delete this Graphic"));
    QAction *chosen = menu.exec(m_pageList->viewport()->mapToGlobal(pos));
    if (chosen == actJump) {
        emit graphicPageSelected(row);
    } else if (chosen == actDel) {
        emit graphicDeleteRequested(row);
    }
}

void GraphicConfigPanel::refreshList(const QStringList &names)
{
    m_pageList->blockSignals(true);
    int prevRow = m_pageList->currentRow();
    m_pageList->clear();
    for (const auto &n : names) {
        auto *item = new QListWidgetItem(m_pageList);
        item->setData(Qt::UserRole, n);
        auto *row = new ExplorerItemRow(n, m_pageList);
        row->setLeadingIcon(svgIcon(QStringLiteral(":/icons/graphic.svg"),
                                    ThemeManager::instance()->currentTheme().text, 14));
        auto *closeBtn = row->addAction(QStringLiteral(":/icons/close.svg"),
                                        QStringLiteral("Close"));
        connect(closeBtn, &QToolButton::clicked, this, [this, item]() {
            const int r = m_pageList->row(item);
            if (r >= 0)
                emit graphicDeleteRequested(r);
        });
        connect(row, &ExplorerItemRow::activated, this, [this, item]() {
            m_pageList->setCurrentItem(item);
            const int r = m_pageList->row(item);
            if (r >= 0)
                emit graphicPageSelected(r);
        });
        item->setSizeHint(row->sizeHint().expandedTo(QSize(0, 22)));
        m_pageList->setItemWidget(item, row);
    }
    if (prevRow >= 0 && prevRow < m_pageList->count())
        m_pageList->setCurrentRow(prevRow);
    m_pageList->blockSignals(false);
}

// ============================================================
//  DevicePanel — 仅设备连接
// ============================================================

DevicePanel::DevicePanel(QWidget *parent)
    : SidePanel(QStringLiteral("DEVICES"), parent)
{
    auto *cl = contentLayout();

    m_devicesSection = new ExplorerSection(QStringLiteral("Devices"), this);
    m_addBtn = m_devicesSection->addHeaderAction(
        QStringLiteral(":/icons/plus.svg"),
        QStringLiteral("Add device (open market)"));
    m_scanBtn = m_devicesSection->addHeaderAction(
        QStringLiteral(":/icons/refresh.svg"),
        QStringLiteral("Scan for devices"));

    m_deviceTree = new QTreeWidget(m_devicesSection->bodyWidget());
    applyExplorerTree(m_deviceTree);
    m_deviceTree->setHeaderHidden(true);
    m_deviceTree->setColumnCount(2);
    m_deviceTree->header()->setStretchLastSection(false);
    m_deviceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_deviceTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_deviceTree->setColumnWidth(1, 24);
    m_deviceTree->setExpandsOnDoubleClick(false);
    installTreeRowActionHover(m_deviceTree);
    m_devicesSection->bodyLayout()->addWidget(m_deviceTree, 1);
    cl->addWidget(m_devicesSection, 1);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        if (m_devicesSection)
            m_devicesSection->refreshTheme();
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));

    connect(m_deviceTree, &QTreeWidget::itemClicked,
            this, &DevicePanel::onItemClicked);
    connect(m_deviceTree, &QTreeWidget::itemDoubleClicked,
            this, &DevicePanel::onItemDoubleClicked);
    connect(m_scanBtn, &QToolButton::clicked, this, &DevicePanel::onScanClicked);
    connect(m_addBtn, &QToolButton::clicked, this, &DevicePanel::onAddDeviceClicked);

    connect(DriverRegistry::instance(), SIGNAL(driversChanged()),
            this, SLOT(refreshDevices()));

    populateTree();
}

void DevicePanel::setSimulator(CanSimulator *sim)
{
    m_simulator = sim;
}

void DevicePanel::setDeviceManager(CanDeviceManager *mgr)
{
    m_deviceMgr = mgr;
    refreshDevices();
}

void DevicePanel::refreshDevices()
{
    populateTree();
}

void DevicePanel::onAddDeviceClicked()
{
    emit addDeviceRequested();
}

void DevicePanel::onScanClicked()
{
    if (m_scanBtn)
        m_scanBtn->setEnabled(false);
    refreshDevices();
    if (m_scanBtn)
        m_scanBtn->setEnabled(true);
}

void DevicePanel::populateTree()
{
    m_deviceTree->clear();

    auto attachOpen = [this](QTreeWidgetItem *item) {
        auto *acts = new ExplorerItemActions(m_deviceTree);
        auto *openBtn = acts->addAction(QStringLiteral(":/icons/goto.svg"),
                                        QStringLiteral("Open connection"));
        connect(openBtn, &QToolButton::clicked, this, [this, item]() {
            m_deviceTree->setCurrentItem(item);
            onItemClicked(item, 0);
        });
        m_deviceTree->setItemWidget(item, 1, acts);
    };

    auto *simItem = new QTreeWidgetItem(m_deviceTree);
    simItem->setText(0, QStringLiteral("openbus Simulator"));
    simItem->setData(0, Qt::UserRole, 0);
    simItem->setData(0, Qt::UserRole + 1, 0);
    attachOpen(simItem);

    const auto allDevices = ICanDevice::enumerateAll();
    const auto drivers = DriverRegistry::instance()->drivers();
    for (const auto &drv : drivers) {
        if (!drv.enabled || !drv.available)
            continue;
        auto *parent = new QTreeWidgetItem(m_deviceTree);
        parent->setText(0, drv.displayName);

        QList<ICanDevice::DeviceInfo> devs;
        for (const auto &d : allDevices) {
            if (d.driverId == drv.driverId)
                devs << d;
        }

        if (!devs.isEmpty()) {
            for (const auto &d : devs) {
                auto *dev = new QTreeWidgetItem(parent);
                dev->setText(0, QStringLiteral("  ") + d.name);
                dev->setData(0, Qt::UserRole, drv.deviceKind);
                dev->setData(0, Qt::UserRole + 1, d.deviceIndex);
                dev->setData(0, Qt::UserRole + 2, d.deviceType);
                attachOpen(dev);
            }
        } else {
            auto *empty = new QTreeWidgetItem(parent);
            empty->setText(0, QStringLiteral("  %1 (no hardware detected)")
                                   .arg(drv.displayName));
            empty->setData(0, Qt::UserRole, drv.deviceKind);
            empty->setData(0, Qt::UserRole + 1, 0);
            attachOpen(empty);
        }
        parent->setExpanded(true);
    }
}

void DevicePanel::onItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }
    int deviceKind = item->data(0, Qt::UserRole).toInt();
    int devIndex = item->data(0, Qt::UserRole + 1).toInt();
    int deviceType = item->data(0, Qt::UserRole + 2).toInt();
    emit deviceOpenRequested(deviceKind, devIndex, item->text(0).trimmed(), deviceType);
}

void DevicePanel::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (item->childCount() > 0)
        return;
    int deviceKind = item->data(0, Qt::UserRole).toInt();
    int devIndex = item->data(0, Qt::UserRole + 1).toInt();
    int deviceType = item->data(0, Qt::UserRole + 2).toInt();
    emit deviceOpenRequested(deviceKind, devIndex, item->text(0).trimmed(), deviceType);
}

// ============================================================
//  TransceivePanel — 收发面板（发送 / 回放 / 离线分析 / 录制）
// ============================================================

TransceivePanel::TransceivePanel(QWidget *parent)
    : SidePanel(QStringLiteral("Transceive"), parent)
{
    auto *cl = contentLayout();

    const QString iconColor = ThemeManager::instance()->currentTheme().text;

    auto *sendBtn = new QPushButton(QStringLiteral("Send"), this);
    sendBtn->setObjectName("SidePanelButton");
    sendBtn->setToolTip(QStringLiteral("Open Send tab"));
    sendBtn->setIcon(svgIcon(":/icons/list.svg", iconColor, 16));
    cl->addWidget(sendBtn);

    auto *playbackBtn = new QPushButton(QStringLiteral("Playback"), this);
    playbackBtn->setObjectName("SidePanelButton");
    playbackBtn->setToolTip(QStringLiteral("Open Playback tab"));
    playbackBtn->setIcon(svgIcon(":/icons/play.svg", iconColor, 16));
    cl->addWidget(playbackBtn);

    auto *offlineBtn = new QPushButton(QStringLiteral("Offline Analysis"), this);
    offlineBtn->setObjectName("SidePanelButton");
    offlineBtn->setToolTip(QStringLiteral("Open Offline Analysis tab"));
    offlineBtn->setIcon(svgIcon(":/icons/file.svg", iconColor, 16));
    cl->addWidget(offlineBtn);

    auto *recordBtn = new QPushButton(QStringLiteral("Record"), this);
    recordBtn->setObjectName("SidePanelButton");
    recordBtn->setToolTip(QStringLiteral("Open Record tab"));
    recordBtn->setIcon(svgIcon(":/icons/record.svg", iconColor, 16));
    cl->addWidget(recordBtn);

    cl->addStretch();

    connect(sendBtn, &QPushButton::clicked, this, &TransceivePanel::onSendClicked);
    connect(playbackBtn, &QPushButton::clicked, this, &TransceivePanel::onPlaybackClicked);
    connect(offlineBtn, &QPushButton::clicked, this, &TransceivePanel::onOfflineAnalysisClicked);
    connect(recordBtn, &QPushButton::clicked, this, &TransceivePanel::onRecordClicked);
}

void TransceivePanel::onSendClicked()
{
    emit openSendRequested();
}

void TransceivePanel::onPlaybackClicked()
{
    emit openPlaybackRequested();
}

void TransceivePanel::onOfflineAnalysisClicked()
{
    emit openOfflineAnalysisRequested();
}

void TransceivePanel::onRecordClicked()
{
    emit openRecordRequested();
}

// ============================================================
//  SettingsPanel — 设置入口
// ============================================================

SettingsPanel::SettingsPanel(QWidget *parent)
    : SidePanel(tr("Settings"), parent)
{
    auto *cl = contentLayout();

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_list->setFrameShape(QFrame::NoFrame);
    auto *general = new QListWidgetItem(tr("General"), m_list);
    general->setData(Qt::UserRole, QStringLiteral("general"));
    general->setSizeHint(QSize(0, 22));
    auto *shortcuts = new QListWidgetItem(tr("Keyboard Shortcuts"), m_list);
    shortcuts->setData(Qt::UserRole, QStringLiteral("shortcuts"));
    shortcuts->setSizeHint(QSize(0, 22));
    cl->addWidget(m_list, 1);

    connect(m_list, &QListWidget::itemClicked,
            this, &SettingsPanel::onItemClicked);
}

void SettingsPanel::onItemClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    // Prefer stable role keys so language changes do not break navigation
    const QString key = item->data(Qt::UserRole).toString();
    emit settingsRequested(key.isEmpty() ? item->text() : key);
}

// ============================================================
//  MeasurementSetupPanel — 协议流模板平铺
// ============================================================

MeasurementSetupPanel::MeasurementSetupPanel(QWidget *parent)
    : SidePanel("Flow", parent)
{
    auto *cl = contentLayout();

    m_openedSection = new ExplorerSection(QStringLiteral("Open Editors"), this);
    m_openedList = new QListWidget(m_openedSection->bodyWidget());
    m_openedList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_openedList->setFrameShape(QFrame::NoFrame);
    m_openedList->setMouseTracking(true);
    connect(m_openedList, &QListWidget::itemClicked,
            this, &MeasurementSetupPanel::onOpenedClicked);
    m_openedSection->bodyLayout()->addWidget(m_openedList, 1);
    cl->addWidget(m_openedSection, 1);

    m_newSection = new ExplorerSection(QStringLiteral("New Protocol Flow"), this);
    m_templateList = new QListWidget(m_newSection->bodyWidget());
    m_templateList->setObjectName(QStringLiteral("ExplorerRecentList"));
    m_templateList->setFrameShape(QFrame::NoFrame);
    m_newSection->bodyLayout()->addWidget(m_templateList, 1);
    cl->addWidget(m_newSection, 1);
    rebuildTemplates();

    connect(m_templateList, &QListWidget::itemClicked,
            this, &MeasurementSetupPanel::onTemplateClicked);

    auto *themeRelay = new SignalRelay(this);
    themeRelay->fire0 = [this]() {
        if (m_openedSection)
            m_openedSection->refreshTheme();
        if (m_newSection)
            m_newSection->refreshTheme();
        rebuildTemplates();
    };
    connect(ProtocolRegistry::instance(), SIGNAL(adapterRegistered(QString)),
            themeRelay, SLOT(fire()));
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            themeRelay, SLOT(fire()));
}

void MeasurementSetupPanel::refreshOpenList(const QStringList &names)
{
    m_openedList->clear();
    for (const auto &n : names) {
        auto *item = new QListWidgetItem(m_openedList);
        item->setData(Qt::UserRole, n);
        auto *row = new ExplorerItemRow(n, m_openedList);
        row->setLeadingIcon(svgIcon(QStringLiteral(":/icons/flow.svg"),
                                    ThemeManager::instance()->currentTheme().text, 14));
        auto *jumpBtn = row->addAction(QStringLiteral(":/icons/goto.svg"),
                                       QStringLiteral("Open canvas"));
        connect(jumpBtn, &QToolButton::clicked, this, [this]() {
            emit openMeasurementSetupRequested();
        });
        connect(row, &ExplorerItemRow::activated, this, [this, item]() {
            m_openedList->setCurrentItem(item);
            emit openMeasurementSetupRequested();
        });
        item->setSizeHint(row->sizeHint().expandedTo(QSize(0, 22)));
        m_openedList->setItemWidget(item, row);
    }
}

void MeasurementSetupPanel::rebuildTemplates()
{
    m_templateList->clear();
    const QString iconCol = ThemeManager::instance()->currentTheme().text;

    // ① 注册表适配器 → 可点击模板行（点击新建/打开该协议流）
    QStringList registered;
    for (auto *adapter : ProtocolRegistry::instance()->adapters()) {
        registered << adapter->protocolId();
        auto *row = new QListWidgetItem(adapter->displayName(), m_templateList);
        row->setData(Qt::UserRole, adapter->protocolId());
        row->setIcon(svgIcon(":/icons/plus.svg", iconCol, 14));
    }

    // ② 未落地协议 → 置灰占位行（预埋模板入口；F 系列落地/协议包安装后启用）
    struct Placeholder { const char *pid; const char *title; const char *tip; };
    static const Placeholder placeholders[] = {
        { "ethercat", "EtherCAT Flow", "Enabled when EtherCAT adapter ships" },
        { "canopen",  "CANopen Flow",  "Enabled when CANopen adapter ships" },
        { "general",  "Generic Flow",  "Enabled when generic Flow adapter ships" },
    };
    for (const auto &p : placeholders) {
        if (registered.contains(QLatin1String(p.pid)))
            continue;   // 注册表已接管：占位行让位
        auto *row = new QListWidgetItem(QString::fromUtf8(p.title), m_templateList);
        row->setFlags(Qt::NoItemFlags);   // 置灰占位：不可选中不可点击
        row->setToolTip(QString::fromUtf8(p.tip));
        row->setData(Qt::UserRole, QLatin1String(p.pid));
        row->setIcon(svgIcon(":/icons/plus.svg", "#6c6c6c", 14));
    }
}

void MeasurementSetupPanel::onTemplateClicked(QListWidgetItem *item)
{
    // 模板行点击：仅注册表行可点（当前仅 CAN Flow，打开/聚焦画布页）；
    // F1 FlowSession 落地后按 protocolId 创建新流实例
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled))
        return;
    emit openMeasurementSetupRequested();
}

void MeasurementSetupPanel::onOpenedClicked(QListWidgetItem *item)
{
    // 已打开行点击：打开/聚焦对应流画布（F1 多实例前 = 单画布，与模板行同归宿）
    if (item)
        emit openMeasurementSetupRequested();
}

// ============================================================
//  ExtensionsPanel — 迷你市场（与插件市场页同源，方案 §13.10）
// ============================================================

ExtensionsPanel::ExtensionsPanel(QWidget *parent)
    : SidePanel("Extensions", parent)
{
    auto *cl = contentLayout();

    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(8, 8, 4, 8);
    searchRow->setSpacing(4);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setObjectName("ExtensionSearch");
    m_searchEdit->setPlaceholderText("Search drivers and plugins…");
    m_searchEdit->setClearButtonEnabled(true);
    applyClearButtonIcon(m_searchEdit, ThemeManager::instance()->currentTheme().text);
    searchRow->addWidget(m_searchEdit, 1);

    m_menuBtn = new QToolButton(this);
    m_menuBtn->setIcon(svgIcon(":/icons/kebab.svg",
                               ThemeManager::instance()->currentTheme().text, 16));
    m_menuBtn->setToolTip("More actions");
    m_menuBtn->setAutoRaise(true);
    connect(m_menuBtn, &QToolButton::clicked,
            this, &ExtensionsPanel::onMenuClicked);
    searchRow->addWidget(m_menuBtn);
    cl->addLayout(searchRow);

    auto *menuRelay = new SignalRelay(this);
    menuRelay->fire0 = [this]() {
        const QString c = ThemeManager::instance()->currentTheme().text;
        m_menuBtn->setIcon(svgIcon(":/icons/kebab.svg", c, 16));
        if (m_installedSection)
            m_installedSection->refreshTheme();
        if (m_runningSection)
            m_runningSection->refreshTheme();
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            menuRelay, SLOT(fire()));

    // VS Code views: sections share remaining height (no outer scroll stack).
    m_sectionsHost = new QWidget(this);
    m_listLay = new QVBoxLayout(m_sectionsHost);
    m_listLay->setContentsMargins(0, 0, 0, 0);
    m_listLay->setSpacing(0);
    cl->addWidget(m_sectionsHost, 1);

    m_installedSection = new ExplorerSection(QStringLiteral("Installed"), m_sectionsHost);
    auto *refreshBtn = m_installedSection->addHeaderAction(
        QStringLiteral(":/icons/refresh.svg"),
        QStringLiteral("Refresh"));
    connect(refreshBtn, &QToolButton::clicked, this, [this]() {
        MarketIndex::instance()->refresh();
        DriverRegistry::instance()->scanAndLoad();
        refreshEntries();
    });
    auto *marketBtn = m_installedSection->addHeaderAction(
        QStringLiteral(":/icons/plus.svg"),
        QStringLiteral("Open marketplace"));
    connect(marketBtn, &QToolButton::clicked, this, [this]() {
        emit openMarketRequested();
    });
    m_listLay->addWidget(m_installedSection, 1);

    m_runningSection = new ExplorerSection(QStringLiteral("Running"), m_sectionsHost);
    m_listLay->addWidget(m_runningSection, 1);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ExtensionsPanel::onSearchChanged);

    connect(DriverRegistry::instance(), SIGNAL(driversChanged()),
            this, SLOT(refreshEntries()));
    connect(PluginManager::instance(), SIGNAL(pluginListChanged()),
            this, SLOT(refreshEntries()));
    auto *loadedRelay = new SignalRelay(this);
    loadedRelay->fire0 = [this]() { refreshEntries(); };
    connect(MarketIndex::instance(), SIGNAL(loaded(bool,QString)),
            loadedRelay, SLOT(fire()));

    refreshEntries();
}

void ExtensionsPanel::refreshEntries()
{
    rebuild();
}

void ExtensionsPanel::onSearchChanged()
{
    rebuild();
}

void ExtensionsPanel::rebuild()
{
    auto clearBody = [](ExplorerSection *sec) {
        if (!sec)
            return;
        QLayout *lay = sec->bodyLayout();
        while (lay->count()) {
            QLayoutItem *child = lay->takeAt(0);
            if (child->widget())
                child->widget()->deleteLater();
            delete child;
        }
    };
    clearBody(m_installedSection);
    clearBody(m_runningSection);

    const QString text = m_searchEdit->text();
    auto *pm = PluginManager::instance();

    int nInstalled = 0;
    int nRunning = 0;

    auto *installedScroll = new QScrollArea(m_installedSection->bodyWidget());
    installedScroll->setWidgetResizable(true);
    installedScroll->setFrameShape(QFrame::NoFrame);
    auto *installedHost = new QWidget(installedScroll);
    auto *installedLay = new QVBoxLayout(installedHost);
    installedLay->setContentsMargins(0, 0, 0, 0);
    installedLay->setSpacing(0);
    installedScroll->setWidget(installedHost);
    m_installedSection->bodyLayout()->addWidget(installedScroll, 1);

    auto *runningScroll = new QScrollArea(m_runningSection->bodyWidget());
    runningScroll->setWidgetResizable(true);
    runningScroll->setFrameShape(QFrame::NoFrame);
    auto *runningHost = new QWidget(runningScroll);
    auto *runningLay = new QVBoxLayout(runningHost);
    runningLay->setContentsMargins(0, 0, 0, 0);
    runningLay->setSpacing(0);
    runningScroll->setWidget(runningHost);
    m_runningSection->bodyLayout()->addWidget(runningScroll, 1);

    for (const auto &e : MarketModel::collectInstalledDrivers()) {
        if (!MarketIndex::matchWords(text, e.searchFields))
            continue;
        installedLay->addWidget(makeRow(e));
        ++nInstalled;
    }
    for (const auto &e : MarketModel::collectInstalledPlugins()) {
        if (!MarketIndex::matchWords(text, e.searchFields))
            continue;
        installedLay->addWidget(makeRow(e));
        ++nInstalled;
        if (pm && pm->isPluginActivated(e.item.id)) {
            runningLay->addWidget(makeRow(e));
            ++nRunning;
        }
    }

    if (nInstalled == 0) {
        auto *empty = new QLabel(QStringLiteral("No installed items"), installedHost);
        empty->setStyleSheet(QStringLiteral("color: %1; padding: 8px;")
            .arg(ThemeManager::instance()->currentTheme().textDim));
        installedLay->addWidget(empty);
    }
    if (nRunning == 0) {
        auto *empty = new QLabel(QStringLiteral("No running plugins"), runningHost);
        empty->setStyleSheet(QStringLiteral("color: %1; padding: 8px;")
            .arg(ThemeManager::instance()->currentTheme().textDim));
        runningLay->addWidget(empty);
    }
    installedLay->addStretch(1);
    runningLay->addStretch(1);

    m_installedSection->setTitle(
        QStringLiteral("Installed (%1)").arg(nInstalled));
    m_runningSection->setTitle(
        QStringLiteral("Running (%1)").arg(nRunning));
}

FrameRow *ExtensionsPanel::makeRow(const MarketEntryData &e)
{
    auto *row = new FrameRow;
    row->item = e.item;

    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 0, 4, 0);
    lay->setSpacing(4);
    row->setFixedHeight(22);

    // Icon: local package SVG first (matches marketplace), else market asset,
    // else letter avatar. Never overwrite a good local SVG with a failed fetch.
    auto *icon = new QLabel;
    icon->setFixedSize(16, 16);
    icon->setAlignment(Qt::AlignCenter);
    row->iconLabel = icon;
    bool haveReal = false;
    if (e.item.kind == MarketItem::InstalledPlugin) {
        const QPixmap local = MarketModel::pluginIconLocal(e.item.id);
        if (!local.isNull()) {
            icon->setPixmap(local.scaled(16, 16, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
            haveReal = true;
        }
    }
    if (!haveReal)
        icon->setPixmap(PluginUi::pluginIconPixmap(QString(), e.title, 16));
    if (!haveReal && !e.marketIcon.isEmpty()) {
        QPointer<QLabel> g(icon);
        MarketModel::fetchMarketPixmap(
            MarketIndex::instance()->resolveUrl(e.marketIcon),
            [g](const QPixmap &pm) {
                if (g && !pm.isNull())
                    g->setPixmap(pm.scaled(16, 16, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
            });
    }
    lay->addWidget(icon, 0, Qt::AlignVCenter);

    auto *titleLabel = new QLabel(e.title);
    titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QString tip = e.meta.isEmpty()
        ? e.title
        : QStringLiteral("%1\n%2").arg(e.title, e.meta);
    if (e.item.kind == MarketItem::InstalledPlugin)
        tip += QStringLiteral("\nDouble-click to run; gear for more actions");
    titleLabel->setToolTip(tip);
    row->setToolTip(tip);
    lay->addWidget(titleLabel, 1);

    if (!e.status.isEmpty()) {
        auto *statusLabel = new QLabel(e.status);
        statusLabel->setStyleSheet(QStringLiteral(
            "color: %1; background: transparent; border: none;")
            .arg(ThemeManager::instance()->currentTheme().textDim));
        lay->addWidget(statusLabel, 0, Qt::AlignVCenter);
    }

    // Gear menu (installed driver / plugin)
    if (e.item.kind == MarketItem::InstalledPlugin
        || e.item.kind == MarketItem::InstalledDriver) {
        auto *gear = new QToolButton;
        gear->setObjectName(QStringLiteral("ExplorerSectionAction"));
        gear->setIcon(svgIcon(":/icons/gear.svg",
                              ThemeManager::instance()->currentTheme().text, 12));
        gear->setToolTip(QStringLiteral("More actions"));
        gear->setAutoRaise(true);
        gear->setFixedSize(16, 16);
        MarketEntryData entry = e;
        connect(gear, &QToolButton::clicked, this, [this, gear, entry]() {
            showGearMenu(entry, gear->mapToGlobal(QPoint(0, gear->height())));
        });
        lay->addWidget(gear, 0, Qt::AlignVCenter);
    }

    // Non-button children: pass clicks through to the row
    icon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    for (QLabel *l : row->findChildren<QLabel *>())
        l->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    row->setOnClick([this, item = e.item]() { emit itemActivated(item); });
    if (e.item.kind == MarketItem::InstalledPlugin) {
        row->setOnDoubleClick([this, id = e.item.id]() {
            emit pluginActivated(id);
        });
    }
    return row;
}

void ExtensionsPanel::addCommand(const QString &, const QString &)
{
}

void ExtensionsPanel::clearCommands()
{
}



void ExtensionsPanel::onMenuClicked()
{
    QMenu menu(this);
    auto *installAct = menu.addAction(QStringLiteral("Install from .odp / .opk…"));
    connect(installAct, &QAction::triggered, this, [this]() {
        emit installFromFileRequested();
    });
    auto *openAct = menu.addAction(QStringLiteral("Open Marketplace"));
    connect(openAct, &QAction::triggered, this, [this]() {
        emit openMarketRequested();
    });
    auto *refreshAct = menu.addAction(QStringLiteral("Refresh"));
    connect(refreshAct, &QAction::triggered, this, [this]() {
        MarketIndex::instance()->refresh();
        DriverRegistry::instance()->scanAndLoad();
        refreshEntries();
    });
    menu.exec(m_menuBtn->mapToGlobal(QPoint(0, m_menuBtn->height())));
}

void ExtensionsPanel::showGearMenu(const MarketEntryData &e, const QPoint &globalPos)
{
    QMenu menu(this);

    auto *viewAct = menu.addAction(QStringLiteral("View in Marketplace"));
    connect(viewAct, &QAction::triggered, this, [this, item = e.item]() {
        emit itemActivated(item);
    });
    menu.addSeparator();

    if (e.item.kind == MarketItem::InstalledPlugin) {
        auto *pm = PluginManager::instance();
        const bool enabled = pm->isPluginEnabled(e.item.id);
        const bool activated = pm->isPluginActivated(e.item.id);
        if (!enabled) {
            auto *enableAct = menu.addAction(QStringLiteral("Enable"));
            connect(enableAct, &QAction::triggered, this, [this, id = e.item.id]() {
                emit pluginToggleRequested(id, true);
            });
        } else {
            if (activated) {
                auto *stopAct = menu.addAction(QStringLiteral("Stop"));
                connect(stopAct, &QAction::triggered, this, [this, id = e.item.id]() {
                    emit pluginDeactivateRequested(id);
                });
            }
            auto *startAct = menu.addAction(activated ? QStringLiteral("Restart")
                                                      : QStringLiteral("Start"));
            connect(startAct, &QAction::triggered, this, [this, id = e.item.id]() {
                emit pluginActivated(id);
            });
            auto *disableAct = menu.addAction(QStringLiteral("Disable"));
            connect(disableAct, &QAction::triggered, this, [this, id = e.item.id]() {
                emit pluginToggleRequested(id, false);
            });
        }
        menu.addSeparator();
        auto *uninstallAct = menu.addAction(QStringLiteral("Uninstall"));
        connect(uninstallAct, &QAction::triggered, this, [this, id = e.item.id]() {
            emit pluginUninstallRequested(id);
        });
    } else {
        const auto drivers = DriverRegistry::instance()->drivers();
        bool enabledNow = true;
        bool builtin = false;
        for (const auto &d : drivers) {
            if (d.driverId == e.item.id) {
                enabledNow = d.enabled;
                builtin = d.builtin;
                break;
            }
        }
        auto *toggleAct = menu.addAction(enabledNow ? QStringLiteral("Disable driver")
                                                     : QStringLiteral("Enable driver"));
        toggleAct->setToolTip(QStringLiteral(
            "When disabled, the driver is hidden from the device tree and not loaded"));
        connect(toggleAct, &QAction::triggered, this,
                [this, id = e.item.id, to = !enabledNow]() {
            emit driverToggleRequested(id, to);
        });
        auto *uninstallAct = menu.addAction(QStringLiteral("Uninstall driver"));
        uninstallAct->setEnabled(!builtin);
        uninstallAct->setToolTip(builtin
            ? QStringLiteral("Built-in drivers cannot be uninstalled")
            : QStringLiteral("Loaded DLLs stay in memory until the app restarts"));
        connect(uninstallAct, &QAction::triggered, this, [this, id = e.item.id]() {
            emit driverUninstallRequested(id);
        });
    }
    menu.exec(globalPos);
}

// ============================================================
//  SideBar — 10 个面板，索引与 ActivityBar 一致
//  0=Project  1=Analysis(Flow)  2=Device  3=Trace  4=Graphic
//  5=Dbc      6=Transceive     7=Extensions        8=Settings
// ============================================================

SideBar::SideBar(QWidget *parent)
    : QStackedWidget(parent)
{
    setObjectName("SideBar");
    setAttribute(Qt::WA_StyledBackground, true);

    m_project      = new ProjectPanel(this);
    m_trace        = new TracePanel(this);
    m_graphicConfig = new GraphicConfigPanel(this);
    m_dbc          = new DbcPanel(this);
    m_transceive   = new TransceivePanel(this);
    m_device       = new DevicePanel(this);
    m_analysis     = new MeasurementSetupPanel(this);
    m_extensions   = new ExtensionsPanel(this);
    m_settings     = new SettingsPanel(this);

    addWidget(m_project);        // 0 = Project
    addWidget(m_analysis);       // 1 = Analysis (Flow)
    addWidget(m_device);         // 2 = Device
    addWidget(m_trace);          // 3 = Trace
    addWidget(m_graphicConfig);  // 4 = Graphic
    addWidget(m_dbc);            // 5 = Dbc
    addWidget(m_transceive);     // 6 = Transceive (收发)
    addWidget(m_extensions);     // 7 = Extensions
    addWidget(m_settings);       // 8 = Settings

    setCurrentIndex(0);
    setMinimumWidth(240);
    setMaximumWidth(500);
}

void SideBar::showPanel(int index)
{
    if (index < 0 || index >= count()) return;
    setCurrentIndex(index);
}

void SideBar::togglePanel(int index)
{
    Q_UNUSED(index);
}

#include "spliteditorarea.h"
#include "core/signalrelay.h"
#include "thememanager.h"
#include "utils/svg_icon.h"

#include <QMenu>
#include <QAction>
#include <QTabBar>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QToolButton>
#include <QPainter>
#include <QPen>
#include <QHash>
#include <QVariantList>

// ============================================================
//  DockDropOverlay — top-level preview (always visible on Windows)
// ============================================================

class DockDropOverlay : public QWidget
{
public:
    explicit DockDropOverlay()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint
                               | Qt::WindowStaysOnTopHint
                               | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void setZone(SplitEditorArea::DockZone zone)
    {
        if (m_zone == zone)
            return;
        m_zone = zone;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 36));

        const QColor ghost(0, 95, 184, 45);
        for (auto z : {SplitEditorArea::DockZone::Center,
                       SplitEditorArea::DockZone::Left,
                       SplitEditorArea::DockZone::Right,
                       SplitEditorArea::DockZone::Top,
                       SplitEditorArea::DockZone::Bottom}) {
            p.fillRect(zoneRect(z), ghost);
        }

        if (m_zone == SplitEditorArea::DockZone::None
            || m_zone == SplitEditorArea::DockZone::Float)
            return;

        const QRect hi = zoneRect(m_zone);
        p.fillRect(hi, QColor(0, 95, 184, 120));
        p.setPen(QPen(QColor(0, 95, 184), 2));
        p.drawRect(hi.adjusted(1, 1, -2, -2));

        // Center crosshair hint (VS Code-like)
        if (m_zone == SplitEditorArea::DockZone::Center) {
            const int cx = hi.center().x();
            const int cy = hi.center().y();
            p.drawLine(cx - 14, cy, cx + 14, cy);
            p.drawLine(cx, cy - 14, cx, cy + 14);
        }
    }

private:
    QRect zoneRect(SplitEditorArea::DockZone z) const
    {
        const int w = width();
        const int h = height();
        const int mw = qMax(64, w / 3);
        const int mh = qMax(64, h / 3);
        switch (z) {
        case SplitEditorArea::DockZone::Left:
            return QRect(0, 0, mw, h);
        case SplitEditorArea::DockZone::Right:
            return QRect(w - mw, 0, mw, h);
        case SplitEditorArea::DockZone::Top:
            return QRect(0, 0, w, mh);
        case SplitEditorArea::DockZone::Bottom:
            return QRect(0, h - mh, w, mh);
        case SplitEditorArea::DockZone::Center:
        default:
            return QRect(mw, mh, w - 2 * mw, h - 2 * mh);
        }
    }

    SplitEditorArea::DockZone m_zone = SplitEditorArea::DockZone::None;
};

// ============================================================
//  DetachedTabWindow — full-bleed content, no duplicate tab strip
// ============================================================

DetachedTabWindow::DetachedTabWindow(QWidget *widget, const QString &label,
                                     SplitEditorArea *editor, QWidget *parent)
    : QMainWindow(parent)
    , m_widget(widget)
    , m_label(label)
    , m_editor(editor)
{
    setWindowTitle(label);
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    setStatusTip(QStringLiteral("Double-click the title bar to dock back into the main window"));
    resize(900, 640);

    // Page fills the window — title bar already shows the name (no inner tab strip)
    if (widget) {
        widget->setVisible(true);
        setCentralWidget(widget);
    }
}

void DetachedTabWindow::mergeBack()
{
    if (!m_widget)
        return;
    QWidget *w = m_widget;
    const QString lbl = m_label;
    m_suppressReattach = true;
    m_widget = nullptr;
    takeCentralWidget();
    if (w)
        w->setParent(nullptr);
    emit reattachRequested(w, lbl);
    close();
}

QWidget *DetachedTabWindow::takePage()
{
    m_suppressReattach = true;
    QWidget *w = m_widget;
    m_widget = nullptr;
    takeCentralWidget();
    if (w)
        w->setParent(nullptr);
    close();
    return w;
}

void DetachedTabWindow::destroyContent()
{
    m_suppressReattach = true;
    if (m_widget) {
        takeCentralWidget();
        delete m_widget;
        m_widget = nullptr;
    }
    close();
}

bool DetachedTabWindow::event(QEvent *event)
{
    // Native title-bar double-click → merge into main editor
    if (event->type() == QEvent::NonClientAreaMouseButtonDblClick) {
        mergeBack();
        return true;
    }
    return QMainWindow::event(event);
}

void DetachedTabWindow::closeEvent(QCloseEvent *event)
{
    if (!m_suppressReattach && m_widget) {
        QWidget *w = m_widget;
        const QString lbl = m_label;
        m_widget = nullptr;
        takeCentralWidget();
        if (w)
            w->setParent(nullptr);
        emit reattachRequested(w, lbl);
    }
    event->accept();
}

// ============================================================
//  TabBarHoverFilter
// ============================================================

class TabBarHoverFilter : public QObject
{
public:
    explicit TabBarHoverFilter(QTabWidget *tabs) : QObject(tabs), m_tabs(tabs) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *bar = qobject_cast<QTabBar *>(watched);
        if (!bar)
            return QObject::eventFilter(watched, event);
        if (event->type() == QEvent::MouseMove
            || event->type() == QEvent::Leave
            || event->type() == QEvent::Show
            || event->type() == QEvent::Resize) {
            for (int i = 0; i < m_tabs->tabBar()->count(); ++i) {
                auto *btn = m_tabs->tabBar()->tabButton(i, QTabBar::RightSide);
                if (!btn)
                    continue;
                QWidget *w = m_tabs->widget(i);
                btn->setVisible(!(w && w->property("pinned").toBool()));
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTabWidget *m_tabs;
};

// ============================================================
//  DockTabBar — owns press/move so drag always starts (event filters
//  were unreliable vs QTabBar + close-button chrome).
// ============================================================

class DockTabBar : public QTabBar
{
public:
    DockTabBar(QTabWidget *tabs, SplitEditorArea *area)
        : QTabBar(tabs)
        , m_tabs(tabs)
        , m_area(area)
    {
        setMouseTracking(true);
        setAcceptDrops(false);
        setElideMode(Qt::ElideRight);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressIndex = tabAt(event->pos());
            // Don't start a dock-drag from the close button
            if (m_pressIndex >= 0) {
                if (QWidget *btn = tabButton(m_pressIndex, QTabBar::RightSide)) {
                    const QPoint topLeft = btn->mapTo(this, QPoint(0, 0));
                    if (QRect(topLeft, btn->size()).contains(event->pos()))
                        m_pressIndex = -1;
                }
            }
            m_pressGlobal = event->globalPosition().toPoint();
            m_dragStarted = false;
        }
        QTabBar::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (m_area && m_area->isDockDragging())
            return;

        if (!m_dragStarted
            && m_pressIndex >= 0
            && (event->buttons() & Qt::LeftButton)
            && m_area
            && m_pressIndex < m_tabs->count()) {
            const int dist = (event->globalPosition().toPoint() - m_pressGlobal)
                                 .manhattanLength();
            if (dist >= QApplication::startDragDistance()) {
                m_dragStarted = true;
                const int idx = m_pressIndex;
                m_pressIndex = -1;
                // Abort QTabBar's internal press state
                QMouseEvent release(QEvent::MouseButtonRelease, event->pos(),
                                    event->globalPosition().toPoint(),
                                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QTabBar::mouseReleaseEvent(&release);
                m_area->beginDockDragFromTab(m_tabs, idx);
                return;
            }
        }
        QTabBar::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        m_pressIndex = -1;
        m_dragStarted = false;
        if (m_area && m_area->isDockDragging())
            return;
        QTabBar::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        const int idx = tabAt(event->pos());
        if (idx >= 0 && m_area) {
            m_area->detachTab(m_tabs, idx);
            return;
        }
        QTabBar::mouseDoubleClickEvent(event);
    }

private:
    QTabWidget *m_tabs = nullptr;
    SplitEditorArea *m_area = nullptr;
    int m_pressIndex = -1;
    QPoint m_pressGlobal;
    bool m_dragStarted = false;
};

/// QTabWidget subclass so we can call protected setTabBar().
class DockTabWidget : public QTabWidget
{
public:
    DockTabWidget(SplitEditorArea *area, QWidget *parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new DockTabBar(this, area));
    }
};

// ============================================================
//  SplitEditorArea
// ============================================================

SplitEditorArea::SplitEditorArea(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_rootSplitter = new QSplitter(Qt::Horizontal, this);
    layout->addWidget(m_rootSplitter);

    m_firstTabs = createTabWidget();
    m_rootSplitter->addWidget(m_firstTabs);

    m_overlay = new DockDropOverlay();
}

SplitEditorArea::~SplitEditorArea()
{
    cancelDockDrag();
    discardDetachedWindows();
    delete m_overlay;
    m_overlay = nullptr;
}

QTabWidget *SplitEditorArea::createTabWidget()
{
    auto *tabs = new DockTabWidget(this, this);
    tabs->setObjectName(QStringLiteral("EditorTabPane"));
    tabs->setTabsClosable(true);
    tabs->setMovable(false);
    tabs->setDocumentMode(true);

    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs->tabBar(), &QWidget::customContextMenuRequested,
            this, [this, tabs](const QPoint &pos) {
        const int idx = tabs->tabBar()->tabAt(pos);
        if (idx >= 0)
            onTabBarContextMenu(idx, pos);
    });

    connect(tabs, &QTabWidget::tabCloseRequested, this, [this, tabs](int index) {
        closeTab(tabs, index);
    });

    tabs->tabBar()->setMouseTracking(true);
    tabs->tabBar()->installEventFilter(new TabBarHoverFilter(tabs));

    connect(tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        emit currentChanged(idx);
    });

    return tabs;
}

void SplitEditorArea::installDragOutFilter(QTabWidget *tabs)
{
    Q_UNUSED(tabs);
    // Drag is handled by DockTabBar (set in createTabWidget).
}

int SplitEditorArea::addTab(QWidget *widget, const QString &label)
{
    QTabWidget *target = activeTabWidget();
    if (!target)
        target = m_firstTabs;
    const int idx = target->addTab(widget, label);
    setupCloseButton(target, idx);
    target->setCurrentIndex(idx);
    emit tabListChanged();
    return idx;
}

void SplitEditorArea::setupCloseButton(QTabWidget *tabs, int index)
{
    if (!tabs || index < 0 || index >= tabs->count())
        return;
    auto *btn = new QToolButton(tabs->tabBar());
    btn->setIcon(svgIcon(QStringLiteral(":/icons/close.svg"),
                         ThemeManager::instance()->currentTheme().text, 14));
    btn->setAutoRaise(true);
    btn->setFixedSize(16, 16);
    btn->setToolTip(QStringLiteral("Close tab"));
    QWidget *w = tabs->widget(index);
    connect(btn, &QToolButton::clicked, this, [this, tabs, w]() {
        const int i = tabs->indexOf(w);
        if (i >= 0)
            closeTab(tabs, i);
    });
    auto *closeBtnRelay = new SignalRelay(btn);
    closeBtnRelay->fire0 = [btn]() {
        btn->setIcon(svgIcon(QStringLiteral(":/icons/close.svg"),
                             ThemeManager::instance()->currentTheme().text, 14));
    };
    connect(ThemeManager::instance(), SIGNAL(themeChanged(QString)),
            closeBtnRelay, SLOT(fire()));
    tabs->tabBar()->setTabButton(index, QTabBar::RightSide, btn);
    btn->setVisible(!(w && w->property("pinned").toBool()));
}

QWidget *SplitEditorArea::currentWidget() const
{
    auto *tabs = activeTabWidget();
    return tabs ? tabs->currentWidget() : nullptr;
}

QTabWidget *SplitEditorArea::activeTabWidget() const
{
    for (auto *w : allTabWidgets()) {
        if (w->hasFocus() || (w->tabBar() && w->tabBar()->hasFocus()))
            return w;
    }
    for (auto *tw : allTabWidgets()) {
        if (tw->count() > 0)
            return tw;
    }
    return m_firstTabs;
}

bool SplitEditorArea::isEditorTabPane(const QTabWidget *tw)
{
    return tw && tw->objectName() == QLatin1String("EditorTabPane");
}

bool SplitEditorArea::isEditorChromeSplitter(const QSplitter *sp) const
{
    if (!sp)
        return false;
    const QWidget *p = sp;
    while (p) {
        if (p == m_rootSplitter || p == this)
            return true;
        if (qobject_cast<const QSplitter *>(p)) {
            p = p->parentWidget();
            continue;
        }
        return false; // inside TraceTab / Graphic / other page content
    }
    return false;
}

QList<QTabWidget *> SplitEditorArea::allTabWidgets() const
{
    // CRITICAL: do NOT use bare findChildren<QTabWidget*> — that also hits
    // TraceTab's Detail/Statistics/Signals explorer and tears pages apart.
    QList<QTabWidget *> out;
    if (!m_rootSplitter)
        return out;
    for (auto *tw : m_rootSplitter->findChildren<QTabWidget *>()) {
        if (isEditorTabPane(tw))
            out << tw;
    }
    return out;
}

void SplitEditorArea::beginDockDragFromTab(QTabWidget *tabs, int index)
{
    if (m_drag.active || !tabs || index < 0 || index >= tabs->count())
        return;

    m_drag.active = true;
    m_drag.sourceTabs = tabs;
    m_drag.sourceIndex = index;
    m_drag.widget = tabs->widget(index);
    m_drag.label = tabs->tabText(index);
    m_drag.sourceFloat = nullptr;
    m_drag.fromFloat = false;

    qApp->installEventFilter(this);
    grabMouse();
    QApplication::setOverrideCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
    // Show overlay immediately on the source pane (even while still on the tab strip)
    if (tabs)
        showOverlay(tabs, DockZone::Center);
    updateDockDrag(QCursor::pos());
}

void SplitEditorArea::beginDockDragFromFloat(DetachedTabWindow *win)
{
    if (m_drag.active || !win || !win->containedWidget())
        return;

    m_drag.active = true;
    m_drag.sourceTabs = nullptr;
    m_drag.sourceIndex = -1;
    m_drag.widget = win->containedWidget();
    m_drag.label = win->label();
    m_drag.sourceFloat = win;
    m_drag.fromFloat = true;

    qApp->installEventFilter(this);
    grabMouse();
    QApplication::setOverrideCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
    updateDockDrag(QCursor::pos());
}

void SplitEditorArea::updateDockDrag(const QPoint &globalPos)
{
    if (!m_drag.active)
        return;

    auto *target = tabWidgetAtGlobal(globalPos);
    if (!target && m_drag.sourceTabs)
        target = m_drag.sourceTabs;
    if (!target) {
        hideOverlay();
        return;
    }
    showOverlay(target, hitTestZone(target, globalPos));
}

void SplitEditorArea::finishDockDrag(const QPoint &globalPos)
{
    if (!m_drag.active)
        return;

    QWidget *widget = m_drag.widget;
    const QString label = m_drag.label;
    QTabWidget *sourceTabs = m_drag.sourceTabs;
    const int sourceIndex = m_drag.sourceIndex;
    QPointer<DetachedTabWindow> srcFloat = m_drag.sourceFloat;
    const bool fromFloat = m_drag.fromFloat;

    m_drag = DockDragState{};
    qApp->removeEventFilter(this);
    releaseMouse();
    QApplication::restoreOverrideCursor();
    hideOverlay();

    if (!widget)
        return;

    auto *target = tabWidgetAtGlobal(globalPos);
    DockZone zone = DockZone::Float;
    if (target)
        zone = hitTestZone(target, globalPos);

    // Same pane + Center → no-op (keep order)
    if (!fromFloat && sourceTabs && target == sourceTabs
        && zone == DockZone::Center)
        return;

    if (fromFloat && (zone == DockZone::Float || !target))
        return;

    if (!fromFloat && sourceTabs && sourceIndex >= 0
        && sourceIndex < sourceTabs->count()
        && sourceTabs->widget(sourceIndex) == widget) {
        sourceTabs->removeTab(sourceIndex);
    }
    if (fromFloat && srcFloat) {
        widget = srcFloat->takePage();
        if (!widget)
            return;
    }

    if (zone == DockZone::Float || !target) {
        createDetachedWindow(widget, label, globalPos);
    } else {
        dropWidget(target, zone, widget, label);
    }

    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::cancelDockDrag()
{
    if (!m_drag.active)
        return;
    m_drag = DockDragState{};
    qApp->removeEventFilter(this);
    releaseMouse();
    QApplication::restoreOverrideCursor();
    hideOverlay();
}

bool SplitEditorArea::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (!m_drag.active)
        return false;

    if (event->type() == QEvent::MouseMove) {
        const auto *me = static_cast<QMouseEvent *>(event);
        updateDockDrag(me->globalPosition().toPoint());
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            finishDockDrag(me->globalPosition().toPoint());
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        const auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            cancelDockDrag();
            return true;
        }
    }
    return false;
}

void SplitEditorArea::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag.active)
        updateDockDrag(event->globalPosition().toPoint());
    QWidget::mouseMoveEvent(event);
}

void SplitEditorArea::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_drag.active && event->button() == Qt::LeftButton) {
        finishDockDrag(event->globalPosition().toPoint());
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SplitEditorArea::keyPressEvent(QKeyEvent *event)
{
    if (m_drag.active && event->key() == Qt::Key_Escape) {
        cancelDockDrag();
        return;
    }
    QWidget::keyPressEvent(event);
}

QTabWidget *SplitEditorArea::tabWidgetAtGlobal(const QPoint &globalPos) const
{
    // Prefer the pane under the cursor (ignore overlay — it is transparent)
    QTabWidget *best = nullptr;
    int bestArea = 0;
    for (auto *tw : allTabWidgets()) {
        if (!tw->isVisible())
            continue;
        const QRect r(tw->mapToGlobal(QPoint(0, 0)), tw->size());
        if (!r.contains(globalPos))
            continue;
        const int area = r.width() * r.height();
        if (!best || area < bestArea) {
            best = tw;
            bestArea = area;
        }
    }
    if (best)
        return best;

    const QRect editorRect(mapToGlobal(QPoint(0, 0)), size());
    if (editorRect.contains(globalPos))
        return activeTabWidget();
    return nullptr;
}

SplitEditorArea::DockZone SplitEditorArea::hitTestZone(QTabWidget *target,
                                                       const QPoint &globalPos) const
{
    if (!target)
        return DockZone::Float;

    const QPoint local = target->mapFromGlobal(globalPos);
    const int w = target->width();
    const int h = target->height();
    if (w <= 0 || h <= 0)
        return DockZone::Center;

    // Thirds — easier to hit edge zones (VS Code-like)
    const int mw = qMax(64, w / 3);
    const int mh = qMax(64, h / 3);
    const QRect center(mw, mh, w - 2 * mw, h - 2 * mh);
    if (center.contains(local))
        return DockZone::Center;

    if (local.x() < mw)
        return DockZone::Left;
    if (local.x() >= w - mw)
        return DockZone::Right;
    if (local.y() < mh)
        return DockZone::Top;
    if (local.y() >= h - mh)
        return DockZone::Bottom;
    return DockZone::Center;
}

void SplitEditorArea::showOverlay(QTabWidget *target, DockZone zone)
{
    if (!m_overlay || !target)
        return;
    const QRect g(target->mapToGlobal(QPoint(0, 0)), target->size());
    m_overlay->setGeometry(g);
    m_overlay->setZone(zone);
    m_overlay->show();
    m_overlay->raise();
}

void SplitEditorArea::hideOverlay()
{
    if (m_overlay)
        m_overlay->hide();
}

void SplitEditorArea::dropWidget(QTabWidget *target, DockZone zone,
                                 QWidget *widget, const QString &label)
{
    if (!widget)
        return;
    if (!target)
        target = m_firstTabs;

    switch (zone) {
    case DockZone::Left:
        insertBeside(target, widget, label, Qt::Horizontal, true);
        break;
    case DockZone::Right:
        insertBeside(target, widget, label, Qt::Horizontal, false);
        break;
    case DockZone::Top:
        insertBeside(target, widget, label, Qt::Vertical, true);
        break;
    case DockZone::Bottom:
        insertBeside(target, widget, label, Qt::Vertical, false);
        break;
    case DockZone::Center:
    default: {
        const int idx = target->addTab(widget, label);
        setupCloseButton(target, idx);
        target->setCurrentIndex(idx);
        break;
    }
    }
}

void SplitEditorArea::insertBeside(QTabWidget *target, QWidget *widget,
                                   const QString &label, Qt::Orientation orient,
                                   bool before)
{
    auto *newTabs = createTabWidget();
    const int idx = newTabs->addTab(widget, label);
    setupCloseButton(newTabs, idx);
    newTabs->setCurrentIndex(idx);

    QSplitter *split = parentSplitter(target);
    if (!split) {
        m_firstTabs->addTab(widget, label);
        newTabs->deleteLater();
        return;
    }

    if (split->orientation() == orient) {
        const int at = split->indexOf(target);
        split->insertWidget(before ? at : at + 1, newTabs);
        QList<int> sizes = split->sizes();
        if (sizes.size() >= 2) {
            const int total = sizes.value(at, 400) + (before ? 0 : 0);
            Q_UNUSED(total);
            // Equalize adjacent panes
            for (int i = 0; i < sizes.size(); ++i)
                sizes[i] = 500;
            split->setSizes(sizes);
        }
    } else {
        const int at = split->indexOf(target);
        auto *newSplit = new QSplitter(orient, this);
        split->replaceWidget(at, newSplit);
        if (before) {
            newSplit->addWidget(newTabs);
            newSplit->addWidget(target);
        } else {
            newSplit->addWidget(target);
            newSplit->addWidget(newTabs);
        }
        newSplit->setSizes({500, 500});
    }
}

void SplitEditorArea::registerDetachedWindow(DetachedTabWindow *win)
{
    m_detachedWindows.append(win);
    connect(win, &DetachedTabWindow::reattachRequested,
            this, [this](QWidget *w, const QString &lbl) {
        reattachTab(w, lbl);
    });
    connect(win, &QObject::destroyed, this, [this, win](QObject *) {
        m_detachedWindows.removeAll(win);
    });
}

DetachedTabWindow *SplitEditorArea::createDetachedWindow(QWidget *widget,
                                                         const QString &label,
                                                         const QPoint &globalPos,
                                                         const QRect &geometry)
{
    auto *win = new DetachedTabWindow(widget, label, this, nullptr);
    registerDetachedWindow(win);

    if (geometry.isValid()) {
        win->setGeometry(geometry);
    } else if (QScreen *scr = QGuiApplication::screenAt(globalPos)) {
        const QRect ag = scr->availableGeometry();
        QPoint pos = globalPos - QPoint(win->width() / 2, 40);
        pos.setX(qBound(ag.left(), pos.x(), ag.right() - win->width()));
        pos.setY(qBound(ag.top(), pos.y(), ag.bottom() - 80));
        win->move(pos);
    } else {
        win->move(globalPos - QPoint(win->width() / 2, 40));
    }

    win->show();
    win->raise();
    win->activateWindow();
    return win;
}

void SplitEditorArea::detachTab(QTabWidget *tabs, int index)
{
    if (!tabs || index < 0 || index >= tabs->count())
        return;
    QWidget *widget = tabs->widget(index);
    const QString label = tabs->tabText(index);
    tabs->removeTab(index);
    createDetachedWindow(widget, label, QCursor::pos());
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::reattachTab(QWidget *widget, const QString &label)
{
    if (!widget)
        return;
    const int idx = addTab(widget, label);
    if (auto *tabs = activeTabWidget())
        tabs->setCurrentIndex(idx);
    emit tabListChanged();
}

void SplitEditorArea::discardDetachedWindows()
{
    const auto copy = m_detachedWindows;
    for (DetachedTabWindow *win : copy) {
        if (win)
            win->destroyContent();
    }
    m_detachedWindows.clear();
}

// ---- Layout persistence -------------------------------------------------

QVariantMap SplitEditorArea::serializeWidget(QWidget *w) const
{
    QVariantMap m;
    if (auto *tabs = qobject_cast<QTabWidget *>(w)) {
        m.insert(QStringLiteral("type"), QStringLiteral("tabs"));
        QStringList titles;
        for (int i = 0; i < tabs->count(); ++i)
            titles << tabs->tabText(i);
        m.insert(QStringLiteral("tabs"), titles);
        m.insert(QStringLiteral("current"), tabs->currentIndex());
        return m;
    }
    if (auto *sp = qobject_cast<QSplitter *>(w)) {
        m.insert(QStringLiteral("type"), QStringLiteral("splitter"));
        m.insert(QStringLiteral("orient"),
                 sp->orientation() == Qt::Horizontal ? QStringLiteral("h")
                                                     : QStringLiteral("v"));
        QVariantList sizes;
        for (int s : sp->sizes())
            sizes << s;
        m.insert(QStringLiteral("sizes"), sizes);
        QVariantList children;
        for (int i = 0; i < sp->count(); ++i) {
            if (QWidget *ch = sp->widget(i))
                children << serializeWidget(ch);
        }
        m.insert(QStringLiteral("children"), children);
        return m;
    }
    m.insert(QStringLiteral("type"), QStringLiteral("empty"));
    return m;
}

QVariantMap SplitEditorArea::saveLayout() const
{
    QVariantMap root;
    root.insert(QStringLiteral("version"), 1);
    if (m_rootSplitter)
        root.insert(QStringLiteral("tree"), serializeWidget(m_rootSplitter));

    QVariantList detached;
    for (DetachedTabWindow *win : m_detachedWindows) {
        if (!win || !win->containedWidget())
            continue;
        QVariantMap d;
        d.insert(QStringLiteral("title"), win->label());
        const QRect g = win->geometry();
        d.insert(QStringLiteral("x"), g.x());
        d.insert(QStringLiteral("y"), g.y());
        d.insert(QStringLiteral("w"), g.width());
        d.insert(QStringLiteral("h"), g.height());
        detached << d;
    }
    root.insert(QStringLiteral("detached"), detached);

    if (QWidget *cur = currentWidget()) {
        for (auto *tw : allTabWidgets()) {
            const int i = tw->indexOf(cur);
            if (i >= 0) {
                root.insert(QStringLiteral("active"), tw->tabText(i));
                break;
            }
        }
    }
    return root;
}

void SplitEditorArea::collectTitlesFromNode(const QVariantMap &node,
                                            QStringList &out) const
{
    const QString type = node.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("tabs")) {
        out << node.value(QStringLiteral("tabs")).toStringList();
        return;
    }
    if (type == QLatin1String("splitter")) {
        const QVariantList children = node.value(QStringLiteral("children")).toList();
        for (const QVariant &c : children)
            collectTitlesFromNode(c.toMap(), out);
    }
}

QStringList SplitEditorArea::collectTabTitles() const
{
    QStringList out;
    for (auto *tw : allTabWidgets()) {
        for (int i = 0; i < tw->count(); ++i)
            out << tw->tabText(i);
    }
    for (DetachedTabWindow *win : m_detachedWindows) {
        if (win)
            out << win->label();
    }
    return out;
}

QWidget *SplitEditorArea::buildFromLayout(const QVariantMap &node,
                                          QHash<QString, QWidget *> &pages)
{
    const QString type = node.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("tabs")) {
        auto *tabs = createTabWidget();
        const QStringList titles = node.value(QStringLiteral("tabs")).toStringList();
        int current = node.value(QStringLiteral("current")).toInt();
        for (const QString &title : titles) {
            QWidget *page = pages.take(title);
            if (!page)
                continue;
            const int idx = tabs->addTab(page, title);
            setupCloseButton(tabs, idx);
        }
        if (tabs->count() > 0)
            tabs->setCurrentIndex(qBound(0, current, tabs->count() - 1));
        return tabs;
    }
    if (type == QLatin1String("splitter")) {
        const bool horiz = node.value(QStringLiteral("orient")).toString()
                           != QLatin1String("v");
        auto *sp = new QSplitter(horiz ? Qt::Horizontal : Qt::Vertical, this);
        const QVariantList children = node.value(QStringLiteral("children")).toList();
        for (const QVariant &c : children) {
            if (QWidget *ch = buildFromLayout(c.toMap(), pages))
                sp->addWidget(ch);
        }
        QList<int> sizes;
        for (const QVariant &s : node.value(QStringLiteral("sizes")).toList())
            sizes << s.toInt();
        if (!sizes.isEmpty() && sizes.size() == sp->count())
            sp->setSizes(sizes);
        return sp;
    }
    return createTabWidget();
}

void SplitEditorArea::restoreLayout(const QVariantMap &layout)
{
    if (layout.isEmpty())
        return;

    // Harvest all pages (main + float) keyed by title
    QHash<QString, QWidget *> pages;
    for (auto *tw : allTabWidgets()) {
        for (int i = tw->count() - 1; i >= 0; --i) {
            const QString title = tw->tabText(i);
            QWidget *w = tw->widget(i);
            tw->removeTab(i);
            if (w)
                pages.insert(title, w);
        }
    }
    const auto floats = m_detachedWindows;
    for (DetachedTabWindow *win : floats) {
        if (!win)
            continue;
        const QString title = win->label();
        if (QWidget *w = win->takePage())
            pages.insert(title, w);
    }
    m_detachedWindows.clear();

    // Tear down splitter children
    while (m_rootSplitter->count() > 0) {
        QWidget *w = m_rootSplitter->widget(0);
        w->setParent(nullptr);
        w->deleteLater();
    }

    const QVariantMap tree = layout.value(QStringLiteral("tree")).toMap();
    QWidget *built = nullptr;
    if (!tree.isEmpty())
        built = buildFromLayout(tree, pages);

    if (auto *sp = qobject_cast<QSplitter *>(built)) {
        // Promote children into root only if this is an editor splitter tree
        while (sp->count() > 0) {
            QWidget *ch = sp->widget(0);
            m_rootSplitter->addWidget(ch);
        }
        m_rootSplitter->setOrientation(sp->orientation());
        // Copy sizes if available
        const QVariantList sizes = tree.value(QStringLiteral("sizes")).toList();
        QList<int> sz;
        for (const QVariant &s : sizes)
            sz << s.toInt();
        if (!sz.isEmpty())
            m_rootSplitter->setSizes(sz);
        sp->deleteLater();
    } else if (built) {
        m_rootSplitter->addWidget(built);
    }

    if (m_rootSplitter->count() == 0) {
        m_firstTabs = createTabWidget();
        m_rootSplitter->addWidget(m_firstTabs);
    } else {
        m_firstTabs = qobject_cast<QTabWidget *>(m_rootSplitter->widget(0));
        if (!m_firstTabs) {
            auto found = allTabWidgets();
            m_firstTabs = found.isEmpty() ? createTabWidget() : found.first();
            if (m_rootSplitter->indexOf(m_firstTabs) < 0)
                m_rootSplitter->addWidget(m_firstTabs);
        }
    }

    // Floating windows first (titles not in tree stay in pages)
    const QVariantList detached = layout.value(QStringLiteral("detached")).toList();
    for (const QVariant &dv : detached) {
        const QVariantMap d = dv.toMap();
        const QString title = d.value(QStringLiteral("title")).toString();
        if (title.isEmpty() || !pages.contains(title))
            continue;
        QWidget *page = pages.take(title);
        const QRect geo(d.value(QStringLiteral("x")).toInt(),
                        d.value(QStringLiteral("y")).toInt(),
                        d.value(QStringLiteral("w")).toInt(),
                        d.value(QStringLiteral("h")).toInt());
        createDetachedWindow(page, title, geo.isValid() ? geo.center() : QCursor::pos(),
                             geo);
    }

    // Leftover pages → active pane
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        const int idx = m_firstTabs->addTab(it.value(), it.key());
        setupCloseButton(m_firstTabs, idx);
    }
    pages.clear();

    removeEmptySplits();

    const QString active = layout.value(QStringLiteral("active")).toString();
    if (!active.isEmpty()) {
        for (auto *tw : allTabWidgets()) {
            for (int i = 0; i < tw->count(); ++i) {
                if (tw->tabText(i) == active) {
                    tw->setCurrentIndex(i);
                    tw->setFocus();
                    break;
                }
            }
        }
    }

    emit tabListChanged();
}

void SplitEditorArea::onTabBarContextMenu(int index, const QPoint &pos)
{
    auto *bar = qobject_cast<QTabBar *>(sender());
    if (!bar)
        return;
    auto *tabs = qobject_cast<QTabWidget *>(bar->parentWidget());
    if (!tabs)
        return;

    QWidget *widget = tabs->widget(index);
    const bool pinned = widget && widget->property("pinned").toBool();

    auto *menu = new QMenu(this);
    auto *pinAct = menu->addAction(pinned ? QStringLiteral("Unpin")
                                          : QStringLiteral("Pin tab"));
    menu->addSeparator();
    auto *closeAct = menu->addAction(QStringLiteral("Close"));
    closeAct->setEnabled(!pinned);
    auto *closeOthersAct = menu->addAction(QStringLiteral("Close others"));
    closeOthersAct->setEnabled(tabs->count() > 1);
    auto *closeRightAct = menu->addAction(QStringLiteral("Close tabs to the right"));
    closeRightAct->setEnabled(index < tabs->count() - 1);
    auto *closeAllAct = menu->addAction(QStringLiteral("Close all"));
    menu->addSeparator();
    auto *splitRight = menu->addAction(QStringLiteral("Split right"));
    auto *splitDown = menu->addAction(QStringLiteral("Split down"));
    auto *detach = menu->addAction(QStringLiteral("Move to new window"));
    menu->addSeparator();
    auto *closeSplit = menu->addAction(QStringLiteral("Close this split group"));

    auto *chosen = menu->exec(tabs->tabBar()->mapToGlobal(pos));
    if (chosen == pinAct) {
        togglePin(tabs, index);
    } else if (chosen == closeAct) {
        closeTab(tabs, index);
    } else if (chosen == closeOthersAct) {
        closeOthers(tabs, index);
    } else if (chosen == closeRightAct) {
        closeRight(tabs, index);
    } else if (chosen == closeAllAct) {
        closeAll(tabs);
    } else if (chosen == splitRight) {
        splitTab(tabs, index, Qt::Horizontal);
    } else if (chosen == splitDown) {
        splitTab(tabs, index, Qt::Vertical);
    } else if (chosen == detach) {
        detachTab(tabs, index);
    } else if (chosen == closeSplit) {
        while (tabs->count() > 0) {
            QWidget *w = tabs->widget(0);
            const QString text = tabs->tabText(0);
            tabs->removeTab(0);
            if (tabs != m_firstTabs)
                m_firstTabs->addTab(w, text);
            else
                delete w;
        }
        if (tabs != m_firstTabs) {
            tabs->deleteLater();
            removeEmptySplits();
        }
        emit tabListChanged();
    }
    menu->deleteLater();
}

bool SplitEditorArea::isPinned(QWidget *w) const
{
    return w && w->property("pinned").toBool();
}

void SplitEditorArea::togglePin(QTabWidget *tabs, int index)
{
    QWidget *w = tabs->widget(index);
    if (!w)
        return;
    const bool newPinned = !w->property("pinned").toBool();
    w->setProperty("pinned", newPinned);
    QString text = tabs->tabText(index);
    static const QString pinPrefix = QStringLiteral("[Pin] ");
    if (newPinned) {
        if (!text.startsWith(pinPrefix))
            tabs->setTabText(index, pinPrefix + text);
    } else if (text.startsWith(pinPrefix)) {
        tabs->setTabText(index, text.mid(pinPrefix.length()));
    }
    if (auto *btn = tabs->tabBar()->tabButton(index, QTabBar::RightSide))
        btn->setVisible(!newPinned);
    emit tabListChanged();
}

void SplitEditorArea::closeTab(QTabWidget *tabs, int index)
{
    if (!tabs || index < 0 || index >= tabs->count())
        return;
    QWidget *w = tabs->widget(index);
    if (w && w->property("pinned").toBool())
        return;
    tabs->removeTab(index);
    if (w)
        w->deleteLater();
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::closeOthers(QTabWidget *tabs, int keepIndex)
{
    if (!tabs)
        return;
    for (int i = tabs->count() - 1; i >= 0; --i) {
        if (i == keepIndex)
            continue;
        QWidget *w = tabs->widget(i);
        if (w && w->property("pinned").toBool())
            continue;
        tabs->removeTab(i);
        if (w)
            w->deleteLater();
    }
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::closeRight(QTabWidget *tabs, int startIndex)
{
    if (!tabs)
        return;
    for (int i = tabs->count() - 1; i > startIndex; --i) {
        QWidget *w = tabs->widget(i);
        if (w && w->property("pinned").toBool())
            continue;
        tabs->removeTab(i);
        if (w)
            w->deleteLater();
    }
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::closeAll(QTabWidget *tabs)
{
    if (!tabs)
        return;
    for (int i = tabs->count() - 1; i >= 0; --i) {
        QWidget *w = tabs->widget(i);
        if (w && w->property("pinned").toBool())
            continue;
        tabs->removeTab(i);
        if (w)
            w->deleteLater();
    }
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::splitTab(QTabWidget *source, int index, Qt::Orientation orient)
{
    QWidget *widget = source->widget(index);
    const QString label = source->tabText(index);
    source->removeTab(index);
    insertBeside(source, widget, label, orient, /*before=*/false);
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::removeEmptySplits()
{
    for (auto *tw : allTabWidgets()) {
        if (tw == m_firstTabs)
            continue;
        if (tw->count() == 0) {
            tw->setParent(nullptr);
            tw->deleteLater();
        }
    }

    // Only collapse EDITOR chrome splitters — never Trace m_vSplitter / m_hSplitter
    const auto splitters = m_rootSplitter->findChildren<QSplitter *>();
    for (auto *split : splitters) {
        if (split == m_rootSplitter || !isEditorChromeSplitter(split))
            continue;
        if (split->count() == 1) {
            QWidget *only = split->widget(0);
            if (QSplitter *parent = parentSplitter(split)) {
                const int idx = parent->indexOf(split);
                if (idx >= 0) {
                    parent->replaceWidget(idx, only);
                    split->deleteLater();
                }
            }
        } else if (split->count() == 0) {
            split->deleteLater();
        }
    }

    if (allTabWidgets().isEmpty()) {
        m_firstTabs = createTabWidget();
        m_rootSplitter->addWidget(m_firstTabs);
    } else if (!m_firstTabs || m_rootSplitter->indexOf(m_firstTabs) < 0) {
        m_firstTabs = allTabWidgets().first();
    }
}

QSplitter *SplitEditorArea::parentSplitter(QWidget *w) const
{
    // Do not walk through page content (TraceTab, etc.) into the editor splitter —
    // that wrongly treated Trace's internal splitters as editor chrome.
    QWidget *p = w ? w->parentWidget() : nullptr;
    while (p) {
        if (auto *s = qobject_cast<QSplitter *>(p))
            return isEditorChromeSplitter(s) ? s : nullptr;
        if (p == this)
            return nullptr;
        if (auto *tw = qobject_cast<QTabWidget *>(p)) {
            if (isEditorTabPane(tw)) {
                p = p->parentWidget();
                continue;
            }
            return nullptr; // TraceExplorer / other in-page tab widget
        }
        return nullptr; // TraceTab, stacked page body, …
    }
    return nullptr;
}

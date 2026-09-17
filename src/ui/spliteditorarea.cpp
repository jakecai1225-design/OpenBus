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

// ============================================================
//  DockDropOverlay — VS / industrial docking preview
// ============================================================

class DockDropOverlay : public QWidget
{
public:
    explicit DockDropOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
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

    SplitEditorArea::DockZone zone() const { return m_zone; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);

        // Dim host pane
        p.fillRect(rect(), QColor(0, 0, 0, 28));

        // Ghost rectangles for all zones
        const QColor ghost(0, 95, 184, 35);
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
        p.fillRect(hi, QColor(0, 95, 184, 110));
        p.setPen(QPen(QColor(0, 95, 184), 2));
        p.drawRect(hi.adjusted(1, 1, -2, -2));
    }

private:
    QRect zoneRect(SplitEditorArea::DockZone z) const
    {
        const int w = width();
        const int h = height();
        const int mw = qMax(48, w / 4);
        const int mh = qMax(48, h / 4);
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
//  DetachedTabWindow
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
    resize(900, 640);

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Single-tab strip so the page can be dragged back onto the main editor
    m_tabBar = new QTabBar(central);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setDrawBase(true);
    m_tabBar->addTab(label);
    m_tabBar->setToolTip(QStringLiteral(
        "Drag onto the main window to dock. Double-click the window title to merge."));
    lay->addWidget(m_tabBar);

    if (widget) {
        widget->setParent(central);
        widget->setVisible(true);
        lay->addWidget(widget, 1);
    }
    setCentralWidget(central);

    // Forward tab-bar presses into the shared dock-drag session
    m_tabBar->installEventFilter(this);
}

QWidget *DetachedTabWindow::takePage()
{
    m_suppressReattach = true;
    QWidget *w = m_widget;
    m_widget = nullptr;
    if (w)
        w->setParent(nullptr);
    if (auto *c = takeCentralWidget())
        c->deleteLater();
    close();
    return w;
}

bool DetachedTabWindow::event(QEvent *event)
{
    // Native title-bar double-click → merge into main editor
    if (event->type() == QEvent::NonClientAreaMouseButtonDblClick) {
        if (m_widget) {
            QWidget *w = m_widget;
            const QString lbl = m_label;
            m_suppressReattach = true;
            m_widget = nullptr;
            if (w)
                w->setParent(nullptr);
            if (auto *c = takeCentralWidget())
                c->deleteLater();
            emit reattachRequested(w, lbl);
            close();
            return true;
        }
    }
    return QMainWindow::event(event);
}

bool DetachedTabWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tabBar && m_editor) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            if (m_widget) {
                QWidget *w = m_widget;
                const QString lbl = m_label;
                m_suppressReattach = true;
                m_widget = nullptr;
                if (w)
                    w->setParent(nullptr);
                if (auto *c = takeCentralWidget())
                    c->deleteLater();
                emit reattachRequested(w, lbl);
                close();
                return true;
            }
        }
        // Drag starts after leaving the float tab strip (same as main tabs)
        if (event->type() == QEvent::MouseButtonPress) {
            const auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton && m_tabBar->tabAt(me->pos()) >= 0) {
                m_dragArmed = true;
                m_pressGlobal = me->globalPosition().toPoint();
            }
        } else if (event->type() == QEvent::MouseMove && m_dragArmed
                   && !m_editor->isDockDragging()) {
            const auto *me = static_cast<QMouseEvent *>(event);
            if ((me->globalPosition().toPoint() - m_pressGlobal).manhattanLength() > 12) {
                m_dragArmed = false;
                m_editor->beginDockDragFromFloat(this);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragArmed = false;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void DetachedTabWindow::closeEvent(QCloseEvent *event)
{
    if (!m_suppressReattach && m_widget) {
        QWidget *w = m_widget;
        const QString lbl = m_label;
        m_widget = nullptr;
        if (w)
            w->setParent(nullptr);
        if (auto *c = takeCentralWidget())
            c->deleteLater();
        emit reattachRequested(w, lbl);
    } else if (centralWidget()) {
        if (auto *c = takeCentralWidget())
            c->deleteLater();
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
                const bool pinned = w && w->property("pinned").toBool();
                btn->setVisible(!pinned);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTabWidget *m_tabs;
};

// ============================================================
//  TabDockDragFilter — start dock drag from a main tab bar
// ============================================================

class TabDockDragFilter : public QObject
{
public:
    TabDockDragFilter(QTabWidget *tabs, SplitEditorArea *area)
        : QObject(tabs), m_tabs(tabs), m_area(area)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *bar = qobject_cast<QTabBar *>(watched);
        if (!bar || !m_area)
            return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::MouseButtonDblClick) {
            const auto *me = static_cast<QMouseEvent *>(event);
            const int idx = bar->tabAt(me->pos());
            if (idx >= 0) {
                m_area->detachTab(m_tabs, idx);
                return true;
            }
        }

        if (event->type() == QEvent::MouseButtonPress) {
            const auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_pressIndex = bar->tabAt(me->pos());
                m_pressGlobal = me->globalPosition().toPoint();
                m_armed = (m_pressIndex >= 0);
            }
        } else if (event->type() == QEvent::MouseMove && m_armed
                   && !m_area->isDockDragging()) {
            const auto *me = static_cast<QMouseEvent *>(event);
            const QPoint g = me->globalPosition().toPoint();
            QRect barRect = bar->rect();
            barRect.moveTopLeft(bar->mapToGlobal(QPoint(0, 0)));

            const int threshold = 24;
            const bool outside = !barRect.adjusted(-threshold, -threshold,
                                                   threshold, threshold)
                                      .contains(g);
            if (outside && m_pressIndex >= 0 && m_pressIndex < m_tabs->count()) {
                // Stop QTabBar internal move
                QMouseEvent releaseEvent(
                    QEvent::MouseButtonRelease, me->pos(),
                    me->globalPosition().toPoint(),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &releaseEvent);

                m_area->beginDockDragFromTab(m_tabs, m_pressIndex);
                m_armed = false;
                m_pressIndex = -1;
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_armed = false;
            m_pressIndex = -1;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTabWidget *m_tabs;
    SplitEditorArea *m_area;
    bool m_armed = false;
    int m_pressIndex = -1;
    QPoint m_pressGlobal;
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

    m_overlay = new DockDropOverlay(this);
    m_overlay->hide();
}

SplitEditorArea::~SplitEditorArea()
{
    cancelDockDrag();
}

QTabWidget *SplitEditorArea::createTabWidget()
{
    auto *tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);
    tabs->setAcceptDrops(false);

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
    installDragOutFilter(tabs);

    connect(tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        emit currentChanged(idx);
    });

    return tabs;
}

void SplitEditorArea::installDragOutFilter(QTabWidget *tabs)
{
    tabs->tabBar()->installEventFilter(new TabDockDragFilter(tabs, this));
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
    const bool pinned = w && w->property("pinned").toBool();
    btn->setVisible(!pinned);
}

QWidget *SplitEditorArea::currentWidget() const
{
    auto *tabs = activeTabWidget();
    return tabs ? tabs->currentWidget() : nullptr;
}

QTabWidget *SplitEditorArea::activeTabWidget() const
{
    for (auto *w : m_rootSplitter->findChildren<QTabWidget *>()) {
        if (w->hasFocus() || (w->tabBar() && w->tabBar()->hasFocus()))
            return w;
    }
    for (auto *tw : allTabWidgets()) {
        if (tw->count() > 0)
            return tw;
    }
    return m_firstTabs;
}

QList<QTabWidget *> SplitEditorArea::allTabWidgets() const
{
    return m_rootSplitter->findChildren<QTabWidget *>();
}

// ---- Dock drag session -------------------------------------------------

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
    setCursor(Qt::ClosedHandCursor);
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
    setCursor(Qt::ClosedHandCursor);
    updateDockDrag(QCursor::pos());
}

void SplitEditorArea::updateDockDrag(const QPoint &globalPos)
{
    if (!m_drag.active)
        return;

    auto *target = tabWidgetAtGlobal(globalPos);
    if (!target) {
        // Outside any pane — float preview (hide overlay or show none)
        hideOverlay();
        return;
    }

    const DockZone zone = hitTestZone(target, globalPos);
    showOverlay(target, zone);
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
    unsetCursor();
    hideOverlay();

    if (!widget)
        return;

    auto *target = tabWidgetAtGlobal(globalPos);
    DockZone zone = DockZone::Float;
    if (target)
        zone = hitTestZone(target, globalPos);

    // Dropped back onto own center → no-op
    if (!fromFloat && sourceTabs && target == sourceTabs
        && zone == DockZone::Center)
        return;

    // Still floating and released outside editor → keep float window
    if (fromFloat && (zone == DockZone::Float || !target))
        return;

    // Remove from source pane / float
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
    unsetCursor();
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
        return false; // don't eat — allow other UI
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

// DetachedTabWindow needs a way to suppress reattach — add public method
// (patched below via friendship workaround: we disconnect + steal parent)

QTabWidget *SplitEditorArea::tabWidgetAtGlobal(const QPoint &globalPos) const
{
    // Prefer deepest tab widget under cursor inside this editor
    for (auto *tw : allTabWidgets()) {
        if (!tw->isVisible())
            continue;
        const QRect r(tw->mapToGlobal(QPoint(0, 0)), tw->size());
        if (r.contains(globalPos))
            return tw;
    }
    // Fallback: if cursor is over the editor area but between handles
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

    const int mw = qMax(48, w / 4);
    const int mh = qMax(48, h / 4);

    const QRect center(mw, mh, w - 2 * mw, h - 2 * mh);
    if (center.contains(local))
        return DockZone::Center;

    const bool left = local.x() < mw;
    const bool right = local.x() >= w - mw;
    const bool top = local.y() < mh;
    const bool bottom = local.y() >= h - mh;

    // Prefer horizontal edges when in corners (industrial default)
    if (left && !right)
        return DockZone::Left;
    if (right)
        return DockZone::Right;
    if (top && !bottom)
        return DockZone::Top;
    if (bottom)
        return DockZone::Bottom;
    return DockZone::Center;
}

void SplitEditorArea::showOverlay(QTabWidget *target, DockZone zone)
{
    if (!m_overlay || !target)
        return;
    // Overlay is child of SplitEditorArea — map target geometry
    const QPoint topLeft = target->mapTo(this, QPoint(0, 0));
    m_overlay->setGeometry(QRect(topLeft, target->size()));
    m_overlay->setZone(zone);
    m_overlay->raise();
    m_overlay->show();
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

    QSplitter *split = parentSplitter(target);
    if (!split) {
        m_firstTabs->addTab(widget, label);
        newTabs->deleteLater();
        return;
    }

    if (split->orientation() == orient) {
        const int at = split->indexOf(target);
        split->insertWidget(before ? at : at + 1, newTabs);
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
                                                         const QPoint &globalPos)
{
    auto *win = new DetachedTabWindow(widget, label, this, nullptr);
    registerDetachedWindow(win);

    // Prefer the screen under the cursor (multi-monitor)
    if (QScreen *scr = QGuiApplication::screenAt(globalPos)) {
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
    closeSplit->setEnabled(tabs->count() > 0);

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
}

void SplitEditorArea::removeEmptySplits()
{
    // Drop empty tab panes (except the root first pane)
    for (auto *tw : allTabWidgets()) {
        if (tw == m_firstTabs)
            continue;
        if (tw->count() == 0) {
            tw->setParent(nullptr);
            tw->deleteLater();
        }
    }

    auto splitters = m_rootSplitter->findChildren<QSplitter *>();
    for (auto *split : splitters) {
        if (split == m_rootSplitter)
            continue;
        if (split->count() == 1) {
            QWidget *only = split->widget(0);
            if (QSplitter *parent = parentSplitter(split)) {
                const int idx = parent->indexOf(split);
                parent->replaceWidget(idx, only);
                split->deleteLater();
            }
        } else if (split->count() == 0) {
            split->deleteLater();
        }
    }

    // Ensure root still has at least one tab widget
    if (allTabWidgets().isEmpty()) {
        m_firstTabs = createTabWidget();
        m_rootSplitter->addWidget(m_firstTabs);
    } else if (!m_firstTabs || m_rootSplitter->indexOf(m_firstTabs) < 0) {
        m_firstTabs = allTabWidgets().first();
    }
}

QSplitter *SplitEditorArea::parentSplitter(QWidget *w) const
{
    auto *p = w->parentWidget();
    while (p) {
        if (auto *s = qobject_cast<QSplitter *>(p))
            return s;
        p = p->parentWidget();
    }
    return nullptr;
}

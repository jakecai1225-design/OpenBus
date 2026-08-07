#include "spliteditorarea.h"
#include <QMenu>
#include <QAction>
#include <QTabBar>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEvent>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QCursor>

// ============================================================
//  DetachedTabWindow — 分离标签页的独立窗口
// ============================================================

DetachedTabWindow::DetachedTabWindow(QWidget *widget, const QString &label, QWidget *parent)
    : QMainWindow(parent), m_widget(widget), m_label(label)
{
    setWindowTitle(label);
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(800, 600);

    if (widget) {
        // setCentralWidget 会自动 reparent，无需先 setParent(nullptr)
        setCentralWidget(widget);
        widget->setVisible(true);  // removeTab 后 widget 可能被隐藏
    }
}

void DetachedTabWindow::closeEvent(QCloseEvent *event)
{
    // 取出 widget，避免被删除
    if (m_widget) {
        takeCentralWidget();
        m_widget->setParent(nullptr);
        emit reattachRequested(m_widget, m_label);
    }
    event->accept();
}

// ============================================================
//  TabBarHoverFilter — 鼠标悬停显示关闭按钮
// ============================================================

class TabBarHoverFilter : public QObject
{
public:
    explicit TabBarHoverFilter(QTabWidget *tabs) : QObject(tabs), m_tabs(tabs) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *bar = qobject_cast<QTabBar *>(watched);
        if (!bar) return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::MouseMove) {
            const auto *me = static_cast<QMouseEvent *>(event);
            int idx = bar->tabAt(me->pos());
            updateButtons(idx);
        } else if (event->type() == QEvent::Leave) {
            updateButtons(-1);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void updateButtons(int visibleIdx)
    {
        for (int i = 0; i < m_tabs->tabBar()->count(); ++i) {
            auto *btn = m_tabs->tabBar()->tabButton(i, QTabBar::RightSide);
            if (btn) {
                // 固定标签页不显示关闭按钮
                QWidget *w = m_tabs->widget(i);
                bool pinned = w && w->property("pinned").toBool();
                btn->setVisible(i == visibleIdx && !pinned);
            }
        }
    }

    QTabWidget *m_tabs;
};

// ============================================================
//  TabDragOutFilter — 检测标签页拖出 tab bar 区域，触发分离
// ============================================================

class TabDragOutFilter : public QObject
{
public:
    explicit TabDragOutFilter(QTabWidget *tabs, SplitEditorArea *area)
        : QObject(tabs), m_tabs(tabs), m_area(area) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *bar = qobject_cast<QTabBar *>(watched);
        if (!bar) return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::MouseButtonPress) {
            const auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_pressIndex = bar->tabAt(me->pos());
                m_pressGlobalPos = me->globalPosition().toPoint();
                m_dragging = (m_pressIndex >= 0);
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            const auto *me = static_cast<QMouseEvent *>(event);
            // 检测鼠标是否拖出 tab bar 边界
            QPoint globalPos = me->globalPosition().toPoint();
            QRect barRect = bar->rect();
            // 转为全局坐标
            barRect.moveTopLeft(bar->mapToGlobal(QPoint(0, 0)));

            // 需要拖出一定距离才触发（避免误触）
            const int threshold = 30;
            bool outside = !barRect.contains(globalPos) &&
                           (globalPos.y() < barRect.top() - threshold ||
                            globalPos.y() > barRect.bottom() + threshold ||
                            globalPos.x() < barRect.left() - threshold ||
                            globalPos.x() > barRect.right() + threshold);

            if (outside && m_pressIndex >= 0 && m_pressIndex < m_tabs->count()) {
                // 发送假的 mouseRelease 给 tab bar，停止内部拖拽
                QMouseEvent releaseEvent(QEvent::MouseButtonRelease, me->pos(),
                                         me->globalPosition().toPoint(),
                                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &releaseEvent);

                // 触发分离
                m_area->detachTab(m_tabs, m_pressIndex);
                m_dragging = false;
                m_pressIndex = -1;
                return true; // 事件已处理
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            m_pressIndex = -1;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QTabWidget *m_tabs;
    SplitEditorArea *m_area;
    bool m_dragging = false;
    int m_pressIndex = -1;
    QPoint m_pressGlobalPos;
};

// ============================================================
//  SplitEditorArea 实现
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
}

QTabWidget *SplitEditorArea::createTabWidget()
{
    auto *tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setDocumentMode(true);

    // 右键标签栏 → 上下文菜单
    tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabs->tabBar(), &QWidget::customContextMenuRequested,
            this, [this, tabs](const QPoint &pos) {
        int idx = tabs->tabBar()->tabAt(pos);
        if (idx >= 0)
            onTabBarContextMenu(idx, pos);
    });

    // 关闭标签页
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this, tabs](int index) {
        closeTab(tabs, index);
    });

    // 鼠标悬停时显示关闭按钮，离开时隐藏
    tabs->tabBar()->setMouseTracking(true);
    tabs->tabBar()->installEventFilter(new TabBarHoverFilter(tabs));

    // 拖拽分离检测
    installDragOutFilter(tabs);

    // 当前页变化时转发信号
    connect(tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        emit currentChanged(idx);
    });

    return tabs;
}

void SplitEditorArea::installDragOutFilter(QTabWidget *tabs)
{
    tabs->tabBar()->installEventFilter(new TabDragOutFilter(tabs, this));
}

int SplitEditorArea::addTab(QWidget *widget, const QString &label)
{
    // 添加到当前活跃的 TabWidget，如果没有则添加到第一个
    QTabWidget *target = activeTabWidget();
    if (!target)
        target = m_firstTabs;
    int idx = target->addTab(widget, label);
    // 隐藏新标签页的关闭按钮（仅悬停时显示）
    auto *btn = target->tabBar()->tabButton(idx, QTabBar::RightSide);
    if (btn) btn->setVisible(false);
    emit tabListChanged();
    return idx;
}

QWidget *SplitEditorArea::currentWidget() const
{
    auto *tabs = activeTabWidget();
    return tabs ? tabs->currentWidget() : nullptr;
}

QTabWidget *SplitEditorArea::activeTabWidget() const
{
    // 找到有焦点的或最后激活的 QTabWidget
    for (auto *w : m_rootSplitter->findChildren<QTabWidget *>()) {
        if (w->hasFocus())
            return w;
    }
    // 默认返回第一个有标签页的
    auto all = allTabWidgets();
    for (auto *tw : all) {
        if (tw->count() > 0)
            return tw;
    }
    return m_firstTabs;
}

QList<QTabWidget *> SplitEditorArea::allTabWidgets() const
{
    return m_rootSplitter->findChildren<QTabWidget *>();
}

void SplitEditorArea::detachTab(QTabWidget *tabs, int index)
{
    if (!tabs || index < 0 || index >= tabs->count())
        return;

    QWidget *widget = tabs->widget(index);
    QString label = tabs->tabText(index);
    tabs->removeTab(index);

    auto *win = new DetachedTabWindow(widget, label, nullptr);
    m_detachedWindows.append(win);

    // 窗口关闭时重新放回标签页
    connect(win, &DetachedTabWindow::reattachRequested,
            this, [this](QWidget *w, const QString &lbl) {
        reattachTab(w, lbl);
    });
    // 窗口销毁时从列表移除
    connect(win, &QObject::destroyed, this, [this, win](QObject *) {
        m_detachedWindows.removeAll(win);
    });

    // 在鼠标当前位置附近显示窗口
    QPoint cursorPos = QCursor::pos();
    win->move(cursorPos - QPoint(win->width() / 2, 30));
    win->show();
    win->raise();
    win->activateWindow();

    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::reattachTab(QWidget *widget, const QString &label)
{
    if (!widget)
        return;

    // 从分离窗口列表中移除
    // (DetachedTabWindow 已设置 WA_DeleteOnClose，会自动删除)

    // 添加回活跃的 TabWidget
    int idx = addTab(widget, label);

    // 激活该标签页
    auto *tabs = activeTabWidget();
    if (tabs)
        tabs->setCurrentIndex(idx);

    emit tabListChanged();
}

void SplitEditorArea::onTabBarContextMenu(int index, const QPoint &pos)
{
    // 找到发出请求的 QTabWidget
    auto *bar = qobject_cast<QTabBar *>(sender());
    if (!bar)
        return;
    auto *tabs = qobject_cast<QTabWidget *>(bar->parentWidget());
    if (!tabs)
        return;

    QWidget *widget = tabs->widget(index);
    bool pinned = widget && widget->property("pinned").toBool();

    auto *menu = new QMenu(this);

    // ---- Pin / Unpin ----
    auto *pinAct = menu->addAction(pinned ? QStringLiteral("取消固定")
                                          : QStringLiteral("固定标签页"));
    menu->addSeparator();

    // ---- 关闭操作 ----
    auto *closeAct = menu->addAction(QStringLiteral("关闭"));
    closeAct->setEnabled(!pinned);
    auto *closeOthersAct = menu->addAction(QStringLiteral("关闭其他"));
    closeOthersAct->setEnabled(tabs->count() > 1);
    auto *closeRightAct = menu->addAction(QStringLiteral("关闭右侧标签页"));
    closeRightAct->setEnabled(index < tabs->count() - 1);
    auto *closeAllAct = menu->addAction(QStringLiteral("关闭所有"));
    closeAllAct->setEnabled(tabs->count() > 0);
    menu->addSeparator();

    // ---- 拆分 / 分离 ----
    auto *splitRight = menu->addAction(QStringLiteral("向右拆分"));
    auto *splitDown = menu->addAction(QStringLiteral("向下拆分"));
    auto *detach = menu->addAction(QStringLiteral("分离到新窗口"));
    menu->addSeparator();
    auto *closeSplit = menu->addAction(QStringLiteral("关闭此拆分组"));
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
        // 移走所有标签页到第一个 TabWidget
        while (tabs->count() > 0) {
            QWidget *w = tabs->widget(0);
            QString text = tabs->tabText(0);
            tabs->removeTab(0);
            if (tabs != m_firstTabs)
                m_firstTabs->addTab(w, text);
            else
                delete w; // 不能删除唯一的 TabWidget 里的内容
        }
        if (tabs != m_firstTabs) {
            tabs->deleteLater();
            removeEmptySplits();
        }
        emit tabListChanged();
    }
    menu->deleteLater();
}

// ============================================================
//  Pin / 关闭操作实现
// ============================================================

bool SplitEditorArea::isPinned(QWidget *w) const
{
    return w && w->property("pinned").toBool();
}

void SplitEditorArea::togglePin(QTabWidget *tabs, int index)
{
    QWidget *w = tabs->widget(index);
    if (!w) return;
    bool newPinned = !w->property("pinned").toBool();
    w->setProperty("pinned", newPinned);

    // 更新标签页文本（添加/移除 [固定] 前缀）
    QString text = tabs->tabText(index);
    static const QString pinPrefix = QStringLiteral("[固定] ");
    if (newPinned) {
        if (!text.startsWith(pinPrefix))
            tabs->setTabText(index, pinPrefix + text);
    } else {
        if (text.startsWith(pinPrefix))
            tabs->setTabText(index, text.mid(pinPrefix.length()));
    }

    // 固定标签页隐藏关闭按钮
    auto *btn = tabs->tabBar()->tabButton(index, QTabBar::RightSide);
    if (btn) btn->setVisible(!newPinned);

    emit tabListChanged();
}

void SplitEditorArea::closeTab(QTabWidget *tabs, int index)
{
    if (!tabs || index < 0 || index >= tabs->count()) return;
    QWidget *w = tabs->widget(index);
    if (w && w->property("pinned").toBool()) return;  // 固定标签页不可关闭
    tabs->removeTab(index);
    if (w) w->deleteLater();
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::closeOthers(QTabWidget *tabs, int keepIndex)
{
    if (!tabs) return;
    for (int i = tabs->count() - 1; i >= 0; --i) {
        if (i == keepIndex) continue;
        QWidget *w = tabs->widget(i);
        if (w && w->property("pinned").toBool()) continue;
        tabs->removeTab(i);
        if (w) w->deleteLater();
    }
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::closeRight(QTabWidget *tabs, int startIndex)
{
    if (!tabs) return;
    for (int i = tabs->count() - 1; i > startIndex; --i) {
        QWidget *w = tabs->widget(i);
        if (w && w->property("pinned").toBool()) continue;
        tabs->removeTab(i);
        if (w) w->deleteLater();
    }
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::closeAll(QTabWidget *tabs)
{
    if (!tabs) return;
    for (int i = tabs->count() - 1; i >= 0; --i) {
        QWidget *w = tabs->widget(i);
        if (w && w->property("pinned").toBool()) continue;
        tabs->removeTab(i);
        if (w) w->deleteLater();
    }
    removeEmptySplits();
    emit tabListChanged();
}

void SplitEditorArea::splitTab(QTabWidget *source, int index, Qt::Orientation orient)
{
    // 取出当前标签页的 widget
    QWidget *widget = source->widget(index);
    QString label = source->tabText(index);
    source->removeTab(index);

    // 创建新的 TabWidget 并把 widget 放进去
    auto *newTabs = createTabWidget();
    newTabs->addTab(widget, label);

    // 找到 source 所在的 QSplitter
    QSplitter *split = parentSplitter(source);
    if (!split) {
        // source 不在任何 splitter 里（不应该发生）
        source->addTab(widget, label);
        return;
    }

    if (split->orientation() == orient) {
        // 方向一致，直接追加
        int idx = split->indexOf(source);
        split->insertWidget(idx + 1, newTabs);
    } else {
        // 方向不一致，需要创建新的嵌套 splitter
        int idx = split->indexOf(source);
        auto *newSplit = new QSplitter(orient, this);
        split->replaceWidget(idx, newSplit);
        newSplit->addWidget(source);
        newSplit->addWidget(newTabs);
        newSplit->setSizes({500, 500});
    }
}

void SplitEditorArea::removeEmptySplits()
{
    // 递归清理：如果 QSplitter 只有一个子项且不是 QTabWidget，
    // 则用该子项替换 QSplitter 本身
    auto splitters = m_rootSplitter->findChildren<QSplitter *>();
    for (auto *split : splitters) {
        if (split == m_rootSplitter)
            continue;
        if (split->count() == 1) {
            QWidget *only = split->widget(0);
            QSplitter *parent = parentSplitter(split);
            if (parent) {
                int idx = parent->indexOf(split);
                parent->replaceWidget(idx, only);
                split->deleteLater();
            }
        }
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

#ifndef SPLITEDITORAREA_H
#define SPLITEDITORAREA_H

#include <QWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QMainWindow>
#include <QPointer>
#include <QVariantMap>

class DockDropOverlay;
class SplitEditorArea;

/**
 * @brief Floating window for a detached editor tab (multi-monitor friendly).
 *
 * No in-window tab strip — the OS title bar shows the name and content is full-bleed.
 * Double-click the title bar (or close the window) to merge back into the main editor.
 */
class DetachedTabWindow : public QMainWindow
{
    Q_OBJECT
public:
    DetachedTabWindow(QWidget *widget, const QString &label,
                      SplitEditorArea *editor, QWidget *parent = nullptr);

    QWidget *containedWidget() const { return m_widget; }
    QString label() const { return m_label; }

    /// Steal content and close without emitting reattachRequested.
    QWidget *takePage();
    /// Close and delete content (project switch) — no reattach.
    void destroyContent();

signals:
    void reattachRequested(QWidget *widget, const QString &label);

protected:
    bool event(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void mergeBack();

    QWidget *m_widget = nullptr;
    QString m_label;
    QPointer<SplitEditorArea> m_editor;
    bool m_suppressReattach = false;
};

/**
 * @brief VS Code-style splittable editor: drag-split, float, layout persistence.
 */
class SplitEditorArea : public QWidget
{
    Q_OBJECT

public:
    enum class DockZone {
        None = 0,
        Center,
        Left,
        Right,
        Top,
        Bottom,
        Float
    };
    Q_ENUM(DockZone)

    explicit SplitEditorArea(QWidget *parent = nullptr);
    ~SplitEditorArea() override;

    int addTab(QWidget *widget, const QString &label);
    QWidget *currentWidget() const;
    QTabWidget *activeTabWidget() const;
    QList<QTabWidget *> allTabWidgets() const;

    bool isPinned(QWidget *w) const;

    void detachTab(QTabWidget *tabs, int index);
    void reattachTab(QWidget *widget, const QString &label);
    void closeTab(QTabWidget *tabs, int index);

    void beginDockDragFromTab(QTabWidget *tabs, int index);
    void beginDockDragFromFloat(DetachedTabWindow *win);
    void updateDockDrag(const QPoint &globalPos);
    void finishDockDrag(const QPoint &globalPos);
    void cancelDockDrag();
    bool isDockDragging() const { return m_drag.active; }

    /// Persist / restore splitter tree + floating windows (titles identify pages).
    QVariantMap saveLayout() const;
    void restoreLayout(const QVariantMap &layout);
    /// Destroy floating windows without merging (project switch).
    void discardDetachedWindows();
    /// Flat tab titles in DFS order (compat with openTabs).
    QStringList collectTabTitles() const;

signals:
    void currentChanged(int index);
    void tabCloseRequested(int index);
    void tabContextMenuRequested(int index, const QPoint &pos);
    void tabListChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onTabBarContextMenu(int index, const QPoint &pos);

private:
    struct DockDragState {
        bool active = false;
        QPointer<QTabWidget> sourceTabs;
        int sourceIndex = -1;
        QPointer<QWidget> widget;
        QString label;
        QPointer<DetachedTabWindow> sourceFloat;
        bool fromFloat = false;
    };

    QSplitter *m_rootSplitter = nullptr;
    QTabWidget *m_firstTabs = nullptr;
    QList<DetachedTabWindow *> m_detachedWindows;
    DockDropOverlay *m_overlay = nullptr;
    DockDragState m_drag;

    QTabWidget *createTabWidget();
    void installDragOutFilter(QTabWidget *tabs);
    void setupCloseButton(QTabWidget *tabs, int index);

    void splitTab(QTabWidget *source, int index, Qt::Orientation orient);
    void insertBeside(QTabWidget *target, QWidget *widget, const QString &label,
                      Qt::Orientation orient, bool before);
    void dropWidget(QTabWidget *target, DockZone zone,
                    QWidget *widget, const QString &label);
    void removeEmptySplits();
    QSplitter *parentSplitter(QWidget *w) const;

    QTabWidget *tabWidgetAtGlobal(const QPoint &globalPos) const;
    DockZone hitTestZone(QTabWidget *target, const QPoint &globalPos) const;
    void showOverlay(QTabWidget *target, DockZone zone);
    void hideOverlay();

    void togglePin(QTabWidget *tabs, int index);
    void closeOthers(QTabWidget *tabs, int keepIndex);
    void closeRight(QTabWidget *tabs, int startIndex);
    void closeAll(QTabWidget *tabs);

    void registerDetachedWindow(DetachedTabWindow *win);
    DetachedTabWindow *createDetachedWindow(QWidget *widget, const QString &label,
                                            const QPoint &globalPos,
                                            const QRect &geometry = QRect());

    QVariantMap serializeWidget(QWidget *w) const;
    QWidget *buildFromLayout(const QVariantMap &node,
                             QHash<QString, QWidget *> &pages);
    void collectTitlesFromNode(const QVariantMap &node, QStringList &out) const;

    /// True for SplitEditorArea panes only — never Trace Detail/Signals etc.
    static bool isEditorTabPane(const QTabWidget *tw);
    bool isEditorChromeSplitter(const QSplitter *sp) const;
};

#endif // SPLITEDITORAREA_H

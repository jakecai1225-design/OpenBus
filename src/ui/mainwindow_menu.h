// ============================================================================
// mainwindow_menu.h - QML MenuBar 集成 (仅用于声明)
// ============================================================================

#ifndef MAINWINDOW_MENU_H
#define MAINWINDOW_MENU_H

#include <QWidget>
#include <QQuickWidget>

class MenuController;
class MainWindow;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // QML MenuBar 事件处理槽
    void handleMenuBarAction(const QString &actionName);
    
    // 文件操作 (保留原 Widgets 实现)
    void openFile();
    void openProject();
    void saveProject();
    void importLogFile();
    
    // 视图操作
    void toggleLeftDock(bool checked);
    void toggleBottomDock(bool checked);
    void toggleRightDock(bool checked);
    void resetLayout();
    
    // 工具操作
    void dataWindow();
    void ioGraph();
    void watcher();
    
    // 帮助操作
    void about();
    void docs();
    void shortcuts();
    void license();

private:
    // QML MenuBar 集成
    void createQmlMenuBar();
    void registerQmlTypes();
    
    // QML 菜单栏组件
    QQuickWidget *m_qmlMenuBar = nullptr;
    MenuController *m_menuController = nullptr;
    
    // 原有 Widgets 菜单栏 (可以注释掉)
    // void createWidgetsMenuBar();
};

#endif // MAINWINDOW_MENU_H

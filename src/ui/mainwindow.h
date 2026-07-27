#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/**
 * @brief 主窗口类
 *
 * 工业级桌面应用的主窗口，后续可扩展为多文档界面(MDI)、
 * 停靠窗口、菜单栏、工具栏等。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onPushButtonClicked();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H

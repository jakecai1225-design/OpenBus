#include <QApplication>
#include <QFile>

#include "ui/mainwindow.h"
#include "core/canframe.h"

int main(int argc, char *argv[])
{
    // 注册元类型，支持信号/槽传递 CanFrame
    qRegisterMetaType<CanFrame>("CanFrame");

    QApplication app(argc, argv);
    app.setApplicationName("sin");
    app.setOrganizationName("sin");
    app.setApplicationVersion("0.1.0");

    // 加载样式表
    QFile qss(":/styles/default.qss");
    if (qss.open(QFile::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    MainWindow window;
    window.show();

    return app.exec();
}

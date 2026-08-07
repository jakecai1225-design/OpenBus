#include <QApplication>

#include "ui/mainwindow.h"
#include "ui/thememanager.h"
#include "core/canframe.h"
#include "core/logging.h"
#include "core/appconfig.h"
#include "core/sessionmanager.h"

int main(int argc, char *argv[])
{
    // 注册元类型，支持信号/槽传递 CanFrame
    qRegisterMetaType<CanFrame>("CanFrame");

    QApplication app(argc, argv);
    app.setApplicationName("sin");
    app.setOrganizationName("sin");
    app.setApplicationVersion("0.1.0");

    // 初始化日志系统（需在 QApplication 设置名称之后）
    logging::init();

    // 加载应用配置
    AppConfig::instance()->load();

    // 加载会话状态（含从 AppConfig 迁移 project.recent）
    SessionManager::instance()->load();

    // 应用主题
    ThemeManager::instance()->applyTheme("Light");

    MainWindow window;
    window.show();

    int ret = app.exec();
    logging::shutdown();
    return ret;
}

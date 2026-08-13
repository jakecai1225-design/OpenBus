#include <QApplication>
#include <QMessageBox>

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
    app.setApplicationName("openbus");
    app.setOrganizationName("openbus");
    app.setApplicationVersion("0.1.0");

    // 初始化日志系统（需在 QApplication 设置名称之后）
    logging::init();

    try {
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
    } catch (const std::exception &e) {
        QString msg = QStringLiteral("致命错误: %1").arg(e.what());
        SIN_LOG_ERROR("main", "uncaught exception: {}", e.what());
        logging::shutdown();
        QMessageBox::critical(nullptr, QStringLiteral("致命错误"), msg);
        return 1;
    } catch (...) {
        SIN_LOG_ERROR("main", "unknown uncaught exception");
        logging::shutdown();
        QMessageBox::critical(nullptr, QStringLiteral("致命错误"),
                             QStringLiteral("程序发生未知异常"));
        return 1;
    }
}

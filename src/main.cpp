#include <QApplication>

#include "ui/mainwindow.h"
#include "ui/thememanager.h"
#include "core/canframe.h"
#include "core/logging.h"
#include "core/appconfig.h"

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

    // 应用主题
    ThemeManager::instance()->applyTheme("Light");

    spdlog::info("[Startup] 开始创建 MainWindow...");
    MainWindow window;
    spdlog::info("[Startup] MainWindow 构造完成, winId={}", (void*)window.winId());
    window.show();
    spdlog::info("[Startup] window.show() 已调用");

    int ret = app.exec();
    spdlog::info("[Startup] app.exec() 返回 {}", ret);
    logging::shutdown();
    return ret;
}

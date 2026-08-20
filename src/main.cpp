#include <QApplication>
#include <QMessageBox>

#include "ui/mainwindow.h"
#include "ui/thememanager.h"
#include "core/canframe.h"
#include "core/logging.h"
#include "core/appconfig.h"
#include "core/sessionmanager.h"
#include "core/module/moduleregistry.h"

// 各业务 DLL 唯一导出的 C 工厂（拆分方案 §4.2：壳不 include 模块头，
// 仅链接导入库；工厂按模块唯一命名 openbus_create<Xxx>Module）
extern "C" IBusinessModule *openbus_createMarketModule();       // openbus_market.dll（B1）
extern "C" IBusinessModule *openbus_createTransceiveModule();   // openbus_transceive.dll（B2）
extern "C" IBusinessModule *openbus_createDbcModule();          // openbus_dbc.dll（B3）
extern "C" IBusinessModule *openbus_createFlowModule();         // openbus_flow.dll（B4）

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

    // 注册业务模块（拆分方案 B1/B2/B3：market、transceive、dbc 迁入各自 DLL，
    // 经其 C 工厂注册；后续模块 DLL 逐个在此追加）
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("market"), &openbus_createMarketModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("transceive"), &openbus_createTransceiveModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("dbc"), &openbus_createDbcModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("flow"), &openbus_createFlowModule);

    // 应用主题
    ThemeManager::instance()->applyTheme("Light");

    MainWindow window;
    window.show();

    int ret = app.exec();
    logging::shutdown();
    return ret;
    } catch (const std::exception &e) {
        QString msg = QStringLiteral("致命错误: %1").arg(e.what());
        OPENBUS_LOG_ERROR("main", "uncaught exception: {}", e.what());
        logging::shutdown();
        QMessageBox::critical(nullptr, QStringLiteral("致命错误"), msg);
        return 1;
    } catch (...) {
        OPENBUS_LOG_ERROR("main", "unknown uncaught exception");
        logging::shutdown();
        QMessageBox::critical(nullptr, QStringLiteral("致命错误"),
                             QStringLiteral("程序发生未知异常"));
        return 1;
    }
}

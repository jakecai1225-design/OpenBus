#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>

#include "ui/mainwindow.h"
#include "ui/thememanager.h"
#include "core/canframe.h"
#include "core/logging.h"
#include "core/appconfig.h"
#include "core/insights.h"
#include "core/translationmanager.h"
#include "core/sessionmanager.h"
#include "core/module/moduleregistry.h"

// Business DLL C factories (shell links import libs only; see split plan §4.2)
extern "C" IBusinessModule *openbus_createMarketModule();       // openbus_market.dll (B1)
extern "C" IBusinessModule *openbus_createTransceiveModule();   // openbus_transceive.dll (B2)
extern "C" IBusinessModule *openbus_createDbcModule();          // openbus_dbc.dll (B3)
extern "C" IBusinessModule *openbus_createFlowModule();         // openbus_flow.dll (B4)
extern "C" IBusinessModule *openbus_createTraceModule();        // openbus_trace.dll (B5)
extern "C" IBusinessModule *openbus_createGraphicModule();      // openbus_graphic.dll (B5)

int main(int argc, char *argv[])
{
    // Must run before QGuiApplication. On this host (RDP / odd screen geometry),
    // Qt6's default High-DPI path abort-fails in Qt6Core (STATUS_FAIL_FAST_EXCEPTION
    // / RaiseFailFastException) when showing FramelessWindowHint + showMaximized.
    // Disabling High-DPI scaling avoids the abort; Round policy alone is not enough.
    if (qEnvironmentVariableIsEmpty("QT_ENABLE_HIGHDPI_SCALING"))
        qputenv("QT_ENABLE_HIGHDPI_SCALING", QByteArrayLiteral("0"));
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::Round);

    // Register meta-types for CanFrame signal/slot delivery
    qRegisterMetaType<CanFrame>("CanFrame");
    qRegisterMetaType<QVector<CanFrame>>("QVector<CanFrame>");

    QApplication app(argc, argv);
    app.setApplicationName("openbus");
    app.setOrganizationName("openbus");
    app.setApplicationVersion("1.10.4");

    // Init logging after QApplication name/org are set
    logging::init();

    try {
    // Load application config
    AppConfig::instance()->load();

    // Install UI translators before widgets are created
    TranslationManager::instance()->setLanguage(
        TranslationManager::resolveStartupLanguage());

    // Load session state (includes migration of project.recent from AppConfig)
    SessionManager::instance()->load();

    // 注册业务模块（拆分方案 B1/B2/B3/B4/B5：market、transceive、dbc、flow、
    // trace、graphic 迁入各自 DLL，经其 C 工厂注册）
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("market"), &openbus_createMarketModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("transceive"), &openbus_createTransceiveModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("dbc"), &openbus_createDbcModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("flow"), &openbus_createFlowModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("trace"), &openbus_createTraceModule);
    ModuleRegistry::instance()->registerModule(
        QStringLiteral("graphic"), &openbus_createGraphicModule);

    // Light-only workbench (legacy "Dark Modern" configs are ignored)
    ThemeManager::instance()->applyTheme(QStringLiteral("Light"));
    if (AppConfig::instance()->getString(QStringLiteral("theme")) != QStringLiteral("Light"))
        AppConfig::instance()->set(QStringLiteral("theme"), QStringLiteral("Light"));

    MainWindow window;
    window.showMaximized();
    Insights::instance()->startSession();

    int ret = app.exec();
    Insights::instance()->endSession();
    logging::shutdown();
    return ret;
    } catch (const std::exception &e) {
        QString msg = QStringLiteral("Fatal error: %1").arg(e.what());
        OPENBUS_LOG_ERROR("main", "uncaught exception: {}", e.what());
        logging::shutdown();
        QMessageBox::critical(nullptr, QStringLiteral("Fatal error"), msg);
        return 1;
    } catch (...) {
        OPENBUS_LOG_ERROR("main", "unknown uncaught exception");
        logging::shutdown();
        QMessageBox::critical(nullptr, QStringLiteral("Fatal error"),
                             QStringLiteral("An unknown exception occurred"));
        return 1;
    }
}

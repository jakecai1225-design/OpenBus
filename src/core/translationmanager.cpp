#include "translationmanager.h"
#include "appconfig.h"
#include "logging.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

const QStringList kLanguages = {
    QStringLiteral("en"),
    QStringLiteral("zh_CN"),
    QStringLiteral("zh_TW"),
    QStringLiteral("es"),
    QStringLiteral("fr"),
    QStringLiteral("de"),
    QStringLiteral("ja"),
    QStringLiteral("pt_BR"),
    QStringLiteral("ru"),
    QStringLiteral("ko"),
};

QString nativeNameFor(const QString &code)
{
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    if (code == QLatin1String("zh_CN"))
        return QString::fromUtf8(u8"\u7b80\u4f53\u4e2d\u6587");
    if (code == QLatin1String("zh_TW"))
        return QString::fromUtf8(u8"\u7e41\u9ad4\u4e2d\u6587");
    if (code == QLatin1String("es"))
        return QStringLiteral("Espa\u00f1ol");
    if (code == QLatin1String("fr"))
        return QStringLiteral("Fran\u00e7ais");
    if (code == QLatin1String("de"))
        return QStringLiteral("Deutsch");
    if (code == QLatin1String("ja"))
        return QString::fromUtf8(u8"\u65e5\u672c\u8a9e");
    if (code == QLatin1String("pt_BR"))
        return QStringLiteral("Portugu\u00eas (Brasil)");
    if (code == QLatin1String("ru"))
        return QString::fromUtf8(u8"\u0420\u0443\u0441\u0441\u043a\u0438\u0439");
    if (code == QLatin1String("ko"))
        return QString::fromUtf8(u8"\ud55c\uad6d\uc5b4");
    return code;
}

} // namespace

TranslationManager *TranslationManager::instance()
{
    static TranslationManager *inst = nullptr;
    if (!inst)
        inst = new TranslationManager(qApp);
    return inst;
}

TranslationManager::TranslationManager(QObject *parent)
    : QObject(parent)
    , m_language(QStringLiteral("en"))
{
}

QStringList TranslationManager::availableLanguages() const
{
    return kLanguages;
}

QList<TranslationManager::LanguageInfo> TranslationManager::availableLanguageInfos() const
{
    QList<LanguageInfo> out;
    out.reserve(kLanguages.size());
    for (const QString &code : kLanguages)
        out.append({code, nativeNameFor(code)});
    return out;
}

bool TranslationManager::isSupported(const QString &locale)
{
    return kLanguages.contains(locale);
}

QString TranslationManager::normalizeLocale(const QString &locale)
{
    QString s = locale.trimmed();
    if (s.isEmpty())
        return QStringLiteral("en");
    s.replace(QLatin1Char('-'), QLatin1Char('_'));
    if (isSupported(s))
        return s;
    if (s.startsWith(QLatin1String("zh"))) {
        if (s.contains(QLatin1String("TW"), Qt::CaseInsensitive)
            || s.contains(QLatin1String("HK"), Qt::CaseInsensitive)
            || s.contains(QLatin1String("Hant"), Qt::CaseInsensitive))
            return QStringLiteral("zh_TW");
        return QStringLiteral("zh_CN");
    }
    if (s.startsWith(QLatin1String("pt")))
        return QStringLiteral("pt_BR");
    const QString langOnly = s.section(QLatin1Char('_'), 0, 0);
    if (isSupported(langOnly))
        return langOnly;
    return QStringLiteral("en");
}

QString TranslationManager::resolveStartupLanguage()
{
    const QString configured = AppConfig::instance()->getString(
        QStringLiteral("ui.language"), QString());
    if (!configured.isEmpty())
        return normalizeLocale(configured);

    return normalizeLocale(QLocale::system().name());
}

void TranslationManager::removeTranslators()
{
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        QCoreApplication::removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = nullptr;
    }
}

bool TranslationManager::loadTranslators(const QString &locale)
{
    removeTranslators();

    if (locale == QLatin1String("en"))
        return true;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString qmName = QStringLiteral("openbus_%1.qm").arg(locale);
    const QStringList searchDirs = {
        appDir + QStringLiteral("/translations"),
        appDir,
        QDir(appDir).absoluteFilePath(QStringLiteral("../translations")),
    };

    bool loaded = false;
    for (const QString &dir : searchDirs) {
        const QString path = QDir(dir).filePath(qmName);
        if (!QFileInfo::exists(path))
            continue;
        auto *tr = new QTranslator(this);
        if (tr->load(path)) {
            QCoreApplication::installTranslator(tr);
            m_appTranslator = tr;
            loaded = true;
            spdlog::info("TranslationManager: loaded {}", path.toStdString());
            break;
        }
        delete tr;
    }
    if (!loaded)
        spdlog::warn("TranslationManager: missing or failed {}", qmName.toStdString());

    const QString qtName = QStringLiteral("qtbase_%1.qm").arg(locale);
    // Also try qt_*.qm shipped beside the exe (windeployqt)
    const QStringList qtNames = {
        QStringLiteral("qtbase_%1.qm").arg(locale),
        QStringLiteral("qt_%1.qm").arg(locale),
    };
    const QStringList qtDirs = {
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
        appDir + QStringLiteral("/translations"),
    };
    for (const QString &name : qtNames) {
        bool ok = false;
        for (const QString &dir : qtDirs) {
            const QString path = QDir(dir).filePath(name);
            if (!QFileInfo::exists(path))
                continue;
            auto *tr = new QTranslator(this);
            if (tr->load(path)) {
                QCoreApplication::installTranslator(tr);
                m_qtTranslator = tr;
                ok = true;
                break;
            }
            delete tr;
        }
        if (ok)
            break;
        Q_UNUSED(qtName);
    }

    return loaded || locale == QLatin1String("en");
}

bool TranslationManager::setLanguage(const QString &locale)
{
    const QString normalized = normalizeLocale(locale);

    loadTranslators(normalized);
    m_language = normalized;

    AppConfig::instance()->set(QStringLiteral("ui.language"), normalized);
    AppConfig::instance()->save();

    // installTranslator already broadcasts LanguageChange; also notify shell listeners.
    emit languageChanged(normalized);
    return true;
}

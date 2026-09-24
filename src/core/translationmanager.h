#ifndef TRANSLATIONMANAGER_H
#define TRANSLATIONMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class QTranslator;

/**
 * @brief Live UI language switch (Qt .qm + notify plugins).
 *
 * Loads translations/openbus_<locale>.qm next to the executable.
 * English (en) is the source language — translators are removed.
 * Emits languageChanged so the shell can retranslate open UI without restart.
 */
class TranslationManager : public QObject
{
    Q_OBJECT

public:
    struct LanguageInfo {
        QString code;        ///< e.g. zh_CN
        QString nativeName;  ///< e.g. Simplified Chinese native label
    };

    static TranslationManager *instance();

    /// Codes: en, zh_CN, zh_TW, es, fr, de, ja, pt_BR, ru, ko
    QStringList availableLanguages() const;
    QList<LanguageInfo> availableLanguageInfos() const;

    QString language() const { return m_language; }

    /// Install translators, persist ui.language, emit languageChanged.
    bool setLanguage(const QString &locale);

    /// Resolve default: AppConfig ui.language, else system if supported, else en.
    static QString resolveStartupLanguage();

signals:
    void languageChanged(const QString &locale);

private:
    explicit TranslationManager(QObject *parent = nullptr);

    bool loadTranslators(const QString &locale);
    void removeTranslators();
    static bool isSupported(const QString &locale);
    static QString normalizeLocale(const QString &locale);

    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;
    QString m_language;
};

#endif // TRANSLATIONMANAGER_H

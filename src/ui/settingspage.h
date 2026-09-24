#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QWidget>
#include <QJsonObject>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QStackedWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class QComboBox;

/**
 * @brief VS Code-style settings page (tab, not modal dialog).
 *
 * Left category tree + right settings list, top search bar.
 * Supports direct JSON editing. Language changes apply immediately.
 */
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

    /// Jump to a category (e.g. "General"); empty → All Settings
    void setCategory(const QString &category);

    void retranslateUi();

private slots:
    void onSearchChanged(const QString &text);
    void onCategorySelected(QTreeWidgetItem *item);
    void onJsonEdited();
    void onSave();
    void onReset();
    void onLanguageComboChanged(int index);

private:
    struct SettingMeta {
        QString key;
        QString label;
        QString category;
        QString type;     // "string", "int", "bool", "double", "combo", "language"
        QString desc;
        QStringList comboChoices;
    };

    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_jsonBtn = nullptr;
    QTreeWidget *m_categoryTree = nullptr;
    QStackedWidget *m_rightStack = nullptr;
    QPlainTextEdit *m_jsonEdit = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QComboBox *m_languageCombo = nullptr;

    QWidget *m_settingsListPage = nullptr;
    QTreeWidget *m_settingsTree = nullptr;
    QWidget *m_jsonPage = nullptr;

    QList<SettingMeta> m_metas;
    bool m_updatingLanguageCombo = false;

    void setupUi();
    void setupMetas();
    void populateCategoryTree();
    void populateSettingsTree(const QString &categoryFilter, const QString &textFilter);
    void switchToJsonPage();
    void switchToSettingsPage();
    void fillLanguageCombo();
};

#endif // SETTINGSPAGE_H

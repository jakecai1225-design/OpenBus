#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QJsonObject>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QStackedWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;

/**
 * @brief VS Code 风格设置对话框
 *
 * 左侧分类树 + 右侧设置项列表，顶部搜索栏。
 * 支持直接编辑 JSON（类似 VS Code 的 settings.json 编辑器）。
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onSearchChanged(const QString &text);
    void onCategorySelected(QTreeWidgetItem *item);
    void onJsonEdited();
    void onSave();
    void onReset();

private:
    // 设置项元数据
    struct SettingMeta {
        QString key;
        QString label;
        QString category;
        QString type;     // "string", "int", "bool", "double", "combo"
        QString desc;
        QStringList comboChoices;
    };

    QLineEdit *m_searchEdit = nullptr;
    QTreeWidget *m_categoryTree = nullptr;
    QStackedWidget *m_rightStack = nullptr;
    QPlainTextEdit *m_jsonEdit = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
    QLabel *m_statusLabel = nullptr;

    // 右侧 "设置列表" 页
    QWidget *m_settingsListPage = nullptr;
    QTreeWidget *m_settingsTree = nullptr;

    // 右侧 "JSON 编辑" 页
    QWidget *m_jsonPage = nullptr;

    QList<SettingMeta> m_metas;

    void setupUi();
    void setupMetas();
    void populateCategoryTree();
    void populateSettingsTree(const QString &categoryFilter, const QString &textFilter);
    void switchToJsonPage();
    void switchToSettingsPage();
};

#endif // SETTINGSDIALOG_H

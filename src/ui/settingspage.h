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

/**
 * @brief VS Code 风格设置页（标签页形态）
 *
 * 左侧分类树 + 右侧设置项列表，顶部搜索栏。
 * 支持直接编辑 JSON（类似 VS Code 的 settings.json 编辑器）。
 * 原为模态 SettingsDialog（弹窗）；侧栏设置面板条目改为统一以
 * 标签页打开后重构为 QWidget 页面，编辑区即所见即所得。
 */
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

    /// 定位到指定分类（如"通用"/"Trace"；空或未匹配则回到"全部设置"）
    void setCategory(const QString &category);

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

#endif // SETTINGSPAGE_H

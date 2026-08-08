#ifndef FILTERBAR_H
#define FILTERBAR_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QToolButton;
class QLabel;
class QMenu;
class FilterPresetManager;

/**
 * @brief Wireshark 风格显示过滤栏
 *
 * 左侧为覆盖模式切换，右侧为过滤输入框。
 * 开始/停止由 flow 标签页全局控制，本组件不再包含独立按钮。
 * 右侧有预设过滤下拉按钮（对标 CANoe Filter Presets）。
 */
class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(QWidget *parent = nullptr);

    QString filterText() const;
    bool filterActive() const;

    void setOverwriteMode(bool enabled);

    /// 设置过滤预设管理器
    void setPresetManager(FilterPresetManager *mgr);

signals:
    void filterApplied(const QString &filter);
    void filterCleared();
    void overwriteModeToggled(bool enabled);

private slots:
    void onApply();
    void onClear();
    void showHelp();
    void onTextChanged();
    void onPresetMenu();
    void onSaveAsPreset();

private:
    QLineEdit *m_edit;
    QPushButton *m_applyBtn;
    QPushButton *m_clearBtn;
    QToolButton *m_helpBtn;
    QToolButton *m_presetBtn;
    QLabel *m_statusIcon;
    QPushButton *m_overwriteBtn;
    FilterPresetManager *m_presetMgr = nullptr;

    void refreshPresets();
};

#endif // FILTERBAR_H

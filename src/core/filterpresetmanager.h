#ifndef FILTERPRESETMANAGER_H
#define FILTERPRESETMANAGER_H

#include <QObject>
#include <QVector>
#include <QString>

/**
 * @brief 过滤预设管理器（对标 CANoe Filter Presets）
 *
 * 管理命名过滤表达式集合，以 .sfilter 文件（JSON 格式）持久化。
 * 支持加载/保存/导入/导出，方便团队共享。
 */
class FilterPresetManager : public QObject
{
    Q_OBJECT

public:
    struct Preset {
        QString name;
        QString expr;
    };

    explicit FilterPresetManager(QObject *parent = nullptr);

    /// 加载 .sfilter 文件
    bool loadFromFile(const QString &filePath);
    /// 保存到 .sfilter 文件
    bool saveToFile(const QString &filePath) const;

    /// 加载默认预设目录下的 .sfilter 文件
    void loadDefault();
    /// 保存到默认预设目录
    bool saveDefault() const;

    /// 获取所有预设
    const QVector<Preset> &presets() const { return m_presets; }

    /// 添加预设
    void addPreset(const QString &name, const QString &expr);
    /// 删除预设
    void removePreset(int index);
    /// 查找预设表达式
    QString findExpr(const QString &name) const;

    /// 获取预设名称列表
    QStringList presetNames() const;

private:
    QVector<Preset> m_presets;
    QString m_defaultPath;

    /// 初始化默认预设目录路径
    void initDefaultPath();
};

#endif // FILTERPRESETMANAGER_H

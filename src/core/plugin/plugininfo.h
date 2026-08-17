#ifndef PLUGININFO_H
#define PLUGININFO_H

#include <QString>
#include <QStringList>
#include <QList>

/**
 * @brief 插件元数据 — 从 plugin.json 解析
 *
 * 类似 VSCode 的 package.json，描述插件的名称、入口、激活事件和贡献点。
 */
struct PluginInfo
{
    // ---- 基本信息 ----
    QString name;            ///< 插件唯一标识
    QString version;
    QString author;
    QString description;
    QString mainScript;      ///< 入口 Python 文件（如 "main.py"）
    QString icon;            ///< 图标文件相对路径（如 "icon.png"，可选）
    QString directory;       ///< 插件所在目录的绝对路径

    // ---- 激活事件 ----
    QStringList activationEvents;  ///< 如 "onStartup", "onCommand:id", "onFrame"

    // ---- 贡献点 ----
    struct CommandContribution {
        QString id;     ///< 命令 ID（如 "myPlugin.hello"）
        QString title;  ///< 菜单显示文本
    };
    QList<CommandContribution> commands;

    struct FileFormatContribution {
        QString extension;  ///< 如 ".j1939"
        QString name;       ///< 如 "J1939 日志"
    };
    QList<FileFormatContribution> fileFormats;

    // ---- 辅助查询 ----

    /// 是否在指定激活事件时触发
    bool hasActivationEvent(const QString &event) const;

    /// 是否监听帧事件
    bool activatesOnFrame() const;

    /// 是否在启动时激活
    bool activatesOnStartup() const;

    /// 从目录中的 plugin.json 加载
    /// @param dir 插件目录的绝对路径
    /// @return 成功加载返回 true
    bool loadFromDirectory(const QString &dir);

    /// 图标文件绝对路径（icon 字段存在且文件存在时有效，否则返回空串）
    QString iconFilePath() const;
};

#endif // PLUGININFO_H

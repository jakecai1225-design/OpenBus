#ifndef RESOURCERESOLVER_H
#define RESOURCERESOLVER_H

#include <QString>
#include <QStringList>

/**
 * @brief 相对路径解析器 — 工程资源路径与绝对路径互转
 *
 * 以 .openbusproj 所在目录为基准，将外部资源（DBC、日志、过滤器）
 * 在保存时转为相对路径，加载时转回绝对路径。
 *
 * Windows 跨盘符路径（如 D: 工程引用 C: 文件）无法相对化，
 * 此时保持绝对路径，不做错误的相对化。
 */
class ResourceResolver
{
public:
    explicit ResourceResolver(const QString &projFilePath);

    /// 相对路径 → 绝对路径（加载工程时调用）
    /// 空路径返回空；已是绝对路径则原样返回
    QString resolve(const QString &relativeOrAbsPath) const;

    /// 绝对路径 → 相对路径（保存工程时调用）
    /// 跨盘符等无法相对化的路径保持绝对路径
    QString relativize(const QString &absolutePath) const;

    /// 批量解析
    QStringList resolveList(const QStringList &paths) const;
    QStringList relativizeList(const QStringList &paths) const;

    /// .openbusproj 所在目录（绝对路径，末尾无分隔符）
    QString projectDir() const { return m_projDir; }

private:
    QString m_projDir;
};

#endif // RESOURCERESOLVER_H

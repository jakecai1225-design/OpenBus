#include "resourceresolver.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

ResourceResolver::ResourceResolver(const QString &projFilePath)
{
    if (!projFilePath.isEmpty())
        m_projDir = QFileInfo(projFilePath).absolutePath();
}

// ============================================================
//  相对路径 → 绝对路径（加载时调用）
// ============================================================
QString ResourceResolver::resolve(const QString &relativeOrAbsPath) const
{
    if (relativeOrAbsPath.isEmpty())
        return {};

    // 已经是绝对路径 — 原样返回
    if (QDir::isAbsolutePath(relativeOrAbsPath))
        return QDir::cleanPath(relativeOrAbsPath);

    // 无基准目录 — 无法解析，原样返回
    if (m_projDir.isEmpty())
        return relativeOrAbsPath;

    // 相对路径 — 基于 .sinproj 所在目录拼接
    return QDir::cleanPath(m_projDir + '/' + relativeOrAbsPath);
}

// ============================================================
//  绝对路径 → 相对路径（保存时调用）
// ============================================================
QString ResourceResolver::relativize(const QString &absolutePath) const
{
    if (absolutePath.isEmpty())
        return {};

    // 无基准目录 — 无法相对化，原样返回
    if (m_projDir.isEmpty())
        return absolutePath;

    // 非绝对路径 — 原样返回（可能本身已经是相对路径）
    if (!QDir::isAbsolutePath(absolutePath))
        return absolutePath;

    // 检查盘符是否一致（Windows 跨盘符无法相对化）
    // 形如 "C:" / "D:" 的驱动器前缀必须相同
    QString driveProj = QFileInfo(m_projDir).absoluteFilePath();
    QString drivePath = QFileInfo(absolutePath).absoluteFilePath();

    // 提取盘符
    QRegularExpression driveRe("^([A-Za-z]:)");
    auto projMatch = driveRe.match(driveProj);
    auto pathMatch = driveRe.match(drivePath);

    if (projMatch.hasMatch() && pathMatch.hasMatch()) {
        // 两个都是 Windows 绝对路径，比较盘符
        if (projMatch.captured(1).compare(pathMatch.captured(1), Qt::CaseInsensitive) != 0)
            return absolutePath;  // 跨盘符 — 保持绝对路径
    }

    // 同盘符 — 生成相对路径
    QString rel = QDir(m_projDir).relativeFilePath(absolutePath);

    // QDir::relativeFilePath 在无法相对化时会返回绝对路径
    // 如果结果仍为绝对路径，说明路径不存在于基准目录下
    if (QDir::isAbsolutePath(rel))
        return absolutePath;

    return rel;
}

// ============================================================
//  批量操作
// ============================================================
QStringList ResourceResolver::resolveList(const QStringList &paths) const
{
    QStringList result;
    result.reserve(paths.size());
    for (const auto &p : paths)
        result << resolve(p);
    return result;
}

QStringList ResourceResolver::relativizeList(const QStringList &paths) const
{
    QStringList result;
    result.reserve(paths.size());
    for (const auto &p : paths)
        result << relativize(p);
    return result;
}

#include "SessionPathUtils.h"

#include <QDir>
#include <QFileInfo>

namespace
{
Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedAbsoluteDirectory(const QString &path)
{
    return QDir::cleanPath(QDir(path).absolutePath());
}

QString resolvedDirectory(const QString &path)
{
    const QString absolute = normalizedAbsoluteDirectory(path);
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    return canonical.isEmpty() ? absolute : QDir::cleanPath(canonical);
}
} // namespace

namespace SessionPathUtils
{
bool isPathInsideRoot(const QString &rootPath, const QString &targetPath)
{
    if (rootPath.trimmed().isEmpty() || targetPath.trimmed().isEmpty())
        return false;

    const QString root = normalizedAbsoluteDirectory(rootPath);
    const QString target = normalizedAbsoluteDirectory(targetPath);
    const Qt::CaseSensitivity cs = pathCaseSensitivity();
    if (target.compare(root, cs) == 0)
        return true;

    const QString prefix = root.endsWith('/') ? root : root + '/';
    return target.startsWith(prefix, cs);
}

QString sessionDirectoryForWorkingPath(const QString &sessionRoot,
                                       const QString &workingPath)
{
    if (sessionRoot.trimmed().isEmpty() || workingPath.trimmed().isEmpty())
        return QString();

    const QString root = normalizedAbsoluteDirectory(sessionRoot);
    const QString workingDir = QDir::cleanPath(QFileInfo(workingPath).absoluteDir().absolutePath());
    if (!isPathInsideRoot(root, workingDir))
        return QString();

    // Resolve existing directories as well. This rejects a session child that
    // is a symlink/junction escaping the recovery root.
    const QString resolvedRoot = resolvedDirectory(root);
    const QString resolvedWorkingDir = resolvedDirectory(workingDir);
    if (!isPathInsideRoot(resolvedRoot, resolvedWorkingDir))
        return QString();

    const QString relative = QDir::fromNativeSeparators(QDir(root).relativeFilePath(workingDir));
    if (relative.isEmpty() || relative == "." || relative == ".." ||
        relative.startsWith("../") || QDir::isAbsolutePath(relative))
    {
        return QString();
    }

    const QString firstSegment = relative.section('/', 0, 0);
    if (firstSegment.isEmpty() || firstSegment == "." || firstSegment == "..")
        return QString();

    const QString sessionDir = QDir::cleanPath(QDir(root).filePath(firstSegment));
    return isPathInsideRoot(root, sessionDir) && sessionDir != root
               ? sessionDir
               : QString();
}
} // namespace SessionPathUtils

#pragma once

#include <QString>

namespace SessionPathUtils
{
// Path comparisons used by recovery cleanup. Both arguments may be relative;
// they are normalized to absolute paths before comparison.
bool isPathInsideRoot(const QString &rootPath, const QString &targetPath);

// Returns the direct child session directory that owns workingPath. An empty
// result means that workingPath is not safely contained by sessionRoot.
QString sessionDirectoryForWorkingPath(const QString &sessionRoot,
                                       const QString &workingPath);
} // namespace SessionPathUtils

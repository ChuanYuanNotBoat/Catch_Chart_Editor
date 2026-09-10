#pragma once

#include <QString>
#include <QStringList>

class FileUtils
{
public:
    static bool copyFile(const QString &src, const QString &dest);
    // Uses the fast native copy path for a new destination. If dest already
    // exists, replacement is committed atomically so a failed copy preserves
    // the previous file.
    static bool copyFileSafely(const QString &src,
                               const QString &dest,
                               QString *errorOut = nullptr);
    // Always publishes through a same-directory temporary file, including
    // when dest does not yet exist.
    static bool copyFileAtomically(const QString &src,
                                   const QString &dest,
                                   QString *errorOut = nullptr);
    static bool removeFile(const QString &path);
    static bool exists(const QString &path);
    static QStringList getFilesInDir(const QString &dir, const QStringList &filters);
    static bool createDir(const QString &path);
    static QString getTempDir();
};

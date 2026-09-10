#include "FileUtils.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

bool FileUtils::copyFile(const QString &src, const QString &dest)
{
    return QFile::copy(src, dest);
}

bool FileUtils::copyFileAtomically(const QString &src,
                                   const QString &dest,
                                   QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

    const QString sourceAbsolute = QFileInfo(src).absoluteFilePath();
    const QString targetAbsolute = QFileInfo(dest).absoluteFilePath();
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    if (QDir::cleanPath(sourceAbsolute).compare(QDir::cleanPath(targetAbsolute), pathCase) == 0)
        return true;

    QFile source(sourceAbsolute);
    if (!source.open(QIODevice::ReadOnly))
    {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to open source file:\n%1").arg(sourceAbsolute);
        return false;
    }

    QSaveFile target(targetAbsolute);
    if (!target.open(QIODevice::WriteOnly))
    {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to prepare target file:\n%1").arg(targetAbsolute);
        return false;
    }

    constexpr qsizetype kCopyBufferSize = 1024 * 1024;
    QByteArray buffer;
    buffer.resize(kCopyBufferSize);
    while (true)
    {
        const qint64 bytesRead = source.read(buffer.data(), buffer.size());
        if (bytesRead < 0)
        {
            if (errorOut)
                *errorOut = QStringLiteral("Failed while reading source file:\n%1").arg(sourceAbsolute);
            target.cancelWriting();
            return false;
        }
        if (bytesRead == 0)
            break;

        qint64 totalWritten = 0;
        while (totalWritten < bytesRead)
        {
            const qint64 written = target.write(
                buffer.constData() + totalWritten, bytesRead - totalWritten);
            if (written <= 0)
            {
                if (errorOut)
                    *errorOut = QStringLiteral("Failed while writing target file:\n%1").arg(targetAbsolute);
                target.cancelWriting();
                return false;
            }
            totalWritten += written;
        }
    }

    const QFileInfo sourceInfo(sourceAbsolute);
    target.setPermissions(sourceInfo.permissions());
    target.setFileTime(sourceInfo.lastModified(), QFileDevice::FileModificationTime);
    if (!target.commit())
    {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to atomically replace target file:\n%1").arg(targetAbsolute);
        return false;
    }
    return true;
}

bool FileUtils::copyFileSafely(const QString &src,
                               const QString &dest,
                               QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

    // A brand-new destination has no previous data to protect. Keep large
    // working-copy media on QFile's optimized copy path; overwrites still use
    // an atomic replacement so failure preserves the previous destination.
    if (!QFileInfo::exists(dest))
    {
        if (QFile::copy(src, dest))
            return true;
        if (errorOut)
            *errorOut = QStringLiteral("Failed to copy file:\n%1\nto:\n%2")
                            .arg(QFileInfo(src).absoluteFilePath(),
                                 QFileInfo(dest).absoluteFilePath());
        return false;
    }
    return copyFileAtomically(src, dest, errorOut);
}

bool FileUtils::removeFile(const QString &path)
{
    return QFile::remove(path);
}

bool FileUtils::exists(const QString &path)
{
    return QFile::exists(path);
}

QStringList FileUtils::getFilesInDir(const QString &dir, const QStringList &filters)
{
    QDir d(dir);
    return d.entryList(filters, QDir::Files);
}

bool FileUtils::createDir(const QString &path)
{
    QDir d;
    return d.mkpath(path);
}

QString FileUtils::getTempDir()
{
    static QTemporaryDir tempDir;
    if (!tempDir.isValid())
        return QString();
    return tempDir.path();
}

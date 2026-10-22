#include "ReviewResources.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QSet>

namespace CceReview
{
bool collectBundleResources(CceDocument::ProjectBundle &bundle, const QHash<QString, QString> &sources,
                            const QString &fallbackRoot, QString *error)
{
    const auto paths = CceDocument::requiredResourcePaths(bundle.project);
    for (const QString &path : paths)
    {
        QFile file(sources.value(path, QDir(fallbackRoot).filePath(path)));
        if (file.exists())
        {
            if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024 * 1024)
            {
                if (error)
                    *error = QObject::tr("Cannot read resource: %1").arg(path);
                return false;
            }
            const QByteArray bytes = file.readAll();
            if (bytes.size() != file.size())
            {
                if (error)
                    *error = QObject::tr("Cannot read resource: %1").arg(path);
                return false;
            }
            const QString hash =
                QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            bundle.resources[hash] = bytes;
            bundle.resourcePaths[path] = hash;
        }
        else if (!bundle.resourcePaths.contains(path) || !bundle.resources.contains(bundle.resourcePaths.value(path)))
        {
            if (error)
                *error = QObject::tr("Missing resource: %1").arg(path);
            return false;
        }
    }
    for (auto it = bundle.project.difficulties.begin(); it != bundle.project.difficulties.end(); ++it)
        it->oggHash = bundle.resourcePaths.value(it->metadata.audioFile);
    return true;
}
} // namespace CceReview

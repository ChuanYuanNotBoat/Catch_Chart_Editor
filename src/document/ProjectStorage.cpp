#include "ProjectStorage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <QSet>
#include <algorithm>
#include <utility>

namespace CceDocument
{
QStringList requiredResourcePaths(const Project &project)
{
    QSet<QString> paths;
    for (const auto &difficulty : project.difficulties)
    {
        if (!difficulty.metadata.audioFile.isEmpty())
            paths.insert(difficulty.metadata.audioFile);
        if (!difficulty.metadata.backgroundFile.isEmpty())
            paths.insert(difficulty.metadata.backgroundFile);
    }
    for (const auto &layer : project.layers)
        for (const auto &note : layer.notes)
            if (note.isSound() && !note.sound.isEmpty())
                paths.insert(note.sound);
    auto sorted = paths.values();
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}
namespace
{
constexpr qint64 kMaxPackageBytes = 256ll * 1024 * 1024;
constexpr qint64 kMaxResourceBytes = 128ll * 1024 * 1024;
const QByteArray kMagic("CCEPROJECT1\n", 12);

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool validResourcePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith('/') || path.startsWith('\\') || path.contains('\\') || path.contains(':')
        || path.contains(QChar::Null) || QDir::isAbsolutePath(path))
        return false;
    const QStringList components = path.split('/', Qt::KeepEmptyParts);
    for (const QString &part : components)
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
            return false;
    return true;
}

bool validateBundle(const ProjectBundle &bundle, QString *error)
{
    if (!bundle.project.validate(error))
        return false;
    qint64 totalBytes = 0;
    QMap<QString, QString> normalizedHashes;
    for (auto it = bundle.resources.cbegin(); it != bundle.resources.cend(); ++it)
    {
        const QString hash = it.key().toLower();
        if (it.key() != hash || it.key().size() != 64
            || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(hash).hasMatch())
            return fail(error, QStringLiteral("resource key must be a SHA-256 hex digest"));
        if (normalizedHashes.contains(hash))
            return fail(error, QStringLiteral("duplicate resource hash"));
        if (it.value().size() > kMaxResourceBytes)
            return fail(error, QStringLiteral("resource exceeds 128 MiB limit"));
        totalBytes += it.value().size();
        if (totalBytes > kMaxPackageBytes)
            return fail(error, QStringLiteral("resources exceed 256 MiB total limit"));
        if (sha256(it.value()) != hash)
            return fail(error, QStringLiteral("resource content hash mismatch"));
        normalizedHashes.insert(hash, it.key());
    }
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
    {
        if (!validResourcePath(it.key()))
            return fail(error, QStringLiteral("resource path must be relative and traversal-free"));
        if (!normalizedHashes.contains(it.value()) || !bundle.resources.contains(it.value()))
            return fail(error, QStringLiteral("resource path references a missing hash"));
    }
    for (const auto &path : requiredResourcePaths(bundle.project))
        if (!bundle.resourcePaths.contains(path))
            return fail(error, QStringLiteral("required resource mapping is missing: %1").arg(path));
    return true;
}

QJsonObject bundleObject(const ProjectBundle &bundle)
{
    QJsonObject resources;
    for (auto it = bundle.resources.cbegin(); it != bundle.resources.cend(); ++it)
        resources.insert(it.key(), QString::fromLatin1(it.value().toBase64()));
    QJsonObject paths;
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
        paths.insert(it.key(), it.value());
    return {{QStringLiteral("formatVersion"), 1},
            {QStringLiteral("project"), bundle.project.toJson()},
            {QStringLiteral("review"), bundle.review},
            {QStringLiteral("resources"), resources},
            {QStringLiteral("resourcePaths"), paths}};
}

bool parseBundle(const QJsonObject &object, ProjectBundle &out, QString *error)
{
    if (object.value(QStringLiteral("formatVersion")).toInt(-1) != 1
        || !object.value(QStringLiteral("project")).isObject() || !object.value(QStringLiteral("review")).isObject()
        || !object.value(QStringLiteral("resources")).isObject()
        || !object.value(QStringLiteral("resourcePaths")).isObject())
        return fail(error, QStringLiteral("invalid project package header"));
    ProjectBundle parsed;
    if (!Project::fromJson(object.value(QStringLiteral("project")).toObject(), parsed.project, error))
        return false;
    parsed.review = object.value(QStringLiteral("review")).toObject();
    const QJsonObject resourceObjects = object.value(QStringLiteral("resources")).toObject();
    for (auto it = resourceObjects.begin(); it != resourceObjects.end(); ++it)
    {
        if (!it.value().isString())
            return fail(error, QStringLiteral("resource payload must be base64 text"));
        const QByteArray encoded = it.value().toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(encoded);
        if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || decoded.decoded.toBase64() != encoded)
            return fail(error, QStringLiteral("invalid resource base64"));
        parsed.resources.insert(it.key(), decoded.decoded);
    }
    const QJsonObject pathObjects = object.value(QStringLiteral("resourcePaths")).toObject();
    for (auto it = pathObjects.begin(); it != pathObjects.end(); ++it)
    {
        if (!it.value().isString())
            return fail(error, QStringLiteral("resource path hash must be text"));
        parsed.resourcePaths.insert(it.key(), it.value().toString());
    }
    if (!validateBundle(parsed, error))
        return false;
    out = parsed;
    return true;
}

bool existingResourcesMatch(const QString &root, const ProjectBundle &bundle, QString *error)
{
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
    {
        const QString target = QDir(root).filePath(it.key());
        const QFileInfo info(target);
        if (!info.exists() || !info.isFile() || info.isSymLink())
            return fail(error, QStringLiteral("existing materialized resource is missing or unsafe: %1").arg(it.key()));
        const QByteArray data = bundle.resources.value(it.value());
        if (info.size() != data.size())
            return fail(error, QStringLiteral("existing materialized resource differs: %1").arg(it.key()));
        QFile file(target);
        if (!file.open(QIODevice::ReadOnly) || sha256(file.readAll()) != it.value())
            return fail(error, QStringLiteral("existing materialized resource differs: %1").arg(it.key()));
    }
    return true;
}
} // namespace

bool writeProject(const QString &path, const ProjectBundle &bundle, QString *error)
{
    if (!validateBundle(bundle, error))
        return false;
    const QByteArray json = QJsonDocument(bundleObject(bundle)).toJson(QJsonDocument::Compact);
    if (json.size() > kMaxPackageBytes)
        return fail(error, QStringLiteral("project data exceeds 256 MiB limit"));
    const QByteArray compressed = qCompress(json, 9);
    if (compressed.isEmpty() || compressed.size() + kMagic.size() > kMaxPackageBytes)
        return fail(error, QStringLiteral("compressed project exceeds 256 MiB limit"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("cannot open project output: %1").arg(file.errorString()));
    if (file.write(kMagic) != kMagic.size() || file.write(compressed) != compressed.size())
    {
        file.cancelWriting();
        return fail(error, QStringLiteral("cannot write project package"));
    }
    if (!file.commit())
        return fail(error, QStringLiteral("cannot commit project package: %1").arg(file.errorString()));
    return true;
}

bool readProject(const QString &path, ProjectBundle &out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot open project package: %1").arg(file.errorString()));
    if (file.size() < kMagic.size() + 4 || file.size() > kMaxPackageBytes)
        return fail(error, QStringLiteral("project package size is invalid"));
    const QByteArray bytes = file.readAll();
    if (bytes.size() != file.size() || !bytes.startsWith(kMagic))
        return fail(error, QStringLiteral("invalid project magic or truncated file"));
    const QByteArray packed = bytes.mid(kMagic.size());
    const quint32 declaredLength = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(packed.constData()));
    if (declaredLength == 0 || declaredLength > static_cast<quint32>(kMaxPackageBytes))
        return fail(error, QStringLiteral("declared project expansion size is invalid"));
    const QByteArray json = qUncompress(packed);
    if (json.size() != static_cast<qsizetype>(declaredLength) || json.size() > kMaxPackageBytes)
        return fail(error, QStringLiteral("project decompression failed or exceeded size limit"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("invalid project JSON: %1").arg(parseError.errorString()));
    ProjectBundle parsed;
    if (!parseBundle(document.object(), parsed, error))
        return false;
    out = parsed;
    return true;
}

bool materializeResources(const ProjectBundle &bundle, const QString &destinationRoot, QString *error)
{
    if (!validateBundle(bundle, error))
        return false;
    QDir root(destinationRoot);
    if (!root.exists() && !QDir().mkpath(destinationRoot))
        return fail(error, QStringLiteral("cannot create resource destination"));
    root = QDir(destinationRoot);
    const QString finalName = bundle.project.projectId;
    const QString finalPath = root.filePath(finalName);
    if (QFileInfo::exists(finalPath))
        return existingResourcesMatch(finalPath, bundle, error);

    QTemporaryDir staged(root.filePath(QStringLiteral(".cce-stage-XXXXXX")));
    if (!staged.isValid())
        return fail(error, QStringLiteral("cannot create resource staging directory"));
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
    {
        const QString target = QDir(staged.path()).filePath(it.key());
        const QFileInfo targetInfo(target);
        if (!QDir().mkpath(targetInfo.absolutePath()))
            return fail(error, QStringLiteral("cannot create staged resource directory"));
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return fail(error, QStringLiteral("cannot create staged resource file"));
        const QByteArray &data = bundle.resources[it.value()];
        if (file.write(data) != data.size() || !file.flush())
            return fail(error, QStringLiteral("cannot write staged resource"));
        file.close();
    }
    if (QFileInfo::exists(finalPath))
        return existingResourcesMatch(finalPath, bundle, error);
    if (!root.rename(QFileInfo(staged.path()).fileName(), finalName))
        return fail(error, QStringLiteral("cannot publish complete resource directory"));
    return true;
}
} // namespace CceDocument

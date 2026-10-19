#include "ExportPipeline.h"

#include "file/ChartIO.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>

namespace CceDocument
{
namespace
{
constexpr qint64 kMaxResourceBytes = 128ll * 1024 * 1024;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString hashBytes(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.startsWith('/') || path.startsWith('\\') || path.contains('\\') || path.contains(':')
        || path.contains(QChar::Null) || QDir::isAbsolutePath(path))
        return false;
    for (const QString &part : path.split('/', Qt::KeepEmptyParts))
        if (part.isEmpty() || part == QStringLiteral(".") || part == QStringLiteral(".."))
            return false;
    return true;
}

QString safeBaseName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    while (name.startsWith('.'))
        name.remove(0, 1);
    if (name.isEmpty() || name == QStringLiteral(".."))
        name = QStringLiteral("resource");
    if (name.size() > 128)
        name.truncate(128);
    return name;
}

bool validateBundleResources(const ProjectBundle &bundle, QString *error)
{
    for (auto it = bundle.resources.cbegin(); it != bundle.resources.cend(); ++it)
    {
        if (it.key().size() != 64 || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(it.key()).hasMatch())
            return fail(error, QStringLiteral("resource key is not a lowercase SHA-256 digest"));
        if (it.value().size() > kMaxResourceBytes)
            return fail(error, QStringLiteral("resource exceeds 128 MiB limit"));
        if (hashBytes(it.value()) != it.key())
            return fail(error, QStringLiteral("resource content hash mismatch"));
    }
    for (auto it = bundle.resourcePaths.cbegin(); it != bundle.resourcePaths.cend(); ++it)
    {
        if (!safeRelativePath(it.key()) || !bundle.resources.contains(it.value()))
            return fail(error, QStringLiteral("resource path is unsafe or references a missing resource"));
    }
    return true;
}

bool resourceFilenameFor(const ProjectBundle &bundle, const QString &logicalPath,
                         QMap<QString, QByteArray> &outputFiles, QString &filename, QString *error)
{
    if (!safeRelativePath(logicalPath) || !bundle.resourcePaths.contains(logicalPath))
        return fail(error, QStringLiteral("required resource is absent from the package: %1").arg(logicalPath));
    const QString hash = bundle.resourcePaths.value(logicalPath);
    if (!bundle.resources.contains(hash))
        return fail(error, QStringLiteral("required resource data is missing: %1").arg(logicalPath));
    const QString candidate = hash + QLatin1Char('_') + safeBaseName(logicalPath);
    const QByteArray bytes = bundle.resources.value(hash);
    if (outputFiles.contains(candidate) && outputFiles.value(candidate) != bytes)
        return fail(error, QStringLiteral("resource filename collision"));
    outputFiles.insert(candidate, bytes);
    filename = candidate;
    return true;
}

bool writeResources(const QString &directory, const QMap<QString, QByteArray> &files, QString *error)
{
    for (auto it = files.cbegin(); it != files.cend(); ++it)
    {
        const QString target = QDir(directory).filePath(it.key());
        const QFileInfo info(target);
        if (info.exists())
        {
            if (!info.isFile() || info.isSymLink() || info.size() != it.value().size())
                return fail(error, QStringLiteral("existing resource has conflicting content: %1").arg(it.key()));
            QFile existing(target);
            if (!existing.open(QIODevice::ReadOnly) || hashBytes(existing.readAll()) != it.key().left(64))
                return fail(error, QStringLiteral("existing resource has conflicting content: %1").arg(it.key()));
            continue;
        }
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly))
            return fail(error, QStringLiteral("cannot create resource file: %1").arg(file.errorString()));
        if (file.write(it.value()) != it.value().size())
        {
            file.cancelWriting();
            return fail(error, QStringLiteral("cannot write resource file"));
        }
        if (!file.commit())
            return fail(error, QStringLiteral("cannot commit resource file: %1").arg(file.errorString()));
    }
    return true;
}

bool metadataEnabled(const Difficulty &difficulty, const QString &field)
{
    return difficulty.exportedMetadataFields.contains(field);
}

void filterMetadata(QJsonObject &root, const Difficulty &difficulty)
{
    QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    QJsonObject song = meta.value(QStringLiteral("song")).toObject();
    QJsonObject modeExt = meta.value(QStringLiteral("mode_ext")).toObject();
    if (!metadataEnabled(difficulty, QStringLiteral("chartAuthor")))
        meta.remove(QStringLiteral("creator"));
    if (!metadataEnabled(difficulty, QStringLiteral("backgroundFile")))
        meta.remove(QStringLiteral("background"));
    if (!metadataEnabled(difficulty, QStringLiteral("difficulty")))
        meta.remove(QStringLiteral("version"));
    if (!metadataEnabled(difficulty, QStringLiteral("title")))
        song.remove(QStringLiteral("title"));
    if (!metadataEnabled(difficulty, QStringLiteral("artist")))
        song.remove(QStringLiteral("artist"));
    if (!metadataEnabled(difficulty, QStringLiteral("titleOrg")))
        song.remove(QStringLiteral("titleorg"));
    if (!metadataEnabled(difficulty, QStringLiteral("artistOrg")))
        song.remove(QStringLiteral("artistorg"));
    if (!metadataEnabled(difficulty, QStringLiteral("audioFile")))
        meta.remove(QStringLiteral("audio"));
    if (!metadataEnabled(difficulty, QStringLiteral("previewTime")))
        meta.remove(QStringLiteral("preview"));
    if (!metadataEnabled(difficulty, QStringLiteral("offset")))
        meta.remove(QStringLiteral("offset"));
    if (!metadataEnabled(difficulty, QStringLiteral("firstBpm")))
        meta.remove(QStringLiteral("bpm"));
    if (!metadataEnabled(difficulty, QStringLiteral("speed")))
        modeExt.remove(QStringLiteral("speed"));
    meta.insert(QStringLiteral("song"), song);
    meta.insert(QStringLiteral("mode_ext"), modeExt);
    root.insert(QStringLiteral("meta"), meta);
}
} // namespace

bool exportDifficulty(const ProjectBundle &bundle, const QString &difficultyId, const QString &outputPath,
                      const QStringList &onlyLayerIds, QString *error)
{
    if (!bundle.project.validate(error) || !validateBundleResources(bundle, error))
        return false;
    if (!bundle.project.difficulties.contains(difficultyId))
        return fail(error, QStringLiteral("difficulty does not exist"));
    const Difficulty &difficulty = bundle.project.difficulties[difficultyId];
    QSet<QString> selected;
    for (const QString &id : onlyLayerIds)
    {
        if (selected.contains(id))
            return fail(error, QStringLiteral("duplicate selected layer"));
        bool found = false;
        for (const LayerBinding &binding : difficulty.bindings)
            if (binding.layerId == id)
            {
                found = true;
                break;
            }
        if (!found)
            return fail(error, QStringLiteral("selected layer is not bound to this difficulty"));
        selected.insert(id);
    }
    Chart chart = bundle.project.compose(difficultyId, onlyLayerIds, onlyLayerIds.isEmpty());
    QMap<QString, QByteArray> resourceFiles;
    QString audioName, backgroundName;
    if (!chart.meta().audioFile.isEmpty()
        && !resourceFilenameFor(bundle, chart.meta().audioFile, resourceFiles, audioName, error))
        return false;
    if (!chart.meta().backgroundFile.isEmpty()
        && !resourceFilenameFor(bundle, chart.meta().backgroundFile, resourceFiles, backgroundName, error))
        return false;
    QHash<QString, QString> soundNames;
    for (Note &note : chart.notes())
        if (note.isSound())
        {
            QString filename;
            if (!resourceFilenameFor(bundle, note.sound, resourceFiles, filename, error))
                return false;
            soundNames.insert(note.id, filename);
            note.sound = filename;
        }
    if (!audioName.isEmpty())
        chart.meta().audioFile = audioName;
    if (!backgroundName.isEmpty())
        chart.meta().backgroundFile = backgroundName;

    const QFileInfo outputInfo(outputPath);
    if (outputInfo.fileName().isEmpty())
        return fail(error, QStringLiteral("output path has no filename"));
    const QString outputDirectory = outputInfo.absolutePath();
    if (!QDir().mkpath(outputDirectory))
        return fail(error, QStringLiteral("cannot create export directory"));
    if (resourceFiles.contains(outputInfo.fileName()))
        return fail(error, QStringLiteral("chart output filename collides with a resource"));
    if (!writeResources(outputDirectory, resourceFiles, error))
        return false;

    QTemporaryDir candidate(QDir(outputDirectory).filePath(QStringLiteral(".cce-export-XXXXXX")));
    if (!candidate.isValid())
        return fail(error, QStringLiteral("cannot create temporary chart directory"));
    for (auto it = resourceFiles.cbegin(); it != resourceFiles.cend(); ++it)
        if (!QFile::copy(QDir(outputDirectory).filePath(it.key()), QDir(candidate.path()).filePath(it.key())))
            return fail(error, QStringLiteral("cannot stage export resource"));
    const QString candidatePath = QDir(candidate.path()).filePath(QStringLiteral("candidate.mc"));
    if (!ChartIO::save(candidatePath, chart))
        return fail(error, QStringLiteral("chart serializer failed"));
    QFile candidateFile(candidatePath);
    if (!candidateFile.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("cannot read generated chart"));
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(candidateFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("chart serializer produced invalid JSON"));
    QJsonObject root = document.object();
    QJsonArray notes = root.value(QStringLiteral("note")).toArray();
    const QVector<Note> &sourceNotes = chart.notes();
    for (int i = 0; i < sourceNotes.size() && i < notes.size(); ++i)
    {
        if (!sourceNotes[i].isSound())
            continue;
        QJsonObject noteObject = notes[i].toObject();
        noteObject.insert(QStringLiteral("sound"), soundNames.value(sourceNotes[i].id));
        noteObject.insert(QStringLiteral("vol"), sourceNotes[i].vol);
        noteObject.insert(QStringLiteral("offset"), sourceNotes[i].offset);
        notes[i] = noteObject;
    }
    root.insert(QStringLiteral("note"), notes);
    if (!audioName.isEmpty())
    {
        QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
        meta.insert(QStringLiteral("audio"), audioName);
        root.insert(QStringLiteral("meta"), meta);
    }
    if (!backgroundName.isEmpty())
    {
        QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
        meta.insert(QStringLiteral("background"), backgroundName);
        root.insert(QStringLiteral("meta"), meta);
    }
    filterMetadata(root, difficulty);
    const QByteArray output = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile finalFile(outputPath);
    if (!finalFile.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("cannot open output chart: %1").arg(finalFile.errorString()));
    if (finalFile.write(output) != output.size())
    {
        finalFile.cancelWriting();
        return fail(error, QStringLiteral("cannot write output chart"));
    }
    if (!finalFile.commit())
        return fail(error, QStringLiteral("cannot commit output chart: %1").arg(finalFile.errorString()));
    return true;
}
} // namespace CceDocument

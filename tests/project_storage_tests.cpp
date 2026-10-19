#include <QtTest>
#include "document/ProjectStorage.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>

using namespace CceDocument;

class ProjectStorageTests : public QObject
{
    Q_OBJECT
private slots:
    void roundTripsProjectReviewAndResources();
    void materializesAtomicallyAndIdempotently();
    void rejectsMalformedOversizedEscapingAndHashMismatchWithoutChangingOutput();
    void failedWriteLeavesExistingFileUntouched();
};

static ProjectBundle sampleBundle()
{
    Chart chart;
    chart.meta().title = QStringLiteral("Storage test");
    chart.meta().audioFile = QStringLiteral("audio/song.ogg");
    chart.bpmList() = {BpmEntry(0, 0, 1, 120.0)};
    chart.setNotes({Note(8, 0, 1, 200)});
    ProjectBundle bundle;
    bundle.project = Project::fromChart(chart);
    const QByteArray bytes("bundled sample resource");
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    bundle.resources.insert(hash, bytes);
    bundle.resourcePaths.insert(QStringLiteral("images/background.bin"), hash);
    bundle.resourcePaths.insert(QStringLiteral("audio/song.ogg"), hash);
    bundle.review = {{QStringLiteral("opaqueReviewData"), 42}};
    return bundle;
}

static QJsonObject bundleObject(const ProjectBundle &bundle)
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

static bool writeRawPackage(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write("CCEPROJECT1\n", 12);
    file.write(qCompress(QJsonDocument(object).toJson(QJsonDocument::Compact), 9));
    return file.flush();
}

void ProjectStorageTests::roundTripsProjectReviewAndResources()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProjectBundle bundle = sampleBundle();
    const QString path = QDir(temp.path()).filePath(QStringLiteral("song.cceproj"));
    QString error;
    QVERIFY2(writeProject(path, bundle, &error), qPrintable(error));
    ProjectBundle restored;
    QVERIFY2(readProject(path, restored, &error), qPrintable(error));
    QCOMPARE(restored.project.toJson(), bundle.project.toJson());
    QCOMPARE(restored.review, bundle.review);
    QCOMPARE(restored.resources, bundle.resources);
    QCOMPARE(restored.resourcePaths, bundle.resourcePaths);
}

void ProjectStorageTests::materializesAtomicallyAndIdempotently()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProjectBundle bundle = sampleBundle();
    QString error;
    QVERIFY2(materializeResources(bundle, temp.path(), &error), qPrintable(error));
    QVERIFY2(materializeResources(bundle, temp.path(), &error), qPrintable(error));
    const QString target =
        QDir(temp.path()).filePath(bundle.project.projectId + QStringLiteral("/images/background.bin"));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bundle.resources.cbegin().value());

    ProjectBundle conflict = bundle;
    const QByteArray other("different resource content");
    const QString otherHash = QString::fromLatin1(QCryptographicHash::hash(other, QCryptographicHash::Sha256).toHex());
    conflict.resources.insert(otherHash, other);
    conflict.resourcePaths[QStringLiteral("images/background.bin")] = otherHash;
    QVERIFY(!materializeResources(conflict, temp.path(), &error));
}

void ProjectStorageTests::rejectsMalformedOversizedEscapingAndHashMismatchWithoutChangingOutput()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const ProjectBundle bundle = sampleBundle();
    const QString path = QDir(temp.path()).filePath(QStringLiteral("bad.cceproj"));
    QString error;
    QVERIFY(writeProject(path, bundle, &error));
    ProjectBundle out = bundle;
    const QString originalProjectId = out.project.projectId;

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("not a project");
    file.close();
    QVERIFY(!readProject(path, out, &error));
    QCOMPARE(out.project.projectId, originalProjectId);

    QByteArray oversized("CCEPROJECT1\n", 12);
    const quint32 tooLarge = qToBigEndian<quint32>(256u * 1024u * 1024u + 1u);
    oversized.append(reinterpret_cast<const char *>(&tooLarge), sizeof(tooLarge));
    oversized.append("x", 1);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(oversized);
    file.close();
    QVERIFY(!readProject(path, out, &error));
    QCOMPARE(out.project.projectId, originalProjectId);

    ProjectBundle escaped = bundle;
    escaped.resourcePaths.clear();
    escaped.resourcePaths.insert(QStringLiteral("../escape.bin"), bundle.resources.firstKey());
    QVERIFY(!writeProject(path, escaped, &error));
    QJsonObject escapedJson = bundleObject(bundle);
    escapedJson.insert(QStringLiteral("resourcePaths"),
                       QJsonObject{{QStringLiteral("../escape.bin"), bundle.resources.firstKey()}});
    QVERIFY(writeRawPackage(path, escapedJson));
    QVERIFY(!readProject(path, out, &error));
    QCOMPARE(out.project.projectId, originalProjectId);

    QVERIFY(writeProject(path, bundle, &error));
    QFile validFile(path);
    QVERIFY(validFile.open(QIODevice::ReadOnly));
    const QByteArray validBytes = validFile.readAll();
    const QByteArray payload = qUncompress(validBytes.mid(12));
    QJsonObject root = QJsonDocument::fromJson(payload).object();
    QJsonObject resources = root.value(QStringLiteral("resources")).toObject();
    resources.insert(bundle.resources.firstKey(), QString::fromLatin1(QByteArray("tampered").toBase64()));
    root.insert(QStringLiteral("resources"), resources);
    QVERIFY(writeRawPackage(path, root));
    QVERIFY(!readProject(path, out, &error));
    QCOMPARE(out.project.projectId, originalProjectId);
}

void ProjectStorageTests::failedWriteLeavesExistingFileUntouched()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = QDir(temp.path()).filePath(QStringLiteral("keep.cceproj"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray original("previous package");
    QCOMPARE(file.write(original), original.size());
    file.close();

    ProjectBundle invalid = sampleBundle();
    invalid.resources[invalid.resources.firstKey()] += 'x';
    QString error;
    QVERIFY(!writeProject(path, invalid, &error));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
}

QTEST_APPLESS_MAIN(ProjectStorageTests)
#include "project_storage_tests.moc"

#include <QtTest>
#include "document/ExportPipeline.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace CceDocument;

class ExportPipelineTests : public QObject
{
    Q_OBJECT
private slots:
    void normalExportUsesExportEnabledButIgnoresVisibility();
    void independentLayerExportAndMetadataSettingsDoNotMutateDocument();
    void sharedLayersExportAndSoundResourcesUseActualFields();
    void missingOrBadResourcesPreserveExistingChart();
};

static QString addResource(ProjectBundle &bundle, const QString &logicalPath, const QByteArray &data)
{
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    bundle.resources.insert(hash, data);
    bundle.resourcePaths.insert(logicalPath, hash);
    return hash;
}

static ProjectBundle plainBundle()
{
    Chart chart;
    chart.meta().title = QStringLiteral("Export song");
    chart.meta().artist = QStringLiteral("Artist");
    chart.meta().difficulty = QStringLiteral("Expert");
    chart.meta().chartAuthor = QStringLiteral("Mapper");
    chart.meta().titleOrg = QStringLiteral("Original title");
    chart.meta().artistOrg = QStringLiteral("Original artist");
    chart.meta().previewTime = 1500;
    chart.meta().firstBpm = 150.0;
    chart.meta().offset = -24;
    chart.meta().speed = 2;
    chart.bpmList() = {BpmEntry(0, 0, 1, 150.0)};
    chart.setNotes({Note(4, 0, 1, 100)});
    ProjectBundle bundle;
    bundle.project = Project::fromChart(chart);
    return bundle;
}

static QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void ExportPipelineTests::normalExportUsesExportEnabledButIgnoresVisibility()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ProjectBundle bundle = plainBundle();
    const QString difficultyId = bundle.project.activeDifficultyId;
    const QString firstLayer = bundle.project.difficulties[difficultyId].bindings.first().layerId;
    const QString secondLayer = bundle.project.addLayer(difficultyId, QStringLiteral("Disabled export"));
    bundle.project.layers[secondLayer].notes.append(Note(8, 0, 1, 300));
    bundle.project.difficulties[difficultyId].bindings[0].visible = false;
    bundle.project.difficulties[difficultyId].bindings[1].exportEnabled = false;

    const QString output = QDir(temp.path()).filePath(QStringLiteral("normal.mc"));
    QString error;
    QVERIFY2(exportDifficulty(bundle, difficultyId, output, {}, &error), qPrintable(error));
    const QJsonArray notes = readObject(output).value(QStringLiteral("note")).toArray();
    QCOMPARE(notes.size(), 1);
    QCOMPARE(notes.first().toObject().value(QStringLiteral("x")).toInt(), 100);
    QVERIFY(firstLayer != secondLayer);
}

void ExportPipelineTests::independentLayerExportAndMetadataSettingsDoNotMutateDocument()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ProjectBundle bundle = plainBundle();
    const QString difficultyId = bundle.project.activeDifficultyId;
    const QString hiddenFromDefault = bundle.project.difficulties[difficultyId].bindings.first().layerId;
    bundle.project.difficulties[difficultyId].bindings[0].exportEnabled = false;
    const QString beforeTitle = bundle.project.difficulties[difficultyId].metadata.title;
    bundle.project.difficulties[difficultyId].exportedMetadataFields.remove(QStringLiteral("title"));
    bundle.project.difficulties[difficultyId].exportedMetadataFields.remove(QStringLiteral("chartAuthor"));
    const QString output = QDir(temp.path()).filePath(QStringLiteral("layer.mc"));
    QString error;
    QVERIFY2(exportDifficulty(bundle, difficultyId, output, {hiddenFromDefault}, &error), qPrintable(error));
    const QJsonObject root = readObject(output);
    QCOMPARE(root.value(QStringLiteral("note")).toArray().size(), 1);
    const QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    QVERIFY(!meta.value(QStringLiteral("song")).toObject().contains(QStringLiteral("title")));
    QVERIFY(!meta.contains(QStringLiteral("creator")));
    QCOMPARE(bundle.project.difficulties[difficultyId].metadata.title, beforeTitle);
    QCOMPARE(bundle.project.difficulties[difficultyId].metadata.chartAuthor, QStringLiteral("Mapper"));
}

void ExportPipelineTests::sharedLayersExportAndSoundResourcesUseActualFields()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    Chart chart;
    chart.meta().title = QStringLiteral("Shared");
    chart.meta().artist = QStringLiteral("Artist");
    chart.meta().difficulty = QStringLiteral("Normal");
    chart.meta().audioFile = QStringLiteral("audio/song.ogg");
    chart.meta().backgroundFile = QStringLiteral("images/bg.png");
    chart.meta().offset = 17;
    chart.bpmList() = {BpmEntry(0, 0, 1, 120.0)};
    chart.setNotes({Note(0, 0, 1, QStringLiteral("audio/song.ogg"), 75, 23),
                    Note(2, 0, 1, QStringLiteral("sfx/hit.wav"), 61, -12)});
    ProjectBundle bundle;
    bundle.project = Project::fromChart(chart, QStringLiteral("unused-audio-hash"));
    const QString audioHash = addResource(bundle, QStringLiteral("audio/song.ogg"), QByteArray("audio bytes"));
    const QString backgroundHash = addResource(bundle, QStringLiteral("images/bg.png"), QByteArray("background bytes"));
    const QString effectHash = addResource(bundle, QStringLiteral("sfx/hit.wav"), QByteArray("effect bytes"));
    Q_UNUSED(audioHash);
    Q_UNUSED(backgroundHash);
    Q_UNUSED(effectHash);

    const QString source = bundle.project.activeDifficultyId;
    const QString shared = bundle.project.difficulties[source].bindings.first().layerId;
    const QString destination =
        bundle.project.addDifficulty(QStringLiteral("Hard"), bundle.project.difficulties[source].metadata,
                                     bundle.project.difficulties[source].timing, QStringLiteral("same-audio"));
    bundle.project.difficulties[source].oggHash = QStringLiteral("same-audio");
    QVERIFY(bundle.project.bindLayer(destination, shared));

    const QString output = QDir(temp.path()).filePath(QStringLiteral("hard.mc"));
    QString error;
    QVERIFY2(exportDifficulty(bundle, destination, output, {}, &error), qPrintable(error));
    const QJsonObject root = readObject(output);
    const QJsonArray notes = root.value(QStringLiteral("note")).toArray();
    QCOMPARE(notes.size(), 2);
    QSet<QString> uniqueSoundFiles;
    for (const QJsonValue &v : notes)
    {
        const QJsonObject n = v.toObject();
        QVERIFY(!n.contains(QStringLiteral("id")));
        if (n.value(QStringLiteral("type")).toInt() == 1)
        {
            uniqueSoundFiles.insert(n.value(QStringLiteral("sound")).toString());
            if (n.value(QStringLiteral("sound")).toString().contains(QStringLiteral("hit.wav")))
            {
                QCOMPARE(n.value(QStringLiteral("vol")).toInt(), 61);
                QCOMPARE(n.value(QStringLiteral("offset")).toInt(), -12);
            }
        }
    }
    QCOMPARE(uniqueSoundFiles.size(), 2);
    QVERIFY(QFileInfo::exists(
        QDir(temp.path())
            .filePath(root.value(QStringLiteral("meta")).toObject().value(QStringLiteral("audio")).toString())));
    QVERIFY(QFileInfo::exists(
        QDir(temp.path())
            .filePath(root.value(QStringLiteral("meta")).toObject().value(QStringLiteral("background")).toString())));
}

void ExportPipelineTests::missingOrBadResourcesPreserveExistingChart()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    ProjectBundle bundle = plainBundle();
    const QString difficultyId = bundle.project.activeDifficultyId;
    bundle.project.difficulties[difficultyId].metadata.audioFile = QStringLiteral("audio/missing.ogg");
    const QString output = QDir(temp.path()).filePath(QStringLiteral("existing.mc"));
    QFile existing(output);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    const QByteArray oldContent("old chart remains intact");
    QCOMPARE(existing.write(oldContent), oldContent.size());
    existing.close();
    QString error;
    QVERIFY(!exportDifficulty(bundle, difficultyId, output, {}, &error));
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), oldContent);
    existing.close();

    const QByteArray content("actual file");
    const QString expectedHash =
        QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
    bundle.resources.insert(expectedHash, QByteArray("corrupted"));
    bundle.resourcePaths.insert(QStringLiteral("audio/missing.ogg"), expectedHash);
    QVERIFY(!exportDifficulty(bundle, difficultyId, output, {}, &error));
    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), oldContent);
}

QTEST_APPLESS_MAIN(ExportPipelineTests)
#include "export_pipeline_tests.moc"

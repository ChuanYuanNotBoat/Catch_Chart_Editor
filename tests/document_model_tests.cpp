#include <QtTest>
#include "document/DocumentModel.h"
#include <QJsonArray>
#include <QJsonDocument>

using namespace CceDocument;

class DocumentModelTests : public QObject
{
    Q_OBJECT
private slots:
    void sharedLayerEditUpdatesEveryDifficulty();
    void independentCopyIsolated();
    void visibilityDoesNotChangeComposition();
    void exportEnabledIsIndependent();
    void additionsUseSelectedLayerAndLockedEditsFailAtomically();
    void referencesRequireMatchingAudioAndDeduplicateLayers();
    void canonicalBeatsAndDuplicateNotes();
    void persistenceOmitsRuntimeNoteIdsAndRollsBackOnInvalidInput();
};

static Chart sampleChart()
{
    Chart c;
    c.meta().title = QStringLiteral("Song");
    c.meta().audioFile = QStringLiteral("audio/song.ogg");
    c.bpmList() = {BpmEntry(0, 0, 1, 120.0)};
    c.setNotes({Note(4, 0, 1, 128)});
    return c;
}

void DocumentModelTests::sharedLayerEditUpdatesEveryDifficulty()
{
    Project p = Project::fromChart(sampleChart(), QStringLiteral("audio-hash"));
    const QString first = p.activeDifficultyId;
    const QString layer = p.difficulties[first].bindings.first().layerId;
    const QString second = p.addDifficulty(QStringLiteral("Hard"), p.difficulties[first].metadata,
                                           p.difficulties[first].timing, QStringLiteral("audio-hash"));
    QVERIFY(p.bindLayer(second, layer));
    Chart before = p.compose(first);
    Chart after = before;
    after.notes()[0].x = 300;
    QVERIFY(p.updateComposedChart(first, before, after));
    QCOMPARE(p.compose(second).notes().first().x, 300);
}

void DocumentModelTests::independentCopyIsolated()
{
    Project p = Project::fromChart(sampleChart());
    const QString diff = p.activeDifficultyId;
    const QString original = p.difficulties[diff].bindings.first().layerId;
    const QString clone = p.copyAsIndependent(diff, original);
    QVERIFY(!clone.isEmpty());
    QVERIFY(p.layers.contains(original));
    QVERIFY(p.layers.contains(clone));
    QVERIFY(p.layers[original].notes.first().id != p.layers[clone].notes.first().id);
    Chart before = p.compose(diff);
    Chart after = before;
    after.notes()[0].x = 9;
    QVERIFY(p.updateComposedChart(diff, before, after));
    QCOMPARE(p.layers[clone].notes.first().x, 9);
    QCOMPARE(p.layers[original].notes.first().x, 128);
}

void DocumentModelTests::visibilityDoesNotChangeComposition()
{
    Project p = Project::fromChart(sampleChart());
    const QString diff = p.activeDifficultyId;
    p.difficulties[diff].bindings[0].visible = false;
    QCOMPARE(p.compose(diff).notes().size(), 1);
    QCOMPARE(p.compose(diff, {}, true).notes().size(), 1);
}

void DocumentModelTests::exportEnabledIsIndependent()
{
    Project p = Project::fromChart(sampleChart());
    const QString diff = p.activeDifficultyId;
    const QString extra = p.addLayer(diff, QStringLiteral("Extra"));
    QVERIFY(!extra.isEmpty());
    p.layers[extra].notes.append(Note(5, 0, 1, 200));
    p.difficulties[diff].bindings[0].visible = false;
    p.difficulties[diff].bindings[0].exportEnabled = false;
    QCOMPARE(p.compose(diff).notes().size(), 2);
    QCOMPARE(p.compose(diff, {}, true).notes().size(), 1);
    QCOMPARE(p.compose(diff, {p.difficulties[diff].bindings[0].layerId}).notes().size(), 1);
}

void DocumentModelTests::additionsUseSelectedLayerAndLockedEditsFailAtomically()
{
    Project p = Project::fromChart(sampleChart());
    const QString diff = p.activeDifficultyId;
    const QString original = p.difficulties[diff].bindings.first().layerId;
    const QString selected = p.addLayer(diff, QStringLiteral("Selected"));
    QVERIFY(!selected.isEmpty());
    Chart before = p.compose(diff);
    Chart after = before;
    after.notes().append(Note(9, 0, 1, 300));
    QVERIFY(p.updateComposedChart(diff, before, after, nullptr, selected));
    QCOMPARE(p.layers[selected].notes.size(), 1);
    QCOMPARE(p.layers[original].notes.size(), 1);

    Chart lockedBefore = p.compose(diff);
    Chart changed = lockedBefore;
    changed.notes()[0].x = 10;
    p.difficulties[diff].bindings[0].locked = true;
    const int oldX = p.layers[original].notes.first().x;
    QVERIFY(!p.updateComposedChart(diff, lockedBefore, changed));
    QCOMPARE(p.layers[original].notes.first().x, oldX);
}

void DocumentModelTests::referencesRequireMatchingAudioAndDeduplicateLayers()
{
    Project p = Project::fromChart(sampleChart(), QStringLiteral("same"));
    const QString source = p.activeDifficultyId;
    const QString target = p.addDifficulty(QStringLiteral("Hard"), p.difficulties[source].metadata,
                                           p.difficulties[source].timing, QStringLiteral("same"));
    const QString layer = p.difficulties[source].bindings.first().layerId;
    QString error;
    QVERIFY(p.addReference(target, source, {layer, layer}, &error));
    QCOMPARE(p.difficulties[target].references.first().layerIds.size(), 1);
    QVERIFY(p.addReference(target, source, {layer}, &error));
    QCOMPARE(p.difficulties[target].references.size(), 1);
    p.difficulties[target].oggHash.clear();
    QVERIFY(!p.addReference(target, source, {}, &error));
    QVERIFY(!p.validate(&error));
}

void DocumentModelTests::canonicalBeatsAndDuplicateNotes()
{
    Note a(2, 2, 4, 128);
    Note b(2, 1, 2, 128);
    QCOMPARE(encodeNote(a, true), encodeNote(b, true));
    QVERIFY(encodeNote(a, false) != encodeNote(b, false));
    Chart c = sampleChart();
    c.notes().append(c.notes().first());
    const QJsonArray encoded = encodeChart(c).value(QStringLiteral("notes")).toArray();
    QCOMPARE(encoded.size(), 2);
    Chart roundtrip;
    QVERIFY(decodeChart(encodeChart(c), roundtrip));
    QCOMPARE(roundtrip.notes().size(), 2);
}

void DocumentModelTests::persistenceOmitsRuntimeNoteIdsAndRollsBackOnInvalidInput()
{
    Project p = Project::fromChart(sampleChart());
    const QJsonObject json = p.toJson();
    const QJsonObject layer = json.value(QStringLiteral("layers"))
                                  .toObject()
                                  .value(p.difficulties[p.activeDifficultyId].bindings.first().layerId)
                                  .toObject();
    QVERIFY(!layer.value(QStringLiteral("notes")).toArray().first().toObject().contains(QStringLiteral("id")));
    Project restored;
    QVERIFY(Project::fromJson(json, restored));
    QCOMPARE(restored.toJson(), json);
    const QString originalId = restored.projectId;
    QJsonObject bad = json;
    bad.insert(QStringLiteral("schemaVersion"), 2);
    QVERIFY(!Project::fromJson(bad, restored));
    QCOMPARE(restored.projectId, originalId);
    bad = json;
    QJsonObject badLayers = bad.value(QStringLiteral("layers")).toObject();
    const QString layerId = badLayers.keys().first();
    QJsonObject badLayer = badLayers.value(layerId).toObject();
    badLayer.insert(QStringLiteral("notes"), QJsonArray{QJsonObject{{QStringLiteral("type"), 99}}});
    badLayers.insert(layerId, badLayer);
    bad.insert(QStringLiteral("layers"), badLayers);
    QVERIFY(!Project::fromJson(bad, restored));
    QCOMPARE(restored.projectId, originalId);
}

QTEST_APPLESS_MAIN(DocumentModelTests)
#include "document_model_tests.moc"

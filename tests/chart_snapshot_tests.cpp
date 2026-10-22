#include "controller/ChartController.h"
#include "document/DocumentModel.h"
#include "file/ChartIO.h"
#include <QtTest>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <future>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::declval<ChartController &>().chart()), const Chart *>);
static_assert(std::is_same_v<decltype(std::declval<ChartSnapshot &>().chart()), const Chart &>);
static_assert(std::is_same_v<decltype(std::declval<ChartSnapshot &>().version()), const ChartReadVersion &>);

namespace
{
Chart fixture(int count = 3)
{
    Chart chart;
    QVector<Note> notes;
    notes.reserve(count);
    for (int i = 0; i < count; ++i)
        notes.append(Note(i, 1, 3, i % 513));
    chart.setNotes(std::move(notes));
    chart.meta().title = QStringLiteral("Snapshot source");
    chart.meta().audioFile = QStringLiteral("song.ogg");
    chart.meta().backgroundFile = QStringLiteral("bg.png");
    chart.meta().offset = 23;
    chart.setAudioSourceFullPath(QStringLiteral("/capture/song.ogg"));
    return chart;
}

const ChartChangeSet statsDomains = ChartChangeType::Notes | ChartChangeType::Timing;
} // namespace

class ChartSnapshotTests : public QObject
{
    Q_OBJECT
private slots:
    void invalidTokensNeverMatch()
    {
        ChartSnapshot empty;
        ChartReadVersion invalid;
        QVERIFY(!empty.isValid());
        QVERIFY(!invalid.matches(invalid));
        ChartController controller;
        const auto fresh = controller.snapshot();
        QVERIFY(fresh.isValid());
        QCOMPARE(fresh.revision(), quint64(0));
        QVERIFY(controller.isCurrent(fresh.version()));
        QVERIFY(!controller.isCurrent(invalid));
        QVERIFY(!controller.isCurrent(invalid, statsDomains));
        QVERIFY(!fresh.version().matches(invalid));
    }

    void oldDataSurvivesEditsUndoAndControllerDestruction()
    {
        ChartSnapshot old;
        const Chart source = fixture();
        {
            ChartController controller;
            QVERIFY(controller.loadChartFromData(QStringLiteral("source.mc"), source));
            old = controller.snapshot();
            QCOMPARE(old.filePath(), QStringLiteral("source.mc"));
            const auto initialRevision = old.revision();
            Note moved = source.notes().first();
            moved.x = 512;
            controller.moveNote(source.notes().first(), moved);
            controller.addBpm(BpmEntry(4, 1, 3, 180));
            MetaData metadata = controller.chart()->meta();
            metadata.title = QStringLiteral("Changed");
            metadata.offset += 20;
            controller.setMetaData(metadata);
            QVERIFY(controller.revision() > initialRevision);
            QVERIFY(!controller.isCurrent(old.version()));
            QCOMPARE(old.chart().notes(), source.notes());
            QCOMPARE(old.chart().meta().title, source.meta().title);
            QCOMPARE(old.chart().meta().offset, source.meta().offset);
            QCOMPARE(old.chart().bpmList().size(), source.bpmList().size());
            QCOMPARE(old.chart().audioSourceFullPath(), source.audioSourceFullPath());

            controller.undo();
            controller.undo();
            controller.undo();
            QCOMPARE(CceDocument::encodeChart(*controller.chart()), CceDocument::encodeChart(source));
            QVERIFY(!controller.isCurrent(old.version(), statsDomains));
            const auto undone = controller.snapshot();
            controller.redo();
            QVERIFY(!controller.isCurrent(undone.version(), ChartChangeType::Notes));
            QCOMPARE(undone.chart().notes(), source.notes());
            QCOMPARE(old.revision(), initialRevision);
        }
        QVERIFY(old.isValid());
        QCOMPARE(CceDocument::encodeChart(old.chart()), CceDocument::encodeChart(source));
        QCOMPARE(old.chart().notes().first().id, source.notes().first().id);
    }

    void explicitEditableCopyIsDetached()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData({}, fixture()));
        const auto frozen = controller.snapshot();
        Chart editable = frozen.chart();
        editable.notes()[0].x = 511;
        editable.bpmList()[0].bpm = 250;
        editable.meta().title = QStringLiteral("Detached");
        editable.setAudioSourceFullPath(QStringLiteral("/detached/song.ogg"));
        QCOMPARE(frozen.chart().notes().first().x, 0);
        QCOMPARE(frozen.chart().meta().title, QStringLiteral("Snapshot source"));
        QCOMPARE(frozen.chart().bpmList().first().bpm, controller.chart()->bpmList().first().bpm);
        QCOMPARE(frozen.chart().audioSourceFullPath(), QStringLiteral("/capture/song.ogg"));
        QVERIFY(controller.isCurrent(frozen.version()));
        QVERIFY(!controller.canUndo());
    }

    void domainFiltersRejectOnlyRelevantEdits()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData({}, fixture()));
        auto version = controller.readVersion();
        auto metadata = controller.chart()->meta();
        metadata.title += QStringLiteral(" revised");
        controller.setMetaData(metadata);
        QVERIFY(!controller.isCurrent(version));
        QVERIFY(!controller.isCurrent(version, ChartChangeType::Metadata));
        QVERIFY(controller.isCurrent(version, statsDomains));
        QVERIFY(controller.isCurrent(version, ChartChangeType::Resources));

        version = controller.readVersion();
        metadata.offset += 10;
        controller.setMetaData(metadata);
        QVERIFY(!controller.isCurrent(version, statsDomains));
        QVERIFY(!controller.isCurrent(version, ChartChangeType::Metadata));
        QVERIFY(controller.isCurrent(version, ChartChangeType::Notes));

        version = controller.readVersion();
        metadata.audioFile = QStringLiteral("other.ogg");
        controller.setMetaData(metadata);
        QVERIFY(!controller.isCurrent(version, ChartChangeType::Resources));
        QVERIFY(!controller.isCurrent(version, ChartChangeType::Metadata));
        QVERIFY(controller.isCurrent(version, statsDomains));

        version = controller.readVersion();
        QVERIFY(controller.replaceBpmList(QStringLiteral("Timing"), {BpmEntry(0, 0, 1, 200)}));
        QVERIFY(!controller.isCurrent(version, statsDomains));
        QVERIFY(controller.isCurrent(version, ChartChangeType::Metadata));
        QVERIFY(!controller.isCurrent(controller.readVersion(), ChartChangeSet::fromInt(0x40)));
    }

    void sessionAndControllerSourcesNeverAlias()
    {
        ChartController first, second;
        const Chart chart = fixture();
        QVERIFY(first.loadChartFromData(QStringLiteral("same.mc"), chart));
        QVERIFY(second.loadChartFromData(QStringLiteral("same.mc"), chart));
        const auto old = first.snapshot();
        QCOMPARE(first.revision(), second.revision());
        QVERIFY(!second.isCurrent(old.version()));
        QVERIFY(!second.isCurrent(old.version(), statsDomains));
        QVERIFY(first.loadChartFromData(QStringLiteral("same.mc"), chart));
        QVERIFY(!first.isCurrent(old.version()));
        QVERIFY(!first.isCurrent(old.version(), ChartChangeType::Resources));
        QCOMPARE(old.filePath(), QStringLiteral("same.mc"));
    }

    void failedLoadAndNoopPreserveReadVersion()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData({}, fixture()));
        const auto old = controller.snapshot();
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(!controller.loadChart(temp.filePath(QStringLiteral("absent.mc"))));
        QVERIFY(controller.isCurrent(old.version()));
        controller.removeNotes({});
        controller.moveNotes({});
        controller.setMetaData(old.chart().meta());
        controller.pushUndoMarker(QStringLiteral("Auxiliary only"));
        QVERIFY(controller.isCurrent(old.version()));
        controller.undo();
        QVERIFY(controller.isCurrent(old.version()));
        QVERIFY(controller.applyDocumentMutation(QStringLiteral("Comment only"), old.chart(), [](bool) {}));
        QVERIFY(controller.isCurrent(old.version()));
        controller.undo();
        QVERIFY(controller.isCurrent(old.version()));
    }

    void versionAndDataArePublishedTogether()
    {
        ChartController controller;
        int notifications = 0;
        connect(&controller, &ChartController::chartChangeCommitted, this, [&](const ChartChange &change) {
            const auto frozen = controller.snapshot();
            QCOMPARE(frozen.revision(), change.revision);
            QVERIFY(controller.isCurrent(frozen.version()));
            QCOMPARE(CceDocument::encodeChart(frozen.chart()), CceDocument::encodeChart(*controller.chart()));
            ++notifications;
        });
        QVERIFY(controller.loadChartFromData({}, fixture()));
        controller.addNote(Note(9, 0, 1, 128));
        controller.undo();
        controller.redo();
        QCOMPARE(notifications, 4);
    }

    void documentTransactionsInvalidateEveryAffectedDomain()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData({}, fixture()));
        const auto old = controller.snapshot();
        Chart changed = old.chart();
        changed.addNote(Note(9, 0, 1, 256));
        changed.addBpm(BpmEntry(8, 0, 1, 175));
        changed.meta().backgroundFile = QStringLiteral("new.png");
        changed.setAudioSourceFullPath(QStringLiteral("/new/song.ogg"));
        int restored = 0;
        QVERIFY(controller.applyDocumentMutation(QStringLiteral("Review"), changed, [&](bool forward) {
            restored = forward ? 1 : -1;
        }));
        QCOMPARE(restored, 1);
        for (auto domain :
             {ChartChangeType::Notes, ChartChangeType::Timing, ChartChangeType::Metadata, ChartChangeType::Resources})
            QVERIFY(!controller.isCurrent(old.version(), domain));
        const auto after = controller.snapshot();
        controller.undo();
        QCOMPARE(restored, -1);
        QCOMPARE(CceDocument::encodeChart(*controller.chart()), CceDocument::encodeChart(old.chart()));
        QVERIFY(!controller.isCurrent(after.version()));
        QCOMPARE(after.chart().notes().size(), 4);
        controller.redo();
        QCOMPARE(restored, 1);
        QCOMPARE(CceDocument::encodeChart(*controller.chart()), CceDocument::encodeChart(after.chart()));
    }

    void snapshotAcquisitionSharesStorage_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("5k") << 5000;
        QTest::newRow("20k") << 20000;
        QTest::newRow("100k") << 100000;
    }

    void snapshotAcquisitionSharesStorage()
    {
        QFETCH(int, count);
        ChartController controller;
        QVERIFY(controller.loadChartFromData({}, fixture(count)));
        const Note *data = controller.chart()->notes().constData();
        const BpmEntry *timing = controller.chart()->bpmList().constData();
        QElapsedTimer elapsed;
        elapsed.start();
        for (int i = 0; i < 1000; ++i)
        {
            const auto frozen = controller.snapshot();
            QCOMPARE(frozen.chart().notes().constData(), data);
            QCOMPARE(frozen.chart().bpmList().constData(), timing);
            QCOMPARE(frozen.chart().notes().size(), count);
        }
        qInfo("%d notes: 1000 snapshots in %.3f ms", count, elapsed.nsecsElapsed() / 1e6);
        const auto old = controller.snapshot();
        Note changed = old.chart().notes().first();
        changed.x = 512;
        controller.moveNote(old.chart().notes().first(), changed);
        QCOMPARE(old.chart().notes().constData(), data);
        QCOMPARE(old.chart().notes().first().x, 0);
        QCOMPARE(controller.chart()->notes().first().x, 512);
    }

    void workerReadsFrozenDataWhileControllerChanges()
    {
        ChartController controller;
        QVERIFY(controller.loadChartFromData({}, fixture(20000)));
        const auto frozen = controller.snapshot();
        const auto expected = CceDocument::encodeChart(frozen.chart());
        auto worker = std::async(std::launch::async, [snapshot = frozen] {
            return CceDocument::encodeChart(snapshot.chart());
        });
        controller.removeNotes({controller.chart()->notes().first()});
        MetaData metadata = controller.chart()->meta();
        metadata.title = QStringLiteral("Live edited");
        controller.setMetaData(metadata);
        QCOMPARE(worker.get(), expected);
        QCOMPARE(frozen.chart().notes().size(), 20000);
        QCOMPARE(controller.chart()->notes().size(), 19999);
        QVERIFY(!controller.isCurrent(frozen.version(), statsDomains));
    }

    void legacyMcDoesNotAcquireSnapshotOrIdentityFields()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        ChartController controller;
        Chart source = fixture();
        source.meta().audioFile.clear();
        source.meta().backgroundFile.clear();
        source.meta().offset = 0;
        source.setAudioSourceFullPath({});
        QVERIFY(controller.loadChartFromData({}, source));
        const auto beforePath = temp.filePath(QStringLiteral("before.mc"));
        const auto afterPath = temp.filePath(QStringLiteral("after.mc"));
        QVERIFY(controller.saveChart(beforePath));
        const auto snapshot = controller.snapshot();
        QVERIFY(controller.saveChart(afterPath));
        QFile beforeFile(beforePath), afterFile(afterPath);
        QVERIFY(beforeFile.open(QIODevice::ReadOnly));
        QVERIFY(afterFile.open(QIODevice::ReadOnly));
        auto before = QJsonDocument::fromJson(beforeFile.readAll()).object();
        auto after = QJsonDocument::fromJson(afterFile.readAll()).object();
        auto beforeMeta = before.value("meta").toObject();
        auto afterMeta = after.value("meta").toObject();
        beforeMeta.remove("time");
        afterMeta.remove("time");
        before["meta"] = beforeMeta;
        after["meta"] = afterMeta;
        QCOMPARE(after, before);
        for (const auto note : after.value("note").toArray())
            QVERIFY(!note.toObject().contains("id"));
        const auto json = QJsonDocument(after).toJson();
        QVERIFY(!json.contains("readVersion"));
        QVERIFY(!json.contains(snapshot.chart().notes().first().id.toUtf8()));
        Chart reloaded;
        QVERIFY(ChartIO::load(afterPath, reloaded));
        QCOMPARE(CceDocument::encodeChart(reloaded), CceDocument::encodeChart(source));
        QVERIFY(!controller.canUndo());
    }
};

QTEST_GUILESS_MAIN(ChartSnapshotTests)
#include "chart_snapshot_tests.moc"

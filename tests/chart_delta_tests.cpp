#include "document/ChartDelta.h"
#include "controller/ChartController.h"
#include <QDataStream>
#include "document/DocumentModel.h"
#include <QtTest>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

using CceDocument::ChartDelta;

namespace
{
Chart fixture(int count = 10)
{
    Chart c;
    QVector<Note> notes;
    for (int i = 0; i < count; ++i)
        notes.append(Note(i, 1, 3, i % 513));
    c.setNotes(std::move(notes));
    c.meta().title = QStringLiteral("Source");
    c.meta().audioFile = QStringLiteral("C:/local/audio.ogg");
    c.meta().backgroundFile = QStringLiteral("images/bg.png");
    c.meta().offset = 17;
    c.setAudioSourceFullPath(QStringLiteral("C:/local/audio.ogg"));
    return c;
}

// Include raw unused fields and runtime handles: Note::operator== intentionally
// omits several of them, so it cannot prove exact in-session Undo equivalence.
QByteArray rawData(const Chart &chart, bool handles)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << qint64(chart.notes().size());
    for (const auto &n : chart.notes())
    {
        if (handles)
            stream << n.id;
        stream << n.beatNum << n.numerator << n.denominator << int(n.type) << n.x << n.isRain << n.endBeatNum
               << n.endNumerator << n.endDenominator << n.sound << n.vol << n.offset;
    }
    stream << qint64(chart.bpmList().size());
    for (const auto &b : chart.bpmList())
        stream << b.beatNum << b.numerator << b.denominator << b.bpm;
    const auto &m = chart.meta();
    stream << m.title << m.titleOrg << m.artist << m.artistOrg << m.difficulty << m.chartAuthor << m.audioFile
           << m.backgroundFile << m.previewTime << m.firstBpm << m.offset << m.speed << chart.audioSourceFullPath();
    return bytes;
}

bool exact(const Chart &a, const Chart &b, bool handles = true)
{
    return rawData(a, handles) == rawData(b, handles);
}
} // namespace

class ChartDeltaTests : public QObject
{
    Q_OBJECT
private slots:
    void allDomainsRoundTripAndInverse()
    {
        const auto before = fixture();
        auto after = before;
        after.notes().removeAt(3);
        after.notes()[0] = Note(20, 2, 6, 30);
        after.notes().append(Note(21, 0, 1, 22, 1, 3, 256));
        after.notes().append(Note(24, 0, 1, QStringLiteral("C:/local/hit.wav"), 72, -11));
        after.sortNotes();
        after.bpmList() = {BpmEntry(0, 2, 6, 137.25), BpmEntry(4, 1, 3, 198.125)};
        after.meta().title = QStringLiteral("After");
        after.meta().titleOrg = QStringLiteral("Original title");
        after.meta().artist = QStringLiteral("Artist");
        after.meta().artistOrg = QStringLiteral("Original artist");
        after.meta().difficulty = QStringLiteral("Expert");
        after.meta().chartAuthor = QStringLiteral("Local author");
        after.meta().backgroundFile = QStringLiteral("D:/other/bg.png");
        after.meta().previewTime = 8700;
        after.meta().speed = 7;
        after.meta().offset = -81;
        after.meta().audioFile = QStringLiteral("D:/other/track.ogg");
        after.meta().firstBpm += 0.0000000001;
        after.setAudioSourceFullPath(QStringLiteral("D:/other/track.ogg"));
        ChartDelta delta;
        QString error;
        QVERIFY2(ChartDelta::between(before, after, delta, &error), qPrintable(error));
        const auto all =
            ChartChangeType::Notes | ChartChangeType::Timing | ChartChangeType::Metadata | ChartChangeType::Resources;
        QCOMPARE(delta.changes(), all);
        Chart applied;
        QVERIFY2(delta.apply(before, applied, &error), qPrintable(error));
        QVERIFY(exact(applied, after));
        QVERIFY(delta.inverted().apply(applied, applied, &error));
        QVERIFY(exact(applied, before));
        ChartDelta decoded;
        QVERIFY2(ChartDelta::fromJson(delta.toJson(), decoded, &error), qPrintable(error));
        ChartDelta bound;
        QVERIFY(decoded.bindTo(before, bound, &error));
        QVERIFY(bound.apply(before, applied, &error));
        QVERIFY(exact(applied, after, false));
        QCOMPARE(applied.meta().audioFile, after.meta().audioFile);
        QCOMPARE(applied.bpmList().first().numerator, 2);
        QCOMPARE(applied.bpmList().first().denominator, 6);
        QVERIFY(bound.inverted().apply(applied, applied, &error));
        QVERIFY(exact(applied, before));
        ChartDelta inverseDecoded;
        QVERIFY(ChartDelta::fromJson(delta.inverted().toJson(), inverseDecoded, &error));
        QVERIFY(inverseDecoded.bindTo(after, bound, &error));
        QVERIFY(bound.apply(after, applied, &error));
        QVERIFY(exact(applied, before, false));
    }

    void emptyWholeReplacementAndNoop()
    {
        const auto full = fixture();
        auto empty = full;
        empty.clearNotes();
        for (bool removal : {false, true})
        {
            const auto &before = removal ? full : empty;
            const auto &after = removal ? empty : full;
            ChartDelta delta, decoded, bound;
            QVERIFY(ChartDelta::between(before, after, delta));
            Chart result;
            QVERIFY(delta.apply(before, result));
            QVERIFY(exact(result, after));
            QVERIFY(delta.inverted().apply(result, result));
            QVERIFY(exact(result, before));
            QVERIFY(ChartDelta::fromJson(delta.toJson(), decoded));
            QVERIFY(decoded.bindTo(before, bound));
            QVERIFY(bound.apply(before, result));
            QVERIFY(exact(result, after, false));
            QVERIFY(bound.inverted().apply(result, result));
            QVERIFY(exact(result, before));
        }
        ChartDelta noop, decoded;
        QVERIFY(ChartDelta::between(full, full, noop));
        QVERIFY(!noop.changes());
        QCOMPARE(noop.retainedNoteCount(), 0);
        QVERIFY(ChartDelta::fromJson(noop.toJson(), decoded));
        Chart result;
        QVERIFY(decoded.apply(full, result));
        QCOMPARE(result.notes().constData(), full.notes().constData());
    }

    void serializationUsesOnlyPositionAndDataReferences()
    {
        const auto before = fixture();
        auto after = before;
        Note moved = after.notes().takeFirst();
        moved.beatNum = 999;
        after.addNote(moved);
        ChartDelta delta, decoded, bound;
        QVERIFY(ChartDelta::between(before, after, delta));
        const auto bytes = QJsonDocument(delta.toJson()).toJson(QJsonDocument::Compact);
        for (const auto &note : before.notes())
            QVERIFY(!bytes.contains(note.id.toUtf8()));
        QVERIFY(bytes.contains("beforeIndex"));
        QVERIFY(ChartDelta::fromJson(QJsonDocument::fromJson(bytes).object(), decoded));
        Chart fresh = before;
        for (auto &note : fresh.notes())
            note.id = Note::generateId();
        QVERIFY(decoded.bindTo(fresh, bound));
        Chart result;
        QVERIFY(bound.apply(fresh, result));
        QCOMPARE(result.notes().last().id, fresh.notes().first().id);
        QVERIFY(bound.inverted().apply(result, result));
        QVERIFY(exact(result, fresh));
    }

    void nullableStringsSurviveSerializationAndUndo()
    {
        auto before = fixture();
        before.notes()[0].sound = QString();
        before.meta().titleOrg = QString();
        before.setAudioSourceFullPath(QString());
        auto after = before;
        after.notes()[0].sound = QStringLiteral("");
        after.meta().titleOrg = QStringLiteral("");
        after.setAudioSourceFullPath(QStringLiteral(""));
        ChartDelta delta, decoded, bound;
        QVERIFY(ChartDelta::between(before, after, delta));
        QVERIFY(delta.changes().testFlag(ChartChangeType::Notes));
        QVERIFY(delta.changes().testFlag(ChartChangeType::Resources));
        QVERIFY(ChartDelta::fromJson(delta.toJson(), decoded));
        QVERIFY(decoded.bindTo(before, bound));
        Chart result;
        QVERIFY(bound.apply(before, result));
        QVERIFY(exact(result, after));
        QVERIFY(bound.inverted().apply(result, result));
        QVERIFY(exact(result, before));
    }

    void duplicatesEmptyHandlesTiesAndUnusedFieldsAreExact()
    {
        auto before = fixture(0);
        Note n(2, 2, 6, 128);
        n.id.clear();
        before.notes() = {n, n, n};
        before.notes()[1].id = QStringLiteral("duplicate");
        before.notes()[2].id = QStringLiteral("duplicate");
        auto after = before;
        after.notes().removeAt(1);
        after.notes()[0].endBeatNum = 444; // unused on Normal, must survive Undo
        after.notes()[0].sound = QStringLiteral("unused");
        after.notes()[0].vol = 18;
        after.notes().append(n);
        std::reverse(after.notes().begin(), after.notes().end());
        ChartDelta delta;
        QVERIFY(ChartDelta::between(before, after, delta));
        Chart result;
        QVERIFY(delta.apply(before, result));
        QVERIFY(exact(result, after));
        QVERIFY(delta.inverted().apply(result, result));
        QVERIFY(exact(result, before));
        ChartDelta decoded, bound;
        QVERIFY(ChartDelta::fromJson(delta.toJson(), decoded));
        QVERIFY(decoded.bindTo(before, bound));
        QVERIFY(bound.apply(before, result));
        QVERIFY(exact(result, after, false));
        QVERIFY(bound.inverted().apply(result, result));
        QVERIFY(exact(result, before));
    }

    void malformedInputDoesNotReplaceExistingDelta_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto &kind : {"version", "unknown", "negative", "fractional", "overlap", "wrong-count", "bad-note",
                                 "note-id", "bad-link", "duplicate-link", "timing", "metadata", "null-patch"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }

    void malformedInputDoesNotReplaceExistingDelta()
    {
        QFETCH(QString, kind);
        const auto before = fixture();
        auto after = before;
        after.notes()[0].x = 512;
        after.meta().title = QStringLiteral("changed");
        ChartDelta valid;
        QVERIFY(ChartDelta::between(before, after, valid));
        auto json = valid.toJson();
        auto splices = json.value("notes").toArray();
        auto splice = splices[0].toObject();
        if (kind == "version")
            json["schemaVersion"] = 2;
        else if (kind == "unknown")
            json["uuid"] = "bad";
        else if (kind == "negative")
        {
            splice["beforeIndex"] = -1;
            splices[0] = splice;
            json["notes"] = splices;
        }
        else if (kind == "fractional")
            json["beforeNoteCount"] = 10.5;
        else if (kind == "overlap")
        {
            splices.append(splice);
            json["notes"] = splices;
        }
        else if (kind == "wrong-count")
            json["afterNoteCount"] = 11;
        else if (kind == "bad-note" || kind == "note-id")
        {
            auto images = splice["after"].toArray();
            auto image = images[0].toObject();
            image[kind == "bad-note" ? "denominator" : "id"] = kind == "bad-note" ? QJsonValue(0) : QJsonValue("bad");
            images[0] = image;
            splice["after"] = images;
            splices[0] = splice;
            json["notes"] = splices;
        }
        else if (kind == "bad-link")
            json["handleLinks"] = QJsonArray{QJsonObject{{"beforeIndex", 8}, {"afterIndex", 0}}};
        else if (kind == "duplicate-link")
        {
            auto links = json["handleLinks"].toArray();
            links.append(links[0]);
            json["handleLinks"] = links;
        }
        else if (kind == "timing")
            json["timing"] = QJsonObject{{"before", QJsonArray{}}, {"after", QJsonArray{3}}};
        else if (kind == "metadata")
        {
            auto patch = json["metadata"].toObject();
            auto meta = patch["after"].toObject();
            meta["speed"] = 1.5;
            patch["after"] = meta;
            json["metadata"] = patch;
        }
        else if (kind == "null-patch")
            json["metadata"] = QJsonValue::Null;
        ChartDelta retained = valid;
        QString error;
        QVERIFY(!ChartDelta::fromJson(json, retained, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(retained.toJson(), valid.toJson());
    }

    void failedPreconditionsNeverPartiallyApply()
    {
        const auto before = fixture();
        auto after = before;
        after.notes()[0].x = 512;
        after.meta().title = QStringLiteral("changed");
        after.bpmList()[0].bpm = 155;
        after.setAudioSourceFullPath(QStringLiteral("D:/new.ogg"));
        ChartDelta delta;
        QVERIFY(ChartDelta::between(before, after, delta));
        for (int domain = 0; domain < 4; ++domain)
        {
            auto stale = before;
            if (domain == 0)
                stale.notes()[0].x = 20;
            if (domain == 1)
                stale.meta().title = QStringLiteral("concurrent");
            if (domain == 2)
                stale.bpmList()[0].bpm = 222;
            if (domain == 3)
                stale.setAudioSourceFullPath(QStringLiteral("E:/concurrent.ogg"));
            const auto original = stale;
            QString error;
            QVERIFY(!delta.apply(stale, stale, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(exact(stale, original));
            ChartDelta bound;
            QVERIFY(!delta.bindTo(stale, bound));
        }
    }

    void controllerTransactionIsAtomicAndVersionGuarded()
    {
        ChartController controller, other;
        const auto before = fixture();
        QVERIFY(controller.loadChartFromData({}, before));
        QVERIFY(other.loadChartFromData({}, before));
        const auto captured = controller.snapshot();
        auto after = before;
        after.notes()[0].x = 512;
        after.bpmList()[0].bpm = 170;
        after.meta().offset = 34;
        after.meta().backgroundFile = QStringLiteral("D:/new.png");
        ChartDelta delta, decoded;
        QVERIFY(ChartDelta::between(before, after, delta));
        QVERIFY(ChartDelta::fromJson(delta.toJson(), decoded));
        QString error;
        QVERIFY(!controller.applyTransaction("foreign", other.readVersion(), decoded, &error));
        QVERIFY(!controller.canUndo());
        controller.setReadOnly(true);
        QVERIFY(!controller.applyTransaction("readonly", captured.version(), decoded, &error));
        controller.setReadOnly(false);
        bool adding = true;
        int affected = -1;
        controller.setEditGuard([&](const QVector<Note> &notes, bool adds) {
            affected = int(notes.size());
            adding = adds;
            return true;
        });
        QSignalSpy changes(&controller, &ChartController::chartChangeCommitted);
        QVERIFY2(controller.applyTransaction("transaction", captured.version(), decoded, &error), qPrintable(error));
        QCOMPARE(affected, 1);
        QVERIFY(!adding);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(controller.revision(), captured.revision() + 1);
        QVERIFY(exact(*controller.chart(), after));
        QVERIFY(!controller.applyTransaction("stale", captured.version(), decoded, &error));
        controller.undo();
        QVERIFY(exact(*controller.chart(), before));
        QVERIFY(!controller.canUndo());
        controller.redo();
        QVERIFY(exact(*controller.chart(), after));
        QCOMPARE(changes.size(), 3);
    }

    void controllerRejectsUnrelatedEditsAndGuardReentrancy()
    {
        ChartController controller;
        const auto before = fixture();
        QVERIFY(controller.loadChartFromData({}, before));
        auto after = before;
        after.notes()[0].x = 511;
        ChartDelta delta;
        QVERIFY(ChartDelta::between(before, after, delta));
        auto version = controller.readVersion();
        auto metadata = before.meta();
        metadata.artist = QStringLiteral("Unrelated edit");
        controller.setMetaData(metadata);
        QVERIFY(!controller.applyTransaction("old", version, delta));
        QCOMPARE(controller.chart()->notes()[0].x, before.notes()[0].x);
        controller.undo();
        QVERIFY(!controller.canUndo());
        version = controller.readVersion();
        bool entered = false;
        controller.setEditGuard([&](const QVector<Note> &, bool) {
            if (!entered)
            {
                entered = true;
                controller.setMetaData(metadata);
            }
            return true;
        });
        QVERIFY(!controller.applyTransaction("guard changed source", version, delta));
        QCOMPARE(controller.nextUndoActionText(), QStringLiteral("Edit Meta"));
        QCOMPARE(controller.chart()->notes()[0].x, before.notes()[0].x);
        controller.undo();
        QVERIFY(!controller.canUndo());
        QVERIFY(exact(*controller.chart(), before));
    }

    void documentAndOpaqueCommandsKeepLegacyHistoryBehavior()
    {
        ChartController controller;
        const auto before = fixture();
        QVERIFY(controller.loadChartFromData({}, before));
        auto after = before;
        after.notes().removeAt(1);
        after.meta().title = QStringLiteral("after");
        int aux = 0;
        QVERIFY(controller.applyDocumentMutation("document", after, [&](bool forward) {
            aux = forward ? 1 : -1;
        }));
        QCOMPARE(aux, 1);
        QVERIFY(exact(*controller.chart(), after));
        controller.undo();
        QCOMPARE(aux, -1);
        QVERIFY(exact(*controller.chart(), before));
        controller.redo();
        QCOMPARE(aux, 1);
        QVERIFY(exact(*controller.chart(), after));
        QSignalSpy notes(&controller, &ChartController::notesChanged),
            timing(&controller, &ChartController::bpmListChanged), meta(&controller, &ChartController::metaDataChanged);
        auto opaque = after;
        opaque.notes()[0].x = 511;
        QVERIFY(controller.applyExternalChartMutation("opaque", opaque));
        QCOMPARE(notes.size(), 1);
        QCOMPARE(timing.size(), 1);
        QCOMPARE(meta.size(), 1);
        controller.undo();
        QVERIFY(exact(*controller.chart(), after));
        controller.redo();
        QVERIFY(exact(*controller.chart(), opaque));
        auto invalid = opaque;
        invalid.notes()[0].denominator = 0;
        QVERIFY(!controller.applyDocumentMutation("bad", invalid, [&](bool) {
            aux = 99;
        }));
        QCOMPARE(aux, 1);
        QVERIFY(exact(*controller.chart(), opaque));
    }

    void failedUndoKeepsHistoryAndAuxiliaryState()
    {
        ChartController controller;
        const auto before = fixture();
        QVERIFY(controller.loadChartFromData({}, before));
        auto after = before;
        after.notes()[0].x = 511;
        int auxiliary = 0;
        QVERIFY(controller.applyDocumentMutation("guarded", after, [&](bool forward) {
            auxiliary = forward ? 1 : -1;
        }));
        // Fault injection only: production code has no mutable Chart accessor.
        auto *injected = const_cast<Chart *>(controller.chart());
        injected->notes()[0].x = 128;
        const auto corrupted = *controller.chart();
        const auto revision = controller.revision();
        QSignalSpy errors(&controller, &ChartController::errorOccurred);
        controller.undo();
        QCOMPARE(errors.size(), 1);
        QCOMPARE(auxiliary, 1);
        QCOMPARE(controller.revision(), revision);
        QVERIFY(controller.canUndo());
        QVERIFY(!controller.canRedo());
        QVERIFY(exact(*controller.chart(), corrupted));
        *injected = after;
        controller.undo();
        QCOMPARE(auxiliary, -1);
        QVERIFY(exact(*controller.chart(), before));
        QVERIFY(controller.canRedo());
        injected->notes()[0].x = 128;
        controller.redo();
        QCOMPARE(errors.size(), 2);
        QCOMPARE(auxiliary, -1);
        QVERIFY(controller.canRedo());
        *injected = before;
        controller.redo();
        QCOMPARE(auxiliary, 1);
        QVERIFY(exact(*controller.chart(), after));
    }

    void randomEditSequencesRetainExactInverse()
    {
        QRandomGenerator random(47821);
        auto before = fixture(30);
        for (int iteration = 0; iteration < 200; ++iteration)
        {
            auto after = before;
            for (int op = 0; op < 6; ++op)
            {
                if (after.notes().isEmpty() || random.bounded(4) == 0)
                    after.addNote(Note(int(random.bounded(40)), 1, 3, int(random.bounded(513))));
                else
                {
                    const int index = int(random.bounded(quint32(after.notes().size())));
                    switch (random.bounded(4))
                    {
                        case 0:
                            after.notes().removeAt(index);
                            break;
                        case 1:
                            after.notes()[index].x = int(random.bounded(513));
                            break;
                        case 2:
                            after.notes().append(after.notes()[index]);
                            break;
                        default:
                            after.notes()[index].beatNum = int(random.bounded(40));
                            break;
                    }
                }
            }
            after.sortNotes();
            ChartDelta delta, decoded, bound;
            QVERIFY(ChartDelta::between(before, after, delta));
            Chart result;
            QVERIFY(delta.apply(before, result));
            QVERIFY(exact(result, after));
            QVERIFY(delta.inverted().apply(result, result));
            QVERIFY(exact(result, before));
            QVERIFY(ChartDelta::fromJson(delta.toJson(), decoded));
            QVERIFY(decoded.bindTo(before, bound));
            QVERIFY(bound.apply(before, result));
            QVERIFY(exact(result, after, false));
            QVERIFY(bound.inverted().apply(result, result));
            QVERIFY(exact(result, before));
            before = std::move(after);
        }
    }

    void sparseEditsAndHistoryRetainChangedNotes_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("5k") << 5000;
        QTest::newRow("20k") << 20000;
        QTest::newRow("100k") << 100000;
    }

    void projectDecodeKeepsDataDeltaSparse_data()
    {
        sparseEditsAndHistoryRetainChangedNotes_data();
    }

    void projectDecodeKeepsDataDeltaSparse()
    {
        QFETCH(int, count);
        auto chart = fixture(count);
        chart.meta().audioFile = QStringLiteral("audio.ogg");
        auto project = CceDocument::Project::fromChart(chart);
        const auto before = project.compose(project.activeDifficultyId);
        project.difficulties[project.activeDifficultyId].metadata.title += QStringLiteral(" revised");
        CceDocument::Project decodedProject;
        QString error;
        QVERIFY2(CceDocument::Project::fromJson(project.toJson(), decodedProject, &error), qPrintable(error));
        auto after = decodedProject.compose(decodedProject.activeDifficultyId);
        QVERIFY(after.notes()[0].id != before.notes()[0].id);
        after.notes()[0].x = 512;
        ChartDelta delta, decoded, bound;
        QVERIFY(ChartDelta::between(before, after, delta));
        QCOMPARE(delta.retainedNoteCount(), 2);
        QCOMPARE(delta.retainedHandleCount(), count - 1);
        const auto json = delta.toJson();
        QVERIFY(QJsonDocument(json).toJson(QJsonDocument::Compact).size() < 5000);
        Chart result;
        QVERIFY(delta.apply(before, result));
        QVERIFY(exact(result, after));
        QVERIFY(delta.inverted().apply(result, result));
        QVERIFY(exact(result, before));
        QVERIFY(ChartDelta::fromJson(json, decoded));
        QCOMPARE(decoded.retainedHandleCount(), 0);
        QVERIFY(decoded.bindTo(before, bound));
        QVERIFY(bound.apply(before, result));
        QVERIFY(exact(result, after, false));
        QVERIFY(bound.inverted().apply(result, result));
        QVERIFY(exact(result, before));
        auto identitiesOnly = before;
        for (auto &note : identitiesOnly.notes())
            note.id = Note::generateId();
        QVERIFY(ChartDelta::between(before, identitiesOnly, delta));
        QCOMPARE(delta.retainedNoteCount(), 0);
        QCOMPARE(delta.retainedHandleCount(), count);
        QVERIFY(delta.apply(before, result));
        QVERIFY(exact(result, identitiesOnly));
        QVERIFY(delta.inverted().apply(result, result));
        QVERIFY(exact(result, before));
        QVERIFY(ChartDelta::fromJson(delta.toJson(), decoded));
        QVERIFY(!decoded.changes());
    }

    void sparseEditsAndHistoryRetainChangedNotes()
    {
        QFETCH(int, count);
        auto chart = fixture(count);
        QVector<ChartDelta> history;
        qsizetype retained = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        for (int operation = 0; operation < 16; ++operation)
        {
            auto next = chart;
            Note moved = next.notes().takeFirst();
            moved.beatNum += count + operation;
            next.addNote(moved);
            ChartDelta delta;
            QVERIFY(ChartDelta::between(chart, next, delta));
            QCOMPARE(delta.retainedNoteCount(), 2);
            QCOMPARE(delta.retainedHandleCount(), 0);
            QVERIFY(QJsonDocument(delta.toJson()).toJson(QJsonDocument::Compact).size() < 1800);
            retained += delta.retainedNoteCount();
            history.append(delta);
            QVERIFY(delta.apply(chart, chart));
            QVERIFY(exact(chart, next));
        }
        QCOMPARE(retained, 32);
        qInfo("%d notes: 16 long-distance edits %.2f ms, %lld retained images", count, elapsed.nsecsElapsed() / 1e6,
              qint64(retained));
        for (auto it = history.crbegin(); it != history.crend(); ++it)
            QVERIFY(it->inverted().apply(chart, chart));
        QCOMPARE(chart.notes().size(), count);
        QCOMPARE(chart.notes().first().beatNum, 0);
        auto metaOnly = chart;
        metaOnly.meta().title += QStringLiteral(" revised");
        ChartDelta metadata;
        QVERIFY(ChartDelta::between(chart, metaOnly, metadata));
        QCOMPARE(metadata.retainedNoteCount(), 0);
        Chart result;
        QVERIFY(metadata.apply(chart, result));
        QCOMPARE(result.notes().constData(), chart.notes().constData());
    }
};

QTEST_GUILESS_MAIN(ChartDeltaTests)
#include "chart_delta_tests.moc"

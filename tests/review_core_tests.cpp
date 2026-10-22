#include "../src/review/ReviewCore.h"
#include "document/DocumentModel.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>
#include <QCryptographicHash>
#include <algorithm>

using namespace CceReview;

namespace
{
const QString LayerId = "10000000-0000-4000-8000-000000000001";
const QString DifficultyId = "10000000-0000-4000-8000-000000000002";
const QString ProjectId = "10000000-0000-4000-8000-000000000003";
QJsonObject note(int beat, int n, int d, int x = 128)
{
    return {{"beat", QJsonArray{beat, n, d}}, {"x", x}, {"type", 0}};
}
QJsonObject snap(QJsonArray notes, QJsonArray timing = {}, QJsonObject metadata = {})
{
    Chart chart;
    chart.meta().difficulty = "Test";
    chart.addBpm(BpmEntry(0, 0, 1, 120));
    const auto model = CceDocument::Project::fromChart(chart).toJson();
    const auto difficulties = model.value("difficulties").toObject();
    auto defaults = difficulties.constBegin().value().toObject().value("metadata").toObject();
    for (auto it = metadata.begin(); it != metadata.end(); ++it)
        defaults[it.key()] = it.value();
    if (timing.isEmpty())
        timing = QJsonArray{QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 120}}};
    QJsonObject layer{{"layerId", LayerId}, {"name", "main"}, {"notes", notes}};
    QJsonObject binding{{"layerId", LayerId},    {"visible", true}, {"locked", false},
                        {"exportEnabled", true}, {"opacity", 1.0},  {"order", 0}};
    QJsonObject difficulty{{"difficultyId", DifficultyId},
                           {"name", "Test"},
                           {"metadata", defaults},
                           {"timing", timing},
                           {"oggHash", ""},
                           {"bindings", QJsonArray{binding}},
                           {"references", QJsonArray{}},
                           {"exportedMetadataFields", QJsonArray{}}};
    return {{"schemaVersion", 1},
            {"projectId", ProjectId},
            {"activeDifficultyId", DifficultyId},
            {"layers", QJsonObject{{LayerId, layer}}},
            {"difficulties", QJsonObject{{DifficultyId, difficulty}}}};
}
QVector<Change> changesFrom(const QJsonArray &array)
{
    QVector<Change> out;
    for (const auto &v : array)
    {
        Change c;
        QString err;
        if (changeFromJson(v.toObject(), c, &err))
            out.append(c);
    }
    return out;
}
} // namespace

class ReviewCoreTests final : public QObject
{
    Q_OBJECT
private slots:
    void dependenciesRequireExplicitGroupAndRoundTrip()
    {
        const auto base = snap({note(1, 0, 1)});
        const auto proposal =
            snap({note(2, 0, 1)}, {QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 135}}}, {{"offset", 10}});
        auto changes = diff(base, proposal);
        QVERIFY(changes.size() >= 4);
        QString group;
        QSet<QString> all;
        for (const auto &change : changes)
        {
            QVERIFY(!change.dependencyGroupId.isEmpty());
            if (group.isEmpty())
                group = change.dependencyGroupId;
            QCOMPARE(change.dependencyGroupId, group);
            Change decoded;
            QVERIFY(changeFromJson(changeToJson(change), decoded));
            QCOMPARE(decoded.dependencyGroupId, group);
            all.insert(change.changeId);
        }
        const QSet<QString> selected{changes.first().changeId};
        QCOMPARE(dependencySelection(changes, selected), all);
        const auto partial = merge(base, base, changes, selected);
        QVERIFY(!partial.committed);
        QCOMPARE(partial.result, base);
        QVERIFY(merge(base, base, changes, all).committed);
        auto legacy = changeToJson(changes.first());
        legacy.remove("dependencyGroupId");
        legacy.remove("dependsOn");
        Change decoded;
        QVERIFY(changeFromJson(legacy, decoded));
        QVERIFY(decoded.dependencyGroupId.isEmpty());
    }
    void mergingBranchesRebuildsDependencies()
    {
        const auto base = snap({note(1, 0, 1)});
        const auto root = createRevision(base, base, {}, "Branches");
        auto left = root, right = root;
        QString error;
        QVERIFY(updateRevision(left, snap({note(1, 0, 1)}, {}, {{"offset", 10}}), {}, {}, &error));
        QVERIFY(updateRevision(right, snap({note(1, 0, 1), note(2, 0, 1)}), {}, {}, &error));
        const auto merged = mergeRevisions(left, right, nullptr, &error);
        QVERIFY2(!merged.isEmpty(), qPrintable(error));
        const auto changes = changesFrom(merged.value("changes").toArray());
        QCOMPARE(changes.size(), 2);
        QVERIFY(!changes[0].dependencyGroupId.isEmpty());
        QCOMPARE(changes[0].dependencyGroupId, changes[1].dependencyGroupId);
        QVERIFY(!merge(base, base, changes, {changes[0].changeId}).committed);
    }
    void canonicalBeatAndDuplicateMultiset()
    {
        const auto a = snap(QJsonArray{note(8, 2, 4), note(8, 2, 4)});
        const auto equivalent = snap(QJsonArray{note(8, 1, 2), note(8, 1, 2)});
        QCOMPARE(diff(a, equivalent).size(), 0);
        auto withExtras = a, changedExtras = equivalent;
        withExtras.insert("extra", QJsonObject{{"opaque", 1}});
        changedExtras.insert("extra", QJsonObject{{"opaque", 2}});
        withExtras.insert("effects", QJsonArray{1});
        changedExtras.insert("effects", QJsonArray{2});
        QCOMPARE(diff(withExtras, changedExtras).size(), 0);
        QCOMPARE(canonicalHash(withExtras), canonicalHash(changedExtras));
        const auto oneLess = snap(QJsonArray{note(8, 1, 2)});
        const auto cs = diff(a, oneLess);
        QCOMPARE(cs.size(), 1);
        QCOMPARE(cs[0].kind, QString("Remove"));
        const auto merged = merge(a, a, cs);
        QVERIFY(merged.committed);
        QCOMPARE(merged.result, snap(QJsonArray{note(8, 2, 4)}));

        // An unrelated identical addition must not be consumed twice or cause an extra deletion.
        const auto current = snap(QJsonArray{note(8, 1, 2), note(8, 1, 2), note(8, 1, 2)});
        const auto againstConcurrent = merge(a, current, cs);
        QVERIFY(againstConcurrent.committed);
        QCOMPARE(againstConcurrent.result, snap(QJsonArray{note(8, 1, 2), note(8, 1, 2)}));

        const auto twoAdds = diff(snap({}), snap(QJsonArray{note(5, 0, 1), note(5, 0, 1)}));
        QCOMPARE(twoAdds.size(), 2);
        const auto duplicateAdds = merge(snap({}), snap({}), twoAdds);
        QVERIFY(duplicateAdds.committed);
        QCOMPARE(duplicateAdds.result, snap(QJsonArray{note(5, 0, 1), note(5, 0, 1)}));

        const auto oneNote = snap(QJsonArray{note(9, 0, 1, 20)});
        const auto removeNote = diff(oneNote, snap({}));
        const auto concurrentModify = snap(QJsonArray{note(9, 0, 1, 40)});
        const auto ambiguousRemove = merge(oneNote, concurrentModify, removeNote);
        QVERIFY(!ambiguousRemove.committed);
        QVERIFY(ambiguousRemove.hasConflicts());
    }
    void explicitMoveOnly()
    {
        const auto a = snap(QJsonArray{note(1, 0, 1, 20)});
        const auto b = snap(QJsonArray{note(1, 0, 1, 30)});
        const auto plain = diff(a, b);
        QCOMPARE(plain.size(), 2); // no inferred identity for delete plus add
        Change intent;
        intent.changeId = "move-1";
        intent.path = "/layers/" + LayerId + "/notes";
        intent.before = note(1, 0, 1, 20);
        intent.after = note(1, 0, 1, 30);
        intent.moveIntent = true;
        const auto linked = diff(a, b, {intent});
        QCOMPARE(linked.size(), 1);
        QCOMPARE(linked[0].kind, QString("Move"));
        const auto applied = merge(a, a, linked);
        QVERIFY(applied.committed);
        QCOMPARE(applied.result, b);
        const auto alreadyMoved = merge(a, b, linked);
        QVERIFY(alreadyMoved.committed);
        QCOMPARE(alreadyMoved.items[0].state, QString("AlreadySatisfied"));
        const auto ambiguousMove = merge(a, snap({}), linked);
        QVERIFY(!ambiguousMove.committed);
        QVERIFY(ambiguousMove.hasConflicts());
        const auto currentNote = note(1, 0, 1, 40);
        const auto current = snap(QJsonArray{currentNote});
        const auto resolved = merge(a, current, linked, {}, {{linked[0].changeId, currentNote}});
        QVERIFY(resolved.committed);
        QCOMPARE(resolved.result, b);
        const auto invalidResolution = merge(a, current, linked, {}, {{linked[0].changeId, note(99, 0, 1, 50)}});
        QVERIFY(!invalidResolution.committed);
        QCOMPARE(invalidResolution.result, current);
    }
    void metadataTimingThreeWay()
    {
        const auto base = snap({}, QJsonArray{QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 120}}},
                               {{"title", "old"}, {"artist", "A"}});
        const auto proposal = snap({}, QJsonArray{QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 130}}},
                                   {{"title", "new"}, {"artist", "A"}});
        auto cs = diff(base, proposal);
        QCOMPARE(cs.size(), 2); // one metadata field and one uniquely matched BPM edit
        bool timingModify = false;
        for (const auto &c : cs)
            if (c.domain == "Timing" && c.kind == "Modify")
                timingModify = true;
        QVERIFY(timingModify);
        const auto current = snap({}, QJsonArray{QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 120}}},
                                  {{"title", "old"}, {"artist", "B"}});
        auto result = merge(base, current, cs);
        QVERIFY(result.committed);
        QVERIFY(!result.hasConflicts());
        QCOMPARE(result.result.value("difficulties")
                     .toObject()
                     .value(DifficultyId)
                     .toObject()
                     .value("metadata")
                     .toObject()
                     .value("artist")
                     .toString(),
                 QString("B"));
        auto sameField = snap({}, QJsonArray{QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 120}}},
                              {{"title", "other"}, {"artist", "A"}});
        result = merge(base, sameField, cs);
        QVERIFY(!result.committed);
        QVERIFY(result.hasConflicts());
        QCOMPARE(result.result, sameField); // atomic: no BPM half-apply
        const auto conflictingBpm = snap({}, QJsonArray{QJsonObject{{"beat", QJsonArray{0, 0, 1}}, {"bpm", 140}}},
                                         {{"title", "old"}, {"artist", "A"}});
        result = merge(base, conflictingBpm, cs);
        QVERIFY(!result.committed);
        QVERIFY(result.hasConflicts());
        QCOMPARE(result.result, conflictingBpm);
    }
    void revisionForksCommentsAndResources()
    {
        const auto base = snap(QJsonArray{note(1, 0, 1)});
        auto root = createRevision(base, base, {{"authorId", "a"}}, "root");
        const auto originalComment = makeComment("t", "c1", {{"authorId", "a"}}, "old", {{"type", "TimePoint"}});
        root.insert("comments", QJsonArray{originalComment});
        root.insert("events", QJsonArray{QJsonObject{{"eventId", "e1"}, {"type", "Edit"}}});
        auto left = root;
        auto updated = left;
        QJsonObject leftComment = originalComment;
        leftComment.insert("body", "edited");
        const auto leftProposal = snap(QJsonArray{note(1, 0, 1), note(2, 0, 1)}, {}, {{"title", "v1"}});
        QString updateError;
        QVERIFY2(updateRevision(updated, leftProposal, {{"authorId", "a"}}, {}, &updateError,
                                {{"comments", QJsonArray{leftComment}}}),
                 qPrintable(updateError));
        QString originalChangeId;
        for (const auto &v : updated.value("changes").toArray())
            if (v.toObject().value("path").toString().endsWith("/metadata/title"))
                originalChangeId = v.toObject().value("changeId").toString();
        QVERIFY(!originalChangeId.isEmpty());
        const auto changedTitleProposal = snap(QJsonArray{note(1, 0, 1), note(2, 0, 1)}, {}, {{"title", "v2"}});
        QVERIFY2(updateRevision(updated, changedTitleProposal, {{"authorId", "a"}}, {}, &updateError,
                                {{"comments", QJsonArray{leftComment}}}),
                 qPrintable(updateError));
        bool retained = false;
        for (const auto &v : updated.value("changes").toArray())
            if (v.toObject().value("path").toString().endsWith("/metadata/title"))
                retained = v.toObject().value("changeId").toString() == originalChangeId;
        QVERIFY(retained);
        QCOMPARE(updated.value("parentRevisionIds").toArray().size(), 1);
        const QString stableRevisionId = updated.value("revisionId").toString();
        const int priorEvents = updated.value("events").toArray().size();
        QVERIFY(updateRevision(updated, changedTitleProposal, {{"authorId", "a"}}, {}, &updateError,
                               {{"comments", QJsonArray{leftComment}}}));
        QCOMPARE(updated.value("revisionId").toString(), stableRevisionId);
        QCOMPARE(updated.value("events").toArray().size(), priorEvents);
        left = updated;
        auto right = root;
        QJsonObject rightComment = originalComment;
        rightComment.insert("state", "Resolved");
        QVERIFY2(updateRevision(right, snap(QJsonArray{note(1, 0, 1), note(3, 0, 1)}, {}, {{"artist", "B"}}),
                                {{"authorId", "b"}}, {}, &updateError, {{"comments", QJsonArray{rightComment}}}),
                 qPrintable(updateError));
        QStringList conflicts;
        QString error;
        const auto merged = mergeRevisions(left, right, &conflicts, &error);
        QVERIFY2(!merged.isEmpty(), qPrintable(error));
        QCOMPARE(merged.value("parentRevisionIds").toArray().size(), 2);
        QCOMPARE(merged.value("comments").toArray().size(), 1);
        QCOMPARE(merged.value("events").toArray().size(),
                 4); // shared event deduped, each real revision update retained
        const auto mergedComment = merged.value("comments").toArray()[0].toObject();
        QCOMPARE(mergedComment.value("body").toString(), QString("edited"));
        QCOMPARE(mergedComment.value("state").toString(), QString("Resolved"));
        QVERIFY(validateRevision(merged, &error));
        const auto repeatedImport = mergeRevisions(merged, right, &conflicts, &error);
        QCOMPARE(repeatedImport, merged);

        auto forkA = root, forkB = root;
        QJsonObject bodyA = originalComment, bodyB = originalComment;
        bodyA.insert("body", "branch A");
        bodyB.insert("body", "branch B");
        QVERIFY(updateRevision(forkA, base, {{"authorId", "a"}}, {}, &updateError, {{"comments", QJsonArray{bodyA}}}));
        QVERIFY(updateRevision(forkB, base, {{"authorId", "b"}}, {}, &updateError, {{"comments", QJsonArray{bodyB}}}));
        conflicts.clear();
        auto conflictMerge = mergeRevisions(forkA, forkB, &conflicts, &error);
        QVERIFY2(!conflictMerge.isEmpty(), qPrintable(error));
        QVERIFY(!conflicts.isEmpty());
        const auto alternatives =
            conflictMerge.value("comments").toArray()[0].toObject().value("_conflictAlternatives").toArray();
        QVERIFY(!alternatives.isEmpty());
        QCOMPARE(alternatives[0].toObject().value("incoming").toString(), QString("branch B"));

        auto fieldA = root, fieldB = root;
        QVERIFY(updateRevision(fieldA, snap(QJsonArray{note(1, 0, 1)}, {}, {{"title", "A"}}), {{"authorId", "a"}}, {},
                               &updateError));
        QVERIFY(updateRevision(fieldB, snap(QJsonArray{note(1, 0, 1)}, {}, {{"title", "B"}}), {{"authorId", "b"}}, {},
                               &updateError));
        conflicts.clear();
        const auto fieldConflict = mergeRevisions(fieldA, fieldB, &conflicts, &error);
        QVERIFY2(!fieldConflict.isEmpty(), qPrintable(error));
        QVERIFY(!conflicts.isEmpty());
        QVERIFY(!fieldConflict.value("mergeConflicts").toArray().isEmpty());

        auto comment = makeComment("t", "old", {}, "old body", {});
        comment.insert("tombstone", true);
        const auto revived = mergeComments(QJsonArray{comment}, QJsonArray{makeComment("t", "old", {}, "stale", {})});
        QVERIFY(revived[0].toObject().value("tombstone").toBool());

        const QByteArray payload("asset-data");
        QJsonObject resourceRevision = left;
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        resourceRevision.insert("resources",
                                QJsonObject{{hash, QJsonObject{{"dataBase64", QString::fromLatin1(payload.toBase64())},
                                                               {"paths", QJsonArray{"audio/song.ogg"}}}}});
        QVERIFY(validateRevision(resourceRevision, &error));
        resourceRevision.insert(
            "resources", QJsonObject{{"bad-hash", QJsonObject{{"dataBase64", QString::fromLatin1(payload.toBase64())},
                                                              {"paths", QJsonArray{"audio/song.ogg"}}}}});
        QVERIFY(!validateRevision(resourceRevision, &error));
        resourceRevision.insert("resources",
                                QJsonObject{{hash, QJsonObject{{"dataBase64", QString::fromLatin1(payload.toBase64())},
                                                               {"paths", QJsonArray{"../escape.ogg"}}}}});
        QVERIFY(!validateRevision(resourceRevision, &error));
    }
    void packageRollbackAndIdempotentRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto base = snap({});
        auto revision = createRevision(base, base, {{"authorId", "a"}}, "empty");
        const QString path = dir.filePath("one.ccepr");
        QString error;
        QVERIFY2(writePackage(path, revision, &error), qPrintable(error));
        QJsonObject decoded{{"sentinel", true}};
        QVERIFY2(readPackage(path, decoded, &error), qPrintable(error));
        QCOMPARE(decoded.value("revisionId"), revision.value("revisionId"));
        QVERIFY2(readPackage(path, decoded, &error), qPrintable(error)); // repeated import is stable
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.resize(12));
        f.close();
        const auto before = decoded;
        QVERIFY(!readPackage(path, decoded, &error));
        QCOMPARE(decoded, before); // failed read never mutates caller's output
    }
    void parallelMovesKeepAlternativesAndResourcePayloads()
    {
        const auto base = snap(QJsonArray{note(1, 0, 1, 20)});
        auto root = createRevision(base, base, {{"authorId", "a"}}, "root");
        auto left = root, right = root;
        QString error;
        Change a;
        a.changeId = "left-move";
        a.domain = "Note";
        a.path = "/layers/" + LayerId + "/notes";
        a.kind = "Move";
        a.moveIntent = true;
        a.before = note(1, 0, 1, 20);
        a.after = note(2, 0, 1, 30);
        Change b = a;
        b.changeId = "right-move";
        b.after = note(3, 0, 1, 40);
        const QByteArray bytes("incoming only");
        const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        const QJsonObject resources{{hash, QJsonObject{{"dataBase64", QString::fromLatin1(bytes.toBase64())},
                                                       {"paths", QJsonArray{"audio.ogg"}}}}};
        QVERIFY2(updateRevision(left, snap(QJsonArray{a.after}), {}, {a}, &error), qPrintable(error));
        QVERIFY2(updateRevision(right, snap(QJsonArray{b.after}), {}, {b}, &error, {{"resources", resources}}),
                 qPrintable(error));
        QStringList conflicts;
        const auto merged = mergeRevisions(left, right, &conflicts, &error);
        QVERIFY2(!merged.isEmpty(), qPrintable(error));
        QVERIFY(!conflicts.isEmpty());
        QCOMPARE(merged.value("changes").toArray().size(), 1);
        const auto alternatives = merged.value("mergeConflicts").toArray();
        QCOMPARE(alternatives.size(), 1);
        QCOMPARE(alternatives[0].toObject().value("incoming").toObject().value("after"), b.after);
        QVERIFY(merged.value("resources").toObject().contains(hash));
        auto resolved = merged;
        QVERIFY2(resolveRevisionConflict(resolved, 0, true, {}, &error), qPrintable(error));
        QCOMPARE(resolved.value("mergeConflicts").toArray().size(), 0);
        QCOMPARE(resolved.value("changes").toArray()[0].toObject().value("after"), b.after);
        QVERIFY(validateRevision(resolved, &error));
        auto fake = createRevision(base, base, {}, "fake");
        fake["ancestors"] = QJsonArray{root.value("revisionId")};
        QVERIFY(!validateRevision(fake, &error));
    }
    void duplicateMoveCountsAndConcurrentCommentDeletion()
    {
        const auto from = note(1, 0, 1, 20), to = note(2, 0, 1, 30);
        Change a;
        a.changeId = "one";
        a.domain = "Note";
        a.path = "/layers/" + LayerId + "/notes";
        a.kind = "Move";
        a.moveIntent = true;
        a.before = from;
        a.after = to;
        Change b = a;
        b.changeId = "two";
        const auto base = snap(QJsonArray{from, from}), proposed = snap(QJsonArray{to, to});
        const auto result = merge(base, base, {a, b});
        QVERIFY(result.committed);
        QCOMPARE(result.result, proposed);
        auto root = createRevision(base, base, {}, "comments");
        auto comment = makeComment("thread", "comment", {}, "before", {});
        root["comments"] = QJsonArray{comment};
        auto left = root, right = root;
        auto tomb = comment, edited = comment;
        tomb["tombstone"] = true;
        edited["body"] = "parallel edit";
        QString error;
        QVERIFY(updateRevision(left, base, {}, {}, &error, {{"comments", QJsonArray{tomb}}}));
        QVERIFY(updateRevision(right, base, {}, {}, &error, {{"comments", QJsonArray{edited}}}));
        QStringList conflicts;
        const auto merged = mergeRevisions(left, right, &conflicts, &error);
        QVERIFY2(!merged.isEmpty(), qPrintable(error));
        QVERIFY(!conflicts.isEmpty());
        const auto record = merged.value("comments").toArray()[0].toObject();
        QVERIFY(record.value("tombstone").toBool());
        QVERIFY(!record.value("_conflictAlternatives").toArray().isEmpty());
        auto malformed = changeToJson(a);
        malformed["after"] = QJsonValue::Null;
        Change bad;
        QVERIFY(!changeFromJson(malformed, bad, &error));
    }
    void largeMultisetIsIndexed_data()
    {
        QTest::addColumn<int>("noteCount");
        QTest::newRow("5k") << 5000;
        QTest::newRow("20k") << 20000;
        QTest::newRow("100k") << 100000;
    }
    void largeMultisetIsIndexed()
    {
        QFETCH(int, noteCount);
        QJsonArray notes;
        for (int i = 0; i < noteCount; ++i)
            notes.append(note(i, 0, 1, i % 512));
        const auto base = snap(notes);
        notes.append(note(100000, 0, 1, 1));
        const auto cs = diff(base, snap(notes));
        QCOMPARE(cs.size(), 1);
        QCOMPARE(cs[0].kind, QString("Add"));
        const auto removals = diff(base, snap({}));
        QCOMPARE(removals.size(), noteCount);
        const auto applied = merge(base, base, removals);
        QVERIFY(applied.committed);
        QCOMPARE(applied.result, snap({}));
    }
    void rebaseRetainsAncestryAndUsesCurrentAsBase()
    {
        const auto base = snap(QJsonArray{note(1, 0, 1, 20)}, {}, {{"title", "base"}});
        const auto proposed = snap(QJsonArray{note(1, 0, 1, 20), note(2, 0, 1, 30)}, {}, {{"title", "base"}});
        auto revision = createRevision(base, proposed, {}, "rebase");
        const auto prior = revision.value("revisionId");
        const auto current = snap(QJsonArray{note(1, 0, 1, 20)}, {}, {{"title", "current"}});
        const auto replayed = merge(base, current, changesFrom(revision.value("changes").toArray()));
        QVERIFY(replayed.committed);
        QString error;
        QVERIFY2(rebaseRevision(revision, current, replayed.result, {}, {}, &error), qPrintable(error));
        QCOMPARE(revision.value("parentRevisionIds").toArray(), QJsonArray{prior});
        QCOMPARE(revision.value("baseSnapshot"), current);
        QCOMPARE(revision.value("changes").toArray().size(), 1);
        QVERIFY(validateRevision(revision, &error));
    }
    void eventConflictsAreResolvableAndRevisionIdsAreImmutable()
    {
        const auto base = snap({});
        auto root = createRevision(base, base, {}, "events");
        const QJsonObject event{{"eventId", "shared-event"}, {"type", "Original"}};
        root["events"] = QJsonArray{event};
        auto left = root, right = root;
        QString error;
        auto l = event, r = event;
        l["type"] = "Left";
        r["type"] = "Right";
        QVERIFY(updateRevision(left, base, {}, {}, &error, {{"events", QJsonArray{l}}}));
        QVERIFY(updateRevision(right, base, {}, {}, &error, {{"events", QJsonArray{r}}}));
        QStringList conflicts;
        auto merged = mergeRevisions(left, right, &conflicts, &error);
        QVERIFY2(!merged.isEmpty(), qPrintable(error));
        QVERIFY(!conflicts.isEmpty());
        QVERIFY2(resolveRevisionConflict(merged, 0, true, {}, &error), qPrintable(error));
        bool found = false;
        for (const auto &v : merged.value("events").toArray())
        {
            const auto e = v.toObject();
            if (e.value("eventId") == "shared-event")
            {
                QCOMPARE(e.value("type").toString(), QString("Right"));
                found = true;
            }
        }
        QVERIFY(found);
        auto divergent = root;
        divergent["events"] = QJsonArray{l};
        QVERIFY(mergeRevisions(root, divergent, &conflicts, &error).isEmpty());
    }
    void modifyingAnAcceptedChangeRequiresReviewAgain()
    {
        const auto base = snap({}, {}, {{"title", "base"}});
        const auto first = snap({}, {}, {{"title", "first"}});
        auto revision = createRevision(base, first, {}, "decisions");
        const auto changes = changesFrom(revision.value("changes").toArray());
        QCOMPARE(changes.size(), 1);
        const auto change = changes.first();
        const QJsonObject decision{
            {"changeId", change.changeId}, {"decision", "Accepted"}, {"changeHash", changeFingerprint(change)}};
        QString error;
        QVERIFY(updateRevision(revision, first, {}, {}, &error, {{"reviewDecisions", QJsonArray{decision}}}));
        QCOMPARE(revision.value("reviewDecisions").toArray()[0].toObject().value("decision").toString(),
                 QString("Accepted"));
        const auto edited = snap({}, {}, {{"title", "second"}});
        QVERIFY(updateRevision(revision, edited, {}, {}, &error));
        const auto next = revision.value("reviewDecisions").toArray()[0].toObject();
        QCOMPARE(next.value("decision").toString(), QString("Pending"));
        const auto current = changesFrom(revision.value("changes").toArray()).first();
        QCOMPARE(current.changeId, change.changeId);
        QVERIFY(!decisionMatchesChange(decision, current));
    }
    void packageRequiresResourcesAndConflictsOnConcurrentResourceReplacement()
    {
        const QByteArray original("original"), leftBytes("left"), rightBytes("right");
        const auto hash = [](const QByteArray &bytes) {
            return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        };
        auto base = snap({}, {}, {{"audioFile", "song.ogg"}});
        base["resources"] = QJsonObject{{"song.ogg", hash(original)}};
        auto left = base, right = base;
        left["resources"] = QJsonObject{{"song.ogg", hash(leftBytes)}};
        right["resources"] = QJsonObject{{"song.ogg", hash(rightBytes)}};
        const auto changes = diff(base, right);
        const auto result = merge(base, left, changes);
        QVERIFY(!result.committed);
        QVERIFY(std::any_of(result.items.cbegin(), result.items.cend(), [](const auto &item) {
            return item.change.domain == "Resource" && item.state == "Conflict";
        }));
        auto revision = createRevision(base, right, {}, "resource test");
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QString error;
        const auto path = temp.filePath("resources.ccepr");
        QVERIFY(!writePackage(path, revision, &error));
        QJsonObject payloads;
        for (const auto &bytes : {original, rightBytes})
            payloads[hash(bytes)] =
                QJsonObject{{"dataBase64", QString::fromLatin1(bytes.toBase64())}, {"paths", QJsonArray{"song.ogg"}}};
        revision["resources"] = payloads;
        QVERIFY2(writePackage(path, revision, &error), qPrintable(error));
        QJsonObject loaded;
        QVERIFY2(readPackage(path, loaded, &error), qPrintable(error));
        QCOMPARE(loaded, revision);
    }
};

QTEST_GUILESS_MAIN(ReviewCoreTests)
#include "review_core_tests.moc"

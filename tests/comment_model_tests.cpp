#include "document/DocumentModel.h"
#include "review/CommentModel.h"
#include "review/ReviewCore.h"

#include <QJsonDocument>
#include <QtTest>

using namespace CceReview;

namespace
{
QJsonObject author(const QString &id, const QString &name)
{
    return {{QStringLiteral("authorId"), id}, {QStringLiteral("authorNameSnapshot"), name}};
}

QJsonObject noteAnchor(const QString &difficultyId, const QString &layerId, const Note &note)
{
    const QJsonObject encoded = CceDocument::encodeNote(note, false);
    return {{QStringLiteral("type"), QStringLiteral("Note")},
            {QStringLiteral("difficultyId"), difficultyId},
            {QStringLiteral("beat"), QJsonArray{note.beatNum, note.numerator, note.denominator}},
            {QStringLiteral("x"), note.x},
            {QStringLiteral("targets"), QJsonArray{QJsonObject{{QStringLiteral("layerId"), layerId},
                                                               {QStringLiteral("note"), encoded},
                                                               {QStringLiteral("originalNote"), encoded},
                                                               {QStringLiteral("state"), QStringLiteral("Valid")}}}}};
}

QJsonObject target(const QString &layerId, const Note &note)
{
    const QJsonObject encoded = CceDocument::encodeNote(note, false);
    return {{QStringLiteral("layerId"), layerId},
            {QStringLiteral("note"), encoded},
            {QStringLiteral("originalNote"), encoded},
            {QStringLiteral("state"), QStringLiteral("Valid")}};
}

QJsonObject bpmJson(const BpmEntry &entry)
{
    return {{QStringLiteral("beat"), QJsonArray{entry.beatNum, entry.numerator, entry.denominator}},
            {QStringLiteral("bpm"), entry.bpm}};
}

QJsonObject singleComment(QJsonArray comments)
{
    return comments.first().toObject();
}

QJsonObject singleTarget(const QJsonObject &comment, int index = 0)
{
    return comment.value(QStringLiteral("anchor"))
        .toObject()
        .value(QStringLiteral("targets"))
        .toArray()
        .at(index)
        .toObject();
}
} // namespace

class CommentModelTests : public QObject
{
    Q_OBJECT

private slots:
    void permissionsTombstonesAndThreadAnchor();
    void noteGroupPartialDeletionAndUndo();
    void explicitNoteMoveAndPositionAnchors();
    void duplicateNoteTargetsBecomeAmbiguous();
    void bpmEntryFollowsExplicitChange();
    void rainPositionBeatStaysFixedWhenReferencedSpanChanges();
    void bpmListModificationAndUndo();
    void bpmGroupPreservesSubsetAndMissingTargets();
    void metadataTracksOnlySelectedFields();
    void rainLegacyXIsNotAReviewChange();
};

void CommentModelTests::permissionsTombstonesAndThreadAnchor()
{
    QJsonArray comments;
    QString rootId;
    QString error;
    const QJsonObject anchor{{QStringLiteral("type"), QStringLiteral("TimePoint")},
                             {QStringLiteral("beat"), QJsonArray{8, 1, 4}}};
    QVERIFY2(addComment(comments, author(QStringLiteral("alice"), QStringLiteral("Alice")),
                        QStringLiteral("Original body"), anchor, {}, &rootId, &error),
             qPrintable(error));
    const QJsonObject root = singleComment(comments);
    const QString threadId = root.value(QStringLiteral("threadId")).toString();
    QCOMPARE(threadId, rootId);
    QCOMPARE(root.value(QStringLiteral("author")).toObject().value(QStringLiteral("authorNameSnapshot")).toString(),
             QStringLiteral("Alice"));

    QVERIFY(!editComment(comments, rootId, QStringLiteral("bob"), QStringLiteral("Attempt"), &error));
    QVERIFY(!deleteComment(comments, rootId, QStringLiteral("bob"), &error));
    QCOMPARE(singleComment(comments).value(QStringLiteral("body")).toString(), QStringLiteral("Original body"));

    QVERIFY2(editComment(comments, rootId, QStringLiteral("alice"), QStringLiteral("Edited body"), &error),
             qPrintable(error));
    QVERIFY2(deleteComment(comments, rootId, QStringLiteral("alice"), &error), qPrintable(error));
    const QJsonObject tombstone = singleComment(comments);
    QVERIFY(tombstone.value(QStringLiteral("tombstone")).toBool());
    QCOMPARE(tombstone.value(QStringLiteral("commentId")).toString(), rootId);
    QVERIFY(!editComment(comments, rootId, QStringLiteral("alice"), QStringLiteral("Resurrect"), &error));

    const QJsonObject unrelatedAnchor{{QStringLiteral("type"), QStringLiteral("Region")},
                                      {QStringLiteral("beat"), QJsonArray{100, 0, 4}}};
    QString replyId;
    QVERIFY2(addComment(comments, author(QStringLiteral("bob"), QStringLiteral("Bob")), QStringLiteral("Reply"),
                        unrelatedAnchor, threadId, &replyId, &error),
             qPrintable(error));
    QCOMPARE(comments.size(), 2);
    const QJsonObject reply = comments.at(1).toObject();
    QCOMPARE(reply.value(QStringLiteral("threadId")).toString(), threadId);
    QCOMPARE(reply.value(QStringLiteral("anchor")).toObject(), anchor);
    QCOMPARE(reply.value(QStringLiteral("author")).toObject().value(QStringLiteral("authorNameSnapshot")).toString(),
             QStringLiteral("Bob"));

    QVERIFY2(setThreadResolved(comments, threadId, true, &error), qPrintable(error));
    QVERIFY(comments.at(0).toObject().value(QStringLiteral("tombstone")).toBool());
    QCOMPARE(comments.at(1).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("Resolved"));
    QVERIFY2(setThreadResolved(comments, threadId, false, &error), qPrintable(error));
    QCOMPARE(comments.at(1).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("Unresolved"));
}

void CommentModelTests::noteGroupPartialDeletionAndUndo()
{
    Chart chart;
    const Note first(4, 0, 4, 128);
    const Note second(8, 0, 4, 256);
    chart.addNotes({first, second});
    const CceDocument::Project original = CceDocument::Project::fromChart(chart);
    const QString difficultyId = original.activeDifficultyId;
    const QString layerId = original.layers.first().layerId;

    QJsonObject groupAnchor{{QStringLiteral("type"), QStringLiteral("NoteGroup")},
                            {QStringLiteral("difficultyId"), difficultyId},
                            {QStringLiteral("targets"), QJsonArray{target(layerId, first), target(layerId, second)}}};
    QJsonArray comments{makeComment(QStringLiteral("thread"), QStringLiteral("comment"),
                                    author(QStringLiteral("a"), QStringLiteral("A")), QStringLiteral("Group"),
                                    groupAnchor)};

    CceDocument::Project afterDelete = original;
    afterDelete.layers[layerId].notes.removeAt(0);
    QJsonArray relocated = relocateComments(comments, original, afterDelete);
    const QJsonArray targets =
        singleComment(relocated).value(QStringLiteral("anchor")).toObject().value(QStringLiteral("targets")).toArray();
    QCOMPARE(targets.size(), 2);
    QCOMPARE(targets.at(0).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("Missing"));
    QCOMPARE(targets.at(1).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("Valid"));
    QCOMPARE(targets.at(0).toObject().value(QStringLiteral("note")).toObject(), CceDocument::encodeNote(first));

    QJsonArray afterUndo = relocateComments(relocated, afterDelete, original);
    const QJsonArray restoredTargets =
        singleComment(afterUndo).value(QStringLiteral("anchor")).toObject().value(QStringLiteral("targets")).toArray();
    QCOMPARE(restoredTargets.at(0).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("Valid"));
    QCOMPARE(restoredTargets.at(1).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("Valid"));

    const QByteArray serialized = QJsonDocument(afterUndo).toJson(QJsonDocument::Compact);
    QVERIFY(!serialized.contains("runtimeId"));
    QVERIFY(!serialized.contains("\"id\""));
}

void CommentModelTests::explicitNoteMoveAndPositionAnchors()
{
    Chart chart;
    const Note originalNote(4, 0, 4, 128);
    chart.addNote(originalNote);
    const CceDocument::Project before = CceDocument::Project::fromChart(chart);
    const QString difficultyId = before.activeDifficultyId;
    const QString layerId = before.layers.first().layerId;
    CceDocument::Project after = before;
    Note moved = originalNote;
    moved.beatNum = 12;
    moved.x = 300;
    after.layers[layerId].notes[0] = moved;

    QJsonObject objectAnchor = noteAnchor(difficultyId, layerId, originalNote);
    QJsonObject positionAnchor{{QStringLiteral("type"), QStringLiteral("TimePosition")},
                               {QStringLiteral("difficultyId"), difficultyId},
                               {QStringLiteral("beat"), QJsonArray{4, 0, 4}},
                               {QStringLiteral("x"), 128}};
    QJsonArray comments{
        makeComment(QStringLiteral("objectThread"), QStringLiteral("objectComment"),
                    author(QStringLiteral("a"), QStringLiteral("A")), QStringLiteral("Follow note"), objectAnchor),
        makeComment(QStringLiteral("fixedThread"), QStringLiteral("fixedComment"),
                    author(QStringLiteral("a"), QStringLiteral("A")), QStringLiteral("Stay here"), positionAnchor)};

    Change move;
    move.domain = QStringLiteral("Note");
    move.path = QStringLiteral("/layers/%1/notes").arg(layerId);
    move.kind = QStringLiteral("Move");
    move.before = CceDocument::encodeNote(originalNote);
    move.after = CceDocument::encodeNote(moved);
    move.moveIntent = true;

    const QJsonArray result = relocateComments(comments, before, after, {move});
    const QJsonObject movedComment = result.at(0).toObject();
    const QJsonObject movedTarget = singleTarget(movedComment);
    QCOMPARE(movedTarget.value(QStringLiteral("state")).toString(), QStringLiteral("Modified"));
    QCOMPARE(movedTarget.value(QStringLiteral("note")).toObject(), CceDocument::encodeNote(moved));
    QCOMPARE(movedComment.value(QStringLiteral("anchor")).toObject().value(QStringLiteral("beat")).toArray(),
             (QJsonArray{12, 0, 4}));
    QCOMPARE(movedComment.value(QStringLiteral("anchor")).toObject().value(QStringLiteral("x")).toInt(), 300);

    const QJsonObject fixed = result.at(1).toObject().value(QStringLiteral("anchor")).toObject();
    QCOMPARE(fixed.value(QStringLiteral("beat")).toArray(), (QJsonArray{4, 0, 4}));
    QCOMPARE(fixed.value(QStringLiteral("x")).toInt(), 128);
}

void CommentModelTests::duplicateNoteTargetsBecomeAmbiguous()
{
    Chart chart;
    const Note duplicate(4, 0, 4, 128);
    chart.addNotes({duplicate, duplicate});
    const CceDocument::Project project = CceDocument::Project::fromChart(chart);
    const QString layerId = project.layers.first().layerId;
    QJsonObject anchor = noteAnchor(project.activeDifficultyId, layerId, duplicate);
    QJsonArray comments{makeComment(QStringLiteral("thread"), QStringLiteral("comment"),
                                    author(QStringLiteral("a"), QStringLiteral("A")),
                                    QStringLiteral("Ambiguous target"), anchor)};

    const QJsonArray result = relocateComments(comments, project, project);
    QCOMPARE(singleTarget(singleComment(result)).value(QStringLiteral("state")).toString(),
             QStringLiteral("Ambiguous"));
}

void CommentModelTests::bpmEntryFollowsExplicitChange()
{
    Chart chart;
    const CceDocument::Project before = CceDocument::Project::fromChart(chart);
    const QString difficultyId = before.activeDifficultyId;
    const BpmEntry original = before.difficulties.value(difficultyId).timing.first();
    BpmEntry modified = original;
    modified.bpm += 12.5;
    CceDocument::Project after = before;
    after.difficulties[difficultyId].timing[0] = modified;

    const QJsonObject anchor{
        {QStringLiteral("type"), QStringLiteral("BpmEntry")},
        {QStringLiteral("difficultyId"), difficultyId},
        {QStringLiteral("beat"), QJsonArray{original.beatNum, original.numerator, original.denominator}},
        {QStringLiteral("bpm"), bpmJson(original)}};
    QJsonArray comments{makeComment(QStringLiteral("thread"), QStringLiteral("comment"),
                                    author(QStringLiteral("a"), QStringLiteral("A")), QStringLiteral("BPM"), anchor)};
    Change change;
    change.domain = QStringLiteral("Timing");
    change.path = QStringLiteral("/difficulties/%1/timing").arg(difficultyId);
    change.kind = QStringLiteral("Modify");
    change.before = bpmJson(original);
    change.after = bpmJson(modified);

    const QJsonArray result = relocateComments(comments, before, after, {change});
    const QJsonObject updatedAnchor = singleComment(result).value(QStringLiteral("anchor")).toObject();
    QCOMPARE(updatedAnchor.value(QStringLiteral("state")).toString(), QStringLiteral("Modified"));
    QCOMPARE(updatedAnchor.value(QStringLiteral("bpm")).toObject(), bpmJson(modified));
}

void CommentModelTests::rainPositionBeatStaysFixedWhenReferencedSpanChanges()
{
    Chart chart;
    Note rain(4, 0, 4, 12, 0, 4, 128);
    chart.addNote(rain);
    const CceDocument::Project before = CceDocument::Project::fromChart(chart);
    const QString difficultyId = before.activeDifficultyId;
    const QString layerId = before.layers.first().layerId;
    Note changed = rain;
    changed.endBeatNum = 8; // The stored point at beat 10 is no longer on the span.
    CceDocument::Project after = before;
    after.layers[layerId].notes[0] = changed;

    QJsonObject rainTarget = target(layerId, rain);
    rainTarget.insert(QStringLiteral("positionBeat"), QJsonArray{10, 0, 4});
    QJsonObject anchor{{QStringLiteral("type"), QStringLiteral("Note")},
                       {QStringLiteral("difficultyId"), difficultyId},
                       {QStringLiteral("positionBeat"), QJsonArray{10, 0, 4}},
                       {QStringLiteral("targets"), QJsonArray{rainTarget}}};
    QJsonArray comments{makeComment(QStringLiteral("thread"), QStringLiteral("comment"),
                                    author(QStringLiteral("a"), QStringLiteral("A")),
                                    QStringLiteral("Rain child position"), anchor)};
    Change change;
    change.domain = QStringLiteral("Note");
    change.path = QStringLiteral("/layers/%1/notes").arg(layerId);
    change.kind = QStringLiteral("Move");
    change.before = CceDocument::encodeNote(rain);
    change.after = CceDocument::encodeNote(changed);

    QJsonArray relocated = relocateComments(comments, before, after, {change});
    QJsonObject movedComment = singleComment(relocated);
    QJsonObject movedAnchor = movedComment.value(QStringLiteral("anchor")).toObject();
    QJsonObject movedTarget = movedAnchor.value(QStringLiteral("targets")).toArray().first().toObject();
    QCOMPARE(movedTarget.value(QStringLiteral("state")).toString(), QStringLiteral("Modified"));
    QCOMPARE(movedTarget.value(QStringLiteral("positionBeat")).toArray(), (QJsonArray{10, 0, 4}));
    QCOMPARE(movedAnchor.value(QStringLiteral("positionBeat")).toArray(), (QJsonArray{10, 0, 4}));

    const QJsonArray restored = relocateComments(relocated, after, before, {change});
    QCOMPARE(singleTarget(singleComment(restored)).value(QStringLiteral("state")).toString(), QStringLiteral("Valid"));
    QCOMPARE(singleTarget(singleComment(restored)).value(QStringLiteral("positionBeat")).toArray(),
             (QJsonArray{10, 0, 4}));
}

void CommentModelTests::bpmListModificationAndUndo()
{
    Chart chart;
    const CceDocument::Project before = CceDocument::Project::fromChart(chart);
    const QString difficultyId = before.activeDifficultyId;
    CceDocument::Project after = before;
    BpmEntry second(4, 0, 4, 150.0);
    after.difficulties[difficultyId].timing.append(second);
    QJsonObject anchor{{QStringLiteral("type"), QStringLiteral("BpmList")},
                       {QStringLiteral("difficultyId"), difficultyId}};
    QJsonArray comments{makeComment(QStringLiteral("thread"), QStringLiteral("comment"),
                                    author(QStringLiteral("a"), QStringLiteral("A")), QStringLiteral("Timing list"),
                                    anchor)};

    const QJsonArray changed = relocateComments(comments, before, after);
    QJsonObject changedAnchor = singleComment(changed).value(QStringLiteral("anchor")).toObject();
    QCOMPARE(changedAnchor.value(QStringLiteral("state")).toString(), QStringLiteral("Modified"));
    QCOMPARE(changedAnchor.value(QStringLiteral("bpmList")).toArray().size(), 2);

    const QJsonArray undone = relocateComments(changed, after, before);
    const QJsonObject undoneAnchor = singleComment(undone).value(QStringLiteral("anchor")).toObject();
    QCOMPARE(undoneAnchor.value(QStringLiteral("state")).toString(), QStringLiteral("Valid"));
    QCOMPARE(undoneAnchor.value(QStringLiteral("bpmList")).toArray().size(), 1);
}

void CommentModelTests::bpmGroupPreservesSubsetAndMissingTargets()
{
    Chart chart;
    chart.addBpm(BpmEntry(4, 0, 1, 150));
    chart.addBpm(BpmEntry(8, 0, 1, 180));
    const auto before = CceDocument::Project::fromChart(chart);
    const auto id = before.activeDifficultyId;
    const auto timing = before.difficulties.value(id).timing;
    const QJsonArray subset{bpmJson(timing.first()), bpmJson(timing.last())};
    QJsonArray comments{makeComment("thread", "comment", author("a", "A"), "Selected timing",
        {{"type", "BpmGroup"}, {"difficultyId", id}, {"bpmList", subset}, {"originalBpmList", subset}})};
    auto after = before;
    after.difficulties[id].timing.removeLast();
    const auto changed = relocateComments(comments, before, after);
    auto anchor = changed.first().toObject().value("anchor").toObject();
    QCOMPARE(anchor.value("bpmList").toArray(), subset);
    QCOMPARE(anchorReferenceState(anchor), QString("Missing"));
    const auto restored = relocateComments(changed, after, before);
    QCOMPARE(anchorReferenceState(restored.first().toObject().value("anchor").toObject()), QString("Valid"));
}

void CommentModelTests::metadataTracksOnlySelectedFields()
{
    Chart chart;
    chart.meta().title = "Original title";
    chart.meta().artist = "Artist";
    const auto before = CceDocument::Project::fromChart(chart);
    const auto id = before.activeDifficultyId;
    const QJsonObject original{{"title", "Original title"}};
    QJsonArray comments{makeComment("thread", "comment", author("a", "A"), "Check title",
        {{"type", "Metadata"}, {"difficultyId", id}, {"fields", QJsonArray{"title"}},
         {"metadata", original}, {"originalMetadata", original}})};
    auto after = before;
    after.difficulties[id].metadata.artist = "Unrelated artist change";
    auto changed = relocateComments(comments, before, after);
    QCOMPARE(anchorReferenceState(changed.first().toObject().value("anchor").toObject()), QString("Valid"));
    after.difficulties[id].metadata.title = "Changed title";
    changed = relocateComments(comments, before, after);
    QCOMPARE(anchorReferenceState(changed.first().toObject().value("anchor").toObject()), QString("Modified"));
    const auto restored = relocateComments(changed, after, before);
    QCOMPARE(restored.first().toObject().value("anchor").toObject().value("metadata").toObject(), original);
    QCOMPARE(anchorReferenceState(restored.first().toObject().value("anchor").toObject()), QString("Valid"));
}

void CommentModelTests::rainLegacyXIsNotAReviewChange()
{
    Chart chart;
    Note rain(2, 0, 1, 6, 0, 1, 64);
    chart.addNote(rain);
    const auto before = CceDocument::Project::fromChart(chart);
    auto after = before;
    const auto difficulty = before.activeDifficultyId;
    const auto layer = before.difficulties.value(difficulty).bindings.first().layerId;
    after.layers[layer].notes.first().x = 400;
    QVERIFY(diff(before.toJson(), after.toJson()).isEmpty());
    QCOMPARE(canonicalHash(CceDocument::encodeNote(rain, true)), canonicalHash(CceDocument::encodeNote(after.layers[layer].notes.first(), true)));
    // Existing package hashes remain compatible, even though review equality ignores legacy X.
    QVERIFY(canonicalHash(CceDocument::encodeNote(rain)) != canonicalHash(CceDocument::encodeNote(after.layers[layer].notes.first())));
    QVERIFY(!CceDocument::encodeNote(rain, true).contains("x"));
    Note decoded;
    QVERIFY(CceDocument::decodeNote(CceDocument::encodeNote(rain, true), decoded));
    QVERIFY(decoded.isRainNote());
    QJsonArray comments{makeComment("thread", "comment", author("a", "A"), "Rain target", noteAnchor(difficulty, layer, rain))};
    const auto relocated = relocateComments(comments, before, after);
    QCOMPARE(anchorReferenceState(relocated.first().toObject().value("anchor").toObject()), QString("Valid"));
}

QTEST_GUILESS_MAIN(CommentModelTests)
#include "comment_model_tests.moc"

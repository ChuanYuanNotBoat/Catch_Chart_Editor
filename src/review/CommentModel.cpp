#include "CommentModel.h"

#include <QHash>
#include <QDateTime>
#include <QSet>
#include <QUuid>

namespace CceReview
{
namespace
{
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

void clearError(QString *error)
{
    if (error)
        error->clear();
}

QString uuid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QString jsonKey(const QJsonObject &object)
{
    if (object.value("type").toInt(-1) == 3 && object.contains("beat"))
    {
        auto rain = object;
        rain.remove("x");
        return QString::fromLatin1(canonicalHash(rain));
    }
    return QString::fromLatin1(canonicalHash(object));
}

QString pointerUnescape(QString value)
{
    value.replace(QStringLiteral("~1"), QStringLiteral("/"));
    value.replace(QStringLiteral("~0"), QStringLiteral("~"));
    return value;
}

QString noteLayerForChange(const Change &change)
{
    if (change.domain != QStringLiteral("Note"))
        return {};
    const QStringList parts = change.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() != 3 || parts.at(0) != QStringLiteral("layers") || parts.at(2) != QStringLiteral("notes"))
        return {};
    return pointerUnescape(parts.at(1));
}

QJsonObject noteObject(QJsonValue value)
{
    if (!value.isObject())
        return {};
    QJsonObject object = value.toObject();
    // Runtime Note IDs are process-local and must never be stored in anchors.
    object.remove(QStringLiteral("id"));
    object.remove(QStringLiteral("runtimeId"));
    return object;
}

QJsonObject sanitizeAnchor(const QJsonObject &source)
{
    QJsonObject anchor = source;
    if (!anchor.value(QStringLiteral("targets")).isArray())
        return anchor;
    QJsonArray targets;
    for (const QJsonValue &targetValue : anchor.value(QStringLiteral("targets")).toArray())
    {
        if (!targetValue.isObject())
        {
            targets.append(targetValue);
            continue;
        }
        QJsonObject target = targetValue.toObject();
        if (target.value(QStringLiteral("note")).isObject())
            target.insert(QStringLiteral("note"), noteObject(target.value(QStringLiteral("note"))));
        if (target.value(QStringLiteral("originalNote")).isObject())
            target.insert(QStringLiteral("originalNote"), noteObject(target.value(QStringLiteral("originalNote"))));
        else if (target.value(QStringLiteral("note")).isObject())
            target.insert(QStringLiteral("originalNote"), target.value(QStringLiteral("note")));
        if (!target.value(QStringLiteral("state")).isString())
            target.insert(QStringLiteral("state"), QStringLiteral("Valid"));
        targets.append(target);
    }
    anchor.insert(QStringLiteral("targets"), targets);
    return anchor;
}

struct ProjectIndex
{
    QHash<QString, QHash<QString, QVector<QJsonObject>>> notesByLayer;
    QHash<QString, QHash<QString, QVector<QJsonObject>>> bpmsByDifficulty;
    QHash<QString, QSet<QString>> boundLayersByDifficulty;
};

ProjectIndex buildIndex(const CceDocument::Project &project)
{
    ProjectIndex index;
    for (auto layer = project.layers.cbegin(); layer != project.layers.cend(); ++layer)
    {
        auto &byKey = index.notesByLayer[layer.key()];
        for (const Note &note : layer->notes)
        {
            const QJsonObject encoded = CceDocument::encodeNote(note, true);
            byKey[jsonKey(encoded)].append(encoded);
        }
    }
    for (auto difficulty = project.difficulties.cbegin(); difficulty != project.difficulties.cend(); ++difficulty)
    {
        auto &byKey = index.bpmsByDifficulty[difficulty.key()];
        auto &boundLayers = index.boundLayersByDifficulty[difficulty.key()];
        for (const CceDocument::LayerBinding &binding : difficulty->bindings)
            boundLayers.insert(binding.layerId);
        for (const BpmEntry &entry : difficulty->timing)
        {
            const QJsonObject encoded{
                {QStringLiteral("beat"), QJsonArray{entry.beatNum, entry.numerator, entry.denominator}},
                {QStringLiteral("bpm"), entry.bpm}};
            byKey[jsonKey(encoded)].append(encoded);
        }
    }
    return index;
}

bool hasLayer(const ProjectIndex &index, const QString &difficultyId, const QString &layerId)
{
    return index.boundLayersByDifficulty.value(difficultyId).contains(layerId);
}

int occurrenceCount(const ProjectIndex &index, const QString &layerId, const QJsonObject &note)
{
    return index.notesByLayer.value(layerId).value(jsonKey(note)).size();
}

int bpmOccurrenceCount(const ProjectIndex &index, const QString &difficultyId, const QJsonObject &bpm)
{
    return index.bpmsByDifficulty.value(difficultyId).value(jsonKey(bpm)).size();
}

QJsonArray timingSnapshot(const CceDocument::Project &project, const QString &difficultyId)
{
    QJsonArray result;
    const auto difficulty = project.difficulties.constFind(difficultyId);
    if (difficulty == project.difficulties.cend())
        return result;
    for (const BpmEntry &entry : difficulty->timing)
    {
        result.append(
            QJsonObject{{QStringLiteral("beat"), QJsonArray{entry.beatNum, entry.numerator, entry.denominator}},
                        {QStringLiteral("bpm"), entry.bpm}});
    }
    return result;
}

QString arrayState(const QJsonArray &left, const QJsonArray &right)
{
    return jsonKey(QJsonObject{{QStringLiteral("value"), left}})
                   == jsonKey(QJsonObject{{QStringLiteral("value"), right}})
               ? QStringLiteral("Valid")
               : QStringLiteral("Modified");
}

QString noteState(const QJsonObject &original, const QJsonObject &current)
{
    return jsonKey(original) == jsonKey(current) ? QStringLiteral("Valid") : QStringLiteral("Modified");
}

struct ExplicitNoteChange
{
    QString kind;
    QJsonObject after;
};

using NoteChangeIndex = QHash<QString, QHash<QString, QVector<ExplicitNoteChange>>>;
using BpmChangeIndex = QHash<QString, QHash<QString, QVector<ExplicitNoteChange>>>;

NoteChangeIndex buildNoteChangeIndex(const QVector<Change> &changes)
{
    NoteChangeIndex index;
    for (const Change &change : changes)
    {
        const QString layerId = noteLayerForChange(change);
        if (layerId.isEmpty() || !change.before.isObject())
            continue;
        index[layerId][jsonKey(noteObject(change.before))].append({change.kind, noteObject(change.after)});
    }
    return index;
}

BpmChangeIndex buildBpmChangeIndex(const QVector<Change> &changes)
{
    BpmChangeIndex index;
    for (const Change &change : changes)
    {
        if (change.domain != QStringLiteral("Timing") || !change.before.isObject())
            continue;
        const QStringList parts = change.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() != 3 || parts.at(0) != QStringLiteral("difficulties")
            || parts.at(2) != QStringLiteral("timing"))
            continue;
        const QString difficultyId = pointerUnescape(parts.at(1));
        index[difficultyId][jsonKey(change.before.toObject())].append({change.kind, change.after.toObject()});
    }
    return index;
}

QString relocateNoteTarget(QJsonObject &target, const QString &difficultyId, const ProjectIndex &beforeIndex,
                           const ProjectIndex &afterIndex, const NoteChangeIndex &explicitIndex)
{
    const QString layerId = target.value(QStringLiteral("layerId")).toString();
    QJsonObject current = noteObject(target.value(QStringLiteral("note")));
    QJsonObject original = noteObject(target.value(QStringLiteral("originalNote")));
    if (original.isEmpty())
        original = current;
    if (current.isEmpty())
    {
        target.insert(QStringLiteral("originalNote"), original);
        target.insert(QStringLiteral("state"), QStringLiteral("Missing"));
        return QStringLiteral("Missing");
    }

    if (!hasLayer(afterIndex, difficultyId, layerId))
    {
        target.insert(QStringLiteral("note"), current);
        target.insert(QStringLiteral("originalNote"), original);
        target.insert(QStringLiteral("state"), QStringLiteral("Missing"));
        return QStringLiteral("Missing");
    }

    const int beforeCount =
        hasLayer(beforeIndex, difficultyId, layerId) ? occurrenceCount(beforeIndex, layerId, current) : 0;
    const int afterCount = occurrenceCount(afterIndex, layerId, current);
    const auto explicitByLayer = explicitIndex.constFind(layerId);
    const QVector<ExplicitNoteChange> transitions = explicitByLayer == explicitIndex.cend()
                                                        ? QVector<ExplicitNoteChange>{}
                                                        : explicitByLayer->value(jsonKey(current));

    if (!transitions.isEmpty())
    {
        if (beforeCount > 1 || transitions.size() != 1)
        {
            target.insert(QStringLiteral("note"), current);
            target.insert(QStringLiteral("originalNote"), original);
            target.insert(QStringLiteral("state"), QStringLiteral("Ambiguous"));
            return QStringLiteral("Ambiguous");
        }
        const ExplicitNoteChange transition = transitions.first();
        if (transition.kind == QStringLiteral("Remove"))
        {
            target.insert(QStringLiteral("note"), current);
            target.insert(QStringLiteral("originalNote"), original);
            target.insert(QStringLiteral("state"), QStringLiteral("Missing"));
            return QStringLiteral("Missing");
        }
        if ((transition.kind == QStringLiteral("Move") || transition.kind == QStringLiteral("Modify"))
            && !transition.after.isEmpty())
        {
            current = transition.after;
            const int mappedCount = occurrenceCount(afterIndex, layerId, current);
            if (mappedCount > 1)
            {
                target.insert(QStringLiteral("note"), current);
                target.insert(QStringLiteral("originalNote"), original);
                target.insert(QStringLiteral("state"), QStringLiteral("Ambiguous"));
                return QStringLiteral("Ambiguous");
            }
            if (mappedCount == 1)
            {
                const QString state = noteState(original, current);
                target.insert(QStringLiteral("note"), current);
                target.insert(QStringLiteral("originalNote"), original);
                target.insert(QStringLiteral("state"), state);
                return state;
            }
            target.insert(QStringLiteral("note"), noteObject(target.value(QStringLiteral("note"))));
            target.insert(QStringLiteral("originalNote"), original);
            target.insert(QStringLiteral("state"), QStringLiteral("Missing"));
            return QStringLiteral("Missing");
        }
    }

    if (beforeCount > 1 || afterCount > 1)
    {
        target.insert(QStringLiteral("note"), current);
        target.insert(QStringLiteral("originalNote"), original);
        target.insert(QStringLiteral("state"), QStringLiteral("Ambiguous"));
        return QStringLiteral("Ambiguous");
    }
    if (afterCount == 1)
    {
        const QString state = noteState(original, current);
        target.insert(QStringLiteral("note"), current);
        target.insert(QStringLiteral("originalNote"), original);
        target.insert(QStringLiteral("state"), state);
        return state;
    }

    // Undo may restore the original encoded object while the anchor currently
    // follows its edited snapshot. Reattach only on a unique content match;
    // this is deterministic and never guesses between duplicate notes.
    if (beforeCount == 1 && !original.isEmpty() && jsonKey(original) != jsonKey(current)
        && occurrenceCount(afterIndex, layerId, original) == 1)
    {
        target.insert(QStringLiteral("note"), original);
        target.insert(QStringLiteral("originalNote"), original);
        target.insert(QStringLiteral("state"), QStringLiteral("Valid"));
        return QStringLiteral("Valid");
    }

    target.insert(QStringLiteral("note"), current);
    target.insert(QStringLiteral("originalNote"), original);
    target.insert(QStringLiteral("state"), QStringLiteral("Missing"));
    return QStringLiteral("Missing");
}

QJsonObject relocateBpm(const QJsonObject &current, const QString &difficultyId, const ProjectIndex &beforeIndex,
                        const ProjectIndex &afterIndex, const BpmChangeIndex &changeIndex, QString *state)
{
    const QString key = jsonKey(current);
    int beforeCount = beforeIndex.bpmsByDifficulty.value(difficultyId).value(key).size();
    int afterCount = afterIndex.bpmsByDifficulty.value(difficultyId).value(key).size();
    const QVector<ExplicitNoteChange> mapped = changeIndex.value(difficultyId).value(key);
    if (!mapped.isEmpty())
    {
        if (beforeCount > 1 || mapped.size() != 1)
        {
            *state = QStringLiteral("Ambiguous");
            return current;
        }
        const ExplicitNoteChange change = mapped.first();
        if (change.kind == QStringLiteral("Remove"))
        {
            *state = QStringLiteral("Missing");
            return current;
        }
        const QJsonObject destination = change.after;
        if ((change.kind == QStringLiteral("Move") || change.kind == QStringLiteral("Modify")) && !destination.isEmpty()
            && bpmOccurrenceCount(afterIndex, difficultyId, destination) > 1)
        {
            *state = QStringLiteral("Ambiguous");
            return destination;
        }
        if ((change.kind == QStringLiteral("Move") || change.kind == QStringLiteral("Modify")) && !destination.isEmpty()
            && bpmOccurrenceCount(afterIndex, difficultyId, destination) == 1)
        {
            *state = jsonKey(destination) == jsonKey(current) ? QStringLiteral("Valid") : QStringLiteral("Modified");
            return destination;
        }
        *state = QStringLiteral("Missing");
        return current;
    }
    if (beforeCount > 1 || afterCount > 1)
    {
        *state = QStringLiteral("Ambiguous");
        return current;
    }
    if (afterCount == 1)
    {
        *state = QStringLiteral("Valid");
        return current;
    }
    *state = QStringLiteral("Missing");
    return current;
}

void updateNoteLocation(QJsonObject &anchor)
{
    const QString type = anchor.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("Note") && type != QStringLiteral("NoteGroup"))
        return;
    const QJsonArray targets = anchor.value(QStringLiteral("targets")).toArray();
    if (type == QStringLiteral("Note") && targets.size() != 1)
        return;
    for (const QJsonValue &targetValue : targets)
    {
        if (!targetValue.isObject())
            continue;
        const QJsonObject target = targetValue.toObject();
        const QString state = target.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("Missing") || state == QStringLiteral("Ambiguous"))
            continue;
        const QJsonObject note = target.value(QStringLiteral("note")).toObject();
        const QJsonArray beat = note.value(QStringLiteral("beat")).toArray();
        if (beat.size() == 3)
        {
            anchor.insert(QStringLiteral("beat"), beat);
            if (note.value(QStringLiteral("x")).isDouble())
                anchor.insert(QStringLiteral("x"), note.value(QStringLiteral("x")));
            const QJsonArray endBeat = note.value(QStringLiteral("endbeat")).toArray();
            if (endBeat.size() == 3)
            {
                anchor.insert(QStringLiteral("endBeat"), endBeat);
                if (note.value(QStringLiteral("x")).isDouble())
                    anchor.insert(QStringLiteral("endX"), note.value(QStringLiteral("x")));
            }
        }
        return;
    }
}
} // namespace

bool addComment(QJsonArray &comments, const QJsonObject &author, const QString &body, const QJsonObject &anchor,
                const QString &threadId, QString *newCommentId, QString *error)
{
    clearError(error);
    const QString authorId = author.value(QStringLiteral("authorId")).toString();
    QString authorName = author.value(QStringLiteral("authorNameSnapshot")).toString();
    if (authorName.isEmpty())
        authorName = author.value(QStringLiteral("authorName")).toString();
    if (authorId.isEmpty() || authorName.trimmed().isEmpty())
        return fail(error, QStringLiteral("A local author ID and name snapshot are required"));
    if (body.trimmed().isEmpty())
        return fail(error, QStringLiteral("Comment body cannot be empty"));
    if (anchor.isEmpty())
        return fail(error, QStringLiteral("Comment anchor is required"));

    QJsonObject authorSnapshot = author;
    authorSnapshot.insert(QStringLiteral("authorId"), authorId);
    authorSnapshot.insert(QStringLiteral("authorNameSnapshot"), authorName);

    QString resolvedThreadId = threadId;
    QJsonObject resolvedAnchor = sanitizeAnchor(anchor);
    bool resolved = false;
    if (!threadId.isEmpty())
    {
        bool foundThread = false;
        bool foundRootAnchor = false;
        QJsonObject rootAnchor;
        QJsonObject latestLiveAnchor;
        bool hasLiveAnchor = false;
        for (const QJsonValue &value : comments)
        {
            if (!value.isObject())
                continue;
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("threadId")).toString() != threadId)
                continue;
            foundThread = true;
            const bool isLive = !entry.value(QStringLiteral("tombstone")).toBool();
            if (entry.value(QStringLiteral("commentId")).toString() == threadId)
            {
                rootAnchor = sanitizeAnchor(entry.value(QStringLiteral("anchor")).toObject());
                foundRootAnchor = true;
            }
            if (isLive)
            {
                latestLiveAnchor = sanitizeAnchor(entry.value(QStringLiteral("anchor")).toObject());
                hasLiveAnchor = true;
                resolved = entry.value(QStringLiteral("state")).toString() == QStringLiteral("Resolved");
            }
        }
        if (!foundThread)
            return fail(error, QStringLiteral("Comment thread does not exist"));
        if (hasLiveAnchor)
            resolvedAnchor = latestLiveAnchor;
        else if (foundRootAnchor)
            resolvedAnchor = rootAnchor;
    }

    QString commentId = uuid();
    QSet<QString> usedIds;
    for (const QJsonValue &value : comments)
    {
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        usedIds.insert(entry.value(QStringLiteral("commentId")).toString());
        usedIds.insert(entry.value(QStringLiteral("threadId")).toString());
    }
    while (usedIds.contains(commentId))
        commentId = uuid();
    if (resolvedThreadId.isEmpty())
    {
        resolvedThreadId = commentId;
        resolved = false;
    }

    comments.append(makeComment(resolvedThreadId, commentId, authorSnapshot, body, resolvedAnchor, resolved));
    if (newCommentId)
        *newCommentId = commentId;
    return true;
}

bool editComment(QJsonArray &comments, const QString &commentId, const QString &activeAuthorId, const QString &body,
                 QString *error)
{
    clearError(error);
    if (body.trimmed().isEmpty())
        return fail(error, QStringLiteral("Comment body cannot be empty"));
    for (qsizetype i = 0; i < comments.size(); ++i)
    {
        if (!comments.at(i).isObject())
            continue;
        QJsonObject entry = comments.at(i).toObject();
        if (entry.value(QStringLiteral("commentId")).toString() != commentId)
            continue;
        if (entry.value(QStringLiteral("tombstone")).toBool())
            return fail(error, QStringLiteral("Deleted comments cannot be edited"));
        if (entry.value(QStringLiteral("author")).toObject().value(QStringLiteral("authorId")).toString()
            != activeAuthorId)
            return fail(error, QStringLiteral("Only the original author can edit this comment"));
        entry.insert(QStringLiteral("body"), body);
        entry.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        comments.replace(i, entry);
        return true;
    }
    return fail(error, QStringLiteral("Comment not found"));
}

bool deleteComment(QJsonArray &comments, const QString &commentId, const QString &activeAuthorId, QString *error)
{
    clearError(error);
    for (qsizetype i = 0; i < comments.size(); ++i)
    {
        if (!comments.at(i).isObject())
            continue;
        QJsonObject entry = comments.at(i).toObject();
        if (entry.value(QStringLiteral("commentId")).toString() != commentId)
            continue;
        if (entry.value(QStringLiteral("author")).toObject().value(QStringLiteral("authorId")).toString()
            != activeAuthorId)
            return fail(error, QStringLiteral("Only the original author can delete this comment"));
        if (!entry.value(QStringLiteral("tombstone")).toBool())
        {
            entry.insert(QStringLiteral("tombstone"), true);
            entry.insert("updatedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
            comments.replace(i, entry);
        }
        return true;
    }
    return fail(error, QStringLiteral("Comment not found"));
}

bool setThreadResolved(QJsonArray &comments, const QString &threadId, bool resolved, QString *error)
{
    clearError(error);
    bool found = false;
    const QString state = resolved ? QStringLiteral("Resolved") : QStringLiteral("Unresolved");
    for (qsizetype i = 0; i < comments.size(); ++i)
    {
        if (!comments.at(i).isObject())
            continue;
        QJsonObject entry = comments.at(i).toObject();
        if (entry.value(QStringLiteral("threadId")).toString() != threadId)
            continue;
        found = true;
        if (!entry.value(QStringLiteral("tombstone")).toBool())
        {
            entry.insert(QStringLiteral("state"), state);
            comments.replace(i, entry);
        }
    }
    return found || fail(error, QStringLiteral("Comment thread not found"));
}

QString anchorReferenceState(const QJsonObject &anchor)
{
    QString state = anchor.value("state").toString("Valid");
    const auto rank = [](const QString &value) {
        return value == "Missing" ? 3 : value == "Ambiguous" ? 2 : value == "Modified" ? 1 : 0;
    };
    for (const auto &value : anchor.value("targets").toArray())
    {
        const auto target = value.toObject().value("state").toString("Valid");
        if (rank(target) > rank(state))
            state = target;
    }
    return state;
}

QJsonArray relocateComments(const QJsonArray &comments, const CceDocument::Project &before,
                            const CceDocument::Project &after, const QVector<Change> &explicitChanges)
{
    if (comments.isEmpty())
        return comments;
    const ProjectIndex beforeIndex = buildIndex(before);
    const ProjectIndex afterIndex = buildIndex(after);
    const NoteChangeIndex noteChanges = buildNoteChangeIndex(explicitChanges);
    const BpmChangeIndex bpmChanges = buildBpmChangeIndex(explicitChanges);
    QJsonArray relocated;

    for (const QJsonValue &commentValue : comments)
    {
        if (!commentValue.isObject())
        {
            relocated.append(commentValue);
            continue;
        }
        QJsonObject comment = commentValue.toObject();
        if (comment.value(QStringLiteral("tombstone")).toBool())
        {
            relocated.append(comment);
            continue;
        }

        QJsonObject anchor = sanitizeAnchor(comment.value(QStringLiteral("anchor")).toObject());
        const QString type = anchor.value(QStringLiteral("type")).toString();
        const QString difficultyId = anchor.value(QStringLiteral("difficultyId")).toString();
        if (type == QStringLiteral("Note") || type == QStringLiteral("NoteGroup"))
        {
            QJsonArray targets;
            for (const QJsonValue &targetValue : anchor.value(QStringLiteral("targets")).toArray())
            {
                if (!targetValue.isObject())
                {
                    targets.append(targetValue);
                    continue;
                }
                QJsonObject target = targetValue.toObject();
                relocateNoteTarget(target, difficultyId, beforeIndex, afterIndex, noteChanges);
                targets.append(target);
            }
            anchor.insert(QStringLiteral("targets"), targets);
            updateNoteLocation(anchor);
        }
        else if (type == QStringLiteral("BpmEntry") && anchor.value(QStringLiteral("bpm")).isObject())
        {
            QString state;
            const QJsonObject updated = relocateBpm(anchor.value(QStringLiteral("bpm")).toObject(), difficultyId,
                                                    beforeIndex, afterIndex, bpmChanges, &state);
            anchor.insert(QStringLiteral("bpm"), updated);
            anchor.insert(QStringLiteral("state"), state);
            if (state != QStringLiteral("Missing") && state != QStringLiteral("Ambiguous"))
            {
                const QJsonArray beat = updated.value(QStringLiteral("beat")).toArray();
                if (beat.size() == 3)
                    anchor.insert(QStringLiteral("beat"), beat);
            }
        }
        else if (type == QStringLiteral("BpmList"))
        {
            const QJsonArray baseline = anchor.value(QStringLiteral("originalBpmList")).isArray()
                                            ? anchor.value(QStringLiteral("originalBpmList")).toArray()
                                            : timingSnapshot(before, difficultyId);
            const bool stillExists = after.difficulties.contains(difficultyId);
            if (stillExists)
                anchor.insert(QStringLiteral("bpmList"), timingSnapshot(after, difficultyId));
            anchor.insert(QStringLiteral("originalBpmList"), baseline);
            anchor.insert(QStringLiteral("state"), stillExists
                                                       ? arrayState(baseline, timingSnapshot(after, difficultyId))
                                                       : QStringLiteral("Missing"));
        }
        else if (type == QStringLiteral("BpmGroup"))
        {
            QJsonArray entries, states;
            for (const auto &value : anchor.value("bpmList").toArray())
            {
                QString state;
                entries.append(relocateBpm(value.toObject(), difficultyId, beforeIndex, afterIndex, bpmChanges, &state));
                states.append(QJsonObject{{"state", state}});
            }
            anchor["bpmList"] = entries;
            anchor["targets"] = states;
            if (!entries.isEmpty())
                anchor["beat"] = entries.first().toObject().value("beat");
        }
        else if (type == QStringLiteral("Metadata"))
        {
            const auto difficulty = after.difficulties.constFind(difficultyId);
            QJsonObject current;
            if (difficulty != after.difficulties.cend())
            {
                Chart chart;
                chart.meta() = difficulty->metadata;
                const auto metadata = CceDocument::encodeChart(chart).value("metadata").toObject();
                for (const auto &field : anchor.value("fields").toArray())
                    current[field.toString()] = metadata.value(field.toString());
                anchor["metadata"] = current;
            }
            anchor["state"] = difficulty == after.difficulties.cend() ? "Missing"
                              : canonicalHash(current) == canonicalHash(anchor.value("originalMetadata").toObject()) ? "Valid"
                                                                                                         : "Modified";
        }
        comment.insert(QStringLiteral("anchor"), anchor);
        relocated.append(comment);
    }
    return relocated;
}
} // namespace CceReview

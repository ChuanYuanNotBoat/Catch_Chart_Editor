#include "ChartController.h"
#include "file/ChartIO.h"
#include "utils/Logger.h"
#include "utils/PerformanceTimer.h"
#include <QUndoCommand>
#include <QList>
#include <QPair>
#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
    bool bpmLess(const BpmEntry &a, const BpmEntry &b)
    {
        if (a.beatNum != b.beatNum)
            return a.beatNum < b.beatNum;

        const double aPos = static_cast<double>(a.numerator) / a.denominator;
        const double bPos = static_cast<double>(b.numerator) / b.denominator;
        return aPos < bPos;
    }

    bool bpmExactEqual(const BpmEntry &a, const BpmEntry &b)
    {
        return a.beatNum == b.beatNum &&
               a.numerator == b.numerator &&
               a.denominator == b.denominator &&
               std::abs(a.bpm - b.bpm) < 1e-9;
    }

    bool bpmPositionEqual(const BpmEntry &a, const BpmEntry &b)
    {
        return a.beatNum == b.beatNum &&
               a.numerator == b.numerator &&
               a.denominator == b.denominator;
    }

    int findBpmExactIndex(const QVector<BpmEntry> &list, const BpmEntry &target)
    {
        for (int i = 0; i < list.size(); ++i)
        {
            if (bpmExactEqual(list[i], target))
                return i;
        }
        return -1;
    }

    int findBpmIndexByPosition(const QVector<BpmEntry> &list, const BpmEntry &target)
    {
        int bestIndex = -1;
        double bestDelta = std::numeric_limits<double>::max();
        for (int i = 0; i < list.size(); ++i)
        {
            if (!bpmPositionEqual(list[i], target))
                continue;
            const double delta = std::abs(list[i].bpm - target.bpm);
            if (delta < bestDelta)
            {
                bestDelta = delta;
                bestIndex = i;
            }
        }
        return bestIndex;
    }

    void sortBpmList(QVector<BpmEntry> &list)
    {
        std::sort(list.begin(), list.end(), bpmLess);
    }

    bool removeBpmByValue(Chart &chart, const BpmEntry &entry, int fallbackIndex)
    {
        QVector<BpmEntry> &list = chart.bpmList();
        int idx = findBpmExactIndex(list, entry);
        if (idx < 0)
            idx = findBpmIndexByPosition(list, entry);
        if (idx < 0 && fallbackIndex >= 0 && fallbackIndex < list.size())
            idx = fallbackIndex;
        if (idx < 0 || idx >= list.size())
            return false;

        list.removeAt(idx);
        return true;
    }

    bool replaceBpmByValue(Chart &chart, const BpmEntry &from, const BpmEntry &to, int fallbackIndex)
    {
        QVector<BpmEntry> &list = chart.bpmList();
        int idx = findBpmExactIndex(list, from);
        if (idx < 0)
            idx = findBpmIndexByPosition(list, from);
        if (idx < 0 && fallbackIndex >= 0 && fallbackIndex < list.size())
            idx = fallbackIndex;
        if (idx < 0 || idx >= list.size())
            return false;

        list[idx] = to;
        sortBpmList(list);
        return true;
    }

    QString noteSignature(const Note &note)
    {
        return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12")
            .arg(note.id)
            .arg(static_cast<int>(note.type))
            .arg(note.beatNum)
            .arg(note.numerator)
            .arg(note.denominator)
            .arg(note.x)
            .arg(note.endBeatNum)
            .arg(note.endNumerator)
            .arg(note.endDenominator)
            .arg(note.sound)
            .arg(note.vol)
            .arg(note.offset);
    }

    bool noteExactEqual(const Note &a, const Note &b)
    {
        return a.beatNum == b.beatNum &&
               a.numerator == b.numerator &&
               a.denominator == b.denominator &&
               a.id == b.id &&
               a.type == b.type &&
               a.x == b.x &&
               a.isRain == b.isRain &&
               a.endBeatNum == b.endBeatNum &&
               a.endNumerator == b.endNumerator &&
               a.endDenominator == b.endDenominator &&
               a.sound == b.sound &&
               a.vol == b.vol &&
               a.offset == b.offset;
    }

    bool notesEqual(const QVector<Note> &a, const QVector<Note> &b)
    {
        if (a.size() != b.size())
            return false;
        for (int index = 0; index < a.size(); ++index)
        {
            if (!noteExactEqual(a.at(index), b.at(index)))
                return false;
        }
        return true;
    }

    bool bpmListsEqual(const QVector<BpmEntry> &a, const QVector<BpmEntry> &b)
    {
        if (a.size() != b.size())
            return false;
        for (int index = 0; index < a.size(); ++index)
        {
            if (!bpmExactEqual(a.at(index), b.at(index)))
                return false;
        }
        return true;
    }

    bool metadataEqual(const MetaData &a, const MetaData &b)
    {
        return a.title == b.title &&
               a.titleOrg == b.titleOrg &&
               a.artist == b.artist &&
               a.artistOrg == b.artistOrg &&
               a.difficulty == b.difficulty &&
               a.chartAuthor == b.chartAuthor &&
               a.audioFile == b.audioFile &&
               a.backgroundFile == b.backgroundFile &&
               a.previewTime == b.previewTime &&
               std::abs(a.firstBpm - b.firstBpm) < 1e-9 &&
               a.offset == b.offset &&
               a.speed == b.speed;
    }

    bool resourceReferencesEqual(const Chart &a, const Chart &b)
    {
        return a.meta().audioFile == b.meta().audioFile &&
               a.meta().backgroundFile == b.meta().backgroundFile &&
               a.audioSourceFullPath() == b.audioSourceFullPath();
    }

    ChartChangeSet metadataChangeSet(const MetaData &before, const MetaData &after)
    {
        ChartChangeSet changes;
        if (metadataEqual(before, after))
            return changes;

        changes |= ChartChangeType::Metadata;
        if (before.offset != after.offset)
            changes |= ChartChangeType::Timing;
        if (before.audioFile != after.audioFile ||
            before.backgroundFile != after.backgroundFile)
        {
            changes |= ChartChangeType::Resources;
        }
        return changes;
    }

    ChartChangeSet chartChangeSet(const Chart &before, const Chart &after)
    {
        ChartChangeSet changes;
        if (!notesEqual(before.notes(), after.notes()))
            changes |= ChartChangeType::Notes;
        if (!bpmListsEqual(before.bpmList(), after.bpmList()) ||
            before.meta().offset != after.meta().offset)
        {
            changes |= ChartChangeType::Timing;
        }
        changes |= metadataChangeSet(before.meta(), after.meta());
        if (!resourceReferencesEqual(before, after))
            changes |= ChartChangeType::Resources;
        return changes;
    }

    bool isReferenceNoteValid(const Note &note)
    {
        // Remove/move-from notes are references to existing data.
        // We still require basic timeline/lane validity to block malformed payloads.
        return note.isTimeValid() && note.isXValid();
    }

    bool isTargetNoteValid(const Note &note)
    {
        // Add/move-to notes must be full valid notes before mutating chart data.
        return note.isValid() && note.isTimeValid() && note.isXValid();
    }

    QHash<QString, int> buildNoteInventory(const QVector<Note> &notes)
    {
        QHash<QString, int> inventory;
        for (const Note &note : notes)
        {
            const QString key = noteSignature(note);
            inventory[key] = inventory.value(key, 0) + 1;
        }
        return inventory;
    }

    bool consumeFromInventory(QHash<QString, int> *inventory, const Note &note)
    {
        if (!inventory)
            return false;
        const QString key = noteSignature(note);
        const int count = inventory->value(key, 0);
        if (count <= 0)
            return false;
        if (count == 1)
            inventory->remove(key);
        else
            (*inventory)[key] = count - 1;
        return true;
    }

    bool validateBatchEditPayload(const QVector<Note> &notesToAdd,
                                  const QVector<Note> &notesToRemove,
                                  const QList<QPair<Note, Note>> &notesToMove,
                                  const Chart &currentChart,
                                  QString *reason)
    {
        auto fail = [reason](const QString &msg) -> bool
        {
            if (reason)
                *reason = msg;
            return false;
        };

        if (notesToAdd.isEmpty() && notesToRemove.isEmpty() && notesToMove.isEmpty())
            return fail("Batch edit is empty.");

        constexpr int kMaxBatchOperations = 20000;
        const int totalOps = notesToAdd.size() + notesToRemove.size() + notesToMove.size();
        if (totalOps > kMaxBatchOperations)
        {
            return fail(QString("Batch edit too large (%1 ops > %2 limit).")
                            .arg(totalOps)
                            .arg(kMaxBatchOperations));
        }

        QHash<QString, int> sourceInventory = buildNoteInventory(currentChart.notes());

        QSet<QString> removeKeys;
        for (const Note &note : notesToRemove)
        {
            if (!isReferenceNoteValid(note))
                return fail("Invalid remove note detected.");
            if (!consumeFromInventory(&sourceInventory, note))
                return fail("Remove note does not exist in current chart.");
            removeKeys.insert(noteSignature(note));
        }

        QSet<QString> moveFromKeys;
        for (const auto &mv : notesToMove)
        {
            const Note &from = mv.first;
            const Note &to = mv.second;
            if (!isReferenceNoteValid(from))
                return fail("Invalid move source note detected.");
            if (!isTargetNoteValid(to))
                return fail("Invalid move target note detected.");

            const QString fromKey = noteSignature(from);
            if (moveFromKeys.contains(fromKey))
                return fail("Duplicated move source note detected.");
            moveFromKeys.insert(fromKey);

            if (removeKeys.contains(fromKey))
                return fail("Conflicting remove + move source detected.");
            if (!consumeFromInventory(&sourceInventory, from))
                return fail("Move source note does not exist in current chart.");
        }

        for (const Note &note : notesToAdd)
        {
            if (!isTargetNoteValid(note))
                return fail("Invalid add note detected.");
        }

        return true;
    }

    bool batchEditHasStableDeltaIdentity(const QVector<Note> &notesToAdd,
                                         const QVector<Note> &notesToRemove,
                                         const QList<QPair<Note, Note>> &notesToMove,
                                         const Chart &currentChart)
    {
        QHash<QString, int> existingIdCounts;
        for (const Note &note : currentChart.notes())
        {
            if (note.id.isEmpty())
                continue;
            existingIdCounts[note.id] = existingIdCounts.value(note.id, 0) + 1;
            if (existingIdCounts.value(note.id) > 1)
                return false;
        }
        QSet<QString> existingIds;
        for (auto it = existingIdCounts.constBegin(); it != existingIdCounts.constEnd(); ++it)
            existingIds.insert(it.key());

        QSet<QString> removeIds;
        for (const Note &note : notesToRemove)
        {
            if (note.id.isEmpty() || removeIds.contains(note.id))
                return false;
            removeIds.insert(note.id);
        }

        QSet<QString> moveIds;
        QSet<QString> targetIds;
        for (const auto &change : notesToMove)
        {
            const Note &from = change.first;
            const Note &to = change.second;
            // Keeping the stable identity unchanged makes the inverse delta
            // unambiguous even when the note ordering changes.
            if (from.id.isEmpty() || to.id.isEmpty() || from.id != to.id)
                return false;
            if (moveIds.contains(from.id) || targetIds.contains(to.id))
                return false;
            moveIds.insert(from.id);
            targetIds.insert(to.id);
            if (existingIds.contains(to.id) && !moveIds.contains(to.id))
                return false;
        }

        QSet<QString> addIds;
        for (const Note &note : notesToAdd)
        {
            if (note.id.isEmpty() || addIds.contains(note.id))
                return false;
            if (existingIds.contains(note.id) || removeIds.contains(note.id)
                || moveIds.contains(note.id) || targetIds.contains(note.id))
            {
                return false;
            }
            addIds.insert(note.id);
        }

        // A move must not write an ID belonging to an untouched note or to a
        // note removed by the same opaque operation. The conservative rule
        // keeps the inverse delta deterministic; the bounded snapshot path
        // below handles legacy/ambiguous payloads.
        for (const QString &id : targetIds)
        {
            if (existingIds.contains(id) && !moveIds.contains(id))
                return false;
        }
        return true;
    }
} // namespace

// 撤销命令基类
class ChartController::ChartCommand : public QUndoCommand
{
public:
    ChartCommand(ChartController *controller, const QString &text) : QUndoCommand(text), m_controller(controller) {}

protected:
    ChartController *m_controller;
};

// 添加单个音符命令
class ChartController::AddNoteCommand : public ChartController::ChartCommand
{
public:
    AddNoteCommand(ChartController *controller, const Note &note) : ChartCommand(controller, "Add Note"), m_note(note) {}
    void undo() override
    {
        m_controller->m_chart.removeNote(m_note);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }
    void redo() override
    {
        m_controller->m_chart.addNote(m_note);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    Note m_note;
};

// 批量添加音符命令（使用 QVector<Note>）
class ChartController::AddNotesCommand : public ChartController::ChartCommand
{
public:
    AddNotesCommand(ChartController *controller, const QVector<Note> &notes)
        : ChartCommand(controller, QString("Add %1 Notes").arg(notes.size())), m_notes(notes) {}
    void undo() override
    {
        m_controller->m_chart.removeNotes(m_notes);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }
    void redo() override
    {
        m_controller->m_chart.addNotes(m_notes);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    QVector<Note> m_notes;
};

// 删除单个音符命令
class ChartController::RemoveNoteCommand : public ChartController::ChartCommand
{
public:
    RemoveNoteCommand(ChartController *controller, const Note &note) : ChartCommand(controller, "Remove Note"), m_note(note) {}
    void undo() override
    {
        m_controller->m_chart.addNote(m_note);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }
    void redo() override
    {
        m_controller->m_chart.removeNote(m_note);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    Note m_note;
};

// 批量删除音符命令
class ChartController::RemoveNotesCommand : public ChartController::ChartCommand
{
public:
    RemoveNotesCommand(ChartController *controller, const QVector<Note> &notes)
        : ChartCommand(controller, QString("Remove %1 Notes").arg(notes.size())), m_notes(notes) {}
    void undo() override
    {
        m_controller->m_chart.addNotes(m_notes);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }
    void redo() override
    {
        m_controller->m_chart.removeNotes(m_notes);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    QVector<Note> m_notes;
};

// 移动单个音符命令
class ChartController::MoveNoteCommand : public ChartController::ChartCommand
{
public:
    MoveNoteCommand(ChartController *controller, const Note &original, const Note &newNote)
        : ChartCommand(controller, "Move Note"), m_original(original), m_new(newNote) {}
    void undo() override
    {
        m_controller->m_chart.replaceNotes(
            QList<QPair<Note, Note>>{qMakePair(m_new, m_original)});
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }
    void redo() override
    {
        m_controller->m_chart.replaceNotes(
            QList<QPair<Note, Note>>{qMakePair(m_original, m_new)});
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    Note m_original, m_new;
};

// 批量移动音符命令
class ChartController::MoveNotesCommand : public ChartController::ChartCommand
{
public:
    MoveNotesCommand(ChartController *controller, const QList<QPair<Note, Note>> &changes)
        : ChartCommand(controller, "Move Notes"), m_changes(changes) {}
    void undo() override
    {
        QList<QPair<Note, Note>> reverseChanges;
        reverseChanges.reserve(m_changes.size());
        for (const auto &change : m_changes)
            reverseChanges.append(qMakePair(change.second, change.first));
        m_controller->m_chart.replaceNotes(reverseChanges);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }
    void redo() override
    {
        m_controller->m_chart.replaceNotes(m_changes);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    QList<QPair<Note, Note>> m_changes;
};

class ChartController::BatchEditCommand : public ChartController::ChartCommand
{
public:
    BatchEditCommand(ChartController *controller,
                     const QString &actionName,
                     const QVector<Note> &notesToAdd,
                     const QVector<Note> &notesToRemove,
                     const QList<QPair<Note, Note>> &notesToMove)
        : ChartCommand(controller, actionName.isEmpty() ? QStringLiteral("Plugin Batch Edit") : actionName),
          m_notesToAdd(notesToAdd),
          m_notesToRemove(notesToRemove),
          m_notesToMove(notesToMove)
    {
    }

    void undo() override
    {
        QList<QPair<Note, Note>> reverseMoves;
        reverseMoves.reserve(m_notesToMove.size());
        for (const auto &change : m_notesToMove)
            reverseMoves.append(qMakePair(change.second, change.first));
        m_controller->m_chart.applyNoteBatch(m_notesToRemove, m_notesToAdd, reverseMoves);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

    void redo() override
    {
        m_controller->m_chart.applyNoteBatch(m_notesToAdd, m_notesToRemove, m_notesToMove);
        m_controller->publishChange(ChartChangeType::Notes);
        m_controller->notesChanged();
    }

private:
    QVector<Note> m_notesToAdd;
    QVector<Note> m_notesToRemove;
    QList<QPair<Note, Note>> m_notesToMove;
};

// 添加 BPM 命令
class ChartController::AddBpmCommand : public ChartController::ChartCommand
{
public:
    AddBpmCommand(ChartController *controller, const BpmEntry &bpm) : ChartCommand(controller, "Add BPM"), m_bpm(bpm) {}
    void undo() override
    {
        removeBpmByValue(m_controller->m_chart, m_bpm, m_controller->m_chart.bpmList().size() - 1);
        m_controller->publishChange(ChartChangeType::Timing);
        m_controller->bpmListChanged();
    }
    void redo() override
    {
        m_controller->m_chart.addBpm(m_bpm);
        m_controller->publishChange(ChartChangeType::Timing);
        m_controller->bpmListChanged();
    }

private:
    BpmEntry m_bpm;
};

// 删除 BPM 命令
class ChartController::RemoveBpmCommand : public ChartController::ChartCommand
{
public:
    RemoveBpmCommand(ChartController *controller, int index, const BpmEntry &bpm)
        : ChartCommand(controller, "Remove BPM"), m_index(index), m_bpm(bpm) {}
    void undo() override
    {
        m_controller->m_chart.addBpm(m_bpm);
        m_controller->publishChange(ChartChangeType::Timing);
        m_controller->bpmListChanged();
    }
    void redo() override
    {
        removeBpmByValue(m_controller->m_chart, m_bpm, m_index);
        m_controller->publishChange(ChartChangeType::Timing);
        m_controller->bpmListChanged();
    }

private:
    int m_index;
    BpmEntry m_bpm;
};

// 更新 BPM 命令
class ChartController::UpdateBpmCommand : public ChartController::ChartCommand
{
public:
    UpdateBpmCommand(ChartController *controller, int index, const BpmEntry &oldBpm, const BpmEntry &newBpm)
        : ChartCommand(controller, "Update BPM"), m_index(index), m_old(oldBpm), m_new(newBpm) {}
    void undo() override
    {
        replaceBpmByValue(m_controller->m_chart, m_new, m_old, m_index);
        m_controller->publishChange(ChartChangeType::Timing);
        m_controller->bpmListChanged();
    }
    void redo() override
    {
        replaceBpmByValue(m_controller->m_chart, m_old, m_new, m_index);
        m_controller->publishChange(ChartChangeType::Timing);
        m_controller->bpmListChanged();
    }

private:
    int m_index;
    BpmEntry m_old, m_new;
};

// 设置元数据命令
class ChartController::SetMetaCommand : public ChartController::ChartCommand
{
public:
    SetMetaCommand(ChartController *controller, const MetaData &oldMeta, const MetaData &newMeta)
        : ChartCommand(controller, "Edit Meta"),
          m_old(oldMeta),
          m_new(newMeta),
          m_changes(metadataChangeSet(oldMeta, newMeta)) {}
    void undo() override
    {
        m_controller->m_chart.meta() = m_old;
        m_controller->publishChange(m_changes);
        m_controller->metaDataChanged();
    }
    void redo() override
    {
        m_controller->m_chart.meta() = m_new;
        m_controller->publishChange(m_changes);
        m_controller->metaDataChanged();
    }

private:
    MetaData m_old, m_new;
    ChartChangeSet m_changes;
};

class ChartController::ExternalMutationCommand : public ChartController::ChartCommand
{
public:
    ExternalMutationCommand(ChartController *controller,
                            const QString &actionName,
                            const Chart &before,
                            const Chart &after)
        : ChartCommand(controller, actionName.isEmpty() ? "Plugin Mutation" : actionName),
          m_before(before),
          m_after(after),
          m_changes(chartChangeSet(before, after))
    {
    }

    void undo() override
    {
        m_controller->m_chart = m_before;
        m_controller->publishChange(m_changes);
        m_controller->notesChanged();
        m_controller->bpmListChanged();
        m_controller->metaDataChanged();
    }

    void redo() override
    {
        m_controller->m_chart = m_after;
        m_controller->publishChange(m_changes);
        m_controller->notesChanged();
        m_controller->bpmListChanged();
        m_controller->metaDataChanged();
    }

private:
    Chart m_before;
    Chart m_after;
    ChartChangeSet m_changes;
};

class ChartController::UndoMarkerCommand : public QUndoCommand
{
public:
    explicit UndoMarkerCommand(const QString &actionName)
        : QUndoCommand(actionName.isEmpty() ? QStringLiteral("Auxiliary Edit") : actionName)
    {
    }

    void undo() override {}
    void redo() override {}
};

// ---------- ChartController 实现 ----------
ChartController::ChartController(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<ChartChange>();
    m_undoStack = new QUndoStack(this);
}

ChartController::~ChartController()
{
}

void ChartController::publishChange(ChartChangeSet changes)
{
    if (changes)
    {
        ChartChange change;
        change.revision = ++m_revision;
        change.types = changes;
        emit chartChangeCommitted(change);
    }
    // Keep the historical signal available to consumers that only need a
    // dirty notification. The typed signal above is the authoritative source
    // for revision-aware cache invalidation.
    emit chartChanged();
}

void ChartController::addNote(const Note &note)
{
    m_undoStack->push(new AddNoteCommand(this, note));
}

void ChartController::addNotes(const QVector<Note> &notes)
{
    if (notes.isEmpty())
        return;
    m_undoStack->push(new AddNotesCommand(this, notes));
}

void ChartController::removeNote(const Note &note)
{
    int idx = m_chart.notes().indexOf(note);
    if (idx != -1)
        m_undoStack->push(new RemoveNoteCommand(this, note));
}

void ChartController::moveNote(const Note &original, const Note &newNote)
{
    if (original == newNote)
        return;
    m_undoStack->push(new MoveNoteCommand(this, original, newNote));
}

void ChartController::moveNotes(const QList<QPair<Note, Note>> &changes)
{
    if (changes.isEmpty())
        return;

    QString invalidReason;
    if (!validateBatchEditPayload(QVector<Note>{}, QVector<Note>{}, changes, m_chart, &invalidReason))
    {
        Logger::warn(QString("moveNotes rejected: %1").arg(invalidReason));
        return;
    }

    m_undoStack->push(new MoveNotesCommand(this, changes));
}

void ChartController::removeNotes(const QVector<Note> &notes)
{
    if (notes.isEmpty())
        return;
    Logger::debug(QString("[ChartController] removeNotes: pushing command for %1 notes").arg(notes.size()));
    m_undoStack->push(new RemoveNotesCommand(this, notes));
}

void ChartController::addBpm(const BpmEntry &bpm)
{
    m_undoStack->push(new AddBpmCommand(this, bpm));
}

void ChartController::removeBpm(int index)
{
    if (index >= 0 && index < m_chart.bpmList().size())
    {
        m_undoStack->push(new RemoveBpmCommand(this, index, m_chart.bpmList()[index]));
    }
}

void ChartController::updateBpm(int index, const BpmEntry &bpm)
{
    if (index >= 0 && index < m_chart.bpmList().size())
    {
        m_undoStack->push(new UpdateBpmCommand(this, index, m_chart.bpmList()[index], bpm));
    }
}

void ChartController::setMetaData(const MetaData &meta)
{
    m_undoStack->push(new SetMetaCommand(this, m_chart.meta(), meta));
}

void ChartController::undo()
{
    Logger::debug("ChartController::undo called");
    try
    {
        m_undoStack->undo();
        Logger::debug("ChartController::undo completed");
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::undo exception: %1").arg(e.what()));
        throw;
    }
    catch (...)
    {
        Logger::error("ChartController::undo unknown exception");
        throw;
    }
}

void ChartController::redo()
{
    Logger::debug("ChartController::redo called");
    try
    {
        m_undoStack->redo();
        Logger::debug("ChartController::redo completed");
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::redo exception: %1").arg(e.what()));
        throw;
    }
    catch (...)
    {
        Logger::error("ChartController::redo unknown exception");
        throw;
    }
}

bool ChartController::canUndo() const
{
    return m_undoStack->canUndo();
}

bool ChartController::canRedo() const
{
    return m_undoStack->canRedo();
}

QString ChartController::nextUndoActionText() const
{
    return m_undoStack ? m_undoStack->undoText() : QString();
}

QString ChartController::nextRedoActionText() const
{
    return m_undoStack ? m_undoStack->redoText() : QString();
}

bool ChartController::loadChart(const QString &path)
{
    PerformanceTimer loadTimer("ChartController::loadChart", "ui_operations");

    Logger::info(QString("ChartController::loadChart: Loading chart from %1").arg(path));
    try
    {
        Chart newChart;
        Logger::debug("ChartController::loadChart: Created new Chart object");

        if (ChartIO::load(path, newChart, false))
        {
            Logger::debug("ChartController::loadChart: ChartIO::load completed successfully");
            Logger::debug(QString("ChartController::loadChart: Chart has %1 notes").arg(newChart.notes().size()));
            return loadChartFromData(path, std::move(newChart));
        }
        Logger::error(QString("ChartController::loadChart: ChartIO::load failed for %1").arg(path));
        emit errorOccurred("Failed to load chart: " + path);
        return false;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::loadChart: Exception - %1").arg(e.what()));
        emit errorOccurred("Exception loading chart: " + QString::fromStdString(std::string(e.what())));
        return false;
    }
    catch (...)
    {
        Logger::error("ChartController::loadChart: Unknown exception");
        emit errorOccurred("Unknown exception loading chart");
        return false;
    }
}

bool ChartController::loadChartFromData(const QString &path, Chart loadedChart)
{
    PerformanceTimer loadTimer("ChartController::loadChartFromData", "ui_operations");

    Logger::info(QString("ChartController::loadChartFromData: Applying loaded chart for %1").arg(path));
    try
    {
        Logger::debug(QString("ChartController::loadChartFromData: Chart has %1 notes").arg(loadedChart.notes().size()));

        m_chart = std::move(loadedChart);
        m_currentChartPath = path;
        Logger::debug("ChartController::loadChartFromData: Chart assigned to m_chart, path saved");

        m_undoStack->clear();
        Logger::debug("ChartController::loadChartFromData: Undo stack cleared");

        ChartChangeSet allChanges;
        allChanges |= ChartChangeType::Notes;
        allChanges |= ChartChangeType::Timing;
        allChanges |= ChartChangeType::Metadata;
        allChanges |= ChartChangeType::Resources;
        publishChange(allChanges);
        emit notesChanged();
        emit bpmListChanged();
        emit metaDataChanged();
        emit chartLoaded();
        Logger::info("ChartController::loadChartFromData: Signals emitted");

        Logger::info(QString("ChartController::loadChartFromData: Successfully applied chart for %1").arg(path));
        return true;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::loadChartFromData: Exception - %1").arg(e.what()));
        emit errorOccurred("Exception applying loaded chart: " + QString::fromStdString(std::string(e.what())));
        return false;
    }
    catch (...)
    {
        Logger::error("ChartController::loadChartFromData: Unknown exception");
        emit errorOccurred("Unknown exception applying loaded chart");
        return false;
    }
}

bool ChartController::saveChart(const QString &path)
{
    PerformanceTimer saveTimer("ChartController::saveChart", "ui_operations");

    Logger::info(QString("ChartController::saveChart: Saving chart to %1").arg(path));
    Logger::debug(QString("ChartController::saveChart: Chart has %1 notes").arg(m_chart.notes().size()));

    try
    {
        if (ChartIO::save(path, m_chart))
        {
            Logger::info(QString("ChartController::saveChart: Successfully saved chart to %1").arg(path));
            return true;
        }
        Logger::error(QString("ChartController::saveChart: ChartIO::save failed for %1").arg(path));
        emit errorOccurred("Failed to save chart: " + path);
        return false;
    }
    catch (const std::exception &e)
    {
        Logger::error(QString("ChartController::saveChart: Exception - %1").arg(e.what()));
        emit errorOccurred("Exception saving chart: " + QString::fromStdString(std::string(e.what())));
        return false;
    }
    catch (...)
    {
        Logger::error("ChartController::saveChart: Unknown exception");
        emit errorOccurred("Unknown exception saving chart");
        return false;
    }
}

bool ChartController::applyExternalChartMutation(const QString &actionName, const Chart &mutatedChart)
{
    if (m_chart.notes().size() > kMaxOpaqueSnapshotNotes
        || mutatedChart.notes().size() > kMaxOpaqueSnapshotNotes)
    {
        Logger::warn(QString("applyExternalChartMutation rejected: opaque snapshot exceeds %1 notes.")
                         .arg(kMaxOpaqueSnapshotNotes));
        return false;
    }
    m_undoStack->push(new ExternalMutationCommand(this, actionName, m_chart, mutatedChart));
    return true;
}

void ChartController::pushUndoMarker(const QString &actionName)
{
    m_undoStack->push(new UndoMarkerCommand(actionName));
}

bool ChartController::applyBatchEdit(const QString &actionName,
                                     const QVector<Note> &notesToAdd,
                                     const QVector<Note> &notesToRemove,
                                     const QList<QPair<Note, Note>> &notesToMove)
{
    QString invalidReason;
    if (!validateBatchEditPayload(notesToAdd, notesToRemove, notesToMove, m_chart, &invalidReason))
    {
        Logger::warn(QString("applyBatchEdit rejected: %1").arg(invalidReason));
        return false;
    }

    const QString resolvedActionName = actionName.isEmpty() ? QStringLiteral("Plugin Batch Edit") : actionName;
    if (batchEditHasStableDeltaIdentity(notesToAdd, notesToRemove, notesToMove, m_chart))
    {
        m_undoStack->push(new BatchEditCommand(this,
                                               resolvedActionName,
                                               notesToAdd,
                                               notesToRemove,
                                               notesToMove));
        return true;
    }

    if (m_chart.notes().size() > kMaxOpaqueSnapshotNotes)
    {
        Logger::warn(QString("applyBatchEdit rejected: opaque fallback exceeds %1 notes.")
                         .arg(kMaxOpaqueSnapshotNotes));
        return false;
    }

    Chart mutated = m_chart;
    mutated.applyNoteBatch(notesToAdd, notesToRemove, notesToMove);

    m_undoStack->push(new ExternalMutationCommand(
        this,
        resolvedActionName,
        m_chart,
        mutated));
    return true;
}

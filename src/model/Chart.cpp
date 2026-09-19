#include "Chart.h"
#include "utils/Logger.h"
#include <QHash>
#include <QSet>
#include <algorithm>
#include <utility>

namespace
{
    bool bpmLess(const BpmEntry &a, const BpmEntry &b)
    {
        if (a.beatNum != b.beatNum)
            return a.beatNum < b.beatNum;
        double aPos = static_cast<double>(a.numerator) / a.denominator;
        double bPos = static_cast<double>(b.numerator) / b.denominator;
        return aPos < bPos;
    }

    void sortBpmList(QVector<BpmEntry> &list)
    {
        std::sort(list.begin(), list.end(), bpmLess);
    }

    bool noteLess(const Note &a, const Note &b)
    {
        if (a.beatNum != b.beatNum)
            return a.beatNum < b.beatNum;

        const double aPos = static_cast<double>(a.numerator) / a.denominator;
        const double bPos = static_cast<double>(b.numerator) / b.denominator;
        if (aPos != bPos)
            return aPos < bPos;

        if (a.type == NoteType::SOUND && b.type != NoteType::SOUND)
            return false;
        if (a.type != NoteType::SOUND && b.type == NoteType::SOUND)
            return true;

        return a.x < b.x;
    }
} // namespace

Chart::Chart() { clear(); }

void Chart::addNote(const Note &note)
{
    const auto position = std::upper_bound(m_notes.begin(), m_notes.end(), note,
                                           [](const Note &value, const Note &existing)
                                           { return noteLess(value, existing); });
    m_notes.insert(position, note);
}

void Chart::addNotes(const QVector<Note> &notes)
{
    if (notes.isEmpty())
        return;
    m_notes.reserve(m_notes.size() + notes.size());
    m_notes.append(notes);
    sortNotes();
}

void Chart::removeNote(int index)
{
    if (index >= 0 && index < m_notes.size())
        m_notes.removeAt(index);
}

void Chart::removeNote(const Note &note)
{
    // Prefer id match to avoid deleting a different note with same content.
    if (!note.id.isEmpty())
    {
        for (int i = 0; i < m_notes.size(); ++i)
        {
            if (m_notes[i].id == note.id)
            {
                m_notes.removeAt(i);
                return;
            }
        }
    }

    // Fallback for notes without id (legacy data).
    int idx = m_notes.indexOf(note);
    if (idx != -1)
    {
        m_notes.removeAt(idx);
        return;
    }

    Logger::debug(QString("[Chart] removeNote: failed to find note with id %1 for removal, beat %2")
                 .arg(note.id).arg(note.getStartBeat()));
}

void Chart::removeNotes(const QVector<Note> &notes)
{
    if (notes.isEmpty() || m_notes.isEmpty())
        return;

    QHash<QString, int> remainingIds;
    for (const Note &note : notes)
    {
        if (!note.id.isEmpty())
            remainingIds[note.id] = remainingIds.value(note.id, 0) + 1;
    }

    if (!remainingIds.isEmpty())
    {
        m_notes.erase(std::remove_if(m_notes.begin(), m_notes.end(),
                                     [&remainingIds](const Note &current)
                                     {
            auto it = remainingIds.find(current.id);
            if (it == remainingIds.end() || it.value() <= 0)
                return false;
            --it.value();
            return true;
        }), m_notes.end());
    }

    // Compatibility fallback for legacy/id-mismatched references. This path
    // is intentionally secondary; normal editor data uses stable IDs and is
    // removed in one linear pass above.
    for (const Note &target : notes)
    {
        if (!target.id.isEmpty() && remainingIds.value(target.id, 0) <= 0)
            continue;
        const auto found = std::find(m_notes.begin(), m_notes.end(), target);
        if (found != m_notes.end())
        {
            m_notes.erase(found);
            if (!target.id.isEmpty())
                remainingIds[target.id] = qMax(0, remainingIds.value(target.id) - 1);
        }
    }
}

void Chart::replaceNotes(const QList<QPair<Note, Note>> &changes)
{
    if (changes.isEmpty() || m_notes.isEmpty())
        return;

    QHash<QString, Note> replacementById;
    for (const auto &change : changes)
    {
        if (!change.first.id.isEmpty())
            replacementById.insert(change.first.id, change.second);
    }

    QSet<QString> appliedIds;
    for (Note &current : m_notes)
    {
        const auto replacement = replacementById.constFind(current.id);
        if (replacement == replacementById.constEnd())
            continue;
        current = replacement.value();
        appliedIds.insert(replacement.key());
    }

    for (const auto &change : changes)
    {
        if (!change.first.id.isEmpty() && appliedIds.contains(change.first.id))
            continue;
        const auto found = std::find(m_notes.begin(), m_notes.end(), change.first);
        if (found != m_notes.end())
            *found = change.second;
    }
    sortNotes();
}

void Chart::applyNoteBatch(const QVector<Note> &notesToAdd,
                           const QVector<Note> &notesToRemove,
                           const QList<QPair<Note, Note>> &notesToMove)
{
    bool hasOnlyStableReferences = true;
    QSet<QString> removeIds;
    QHash<QString, Note> moveById;
    for (const Note &note : notesToRemove)
    {
        if (note.id.isEmpty())
            hasOnlyStableReferences = false;
        else
            removeIds.insert(note.id);
    }
    for (const auto &change : notesToMove)
    {
        if (change.first.id.isEmpty())
            hasOnlyStableReferences = false;
        else
            moveById.insert(change.first.id, change.second);
    }

    if (hasOnlyStableReferences)
    {
        QVector<Note> result;
        result.reserve(m_notes.size() - qMin(m_notes.size(), notesToRemove.size()) + notesToAdd.size());
        for (const Note &current : std::as_const(m_notes))
        {
            if (removeIds.contains(current.id))
                continue;
            const auto moved = moveById.constFind(current.id);
            result.append(moved == moveById.constEnd() ? current : moved.value());
        }
        result.append(notesToAdd);
        m_notes = std::move(result);
        sortNotes();
        return;
    }

    // Legacy charts without IDs are rare; preserve their content-based
    // matching semantics while still sorting only at the mutation boundary.
    removeNotes(notesToRemove);
    replaceNotes(notesToMove);
    m_notes.append(notesToAdd);
    sortNotes();
}

void Chart::setNotes(QVector<Note> notes)
{
    m_notes = std::move(notes);
    sortNotes();
}

void Chart::clearNotes() { m_notes.clear(); }

const QVector<Note> &Chart::notes() const { return m_notes; }
QVector<Note> &Chart::notes() { return m_notes; }

void Chart::addBpm(const BpmEntry &bpm)
{
    m_bpmList.append(bpm);
    sortBpmList(m_bpmList);
}

void Chart::removeBpm(int index)
{
    if (index >= 0 && index < m_bpmList.size())
        m_bpmList.removeAt(index);
}

void Chart::updateBpm(int index, const BpmEntry &bpm)
{
    if (index >= 0 && index < m_bpmList.size())
    {
        m_bpmList[index] = bpm;
        sortBpmList(m_bpmList);
    }
}

const QVector<BpmEntry> &Chart::bpmList() const { return m_bpmList; }
QVector<BpmEntry> &Chart::bpmList() { return m_bpmList; }

MetaData &Chart::meta() { return m_meta; }
const MetaData &Chart::meta() const { return m_meta; }

void Chart::sortNotes()
{
    std::sort(m_notes.begin(), m_notes.end(), noteLess);
}

bool Chart::isValid() const { return !m_notes.isEmpty() || m_bpmList.size() >= 1; }

void Chart::clear()
{
    m_notes.clear();
    m_bpmList.clear();
    m_meta = MetaData();
    m_bpmList.append(BpmEntry(0, 1, 1, 120.0));
    m_audioSourceFullPath.clear();
}

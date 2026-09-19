#include "SelectionController.h"
#include <QDebug>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <algorithm>
#include <utility>

SelectionController::SelectionController(QObject *parent) : QObject(parent)
{
}

QSet<int> SelectionController::selectedIndices() const
{
    if (!m_selectedIndicesDirty)
        return m_selectedIndicesCache;

    m_selectedIndicesCache.clear();
    if (!m_notes || m_selectedIds.isEmpty())
    {
        m_selectedIndicesDirty = false;
        m_selectedIndicesRevision = m_notesRevision;
        return m_selectedIndicesCache;
    }

    if (m_noteIndexDirty)
        rebuildNoteIndex();

    for (const QString &id : std::as_const(m_selectedIds))
    {
        const auto indices = m_noteIndicesById.constFind(id);
        if (indices == m_noteIndicesById.constEnd())
            continue;
        for (int index : indices.value())
            m_selectedIndicesCache.insert(index);
    }
    m_selectedIndicesDirty = false;
    m_selectedIndicesRevision = m_notesRevision;
    return m_selectedIndicesCache;
}

void SelectionController::setNotes(const QVector<Note> *notes, quint64 revision)
{
    const bool sameRevision = m_notes == notes && revision != 0 &&
                              revision == m_notesRevision;
    m_notes = notes;
    if (revision != 0)
        m_notesRevision = revision;
    if (sameRevision)
        return;
    m_noteIndexDirty = true;
    rebuildNoteIndex();
    m_selectedIndicesDirty = true;
}

void SelectionController::select(int index)
{
    if (!m_notes || index < 0 || index >= m_notes->size())
        return;
    m_selectedIds.clear();
    m_selectedIds.insert((*m_notes)[index].id);
    m_selectedIndicesDirty = true;
    emit selectionChanged(selectedIndices());
}

void SelectionController::select(const QSet<int> &indices)
{
    if (!m_notes)
        return;
    m_selectedIds.clear();
    for (int idx : indices)
    {
        if (idx >= 0 && idx < m_notes->size())
            m_selectedIds.insert((*m_notes)[idx].id);
    }
    m_selectedIndicesDirty = true;
    emit selectionChanged(selectedIndices());
}

void SelectionController::addToSelection(int index)
{
    if (!m_notes || index < 0 || index >= m_notes->size())
        return;
    m_selectedIds.insert((*m_notes)[index].id);
    m_selectedIndicesDirty = true;
    emit selectionChanged(selectedIndices());
}

void SelectionController::removeFromSelection(int index)
{
    if (!m_notes || index < 0 || index >= m_notes->size())
        return;
    m_selectedIds.remove((*m_notes)[index].id);
    m_selectedIndicesDirty = true;
    emit selectionChanged(selectedIndices());
}

void SelectionController::clearSelection()
{
    if (!m_selectedIds.isEmpty())
    {
        m_selectedIds.clear();
        m_selectedIndicesDirty = true;
        emit selectionChanged(selectedIndices());
    }
}

void SelectionController::selectInRect(const QRectF &rect, const QVector<Note> &notes,
                                       std::function<QPointF(const Note &)> noteToPos)
{
    QVector<int> allIndices;
    allIndices.reserve(notes.size());
    for (int i = 0; i < notes.size(); ++i)
        allIndices.append(i);
    selectInRect(rect, notes, allIndices, std::move(noteToPos));
}

void SelectionController::selectInRect(const QRectF &rect, const QVector<Note> &notes,
                                       const QVector<int> &candidateIndices,
                                       std::function<QPointF(const Note &)> noteToPos)
{
    QSet<int> newSelection;
    for (int i : candidateIndices)
    {
        if (i < 0 || i >= notes.size())
            continue;
        QPointF pos = noteToPos(notes[i]);
        if (rect.contains(pos))
        {
            newSelection.insert(i);
        }
    }
    select(newSelection);
}

void SelectionController::selectInBeatRange(double startBeat, double endBeat)
{
    if (!m_notes)
        return;

    const double rangeStart = qMin(startBeat, endBeat);
    const double rangeEnd = qMax(startBeat, endBeat);
    const QVector<int> candidates = noteIndicesInBeatRange(rangeStart, rangeEnd);
    QSet<int> newSelection;
    for (int index : candidates)
    {
        const Note &note = (*m_notes)[index];
        if (note.isRain && note.getEndBeat() > rangeEnd)
            continue;
        newSelection.insert(index);
    }
    select(newSelection);
}

QVector<int> SelectionController::noteIndicesInBeatRange(double startBeat, double endBeat) const
{
    if (!m_notes)
        return {};
    if (m_noteIndexDirty)
        rebuildNoteIndex();
    if (endBeat < startBeat)
        std::swap(startBeat, endBeat);

    const auto lower = std::lower_bound(
        m_sortedNoteIndicesByBeat.cbegin(), m_sortedNoteIndicesByBeat.cend(), startBeat,
        [this](int index, double beat) {
            return m_notes->at(index).getStartBeat() < beat;
        });
    const auto upper = std::upper_bound(
        m_sortedNoteIndicesByBeat.cbegin(), m_sortedNoteIndicesByBeat.cend(), endBeat,
        [this](double beat, int index) {
            return beat < m_notes->at(index).getStartBeat();
        });
    return QVector<int>(lower, upper);
}

void SelectionController::rebuildNoteIndex() const
{
    m_sortedNoteIndicesByBeat.clear();
    m_noteIndicesById.clear();
    if (!m_notes)
    {
        m_noteIndexDirty = false;
        return;
    }

    m_sortedNoteIndicesByBeat.reserve(m_notes->size());
    for (int i = 0; i < m_notes->size(); ++i)
    {
        m_sortedNoteIndicesByBeat.append(i);
        m_noteIndicesById[(*m_notes)[i].id].append(i);
    }
    std::sort(m_sortedNoteIndicesByBeat.begin(), m_sortedNoteIndicesByBeat.end(),
              [this](int left, int right) {
        const Note &leftNote = m_notes->at(left);
        const Note &rightNote = m_notes->at(right);
        if (leftNote.getStartBeat() != rightNote.getStartBeat())
            return leftNote.getStartBeat() < rightNote.getStartBeat();
        if (leftNote.x != rightNote.x)
            return leftNote.x < rightNote.x;
        return left < right;
    });
    m_noteIndexDirty = false;
}

void SelectionController::copySelected(const QVector<Note> &notes)
{
    m_clipboard.clear();
    QSet<int> indices = selectedIndices();
    for (int idx : indices)
    {
        if (idx >= 0 && idx < notes.size())
            m_clipboard.append(notes[idx]);
    }
}

void SelectionController::updateSelectionFromNotes(quint64 revision)
{
    if (revision != 0)
        m_notesRevision = revision;
    if (revision != 0 &&
        revision == m_selectedIndicesRevision &&
        !m_selectedIndicesDirty)
    {
        return;
    }
    // 音符列表变化后，重新计算选中索引并发出信号（画布依赖索引）
    m_noteIndexDirty = true;
    rebuildNoteIndex();
    m_selectedIndicesDirty = true;
    emit selectionChanged(selectedIndices());
}

QVector<Note> SelectionController::getClipboard() const
{
    return m_clipboard;
}

void SelectionController::clearClipboard()
{
    m_clipboard.clear();
}

void SelectionController::setClipboard(const QVector<Note> &notes)
{
    m_clipboard = notes;
}

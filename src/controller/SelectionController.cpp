#include "SelectionController.h"
#include <QDebug>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>

SelectionController::SelectionController(QObject *parent) : QObject(parent)
{
}

QSet<int> SelectionController::selectedIndices() const
{
    if (!m_selectedIndicesDirty)
        return m_selectedIndicesCache;

    m_selectedIndicesCache.clear();
    if (!m_notes)
    {
        m_selectedIndicesDirty = false;
        return m_selectedIndicesCache;
    }

    for (qsizetype i = 0; i < m_notes->size(); ++i)
    {
        if (m_selectedIds.contains((*m_notes)[i].id))
            m_selectedIndicesCache.insert(static_cast<int>(i));
    }
    m_selectedIndicesDirty = false;
    return m_selectedIndicesCache;
}

void SelectionController::setNotes(const QVector<Note> *notes)
{
    m_notes = notes;
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
    QSet<int> newSelection;
    for (int i = 0; i < notes.size(); ++i)
    {
        QPointF pos = noteToPos(notes[i]);
        if (rect.contains(pos))
        {
            newSelection.insert(i);
        }
    }
    select(newSelection);
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

void SelectionController::updateSelectionFromNotes()
{
    // 音符列表变化后，重新计算选中索引并发出信号（画布依赖索引）
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

// src/controller/SelectionController.h
#pragma once

#include <QObject>
#include <QSet>
#include <QHash>
#include <QVector>
#include <QRectF>
#include <functional>
#include "model/Note.h"

/**
 * @brief 管理音符选中状态。
 * @note 主线程调用。
 */
class SelectionController : public QObject
{
    Q_OBJECT
public:
    explicit SelectionController(QObject *parent = nullptr);

    QSet<int> selectedIndices() const;

    void setNotes(const QVector<Note> *notes, quint64 revision = 0);
    void select(int index);
    void select(const QSet<int> &indices);
    void addToSelection(int index);
    void removeFromSelection(int index);
    void clearSelection();

    void selectInRect(const QRectF &rect, const QVector<Note> &notes,
                      std::function<QPointF(const Note &)> noteToPos);
    void selectInRect(const QRectF &rect, const QVector<Note> &notes,
                      const QVector<int> &candidateIndices,
                      std::function<QPointF(const Note &)> noteToPos);
    void selectInBeatRange(double startBeat, double endBeat);
    QVector<int> noteIndicesInBeatRange(double startBeat, double endBeat) const;

    void copySelected(const QVector<Note> &notes); // 复制当前选中的音符到剪贴板
    void setClipboard(const QVector<Note> &notes);
    QVector<Note> getClipboard() const;
    void clearClipboard();

    // 当音符列表变化时调用，根据存储的 ID 重新计算选中的索引
    void updateSelectionFromNotes(quint64 revision = 0);

signals:
    void selectionChanged(const QSet<int> &selectedIndices);

private:
    void rebuildNoteIndex() const;

    QSet<QString> m_selectedIds;            // 存储选中音符的 ID
    const QVector<Note> *m_notes = nullptr; // 指向当前音符列表，用于转换
    QVector<Note> m_clipboard;
    mutable QSet<int> m_selectedIndicesCache;
    mutable bool m_selectedIndicesDirty = true;
    mutable QVector<int> m_sortedNoteIndicesByBeat;
    mutable QHash<QString, QVector<int>> m_noteIndicesById;
    mutable bool m_noteIndexDirty = true;
    quint64 m_notesRevision = 0;
    mutable quint64 m_selectedIndicesRevision = 0;
};

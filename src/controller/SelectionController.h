// src/controller/SelectionController.h
#pragma once

#include <QObject>
#include <QSet>
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

    void setNotes(const QVector<Note> *notes);
    void select(int index);
    void select(const QSet<int> &indices);
    void addToSelection(int index);
    void removeFromSelection(int index);
    void clearSelection();

    void selectInRect(const QRectF &rect, const QVector<Note> &notes,
                      std::function<QPointF(const Note &)> noteToPos);

    void copySelected(const QVector<Note> &notes); // 复制当前选中的音符到剪贴板
    QVector<Note> getClipboard() const;
    void clearClipboard();

    // 当音符列表变化时调用，根据存储的 ID 重新计算选中的索引
    void updateSelectionFromNotes();

signals:
    void selectionChanged(const QSet<int> &selectedIndices);

private:
    QSet<QString> m_selectedIds;            // 存储选中音符的 ID
    const QVector<Note> *m_notes = nullptr; // 指向当前音符列表，用于转换
    QVector<Note> m_clipboard;
    mutable QSet<int> m_selectedIndicesCache;
    mutable bool m_selectedIndicesDirty = true;
};

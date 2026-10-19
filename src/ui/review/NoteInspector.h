#pragma once

#include "model/Note.h"

#include <QPair>
#include <QWidget>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class ChartController;
class SelectionController;

namespace CceReviewUi
{
class NoteInspector : public QWidget
{
    Q_OBJECT

public:
    explicit NoteInspector(ChartController *chartController, SelectionController *selectionController,
                           QWidget *parent = nullptr);

    void setComparison(const QVector<QPair<Note, Note>> &comparison);
    void clearComparison();
    void setReadOnly(bool readOnly);
    void setDisplayNotes(const QVector<Note> *notes);
    bool isReadOnly() const;

private slots:
    void refresh();
    void applyChanges();

private:
    enum class Field
    {
        StartBeat,
        X,
        EndBeat,
        Sound,
        Volume,
        Offset
    };

    struct FieldWidgets
    {
        QWidget *row = nullptr;
        QLabel *label = nullptr;
        QLineEdit *editor = nullptr;
    };

    void buildUi();
    void updateEditor(Field field, const QVector<Note> &notes, bool supported);
    QString valueFor(Field field, const Note &note) const;
    QString commonValue(Field field, const QVector<Note> &notes) const;
    void showError(const QString &message);
    QVector<Note> selectedNotes() const;
    bool comparisonMode() const
    {
        return !m_comparison.isEmpty();
    }

    ChartController *m_chartController = nullptr;
    SelectionController *m_selectionController = nullptr;
    QVector<QPair<Note, Note>> m_comparison;
    const QVector<Note> *m_displayNotes = nullptr;
    bool m_localReadOnly = false;
    bool m_refreshing = false;

    QLabel *m_typeValue = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_applyButton = nullptr;
    FieldWidgets m_startBeat;
    FieldWidgets m_x;
    FieldWidgets m_endBeat;
    FieldWidgets m_sound;
    FieldWidgets m_volume;
    FieldWidgets m_offset;
};
} // namespace CceReviewUi

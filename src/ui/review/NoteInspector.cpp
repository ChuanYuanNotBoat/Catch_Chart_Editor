#include "NoteInspector.h"

#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "model/BeatPosition.h"
#include "model/Chart.h"

#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace CceReviewUi
{
namespace
{
QString typeName(NoteType type)
{
    switch (type)
    {
        case NoteType::NORMAL:
            return QObject::tr("Normal");
        case NoteType::RAIN:
            return QObject::tr("Rain");
        case NoteType::SOUND:
            return QObject::tr("Sound");
    }
    return QObject::tr("Unknown");
}

bool parseBeatTriplet(const QString &text, int *whole, int *numerator, int *denominator)
{
    static const QRegularExpression expression(QStringLiteral("^\\s*(-?\\d+)\\s*:\\s*(-?\\d+)\\s*/\\s*(-?\\d+)\\s*$"));
    const auto match = expression.match(text);
    if (!match.hasMatch())
        return false;
    bool wholeOk = false;
    bool numeratorOk = false;
    bool denominatorOk = false;
    *whole = match.captured(1).toInt(&wholeOk);
    *numerator = match.captured(2).toInt(&numeratorOk);
    *denominator = match.captured(3).toInt(&denominatorOk);
    return wholeOk && numeratorOk && denominatorOk && *numerator >= 0 && *denominator > 0;
}

QString beatText(int whole, int numerator, int denominator)
{
    return QStringLiteral("%1:%2/%3").arg(whole).arg(numerator).arg(denominator);
}

QString aggregate(const QVector<QString> &values)
{
    if (values.isEmpty())
        return {};
    const QString first = values.first();
    for (const QString &value : values)
    {
        if (value != first)
            return QObject::tr("Mixed");
    }
    return first;
}

bool anyType(const QVector<Note> &notes, NoteType type)
{
    return std::any_of(notes.cbegin(), notes.cend(), [type](const Note &note) {
        return note.type == type;
    });
}

QString pairValues(const QVector<QPair<Note, Note>> &pairs, bool before,
                   const std::function<QString(const Note &)> &valueForNote)
{
    QStringList values;
    values.reserve(pairs.size());
    for (const auto &pair : pairs)
        values.append(valueForNote(before ? pair.first : pair.second));
    return values.join(QStringLiteral(", "));
}
} // namespace

NoteInspector::NoteInspector(ChartController *chartController, SelectionController *selectionController,
                             QWidget *parent)
    : QGroupBox(tr("Note Properties"), parent), m_chartController(chartController), m_selectionController(selectionController)
{
    setObjectName(QStringLiteral("reviewNoteInspector"));
    buildUi();
    if (m_selectionController)
        connect(m_selectionController, &SelectionController::selectionChanged, this, &NoteInspector::refresh);
    if (m_chartController)
    {
        connect(m_chartController, &ChartController::chartChangeCommitted, this, [this](const ChartChange &) {
            if (comparisonMode())
                clearComparison();
            else
                refresh();
        });
        connect(m_chartController, &ChartController::chartLoaded, this, [this] {
            clearComparison();
            refresh();
        });
    }
    refresh();
}

void NoteInspector::changeEvent(QEvent *event)
{
    QGroupBox::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        setTitle(tr("Note Properties"));
}

void NoteInspector::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_typeValue = new QLabel(this);
    m_typeValue->setObjectName(QStringLiteral("reviewNoteTypeValue"));
    form->addRow(tr("Type:"), m_typeValue);

    const auto addField = [this, form](FieldWidgets *field, const QString &label, const QString &objectName) {
        field->label = new QLabel(label, this);
        field->label->setWordWrap(true);
        field->editor = new QLineEdit(this);
        field->editor->setObjectName(objectName);
        field->editor->setClearButtonEnabled(true);
        form->addRow(field->label, field->editor);
    };
    addField(&m_startBeat, tr("Beat (whole:numerator/denominator):"), QStringLiteral("reviewNoteBeatEdit"));
    addField(&m_x, tr("X (0–512):"), QStringLiteral("reviewNoteXEdit"));
    addField(&m_endBeat, tr("Rain end beat:"), QStringLiteral("reviewNoteRainEndEdit"));
    addField(&m_sound, tr("Sound file:"), QStringLiteral("reviewNoteSoundEdit"));
    addField(&m_volume, tr("Sound volume (0–100):"), QStringLiteral("reviewNoteVolumeEdit"));
    addField(&m_offset, tr("Sound offset (ms):"), QStringLiteral("reviewNoteOffsetEdit"));
    layout->addLayout(form);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("reviewNoteInspectorStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #b42318"));
    layout->addWidget(m_statusLabel);

    m_applyButton = new QPushButton(tr("Apply"), this);
    m_applyButton->setObjectName(QStringLiteral("reviewNoteApplyButton"));
    connect(m_applyButton, &QPushButton::clicked, this, &NoteInspector::applyChanges);
    layout->addWidget(m_applyButton);
}

QVector<Note> NoteInspector::selectedNotes() const
{
    QVector<Note> result;
    if (!m_chartController || !m_selectionController || !m_chartController->chart())
        return result;
    const QVector<Note> &notes = m_displayNotes ? *m_displayNotes : m_chartController->chart()->notes();
    QList<int> indices = m_selectionController->selectedIndices().values();
    std::sort(indices.begin(), indices.end());
    for (int index : indices)
    {
        if (index >= 0 && index < notes.size())
            result.append(notes.at(index));
    }
    return result;
}

QString NoteInspector::valueFor(Field field, const Note &note) const
{
    switch (field)
    {
        case Field::StartBeat:
            return beatText(note.beatNum, note.numerator, note.denominator);
        case Field::X:
            return note.isSound() || note.isRainNote() ? tr("—") : QString::number(note.x);
        case Field::EndBeat:
            return note.isRainNote() ? beatText(note.endBeatNum, note.endNumerator, note.endDenominator) : tr("—");
        case Field::Sound:
            return note.isSound() ? note.sound : tr("—");
        case Field::Volume:
            return note.isSound() ? QString::number(note.vol) : tr("—");
        case Field::Offset:
            return note.isSound() ? QString::number(note.offset) : tr("—");
    }
    return {};
}

QString NoteInspector::commonValue(Field field, const QVector<Note> &notes) const
{
    QVector<QString> values;
    values.reserve(notes.size());
    for (const Note &note : notes)
        values.append(valueFor(field, note));
    return aggregate(values);
}

void NoteInspector::updateEditor(Field field, const QVector<Note> &notes, bool supported)
{
    FieldWidgets *widgets = nullptr;
    switch (field)
    {
        case Field::StartBeat:
            widgets = &m_startBeat;
            break;
        case Field::X:
            widgets = &m_x;
            break;
        case Field::EndBeat:
            widgets = &m_endBeat;
            break;
        case Field::Sound:
            widgets = &m_sound;
            break;
        case Field::Volume:
            widgets = &m_volume;
            break;
        case Field::Offset:
            widgets = &m_offset;
            break;
    }
    if (!widgets)
        return;

    widgets->label->setVisible(supported);
    widgets->editor->setVisible(supported);
    widgets->editor->setEnabled(supported && !isReadOnly());
    widgets->editor->setStyleSheet(QString());
    if (!supported)
    {
        widgets->editor->clear();
        widgets->editor->setPlaceholderText(tr("Not applicable to all selected notes"));
        widgets->editor->setProperty("reviewInitialText", QString());
        return;
    }

    const QString value = commonValue(field, notes);
    const bool mixed = value == tr("Mixed");
    widgets->editor->setText(mixed ? QString() : value);
    widgets->editor->setPlaceholderText(mixed ? tr("Mixed") : QString());
    widgets->editor->setProperty("reviewInitialText", mixed ? QString() : value);
    widgets->editor->setProperty("reviewWasMixed", mixed);
    widgets->editor->setReadOnly(isReadOnly());
}

void NoteInspector::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    m_statusLabel->clear();

    if (comparisonMode())
    {
        QVector<Note> before;
        QVector<Note> after;
        before.reserve(m_comparison.size());
        after.reserve(m_comparison.size());
        for (const auto &pair : m_comparison)
        {
            before.append(pair.first);
            after.append(pair.second);
        }
        QVector<QString> beforeTypes;
        for (const Note &note : before)
            beforeTypes.append(typeName(note.type));
        const QString beforeType = aggregate(beforeTypes);
        const QString afterType = [&] {
            QVector<QString> types;
            for (const Note &note : after)
                types.append(typeName(note.type));
            return aggregate(types);
        }();
        m_typeValue->setText(tr("Before: %1   After: %2").arg(beforeType, afterType));

        const auto setComparisonField = [this](FieldWidgets *widgets, Field field, const QString &name, bool relevant) {
            widgets->label->setVisible(relevant);
            widgets->editor->setVisible(relevant);
            if (!relevant)
                return;
            const QString beforeValue = pairValues(m_comparison, true, [this, field](const Note &note) {
                return valueFor(field, note);
            });
            const QString afterValue = pairValues(m_comparison, false, [this, field](const Note &note) {
                return valueFor(field, note);
            });
            widgets->label->setText(name);
            widgets->editor->setReadOnly(true);
            widgets->editor->setEnabled(false);
            widgets->editor->setPlaceholderText(QString());
            widgets->editor->setText(tr("Before: %1    After: %2").arg(beforeValue, afterValue));
            const bool changed =
                std::any_of(m_comparison.cbegin(), m_comparison.cend(), [this, field](const auto &pair) {
                    return valueFor(field, pair.first) != valueFor(field, pair.second);
                });
            widgets->editor->setStyleSheet(changed ? QStringLiteral("font-weight: bold; color: #b54708") : QString());
        };
        setComparisonField(&m_startBeat, Field::StartBeat, tr("Beat"), true);
        setComparisonField(&m_x, Field::X, tr("X"),
                           anyType(before, NoteType::NORMAL) || anyType(before, NoteType::RAIN)
                               || anyType(after, NoteType::NORMAL) || anyType(after, NoteType::RAIN));
        setComparisonField(&m_endBeat, Field::EndBeat, tr("Rain end beat"),
                           anyType(before, NoteType::RAIN) || anyType(after, NoteType::RAIN));
        setComparisonField(&m_sound, Field::Sound, tr("Sound file"),
                           anyType(before, NoteType::SOUND) || anyType(after, NoteType::SOUND));
        setComparisonField(&m_volume, Field::Volume, tr("Volume"),
                           anyType(before, NoteType::SOUND) || anyType(after, NoteType::SOUND));
        setComparisonField(&m_offset, Field::Offset, tr("Offset"),
                           anyType(before, NoteType::SOUND) || anyType(after, NoteType::SOUND));
        m_applyButton->hide();
        m_statusLabel->clear();
        m_refreshing = false;
        return;
    }

    const QVector<Note> notes = selectedNotes();
    if (notes.isEmpty())
    {
        m_typeValue->setText(tr("No notes selected"));
        updateEditor(Field::StartBeat, notes, false);
        updateEditor(Field::X, notes, false);
        updateEditor(Field::EndBeat, notes, false);
        updateEditor(Field::Sound, notes, false);
        updateEditor(Field::Volume, notes, false);
        updateEditor(Field::Offset, notes, false);
    }
    else
    {
        QVector<QString> types;
        for (const Note &note : notes)
            types.append(typeName(note.type));
        m_typeValue->setText(aggregate(types));
        updateEditor(Field::StartBeat, notes, true);
        const bool allSound = std::all_of(notes.cbegin(), notes.cend(), [](const Note &note) {
            return note.isSound();
        });
        const bool allSpatial = std::all_of(notes.cbegin(), notes.cend(), [](const Note &note) {
            return !note.isSound() && !note.isRainNote();
        });
        const bool allRain = std::all_of(notes.cbegin(), notes.cend(), [](const Note &note) {
            return note.isRainNote();
        });
        updateEditor(Field::X, notes, allSpatial);
        updateEditor(Field::EndBeat, notes, allRain);
        updateEditor(Field::Sound, notes, allSound);
        updateEditor(Field::Volume, notes, allSound);
        updateEditor(Field::Offset, notes, allSound);
    }
    m_applyButton->setVisible(!isReadOnly());
    m_applyButton->setEnabled(!notes.isEmpty() && !isReadOnly());
    m_refreshing = false;
}

void NoteInspector::setComparison(const QVector<QPair<Note, Note>> &comparison)
{
    m_comparison = comparison;
    refresh();
}

void NoteInspector::clearComparison()
{
    m_comparison.clear();
    refresh();
}

void NoteInspector::setReadOnly(bool readOnly)
{
    m_localReadOnly = readOnly;
    refresh();
}

void NoteInspector::setDisplayNotes(const QVector<Note> *notes)
{
    m_displayNotes = notes;
    refresh();
}

bool NoteInspector::isReadOnly() const
{
    return m_localReadOnly || comparisonMode() || (m_chartController && m_chartController->isReadOnly());
}

void NoteInspector::showError(const QString &message)
{
    m_statusLabel->setText(message);
}

void NoteInspector::applyChanges()
{
    if (comparisonMode() || isReadOnly())
    {
        showError(tr("This inspector is read-only."));
        return;
    }
    if (!m_chartController || !m_selectionController || !m_chartController->chart())
    {
        showError(tr("No chart is available."));
        return;
    }

    const QVector<Note> originalNotes = selectedNotes();
    if (originalNotes.isEmpty())
    {
        showError(tr("Select one or more notes first."));
        return;
    }

    struct Changes
    {
        bool beat = false;
        bool x = false;
        bool endBeat = false;
        bool sound = false;
        bool volume = false;
        bool offset = false;
        int beatNum = 0, numerator = 0, denominator = 0;
        int endBeatNum = 0, endNumerator = 0, endDenominator = 0;
        int xValue = 0, volumeValue = 0, offsetValue = 0;
        QString soundValue;
    } changes;

    const auto changedText = [](const FieldWidgets &widgets) {
        return widgets.editor->text() != widgets.editor->property("reviewInitialText").toString();
    };
    if (changedText(m_startBeat))
    {
        changes.beat = true;
        if (!parseBeatTriplet(m_startBeat.editor->text(), &changes.beatNum, &changes.numerator, &changes.denominator))
        {
            showError(tr(
                "Beat must use whole:numerator/denominator with a non-negative numerator and positive denominator."));
            return;
        }
    }
    if (changedText(m_x))
    {
        changes.x = true;
        bool ok = false;
        changes.xValue = m_x.editor->text().toInt(&ok);
        if (!ok || changes.xValue < 0 || changes.xValue > 512)
        {
            showError(tr("X must be an integer from 0 to 512."));
            return;
        }
    }
    if (changedText(m_endBeat))
    {
        changes.endBeat = true;
        if (!parseBeatTriplet(m_endBeat.editor->text(), &changes.endBeatNum, &changes.endNumerator,
                              &changes.endDenominator))
        {
            showError(tr("Rain end beat must use whole:numerator/denominator with a non-negative numerator and "
                         "positive denominator."));
            return;
        }
    }
    if (changedText(m_sound))
    {
        changes.sound = true;
        changes.soundValue = m_sound.editor->text().trimmed();
        if (changes.soundValue.isEmpty())
        {
            showError(tr("Sound file cannot be empty."));
            return;
        }
    }
    if (changedText(m_volume))
    {
        changes.volume = true;
        bool ok = false;
        changes.volumeValue = m_volume.editor->text().toInt(&ok);
        if (!ok || changes.volumeValue < 0 || changes.volumeValue > 100)
        {
            showError(tr("Sound volume must be an integer from 0 to 100."));
            return;
        }
    }
    if (changedText(m_offset))
    {
        changes.offset = true;
        bool ok = false;
        changes.offsetValue = m_offset.editor->text().toInt(&ok);
        if (!ok)
        {
            showError(tr("Sound offset must be an integer number of milliseconds."));
            return;
        }
    }
    if (!changes.beat && !changes.x && !changes.endBeat && !changes.sound && !changes.volume && !changes.offset)
    {
        showError(QString());
        return;
    }

    QList<QPair<Note, Note>> moved;
    moved.reserve(originalNotes.size());
    for (const Note &original : originalNotes)
    {
        if ((changes.x && (original.isSound() || original.isRainNote())) || (changes.endBeat && original.isSound()))
        {
            showError(tr("X and Rain end beat can only be edited when every selected note supports them."));
            return;
        }
        if ((changes.sound || changes.volume || changes.offset) && !original.isSound())
        {
            showError(tr("Sound properties can only be edited when every selected note is a Sound note."));
            return;
        }
        Note updated = original;
        if (changes.beat)
        {
            updated.beatNum = changes.beatNum;
            updated.numerator = changes.numerator;
            updated.denominator = changes.denominator;
        }
        if (changes.x)
            updated.x = changes.xValue;
        if (changes.endBeat)
        {
            if (!original.isRainNote())
            {
                showError(tr("Rain end beat can only be edited when every selected note is Rain."));
                return;
            }
            updated.endBeatNum = changes.endBeatNum;
            updated.endNumerator = changes.endNumerator;
            updated.endDenominator = changes.endDenominator;
        }
        if (changes.sound)
            updated.sound = changes.soundValue;
        if (changes.volume)
            updated.vol = changes.volumeValue;
        if (changes.offset)
            updated.offset = changes.offsetValue;
        if (!updated.isValid())
        {
            showError(tr("The proposed values would make at least one selected note invalid."));
            return;
        }
        if (updated != original)
            moved.append(qMakePair(original, updated));
    }

    if (moved.isEmpty())
    {
        showError(QString());
        return;
    }
    const quint64 revisionBeforeApply = m_chartController->revision();
    m_chartController->moveNotes(moved);
    if (m_chartController->revision() == revisionBeforeApply)
    {
        showError(tr("The chart rejected this edit."));
        return;
    }
    showError(QString());
    refresh();
}
} // namespace CceReviewUi

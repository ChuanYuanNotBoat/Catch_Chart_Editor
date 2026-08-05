#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "render/NoteRenderer.h"
#include "utils/MathUtils.h"
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include "model/Chart.h"
#include "editor/NoteChain/NoteChainCanvasBridge.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>
#include <QMessageBox>
#include <QDateTime>
#include <QCoreApplication>
#include <QHash>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

constexpr double kMoveDragThresholdPx = 5.0;

namespace
{
double extractWheelDeltaY(const QWheelEvent *event)
{
    if (!event)
        return 0.0;

    const int angleDeltaY = event->angleDelta().y();
    if (angleDeltaY != 0)
        return static_cast<double>(angleDeltaY);

    const int pixelDeltaY = event->pixelDelta().y();
    if (pixelDeltaY != 0)
        return static_cast<double>(pixelDeltaY);

    return 0.0;
}

void fillPluginEventModifiers(PluginInterface::CanvasInputEvent *outEvent, Qt::KeyboardModifiers eventModifiers)
{
    if (!outEvent)
        return;
    outEvent->modifiers = static_cast<int>(eventModifiers);
    outEvent->shiftDown = eventModifiers.testFlag(Qt::ShiftModifier);
    outEvent->ctrlDown = eventModifiers.testFlag(Qt::ControlModifier);
}struct ColorDivisionOption
{
    int denominator;
    const char *label;
};

const ColorDivisionOption kColorDivisionOptions[] = {
    {1, "1/1"},
    {2, "1/2"},
    {3, "1/3"},
    {4, "1/4"},
    {6, "1/6"},
    {8, "1/8"},
    {12, "1/12"},
    {16, "1/16"},
    {24, "1/24"},
    {32, "1/32"},
    {288, "1/288"}};

bool convertBeatDenExactly(int beatNum, int numerator, int denominator, int targetDen,
                           int &outBeatNum, int &outNumerator, int &outDenominator)
{
    if (denominator <= 0 || targetDen <= 0)
        return false;

    const qint64 scaledNumerator = static_cast<qint64>(beatNum) * denominator + numerator;
    const qint64 scaledTarget = scaledNumerator * targetDen;
    if (scaledTarget % denominator != 0)
        return false;

    qint64 ticks = scaledTarget / denominator;
    qint64 beat = ticks / targetDen;
    qint64 num = ticks % targetDen;
    if (num < 0)
    {
        num += targetDen;
        --beat;
    }

    outBeatNum = static_cast<int>(beat);
    outNumerator = static_cast<int>(num);
    outDenominator = targetDen;
    return true;
}

bool isNoteConvertibleToDenominator(const Note &note, int targetDen)
{
    if (note.type == NoteType::SOUND)
        return false;

    int b = 0, n = 0, d = 1;
    if (!convertBeatDenExactly(note.beatNum, note.numerator, note.denominator, targetDen, b, n, d))
        return false;

    if (note.type == NoteType::RAIN)
    {
        int eb = 0, en = 0, ed = 1;
        if (!convertBeatDenExactly(note.endBeatNum, note.endNumerator, note.endDenominator, targetDen, eb, en, ed))
            return false;
    }

    return true;
}

int reduceFractionDenominator(int numerator, int denominator)
{
    if (denominator <= 0)
        return 1;
    const int g = std::gcd(std::abs(numerator), denominator);
    const int reduced = denominator / qMax(1, g);
    return qMax(1, reduced);
}

int computeMinimalDenominatorForColor(const Note &note)
{
    int minimal = reduceFractionDenominator(note.numerator, note.denominator);
    if (note.type == NoteType::RAIN)
    {
        const int endMinimal = reduceFractionDenominator(note.endNumerator, note.endDenominator);
        minimal = std::lcm(qMax(1, minimal), qMax(1, endMinimal));
    }
    return qMax(1, minimal);
}

bool isRegularColorDenominator(int denominator)
{
    for (const ColorDivisionOption &option : kColorDivisionOptions)
    {
        if (option.denominator == denominator)
            return true;
    }
    return false;
}

int computeSmallIrregularDenominatorForColor(const Note &note)
{
    const int base = qMax(1, computeMinimalDenominatorForColor(note));
    if (!isRegularColorDenominator(base))
        return base;

    // Keep exact timing by using multiples of the minimal denominator.
    for (int k = 2; k <= 1024; ++k)
    {
        const qint64 candidate64 = static_cast<qint64>(base) * k;
        if (candidate64 > std::numeric_limits<int>::max())
            break;
        const int candidate = static_cast<int>(candidate64);
        if (!isRegularColorDenominator(candidate))
            return candidate;
    }
    return base;
}

QRect selectionPreviewDirtyRect(const QPointF &start, const QPointF &oldEnd, const QPointF &newEnd)
{
    const QRect oldRect = QRectF(start, oldEnd).normalized().toAlignedRect();
    const QRect newRect = QRectF(start, newEnd).normalized().toAlignedRect();
    return oldRect.united(newRect).adjusted(-2, -2, 2, 2);
}

Note mirroredNote(const Note &note, int axisX, int laneWidth)
{
    Note mirrored = note;
    mirrored.x = qBound(0, axisX * 2 - note.x, laneWidth);
    return mirrored;
}

PluginManager *activePluginManager()
{
    auto *app = qobject_cast<Application *>(QCoreApplication::instance());
    if (!app || !app->pluginSystemReady())
        return nullptr;
    return app->pluginManager();
}
} // namespace


void ChartCanvas::prepareMoveChanges()
{
    m_moveChanges.clear();
    const auto &notes = chart()->notes();
    for (int idx : m_originalSelectedIndices)
    {
        if (idx >= 0 && idx < notes.size())
            m_moveChanges.insert(idx, qMakePair(notes[idx], notes[idx]));
    }
}

bool ChartCanvas::hasNoteSnapReferenceOverlays() const
{
    for (const PluginInterface::CanvasOverlayItem &item : m_overlayCache)
    {
        if (!item.noteSnapReference)
            continue;
        if (item.kind == PluginInterface::CanvasOverlayItem::Line && item.chartSpace)
            return true;
    }
    return false;
}

void ChartCanvas::refreshPluginOverlayCacheForSnap()
{
    if (!m_pluginToolModeActive)
        return;

    PluginManager *pm = activePluginManager();
    if (!pm)
        return;

    QVariantMap overlayContext = buildPluginCanvasContext();
    overlayContext.insert("left_margin", leftMargin());
    overlayContext.insert("right_margin", rightMargin());
    m_overlayCache = pm->canvasOverlays(overlayContext);
    m_lastOverlayQueryMs = QDateTime::currentMSecsSinceEpoch();
}

bool ChartCanvas::curveSnapXForBeat(double beat, int currentX, int *outX) const
{
    if (!outX)
        return false;

    constexpr double kSnapRangePx = 16.0;
    constexpr double kBeatEpsilon = 1e-7;
    bool found = false;
    double bestDistancePx = kSnapRangePx + 1.0;
    double bestLaneX = currentX;
    const double currentCanvasX = laneXToCanvasX(currentX);

    for (const PluginInterface::CanvasOverlayItem &item : m_overlayCache)
    {
        if (item.kind != PluginInterface::CanvasOverlayItem::Line || !item.chartSpace)
            continue;
        if (!item.noteSnapReference)
            continue;

        const double b0 = item.chartFrom.y();
        const double b1 = item.chartTo.y();
        const double lo = qMin(b0, b1) - kBeatEpsilon;
        const double hi = qMax(b0, b1) + kBeatEpsilon;
        if (beat < lo || beat > hi)
            continue;

        double laneX = item.chartFrom.x();
        const double span = b1 - b0;
        if (qAbs(span) > kBeatEpsilon)
        {
            const double t = qBound(0.0, (beat - b0) / span, 1.0);
            laneX = item.chartFrom.x() + (item.chartTo.x() - item.chartFrom.x()) * t;
        }
        else if (qAbs(beat - b0) > kBeatEpsilon)
        {
            continue;
        }

        const double distancePx = qAbs(laneXToCanvasX(laneX) - currentCanvasX);
        if (distancePx <= kSnapRangePx && distancePx < bestDistancePx)
        {
            bestDistancePx = distancePx;
            bestLaneX = laneX;
            found = true;
        }
    }

    if (!found)
        return false;

    *outX = qBound(0, qRound(bestLaneX), kLaneWidth);
    return true;
}

void ChartCanvas::applyCurveSnapToMovedNote(Note *note, double beat) const
{
    if (!note || !m_noteSnapReferenceActiveForMove || note->type == NoteType::SOUND)
        return;

    int snappedX = note->x;
    if (curveSnapXForBeat(beat, note->x, &snappedX))
        note->x = snappedX;
}

void ChartCanvas::beginMoveSelection(const QPointF &startPos, int referenceIndex)
{
    m_isMovingSelection = true;
    m_moveStartPos = startPos;
    m_moveDeltaBeatRaw = 0.0;
    m_moveDeltaXRaw = 0.0;
    m_originalSelectedIndices = m_selectionController->selectedIndices();
    m_dragReferenceIndex = referenceIndex;
    if (m_dragReferenceIndex == -1 && !m_originalSelectedIndices.isEmpty())
    {
        m_dragReferenceIndex = *m_originalSelectedIndices.begin();
    }

    m_gridSnapBackup = m_gridSnap;
    m_wasGridSnapEnabled = m_gridSnap;
    m_gridSnap = false;
    refreshPluginOverlayCacheForSnap();
    m_noteSnapReferenceActiveForMove = hasNoteSnapReferenceOverlays();

    prepareMoveChanges();
    update();
}

void ChartCanvas::updateMoveSelection(const QPointF &currentPos)
{
    if (!m_isMovingSelection)
        return;

    QPointF delta = currentPos - m_moveStartPos;
    if (qAbs(delta.x()) < 1e-6 && qAbs(delta.y()) < 1e-6)
        return;

    double deltaY = delta.y();
    if (m_verticalFlip)
        deltaY = -deltaY;

    const int availableWidth = qMax(1, width() - leftMargin() - rightMargin());
    const double deltaBeat = (deltaY / height()) * effectiveVisibleBeatRange();
    const double deltaX = delta.x() / static_cast<double>(availableWidth) * static_cast<double>(kLaneWidth);

    m_moveDeltaBeatRaw += deltaBeat;
    m_moveDeltaXRaw += deltaX;
    m_moveStartPos = currentPos;

    double appliedDeltaBeat = m_moveDeltaBeatRaw;
    if (m_timeDivision > 0 && m_dragReferenceIndex >= 0 && m_moveChanges.contains(m_dragReferenceIndex))
    {
        const Note &refOriginal = m_moveChanges[m_dragReferenceIndex].first;
        const double refOriginalBeat = MathUtils::beatToFloat(refOriginal.beatNum, refOriginal.numerator, refOriginal.denominator);
        const double refNewBeat = refOriginalBeat + m_moveDeltaBeatRaw;

        Note refSnapped = refOriginal;
        MathUtils::floatToBeat(refNewBeat, refSnapped.beatNum, refSnapped.numerator, refSnapped.denominator);
        refSnapped = MathUtils::snapNoteToTimeWithBoundary(refSnapped, m_timeDivision);
        const double refSnappedBeat = MathUtils::beatToFloat(refSnapped.beatNum, refSnapped.numerator, refSnapped.denominator);
        appliedDeltaBeat = refSnappedBeat - refOriginalBeat;
    }
    const double appliedDeltaX = m_moveDeltaXRaw;

    QVector<Note> *notes = mutableNotes();
    if (!notes)
        return;
    QList<int> selectedList = m_originalSelectedIndices.values();

    for (int idx : selectedList)
    {
        if (idx < 0 || idx >= notes->size())
            continue;
        auto it = m_moveChanges.constFind(idx);
        if (it == m_moveChanges.constEnd())
            continue;

        const Note &original = it->first;
        Note newNote = original;

        double originalBeat = MathUtils::beatToFloat(original.beatNum, original.numerator, original.denominator);
        double newBeat = originalBeat + appliedDeltaBeat;
        if (newBeat < 0)
            newBeat = 0;
        MathUtils::floatToBeat(newBeat, newNote.beatNum, newNote.numerator, newNote.denominator);

        newNote.x = qBound(0, qRound(original.x + appliedDeltaX), kLaneWidth);

        if (original.type == NoteType::RAIN)
        {
            double originalEndBeat = MathUtils::beatToFloat(original.endBeatNum, original.endNumerator, original.endDenominator);
            double newEndBeat = originalEndBeat + appliedDeltaBeat;
            if (newEndBeat < newBeat)
                newEndBeat = newBeat;
            MathUtils::floatToBeat(newEndBeat, newNote.endBeatNum, newNote.endNumerator, newNote.endDenominator);
        }

        applyCurveSnapToMovedNote(&newNote, newBeat);
        (*notes)[idx] = newNote;
    }

    m_noteDataDirty = true;
    m_timesDirty = true;
    m_hyperCacheValid = false;

    update();
}

void ChartCanvas::endMoveSelection()
{
    if (!m_isMovingSelection)
        return;

    m_isMovingSelection = false;

    if (m_wasGridSnapEnabled)
    {
        m_gridSnap = m_gridSnapBackup;
        m_wasGridSnapEnabled = false;
    }

    double finalAppliedDeltaBeat = m_moveDeltaBeatRaw;
    if (m_timeDivision > 0 && m_dragReferenceIndex >= 0 && m_moveChanges.contains(m_dragReferenceIndex))
    {
        const Note &refOriginal = m_moveChanges[m_dragReferenceIndex].first;
        const double refOriginalBeat = MathUtils::beatToFloat(refOriginal.beatNum, refOriginal.numerator, refOriginal.denominator);
        const double refNewBeat = refOriginalBeat + m_moveDeltaBeatRaw;

        Note refSnapped = refOriginal;
        MathUtils::floatToBeat(refNewBeat, refSnapped.beatNum, refSnapped.numerator, refSnapped.denominator);
        refSnapped = MathUtils::snapNoteToTimeWithBoundary(refSnapped, m_timeDivision);
        const double refSnappedBeat = MathUtils::beatToFloat(refSnapped.beatNum, refSnapped.numerator, refSnapped.denominator);
        finalAppliedDeltaBeat = refSnappedBeat - refOriginalBeat;
    }
    const double finalAppliedDeltaX = m_moveDeltaXRaw;

    QVector<Note> *notes = mutableNotes();
    if (!notes)
        return;

    QList<QPair<Note, Note>> finalChanges;
    const auto &notesNow = chart()->notes();
    QHash<QString, int> indexById;
    indexById.reserve(notesNow.size());
    for (int i = 0; i < notesNow.size(); ++i)
    {
        if (!notesNow[i].id.isEmpty() && !indexById.contains(notesNow[i].id))
            indexById.insert(notesNow[i].id, i);
    }

    QSet<int> consumedFallbackIndices;
    auto findIdentityIndex = [&notesNow, &indexById, &consumedFallbackIndices](const Note &originalNote,
                                                                                const Note &expectedMovedNote,
                                                                                int sourceIndex) -> int
    {
        if (!originalNote.id.isEmpty())
            return indexById.value(originalNote.id, -1);

        for (int i = 0; i < notesNow.size(); ++i)
        {
            if (consumedFallbackIndices.contains(i))
                continue;
            if (notesNow[i] == expectedMovedNote)
            {
                consumedFallbackIndices.insert(i);
                return i;
            }
        }

        for (int i = 0; i < notesNow.size(); ++i)
        {
            if (consumedFallbackIndices.contains(i))
                continue;
            if (notesNow[i] == originalNote)
            {
                consumedFallbackIndices.insert(i);
                return i;
            }
        }

        if (sourceIndex >= 0 &&
            sourceIndex < notesNow.size() &&
            !consumedFallbackIndices.contains(sourceIndex) &&
            (notesNow[sourceIndex] == expectedMovedNote || notesNow[sourceIndex] == originalNote))
        {
            consumedFallbackIndices.insert(sourceIndex);
            return sourceIndex;
        }
        return -1;
    };

    const auto buildFinalMovedNote = [this, finalAppliedDeltaBeat, finalAppliedDeltaX](const Note &original) -> Note
    {
        Note moved = original;
        double newBeat = MathUtils::beatToFloat(original.beatNum, original.numerator, original.denominator) + finalAppliedDeltaBeat;
        if (newBeat < 0.0)
            newBeat = 0.0;
        MathUtils::floatToBeat(newBeat, moved.beatNum, moved.numerator, moved.denominator);

        moved.x = qBound(0, qRound(original.x + finalAppliedDeltaX), kLaneWidth);

        if (original.type == NoteType::RAIN)
        {
            double newEndBeat = MathUtils::beatToFloat(original.endBeatNum, original.endNumerator, original.endDenominator) + finalAppliedDeltaBeat;
            if (newEndBeat < newBeat)
                newEndBeat = newBeat;
            MathUtils::floatToBeat(newEndBeat, moved.endBeatNum, moved.endNumerator, moved.endDenominator);
        }

        applyCurveSnapToMovedNote(&moved, newBeat);
        return moved;
    };

    QList<QPair<int, Note>> rollbackPairs;
    rollbackPairs.reserve(m_moveChanges.size());
    for (auto it = m_moveChanges.constBegin(); it != m_moveChanges.constEnd(); ++it)
    {
        const int sourceIndex = it.key();
        const Note &originalNote = it.value().first;
        const Note movedNote = buildFinalMovedNote(originalNote);
        if (originalNote == movedNote)
            continue;

        const int identityIndex = findIdentityIndex(originalNote, movedNote, sourceIndex);
        if (identityIndex < 0 || identityIndex >= notesNow.size())
            continue;

        finalChanges.append(qMakePair(originalNote, movedNote));
        rollbackPairs.append(qMakePair(identityIndex, originalNote));
    }

    if (!finalChanges.isEmpty())
    {
        for (const auto &rollback : rollbackPairs)
        {
            const int idx = rollback.first;
            if (idx >= 0 && idx < notes->size())
                (*notes)[idx] = rollback.second;
        }
        m_chartController->moveNotes(finalChanges);
    }

    m_originalSelectedIndices.clear();
    m_dragReferenceIndex = -1;
    m_moveDeltaBeatRaw = 0.0;
    m_moveDeltaXRaw = 0.0;
    m_noteSnapReferenceActiveForMove = false;
    m_moveChanges.clear();
    m_moveStartPos = QPointF();

    m_noteDataDirty = true;
    m_timesDirty = true;
    update();
}

QVector<int> ChartCanvas::collectColorTargetIndices(const QPoint &pos) const
{
    QVector<int> targetIndices;
    if (!chart())
        return targetIndices;

    const QVector<Note> &notes = chart()->notes();
    const int hitIndex = hitTestNote(pos);
    const QSet<int> selected = m_selectionController ? m_selectionController->selectedIndices() : QSet<int>();

    auto appendFiltered = [&notes, &targetIndices](const QSet<int> &indices)
    {
        QList<int> sorted = indices.values();
        std::sort(sorted.begin(), sorted.end());
        for (int idx : sorted)
        {
            if (idx >= 0 && idx < notes.size() && notes[idx].type != NoteType::SOUND)
                targetIndices.append(idx);
        }
    };

    if (hitIndex >= 0 && hitIndex < notes.size())
    {
        if (!selected.isEmpty() && selected.contains(hitIndex))
        {
            appendFiltered(selected);
        }
        else if (notes[hitIndex].type != NoteType::SOUND)
        {
            targetIndices.append(hitIndex);
        }
    }
    else if (!selected.isEmpty())
    {
        appendFiltered(selected);
    }

    return targetIndices;
}

QVector<int> ChartCanvas::collectMirrorTargetIndices(const QPoint &pos) const
{
    QVector<int> targetIndices;
    if (!chart())
        return targetIndices;

    const QVector<Note> &notes = chart()->notes();
    const bool hasPoint = !pos.isNull();
    const int hitIndex = hasPoint ? hitTestNote(pos) : -1;
    const QSet<int> selected = m_selectionController ? m_selectionController->selectedIndices() : QSet<int>();

    auto appendFiltered = [&notes, &targetIndices](const QSet<int> &indices)
    {
        QList<int> sorted = indices.values();
        std::sort(sorted.begin(), sorted.end());
        for (int idx : sorted)
        {
            if (idx >= 0 && idx < notes.size() && notes[idx].type != NoteType::SOUND)
                targetIndices.append(idx);
        }
    };

    if (hitIndex >= 0 && hitIndex < notes.size())
    {
        if (!selected.isEmpty() && selected.contains(hitIndex))
        {
            appendFiltered(selected);
        }
        else if (notes[hitIndex].type != NoteType::SOUND)
        {
            targetIndices.append(hitIndex);
        }
    }
    else if (!selected.isEmpty())
    {
        appendFiltered(selected);
    }

    return targetIndices;
}

bool ChartCanvas::performMirrorFlip(const QVector<int> &targetIndices, int axisX, const QString &actionName)
{
    Q_UNUSED(actionName);
    if (!chart() || !m_chartController || targetIndices.isEmpty())
        return false;

    const QVector<Note> &notes = chart()->notes();
    QList<QPair<Note, Note>> changes;
    for (int idx : targetIndices)
    {
        if (idx < 0 || idx >= notes.size())
            continue;

        const Note &original = notes[idx];
        if (original.type == NoteType::SOUND)
            continue;

        const Note updated = mirroredNote(original, clampMirrorAxisX(axisX), kLaneWidth);
        if (!(updated == original))
            changes.append(qMakePair(original, updated));
    }

    if (changes.isEmpty())
        return false;

    m_chartController->moveNotes(changes);
    emit statusMessage(tr("Mirrored %1 note(s).").arg(changes.size()));
    return true;
}

void ChartCanvas::populateColorMenu(QMenu *colorMenu, const QVector<int> &targetIndices)
{
    if (!colorMenu || targetIndices.isEmpty() || !chart())
        return;

    const QVector<Note> &notes = chart()->notes();
    QVector<int> availableDenominators;
    for (const ColorDivisionOption &option : kColorDivisionOptions)
    {
        bool allConvertible = true;
        for (int idx : targetIndices)
        {
            if (!isNoteConvertibleToDenominator(notes[idx], option.denominator))
            {
                allConvertible = false;
                break;
            }
        }
        if (allConvertible)
            availableDenominators.append(option.denominator);
    }

    if (!availableDenominators.isEmpty())
    {
        colorMenu->setEnabled(true);
        for (const ColorDivisionOption &option : kColorDivisionOptions)
        {
            if (!availableDenominators.contains(option.denominator))
                continue;

            QAction *act = colorMenu->addAction(tr(option.label));
            connect(act, &QAction::triggered, this, [this, targetIndices, option]()
                    {
                if (!chart())
                    return;

                QVector<Note> *notesRef = mutableNotes();
                if (!notesRef)
                    return;
                QList<QPair<Note, Note>> changes;
                for (int idx : targetIndices)
                {
                    if (idx < 0 || idx >= notesRef->size())
                        continue;
                    const Note &original = (*notesRef)[idx];
                    if (original.type == NoteType::SOUND)
                        continue;

                    Note updated = original;
                    if (!convertBeatDenExactly(original.beatNum, original.numerator, original.denominator,
                                                option.denominator, updated.beatNum, updated.numerator, updated.denominator))
                        continue;
                    if (original.type == NoteType::RAIN)
                    {
                        if (!convertBeatDenExactly(original.endBeatNum, original.endNumerator, original.endDenominator,
                                                    option.denominator, updated.endBeatNum, updated.endNumerator, updated.endDenominator))
                            continue;
                    }

                    if (!(updated == original))
                        changes.append(qMakePair(original, updated));
                }

                if (!changes.isEmpty())
                {
                    m_chartController->moveNotes(changes);
                    emit statusMessage(tr("Color division changed to %1 for %2 note(s).")
                                            .arg(option.denominator)
                                            .arg(changes.size()));
                } });
        }
    }

    QAction *minimalIrregularAction = colorMenu->addAction(tr("Minimal Irregular (Red)"));
    connect(minimalIrregularAction, &QAction::triggered, this, [this, targetIndices]()
            {
        if (!chart())
            return;

        QVector<Note> *notesRef = mutableNotes();
        if (!notesRef)
            return;
        QList<QPair<Note, Note>> changes;
        for (int idx : targetIndices)
        {
            if (idx < 0 || idx >= notesRef->size())
                continue;
            const Note &original = (*notesRef)[idx];
            if (original.type == NoteType::SOUND)
                continue;

            const int targetDen = computeSmallIrregularDenominatorForColor(original);

            Note updated = original;
            if (!convertBeatDenExactly(original.beatNum, original.numerator, original.denominator,
                                        targetDen, updated.beatNum, updated.numerator, updated.denominator))
                continue;
            if (original.type == NoteType::RAIN)
            {
                if (!convertBeatDenExactly(original.endBeatNum, original.endNumerator, original.endDenominator,
                                            targetDen, updated.endBeatNum, updated.endNumerator, updated.endDenominator))
                    continue;
            }

            if (!(updated == original))
                changes.append(qMakePair(original, updated));
        }

        if (!changes.isEmpty())
        {
            m_chartController->moveNotes(changes);
            emit statusMessage(tr("Applied minimal irregular denominator for %1 note(s).")
                                    .arg(changes.size()));
        } });
    minimalIrregularAction->setEnabled(true);
    colorMenu->setEnabled(true);
}

void ChartCanvas::showRightClickMenu(QMouseEvent *event)
{
    if (m_pluginToolModeActive)
    {
        const QString pluginId = resolvePluginCanvasToolId();
        const bool routeDensityToSelectedSegments =
            (pluginId.trimmed().compare(QStringLiteral("builtin.note_chain_assist"), Qt::CaseInsensitive) == 0);
        QHash<QString, bool> segmentDensityCheckedMap;
        bool segmentDensityMixed = false;
        if (routeDensityToSelectedSegments)
        {
            if (PluginManager *pm = activePluginManager())
            {
                if (!pluginId.trimmed().isEmpty())
                {
                    const QList<PluginManager::ToolActionEntry> entries = pm->toolActions();
                    for (const PluginManager::ToolActionEntry &entry : entries)
                    {
                        if (entry.pluginId.trimmed() != pluginId)
                            continue;
                        const QString actionId = entry.action.actionId.trimmed();
                        if (actionId == QStringLiteral("set_segment_density_follow") ||
                            actionId.startsWith(QStringLiteral("set_segment_density_")))
                        {
                            segmentDensityCheckedMap.insert(actionId, entry.action.checked);
                        }
                        else if (actionId == QStringLiteral("segment_density_mixed_info") && entry.action.checked)
                        {
                            segmentDensityMixed = true;
                        }
                    }
                }
            }
        }

        QMenu pluginMenu(this);
        QAction *commitCurveAction = pluginMenu.addAction(tr("Commit Curve -> Notes"));
        pluginMenu.addSeparator();

        QMenu *densityMenu = pluginMenu.addMenu(tr("Curve Placement Density"));
        struct DensityOption
        {
            int denominator;
            const char *label;
        };
        const DensityOption densityOptions[] = {
            {0, "Follow Editor"},
            {1, "1/1"},
            {2, "1/2"},
            {3, "1/3"},
            {4, "1/4"},
            {6, "1/6"},
            {8, "1/8"},
            {12, "1/12"},
            {16, "1/16"},
            {24, "1/24"},
            {32, "1/32"},
            {48, "1/48"},
            {64, "1/64"},
            {96, "1/96"},
            {192, "1/192"},
            {288, "1/288"}};

        for (const DensityOption &opt : densityOptions)
        {
            QAction *act = densityMenu->addAction(tr(opt.label));
            if (routeDensityToSelectedSegments)
            {
                act->setCheckable(true);
                const QString actionId = (opt.denominator <= 0)
                                             ? QStringLiteral("set_segment_density_follow")
                                             : QStringLiteral("set_segment_density_%1").arg(opt.denominator);
                act->setChecked(segmentDensityCheckedMap.value(actionId, false));
                const QString actionTitle = (opt.denominator <= 0)
                                                ? tr("Set Segment Density: Follow Editor")
                                                : tr("Set Segment Density: 1/%1").arg(opt.denominator);
                connect(act, &QAction::triggered, this, [this, actionId, actionTitle]() {
                    if (!triggerPluginToolAction(actionId, actionTitle))
                        emit statusMessage(tr("No segment selected for density change."));
                });
            }
            else
            {
                act->setCheckable(true);
                act->setChecked(m_pluginPlacementDensityOverride == opt.denominator);
                connect(act, &QAction::triggered, this, [this, opt]() {
                    m_pluginPlacementDensityOverride = opt.denominator;
                    if (opt.denominator <= 0)
                        emit statusMessage(tr("Curve density: follow editor"));
                    else
                        emit statusMessage(tr("Curve density set to 1/%1").arg(opt.denominator));
                    update();
                });
            }
        }
        if (routeDensityToSelectedSegments && segmentDensityMixed)
        {
            densityMenu->addSeparator();
            QAction *mixedAct = densityMenu->addAction(tr("Mixed"));
            mixedAct->setEnabled(false);
            mixedAct->setCheckable(true);
            mixedAct->setChecked(true);
        }

        QAction *contextSeparator = nullptr;
        QHash<QAction *, QPair<QString, QString>> pluginContextActions;
        if (PluginManager *pm = activePluginManager())
        {
            if (!pluginId.trimmed().isEmpty())
            {
                const QList<PluginManager::ToolActionEntry> entries = pm->toolActions();
                for (const PluginManager::ToolActionEntry &entry : entries)
                {
                    if (entry.pluginId.trimmed() != pluginId)
                        continue;
                    if (routeDensityToSelectedSegments)
                    {
                        const QString actionId = entry.action.actionId.trimmed();
                        if (actionId == QStringLiteral("set_segment_density_follow") ||
                            actionId.startsWith(QStringLiteral("set_segment_density_")) ||
                            actionId == QStringLiteral("segment_density_mixed_info"))
                        {
                            continue;
                        }
                    }
                    const QString placement = entry.action.placement.trimmed().toLower();
                    if (placement != QString(PluginInterface::kPlacementPluginContextMenu))
                        continue;
                    if (contextSeparator == nullptr)
                        contextSeparator = pluginMenu.addSeparator();
                    QAction *act = pluginMenu.addAction(entry.action.title);
                    if (!entry.action.description.isEmpty())
                        act->setToolTip(entry.action.description);
                    act->setCheckable(entry.action.checkable);
                    if (entry.action.checkable)
                        act->setChecked(entry.action.checked);
                    pluginContextActions.insert(act, qMakePair(entry.action.actionId, entry.action.title));
                }
            }
        }

        QAction *selected = pluginMenu.exec(event->globalPos());
        if (selected == commitCurveAction)
        {
            triggerPluginBatchAction("commit_curve_to_notes", tr("Commit Curve -> Notes"));
        }
        else if (pluginContextActions.contains(selected))
        {
            const auto pair = pluginContextActions.value(selected);
            triggerPluginToolAction(pair.first, pair.second);
        }
        return;
    }

    QMenu menu(this);
    QAction *playFromRefAction = menu.addAction(tr("Play from Reference Time"));
    QAction *pasteAction = menu.addAction(tr("Paste"));
    pasteAction->setEnabled(m_selectionController && !m_selectionController->getClipboard().isEmpty());
    const QVector<int> mirrorTargetIndices = collectMirrorTargetIndices(event->pos());
    QAction *mirrorFlipAction = menu.addAction(tr("Mirror Flip Selected (Center Line)"));
    mirrorFlipAction->setEnabled(!mirrorTargetIndices.isEmpty());
    QMenu *colorMenu = menu.addMenu(tr("Edit Color (By Division)"));
    colorMenu->setEnabled(false);

    const QVector<int> targetIndices = collectColorTargetIndices(event->pos());
    if (!targetIndices.isEmpty())
        populateColorMenu(colorMenu, targetIndices);

    QAction *selectedAction = menu.exec(event->globalPos());
    if (selectedAction == playFromRefAction)
    {
        playFromReferenceLine();
    }
    else if (selectedAction == pasteAction)
    {
        pasteAtCursor(event->pos());
    }
    else if (selectedAction == mirrorFlipAction)
    {
        performMirrorFlip(mirrorTargetIndices, kLaneWidth / 2, tr("Mirror Flip Notes"));
    }
}

bool ChartCanvas::handleMirrorGuidePress(const QPointF &pos)
{
    if (!isMirrorGuideHandleHit(pos))
        return false;

    m_isDraggingMirrorGuide = true;
    setMirrorAxisX(canvasXToLaneX(pos.x()));
    return true;
}

bool ChartCanvas::handlePastePreviewLeftClick(const QPoint &pos)
{
    if (!m_isPasting)
        return false;

    if (pos.x() >= 10 && pos.x() <= 110 && pos.y() >= 10 && pos.y() <= 40)
    {
        confirmPaste();
        return true;
    }
    if (pos.x() >= 120 && pos.x() <= 220 && pos.y() >= 10 && pos.y() <= 40)
    {
        cancelPaste();
        return true;
    }

    beginDragPaste(pos);
    return true;
}

bool ChartCanvas::handleRainPlacementLeftClick(const QPointF &pos)
{
    if (m_currentMode != PlaceRain)
        return false;

    if (m_rainFirst)
    {
        m_rainStartPos = pos;
        m_rainFirst = false;
        return true;
    }

    const QPointF endPos = pos;
    m_rainFirst = true;
    const Note startNote = posToNote(m_rainStartPos);
    const Note endNote = posToNote(endPos);
    const double startTime = MathUtils::beatToMs(startNote.beatNum, startNote.numerator, startNote.denominator,
                                                 chart()->bpmList(),
                                                 chart()->meta().offset);
    const double endTime = MathUtils::beatToMs(endNote.beatNum, endNote.numerator, endNote.denominator,
                                               chart()->bpmList(),
                                               chart()->meta().offset);
    if (endTime > startTime)
    {
        Note rainNote(startNote.beatNum, startNote.numerator, startNote.denominator,
                      endNote.beatNum, endNote.numerator, endNote.denominator,
                      startNote.x);
        if (rainNote.isValidRain())
        {
            m_chartController->addNote(rainNote);
        }
        else
        {
            QMessageBox::warning(this, tr("Invalid Rain Note"), tr("Invalid rain note parameters."));
        }
    }
    else
    {
        QMessageBox::warning(this, tr("Invalid Rain Note"), tr("End time must be later than start time"));
    }
    return true;
}

bool ChartCanvas::handleHitNoteLeftClick(int hitIndex, Qt::KeyboardModifiers modifiers, const QPointF &pos)
{
    if (hitIndex == -1)
        return false;

    if (m_currentMode == Delete)
    {
        const auto &notes = chart()->notes();
        if (hitIndex >= 0 && hitIndex < notes.size())
        {
            QVector<Note> noteToDelete;
            noteToDelete.append(notes[hitIndex]);
            m_chartController->removeNotes(noteToDelete);
        }
        return true;
    }

    if (modifiers & Qt::ControlModifier)
    {
        if (m_selectionController->selectedIndices().contains(hitIndex))
            m_selectionController->removeFromSelection(hitIndex);
        else
            m_selectionController->addToSelection(hitIndex);
        return true;
    }

    if (!m_selectionController->selectedIndices().contains(hitIndex))
        m_selectionController->addToSelection(hitIndex);

    // Defer move start until mouse drag exceeds threshold (to distinguish click vs drag)
    m_pendingMove = true;
    m_moveStartPos = pos;
    m_dragReferenceIndex = hitIndex;
    return true;
}

void ChartCanvas::handleLeftMousePress(QMouseEvent *event)
{
    if (handleMirrorGuidePress(event->pos()))
        return;

    if (handlePastePreviewLeftClick(event->pos()))
        return;

    if (m_currentMode == Select)
    {
        const int hitIndex = hitTestNote(event->pos());
        if (handleHitNoteLeftClick(hitIndex, Qt::NoModifier, event->pos()))
            return;

        m_isSelecting = true;
        m_selectionStart = event->pos();
        m_selectionEnd = event->pos();
        return;
    }

    if (event->modifiers() & Qt::ControlModifier)
    {
        m_isSelecting = true;
        m_selectionStart = event->pos();
        m_selectionEnd = event->pos();
        return;
    }

    if (handleRainPlacementLeftClick(event->pos()))
        return;

    const int hitIndex = hitTestNote(event->pos());
    if (handleHitNoteLeftClick(hitIndex, event->modifiers(), event->pos()))
        return;

    const bool hadSelection = m_selectionController && !m_selectionController->selectedIndices().isEmpty();
    if (m_selectionController)
        m_selectionController->clearSelection();
    if (hadSelection)
        return;

    if (m_currentMode == PlaceNote)
    {
        Note note = posToNote(event->pos());
        m_chartController->addNote(note);
    }
}

void ChartCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus();

    // NoteChain native: dispatch before plugin
    if (m_noteChainModeActive && m_noteChainEditor) {
        bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
        bool ctrl  = event->modifiers().testFlag(Qt::ControlModifier);
        double laneX = canvasXToLaneX(event->position().x());
        double beat = yToBeat(event->position().y());
        if (NoteChain::bridgeHandleMousePress(m_noteChainEditor, event,
                laneX, beat, shift, ctrl)) {
            event->accept();
            update();
            return;
        }
    }


    // Range handle drag detection
    if (m_rangeOverlayValid && m_rangeOverlayVisible && event->button() == Qt::LeftButton)
    {
        int handle = hitTestRangeHandle(event->position());
        if (handle == 1)
        {
            m_isDraggingRangeStart = true;
            setCursor(Qt::SizeVerCursor);
            event->accept();
            return;
        }
        if (handle == 2)
        {
            m_isDraggingRangeEnd = true;
            setCursor(Qt::SizeVerCursor);
            event->accept();
            return;
        }
    }



    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "mouse_down";
    pluginEvent.x = event->position().x();
    pluginEvent.y = event->position().y();
    pluginEvent.button = static_cast<int>(event->button());
    pluginEvent.buttons = static_cast<int>(event->buttons());
    fillPluginEventModifiers(&pluginEvent, event->modifiers());
    pluginEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();
    bool consumed = false;
    if (dispatchPluginCanvasInput(pluginEvent, &consumed) && consumed)
    {
        event->accept();
        return;
    }

    if (m_intervalState != IntervalNone)
    {
        if (event->button() == Qt::RightButton)
            cancelIntervalSelection();
        return;
    }

    if (event->button() == Qt::LeftButton)
    {
        handleLeftMousePress(event);
    }
    else if (event->button() == Qt::RightButton)
    {
        showRightClickMenu(event);
    }
}

void ChartCanvas::mouseMoveEvent(QMouseEvent *event)
{
    // NoteChain native: dispatch before plugin
    if (m_noteChainModeActive && m_noteChainEditor) {
        double laneX = canvasXToLaneX(event->position().x());
        double beat = yToBeat(event->position().y());
        if (NoteChain::bridgeHandleMouseMove(m_noteChainEditor, event,
                laneX, beat)) {
            event->accept();
            update();
            return;
        }
    }

    // Range handle drag (before throttle to ensure responsive dragging)
    if (m_isDraggingRangeStart || m_isDraggingRangeEnd)
    {
        double rawBeat = yToBeat(event->position().y());
        double snapped = snapBeatToTimeDivision(rawBeat);

        if (m_isDraggingRangeStart)
        {
            if (snapped <= m_rangeEndBeat)
            {
                if (!qFuzzyCompare(m_rangeStartBeat, snapped))
                {
                    m_rangeStartBeat = snapped;
                    emit rangeStartChanged(snapped);
                    update();
                }
            }
        }
        else if (m_isDraggingRangeEnd)
        {
            if (snapped >= m_rangeStartBeat)
            {
                if (!qFuzzyCompare(m_rangeEndBeat, snapped))
                {
                    m_rangeEndBeat = snapped;
                    emit rangeEndChanged(snapped);
                    update();
                }
            }
        }
        event->accept();
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (nowMs - m_lastPluginMouseMoveDispatchMs < kPluginMouseMoveMinIntervalMs)
        return;
    m_lastPluginMouseMoveDispatchMs = nowMs;

    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "mouse_move";
    pluginEvent.x = event->position().x();
    pluginEvent.y = event->position().y();
    pluginEvent.button = static_cast<int>(Qt::NoButton);
    pluginEvent.buttons = static_cast<int>(event->buttons());
    fillPluginEventModifiers(&pluginEvent, event->modifiers());

    pluginEvent.timestampMs = nowMs;
    bool consumed = false;
    if (dispatchPluginCanvasInput(pluginEvent, &consumed) && consumed)
    {
        event->accept();
        return;
    }

    if (m_isDraggingMirrorGuide)
    {
        setMirrorAxisX(canvasXToLaneX(event->pos().x()));
    }
    else if (m_isSelecting)
    {
        if (m_selectionEnd != event->pos())
        {
            const QPointF oldEnd = m_selectionEnd;
            m_selectionEnd = event->pos();
            const QRect dirty = selectionPreviewDirtyRect(m_selectionStart, oldEnd, m_selectionEnd);
            update(dirty);
        }
    }
    else if (m_pendingMove)
    {
        QPointF delta = event->pos() - m_moveStartPos;
        if (qAbs(delta.x()) > kMoveDragThresholdPx || qAbs(delta.y()) > kMoveDragThresholdPx)
        {
            m_pendingMove = false;
            beginMoveSelection(m_moveStartPos, m_dragReferenceIndex);
            updateMoveSelection(event->pos());
        }
    }
    else if (m_isMovingSelection)
    {
        updateMoveSelection(event->pos());
    }
    else if (m_isDraggingPaste)
    {
        updateDragPaste(event->pos());
    }
}

bool ChartCanvas::handleSelectionRelease()
{
    if (!m_isSelecting)
        return false;

    const QRect dirty = QRectF(m_selectionStart, m_selectionEnd).normalized().toAlignedRect().adjusted(-2, -2, 2, 2);
    QRectF rect = QRectF(m_selectionStart, m_selectionEnd).normalized();
    m_selectionController->selectInRect(rect, chart()->notes(),
                                        [this](const Note &note)
                                        { return noteToPos(note); });
    m_isSelecting = false;
    update(dirty);
    return true;
}

bool ChartCanvas::handleMoveSelectionRelease()
{
    if (m_pendingMove)
    {
        m_pendingMove = false;
        return false;
    }

    if (!m_isMovingSelection)
        return false;
    endMoveSelection();
    return true;
}

bool ChartCanvas::handlePasteDragRelease()
{
    if (!m_isDraggingPaste)
        return false;
    endDragPaste();
    return true;
}


bool ChartCanvas::handleRangeHandleRelease()
{
    if (!m_isDraggingRangeStart && !m_isDraggingRangeEnd)
        return false;

    double startBeat = m_rangeStartBeat;
    double endBeat = m_rangeEndBeat;
    m_isDraggingRangeStart = false;
    m_isDraggingRangeEnd = false;
    setCursor(Qt::ArrowCursor);
    emit rangeDragFinished(startBeat, endBeat);
    return true;
}


bool ChartCanvas::handleMirrorGuideRelease()
{
    if (!m_isDraggingMirrorGuide)
        return false;

    m_isDraggingMirrorGuide = false;
    return true;
}

bool ChartCanvas::handleGenericDragRelease()
{
    if (!m_isDragging)
        return false;
    m_isDragging = false;
    m_draggedNotes.clear();
    return true;
}

void ChartCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    // NoteChain native: dispatch before plugin
    if (m_noteChainModeActive && m_noteChainEditor) {
        double laneX = canvasXToLaneX(event->position().x());
        double beat = yToBeat(event->position().y());
        if (NoteChain::bridgeHandleMouseRelease(m_noteChainEditor, event,
                laneX, beat)) {
            event->accept();
            update();
            return;
        }
    }

    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "mouse_up";
    pluginEvent.x = event->position().x();
    pluginEvent.y = event->position().y();
    pluginEvent.button = static_cast<int>(event->button());
    pluginEvent.buttons = static_cast<int>(event->buttons());
    fillPluginEventModifiers(&pluginEvent, event->modifiers());
    pluginEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();
    bool consumed = false;
    if (dispatchPluginCanvasInput(pluginEvent, &consumed) && consumed)
    {
        event->accept();
        return;
    }

    if (handleRangeHandleRelease())
        return;

    if (handleMirrorGuideRelease())
        return;
    if (handleSelectionRelease())
        return;
    if (handleMoveSelectionRelease())
        return;
    if (handlePasteDragRelease())
        return;
    handleGenericDragRelease();
}

void ChartCanvas::wheelEvent(QWheelEvent *event)
{
    const double wheelDeltaY = extractWheelDeltaY(event);

    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "wheel";
    pluginEvent.x = event->position().x();
    pluginEvent.y = event->position().y();
    pluginEvent.button = static_cast<int>(Qt::NoButton);
    pluginEvent.buttons = static_cast<int>(event->buttons());
    fillPluginEventModifiers(&pluginEvent, event->modifiers());
    pluginEvent.wheelDelta = wheelDeltaY;
    pluginEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();
    bool consumed = false;
    if (dispatchPluginCanvasInput(pluginEvent, &consumed) && consumed)
    {
        event->accept();
        return;
    }

    if (event->modifiers() & Qt::ControlModifier)
    {
        const double delta = wheelDeltaY;
        if (delta != 0)
        {
            double factor = (delta > 0) ? 1.2 : 1.0 / 1.2;
            double newScale = m_timeScale * factor;
            newScale = qBound(0.2, newScale, 10.0);
            setTimeScale(newScale);
        }
        event->accept();
        return;
    }

    m_isScrolling = true;
    stopSnapTimer();

    const double delta = wheelDeltaY;
    if (delta != 0)
    {
        double step = effectiveVisibleBeatRange() * kWheelScrollBeatStepRatio;
        double newPos = m_scrollBeat + (delta / 120.0) * step;
        if (newPos < 0)
            newPos = 0;
        const bool scrollChanged = qAbs(newPos - m_scrollBeat) >= 1e-6;
        if (scrollChanged && m_playbackController &&
            m_playbackController->state() == PlaybackController::Playing)
        {
            m_playbackController->pause();
        }
        m_scrollBeat = newPos;
        m_autoScrollEnabled = false;
        if (scrollChanged)
        {
            update();
            emit scrollPositionChanged(m_scrollBeat);
        }

        if (scrollChanged)
            syncCurrentPlayTimeToReferenceLine();
    }

    startSnapTimer();
}








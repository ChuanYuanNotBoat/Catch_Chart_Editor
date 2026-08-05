#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "model/Chart.h"
#include "utils/MathUtils.h"
#include "utils/Settings.h"
#include <algorithm>
#include <cmath>
#include <limits>

void ChartCanvas::handleCopy()
{
    if (!m_selectionController)
        return;

    QSet<int> selected = m_selectionController->selectedIndices();
    if (!selected.isEmpty())
    {
        m_selectionController->copySelected(chart()->notes());
        emit statusMessage(tr("Copied %1 notes").arg(selected.size()));
        if (m_intervalState != IntervalNone)
            cancelIntervalSelection();
        return;
    }

    if (m_intervalState == IntervalNone)
    {
        startIntervalSelection();
    }
    else if (m_intervalState == IntervalWaitingEnd)
    {
        completeIntervalSelection();
    }
}

void ChartCanvas::startIntervalSelection()
{
    double refTime = calculatePasteReferenceTime();
    m_intervalStartTime = refTime;
    m_intervalState = IntervalWaitingEnd;

    emit statusMessage(tr("Interval start at %1 ms. Adjust view and press Copy again to set end (Esc to cancel).").arg(refTime, 0, 'f', 0));
    setCursor(Qt::CrossCursor);
    update();
}

void ChartCanvas::completeIntervalSelection()
{
    if (m_intervalState != IntervalWaitingEnd)
        return;

    double endTime = calculatePasteReferenceTime();
    double startTime = m_intervalStartTime;
    if (startTime > endTime)
        std::swap(startTime, endTime);

    const auto &notes = chart()->notes();
    const auto &bpmList = chart()->bpmList();
    int offset = chart()->meta().offset;

    m_intervalNotes.clear();
    for (const Note &note : notes)
    {
        if (note.type == NoteType::SOUND)
            continue;
        double t = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmList, offset);
        if (t >= startTime && t <= endTime)
            m_intervalNotes.append(note);
    }

    m_intervalState = IntervalNone;
    setCursor(Qt::ArrowCursor);

    if (m_intervalNotes.isEmpty())
    {
        emit statusMessage(tr("No notes found in interval."));
        return;
    }

    m_selectionController->clearClipboard();
    for (const Note &note : m_intervalNotes)
        m_selectionController->getClipboard().append(note);

    beginPastePreview(m_intervalNotes);
    emit statusMessage(tr("Interval copied (%1 notes). Drag preview to adjust position, then click Confirm.").arg(m_intervalNotes.size()));
}

void ChartCanvas::cancelIntervalSelection()
{
    if (m_intervalState != IntervalNone)
    {
        m_intervalState = IntervalNone;
        setCursor(Qt::ArrowCursor);
        emit statusMessage(tr("Interval selection cancelled."));
        update();
    }
}

void ChartCanvas::cancelOperation()
{
    m_pendingMove = false;

    if (m_isPasting)
        cancelPaste();
    if (m_intervalState != IntervalNone)
        cancelIntervalSelection();
}

void ChartCanvas::paste()
{
    if (!m_selectionController)
        return;
    QVector<Note> clipboard = m_selectionController->getClipboard();
    if (clipboard.isEmpty())
    {
        emit statusMessage(tr("Clipboard is empty."));
        return;
    }
    beginPastePreview(clipboard);
}

void ChartCanvas::pasteAtCursor(const QPoint &pos)
{
    Q_ASSERT_X(!pos.isNull(), "ChartCanvas::pasteAtCursor", "Cursor position must be valid");
    if (!m_selectionController)
        return;
    QVector<Note> clipboard = m_selectionController->getClipboard();
    if (clipboard.isEmpty())
    {
        emit statusMessage(tr("Clipboard is empty."));
        return;
    }
    m_pasteCursorPos = pos;
    m_useCursorPaste = true;
    beginPastePreview(clipboard);
}

void ChartCanvas::beginPastePreview(const QVector<Note> &notes, const QPoint &cursorPos)
{
    m_pasteNotes = notes;
    m_pasteOriginalTimesMs.clear();
    m_pasteBaseOriginalTimeMs = std::numeric_limits<double>::max();
    m_pasteAnchorBeat = 0.0;
    m_isPasting = true;
    m_pasteTimeOffset = 0.0;
    m_pasteXOffset = 0.0;
    m_pasteTimeOffsetRaw = 0.0;
    m_pasteXOffsetRaw = 0.0;
    m_isDraggingPaste = false;

    if (!cursorPos.isNull())
    {
        m_pasteCursorPos = cursorPos;
        m_useCursorPaste = true;
    }
    else
    {
        m_useCursorPaste = false;
    }

    if (m_useCursorPaste)
    {
        m_pasteAnchorBeat = yToBeat(m_pasteCursorPos.y());
    }
    else
    {
        const double baselineRatio = kReferenceLineRatio;
        if (m_verticalFlip)
            m_pasteAnchorBeat = m_scrollBeat + (1.0 - baselineRatio) * effectiveVisibleBeatRange();
        else
            m_pasteAnchorBeat = m_scrollBeat + baselineRatio * effectiveVisibleBeatRange();
    }

    if (!m_pasteNotes.isEmpty())
    {
        const bool canBuildTimeCache =
            chart() &&
            !chart()->bpmList().isEmpty();
        if (canBuildTimeCache)
        {
            const auto &bpmList = chart()->bpmList();
            int offset = chart()->meta().offset;
            m_pasteOriginalTimesMs.resize(m_pasteNotes.size());
            for (int i = 0; i < m_pasteOriginalTimesMs.size(); ++i)
                m_pasteOriginalTimesMs[i] = std::numeric_limits<double>::quiet_NaN();

            for (int i = 0; i < m_pasteNotes.size(); ++i)
            {
                const Note &note = m_pasteNotes[i];
                if (note.type == NoteType::SOUND)
                    continue;
                const double t = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmList, offset);
                m_pasteOriginalTimesMs[i] = t;
                if (t < m_pasteBaseOriginalTimeMs)
                    m_pasteBaseOriginalTimeMs = t;
            }
        }
        double minBeat = std::numeric_limits<double>::max();
        int refIdx = 0;
        for (int i = 0; i < m_pasteNotes.size(); ++i)
        {
            const Note &note = m_pasteNotes[i];
            if (note.type == NoteType::SOUND)
                continue;
            double beat = MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator);
            if (beat < minBeat)
            {
                minBeat = beat;
                refIdx = i;
            }
        }
        m_pasteRefBeat = minBeat;
        m_pasteDragReferenceIndex = refIdx;
    }

    setFocus();
    update();
}

double ChartCanvas::calculatePasteReferenceTime() const
{
    if (!chart())
        return 0.0;

    const auto &bpmList = chart()->bpmList();
    int offset = chart()->meta().offset;

    if (m_useCursorPaste)
    {
        double y = m_pasteCursorPos.y();
        double beat = yToBeat(y);
        int beatNum, num, den;
        MathUtils::floatToBeat(beat, beatNum, num, den);
        return MathUtils::beatToMs(beatNum, num, den, bpmList, offset);
    }
    else
    {
        const double baselineRatio = kReferenceLineRatio;
        double baselineBeat;
        if (m_verticalFlip)
            baselineBeat = m_scrollBeat + (1.0 - baselineRatio) * effectiveVisibleBeatRange();
        else
            baselineBeat = m_scrollBeat + baselineRatio * effectiveVisibleBeatRange();
        int beatNum, num, den;
        MathUtils::floatToBeat(baselineBeat, beatNum, num, den);
        return MathUtils::beatToMs(beatNum, num, den, bpmList, offset);
    }
}

double ChartCanvas::snapPasteTimeOffset(double offsetBeat) const
{
    if (m_timeDivision <= 0 || m_pasteDragReferenceIndex < 0 || m_pasteDragReferenceIndex >= m_pasteNotes.size())
        return offsetBeat;

    const Note &refNote = m_pasteNotes[m_pasteDragReferenceIndex];
    double refOriginalBeat = MathUtils::beatToFloat(refNote.beatNum, refNote.numerator, refNote.denominator);
    double refNewBeat = refOriginalBeat + offsetBeat;

    Note tempNote = refNote;
    MathUtils::floatToBeat(refNewBeat, tempNote.beatNum, tempNote.numerator, tempNote.denominator);
    tempNote = MathUtils::snapNoteToTimeWithBoundary(tempNote, m_timeDivision);
    double refSnappedBeat = MathUtils::beatToFloat(tempNote.beatNum, tempNote.numerator, tempNote.denominator);
    return refSnappedBeat - refOriginalBeat;
}

void ChartCanvas::beginDragPaste(const QPointF &startPos)
{
    if (!m_isPasting)
        return;
    m_isDraggingPaste = true;
    m_pasteDragStartPos = startPos;
    m_pasteTimeOffsetRaw = m_pasteTimeOffset;
    m_pasteXOffsetRaw = m_pasteXOffset;

    refreshPluginOverlayCacheForSnap();
    m_pasteSnapReferenceActive = hasNoteSnapReferenceOverlays();
}

void ChartCanvas::updateDragPaste(const QPointF &currentPos)
{
    if (!m_isDraggingPaste)
        return;

    QPointF delta = currentPos - m_pasteDragStartPos;
    if (qAbs(delta.x()) < 1e-6 && qAbs(delta.y()) < 1e-6)
        return;

    double deltaY = delta.y();
    if (m_verticalFlip)
        deltaY = -deltaY;

    double deltaBeat = (deltaY / height()) * effectiveVisibleBeatRange();
    const int availableWidth = qMax(1, width() - leftMargin() - rightMargin());
    double deltaX = delta.x() / static_cast<double>(availableWidth) * static_cast<double>(kLaneWidth);

    m_pasteTimeOffsetRaw += deltaBeat;
    m_pasteXOffsetRaw += deltaX;

    // Keep unsnapped offsets to preserve sub-grid remainder while dragging.
    m_pasteTimeOffset = snapPasteTimeOffset(m_pasteTimeOffsetRaw);
    m_pasteXOffset = m_pasteXOffsetRaw;

    // Apply curve snap to the reference note (reuse move-selection logic).
    if (m_pasteSnapReferenceActive && m_pasteDragReferenceIndex >= 0 &&
        m_pasteDragReferenceIndex < m_pasteNotes.size()) {
        const Note &refNote = m_pasteNotes[m_pasteDragReferenceIndex];
        double refBeat = MathUtils::beatToFloat(refNote.beatNum, refNote.numerator, refNote.denominator) + m_pasteTimeOffset;
        int snappedX = qBound(0, refNote.x + qRound(m_pasteXOffset), kLaneWidth);
        if (curveSnapXForBeat(refBeat, snappedX, &snappedX))
            m_pasteXOffset = static_cast<double>(snappedX - refNote.x);
    }

    m_pasteDragStartPos = currentPos;
    update();
}

void ChartCanvas::endDragPaste()
{
    m_isDraggingPaste = false;
    m_pasteSnapReferenceActive = false;
}

void ChartCanvas::confirmPaste()
{
    if (!m_chartController || m_pasteNotes.isEmpty())
    {
        m_isPasting = false;
        m_pasteNotes.clear();
        m_pasteOriginalTimesMs.clear();
        m_pasteBaseOriginalTimeMs = std::numeric_limits<double>::max();
        m_pasteAnchorBeat = 0.0;
        return;
    }

    const auto &bpmList = chart()->bpmList();
    int offset = chart()->meta().offset;
    const QVector<MathUtils::BpmCacheEntry> &bpmCache = bpmTimeCache();
    auto beatFromTimeMs = [&bpmCache, &bpmList, offset](double ms) -> double
    {
        if (!bpmCache.isEmpty())
        {
            int lo = 0;
            int hi = bpmCache.size() - 1;
            while (lo < hi)
            {
                int mid = (lo + hi + 1) / 2;
                if (bpmCache[mid].accumulatedMs <= ms)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            const auto &seg = bpmCache[lo];
            if (seg.bpm <= 0.0)
                return seg.beatPos;
            return seg.beatPos + (ms - seg.accumulatedMs) * (seg.bpm / 60000.0);
        }
        int b = 0, n = 0, d = 1;
        MathUtils::msToBeat(ms, bpmList, offset, b, n, d);
        return MathUtils::beatToFloat(b, n, d);
    };
    auto assignBeatWithDen = [](double beatFloat, int targetDen, int &outBeatNum, int &outNum, int &outDen)
    {
        if (targetDen <= 0)
            targetDen = 1;
        const qint64 ticks = qRound64(beatFloat * targetDen);
        outBeatNum = static_cast<int>(ticks / targetDen);
        outNum = static_cast<int>(ticks % targetDen);
        if (outNum < 0)
        {
            outNum += targetDen;
            outBeatNum -= 1;
        }
        outDen = targetDen;
    };

    QVector<double> fallbackOriginalTimes;
    bool usingCachedOriginalTimes =
        (m_pasteOriginalTimesMs.size() == m_pasteNotes.size()) &&
        std::isfinite(m_pasteBaseOriginalTimeMs);
    if (!usingCachedOriginalTimes)
    {
        fallbackOriginalTimes.resize(m_pasteNotes.size());
        for (int i = 0; i < fallbackOriginalTimes.size(); ++i)
            fallbackOriginalTimes[i] = std::numeric_limits<double>::quiet_NaN();

        m_pasteBaseOriginalTimeMs = std::numeric_limits<double>::max();
        for (int i = 0; i < m_pasteNotes.size(); ++i)
        {
            const Note &note = m_pasteNotes[i];
            if (note.type == NoteType::SOUND)
                continue;
            const double t = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmList, offset);
            fallbackOriginalTimes[i] = t;
            if (t < m_pasteBaseOriginalTimeMs)
                m_pasteBaseOriginalTimeMs = t;
        }
        usingCachedOriginalTimes = std::isfinite(m_pasteBaseOriginalTimeMs);
    }

    const QVector<double> &sourceOriginalTimes = usingCachedOriginalTimes ? m_pasteOriginalTimesMs : fallbackOriginalTimes;
    const double baseOriginalTime = m_pasteBaseOriginalTimeMs;
    if (baseOriginalTime == std::numeric_limits<double>::max())
    {
        m_isPasting = false;
        m_pasteNotes.clear();
        m_pasteOriginalTimesMs.clear();
        m_pasteBaseOriginalTimeMs = std::numeric_limits<double>::max();
        m_pasteTimeOffsetRaw = 0.0;
        m_pasteXOffsetRaw = 0.0;
        m_pasteAnchorBeat = 0.0;
        update();
        return;
    }

    const double baseOriginalBeat = beatFromTimeMs(baseOriginalTime);
    const double referenceBeat = m_pasteAnchorBeat;
    const double baseBeatShift = referenceBeat - baseOriginalBeat;
    const double snappedTotalBeatShift = snapPasteTimeOffset(baseBeatShift + m_pasteTimeOffset);

    double finalXShift = m_pasteXOffset;

    // Align X via curve snap reference, same as move-selection logic.
    if (m_pasteSnapReferenceActive && m_pasteDragReferenceIndex >= 0 &&
        m_pasteDragReferenceIndex < m_pasteNotes.size()) {
        const Note &refNote = m_pasteNotes[m_pasteDragReferenceIndex];
        double refBeat = MathUtils::beatToFloat(refNote.beatNum, refNote.numerator, refNote.denominator) + m_pasteTimeOffset;
        int snappedX = qBound(0, refNote.x + qRound(finalXShift), kLaneWidth);
        if (curveSnapXForBeat(refBeat, snappedX, &snappedX))
            finalXShift = static_cast<double>(snappedX - refNote.x);
    }

    QVector<Note> newNotes;
    for (int i = 0; i < m_pasteNotes.size(); ++i)
    {
        const Note &originalNote = m_pasteNotes[i];
        if (originalNote.type == NoteType::SOUND)
            continue;
        if (i >= sourceOriginalTimes.size())
            continue;
        const double originalTime = sourceOriginalTimes[i];
        if (!std::isfinite(originalTime))
            continue;
        Note newNote = originalNote;
        newNote.id = Note::generateId();

        // Preserve original denominator, same as move-selection logic.
        const double originalBeatFloat = MathUtils::beatToFloat(originalNote.beatNum, originalNote.numerator, originalNote.denominator);
        const double newBeatFloat = originalBeatFloat + snappedTotalBeatShift;
        MathUtils::floatToBeat(newBeatFloat, newNote.beatNum, newNote.numerator, newNote.denominator);

        if (originalNote.type == NoteType::RAIN)
        {
            const double originalEndBeatFloat = MathUtils::beatToFloat(originalNote.endBeatNum, originalNote.endNumerator, originalNote.endDenominator);
            const double newEndBeatFloat = originalEndBeatFloat + snappedTotalBeatShift;
            MathUtils::floatToBeat(newEndBeatFloat, newNote.endBeatNum, newNote.endNumerator, newNote.endDenominator);
        }

        newNote.x = originalNote.x + qRound(finalXShift);
        newNote.x = qBound(0, newNote.x, kLaneWidth);

        newNotes.append(newNote);
    }

    if (!newNotes.isEmpty())
    {
        m_chartController->addNotes(newNotes);
        emit statusMessage(tr("Pasted %1 notes").arg(newNotes.size()));
    }

    m_isPasting = false;
    m_pasteNotes.clear();
    m_pasteOriginalTimesMs.clear();
    m_pasteBaseOriginalTimeMs = std::numeric_limits<double>::max();
    m_pasteTimeOffsetRaw = 0.0;
    m_pasteXOffsetRaw = 0.0;
    m_pasteAnchorBeat = 0.0;
    update();
}

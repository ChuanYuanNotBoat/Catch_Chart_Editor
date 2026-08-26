#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "audio/NoteSoundPlayer.h"
#include "audio/PlaybackTiming.h"
#include "utils/MathUtils.h"
#include "utils/PlaybackStutterProbe.h"
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include <QDateTime>
#include <QKeyEvent>
#include <QKeySequence>
#include <QCoreApplication>
#include <QHideEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <limits>


namespace
{
    void fillPluginEventModifiers(PluginInterface::CanvasInputEvent *outEvent, Qt::KeyboardModifiers eventModifiers)
    {
        if (!outEvent)
            return;
        outEvent->modifiers = static_cast<int>(eventModifiers);
        outEvent->shiftDown = eventModifiers.testFlag(Qt::ShiftModifier);
        outEvent->ctrlDown = eventModifiers.testFlag(Qt::ControlModifier);
    }

    PluginManager *activePluginManager()
    {
        auto *app = qobject_cast<Application *>(QCoreApplication::instance());
        if (!app || !app->pluginSystemReady())
            return nullptr;
        return app->pluginManager();
    }
}

void ChartCanvas::playbackPositionChanged(double timeMs)
{
    constexpr double kPlaybackVisualEpsilonMs = 0.05;

    if (m_timesDirty || m_noteDataDirty)
        rebuildNoteTimesCache();

    const double clampedTimeMs = qMax(0.0, timeMs);

    // During playback the high-resolution controller tick is the only clock
    // for visuals and note sounds. QMediaPlayer position callbacks are sparse,
    // especially at low rates, and are used only to re-anchor that clock.
    if (m_playbackController && m_playbackController->state() == PlaybackController::Playing)
        return;

    const bool visualChanged = std::abs(m_currentPlayTime - clampedTimeMs) > kPlaybackVisualEpsilonMs;
    m_currentPlayTime = clampedTimeMs;
    m_lastNoteSoundTimeMs = clampedTimeMs;
    m_nextPlayableNoteIndex = static_cast<int>(std::lower_bound(
                                                   m_playableNoteTimesMs.begin(),
                                                   m_playableNoteTimesMs.end(),
                                                   m_lastNoteSoundTimeMs) -
                                               m_playableNoteTimesMs.begin());
    if (visualChanged)
        update();
}

void ChartCanvas::advanceNoteSoundClock(double playbackTimeMs)
{
    constexpr double kComparisonEpsilonMs = 0.5;
    constexpr double kOutputLeadWallMs = 8.0;

    if (m_timesDirty || m_noteDataDirty)
        rebuildNoteTimesCache();

    if (!m_playbackController ||
        !m_noteSoundPlayer ||
        !m_noteSoundPlayer->isEnabled() ||
        !m_noteSoundPlayer->hasValidSound() ||
        m_playableNoteTimesMs.isEmpty())
    {
        m_lastNoteSoundTimeMs = qMax(0.0, playbackTimeMs);
        return;
    }

    // QSoundEffect starts asynchronously. A small wall-time lead both offsets
    // its output queue and centers the error of a 60 Hz controller pulse. The
    // conversion by playback rate is essential: 8 ms wall time is only 0.8 ms
    // on the media timeline at 0.1x.
    const double schedulingTimeMs = qMax(
        0.0,
        playbackTimeMs + PlaybackTiming::wallDurationToMediaMs(
                             kOutputLeadWallMs,
                             m_playbackController->speed()));

    if (schedulingTimeMs < m_lastNoteSoundTimeMs - 2.0)
    {
        m_nextPlayableNoteIndex = static_cast<int>(std::lower_bound(
                                                       m_playableNoteTimesMs.begin(),
                                                       m_playableNoteTimesMs.end(),
                                                       schedulingTimeMs) -
                                                   m_playableNoteTimesMs.begin());
    }

    double lastTriggeredTimeMs = -std::numeric_limits<double>::infinity();
    while (m_nextPlayableNoteIndex < m_playableNoteTimesMs.size() &&
           m_playableNoteTimesMs[m_nextPlayableNoteIndex] <=
               schedulingTimeMs + kComparisonEpsilonMs)
    {
        const double noteTimeMs = m_playableNoteTimesMs[m_nextPlayableNoteIndex];
        if (noteTimeMs > m_lastNoteSoundTimeMs + kComparisonEpsilonMs &&
            noteTimeMs > lastTriggeredTimeMs + kComparisonEpsilonMs)
        {
            m_noteSoundPlayer->playHitSound();
            lastTriggeredTimeMs = noteTimeMs;
        }
        ++m_nextPlayableNoteIndex;
    }

    m_lastNoteSoundTimeMs = schedulingTimeMs;
}

void ChartCanvas::playFromReferenceLine()
{
    if (!m_playbackController)
        return;

    if (m_currentPlayTime < 0)
        m_currentPlayTime = 0;

    if (m_playbackController->state() == PlaybackController::Playing)
    {
        m_playbackController->pause();
    }

    m_autoScrollEnabled = true;
    m_playbackController->playFromTime(m_currentPlayTime);
}

double ChartCanvas::currentPlayTime() const
{
    return m_currentPlayTime;
}

void ChartCanvas::recordManualJerkMark()
{
    PlaybackStutterProbe::markManualJerk(m_currentPlayTime, m_lastPlaybackFrameSeq);
    emit statusMessage(tr("Manual jerk mark recorded (F8)."));
}

void ChartCanvas::onSelectionChanged()
{
}

void ChartCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F8)
    {
        recordManualJerkMark();
        event->accept();
        return;
    }

    // NoteChain native: keyboard shortcuts
    if (m_noteChainModeActive && m_noteChainEditor) {
        m_noteChainEditor->setHostContext(buildPluginCanvasContext());
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            m_noteChainEditor->commitCurveToNotes();
            event->accept(); return;
        }
        if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
            m_noteChainEditor->deleteSelected();
            event->accept(); return;
        }
        if (event->key() == Qt::Key_Escape) {
            m_noteChainEditor->handleKeyDown(Qt::Key_Escape, false, false);
            event->accept(); return;
        }
        if (event->key() == Qt::Key_A && !event->modifiers().testFlag(Qt::ControlModifier)) {
            m_noteChainEditor->toggleAnchorPlacement();
            event->accept(); return;
        }
        if (event->matches(QKeySequence::Undo)) {
            if (m_chartController && m_chartController->canUndo()) {
                const QString actionText = m_chartController->nextUndoActionText();
                m_chartController->undo();
                m_noteChainEditor->onHostUndo(actionText);
            } else {
                m_noteChainEditor->undo();
            }
            event->accept(); return;
        }
        if (event->matches(QKeySequence::Redo)) {
            if (m_chartController && m_chartController->canRedo()) {
                const QString actionText = m_chartController->nextRedoActionText();
                m_chartController->redo();
                m_noteChainEditor->onHostRedo(actionText);
            } else {
                m_noteChainEditor->redo();
            }
            event->accept(); return;
        }
    }

    if (m_pluginToolModeActive && event->key() == Qt::Key_Return)
    {
        if (triggerPluginBatchAction("commit_curve_to_notes", tr("Commit Curve -> Notes")))
        {
            event->accept();
            return;
        }
    }
    if (m_pluginToolModeActive && event->key() == Qt::Key_Enter)
    {
        if (triggerPluginBatchAction("commit_curve_to_notes", tr("Commit Curve -> Notes")))
        {
            event->accept();
            return;
        }
    }
    if (m_pluginToolModeActive && event->key() == Qt::Key_Escape)
    {
        PluginInterface::CanvasInputEvent cancelEvent;
        cancelEvent.type = "cancel";
        fillPluginEventModifiers(&cancelEvent, event->modifiers());
        cancelEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();
        bool consumedCancel = false;
        if (dispatchPluginCanvasInput(cancelEvent, &consumedCancel) && consumedCancel)
        {
            event->accept();
            return;
        }
    }

    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "key_down";
    pluginEvent.key = event->key();
    fillPluginEventModifiers(&pluginEvent, event->modifiers());
    pluginEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();
    bool consumed = false;
    if (dispatchPluginCanvasInput(pluginEvent, &consumed) && consumed)
    {
        event->accept();
        return;
    }

    if (m_pluginToolModeActive && event->matches(QKeySequence::Undo))
    {
        bool handled = false;
        if (m_chartController && m_chartController->canUndo())
        {
            const QString actionText = m_chartController->nextUndoActionText();
            m_chartController->undo();
            if (PluginManager *pm = activePluginManager())
                pm->notifyHostUndo(actionText);
            handled = true;
        }
        if (handled)
        {
            event->accept();
            return;
        }
    }
    if (m_pluginToolModeActive && event->matches(QKeySequence::Redo))
    {
        bool handled = false;
        if (m_chartController && m_chartController->canRedo())
        {
            const QString actionText = m_chartController->nextRedoActionText();
            m_chartController->redo();
            if (PluginManager *pm = activePluginManager())
                pm->notifyHostRedo(actionText);
            handled = true;
        }
        if (handled)
        {
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape)
    {
        cancelOperation();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Delete)
    {
        if (m_selectionController && !m_selectionController->selectedIndices().isEmpty())
        {
            QSet<int> selected = m_selectionController->selectedIndices();
            const auto &notes = chart()->notes();
            QList<int> sorted = selected.values();
            std::sort(sorted.begin(), sorted.end(), std::greater<int>());

            QVector<Note> notesToDelete;
            for (int idx : sorted)
            {
                if (idx >= 0 && idx < notes.size())
                {
                    notesToDelete.append(notes[idx]);
                }
            }

            if (!notesToDelete.isEmpty())
            {
                m_chartController->removeNotes(notesToDelete);
            }

            m_selectionController->clearSelection();
        }
    }

    // ---- Arrow key navigation ----

    // ↑ / ↓ : scroll by one time division (snapped)
    // Shift + ↑ / ↓ : scroll by one beat (snapped)
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
    {
        const bool shiftHeld = event->modifiers().testFlag(Qt::ShiftModifier);
        double step = shiftHeld ? 1.0 : (m_timeDivision > 0 ? 1.0 / m_timeDivision : 1.0);

        if (event->key() == Qt::Key_Down)
            step = -step;

        double newScrollBeat = m_scrollBeat + step;
        newScrollBeat = snapBeatToTimeDivision(newScrollBeat);
        if (newScrollBeat < 0.0)
            newScrollBeat = 0.0;

        if (qAbs(newScrollBeat - m_scrollBeat) >= 1e-6)
        {
            if (m_playbackController
                && m_playbackController->state() == PlaybackController::Playing)
            {
                m_playbackController->pause();
            }

            m_scrollBeat = newScrollBeat;
            m_autoScrollEnabled = false;
            update();
            emit scrollPositionChanged(m_scrollBeat);
            syncCurrentPlayTimeToReferenceLine();
        }
        event->accept();
        return;
    }

    // ← / → : select previous / next note (single select)
    // Shift + ← / → : range selection (text-editor style, anchor + movable extent)
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
    {
        if (!m_selectionController || !chart())
        {
            event->accept();
            return;
        }

        const auto &notes = chart()->notes();
        if (notes.isEmpty())
        {
            event->accept();
            return;
        }

        const bool shiftHeld = event->modifiers().testFlag(Qt::ShiftModifier);
        const QSet<int> currentSelection = m_selectionController->selectedIndices();

        if (currentSelection.isEmpty())
        {
            // Nothing selected yet: find the note closest to the reference line beat.
            const double baselineRatio = kReferenceLineRatio;
            const double refBeat = m_verticalFlip
                ? m_scrollBeat + (1.0 - baselineRatio) * effectiveVisibleBeatRange()
                : m_scrollBeat + baselineRatio * effectiveVisibleBeatRange();

            int closestIndex = -1;
            double closestDist = std::numeric_limits<double>::max();
            int closestX = kLaneWidth + 1;

            for (int i = 0; i < notes.size(); ++i)
            {
                const double noteBeat = MathUtils::beatToFloat(
                    notes[i].beatNum, notes[i].numerator, notes[i].denominator);
                const double dist = qAbs(noteBeat - refBeat);

                if (dist < closestDist - 1e-9
                    || (qAbs(dist - closestDist) < 1e-9 && notes[i].x < closestX))
                {
                    closestDist = dist;
                    closestIndex = i;
                    closestX = notes[i].x;
                }
            }

            if (closestIndex >= 0)
            {
                m_selectionController->select(closestIndex);
                m_selectionAnchorIndex = closestIndex;
                m_selectionExtentIndex = closestIndex;
                autoScrollToNote(notes[closestIndex]);
            }
            event->accept();
            return;
        }

        // --- Existing selection ---
        if (!shiftHeld)
        {
            // No shift: single-select next / previous note, reset anchor & extent
            QList<int> sorted = currentSelection.values();
            std::sort(sorted.begin(), sorted.end());
            const int minSel = sorted.first();
            const int maxSel = sorted.last();

            int targetIndex = -1;
            if (event->key() == Qt::Key_Right)
            {
                if (maxSel + 1 < notes.size())
                    targetIndex = maxSel + 1;
            }
            else // Qt::Key_Left
            {
                if (minSel - 1 >= 0)
                    targetIndex = minSel - 1;
            }

            if (targetIndex >= 0 && targetIndex < notes.size())
            {
                m_selectionController->select(targetIndex);
                m_selectionAnchorIndex = targetIndex;
                m_selectionExtentIndex = targetIndex;
                autoScrollToNote(notes[targetIndex]);
            }
        }
        else
        {
            // Shift held: text-editor style range selection
            // Anchor stays fixed, extent moves with the arrow.
            if (m_selectionExtentIndex < 0 ||
                m_selectionAnchorIndex < 0 ||
                m_selectionExtentIndex >= notes.size())
            {
                // Stale state – reset to current selection bounds
                QList<int> sorted = currentSelection.values();
                std::sort(sorted.begin(), sorted.end());
                m_selectionAnchorIndex = sorted.first();
                m_selectionExtentIndex = sorted.last();
            }

            const int delta = (event->key() == Qt::Key_Right) ? 1 : -1;
            const int newExtent = m_selectionExtentIndex + delta;

            if (newExtent >= 0 && newExtent < notes.size())
            {
                m_selectionExtentIndex = newExtent;

                // Compute the range [low, high] from anchor to extent (inclusive)
                const int low = qMin(m_selectionAnchorIndex, m_selectionExtentIndex);
                const int high = qMax(m_selectionAnchorIndex, m_selectionExtentIndex);

                QSet<int> newSelection;
                for (int i = low; i <= high; ++i)
                    newSelection.insert(i);

                m_selectionController->select(newSelection);
                autoScrollToNote(notes[newExtent]);

                // Play note sound for the newly added end
                if (m_noteSoundPlayer && m_noteSoundPlayer->isEnabled())
                {
                    m_noteSoundPlayer->playHitSound();
                }
            }
            // else: already at edge, no-op
        }

        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ChartCanvas::keyReleaseEvent(QKeyEvent *event)
{
    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "key_up";
    pluginEvent.key = event->key();
    fillPluginEventModifiers(&pluginEvent, event->modifiers());
    pluginEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();
    bool consumed = false;
    if (dispatchPluginCanvasInput(pluginEvent, &consumed) && consumed)
    {
        event->accept();
        return;
    }

    QWidget::keyReleaseEvent(event);
}


void ChartCanvas::autoScrollToNote(const Note &note)
{
    const double noteBeat = MathUtils::beatToFloat(
        note.beatNum, note.numerator, note.denominator);
    const double visibleRange = effectiveVisibleBeatRange();
    const double margin = visibleRange * 0.1;

    bool scrolled = false;
    if (noteBeat < m_scrollBeat + margin)
    {
        m_scrollBeat = noteBeat - margin;
        scrolled = true;
    }
    else if (noteBeat > m_scrollBeat + visibleRange - margin)
    {
        m_scrollBeat = noteBeat - visibleRange + margin;
        scrolled = true;
    }
    if (m_scrollBeat < 0.0)
        m_scrollBeat = 0.0;

    if (scrolled)
    {
        update();
        emit scrollPositionChanged(m_scrollBeat);
    }
}

int ChartCanvas::leftMargin() const
{
    return width() / kSideMarginDivisor;
}

int ChartCanvas::rightMargin() const
{
    return width() / kSideMarginDivisor;
}

void ChartCanvas::snapPlayheadToGrid()
{
    if (!chart() || !m_snapToGrid)
    {
        return;
    }

    if (m_playbackController && m_playbackController->state() == PlaybackController::Playing)
        return;

    double currentTime = m_currentPlayTime;
    const auto &bpmList = chart()->bpmList();
    int offset = chart()->meta().offset;

    double snappedTime = MathUtils::snapTimeToGrid(currentTime, bpmList, offset, m_timeDivision);

    if (std::abs(snappedTime - currentTime) > 1e-6)
    {
        m_currentPlayTime = snappedTime;
        if (m_playbackController)
        {
            m_playbackController->seekTo(snappedTime);
        }
        update();
    }
}

void ChartCanvas::startSnapTimer()
{
    stopSnapTimer();
    m_snapTimerId = startTimer(300);
}

void ChartCanvas::stopSnapTimer()
{
    if (m_snapTimerId != 0)
    {
        killTimer(m_snapTimerId);
        m_snapTimerId = 0;
    }
}

void ChartCanvas::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_snapTimerId)
    {
        m_isScrolling = false;
        snapPlayheadToGrid();
        stopSnapTimer();
    }
    QWidget::timerEvent(event);
}

void ChartCanvas::resizeEvent(QResizeEvent *event)
{
    m_backgroundCacheDirty = true;
    invalidateGridCache();
    QWidget::resizeEvent(event);
}

void ChartCanvas::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    attachDisplayFrameWindow();
    if (m_isPlaying)
        requestNextFrame();
}

void ChartCanvas::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
}

void ChartCanvas::attachDisplayFrameWindow()
{
    QWidget *topLevel = window();
    QWindow *windowHandle = topLevel ? topLevel->windowHandle() : nullptr;
    if (windowHandle == m_displayFrameWindow)
    {
        updateDisplayRefreshRate(windowHandle ? windowHandle->screen() : nullptr);
        return;
    }

    QObject::disconnect(m_displayWindowDestroyedConnection);
    QObject::disconnect(m_displayScreenChangedConnection);
    m_displayFrameWindow = windowHandle;

    if (!windowHandle)
    {
        updateDisplayRefreshRate(nullptr);
        return;
    }

    m_displayWindowDestroyedConnection = connect(
        windowHandle,
        &QObject::destroyed,
        this,
        [this]()
        {
            m_displayFrameWindow.clear();
        });
    m_displayScreenChangedConnection = connect(
        windowHandle,
        &QWindow::screenChanged,
        this,
        &ChartCanvas::updateDisplayRefreshRate);
    updateDisplayRefreshRate(windowHandle->screen());
}

void ChartCanvas::updateDisplayRefreshRate(QScreen *screen)
{
    if (screen != m_displayFrameScreen)
    {
        QObject::disconnect(m_displayRefreshRateConnection);
        m_displayFrameScreen = screen;
        if (screen)
        {
            m_displayRefreshRateConnection = connect(
                screen,
                &QScreen::refreshRateChanged,
                this,
                [this](qreal refreshRateHz)
                {
                    if (m_playbackController)
                        m_playbackController->setDisplayRefreshRate(refreshRateHz);
                });
        }
    }

    if (m_playbackController)
        m_playbackController->setDisplayRefreshRate(screen ? screen->refreshRate() : 60.0);
}

void ChartCanvas::cancelPaste()
{
    if (m_isPasting)
    {
        m_isPasting = false;
        m_pasteNotes.clear();
        m_pasteOriginalTimesMs.clear();
        m_pasteBaseOriginalTimeMs = std::numeric_limits<double>::max();
        m_pasteTimeOffsetRaw = 0.0;
        m_pasteXOffsetRaw = 0.0;
        m_pasteAnchorBeat = 0.0;
        m_pasteSnapReferenceActive = false;
        update();
        emit statusMessage(tr("Paste cancelled."));
    }
}

#include "ChartCanvas.h"
#include "controller/ChartController.h"
#include "controller/SelectionController.h"
#include "controller/PlaybackController.h"
#include "audio/NoteSoundPlayer.h"
#include "render/NoteRenderer.h"
#include "render/GridRenderer.h"
#include "render/BackgroundRenderer.h"
#include "render/HyperfruitDetector.h"
#include "utils/MathUtils.h"
#include "utils/Settings.h"
#include "utils/Logger.h"
#include "utils/PlaybackStutterProbe.h"
#include "utils/DiagnosticCollector.h"
#include "model/Chart.h"
#include "app/Application.h"
#include "plugin/PluginManager.h"
#include <QPainter>
#include <QPen>
#include <QFileInfo>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace
{
    PluginManager *activePluginManager()
    {
        auto *app = qobject_cast<Application *>(QCoreApplication::instance());
        if (!app || !app->pluginSystemReady())
            return nullptr;
        return app->pluginManager();
    }

    QString sidecarDirForChart(const QString &chartPath)
    {
        if (chartPath.trimmed().isEmpty())
            return QString();
        const QFileInfo fi(chartPath);
        if (!fi.exists())
            return QString();
        return fi.absoluteDir().filePath(".mcce-plugin");
    }

    QVariantMap serializeSelectedNoteForPlugin(const Note &note)
    {
        QVariantMap noteObj;
        noteObj.insert("id", note.id);
        noteObj.insert("x", note.x);
        noteObj.insert("lane_x", note.x);
        noteObj.insert("beat", MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator));
        return noteObj;
    }

    QVariantMap serializeNotePositionForPlugin(const Note &note)
    {
        QVariantMap noteObj;
        noteObj.insert("type", static_cast<int>(note.type));
        noteObj.insert("x", note.x);

        QVariantList beat;
        beat.append(note.beatNum);
        beat.append(note.numerator);
        beat.append(note.denominator);
        noteObj.insert("beat", beat);
        return noteObj;
    }
}

void ChartCanvas::showGridSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Grid Settings"));
    QFormLayout form(&dialog);
    QCheckBox *snapCheck = new QCheckBox(tr("Enable Grid Snap"));
    snapCheck->setChecked(m_gridSnap);
    QSpinBox *divisionSpin = new QSpinBox;
    divisionSpin->setRange(4, 64);
    divisionSpin->setValue(m_gridDivision);
    form.addRow(tr("Snap to Grid:"), snapCheck);
    form.addRow(tr("Grid Divisions (4-64):"), divisionSpin);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(buttons);

    if (dialog.exec() == QDialog::Accepted)
    {
        setGridSnap(snapCheck->isChecked());
        setGridDivision(divisionSpin->value());
    }
}

void ChartCanvas::setTimeScale(double scale)
{
    const double clampedScale = qBound(0.2, scale, 10.0);
    if (qFuzzyCompare(m_timeScale, clampedScale))
        return;

    const double baselineRatio = kReferenceLineRatio;
    double baselineBeat;
    if (m_verticalFlip)
    {
        baselineBeat = m_scrollBeat + (1.0 - baselineRatio) * effectiveVisibleBeatRange();
    }
    else
    {
        baselineBeat = m_scrollBeat + baselineRatio * effectiveVisibleBeatRange();
    }

    m_timeScale = clampedScale;

    if (m_verticalFlip)
    {
        m_scrollBeat = baselineBeat - (1.0 - baselineRatio) * effectiveVisibleBeatRange();
    }
    else
    {
        m_scrollBeat = baselineBeat - baselineRatio * effectiveVisibleBeatRange();
    }
    if (m_scrollBeat < 0)
        m_scrollBeat = 0;

    invalidateGridCache();
    update();
    emit scrollPositionChanged(m_scrollBeat);
    emit timeScaleChanged(m_timeScale);
}

void ChartCanvas::setMirrorAxisX(int axisX)
{
    const int clamped = clampMirrorAxisX(axisX);
    if (m_mirrorAxisX == clamped)
        return;

    m_mirrorAxisX = clamped;
    emit mirrorAxisChanged(m_mirrorAxisX);
    update();
}

void ChartCanvas::setMirrorGuideVisible(bool visible)
{
    if (m_mirrorGuideVisible == visible)
        return;
    m_mirrorGuideVisible = visible;
    update();
}

void ChartCanvas::setMirrorPreviewVisible(bool visible)
{
    if (m_mirrorPreviewVisible == visible)
        return;
    m_mirrorPreviewVisible = visible;
    update();
}

bool ChartCanvas::flipSelectedNotes()
{
    return performMirrorFlip(collectMirrorTargetIndices(QPoint()), m_mirrorAxisX, tr("Mirror Flip Notes"));
}

bool ChartCanvas::flipSelectedNotesAroundCenter()
{
    return performMirrorFlip(collectMirrorTargetIndices(QPoint()), kLaneWidth / 2, tr("Mirror Flip Notes"));
}

int ChartCanvas::clampMirrorAxisX(int axisX) const
{
    return qBound(0, axisX, kLaneWidth);
}

int ChartCanvas::canvasXToLaneX(double canvasX) const
{
    const int lmargin = leftMargin();
    const int rmargin = rightMargin();
    const int availableWidth = qMax(1, width() - lmargin - rmargin);
    const double normalized = (canvasX - lmargin) / static_cast<double>(availableWidth);
    return clampMirrorAxisX(qRound(normalized * kLaneWidth));
}

double ChartCanvas::laneXToCanvasX(int laneX) const
{
    const int lmargin = leftMargin();
    const int rmargin = rightMargin();
    const int availableWidth = qMax(1, width() - lmargin - rmargin);
    return lmargin + (clampMirrorAxisX(laneX) / static_cast<double>(kLaneWidth)) * availableWidth;
}

bool ChartCanvas::isMirrorGuideHandleHit(const QPointF &pos) const
{
    if (!m_mirrorGuideVisible)
        return false;

    const double axisCanvasX = laneXToCanvasX(m_mirrorAxisX);
    const QPointF topHandle(axisCanvasX, 14.0);
    const QPointF bottomHandle(axisCanvasX, height() - 14.0);
    constexpr double kHandleRadius = 10.0;

    return QLineF(pos, topHandle).length() <= kHandleRadius + 2.0 ||
           QLineF(pos, bottomHandle).length() <= kHandleRadius + 2.0;
}

void ChartCanvas::updateBackgroundCache()
{
    m_backgroundCacheDirty = true;
}

void ChartCanvas::invalidateGridCache()
{
    m_gridCacheValid = false;
    m_gridCache = QPixmap();
    m_gridCacheRect = QRect();
    m_gridCachePadPx = 0;
}

void ChartCanvas::refreshBackground()
{
    updateBackgroundCache();
    // When auto-scroll is disabled during playback, playbackPositionChanged()
    // already drives repaint; avoid duplicate frame updates from both paths.
    if (!m_autoScrollEnabled && m_playbackController &&
        m_playbackController->state() == PlaybackController::Playing)
        return;

    update();
}

void ChartCanvas::advancePlaybackVisual(bool scheduleRepaint, bool recordProbe)
{
    constexpr double kScrollSignalEpsilonBeat = 1e-6;

    if (!m_isPlaying || !chart())
        return;

    QElapsedTimer timer;
    timer.start();

    if (m_autoScrollEnabled)
    {
        const QVector<MathUtils::BpmCacheEntry> &cache = bpmTimeCache();
        if (cache.isEmpty())
            return;

        auto beatFromTimeMs = [&cache](double timeMs) -> double
        {
            int lo = 0;
            int hi = cache.size() - 1;
            while (lo < hi)
            {
                const int mid = (lo + hi + 1) / 2;
                if (cache[mid].accumulatedMs <= timeMs)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            const auto &seg = cache[lo];
            if (seg.bpm <= 0.0)
                return seg.beatPos;
            return seg.beatPos + (timeMs - seg.accumulatedMs) * (seg.bpm / 60000.0);
        };
        const double beat = beatFromTimeMs(m_currentPlayTime);

        const double baselineRatio = kReferenceLineRatio;
        double targetScrollBeat;
        if (m_verticalFlip)
        {
            targetScrollBeat = beat - (1.0 - baselineRatio) * effectiveVisibleBeatRange();
        }
        else
        {
            targetScrollBeat = beat - baselineRatio * effectiveVisibleBeatRange();
        }

        const double previousScrollBeat = m_scrollBeat;
        m_scrollBeat = targetScrollBeat;
        if (m_scrollBeat < 0)
            m_scrollBeat = 0;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool scrollChanged = std::abs(m_scrollBeat - previousScrollBeat) > kScrollSignalEpsilonBeat;
        if (!m_isPlaying &&
            scrollChanged &&
            (m_lastScrollSignalTimeMs == 0 || nowMs - m_lastScrollSignalTimeMs >= kScrollSignalIntervalMs))
        {
            emit scrollPositionChanged(m_scrollBeat);
            m_lastScrollSignalTimeMs = nowMs;
            PlaybackStutterProbe::recordCounter("canvas.scroll_signal_emits", 1, m_isPlaying);
        }
    }

    if (scheduleRepaint)
        update();
    if (recordProbe)
    {
        PlaybackStutterProbe::recordDuration("canvas.request_next_frame",
                                             static_cast<double>(timer.nsecsElapsed()) / 1000000.0,
                                             1.5,
                                             m_isPlaying);
    }
}

void ChartCanvas::requestNextFrame()
{
    advancePlaybackVisual(true, true);
}

void ChartCanvas::onPlaybackFrameTick(double predictedTimeMs, qint64 frameSeq)
{
    constexpr double kVisualScrollJerkBudgetPx = 1.4;
    constexpr double kVisualPlayheadStepJerkBudgetPx = 1.4;
    constexpr double kVisualPlayheadDriftBudgetPx = 0.9;
    constexpr double kVisualScrollStepJankThresholdPx = 3.5;
    constexpr double kVisualPlayheadStepJankThresholdPx = 2.4;
    constexpr double kVisualPlayheadDriftEventThresholdPx = 2.0;
    const bool lightProbeSample = (frameSeq % 3) == 0;
    if (!m_isPlaying)
        return;
    QElapsedTimer timer;
    timer.start();

    if (frameSeq <= m_lastPlaybackFrameSeq)
    {
        PlaybackStutterProbe::recordCounter("canvas.tick_out_of_order", 1, true);
        return;
    }

    if (m_lastPlaybackFrameSeq >= 0 && frameSeq > m_lastPlaybackFrameSeq + 1)
        PlaybackStutterProbe::recordCounter("canvas.tick_gap", frameSeq - m_lastPlaybackFrameSeq - 1, true);

    if (m_lastPlaybackPredictedTimeMs >= 0.0)
    {
        const double stepMs = qMax(0.0, predictedTimeMs - m_lastPlaybackPredictedTimeMs);
        PlaybackStutterProbe::recordDuration("playback.time_step_ms", stepMs, 20.0, true);
        if (m_lastPlaybackStepMs >= 0.0)
        {
            const double stepJerkMs = std::abs(stepMs - m_lastPlaybackStepMs);
            PlaybackStutterProbe::recordDuration("playback.time_step_jerk", stepJerkMs, 4.0, true);
            if (stepJerkMs > 8.0)
                PlaybackStutterProbe::recordCounter("playback.time_step_jank_events", 1, true);
        }
        m_lastPlaybackStepMs = stepMs;
    }

    const double previousScrollBeat = m_scrollBeat;
    const double previousPlayheadYPx = m_lastPlaybackPlayheadYPx;

    m_lastPlaybackPredictedTimeMs = predictedTimeMs;
    m_lastPlaybackFrameSeq = frameSeq;
    m_lastPlaybackTickNs = m_playbackVisualClock.nsecsElapsed();
    m_lastPlaybackTargetTimeMs = qMax(0.0, predictedTimeMs);
    m_currentPlayTime = m_lastPlaybackTargetTimeMs;
    PlaybackStutterProbe::markUiHeartbeat(true);
    m_playbackVisualFramePending = false;
    advancePlaybackVisual(true, lightProbeSample);

    const double visibleRange = qMax(1e-6, effectiveVisibleBeatRange());
    const double pixelsPerBeat = (height() > 0) ? (static_cast<double>(height()) / visibleRange) : 0.0;
    const double scrollDeltaBeat = m_scrollBeat - previousScrollBeat;
    const double scrollStepPx = std::abs(scrollDeltaBeat) * pixelsPerBeat;
    if (lightProbeSample)
        PlaybackStutterProbe::recordDuration("visual.scroll_step_px", scrollStepPx, 24.0, true);
    if (m_lastPlaybackScrollStepPx >= 0.0)
    {
        const double scrollJerkPx = std::abs(scrollStepPx - m_lastPlaybackScrollStepPx);
        if (lightProbeSample)
            PlaybackStutterProbe::recordDuration("visual.scroll_step_jerk_px", scrollJerkPx, kVisualScrollJerkBudgetPx, true);
        if (lightProbeSample && scrollJerkPx > kVisualScrollStepJankThresholdPx)
            PlaybackStutterProbe::recordCounter("visual.scroll_step_jank_events", 1, true);
    }
    m_lastPlaybackScrollStepPx = scrollStepPx;
    if (lightProbeSample && scrollDeltaBeat < -1e-5)
        PlaybackStutterProbe::recordCounter("visual.scroll_backtrack_events", 1, true);

    const double playheadYPx = yPosFromTime(m_currentPlayTime);
    const double baselineY = static_cast<double>(height()) * kReferenceLineRatio;
    const double playheadDriftPx = std::abs(playheadYPx - baselineY);
    if (lightProbeSample)
        PlaybackStutterProbe::recordDuration("visual.playhead_drift_px", playheadDriftPx, kVisualPlayheadDriftBudgetPx, true);
    if (lightProbeSample && playheadDriftPx > kVisualPlayheadDriftEventThresholdPx)
        PlaybackStutterProbe::recordCounter("visual.playhead_drift_events", 1, true);

    if (previousPlayheadYPx >= 0.0)
    {
        const double playheadStepPx = std::abs(playheadYPx - previousPlayheadYPx);
        if (lightProbeSample)
            PlaybackStutterProbe::recordDuration("visual.playhead_step_px", playheadStepPx, 18.0, true);
        if (m_lastPlaybackPlayheadStepPx >= 0.0)
        {
            const double stepJerkPx = std::abs(playheadStepPx - m_lastPlaybackPlayheadStepPx);
            if (lightProbeSample)
                PlaybackStutterProbe::recordDuration("visual.playhead_step_jerk_px", stepJerkPx, kVisualPlayheadStepJerkBudgetPx, true);
            if (lightProbeSample && stepJerkPx > kVisualPlayheadStepJankThresholdPx)
                PlaybackStutterProbe::recordCounter("visual.playhead_step_jank_events", 1, true);
        }
        m_lastPlaybackPlayheadStepPx = playheadStepPx;
    }
    m_lastPlaybackPlayheadYPx = playheadYPx;

    if (lightProbeSample)
    {
        PlaybackStutterProbe::recordDuration("canvas.playback_tick_handler",
                                             static_cast<double>(timer.nsecsElapsed()) / 1000000.0,
                                             1.0,
                                             true);
    }
}

void ChartCanvas::setPluginToolMode(bool enabled, const QString &pluginId)
{
    m_pluginToolModeActive = enabled;
    if (!pluginId.trimmed().isEmpty())
        m_pluginToolPluginId = pluginId.trimmed();
    if (!enabled)
    {
        stopOverlayQueryTimer();
        m_overlayCache.clear();
        m_eventOverlayCache.clear();
        m_lastOverlayQueryMs = 0;
        m_overlayQueryBlockedUntilMs = 0;
        m_overlayPlaybackIntervalMs = kOverlayQueryIntervalMsToolModePlaying;
        applyPluginCursor(QString());
    }
    else
    {
        // Trigger an immediate query so overlay appears without waiting for timer
        m_overlayQueryScheduled = false;
        onOverlayQueryTimerFire();
    }
    update();
}

void ChartCanvas::setSourceChartPath(const QString &sourceChartPath)
{
    m_sourceChartPath = sourceChartPath.trimmed();
}

void ChartCanvas::setPluginOverlayToggles(const QVariantMap &toggles)
{
    if (toggles.isEmpty())
        return;

    bool changed = false;
    for (auto it = toggles.constBegin(); it != toggles.constEnd(); ++it)
    {
        if (!it.value().canConvert<bool>())
            continue;
        const bool value = it.value().toBool();
        if (m_pluginOverlayToggles.value(it.key()).toBool() == value)
            continue;
        m_pluginOverlayToggles.insert(it.key(), value);
        changed = true;
    }

    if (changed)
        update();
}

QString ChartCanvas::resolvePluginCanvasToolId() const
{
    if (!m_pluginToolPluginId.trimmed().isEmpty())
        return m_pluginToolPluginId.trimmed();

    PluginManager *pm = activePluginManager();
    if (!pm)
        return QString();
    const QVector<PluginInterface *> plugins = pm->plugins();
    for (PluginInterface *p : plugins)
    {
        if (!p)
            continue;
        if (!p->hasCapability(PluginInterface::kCapabilityCanvasInteraction))
            continue;
        return p->pluginId();
    }
    return QString();
}

QVariantMap ChartCanvas::buildPluginCanvasContext() const
{
    QVariantMap overlayContext;
    overlayContext.insert("canvas_width", width());
    overlayContext.insert("canvas_height", height());
    overlayContext.insert("scroll_beat", m_scrollBeat);
    overlayContext.insert("visible_beat_range", effectiveVisibleBeatRange());
    overlayContext.insert("vertical_flip", m_verticalFlip);
    overlayContext.insert("time_division", m_timeDivision);
    overlayContext.insert("grid_division", m_gridDivision);
    overlayContext.insert("grid_snap", m_gridSnap);
    overlayContext.insert("left_margin", leftMargin());
    overlayContext.insert("right_margin", rightMargin());
    overlayContext.insert("lane_width", kLaneWidth);
    overlayContext.insert("tool_mode_active", m_pluginToolModeActive);
    overlayContext.insert("note_type_scope", "normal_only");
    overlayContext.insert("overlay_toggles", m_pluginOverlayToggles);
    overlayContext.insert("plugin_time_division_override", m_pluginPlacementDensityOverride);
    overlayContext.insert("is_playing", m_isPlaying);
    overlayContext.insert("frame_seq", m_lastPlaybackFrameSeq);
    overlayContext.insert("overlay_snapshot_requested_at_ms", m_lastOverlayQueryMs);

    QVariantMap hostSelectionTool;
    QString modeName = "place_note";
    if (m_currentMode == PlaceRain)
        modeName = "place_rain";
    else if (m_currentMode == Delete)
        modeName = "delete";
    else if (m_currentMode == Select)
        modeName = "select";
    else if (m_currentMode == AnchorPlace)
        modeName = "anchor_place";
    hostSelectionTool.insert("mode", modeName);
    hostSelectionTool.insert("is_select_mode", m_currentMode == Select);
    hostSelectionTool.insert("is_selecting_rect", m_isSelecting);
    if (m_isSelecting)
    {
        const QRectF rect = QRectF(m_selectionStart, m_selectionEnd).normalized();
        QVariantMap rectObj;
        rectObj.insert("x", rect.x());
        rectObj.insert("y", rect.y());
        rectObj.insert("w", rect.width());
        rectObj.insert("h", rect.height());
        hostSelectionTool.insert("selection_rect", rectObj);
    }
    hostSelectionTool.insert("ctrl_toggle_select", true);
    hostSelectionTool.insert("empty_click_clears_selection", true);
    hostSelectionTool.insert("escape_clears_selection", true);
    overlayContext.insert("host_selection_tool", hostSelectionTool);

    const QString chartPath = m_chartController ? m_chartController->chartFilePath() : QString();
    if (!chartPath.isEmpty())
        overlayContext.insert("chart_path", chartPath);
    if (!m_sourceChartPath.isEmpty())
        overlayContext.insert("chart_path_source", m_sourceChartPath);
    const QString sidecarDir = sidecarDirForChart(chartPath);
    if (!sidecarDir.isEmpty())
    {
        const QString chartStem = QFileInfo(chartPath).completeBaseName();
        overlayContext.insert("curve_project_path", QDir(sidecarDir).filePath(chartStem + ".curve_tbd.json"));
        overlayContext.insert("sidecar_dir", sidecarDir);
    }

    QVariantList styleLibraryPaths;
    if (!sidecarDir.isEmpty())
        styleLibraryPaths.append(QDir(sidecarDir).filePath("styles"));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty())
        styleLibraryPaths.append(QDir(appData).filePath("plugin_styles"));
    overlayContext.insert("style_library_paths", styleLibraryPaths);

    QVariantList selectedIds;
    QVariantList selectedNotes;
    QVariantList existingNotePositions;
    if (m_selectionController && chart())
    {
        const auto &notes = chart()->notes();
        const QSet<int> selected = m_selectionController->selectedIndices();
        QList<int> sorted = selected.values();
        std::sort(sorted.begin(), sorted.end());
        for (int idx : sorted)
        {
            if (idx < 0 || idx >= notes.size())
                continue;
            const auto &note = notes[idx];
            if (note.id.isEmpty())
                continue;
            selectedIds.append(note.id);

            selectedNotes.append(serializeSelectedNoteForPlugin(note));
        }
    }
    if (chart())
    {
        const auto &notes = chart()->notes();
        for (const Note &note : notes)
        {
            if (note.type == NoteType::NORMAL)
                existingNotePositions.append(serializeNotePositionForPlugin(note));
        }
    }
    overlayContext.insert("selected_note_ids", selectedIds);
    overlayContext.insert("selected_notes", selectedNotes);
    overlayContext.insert("existing_note_positions", existingNotePositions);
    return overlayContext;
}

QVariantMap ChartCanvas::pluginCanvasActionContext() const
{
    return buildPluginCanvasContext();
}

void ChartCanvas::applyPluginCursor(const QString &cursorName)
{
    const QString key = cursorName.trimmed().toLower();
    if (key.isEmpty() || key == "default" || key == "arrow")
    {
        unsetCursor();
        return;
    }

    if (key == "crosshair")
    {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (key == "size_all")
    {
        setCursor(Qt::SizeAllCursor);
        return;
    }
    if (key == "size_hor")
    {
        setCursor(Qt::SizeHorCursor);
        return;
    }
    if (key == "size_ver")
    {
        setCursor(Qt::SizeVerCursor);
        return;
    }
    if (key == "pointing_hand")
    {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    unsetCursor();
}

bool ChartCanvas::dispatchPluginCanvasInput(const PluginInterface::CanvasInputEvent &event, bool *outConsumed)
{
    if (outConsumed)
        *outConsumed = false;
    if (!m_pluginToolModeActive)
        return false;

    PluginManager *pm = activePluginManager();
    if (!pm)
        return false;

    const QString pluginId = resolvePluginCanvasToolId();
    if (pluginId.isEmpty())
        return false;

    PluginInterface::CanvasInputResult result;
    const QVariantMap context = buildPluginCanvasContext();
    m_overlayQueryInCanvasInput = true;
    const bool handled = pm->handleCanvasInput(pluginId, context, event, &result);
    m_overlayQueryInCanvasInput = false;
    if (!handled)
        return false;

    if (!result.overlay.isEmpty())
    {
        m_eventOverlayCache = result.overlay;
        m_overlayCache = result.overlay;
        update();
    }
    if (!result.cursor.trimmed().isEmpty())
        applyPluginCursor(result.cursor);
    if (!result.statusText.trimmed().isEmpty())
        emit statusMessage(result.statusText);
    if (result.requestUndoCheckpoint && m_chartController && m_chartController->chart())
    {
        const QString label = result.undoCheckpointLabel.trimmed().isEmpty()
                                  ? tr("Plugin Curve Edit")
                                  : result.undoCheckpointLabel.trimmed();
        m_chartController->applyExternalChartMutation(label, *m_chartController->chart());
    }

    if (outConsumed)
        *outConsumed = result.consumed;
    return true;
}

bool ChartCanvas::triggerPluginBatchAction(const QString &actionId, const QString &actionTitle)
{
    if (!m_pluginToolModeActive || !m_chartController)
        return false;
    PluginManager *pm = activePluginManager();
    if (!pm)
        return false;

    const QString pluginId = resolvePluginCanvasToolId();
    if (pluginId.isEmpty())
        return false;
    if (!pm->supportsHostBatchEdit(pluginId))
        return false;

    const QVariantMap context = buildPluginCanvasContext();
    PluginInterface::BatchEdit edit;
    if (!pm->buildToolActionBatchEdit(pluginId, actionId, context, &edit))
        return false;

    const QString title = actionTitle.trimmed().isEmpty() ? tr("Plugin Action") : actionTitle.trimmed();
    const bool ok = m_chartController->applyBatchEdit(
        tr("Plugin Action: %1").arg(title),
        edit.notesToAdd,
        edit.notesToRemove,
        edit.notesToMove);
    if (ok)
        emit statusMessage(tr("Plugin batch action completed: %1").arg(title));
    return ok;
}

bool ChartCanvas::triggerPluginToolAction(const QString &actionId, const QString &actionTitle)
{
    if (!m_pluginToolModeActive || !m_chartController)
        return false;
    PluginManager *pm = activePluginManager();
    if (!pm)
        return false;

    const QString pluginId = resolvePluginCanvasToolId();
    if (pluginId.isEmpty())
        return false;

    const QVariantMap context = buildPluginCanvasContext();
    const QString title = actionTitle.trimmed().isEmpty() ? tr("Plugin Action") : actionTitle.trimmed();

    PluginInterface::BatchEdit edit;
    if (pm->supportsHostBatchEdit(pluginId) &&
        pm->buildToolActionBatchEdit(pluginId, actionId, context, &edit))
    {
        const bool ok = m_chartController->applyBatchEdit(
            tr("Plugin Action: %1").arg(title),
            edit.notesToAdd,
            edit.notesToRemove,
            edit.notesToMove);
        if (ok)
            emit statusMessage(tr("Plugin batch action completed: %1").arg(title));
        return ok;
    }

    if (!pm->runToolAction(pluginId, actionId, context))
        return false;

    m_overlayCache = pm->canvasOverlays(context);
    m_eventOverlayCache = m_overlayCache;
    update();
    emit statusMessage(tr("Plugin action completed: %1").arg(title));
    return true;
}

bool ChartCanvas::triggerPluginDeleteSelection()
{
    if (!m_pluginToolModeActive)
        return false;

    PluginInterface::CanvasInputEvent pluginEvent;
    pluginEvent.type = "key_down";
    pluginEvent.key = static_cast<int>(Qt::Key_Delete);
    pluginEvent.modifiers = static_cast<int>(Qt::NoModifier);
    pluginEvent.timestampMs = QDateTime::currentMSecsSinceEpoch();

    bool consumed = false;
    if (!dispatchPluginCanvasInput(pluginEvent, &consumed))
        return false;
    return consumed;
}

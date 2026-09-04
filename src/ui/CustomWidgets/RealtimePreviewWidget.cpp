#include "ui/CustomWidgets/RealtimePreviewWidget.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/Chart.h"
#include "model/Skin.h"
#include "render/HyperfruitDetector.h"
#include "render/NoteRenderer.h"
#include "render/RainRewardGenerator.h"
#include "utils/MathUtils.h"
#include "utils/PlaybackStutterProbe.h"
#include <cmath>
#include <limits>
#include <QPainter>
#include <QPen>
#include <QTimer>
#include <algorithm>

namespace
{
class PreviewPaintMeasurement final
{
public:
    explicit PreviewPaintMeasurement(bool active)
        : m_active(active)
    {
        if (m_active)
            m_timer.start();
    }

    ~PreviewPaintMeasurement()
    {
        if (m_active)
        {
            PlaybackStutterProbe::recordDuration(
                "preview.paint_total",
                static_cast<double>(m_timer.nsecsElapsed()) / 1000000.0,
                4.0,
                true);
        }
    }

private:
    bool m_active;
    QElapsedTimer m_timer;
};
}

RealtimePreviewWidget::RealtimePreviewWidget(QWidget *parent)
    : QWidget(parent),
      m_noteRenderer(new NoteRenderer()),
      m_hyperfruitDetector(new HyperfruitDetector()),
      m_deferredUpdateTimer(new QTimer(this))
{
    setMinimumWidth(170);
    setMaximumWidth(260);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    m_hyperfruitDetector->setCS(3.2);
    // Preview uses unified fallback style only (no skin texture).
    m_noteRenderer->setSkin(nullptr);
    m_noteRenderer->setShowColors(false);

    m_deferredUpdateTimer->setSingleShot(true);
    m_deferredUpdateTimer->setTimerType(Qt::PreciseTimer);
    connect(m_deferredUpdateTimer, &QTimer::timeout, this, [this]() {
        m_updateScheduled = false;
        if (!isVisible())
            return;
        update();
        m_frameTimer.restart();
    });
    m_frameTimer.start();
    m_probeClock.start();
}

RealtimePreviewWidget::~RealtimePreviewWidget()
{
    delete m_noteRenderer;
    delete m_hyperfruitDetector;
}

void RealtimePreviewWidget::setChartController(ChartController *controller)
{
    if (m_chartController == controller)
        return;

    if (m_chartController)
        disconnect(m_chartController, nullptr, this, nullptr);

    m_chartController = controller;
    invalidateNoteCache();
    invalidateHyperCache();
    if (m_chartController)
    {
        connect(m_chartController, &ChartController::notesChanged, this, [this]() {
            invalidateNoteCache();
            invalidateHyperCache();
            scheduleUpdate();
        });
    }
    scheduleUpdate();
}

void RealtimePreviewWidget::setPlaybackController(PlaybackController *controller)
{
    if (m_playbackController == controller)
        return;

    if (m_playbackController)
        disconnect(m_playbackController, nullptr, this, nullptr);

    m_lastPlaybackFrameSeq = -1;

    m_playbackController = controller;
    if (m_playbackController)
    {
        connect(m_playbackController, &PlaybackController::positionChanged, this, [this](double timeMs) {
            const double clamped = qMax(0.0, timeMs);
            if (m_playbackController->state() == PlaybackController::Playing)
                return;
            if (std::abs(clamped - m_currentTimeMs) < 0.05)
                return;
            m_currentTimeMs = clamped;
            scheduleUpdate();
        });
        connect(m_playbackController, &PlaybackController::playbackFrameTick,
                this, &RealtimePreviewWidget::handlePlaybackFrameTick);
        connect(m_playbackController, &PlaybackController::stateChanged, this, [this](PlaybackController::State) {
            onPlaybackStateChanged();
        });
        onPlaybackStateChanged();
    }
}

void RealtimePreviewWidget::setSkin(Skin *skin)
{
    Q_UNUSED(skin);
    // Intentionally ignore external skin to keep preview style consistent.
    m_noteRenderer->setSkin(nullptr);
    scheduleUpdate();
}

void RealtimePreviewWidget::setColorMode(bool enabled)
{
    Q_UNUSED(enabled);
    // Keep preview in fixed monochrome style.
    m_colorMode = false;
    m_noteRenderer->setShowColors(false);
}

void RealtimePreviewWidget::setHyperfruitEnabled(bool enabled)
{
    m_hyperfruitEnabled = enabled;
    m_noteRenderer->setHyperfruitEnabled(enabled);
    invalidateHyperCache();
    scheduleUpdate();
}

void RealtimePreviewWidget::setRainRewardPreviewEnabled(bool enabled)
{
    if (m_rainRewardPreviewEnabled == enabled)
        return;
    m_rainRewardPreviewEnabled = enabled;
    m_noteRenderer->setRainRewardPreviewEnabled(enabled);
    scheduleUpdate();
}

void RealtimePreviewWidget::setNoteSize(int size)
{
    m_noteRenderer->setNoteSize(size);
    scheduleUpdate();
}

void RealtimePreviewWidget::refreshRenderSettings()
{
    m_noteRenderer->refreshSettings();
    scheduleUpdate();
}

void RealtimePreviewWidget::setCurrentBeat(double beat)
{
    if (!m_chartController || !m_chartController->chart())
        return;
    // Avoid fighting playback time updates while audio is running.
    if (m_playbackController && m_playbackController->state() == PlaybackController::Playing)
        return;

    m_currentTimeMs = beatToTimeMs(beat);
    scheduleUpdate();
}

void RealtimePreviewWidget::setCurrentTimeMs(double timeMs)
{
    const double clamped = qMax(0.0, timeMs);
    if (std::abs(clamped - m_currentTimeMs) < 0.05)
        return;
    m_currentTimeMs = clamped;
    scheduleUpdate();
}

double RealtimePreviewWidget::beatToTimeMs(double beat)
{
    ensureNoteCache();
    if (m_bpmSegments.isEmpty())
        return 0.0;

    int lo = 0;
    int hi = m_bpmSegments.size() - 1;
    while (lo < hi)
    {
        const int mid = (lo + hi + 1) / 2;
        if (m_bpmSegments[mid].beatPos <= beat)
            lo = mid;
        else
            hi = mid - 1;
    }

    const auto &seg = m_bpmSegments[lo];
    if (seg.bpm <= 0.0)
        return seg.accumulatedMs;
    return seg.accumulatedMs + (beat - seg.beatPos) * (60000.0 / seg.bpm);
}

double RealtimePreviewWidget::timeToY(double timeMs,
                                      const QRectF &laneRect,
                                      double referenceY,
                                      double upperSpanMs,
                                      double lowerSpanMs) const
{
    const double spanMs = qMax(1.0, qMax(upperSpanMs, lowerSpanMs));
    const double pxPerMs = qMax(1.0, laneRect.height()) / spanMs;
    return referenceY - (timeMs - m_currentTimeMs) * pxPerMs;
}

void RealtimePreviewWidget::scheduleUpdate()
{
    if (!isVisible())
        return;

    if (m_playbackController && m_playbackController->state() == PlaybackController::Playing)
    {
        if (m_deferredUpdateTimer->isActive())
            m_deferredUpdateTimer->stop();
        m_updateScheduled = false;
        return;
    }

    if (!m_frameTimer.isValid() || m_frameTimer.elapsed() >= kMinFrameIntervalMs)
    {
        if (m_deferredUpdateTimer->isActive())
            m_deferredUpdateTimer->stop();
        m_updateScheduled = false;
        update();
        m_frameTimer.restart();
        return;
    }

    if (m_updateScheduled)
        return;

    const int delayMs = qMax(1, kMinFrameIntervalMs - static_cast<int>(m_frameTimer.elapsed()));
    m_updateScheduled = true;
    m_deferredUpdateTimer->start(delayMs);
}

void RealtimePreviewWidget::onPlaybackStateChanged()
{
    if (!m_playbackController)
        return;

    if (m_playbackController->state() == PlaybackController::Playing)
    {
        if (m_deferredUpdateTimer->isActive())
            m_deferredUpdateTimer->stop();
        m_updateScheduled = false;
        m_lastPlaybackFrameSeq = -1;
        m_currentTimeMs = qMax(0.0, m_playbackController->currentTime());
        update();
        m_frameTimer.restart();
        return;
    }

    m_lastPlaybackFrameSeq = -1;
    m_currentTimeMs = qMax(0.0, m_playbackController->currentTime());
    scheduleUpdate();
}

void RealtimePreviewWidget::handlePlaybackFrameTick(double predictedTimeMs, qint64 frameSeq)
{
    if (!m_playbackController || m_playbackController->state() != PlaybackController::Playing)
        return;
    if (frameSeq <= m_lastPlaybackFrameSeq)
        return;
    m_lastPlaybackFrameSeq = frameSeq;
    m_currentTimeMs = qMax(0.0, predictedTimeMs);
    if (!isVisible())
        return;
    if (m_deferredUpdateTimer->isActive())
        m_deferredUpdateTimer->stop();
    m_updateScheduled = false;
    update();
    m_frameTimer.restart();
}

void RealtimePreviewWidget::invalidateHyperCache()
{
    m_hyperCacheValid = false;
    m_hyperIndices.clear();
    m_hyperMask.clear();
}

void RealtimePreviewWidget::invalidateNoteCache()
{
    m_noteCacheValid = false;
    m_bpmSegments.clear();
    m_noteStartTimesMs.clear();
    m_noteEndTimesMs.clear();
    m_normalIndices.clear();
    m_rainIndices.clear();
    m_sortedNormalEntries.clear();
}

void RealtimePreviewWidget::ensureNoteCache()
{
    if (m_noteCacheValid)
        return;

    m_bpmSegments.clear();
    m_noteStartTimesMs.clear();
    m_noteEndTimesMs.clear();
    m_normalIndices.clear();
    m_rainIndices.clear();
    m_sortedNormalEntries.clear();

    if (!m_chartController || !m_chartController->chart())
    {
        m_noteCacheValid = true;
        return;
    }

    const Chart *chart = m_chartController->chart();
    const auto &notes = chart->notes();
    const auto &bpmList = chart->bpmList();
    if (bpmList.isEmpty())
    {
        m_noteCacheValid = true;
        return;
    }

    const QVector<MathUtils::BpmCacheEntry> bpmCache = MathUtils::buildBpmTimeCache(bpmList, chart->meta().offset);
    if (bpmCache.isEmpty())
    {
        m_noteCacheValid = true;
        return;
    }

    m_bpmSegments.reserve(bpmCache.size());
    for (const auto &entry : bpmCache)
    {
        m_bpmSegments.push_back(BpmSegment{entry.beatPos, entry.accumulatedMs, entry.bpm});
    }

    m_noteStartTimesMs.fill(std::numeric_limits<double>::quiet_NaN(), notes.size());
    m_noteEndTimesMs.fill(std::numeric_limits<double>::quiet_NaN(), notes.size());
    m_normalIndices.reserve(notes.size());
    m_rainIndices.reserve(notes.size());
    m_sortedNormalEntries.reserve(notes.size());

    for (int i = 0; i < notes.size(); ++i)
    {
        const Note &note = notes[i];
        if (note.type == NoteType::SOUND)
            continue;

        const double startMs = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, bpmCache);
        m_noteStartTimesMs[i] = startMs;

        if (note.type == NoteType::RAIN)
        {
            m_noteEndTimesMs[i] = MathUtils::beatToMs(note.endBeatNum, note.endNumerator, note.endDenominator, bpmCache);
            m_rainIndices.push_back(i);
        }
        else
        {
            m_noteEndTimesMs[i] = startMs;
            m_normalIndices.push_back(i);
            m_sortedNormalEntries.push_back(TimedNoteEntry{i, startMs, startMs});
        }
    }

    std::sort(m_sortedNormalEntries.begin(), m_sortedNormalEntries.end(),
              [](const TimedNoteEntry &a, const TimedNoteEntry &b) {
                  return a.startMs < b.startMs;
              });

    m_noteCacheValid = true;
}

void RealtimePreviewWidget::ensureHyperCache()
{
    if (m_hyperCacheValid)
        return;

    m_hyperIndices.clear();
    if (!m_hyperfruitEnabled || !m_chartController || !m_chartController->chart())
    {
        m_hyperCacheValid = true;
        return;
    }

    const Chart *chart = m_chartController->chart();
    const auto &bpmList = chart->bpmList();
    if (bpmList.isEmpty())
    {
        m_hyperCacheValid = true;
        return;
    }

    m_hyperIndices = m_hyperfruitDetector->detect(chart->notes(), bpmList, chart->meta().offset);
    m_hyperMask.fill(false, chart->notes().size());
    for (int idx : std::as_const(m_hyperIndices))
    {
        if (idx >= 0 && idx < m_hyperMask.size())
            m_hyperMask[idx] = true;
    }
    m_hyperCacheValid = true;
}

void RealtimePreviewWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (m_playbackController &&
        m_playbackController->state() == PlaybackController::Playing)
    {
        m_currentTimeMs = qMax(
            m_currentTimeMs,
            m_playbackController->visualTime());
    }

    const bool probeFrame =
        m_playbackController &&
        m_playbackController->state() == PlaybackController::Playing &&
        PlaybackStutterProbe::enabled();
    const qint64 paintStartNs = probeFrame ? m_probeClock.nsecsElapsed() : 0;
    if (probeFrame && m_lastPaintProbeNs > 0 && paintStartNs > m_lastPaintProbeNs)
    {
        const double intervalMs =
            static_cast<double>(paintStartNs - m_lastPaintProbeNs) / 1000000.0;
        const double targetIntervalMs =
            1000.0 / qMax(1.0, m_playbackController->effectiveFrameRate());
        PlaybackStutterProbe::recordDuration(
            "preview.paint_interval",
            intervalMs,
            targetIntervalMs * 1.35,
            true);
    }
    if (probeFrame)
        m_lastPaintProbeNs = paintStartNs;
    PreviewPaintMeasurement paintMeasurement(probeFrame);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor(10, 10, 10));

    const QRectF laneRect(10.0, 8.0, qMax(1.0, width() - 20.0), qMax(1.0, height() - 16.0));
    painter.setPen(QPen(QColor(50, 50, 50), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(laneRect);

    const double upperSpanMs = 2000.0;
    const double lowerSpanMs = 2000.0;
    const double referenceY = laneRect.top() + laneRect.height() * 0.8;

    painter.setPen(QPen(Qt::white, 2));
    painter.drawLine(QPointF(laneRect.left(), referenceY), QPointF(laneRect.right(), referenceY));

    if (!m_chartController || !m_chartController->chart())
        return;

    const Chart *chart = m_chartController->chart();
    const auto &notes = chart->notes();
    ensureNoteCache();
    if (notes.isEmpty())
        return;

    ensureHyperCache();

    for (int idx : m_rainIndices)
    {
        if (idx < 0 || idx >= notes.size())
            continue;
        const Note &note = notes[idx];

        const double startTime = m_noteStartTimesMs.value(idx, std::numeric_limits<double>::quiet_NaN());
        const double endTime = m_noteEndTimesMs.value(idx, std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(startTime) || !std::isfinite(endTime))
            continue;
        const double minT = qMin(startTime, endTime);
        const double maxT = qMax(startTime, endTime);

        if (minT > m_currentTimeMs + upperSpanMs || maxT < m_currentTimeMs - lowerSpanMs)
            continue;

        const double yStart = timeToY(startTime, laneRect, referenceY, upperSpanMs, lowerSpanMs);
        const double yEnd = timeToY(endTime, laneRect, referenceY, upperSpanMs, lowerSpanMs);

        QRectF rainRect(laneRect.left(), qMin(yStart, yEnd), laneRect.width(), qAbs(yEnd - yStart));
        rainRect = rainRect.intersected(laneRect);
        if (rainRect.height() <= 0.1)
            continue;

        // rain 奖励 note 预览点（同一 paint pass 内绘制，无额外图层）。
        QVector<QPointF> rewardPoints;
        if (m_rainRewardPreviewEnabled)
        {
            auto &generator = RainRewardGenerator::instance();
            generator.ensureChart(notes, chart->bpmList(), chart->meta().offset);
            const QVector<RainDrop> drops = generator.dropsFor(note);
            if (!drops.isEmpty())
            {
                const double startBeatFloat =
                    MathUtils::beatToFloat(note.beatNum, note.numerator, note.denominator);
                rewardPoints.reserve(drops.size());
                for (const RainDrop &drop : drops)
                {
                    const double dropTimeMs = beatToTimeMs(startBeatFloat + drop.beatOffset);
                    const double dropY = timeToY(dropTimeMs, laneRect, referenceY, upperSpanMs, lowerSpanMs);
                    if (dropY < laneRect.top() || dropY > laneRect.bottom())
                        continue;
                    const double dropX = laneRect.left() + laneRect.width() * drop.xRatio;
                    rewardPoints.append(QPointF(dropX, dropY));
                }
            }
        }

        m_noteRenderer->drawRain(painter, note, rainRect, false,
                                 rewardPoints.isEmpty() ? nullptr : &rewardPoints);
    }

    const int noteSize = qMax(6, m_noteRenderer->getNoteSize());
    const qreal radius = noteSize * 0.5;
    const qreal hyperRadius = radius * 1.3;

    const double minVisibleTime = m_currentTimeMs - lowerSpanMs;
    const double maxVisibleTime = m_currentTimeMs + upperSpanMs;
    auto lower = std::lower_bound(m_sortedNormalEntries.begin(),
                                  m_sortedNormalEntries.end(),
                                  minVisibleTime,
                                  [](const TimedNoteEntry &entry, double value) {
                                      return entry.startMs < value;
                                  });

    for (auto it = lower; it != m_sortedNormalEntries.end(); ++it)
    {
        if (it->startMs > maxVisibleTime)
            break;
        const int idx = it->index;
        if (idx < 0 || idx >= notes.size())
            continue;
        const Note &note = notes[idx];

        const double timeMs = it->startMs;
        if (!std::isfinite(timeMs))
            continue;

        const int clampedX = qBound(0, note.x, kLaneWidth);
        const double normX = clampedX / static_cast<double>(kLaneWidth);
        const double x = laneRect.left() + laneRect.width() * normX;
        const double y = timeToY(timeMs, laneRect, referenceY, upperSpanMs, lowerSpanMs);
        if (y < laneRect.top() - 64.0 || y > laneRect.bottom() + 64.0)
            continue;

        const QPointF center(x, y);
        const bool isHyper = m_hyperfruitEnabled &&
                             idx >= 0 &&
                             idx < m_hyperMask.size() &&
                             m_hyperMask[idx];
        if (isHyper)
        {
            painter.setPen(QPen(Qt::red, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(center, hyperRadius, hyperRadius);
        }

        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    }
}

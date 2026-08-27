#include "PlaybackController.h"
#include "DisplayFrameScheduler.h"
#include "utils/MathUtils.h"
#include "utils/Logger.h"
#include "utils/PlaybackSpeed.h"
#include "utils/PlaybackStutterProbe.h"
#include "utils/Settings.h"
#include "model/Chart.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
    constexpr qint64 kSeekSameValueThresholdMs = 2;
    constexpr double kClockSlewConvergenceWallMs = 1500.0;
    constexpr double kClockSlewMaxRateFraction = 0.005;
    constexpr double kClockSlewFilterGain = 0.15;

    QEvent::Type scheduledFrameEventType()
    {
        static const auto type = static_cast<QEvent::Type>(QEvent::registerEventType());
        return type;
    }
}

PlaybackController::PlaybackController(AudioPlayer *audioPlayer, QObject *parent)
    : QObject(parent),
      m_audioPlayer(audioPlayer),
      m_state(Stopped),
      m_speed(1.0),
      m_noteSoundEnabled(true),
      m_autoPausedAtEnd(false),
      m_frameScheduler(nullptr),
      m_originalUiThreadPriority(0),
      m_uiPriorityRaised(false),
      m_frameRateCap(60),
      m_displayRefreshRateHz(60.0),
      m_frameInFlight(false),
      m_schedulerReadySteadyNs(0),
      m_schedulerIntervalNs(0),
      m_schedulerPresentationSteadyNs(0),
      m_schedulerSkippedFrames(0),
      m_pendingPaintFrameSeq(0),
      m_frameAnchorValid(false),
      m_frameAnchorTimeMs(0.0),
      m_frameAnchorWallNs(0),
      m_frameRateCorrection(0.0),
      m_waitingForAudioProgress(false),
      m_audioProgressStartMs(0.0),
      m_frameSeq(0),
      m_lastFrameTickMs(0.0),
      m_lastPulseProbeNs(0),
      m_lastPulseIntervalMs(-1.0)
{
    connect(m_audioPlayer, &AudioPlayer::positionChanged, this, &PlaybackController::onAudioPositionChanged);
    connect(m_audioPlayer, &AudioPlayer::stateChanged, this, &PlaybackController::onAudioStateChanged);
    connect(m_audioPlayer, &AudioPlayer::errorOccurred, this, &PlaybackController::onAudioError);
    m_frameClock.start();
    m_frameScheduler = std::make_unique<DisplayFrameScheduler>(
        [this](const DisplayFrameScheduler::Pulse &pulse)
        {
            // Always publish the newest display phase. If a previous Qt event
            // is still queued, it can then coalesce to the latest presentable
            // frame instead of rendering a target that is already one refresh
            // old when the UI thread finally becomes available.
            m_schedulerReadySteadyNs.store(pulse.readySteadyNs, std::memory_order_release);
            m_schedulerIntervalNs.store(pulse.intervalNs, std::memory_order_release);
            m_schedulerPresentationSteadyNs.store(
                pulse.presentationSteadyNs,
                std::memory_order_release);
            if (m_frameInFlight.exchange(true, std::memory_order_acq_rel))
            {
                m_schedulerSkippedFrames.fetch_add(
                    1 + pulse.missedTargetFrames,
                    std::memory_order_relaxed);
                return;
            }

            if (pulse.missedTargetFrames > 0)
            {
                m_schedulerSkippedFrames.fetch_add(
                    pulse.missedTargetFrames,
                    std::memory_order_relaxed);
            }
            QCoreApplication::postEvent(
                this,
                new QEvent(scheduledFrameEventType()),
                Qt::HighEventPriority);
        });
    setFrameRateCap(Settings::instance().playbackFrameRateCap());
}

PlaybackController::~PlaybackController()
{
    if (m_frameScheduler)
        m_frameScheduler->setActive(false);
    m_frameScheduler.reset();
    endPlaybackUiPriority();
}

PlaybackController::State PlaybackController::state() const
{
    return m_state;
}

void PlaybackController::play()
{
    if (m_state == Stopped || m_state == Paused)
    {
        if (m_autoPausedAtEnd)
        {
            m_audioPlayer->setAdjustedPosition(0);
            m_autoPausedAtEnd = false;
            emit positionChanged(static_cast<double>(m_audioPlayer->adjustedPosition()));
            Logger::info("PlaybackController::play - Restarting from beginning after end-of-media auto pause");
        }

        if (!m_audioPlayer->canPlay())
        {
            const QString playerError = m_audioPlayer->lastError().trimmed();
            QString errorMsg = playerError.isEmpty()
                                   ? QString("Cannot play audio")
                                   : QString("Cannot play audio: %1").arg(playerError);
            Logger::error(QString("PlaybackController::play - %1").arg(errorMsg));
            emit errorOccurred(errorMsg);
            return;
        }
        m_audioPlayer->play();
        m_state = Playing;
        m_frameSeq = 0;
        m_lastPulseProbeNs = 0;
        m_lastPulseIntervalMs = -1.0;
        m_lastFrameTickMs = static_cast<double>(m_audioPlayer->adjustedPosition());
        m_frameRateCorrection = 0.0;
        resetFrameAnchor(m_lastFrameTickMs, m_frameClock.nsecsElapsed());
        // QMediaPlayer can report PlayingState before decoded audio reaches the
        // output. Hold the editor clock until the media position really moves.
        m_waitingForAudioProgress = true;
        m_audioProgressStartMs = m_lastFrameTickMs;
        m_frameInFlight.store(false, std::memory_order_release);
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_schedulerSkippedFrames.store(0, std::memory_order_release);
        updateFrameScheduler();
        beginPlaybackUiPriority();
        m_frameScheduler->setActive(true);
        Logger::debug(QString("PlaybackController::play - Playing from position %1ms").arg(m_audioPlayer->position()));
        PlaybackStutterProbe::markPlaybackState(true);
        emit stateChanged(m_state);
    }
    else
    {
        Logger::debug(QString("PlaybackController::play - Ignored, already in state %1").arg(m_state));
    }
}

void PlaybackController::playFromTime(double timeMs)
{
    if (m_state == Playing)
    {
        pause();
    }

    if (m_autoPausedAtEnd)
    {
        timeMs = 0.0;
        m_autoPausedAtEnd = false;
        Logger::info("PlaybackController::playFromTime - End-of-media replay requested, forcing restart from 0ms");
    }

    const qint64 targetMs = clampSeekTargetMs(static_cast<qint64>(qRound64(timeMs)));
    applySeekNow(targetMs, "playFromTime");
    play();
}

void PlaybackController::pause()
{
    if (m_state == Playing)
    {
        m_autoPausedAtEnd = false;
        m_state = Paused;
        m_frameScheduler->setActive(false);
        endPlaybackUiPriority();
        m_frameInFlight.store(false, std::memory_order_release);
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_frameAnchorValid = false;
        m_frameRateCorrection = 0.0;
        m_waitingForAudioProgress = false;
        m_lastPulseProbeNs = 0;
        m_lastPulseIntervalMs = -1.0;
        m_audioPlayer->pause();
        Logger::debug(QString("PlaybackController::pause - Paused at position %1ms").arg(m_audioPlayer->position()));
        emit positionChanged(static_cast<double>(m_audioPlayer->adjustedPosition()));
        PlaybackStutterProbe::markPlaybackState(false);
        emit stateChanged(m_state);
    }
}

void PlaybackController::stop()
{
    if (m_state != Stopped)
    {
        m_autoPausedAtEnd = false;
        m_state = Stopped;
        m_frameScheduler->setActive(false);
        endPlaybackUiPriority();
        m_frameInFlight.store(false, std::memory_order_release);
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_frameAnchorValid = false;
        m_frameRateCorrection = 0.0;
        m_waitingForAudioProgress = false;
        m_frameSeq = 0;
        m_lastPulseProbeNs = 0;
        m_lastPulseIntervalMs = -1.0;
        m_audioPlayer->stop();
        Logger::debug("PlaybackController::stop - Playback stopped");
        emit positionChanged(static_cast<double>(m_audioPlayer->adjustedPosition()));
        PlaybackStutterProbe::markPlaybackState(false);
        emit stateChanged(m_state);
    }
}

void PlaybackController::setSpeed(double speed)
{
    speed = PlaybackSpeed::sanitize(speed);
    if (qFuzzyCompare(m_speed, speed))
        return;

    const qint64 nowNs = m_frameClock.nsecsElapsed();
    const double continuousTimeMs = (m_state == Playing)
                                        ? predictedTimeAt(nowNs)
                                        : static_cast<double>(m_audioPlayer->adjustedPosition());
    m_speed = speed;
    m_frameRateCorrection = 0.0;
    m_audioPlayer->setSpeed(speed);
    if (m_state == Playing)
    {
        resetFrameAnchor(continuousTimeMs, nowNs);
        m_lastFrameTickMs = continuousTimeMs;
        m_waitingForAudioProgress = true;
        m_audioProgressStartMs = static_cast<double>(m_audioPlayer->adjustedPosition());
    }
    else
    {
        // Changing the rate changes the wall-latency conversion. Preserve the
        // visible/seek position while paused instead of shifting the playhead.
        m_audioPlayer->setAdjustedPosition(qRound64(continuousTimeMs));
    }
    Logger::debug(QString("PlaybackController::setSpeed - Speed changed to %1x").arg(speed));
    emit speedChanged(speed);
}

double PlaybackController::speed() const
{
    return m_speed;
}

void PlaybackController::setFrameRateCap(int fpsCap)
{
    switch (fpsCap)
    {
    case 0:
    case 60:
    case 90:
    case 120:
        m_frameRateCap = fpsCap;
        break;
    default:
        m_frameRateCap = 60;
        break;
    }

    updateFrameScheduler();
}

bool PlaybackController::event(QEvent *event)
{
    if (event && event->type() == scheduledFrameEventType())
    {
        dispatchScheduledFrame();
        return true;
    }
    return QObject::event(event);
}

int PlaybackController::frameRateCap() const
{
    return m_frameRateCap;
}

void PlaybackController::setDisplayRefreshRate(double refreshRateHz)
{
    constexpr double kFallbackRefreshRateHz = 60.0;
    constexpr double kMinimumRefreshRateHz = 24.0;
    constexpr double kMaximumRefreshRateHz = 1000.0;

    if (!std::isfinite(refreshRateHz) ||
        refreshRateHz < kMinimumRefreshRateHz ||
        refreshRateHz > kMaximumRefreshRateHz)
    {
        refreshRateHz = kFallbackRefreshRateHz;
    }
    if (std::abs(m_displayRefreshRateHz - refreshRateHz) < 0.01)
        return;

    m_displayRefreshRateHz = refreshRateHz;
    updateFrameScheduler();
}

double PlaybackController::displayRefreshRate() const
{
    return m_displayRefreshRateHz;
}

double PlaybackController::effectiveFrameRate() const
{
    const double displayHz = qMax(1.0, m_displayRefreshRateHz);
    if (m_frameRateCap <= 0 || static_cast<double>(m_frameRateCap) >= displayHz * 0.98)
        return displayHz;

    // A QWidget frame can only become visible on a display refresh. Choose a
    // whole-number refresh divisor below the requested cap; arbitrary ratios
    // (for example 90 FPS on 120 Hz) necessarily alternate presentation gaps.
    const double requestedHz = qMax(1.0, static_cast<double>(m_frameRateCap));
    const int refreshDivider = qMax(1, static_cast<int>(std::ceil(displayHz / requestedHz - 1e-9)));
    return displayHz / static_cast<double>(refreshDivider);
}

void PlaybackController::seekTo(double timeMs)
{
    m_autoPausedAtEnd = false;
    const qint64 targetMs = clampSeekTargetMs(static_cast<qint64>(qRound64(timeMs)));
    const qint64 nowNs = m_frameClock.nsecsElapsed();
    m_frameRateCorrection = 0.0;
    resetFrameAnchor(static_cast<double>(targetMs), nowNs);
    m_lastFrameTickMs = static_cast<double>(targetMs);
    m_waitingForAudioProgress = (m_state == Playing);
    m_audioProgressStartMs = static_cast<double>(targetMs);
    applySeekNow(targetMs, "direct");
    emit positionChanged(static_cast<double>(targetMs));
}

void PlaybackController::seekToBeat(int beat, int num, int den)
{
    Q_UNUSED(beat);
    Q_UNUSED(num);
    Q_UNUSED(den);

    const QString msg = "PlaybackController::seekToBeat is not supported without chart timing context; use seekTo(ms).";
    Logger::warn(msg);
    emit errorOccurred(msg);
}

double PlaybackController::currentTime() const
{
    if (m_state == Playing && m_frameAnchorValid)
        return predictedTimeAt(m_frameClock.nsecsElapsed());
    return static_cast<double>(m_audioPlayer->adjustedPosition());
}

double PlaybackController::visualTime() const
{
    if (m_state != Playing || !m_frameAnchorValid)
        return currentTime();

    const qint64 nowFrameClockNs = m_frameClock.nsecsElapsed();
    const qint64 nowSteadyNs = DisplayFrameScheduler::steadyNowNs();
    const qint64 presentationSteadyNs =
        m_schedulerPresentationSteadyNs.load(std::memory_order_acquire);
    const qint64 presentationFrameClockNs =
        presentationSteadyNs > nowSteadyNs
            ? nowFrameClockNs + (presentationSteadyNs - nowSteadyNs)
            : nowFrameClockNs;
    return predictedTimeAt(presentationFrameClockNs);
}

void PlaybackController::acknowledgeFramePainted(qint64 frameSeq)
{
    qint64 expectedFrameSeq = frameSeq;
    if (!m_pendingPaintFrameSeq.compare_exchange_strong(
            expectedFrameSeq,
            0,
            std::memory_order_acq_rel,
            std::memory_order_acquire))
    {
        return;
    }
    m_frameInFlight.store(false, std::memory_order_release);
}

void PlaybackController::setNoteSoundEnabled(bool enabled)
{
    m_noteSoundEnabled = enabled;
}

bool PlaybackController::autoPausedAtEnd() const
{
    return m_autoPausedAtEnd;
}

void PlaybackController::onAudioPositionChanged(qint64 position)
{
    Q_UNUSED(position);
    const bool probeFanout =
        m_state == Playing && PlaybackStutterProbe::enabled();
    QElapsedTimer fanoutTimer;
    if (probeFanout)
        fanoutTimer.start();
    const double observedMs = static_cast<double>(m_audioPlayer->adjustedPosition());
    emit positionChanged(observedMs);
    if (probeFanout)
    {
        PlaybackStutterProbe::recordDuration(
            "audio.position_ui_fanout",
            static_cast<double>(fanoutTimer.nsecsElapsed()) / 1000000.0,
            1.0,
            true);
    }
    if (m_state == Playing)
    {
        const qint64 nowNs = m_frameClock.nsecsElapsed();
        if (m_waitingForAudioProgress)
        {
            if (std::abs(observedMs - m_audioProgressStartMs) <= kAudioProgressEpsilonMs)
                return;
            m_waitingForAudioProgress = false;
            m_frameRateCorrection = 0.0;
            resetFrameAnchor(observedMs, nowNs);
            m_lastFrameTickMs = qMax(m_lastFrameTickMs, observedMs);
            if (PlaybackStutterProbe::enabled())
                PlaybackStutterProbe::recordCounter("playback.anchor_startup_resync", 1, true);
            return;
        }
        applyObservedTimeToAnchor(observedMs, nowNs);
    }
}

void PlaybackController::onAudioStateChanged(QMediaPlayer::PlaybackState state)
{
    if (m_state == Playing && state == QMediaPlayer::StoppedState)
    {
        m_autoPausedAtEnd = true;
        m_state = Paused;
        m_frameScheduler->setActive(false);
        endPlaybackUiPriority();
        m_frameInFlight.store(false, std::memory_order_release);
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_frameAnchorValid = false;
        m_frameRateCorrection = 0.0;
        m_waitingForAudioProgress = false;
        m_frameSeq = 0;
        m_lastFrameTickMs = 0.0;
        m_lastPulseProbeNs = 0;
        m_lastPulseIntervalMs = -1.0;
        m_audioPlayer->setAdjustedPosition(0);
        Logger::info("PlaybackController::onAudioStateChanged - Reached end of media, auto-paused at beginning");
        emit positionChanged(static_cast<double>(m_audioPlayer->adjustedPosition()));
        PlaybackStutterProbe::markPlaybackState(false);
        emit stateChanged(m_state);
    }
}

void PlaybackController::onAudioError(const QString &error)
{
    Logger::error(QString("PlaybackController::onAudioError - Audio error: %1").arg(error));
    m_autoPausedAtEnd = false;
    if (m_state != Stopped)
    {
        m_state = Stopped;
        m_frameScheduler->setActive(false);
        endPlaybackUiPriority();
        m_frameInFlight.store(false, std::memory_order_release);
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_frameAnchorValid = false;
        m_frameRateCorrection = 0.0;
        m_waitingForAudioProgress = false;
        m_frameSeq = 0;
        m_lastPulseProbeNs = 0;
        m_lastPulseIntervalMs = -1.0;
        PlaybackStutterProbe::markPlaybackState(false);
        emit stateChanged(m_state);
    }
    emit errorOccurred(error);
}

qint64 PlaybackController::clampSeekTargetMs(qint64 timeMs) const
{
    qint64 clamped = qMax<qint64>(0, timeMs);
    if (!m_audioPlayer)
        return clamped;
    const qint64 duration = m_audioPlayer->duration();
    if (duration > 0)
        clamped = qBound<qint64>(0, clamped, duration);
    return clamped;
}

void PlaybackController::applySeekNow(qint64 targetMs, const char *reason)
{
    const qint64 clampedMs = clampSeekTargetMs(targetMs);
    const qint64 currentMs = m_audioPlayer
                                 ? clampSeekTargetMs(m_audioPlayer->adjustedPosition())
                                 : clampedMs;
    if (qAbs(clampedMs - currentMs) <= kSeekSameValueThresholdMs)
        return;

    m_audioPlayer->setAdjustedPosition(clampedMs);
    Logger::debug(QString("PlaybackController::seekTo - Seeking to %1ms (adjusted, %2)")
                      .arg(clampedMs)
                      .arg(QString::fromUtf8(reason)));
}

void PlaybackController::updateFrameScheduler()
{
    if (m_frameScheduler)
        m_frameScheduler->setRates(m_displayRefreshRateHz, effectiveFrameRate());
}

void PlaybackController::beginPlaybackUiPriority()
{
#ifdef _WIN32
    if (m_uiPriorityRaised)
        return;

    HANDLE threadHandle = GetCurrentThread();
    const int currentPriority = GetThreadPriority(threadHandle);
    if (currentPriority == THREAD_PRIORITY_ERROR_RETURN)
    {
        Logger::warn("PlaybackController - Could not read UI playback thread priority");
        return;
    }

    if (!SetThreadPriority(threadHandle, THREAD_PRIORITY_HIGHEST))
    {
        Logger::warn("PlaybackController - Could not raise UI playback thread priority");
        return;
    }
    m_originalUiThreadPriority = currentPriority;
    m_uiPriorityRaised = true;
#endif
}

void PlaybackController::endPlaybackUiPriority()
{
#ifdef _WIN32
    if (!m_uiPriorityRaised)
        return;
    SetThreadPriority(GetCurrentThread(), m_originalUiThreadPriority);
    m_uiPriorityRaised = false;
#endif
}

void PlaybackController::dispatchScheduledFrame()
{
    if (m_state != Playing)
    {
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_frameInFlight.store(false, std::memory_order_release);
        return;
    }

    const qint64 readySteadyNs = m_schedulerReadySteadyNs.load(std::memory_order_acquire);
    const qint64 displayIntervalNs = m_schedulerIntervalNs.load(std::memory_order_acquire);
    const qint64 presentationSteadyNs =
        m_schedulerPresentationSteadyNs.load(std::memory_order_acquire);
    const qint64 skippedRefreshes = m_schedulerSkippedFrames.exchange(0, std::memory_order_acq_rel);
    if (PlaybackStutterProbe::enabled())
    {
        const double displayBudgetMs = 1000.0 / qMax(1.0, effectiveFrameRate());
        if (displayIntervalNs > 0)
        {
            PlaybackStutterProbe::recordDuration(
                "frame_scheduler.display_interval",
                static_cast<double>(displayIntervalNs) / 1000000.0,
                displayBudgetMs * 1.35,
                true);
        }
        if (readySteadyNs > 0)
        {
            const qint64 dispatchDelayNs =
                qMax<qint64>(0, DisplayFrameScheduler::steadyNowNs() - readySteadyNs);
            PlaybackStutterProbe::recordDuration(
                "frame_scheduler.ui_dispatch_delay",
                static_cast<double>(dispatchDelayNs) / 1000000.0,
                displayBudgetMs * 0.35,
                true);
        }
        if (skippedRefreshes > 0)
        {
            PlaybackStutterProbe::recordCounter(
                "frame_scheduler.skipped_display_refreshes",
                skippedRefreshes,
                true);
        }
    }

    const qint64 dispatchFrameClockNs = m_frameClock.nsecsElapsed();
    const qint64 dispatchSteadyNs = DisplayFrameScheduler::steadyNowNs();
    const qint64 presentationFrameClockNs =
        presentationSteadyNs > 0
            ? dispatchFrameClockNs + (presentationSteadyNs - dispatchSteadyNs)
            : dispatchFrameClockNs;
    if (!emitFramePulse(presentationFrameClockNs))
    {
        m_pendingPaintFrameSeq.store(0, std::memory_order_release);
        m_frameInFlight.store(false, std::memory_order_release);
        return;
    }

    // The main canvas acknowledges this exact emitted frame after paintEvent.
    // Using the sequence prevents an unrelated expose/resize paint from
    // releasing a newer frame that has only just been queued.
    m_pendingPaintFrameSeq.store(m_frameSeq, std::memory_order_release);
}

bool PlaybackController::emitFramePulse(qint64 nowNs)
{
    if (m_waitingForAudioProgress)
        return false;

    if (PlaybackStutterProbe::enabled())
    {
        if (m_lastPulseProbeNs > 0 && nowNs > m_lastPulseProbeNs)
        {
            const double intervalMs = static_cast<double>(nowNs - m_lastPulseProbeNs) / 1000000.0;
            const double targetIntervalMs = 1000.0 / qMax(1.0, effectiveFrameRate());
            PlaybackStutterProbe::recordDuration(
                "playback.pulse_interval", intervalMs, targetIntervalMs * 1.35, true);
            if (m_lastPulseIntervalMs >= 0.0)
            {
                PlaybackStutterProbe::recordDuration(
                    "playback.pulse_interval_jerk",
                    std::abs(intervalMs - m_lastPulseIntervalMs),
                    2.0,
                    true);
            }
            m_lastPulseIntervalMs = intervalMs;
        }
        m_lastPulseProbeNs = nowNs;
    }

    if (!m_frameAnchorValid)
        resetFrameAnchor(currentTime(), nowNs);

    double predictedMs = predictedTimeAt(nowNs);
    predictedMs = qMax(0.0, predictedMs);
    if (predictedMs < m_lastFrameTickMs)
        predictedMs = m_lastFrameTickMs;

    m_lastFrameTickMs = predictedMs;
    ++m_frameSeq;
    emit playbackFrameTick(predictedMs, m_frameSeq);
    return true;
}

void PlaybackController::resetFrameAnchor(double timeMs, qint64 nowNs)
{
    m_frameAnchorTimeMs = qMax(0.0, timeMs);
    m_frameAnchorWallNs = nowNs;
    m_frameAnchorValid = true;
}

double PlaybackController::predictedTimeAt(qint64 nowNs) const
{
    if (!m_frameAnchorValid)
        return qMax(0.0, static_cast<double>(m_audioPlayer->adjustedPosition()));
    if (m_waitingForAudioProgress)
        return m_frameAnchorTimeMs;

    const double elapsedMs = static_cast<double>(nowNs - m_frameAnchorWallNs) / 1000000.0;
    const double effectiveRate = qMax(0.0, m_speed + m_frameRateCorrection);
    double predictedMs = m_frameAnchorTimeMs + elapsedMs * effectiveRate;
    predictedMs = qMax(0.0, predictedMs);
    const qint64 durationMs = m_audioPlayer->duration();
    if (durationMs > 0)
        predictedMs = qMin(predictedMs, static_cast<double>(durationMs));
    return predictedMs;
}

void PlaybackController::applyObservedTimeToAnchor(double observedMs, qint64 nowNs)
{
    const double clampedObserved = qMax(0.0, observedMs);
    if (!m_frameAnchorValid)
    {
        resetFrameAnchor(clampedObserved, nowNs);
        return;
    }

    const double predictedMs = predictedTimeAt(nowNs);
    const double errorMs = clampedObserved - predictedMs;
    const double absoluteErrorMs = std::abs(errorMs);
    const double deadZoneMs = qMax(0.15, 0.75 * m_speed);
    // Backend positions are particularly coarse at low playback rates. A
    // 20 ms media-time observation error at 0.1x can still be a harmless
    // decoder callback quantization artifact, so it must not teleport the
    // visual clock. Explicit seeks and startup already reset the anchor.
    const double hardResyncMs = qMax(50.0, 120.0 * m_speed);

    if (PlaybackStutterProbe::enabled())
    {
        PlaybackStutterProbe::recordDuration(
            "playback.audio_clock_error_abs_ms",
            absoluteErrorMs,
            qMax(0.25, 4.0 * m_speed),
            true);
    }

    if (absoluteErrorMs >= hardResyncMs)
    {
        m_frameRateCorrection = 0.0;
        resetFrameAnchor(clampedObserved, nowNs);
        if (PlaybackStutterProbe::enabled())
            PlaybackStutterProbe::recordCounter("playback.anchor_hard_resync", 1, true);
        return;
    }

    double targetRateCorrection = 0.0;
    if (absoluteErrorMs > deadZoneMs)
    {
        const double maxRateCorrection = qMax(1e-6, m_speed * kClockSlewMaxRateFraction);
        targetRateCorrection = qBound(
            -maxRateCorrection,
            errorMs / kClockSlewConvergenceWallMs,
            maxRateCorrection);
    }

    // Keep the predicted position continuous. Applying a fraction of the
    // backend error directly to the anchor creates a visible one-frame jump
    // every time QMediaPlayer publishes its coarse position. Slewing the rate
    // converges to the audio clock without moving the current frame.
    m_frameRateCorrection +=
        (targetRateCorrection - m_frameRateCorrection) * kClockSlewFilterGain;
    if (std::abs(m_frameRateCorrection) < 1e-7)
        m_frameRateCorrection = 0.0;
    resetFrameAnchor(predictedMs, nowNs);
    if (PlaybackStutterProbe::enabled())
    {
        PlaybackStutterProbe::recordDuration(
            "playback.anchor_slew_rate_pct",
            std::abs(m_frameRateCorrection) * 100.0 / qMax(1e-6, m_speed),
            kClockSlewMaxRateFraction * 100.0,
            true);
    }
}

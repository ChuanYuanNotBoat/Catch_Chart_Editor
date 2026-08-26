#include "PlaybackController.h"
#include "utils/MathUtils.h"
#include "utils/Logger.h"
#include "utils/PlaybackSpeed.h"
#include "utils/PlaybackStutterProbe.h"
#include "utils/Settings.h"
#include "model/Chart.h"
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr qint64 kSeekSameValueThresholdMs = 2;
}

PlaybackController::PlaybackController(AudioPlayer *audioPlayer, QObject *parent)
    : QObject(parent),
      m_audioPlayer(audioPlayer),
      m_state(Stopped),
      m_speed(1.0),
      m_noteSoundEnabled(true),
      m_autoPausedAtEnd(false),
      m_framePulseTimer(new QTimer(this)),
      m_frameRateCap(60),
      m_displayRefreshRateHz(60.0),
      m_framePulseIntervalNs(kNanosecondsPerSecond / 60),
      m_nextFramePulseDeadlineNs(0),
      m_externalFramePulseEnabled(false),
      m_externalFrameAccumulator(0.0),
      m_frameAnchorValid(false),
      m_frameAnchorTimeMs(0.0),
      m_frameAnchorWallMs(0),
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
    m_framePulseTimer->setSingleShot(true);
    m_framePulseTimer->setTimerType(Qt::PreciseTimer);
    connect(m_framePulseTimer, &QTimer::timeout, this, &PlaybackController::onFramePulseTimeout);
    m_frameClock.start();
    setFrameRateCap(Settings::instance().playbackFrameRateCap());
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
        resetFrameAnchor(m_lastFrameTickMs, m_frameClock.elapsed());
        // QMediaPlayer can report PlayingState before decoded audio reaches the
        // output. Hold the editor clock until the media position really moves.
        m_waitingForAudioProgress = true;
        m_audioProgressStartMs = m_lastFrameTickMs;
        startFramePulse();
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
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
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
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
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

    const qint64 nowMs = m_frameClock.elapsed();
    const double continuousTimeMs = (m_state == Playing)
                                        ? predictedTimeAt(nowMs)
                                        : static_cast<double>(m_audioPlayer->adjustedPosition());
    m_speed = speed;
    m_audioPlayer->setSpeed(speed);
    if (m_state == Playing)
    {
        resetFrameAnchor(continuousTimeMs, nowMs);
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

    updateFramePulseInterval();
    resetExternalFrameAccumulator();
    if (m_framePulseTimer->isActive())
        startFramePulse();
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
    updateFramePulseInterval();
    resetExternalFrameAccumulator();
    if (m_framePulseTimer->isActive())
        startFramePulse();
}

double PlaybackController::displayRefreshRate() const
{
    return m_displayRefreshRateHz;
}

double PlaybackController::effectiveFrameRate() const
{
    return (m_frameRateCap <= 0)
               ? m_displayRefreshRateHz
               : static_cast<double>(m_frameRateCap);
}

void PlaybackController::setExternalFramePulseEnabled(bool enabled)
{
    if (m_externalFramePulseEnabled == enabled)
        return;

    m_externalFramePulseEnabled = enabled;
    resetExternalFrameAccumulator();
    if (enabled)
    {
        m_framePulseTimer->stop();
    }
    else if (m_state == Playing)
    {
        startFramePulse();
    }
}

void PlaybackController::advanceExternalFramePulse()
{
    if (!m_externalFramePulseEnabled || m_state != Playing)
        return;

    const double targetFps = effectiveFrameRate();
    const double displayFps = qMax(1.0, m_displayRefreshRateHz);
    // Treat near-equal nominal rates (for example 60 vs 59.94/60.75 Hz) as
    // one pulse per refresh. Downsampling them creates a periodic skipped
    // frame even though the requested cap and the display mode are equivalent.
    if (targetFps < displayFps * 0.98)
    {
        m_externalFrameAccumulator += targetFps;
        if (m_externalFrameAccumulator + 0.001 < displayFps)
            return;
        m_externalFrameAccumulator -= displayFps;
    }

    emitFramePulse(m_frameClock.nsecsElapsed());
}

void PlaybackController::seekTo(double timeMs)
{
    m_autoPausedAtEnd = false;
    const qint64 targetMs = clampSeekTargetMs(static_cast<qint64>(qRound64(timeMs)));
    const qint64 nowMs = m_frameClock.elapsed();
    resetFrameAnchor(static_cast<double>(targetMs), nowMs);
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
        return predictedTimeAt(m_frameClock.elapsed());
    return static_cast<double>(m_audioPlayer->adjustedPosition());
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
    const double observedMs = static_cast<double>(m_audioPlayer->adjustedPosition());
    emit positionChanged(observedMs);
    if (m_state == Playing)
    {
        const qint64 nowMs = m_frameClock.elapsed();
        if (m_waitingForAudioProgress)
        {
            if (std::abs(observedMs - m_audioProgressStartMs) <= kAudioProgressEpsilonMs)
                return;
            m_waitingForAudioProgress = false;
        }
        applyObservedTimeToAnchor(observedMs, nowMs);
    }
}

void PlaybackController::onAudioStateChanged(QMediaPlayer::PlaybackState state)
{
    if (m_state == Playing && state == QMediaPlayer::StoppedState)
    {
        m_autoPausedAtEnd = true;
        m_state = Paused;
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
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
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
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

void PlaybackController::onFramePulseTimeout()
{
    if (m_state != Playing || m_externalFramePulseEnabled)
        return;

    const qint64 nowNs = m_frameClock.nsecsElapsed();
    scheduleNextFramePulse(nowNs);
    emitFramePulse(nowNs);
}

void PlaybackController::emitFramePulse(qint64 nowNs)
{
    if (m_waitingForAudioProgress)
        return;

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

    const qint64 nowMs = nowNs / 1000000LL;
    if (!m_frameAnchorValid)
        resetFrameAnchor(currentTime(), nowMs);

    double predictedMs = predictedTimeAt(nowMs);
    predictedMs = qMax(0.0, predictedMs);
    if (predictedMs < m_lastFrameTickMs)
        predictedMs = m_lastFrameTickMs;

    m_lastFrameTickMs = predictedMs;
    ++m_frameSeq;
    emit playbackFrameTick(predictedMs, m_frameSeq);
}

void PlaybackController::startFramePulse()
{
    if (m_externalFramePulseEnabled || m_state != Playing)
        return;

    const qint64 nowNs = m_frameClock.nsecsElapsed();
    m_nextFramePulseDeadlineNs = nowNs + m_framePulseIntervalNs;
    const int delayMs = qMax(1, static_cast<int>(qRound64(
                                  static_cast<double>(m_framePulseIntervalNs) / 1000000.0)));
    m_framePulseTimer->start(delayMs);
}

void PlaybackController::scheduleNextFramePulse(qint64 nowNs)
{
    m_nextFramePulseDeadlineNs += m_framePulseIntervalNs;
    while (m_nextFramePulseDeadlineNs <= nowNs)
        m_nextFramePulseDeadlineNs += m_framePulseIntervalNs;

    const qint64 remainingNs = m_nextFramePulseDeadlineNs - nowNs;
    const int delayMs = qMax(1, static_cast<int>(qRound64(
                                  static_cast<double>(remainingNs) / 1000000.0)));
    m_framePulseTimer->start(delayMs);
}

void PlaybackController::updateFramePulseInterval()
{
    const double targetFps = qMax(1.0, effectiveFrameRate());
    m_framePulseIntervalNs = qMax<qint64>(
        1,
        qRound64(static_cast<double>(kNanosecondsPerSecond) / targetFps));
}

void PlaybackController::resetExternalFrameAccumulator()
{
    const double displayFps = qMax(1.0, m_displayRefreshRateHz);
    const double targetFps = qMax(1.0, effectiveFrameRate());
    m_externalFrameAccumulator = qMax(0.0, displayFps - qMin(displayFps, targetFps));
}

void PlaybackController::resetFrameAnchor(double timeMs, qint64 nowMs)
{
    m_frameAnchorTimeMs = qMax(0.0, timeMs);
    m_frameAnchorWallMs = nowMs;
    m_frameAnchorValid = true;
}

double PlaybackController::predictedTimeAt(qint64 nowMs) const
{
    if (!m_frameAnchorValid)
        return qMax(0.0, static_cast<double>(m_audioPlayer->adjustedPosition()));
    if (m_waitingForAudioProgress)
        return m_frameAnchorTimeMs;

    double predictedMs = m_frameAnchorTimeMs +
                         static_cast<double>(nowMs - m_frameAnchorWallMs) * m_speed;
    predictedMs = qMax(0.0, predictedMs);
    const qint64 durationMs = m_audioPlayer->duration();
    if (durationMs > 0)
        predictedMs = qMin(predictedMs, static_cast<double>(durationMs));
    return predictedMs;
}

void PlaybackController::applyObservedTimeToAnchor(double observedMs, qint64 nowMs)
{
    const double clampedObserved = qMax(0.0, observedMs);
    // Media position is the authoritative audio clock. Anchor directly to it;
    // gentle percentage corrections retain a large wall-clock error for much
    // too long at 0.1x. Emitted frame ticks remain monotonic in the caller.
    resetFrameAnchor(clampedObserved, nowMs);
}

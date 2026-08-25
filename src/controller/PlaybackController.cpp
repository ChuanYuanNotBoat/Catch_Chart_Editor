#include "PlaybackController.h"
#include "utils/MathUtils.h"
#include "utils/Logger.h"
#include "utils/PlaybackSpeed.h"
#include "utils/Settings.h"
#include "model/Chart.h"
#include <QTimer>
#include <algorithm>

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
      m_frameAnchorValid(false),
      m_frameAnchorTimeMs(0.0),
      m_frameAnchorWallMs(0),
      m_frameSeq(0),
      m_lastFrameTickMs(0.0)
{
    connect(m_audioPlayer, &AudioPlayer::positionChanged, this, &PlaybackController::onAudioPositionChanged);
    connect(m_audioPlayer, &AudioPlayer::stateChanged, this, &PlaybackController::onAudioStateChanged);
    connect(m_audioPlayer, &AudioPlayer::errorOccurred, this, &PlaybackController::onAudioError);
    m_framePulseTimer->setInterval(kFramePulseIntervalMs);
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
        m_lastFrameTickMs = currentTime();
        resetFrameAnchor(m_lastFrameTickMs, m_frameClock.elapsed());
        if (!m_framePulseTimer->isActive())
            m_framePulseTimer->start();
        Logger::debug(QString("PlaybackController::play - Playing from position %1ms").arg(m_audioPlayer->position()));
        emit stateChanged(m_state);
        emit playbackFrameTick(m_lastFrameTickMs, m_frameSeq);
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
        m_audioPlayer->pause();
        m_state = Paused;
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
        Logger::debug(QString("PlaybackController::pause - Paused at position %1ms").arg(m_audioPlayer->position()));
        emit stateChanged(m_state);
    }
}

void PlaybackController::stop()
{
    if (m_state != Stopped)
    {
        m_autoPausedAtEnd = false;
        m_audioPlayer->stop();
        m_state = Stopped;
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
        m_frameSeq = 0;
        Logger::debug("PlaybackController::stop - Playback stopped");
        emit stateChanged(m_state);
    }
}

void PlaybackController::setSpeed(double speed)
{
    speed = PlaybackSpeed::sanitize(speed);
    m_speed = speed;
    m_audioPlayer->setSpeed(speed);
    if (m_state == Playing)
    {
        const qint64 nowMs = m_frameClock.elapsed();
        resetFrameAnchor(currentTime(), nowMs);
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

    int intervalMs = kFramePulseIntervalMs;
    if (m_frameRateCap == 120 || m_frameRateCap == 0)
        intervalMs = 8;
    else if (m_frameRateCap == 90)
        intervalMs = 11;

    m_framePulseTimer->setInterval(intervalMs);
}

int PlaybackController::frameRateCap() const
{
    return m_frameRateCap;
}

void PlaybackController::seekTo(double timeMs)
{
    m_autoPausedAtEnd = false;
    const qint64 targetMs = clampSeekTargetMs(static_cast<qint64>(qRound64(timeMs)));
    applySeekNow(targetMs, "direct");
    m_frameAnchorValid = false;
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
        applyObservedTimeToAnchor(observedMs, m_frameClock.elapsed());
}

void PlaybackController::onAudioStateChanged(QMediaPlayer::PlaybackState state)
{
    if (m_state == Playing && state == QMediaPlayer::StoppedState)
    {
        m_autoPausedAtEnd = true;
        m_audioPlayer->setAdjustedPosition(0);
        m_state = Paused;
        if (m_framePulseTimer->isActive())
            m_framePulseTimer->stop();
        m_frameAnchorValid = false;
        m_frameSeq = 0;
        m_lastFrameTickMs = 0.0;
        Logger::info("PlaybackController::onAudioStateChanged - Reached end of media, auto-paused at beginning");
        emit positionChanged(static_cast<double>(m_audioPlayer->adjustedPosition()));
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
        m_frameSeq = 0;
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
    if (m_state != Playing)
        return;

    const qint64 nowMs = m_frameClock.elapsed();
    if (!m_frameAnchorValid)
        resetFrameAnchor(currentTime(), nowMs);

    double predictedMs = m_frameAnchorTimeMs +
                         static_cast<double>(nowMs - m_frameAnchorWallMs) * m_speed;
    predictedMs = qMax(0.0, predictedMs);
    if (predictedMs < m_lastFrameTickMs)
        predictedMs = m_lastFrameTickMs;

    m_lastFrameTickMs = predictedMs;
    ++m_frameSeq;
    emit playbackFrameTick(predictedMs, m_frameSeq);
}

void PlaybackController::resetFrameAnchor(double timeMs, qint64 nowMs)
{
    m_frameAnchorTimeMs = qMax(0.0, timeMs);
    m_frameAnchorWallMs = nowMs;
    m_frameAnchorValid = true;
}

void PlaybackController::applyObservedTimeToAnchor(double observedMs, qint64 nowMs)
{
    const double clampedObserved = qMax(0.0, observedMs);
    if (!m_frameAnchorValid)
    {
        resetFrameAnchor(clampedObserved, nowMs);
        return;
    }

    const double predicted = m_frameAnchorTimeMs +
                             static_cast<double>(nowMs - m_frameAnchorWallMs) * m_speed;
    const double delta = clampedObserved - predicted;

    if (std::abs(delta) <= kAnchorDeadZoneMs)
    {
        resetFrameAnchor(predicted, nowMs);
        return;
    }
    if (std::abs(delta) < kAnchorModerateWindowMs)
    {
        resetFrameAnchor(predicted + delta * kAnchorModerateGain, nowMs);
        return;
    }
    if (std::abs(delta) < kAnchorLargeWindowMs)
    {
        resetFrameAnchor(predicted + delta * kAnchorLargeGain, nowMs);
        return;
    }

    resetFrameAnchor(clampedObserved, nowMs);
}

#pragma once

#include <QObject>
#include <QElapsedTimer>
#include "audio/AudioPlayer.h"

class QTimer;

class PlaybackController : public QObject
{
    Q_OBJECT
public:
    enum State
    {
        Stopped,
        Playing,
        Paused
    };

    explicit PlaybackController(AudioPlayer *audioPlayer, QObject *parent = nullptr);

    State state() const;
    AudioPlayer *audioPlayer() const { return m_audioPlayer; }

    void play();
    void playFromTime(double timeMs);
    void pause();
    void stop();
    void setSpeed(double speed);
    double speed() const;
    void setFrameRateCap(int fpsCap);
    int frameRateCap() const;
    void setDisplayRefreshRate(double refreshRateHz);
    double displayRefreshRate() const;
    double effectiveFrameRate() const;
    void setExternalFramePulseEnabled(bool enabled);
    void advanceExternalFramePulse();
    void seekTo(double timeMs);
    void seekToBeat(int beat, int num, int den);

    double currentTime() const;

    void setNoteSoundEnabled(bool enabled);
    bool autoPausedAtEnd() const;

signals:
    void stateChanged(State newState);
    void positionChanged(double timeMs);
    void playbackFrameTick(double predictedTimeMs, qint64 frameSeq);
    void speedChanged(double speed);
    void beatReached(int beatNum, int num, int den);
    void errorOccurred(const QString &error);
private slots:
    void onAudioPositionChanged(qint64 position);
    void onAudioStateChanged(QMediaPlayer::PlaybackState state);
    void onAudioError(const QString &error);
    void onFramePulseTimeout();

private:
    static constexpr qint64 kNanosecondsPerSecond = 1000000000LL;
    static constexpr double kAudioProgressEpsilonMs = 0.25;

    qint64 clampSeekTargetMs(qint64 timeMs) const;
    void applySeekNow(qint64 targetMs, const char *reason);
    void startFramePulse();
    void scheduleNextFramePulse(qint64 nowNs);
    void updateFramePulseInterval();
    void emitFramePulse(qint64 nowNs);
    void resetExternalFrameAccumulator();
    double predictedTimeAt(qint64 nowMs) const;
    void resetFrameAnchor(double timeMs, qint64 nowMs);
    void applyObservedTimeToAnchor(double observedMs, qint64 nowMs);

    AudioPlayer *m_audioPlayer;
    State m_state;
    double m_speed;
    bool m_noteSoundEnabled;
    bool m_autoPausedAtEnd;
    QTimer *m_framePulseTimer;
    int m_frameRateCap;
    double m_displayRefreshRateHz;
    qint64 m_framePulseIntervalNs;
    qint64 m_nextFramePulseDeadlineNs;
    bool m_externalFramePulseEnabled;
    double m_externalFrameAccumulator;
    QElapsedTimer m_frameClock;
    bool m_frameAnchorValid;
    double m_frameAnchorTimeMs;
    qint64 m_frameAnchorWallMs;
    bool m_waitingForAudioProgress;
    double m_audioProgressStartMs;
    qint64 m_frameSeq;
    double m_lastFrameTickMs;
};

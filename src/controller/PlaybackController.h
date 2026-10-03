#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <atomic>
#include <memory>
#include "audio/AudioPlayer.h"

class DisplayFrameScheduler;
class QEvent;

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
    ~PlaybackController() override;

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
    void seekTo(double timeMs);
    void seekToBeat(int beat, int num, int den);

    void setLoopRange(double startMs, double endMs, bool enabled);
    double loopStartMs() const { return m_loopStartMs; }
    double loopEndMs() const { return m_loopEndMs; }
    bool loopEnabled() const { return m_loopEnabled; }

    double currentTime() const;
    double visualTime() const;
    void acknowledgeFramePainted(qint64 frameSeq);

    void setNoteSoundEnabled(bool enabled);
    bool autoPausedAtEnd() const;

signals:
    void loopRangeChanged(double startMs, double endMs, bool enabled);
    void stateChanged(State newState);
    void positionChanged(double timeMs);
    void playbackFrameTick(double predictedTimeMs, qint64 frameSeq);
    void speedChanged(double speed);
    void beatReached(int beatNum, int num, int den);
    void errorOccurred(const QString &error);

protected:
    bool event(QEvent *event) override;

private slots:
    void onAudioPositionChanged(qint64 position);
    void onAudioStateChanged(QMediaPlayer::PlaybackState state);
    void onAudioError(const QString &error);

private:
    static constexpr double kAudioProgressEpsilonMs = 0.25;

    qint64 clampSeekTargetMs(qint64 timeMs) const;
    void applySeekNow(qint64 targetMs, const char *reason);
    void updateFrameScheduler();
    void dispatchScheduledFrame();
    void beginPlaybackUiPriority();
    void endPlaybackUiPriority();
    bool emitFramePulse(qint64 nowNs);
    double predictedTimeAt(qint64 nowNs) const;
    void resetFrameAnchor(double timeMs, qint64 nowNs);
    void applyObservedTimeToAnchor(double observedMs,
                                   qint64 nowNs,
                                   bool allowHardResync = true);

    double m_loopStartMs = 0, m_loopEndMs = 0;
    bool m_loopEnabled = false;
    AudioPlayer *m_audioPlayer;
    State m_state;
    double m_speed;
    bool m_noteSoundEnabled;
    bool m_autoPausedAtEnd;
    std::unique_ptr<DisplayFrameScheduler> m_frameScheduler;
    int m_originalUiThreadPriority;
    bool m_uiPriorityRaised;
    int m_frameRateCap;
    double m_displayRefreshRateHz;
    std::atomic<bool> m_frameInFlight;
    std::atomic<qint64> m_schedulerReadySteadyNs;
    std::atomic<qint64> m_schedulerIntervalNs;
    std::atomic<qint64> m_schedulerPresentationSteadyNs;
    std::atomic<qint64> m_schedulerSkippedFrames;
    std::atomic<qint64> m_pendingPaintFrameSeq;
    QElapsedTimer m_frameClock;
    bool m_frameAnchorValid;
    double m_frameAnchorTimeMs;
    qint64 m_frameAnchorWallNs;
    double m_frameRateCorrection;
    bool m_awaitingInitialAudioProgress;
    double m_audioProgressStartMs;
    qint64 m_frameSeq;
    double m_lastFrameTickMs;
    qint64 m_lastPulseProbeNs;
    double m_lastPulseIntervalMs;
};

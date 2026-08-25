#pragma once

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QTimer>

class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject *parent = nullptr);
    ~AudioPlayer();

    bool load(const QString &filePath);
    void play();
    void pause();
    void stop();
    void setPosition(qint64 positionMs);
    qint64 position() const;
    qint64 duration() const;
    void setSpeed(double speed); // 0.1 ~ 10.0
    double speed() const;

    bool isPlaying() const;
    bool isPaused() const;
    bool isLoaded() const;
    bool canPlay() const;
    QString lastError() const;

    enum class LoadingState
    {
        Idle,
        Loading,
        Loaded,
        Error
    };
    Q_ENUM(LoadingState)

    LoadingState loadingState() const;
    void setLoadingState(LoadingState state);

    void setAudioLatency(int latency);
    int audioLatency() const;
    void setUserOffset(int offset);
    int userOffset() const;
    void setAudioCorrectionEnabled(bool enabled);
    bool audioCorrectionEnabled() const;
    qint64 adjustedPosition() const;
    void setAdjustedPosition(qint64 adjustedMs);

signals:
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void stateChanged(QMediaPlayer::PlaybackState state);
    void errorOccurred(const QString &error);
    void loadingStateChanged(LoadingState state);

private:
    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    LoadingState m_loadingState;
    QTimer *m_loadTimeoutTimer;
    QString m_currentLoadPath;
    bool m_loaded;
    QString m_lastError;
    QStringList m_tempAudioFiles;

    int m_audioLatency;
    int m_userOffset;
    bool m_audioCorrectionEnabled;

    QString normalizeAudioPath(const QString &originalPath);
    void cleanupTempAudioFiles();
};

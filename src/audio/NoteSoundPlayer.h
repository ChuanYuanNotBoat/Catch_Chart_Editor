#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QSoundEffect;

class NoteSoundPlayer : public QObject
{
    Q_OBJECT
public:
    explicit NoteSoundPlayer(QObject *parent = nullptr);

    void playHitSound();
    void setSoundFile(const QString &filePath);
    QString soundFile() const;
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }
    void setVolumePercent(int volume);
    int volumePercent() const;
    bool hasValidSound() const;

    // Timing probe support: steady-clock timestamp of the last play() call
    // and how often the voice pool had to steal a busy voice.
    qint64 lastPlaySteadyNs() const { return m_lastPlaySteadyNs; }
    qint64 restartCount() const { return m_restartCount; }

signals:
    // A submission timestamp, not a hardware/output timestamp.
    void hitSoundTriggered(qint64 steadyNs);

private:
    static constexpr int kVoiceCount = 6;
    QVector<QSoundEffect *> m_voices;
    bool m_enabled;
    int m_nextVoice;
    qint64 m_lastPlaySteadyNs = 0;
    qint64 m_restartCount = 0;
};

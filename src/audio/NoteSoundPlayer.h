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

private:
    static constexpr int kVoiceCount = 6;
    QVector<QSoundEffect *> m_voices;
    bool m_enabled;
    int m_nextVoice;
};

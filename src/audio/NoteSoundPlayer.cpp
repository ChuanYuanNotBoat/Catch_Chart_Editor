#include "NoteSoundPlayer.h"

#include <QFile>
#include <QSoundEffect>
#include <QUrl>
#include <QtGlobal>

NoteSoundPlayer::NoteSoundPlayer(QObject *parent)
    : QObject(parent),
      m_enabled(false),
      m_nextVoice(0)
{
    m_voices.reserve(kVoiceCount);
    for (int i = 0; i < kVoiceCount; ++i)
    {
        auto *voice = new QSoundEffect(this);
        voice->setLoopCount(1);
        m_voices.append(voice);
    }
    setVolumePercent(100);
}

void NoteSoundPlayer::playHitSound()
{
    if (!m_enabled || !hasValidSound())
        return;

    // Rotate through preloaded voices. Reusing one QSoundEffect requires a
    // stop/play round trip and either delays or drops dense note sounds.
    QSoundEffect *voice = nullptr;
    for (int i = 0; i < m_voices.size(); ++i)
    {
        const int candidate = (m_nextVoice + i) % m_voices.size();
        if (!m_voices[candidate]->isPlaying())
        {
            voice = m_voices[candidate];
            m_nextVoice = (candidate + 1) % m_voices.size();
            break;
        }
    }

    if (!voice)
    {
        voice = m_voices[m_nextVoice];
        voice->stop();
        m_nextVoice = (m_nextVoice + 1) % m_voices.size();
    }
    voice->play();
}

void NoteSoundPlayer::setSoundFile(const QString &filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath))
    {
        for (QSoundEffect *voice : m_voices)
            voice->setSource(QUrl());
        return;
    }

    const QUrl source = QUrl::fromLocalFile(filePath);
    for (QSoundEffect *voice : m_voices)
        voice->setSource(source);
    m_nextVoice = 0;
}

QString NoteSoundPlayer::soundFile() const
{
    return m_voices.isEmpty() ? QString() : m_voices.constFirst()->source().toLocalFile();
}

void NoteSoundPlayer::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
    {
        for (QSoundEffect *voice : m_voices)
            voice->stop();
    }
}

void NoteSoundPlayer::setVolumePercent(int volume)
{
    const int clamped = qBound(0, volume, 200);
    for (QSoundEffect *voice : m_voices)
        voice->setVolume(static_cast<qreal>(clamped) / 100.0);
}

int NoteSoundPlayer::volumePercent() const
{
    return m_voices.isEmpty() ? 0 : qRound(m_voices.constFirst()->volume() * 100.0);
}

bool NoteSoundPlayer::hasValidSound() const
{
    return !m_voices.isEmpty() && !m_voices.constFirst()->source().isEmpty();
}

#include "NoteSoundPlayer.h"

#include <QFile>
#include <QSoundEffect>
#include <QUrl>
#include <QtGlobal>
#include <chrono>

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

bool NoteSoundPlayer::playHitSound()
{
    if (!m_enabled)
        return false;

    // Rotate through preloaded voices. Reusing one QSoundEffect requires a
    // stop/play round trip and either delays or drops dense note sounds.
    int selected = -1;
    for (int i = 0; i < m_voices.size(); ++i)
    {
        const int candidate = (m_nextVoice + i) % m_voices.size();
        if (!m_voices[candidate]->isLoaded())
            continue;
        if (selected < 0)
            selected = candidate;
        if (!m_voices[candidate]->isPlaying())
        {
            selected = candidate;
            break;
        }
    }
    if (selected < 0)
        return false;

    auto *voice = m_voices[selected];
    if (voice->isPlaying())
    {
        voice->stop();
        ++m_restartCount;
    }
    m_nextVoice = (selected + 1) % m_voices.size();
    voice->play();
    m_lastPlaySteadyNs = static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    emit hitSoundTriggered(m_lastPlaySteadyNs);
    return true;
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
    const int clamped = qBound(0, volume, 100);
    for (QSoundEffect *voice : m_voices)
        voice->setVolume(static_cast<qreal>(clamped) / 100.0);
}

int NoteSoundPlayer::volumePercent() const
{
    return m_voices.isEmpty() ? 0 : qRound(m_voices.constFirst()->volume() * 100.0);
}

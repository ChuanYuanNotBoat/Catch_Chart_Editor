#include "AudioPlayer.h"

#include "audio/PlaybackTiming.h"
#include "utils/Logger.h"
#include "utils/PerformanceTimer.h"
#include "utils/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QPlaybackOptions>
#endif
#include <QTemporaryFile>
#include <QUrl>

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent),
      m_player(new QMediaPlayer(this)),
      m_audioOutput(new QAudioOutput(this)),
      m_loadingState(LoadingState::Idle),
      m_loadTimeoutTimer(new QTimer(this)),
      m_loaded(false),
      m_audioLatency(Settings::instance().audioLatency()),
      m_userOffset(Settings::instance().globalAudioOffset()),
      m_audioCorrectionEnabled(Settings::instance().audioCorrectionEnabled())
{
    m_player->setAudioOutput(m_audioOutput);

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    // Editing favors responsive local playback over a large glitch-resistant
    // buffer. Backends that do not support this hint safely ignore it.
    QPlaybackOptions playbackOptions = m_player->playbackOptions();
    playbackOptions.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
    m_player->setPlaybackOptions(playbackOptions);

    // Pitch compensation adds a large processing queue at very low rates.
    if (m_player->pitchCompensationAvailability() ==
        QMediaPlayer::PitchCompensationAvailability::Available)
    {
        m_player->setPitchCompensation(false);
    }
#endif

    connect(m_player, &QMediaPlayer::positionChanged, this, &AudioPlayer::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &AudioPlayer::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &AudioPlayer::stateChanged);

    m_loadTimeoutTimer->setSingleShot(true);
    connect(m_loadTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_loadingState == LoadingState::Loading)
            finishLoadFailure(tr("Audio loading timed out after 5 seconds."));
    });
}

AudioPlayer::~AudioPlayer()
{
    cancelPendingLoad();
    cleanupTempAudioFiles();
}

AudioPlayer::LoadingState AudioPlayer::loadingState() const
{
    return m_loadingState;
}

void AudioPlayer::setLoadingState(LoadingState state)
{
    if (m_loadingState == state)
        return;

    m_loadingState = state;
    m_loaded = (state == LoadingState::Loaded);
    emit loadingStateChanged(state);
}

void AudioPlayer::disconnectLoadSignals()
{
    QObject::disconnect(m_mediaStatusConnection);
    QObject::disconnect(m_mediaErrorConnection);
    m_mediaStatusConnection = QMetaObject::Connection();
    m_mediaErrorConnection = QMetaObject::Connection();
}

void AudioPlayer::cancelPendingLoad()
{
    ++m_loadGeneration;
    m_loadTimeoutTimer->stop();
    disconnectLoadSignals();
}

void AudioPlayer::finishLoadFailure(const QString &message)
{
    m_loadTimeoutTimer->stop();
    disconnectLoadSignals();
    m_lastError = message;
    Logger::error(QString("AudioPlayer::load - %1").arg(message));
    setLoadingState(LoadingState::Error);
    emit errorOccurred(message);
}

void AudioPlayer::finishLoadSuccess()
{
    const qint64 mediaDuration = m_player->duration();
    if (mediaDuration <= 0)
    {
        finishLoadFailure(tr("The audio duration is invalid or the format is unsupported: %1")
                              .arg(m_player->errorString()));
        return;
    }

    m_loadTimeoutTimer->stop();
    disconnectLoadSignals();
    Logger::info(QString("AudioPlayer::load - Loaded successfully, duration: %1 ms")
                     .arg(mediaDuration));
    setLoadingState(LoadingState::Loaded);
}

bool AudioPlayer::load(const QString &filePath)
{
    PerformanceTimer loadTimer("AudioPlayer::load", "audio");
    Logger::info(QString("AudioPlayer::load - Loading audio from: %1").arg(filePath));

    // Invalidate callbacks before changing QMediaPlayer's source. The previous
    // implementation reset the state first, making its cancellation branch
    // unreachable and allowing stale load callbacks to survive a quick reload.
    cancelPendingLoad();
    setLoadingState(LoadingState::Idle);
    m_lastError.clear();
    m_currentLoadPath.clear();
    m_player->stop();
    m_player->setSource(QUrl());
    cleanupTempAudioFiles();

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile())
    {
        finishLoadFailure(tr("Audio file does not exist: %1").arg(filePath));
        return false;
    }
    if (!fileInfo.isReadable())
    {
        finishLoadFailure(tr("Audio file is not readable: %1").arg(filePath));
        return false;
    }

    Logger::debug(QString("AudioPlayer::load - File size: %1 bytes, extension: %2")
                      .arg(fileInfo.size())
                      .arg(fileInfo.suffix()));

    const QString actualPath = normalizeAudioPath(filePath);
    if (actualPath.isEmpty())
    {
        finishLoadFailure(tr("Failed to prepare the audio path for playback."));
        return false;
    }

    const QUrl url = QUrl::fromLocalFile(actualPath);
    const quint64 generation = ++m_loadGeneration;
    setLoadingState(LoadingState::Loading);
    m_currentLoadPath = filePath;

    m_mediaStatusConnection = connect(
        m_player,
        &QMediaPlayer::mediaStatusChanged,
        this,
        [this, generation](QMediaPlayer::MediaStatus status) {
            if (generation != m_loadGeneration || m_loadingState != LoadingState::Loading)
                return;

            Logger::debug(QString("AudioPlayer::mediaStatusChanged - status: %1")
                              .arg(static_cast<int>(status)));
            if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
                finishLoadSuccess();
            else if (status == QMediaPlayer::InvalidMedia)
                finishLoadFailure(tr("Invalid audio media: %1").arg(m_player->errorString()));
        });

    m_mediaErrorConnection = connect(
        m_player,
        &QMediaPlayer::errorOccurred,
        this,
        [this, generation](QMediaPlayer::Error error, const QString &errorString) {
            if (generation != m_loadGeneration || m_loadingState != LoadingState::Loading)
                return;
            finishLoadFailure(tr("Audio backend error %1: %2")
                                  .arg(static_cast<int>(error))
                                  .arg(errorString));
        });

    Logger::debug(QString("AudioPlayer::load - Source URL: %1").arg(url.toString()));
    m_player->setSource(url);
    if (m_loadingState == LoadingState::Loading)
        m_loadTimeoutTimer->start(5000);
    return true;
}

QString AudioPlayer::normalizeAudioPath(const QString &originalPath)
{
    bool hasNonAscii = false;
    for (const QChar ch : originalPath)
    {
        if (ch.unicode() > 127)
        {
            hasNonAscii = true;
            break;
        }
    }

    if (!hasNonAscii)
        return originalPath;

    const QString suffix = QFileInfo(originalPath).suffix().trimmed();
    const QString extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    QTemporaryFile tempFile(QDir::tempPath() + QStringLiteral("/audio_XXXXXX") + extension);
    tempFile.setAutoRemove(false);
    if (tempFile.open())
    {
        const QString tempPath = tempFile.fileName();
        tempFile.close();

        // QFile::copy refuses to overwrite the placeholder created by
        // QTemporaryFile, so remove it before copying the real media.
        if (QFile::remove(tempPath) && QFile::copy(originalPath, tempPath))
        {
            Logger::info(QString("AudioPlayer::normalizeAudioPath - Copied to: %1")
                             .arg(tempPath));
            m_tempAudioFiles.append(tempPath);
            return tempPath;
        }

        QFile::remove(tempPath);
    }

    Logger::warn("AudioPlayer::normalizeAudioPath - Temporary copy failed; using original path");
    return originalPath;
}

void AudioPlayer::cleanupTempAudioFiles()
{
    for (const QString &tempPath : m_tempAudioFiles)
    {
        if (tempPath.isEmpty() || !QFile::exists(tempPath))
            continue;
        if (!QFile::remove(tempPath))
        {
            Logger::warn(QString("AudioPlayer::cleanupTempAudioFiles - Failed to remove: %1")
                             .arg(tempPath));
        }
    }
    m_tempAudioFiles.clear();
}

void AudioPlayer::play()
{
    m_player->play();
}

void AudioPlayer::pause()
{
    m_player->pause();
}

void AudioPlayer::stop()
{
    m_player->stop();
}

void AudioPlayer::setPosition(qint64 positionMs)
{
    m_player->setPosition(positionMs);
}

qint64 AudioPlayer::position() const
{
    return m_player->position();
}

qint64 AudioPlayer::duration() const
{
    return m_player->duration();
}

void AudioPlayer::setSpeed(double speed)
{
    m_player->setPlaybackRate(speed);
}

double AudioPlayer::speed() const
{
    return m_player->playbackRate();
}

bool AudioPlayer::isPlaying() const
{
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

bool AudioPlayer::isPaused() const
{
    return m_player->playbackState() == QMediaPlayer::PausedState;
}

bool AudioPlayer::isLoaded() const
{
    return m_loaded;
}

bool AudioPlayer::canPlay() const
{
    return m_loaded && m_player->duration() > 0 && m_player->error() == QMediaPlayer::NoError;
}

QString AudioPlayer::lastError() const
{
    return m_lastError;
}

void AudioPlayer::setAudioLatency(int latency)
{
    m_audioLatency = latency;
}

int AudioPlayer::audioLatency() const
{
    return m_audioLatency;
}

void AudioPlayer::setUserOffset(int offset)
{
    m_userOffset = offset;
}

int AudioPlayer::userOffset() const
{
    return m_userOffset;
}

void AudioPlayer::setAudioCorrectionEnabled(bool enabled)
{
    m_audioCorrectionEnabled = enabled;
}

bool AudioPlayer::audioCorrectionEnabled() const
{
    return m_audioCorrectionEnabled;
}

qint64 AudioPlayer::adjustedPosition() const
{
    if (!m_audioCorrectionEnabled)
        return position();

    // Device latency is wall-clock time. Convert it to media-timeline time;
    // the user/chart offset is already expressed in media-timeline ms.
    const double totalOffset = static_cast<double>(m_userOffset) +
                               PlaybackTiming::wallDurationToMediaMs(m_audioLatency, speed());
    return qRound64(static_cast<double>(position()) + totalOffset);
}

void AudioPlayer::setAdjustedPosition(qint64 adjustedMs)
{
    if (!m_audioCorrectionEnabled)
    {
        setPosition(adjustedMs);
        return;
    }

    const double totalOffset = static_cast<double>(m_userOffset) +
                               PlaybackTiming::wallDurationToMediaMs(m_audioLatency, speed());
    setPosition(qRound64(static_cast<double>(adjustedMs) - totalOffset));
}

#include "AudioPlayer.h"

#include "audio/PlaybackTiming.h"
#include "utils/Logger.h"
#include "utils/PerformanceTimer.h"
#include "utils/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QUrl>
#ifdef _WIN32
#include "WindowsAudioOutput.h"
#include <QAudioDevice>
#include <QFutureWatcher>
#include <QMediaDevices>
#include <QtConcurrentRun>
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#endif

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

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    auto *bufferOutput = new QAudioBufferOutput(this);
    m_player->setAudioBufferOutput(bufferOutput);
    connect(bufferOutput, &QAudioBufferOutput::audioBufferReceived, this,
            [this](const QAudioBuffer &buffer) {
        if (buffer.isValid())
            m_positionGranularityMs = qMax(m_positionGranularityMs, buffer.duration() / 1000.0);
    });
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    // Keep the default file-playback intent. LowLatencyStreaming enables
    // FFmpeg's nobuffer flag, which discards packets consumed by stream probing
    // (the first 4266 ms of a 48 kHz stereo PCM WAV on Qt 6.10.2). A local chart
    // must preserve its complete media timeline, including playback from zero.

    // Pitch compensation adds a large processing queue at very low rates.
    if (m_player->pitchCompensationAvailability() ==
        QMediaPlayer::PitchCompensationAvailability::Available)
    {
        m_player->setPitchCompensation(false);
    }
#endif

    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 positionMs) {
        if (!hasOutputClock())
            emit positionChanged(positionMs);
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, &AudioPlayer::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (!hasOutputClock())
            emit stateChanged(state);
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &message) {
        if (!hasOutputClock() && m_loaded && error != QMediaPlayer::NoError)
        {
            m_lastError = message;
            emit errorOccurred(message);
        }
    });

#ifdef _WIN32
    m_nativePoll.setTimerType(Qt::PreciseTimer);
    m_nativePoll.setInterval(10);
    connect(&m_nativePoll, &QTimer::timeout, this, &AudioPlayer::pollNativeOutput);
    auto *devices = new QMediaDevices(this);
    connect(devices, &QMediaDevices::audioOutputsChanged, this, [this] {
        const auto deviceId = QMediaDevices::defaultAudioOutput().id();
        if (m_nativeOutput && deviceId != m_nativeDeviceId)
        {
            m_nativeDeviceId = deviceId;
            requestNativePosition(m_nativeOutput->positionMs(), true);
        }
    });
#endif
    m_loadTimeoutTimer->setSingleShot(true);
    connect(m_loadTimeoutTimer, &QTimer::timeout, this, [this]() {
        if (m_loadingState != LoadingState::Loading)
            return;
#ifdef _WIN32
        if (m_nativePreparation &&
            (m_player->mediaStatus() == QMediaPlayer::LoadedMedia ||
             m_player->mediaStatus() == QMediaPlayer::BufferedMedia))
        {
            m_nativePreparation->store(true);
            m_nativePreparation.reset();
            finishLoadSuccess();
            return;
        }
#endif
        finishLoadFailure(tr("Audio loading timed out after 5 seconds."));
    });
}

AudioPlayer::~AudioPlayer()
{
    cancelPendingLoad();
#ifdef _WIN32
    m_nativeOutput.reset();
#endif
    // Release the backend's open media handle before removing temporary audio.
    disconnect(m_player, nullptr, this, nullptr);
    delete m_player;
    m_player = nullptr;
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
#ifdef _WIN32
    if (m_nativePreparation)
        m_nativePreparation->store(true);
    m_nativePreparation.reset();
#endif
}

void AudioPlayer::finishLoadFailure(const QString &message)
{
    cancelPendingLoad();
#ifdef _WIN32
    m_nativeOutput.reset();
#endif
    m_lastError = message;
    Logger::error(QString("AudioPlayer::load - %1").arg(message));
    setLoadingState(LoadingState::Error);
    emit errorOccurred(message);
}

void AudioPlayer::finishLoadSuccess()
{
#ifdef _WIN32
    if (m_nativePreparation)
        return;
#endif
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
    m_positionGranularityMs = 0.0;
    stop();
#ifdef _WIN32
    m_nativeOutput.reset();
#endif
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
#ifdef _WIN32
    // Keep the original player available for unsupported sources or devices.
    if (!qEnvironmentVariableIntValue("CCE_LEGACY_AUDIO"))
        prepareNativeOutput(fileInfo.absoluteFilePath(), generation);
#endif
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
    QString copyError;
    if (tempFile.open())
    {
        const QString tempPath = tempFile.fileName();
        tempFile.close();

        // close() keeps QTemporaryFile's native handle alive for reopening.
        // Remove through its owner so Windows releases that handle before copy.
        QFile source(originalPath);
        if (tempFile.remove() && source.copy(tempPath))
        {
            Logger::info(QString("AudioPlayer::normalizeAudioPath - Copied to: %1")
                             .arg(tempPath));
            m_tempAudioFiles.append(tempPath);
            return tempPath;
        }

        copyError = tempFile.error() != QFileDevice::NoError ? tempFile.errorString() : source.errorString();
        tempFile.remove();
    }
    else
        copyError = tempFile.errorString();

    Logger::warn(QString("AudioPlayer::normalizeAudioPath - Temporary copy failed for '%1': %2; using original path")
                     .arg(originalPath, copyError));
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
#ifdef _WIN32
    if (m_nativeOutput)
    {
        if (m_nativeState == QMediaPlayer::PlayingState)
            return;
        m_nativeState = QMediaPlayer::PlayingState;
        requestNativePosition(m_nativeOutput->positionMs());
        m_nativePoll.start();
        emit stateChanged(m_nativeState);
        return;
    }
#endif
    m_player->play();
}

void AudioPlayer::pause()
{
#ifdef _WIN32
    if (m_nativeOutput)
    {
        if (m_nativeState != QMediaPlayer::PlayingState)
            return;
        m_nativeState = QMediaPlayer::PausedState;
        m_nativePoll.stop();
        requestNativePosition(m_nativeOutput->positionMs());
        emit stateChanged(m_nativeState);
        return;
    }
#endif
    m_player->pause();
}

void AudioPlayer::stop()
{
#ifdef _WIN32
    if (m_nativeOutput)
    {
        if (m_nativeState == QMediaPlayer::StoppedState)
            return;
        m_nativeState = QMediaPlayer::StoppedState;
        m_nativePoll.stop();
        requestNativePosition(0);
        emit stateChanged(m_nativeState);
        return;
    }
#endif
    m_player->stop();
}

void AudioPlayer::setPosition(qint64 positionMs)
{
#ifdef _WIN32
    if (m_nativeOutput)
    {
        requestNativePosition(positionMs);
        emit positionChanged(position());
        return;
    }
#endif
    m_player->setPosition(positionMs);
}

qint64 AudioPlayer::position() const
{
#ifdef _WIN32
    if (m_nativeOutput)
        return qRound64(m_nativeOutput->positionMs());
#endif
    return m_player->position();
}

qint64 AudioPlayer::duration() const
{
    return m_player->duration();
}

void AudioPlayer::setSpeed(double speed)
{
#ifdef _WIN32
    const double nativePosition = m_nativeOutput ? m_nativeOutput->positionMs() : 0.0;
#endif
    m_player->setPlaybackRate(speed);
#ifdef _WIN32
    if (m_nativeOutput)
        requestNativePosition(nativePosition);
#endif
}

void AudioPlayer::setMuted(bool muted)
{
    m_audioOutput->setMuted(muted);
#ifdef _WIN32
    if (m_nativeOutput)
        m_nativeOutput->setMuted(muted);
#endif
}

bool AudioPlayer::isMuted() const
{
    return m_audioOutput->isMuted();
}

double AudioPlayer::speed() const
{
    return m_player->playbackRate();
}

bool AudioPlayer::isPlaying() const
{
#ifdef _WIN32
    if (m_nativeOutput)
        return m_nativeState == QMediaPlayer::PlayingState;
#endif
    return m_player->playbackState() == QMediaPlayer::PlayingState;
}

bool AudioPlayer::isPaused() const
{
#ifdef _WIN32
    if (m_nativeOutput)
        return m_nativeState == QMediaPlayer::PausedState;
#endif
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
    return qRound64(outputTime());
}

void AudioPlayer::setAdjustedPosition(qint64 adjustedMs)
{
    setPosition(qRound64(static_cast<double>(adjustedMs) - correctionMs()));
}

double AudioPlayer::correctionMs() const
{
    if (!m_audioCorrectionEnabled)
        return 0;
    return m_userOffset + PlaybackTiming::wallDurationToMediaMs(m_audioLatency, speed());
}

bool AudioPlayer::hasOutputClock() const
{
#ifdef _WIN32
    return bool(m_nativeOutput);
#else
    return false;
#endif
}

double AudioPlayer::outputTime(qint64 aheadNs) const
{
#ifdef _WIN32
    if (m_nativeOutput)
        return m_nativeOutput->positionMs(aheadNs) + correctionMs();
#else
    Q_UNUSED(aheadNs);
#endif
    return m_player->position() + correctionMs();
}

void AudioPlayer::setPlaybackLimit(double adjustedEndMs)
{
#ifdef _WIN32
    if (m_playbackLimitMs == adjustedEndMs)
        return;
    m_playbackLimitMs = adjustedEndMs;
    if (m_nativeOutput && m_nativeState == QMediaPlayer::PlayingState)
        requestNativePosition(m_nativeOutput->positionMs());
#else
    Q_UNUSED(adjustedEndMs);
#endif
}

#ifdef _WIN32
void AudioPlayer::prepareNativeOutput(const QString &path, quint64 generation)
{
    const auto cancel = m_nativePreparation = std::make_shared<std::atomic_bool>(false);
    using Result = std::shared_ptr<const WindowsAudioOutput::Pcm>;
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation, cancel] {
        watcher->deleteLater();
        if (cancel->load() || generation != m_loadGeneration || m_loadingState != LoadingState::Loading)
            return;
        m_nativePreparation.reset();
        const auto pcm = watcher->result();
        if (pcm)
        {
            m_nativeOutput = std::make_unique<WindowsAudioOutput>(pcm);
            m_nativeOutput->setMuted(isMuted());
            m_nativeDeviceId = QMediaDevices::defaultAudioOutput().id();
        }
        if (m_player->mediaStatus() == QMediaPlayer::LoadedMedia ||
            m_player->mediaStatus() == QMediaPlayer::BufferedMedia)
            finishLoadSuccess();
    });
    watcher->setFuture(QtConcurrent::run([path, cancel] {
        return WindowsAudioOutput::decode(path, *cancel);
    }));
}

void AudioPlayer::requestNativePosition(double positionMs, bool reopenDevice)
{
    const double endMs = m_playbackLimitMs >= 0 ? std::max(0.0, m_playbackLimitMs - correctionMs()) : -1.0;
    m_nativeOutput->setPlayback(positionMs, endMs, speed(),
                                m_nativeState == QMediaPlayer::PlayingState, reopenDevice);
}

void AudioPlayer::pollNativeOutput()
{
    const auto status = m_nativeOutput->status();
    if (status == WindowsAudioOutput::Status::Failed)
    {
        const auto resumeMs = position();
        m_nativePoll.stop();
        m_nativeOutput.reset();
        m_nativeState = QMediaPlayer::StoppedState;
        Logger::warn("AudioPlayer - Windows output clock unavailable, resuming with Qt playback");
        m_player->setPosition(resumeMs);
        m_player->play();
    }
    else if (status == WindowsAudioOutput::Status::Ended)
    {
        if (m_playbackLimitMs >= 0)
        {
            emit playbackLimitReached();
            if (!m_nativeOutput || m_nativeOutput->status() != WindowsAudioOutput::Status::Ended)
                return;
        }
        m_nativePoll.stop();
        m_nativeState = QMediaPlayer::StoppedState;
        emit stateChanged(m_nativeState);
    }
    else if (status == WindowsAudioOutput::Status::Playing)
        emit positionChanged(position());
}
#endif

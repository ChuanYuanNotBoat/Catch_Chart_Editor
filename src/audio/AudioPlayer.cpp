#include "AudioPlayer.h"
#include "audio/PlaybackTiming.h"
#include "utils/Logger.h"
#include "utils/PerformanceTimer.h"
#include "utils/Settings.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QPlaybackOptions>
#endif
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QTemporaryFile>
#include <QDir>

AudioPlayer::AudioPlayer(QObject *parent) : QObject(parent),
                                            m_loaded(false),
                                            m_lastError(),
                                            m_loadingState(LoadingState::Idle),
                                            m_loadTimeoutTimer(nullptr),
                                            m_currentLoadPath(),
                                            m_audioLatency(0),
                                            m_userOffset(0),
                                            m_audioCorrectionEnabled(true)
{
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    // The editor needs responsive local scrubbing and rate changes more than a
    // large, glitch-resistant media buffer.  These are backend hints and are
    // safely ignored by backends that do not support them.
    QPlaybackOptions playbackOptions = m_player->playbackOptions();
    playbackOptions.setPlaybackIntent(QPlaybackOptions::PlaybackIntent::LowLatencyStreaming);
    m_player->setPlaybackOptions(playbackOptions);

    // Pitch compensation adds a sizeable processing queue at very low rates.
    // Editor playback favors timing accuracy; the corresponding pitch change
    // also makes the selected playback rate audible to the mapper.
    if (m_player->pitchCompensationAvailability() ==
        QMediaPlayer::PitchCompensationAvailability::Available)
    {
        m_player->setPitchCompensation(false);
    }
#endif

    connect(m_player, &QMediaPlayer::positionChanged, this, &AudioPlayer::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &AudioPlayer::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &AudioPlayer::stateChanged);

    // 创建超时定时器
    m_loadTimeoutTimer = new QTimer(this);
    m_loadTimeoutTimer->setSingleShot(true);
    connect(m_loadTimeoutTimer, &QTimer::timeout, this, [this]()
            {
        if (m_loadingState == LoadingState::Loading) {
            m_lastError = QString("音频加载超时 (5秒)");
            Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
            // 断开信号连接
            disconnect(m_player, &QMediaPlayer::mediaStatusChanged, this, nullptr);
            disconnect(m_player, &QMediaPlayer::errorOccurred, this, nullptr);
            setLoadingState(LoadingState::Error);
            emit errorOccurred(m_lastError);
        } });

    // 从设置加载音频延迟和全局偏移
    m_audioLatency = Settings::instance().audioLatency();
    m_userOffset = Settings::instance().globalAudioOffset();
    m_audioCorrectionEnabled = Settings::instance().audioCorrectionEnabled();
}

AudioPlayer::~AudioPlayer()
{
    cleanupTempAudioFiles();
}

AudioPlayer::LoadingState AudioPlayer::loadingState() const
{
    return m_loadingState;
}

void AudioPlayer::setLoadingState(LoadingState state)
{
    if (m_loadingState != state)
    {
        m_loadingState = state;
        emit loadingStateChanged(state);
        // 同步更新 m_loaded 状态
        m_loaded = (state == LoadingState::Loaded);
    }
}

bool AudioPlayer::load(const QString &filePath)
{
    PerformanceTimer loadTimer("AudioPlayer::load", "audio");

    Logger::info(QString("AudioPlayer::load - Loading audio from: %1").arg(filePath));

    // 重置状态
    setLoadingState(LoadingState::Idle);
    m_lastError.clear();
    m_currentLoadPath.clear();
    m_player->stop();
    m_player->setSource(QUrl());
    cleanupTempAudioFiles();

    // 检查文件是否存在
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists())
    {
        m_lastError = QString("文件不存在: %1").arg(filePath);
        Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
        emit errorOccurred(m_lastError);
        return false;
    }
    if (!fileInfo.isReadable())
    {
        m_lastError = QString("文件不可读: %1").arg(filePath);
        Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
        emit errorOccurred(m_lastError);
        return false;
    }

    Logger::debug(QString("AudioPlayer::load - 文件大小: %1 字节, 扩展名: %2").arg(fileInfo.size()).arg(fileInfo.suffix()));

    // 路径规范化：如果路径包含非ASCII字符，复制到临时文件
    QString actualPath = normalizeAudioPath(filePath);
    if (actualPath.isEmpty())
    {
        m_lastError = QString("路径规范化失败");
        Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
        emit errorOccurred(m_lastError);
        return false;
    }

    Logger::debug(QString("AudioPlayer::load - 实际加载路径: %1").arg(actualPath));

    try
    {
        QUrl url = QUrl::fromLocalFile(actualPath);
        Logger::debug(QString("AudioPlayer::load - URL: %1").arg(url.toString()));

        // 停止任何正在进行的加载
        if (m_loadingState == LoadingState::Loading)
        {
            Logger::debug("AudioPlayer::load - 取消之前的加载");
            m_loadTimeoutTimer->stop();
            // 断开之前的连接
            disconnect(m_player, &QMediaPlayer::mediaStatusChanged, this, nullptr);
            disconnect(m_player, &QMediaPlayer::errorOccurred, this, nullptr);
        }

        // 设置加载状态
        setLoadingState(LoadingState::Loading);
        m_currentLoadPath = filePath;

        // 连接媒体状态变化信号
        connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status)
                {
            Logger::debug(QString("AudioPlayer::mediaStatusChanged - MediaStatus: %1").arg(static_cast<int>(status)));
            if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
                // 加载成功
                m_loadTimeoutTimer->stop();
                // 验证音频时长
                qint64 duration = m_player->duration();
                if (duration <= 0) {
                    m_lastError = QString("音频时长无效（可能格式不支持），错误信息: %1").arg(m_player->errorString());
                    Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
                    setLoadingState(LoadingState::Error);
                    emit errorOccurred(m_lastError);
                } else {
                    Logger::info(QString("AudioPlayer::load - 音频加载成功，时长: %1 ms").arg(duration));
                    setLoadingState(LoadingState::Loaded);
                }
                // 断开信号连接
                disconnect(m_player, &QMediaPlayer::mediaStatusChanged, this, nullptr);
                disconnect(m_player, &QMediaPlayer::errorOccurred, this, nullptr);
            } else if (status == QMediaPlayer::InvalidMedia) {
                // 媒体无效
                m_loadTimeoutTimer->stop();
                m_lastError = QString("媒体无效，错误信息: %1").arg(m_player->errorString());
                Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
                setLoadingState(LoadingState::Error);
                emit errorOccurred(m_lastError);
                disconnect(m_player, &QMediaPlayer::mediaStatusChanged, this, nullptr);
                disconnect(m_player, &QMediaPlayer::errorOccurred, this, nullptr);
            } });

        // 连接错误信号
        connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString)
                {
            Logger::error(QString("AudioPlayer::load - 媒体错误: %1, %2").arg(int(error)).arg(errorString));
            m_loadTimeoutTimer->stop();
            m_lastError = QString("媒体错误 %1: %2").arg(int(error)).arg(errorString);
            setLoadingState(LoadingState::Error);
            emit errorOccurred(m_lastError);
            disconnect(m_player, &QMediaPlayer::mediaStatusChanged, this, nullptr);
            disconnect(m_player, &QMediaPlayer::errorOccurred, this, nullptr); });

        // 开始加载
        m_player->setSource(url);

        // 启动超时定时器
        m_loadTimeoutTimer->start(5000); // 5秒超时

        Logger::debug("AudioPlayer::load - 异步加载已启动");
        return true; // 表示加载已开始
    }
    catch (const std::exception &e)
    {
        m_lastError = QString("异常: %1").arg(e.what());
        Logger::error(QString("AudioPlayer::load - %1").arg(m_lastError));
        emit errorOccurred(m_lastError);
        setLoadingState(LoadingState::Error);
        return false;
    }
    catch (...)
    {
        m_lastError = QString("未知异常");
        Logger::error("AudioPlayer::load - Unknown exception");
        emit errorOccurred(m_lastError);
        setLoadingState(LoadingState::Error);
        return false;
    }
}

QString AudioPlayer::normalizeAudioPath(const QString &originalPath)
{
    // 检查路径是否包含非ASCII字符
    bool hasNonAscii = false;
    for (const QChar &ch : originalPath)
    {
        if (ch.unicode() > 127)
        {
            hasNonAscii = true;
            break;
        }
    }

    if (!hasNonAscii)
    {
        return originalPath;
    }

    // 复制到临时文件
    QTemporaryFile tempFile(QDir::tempPath() + "/audio_XXXXXX.ogg");
    tempFile.setAutoRemove(false); // 程序运行期间保留
    if (tempFile.open())
    {
        QString tempPath = tempFile.fileName();
        tempFile.close();

        if (QFile::copy(originalPath, tempPath))
        {
            Logger::info(QString("AudioPlayer::normalizeAudioPath - Audio file copied to temporary path: %1").arg(tempPath));
            m_tempAudioFiles.append(tempPath); // 记录以便清理
            return tempPath;
        }
    }

    Logger::warn(QString("AudioPlayer::normalizeAudioPath - Failed to copy audio file to temp path, using original"));
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
            Logger::warn(QString("AudioPlayer::cleanupTempAudioFiles - Failed to remove temporary file: %1").arg(tempPath));
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
    const qint64 pos = position();
    // Device latency is wall-clock time. Convert it to media-timeline time so
    // a 50 ms device delay does not become a 500 ms audible offset at 0.1x.
    // The user/chart offset is already expressed in media-timeline ms.
    const double totalOffset = static_cast<double>(m_userOffset) +
                               PlaybackTiming::wallDurationToMediaMs(m_audioLatency, speed());
    // 调整位置：实际听到的时间 = 音频位置 + 总偏移量
    // 注意：偏移量符号需要根据延迟定义确定
    // 假设正偏移量表示音频延迟（需要提前播放），因此调整后位置 = pos + totalOffset
    return qRound64(static_cast<double>(pos) + totalOffset);
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
    // 调整位置：音频位置 = 调整后位置 - 总偏移量
    const qint64 pos = qRound64(static_cast<double>(adjustedMs) - totalOffset);
    setPosition(pos);
}

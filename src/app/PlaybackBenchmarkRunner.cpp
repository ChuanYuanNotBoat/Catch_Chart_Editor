#include "PlaybackBenchmarkRunner.h"

#include "Application.h"
#include "MainWindow.h"
#include "audio/AudioPlayer.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/Chart.h"
#include "utils/Logger.h"
#include "utils/MathUtils.h"
#include "utils/PlaybackStutterProbe.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr auto kChartOption = "benchmark-chart";
    constexpr auto kFpsOption = "benchmark-fps";
    constexpr auto kDurationOption = "benchmark-duration-ms";
    constexpr auto kWarmupOption = "benchmark-warmup-ms";
    constexpr auto kStartOption = "benchmark-start-ms";
    constexpr auto kSpeedOption = "benchmark-speed";
    constexpr auto kOutputOption = "benchmark-output";

    bool parseInteger(const QString &text, qint64 minimum, qint64 maximum,
                      qint64 *result, const QString &optionName, QString *errorMessage)
    {
        bool ok = false;
        const qint64 value = text.toLongLong(&ok);
        if (!ok || value < minimum || value > maximum)
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("--%1 must be between %2 and %3.")
                                    .arg(optionName)
                                    .arg(minimum)
                                    .arg(maximum);
            }
            return false;
        }
        *result = value;
        return true;
    }
}

PlaybackBenchmarkRunner::PlaybackBenchmarkRunner(Application *application,
                                                 PlaybackBenchmarkOptions options,
                                                 QObject *parent)
    : QObject(parent),
      m_application(application),
      m_options(std::move(options)),
      m_phaseTimer(new QTimer(this)),
      m_watchdogTimer(new QTimer(this))
{
    m_phaseTimer->setSingleShot(true);
    m_phaseTimer->setTimerType(Qt::PreciseTimer);
    m_watchdogTimer->setSingleShot(true);
    connect(m_watchdogTimer, &QTimer::timeout, this, [this]()
            { fail(QStringLiteral("Benchmark timed out."), 5); });
}

void PlaybackBenchmarkRunner::addCommandLineOptions(QCommandLineParser &parser)
{
    parser.addOption(QCommandLineOption(
        kChartOption,
        QStringLiteral("Run an automated playback benchmark using an extracted .mc chart."),
        QStringLiteral("path")));
    parser.addOption(QCommandLineOption(
        kFpsOption,
        QStringLiteral("Playback frame-rate cap: 0, 60, 90, or 120."),
        QStringLiteral("fps"),
        QStringLiteral("120")));
    parser.addOption(QCommandLineOption(
        kDurationOption,
        QStringLiteral("Measured wall-clock duration in milliseconds."),
        QStringLiteral("ms"),
        QStringLiteral("15000")));
    parser.addOption(QCommandLineOption(
        kWarmupOption,
        QStringLiteral("Unmeasured playback warm-up in milliseconds."),
        QStringLiteral("ms"),
        QStringLiteral("3000")));
    parser.addOption(QCommandLineOption(
        kStartOption,
        QStringLiteral("Measurement start on the media timeline. Omit to select the densest chart section."),
        QStringLiteral("ms")));
    parser.addOption(QCommandLineOption(
        kSpeedOption,
        QStringLiteral("Playback speed used by the benchmark."),
        QStringLiteral("factor"),
        QStringLiteral("1.0")));
    parser.addOption(QCommandLineOption(
        kOutputOption,
        QStringLiteral("JSON report path. Defaults to artifacts/playback_benchmark_*.json."),
        QStringLiteral("path")));
}

bool PlaybackBenchmarkRunner::isRequested(const QCommandLineParser &parser)
{
    return parser.isSet(kChartOption);
}

bool PlaybackBenchmarkRunner::optionsFromParser(const QCommandLineParser &parser,
                                                PlaybackBenchmarkOptions *options,
                                                QString *errorMessage)
{
    if (!options)
        return false;
    if (errorMessage)
        errorMessage->clear();

    PlaybackBenchmarkOptions parsed;
    parsed.chartPath = QFileInfo(parser.value(kChartOption)).absoluteFilePath();
    parsed.outputPath = parser.value(kOutputOption).trimmed();

    qint64 value = 0;
    if (!parseInteger(parser.value(kFpsOption), 0, 120, &value, kFpsOption, errorMessage))
        return false;
    if (value != 0 && value != 60 && value != 90 && value != 120)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("--benchmark-fps must be 0, 60, 90, or 120.");
        return false;
    }
    parsed.frameRate = static_cast<int>(value);

    if (!parseInteger(parser.value(kDurationOption), 1000, 300000, &value, kDurationOption, errorMessage))
        return false;
    parsed.durationMs = static_cast<int>(value);
    if (!parseInteger(parser.value(kWarmupOption), 0, 60000, &value, kWarmupOption, errorMessage))
        return false;
    parsed.warmupMs = static_cast<int>(value);

    if (parser.isSet(kStartOption))
    {
        if (!parseInteger(parser.value(kStartOption), 0, 24LL * 60LL * 60LL * 1000LL,
                          &value, kStartOption, errorMessage))
            return false;
        parsed.startMs = value;
    }

    bool speedOk = false;
    parsed.speed = parser.value(kSpeedOption).toDouble(&speedOk);
    if (!speedOk || !std::isfinite(parsed.speed) || parsed.speed < 0.1 || parsed.speed > 10.0)
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("--benchmark-speed must be between 0.1 and 10.0.");
        return false;
    }
    if (!QFileInfo(parsed.chartPath).isFile())
    {
        if (errorMessage)
            *errorMessage = QStringLiteral("Benchmark chart does not exist: %1").arg(parsed.chartPath);
        return false;
    }

    *options = parsed;
    return true;
}

void PlaybackBenchmarkRunner::start()
{
    if (!m_application || !m_application->mainWindow() || !m_application->playbackController())
    {
        fail(QStringLiteral("Application playback components are unavailable."));
        return;
    }

    PlaybackStutterProbe::setProcessOverrideEnabled(true);
    m_watchdogTimer->start(qMax(30000, m_options.durationMs + m_options.warmupMs + 20000));

    MainWindow *window = m_application->mainWindow();
    window->showMaximized();
    window->raise();
    window->activateWindow();
    QCoreApplication::processEvents();

    QString loadError;
    if (!window->loadChartForBenchmark(m_options.chartPath, &loadError))
    {
        fail(loadError.isEmpty() ? QStringLiteral("Failed to load benchmark chart.") : loadError);
        return;
    }
    waitForAudio();
}

void PlaybackBenchmarkRunner::waitForAudio()
{
    AudioPlayer *audio = m_application->playbackController()->audioPlayer();
    if (!audio)
    {
        fail(QStringLiteral("Audio player is unavailable."));
        return;
    }
    if (audio->loadingState() == AudioPlayer::LoadingState::Loaded && audio->canPlay())
    {
        QTimer::singleShot(500, this, &PlaybackBenchmarkRunner::beginWarmup);
        return;
    }
    if (audio->loadingState() == AudioPlayer::LoadingState::Error)
    {
        fail(QStringLiteral("Audio failed to load: %1").arg(audio->lastError()));
        return;
    }

    connect(audio, &AudioPlayer::loadingStateChanged, this,
            [this, audio](AudioPlayer::LoadingState state)
            {
                if (m_finished)
                    return;
                if (state == AudioPlayer::LoadingState::Loaded && audio->canPlay())
                    QTimer::singleShot(500, this, &PlaybackBenchmarkRunner::beginWarmup);
                else if (state == AudioPlayer::LoadingState::Error)
                    fail(QStringLiteral("Audio failed to load: %1").arg(audio->lastError()));
            },
            Qt::SingleShotConnection);
}

qint64 PlaybackBenchmarkRunner::chooseDenseMeasurementStartMs() const
{
    const Chart *chart = m_application->chartController()->chart();
    const AudioPlayer *audio = m_application->playbackController()->audioPlayer();
    if (!chart || chart->notes().isEmpty())
        return 0;

    const auto bpmCache = MathUtils::buildBpmTimeCache(chart->bpmList(), chart->meta().offset);
    QVector<double> noteTimes;
    noteTimes.reserve(chart->notes().size());
    for (const Note &note : chart->notes())
    {
        noteTimes.append(MathUtils::beatToMs(note.beatNum, note.numerator,
                                             qMax(1, note.denominator), bpmCache));
    }
    std::sort(noteTimes.begin(), noteTimes.end());

    const double mediaWindowMs = static_cast<double>(m_options.durationMs) * m_options.speed;
    int bestLeft = 0;
    int bestCount = 0;
    int right = 0;
    for (int left = 0; left < noteTimes.size(); ++left)
    {
        right = qMax(right, left);
        while (right < noteTimes.size() && noteTimes[right] < noteTimes[left] + mediaWindowMs)
            ++right;
        const int count = right - left;
        if (count > bestCount)
        {
            bestCount = count;
            bestLeft = left;
        }
    }

    const qint64 warmupMediaMs = qRound64(static_cast<double>(m_options.warmupMs) * m_options.speed);
    qint64 result = qMax<qint64>(warmupMediaMs, qRound64(noteTimes.value(bestLeft)));
    const qint64 latestStart = audio && audio->duration() > 0
                                   ? qMax<qint64>(0, audio->duration() - qRound64(mediaWindowMs) - 250)
                                   : result;
    return qBound<qint64>(0, result, latestStart);
}

void PlaybackBenchmarkRunner::beginWarmup()
{
    if (m_finished)
        return;

    PlaybackController *controller = m_application->playbackController();
    controller->setFrameRateCap(m_options.frameRate);
    controller->setSpeed(m_options.speed);

    m_measurementStartMs = m_options.startMs >= 0 ? m_options.startMs : chooseDenseMeasurementStartMs();
    const qint64 warmupMediaMs = qRound64(static_cast<double>(m_options.warmupMs) * m_options.speed);
    m_playbackStartMs = qMax<qint64>(0, m_measurementStartMs - warmupMediaMs);
    controller->playFromTime(static_cast<double>(m_playbackStartMs));
    if (controller->state() != PlaybackController::Playing)
    {
        fail(QStringLiteral("Audio playback did not start."));
        return;
    }

    QTextStream(stdout) << "BENCHMARK_WARMUP chart=\"" << m_options.chartPath
                        << "\" fps=" << m_options.frameRate
                        << " start_ms=" << m_playbackStartMs
                        << " measurement_start_ms=" << m_measurementStartMs << Qt::endl;
    connect(m_phaseTimer, &QTimer::timeout, this, &PlaybackBenchmarkRunner::beginMeasurement,
            Qt::SingleShotConnection);
    m_phaseTimer->start(m_options.warmupMs);
}

void PlaybackBenchmarkRunner::beginMeasurement()
{
    if (m_finished)
        return;

    PlaybackController *controller = m_application->playbackController();
    AudioPlayer *audio = controller->audioPlayer();
    MainWindow *window = m_application->mainWindow();
    QScreen *screen = window->windowHandle() ? window->windowHandle()->screen() : QGuiApplication::primaryScreen();
    const Chart *chart = m_application->chartController()->chart();
    m_audioPositionAtMeasurementStart = audio->adjustedPosition();

    QJsonObject metadata;
    metadata.insert("application_version", QCoreApplication::applicationVersion());
    metadata.insert("qt_version", QString::fromLatin1(qVersion()));
    metadata.insert("os", QSysInfo::prettyProductName());
    metadata.insert("cpu_architecture", QSysInfo::currentCpuArchitecture());
    metadata.insert("chart_path", m_options.chartPath);
    metadata.insert("chart_title", chart ? chart->meta().title : QString());
    metadata.insert("note_count", chart ? chart->notes().size() : 0);
    metadata.insert("fps_cap", m_options.frameRate);
    metadata.insert("effective_fps", controller->effectiveFrameRate());
    metadata.insert("display_refresh_hz", screen ? screen->refreshRate() : 0.0);
    metadata.insert("playback_speed", m_options.speed);
    metadata.insert("warmup_ms", m_options.warmupMs);
    metadata.insert("requested_duration_ms", m_options.durationMs);
    metadata.insert("requested_measurement_start_ms", static_cast<double>(m_measurementStartMs));
    metadata.insert("actual_measurement_start_ms", static_cast<double>(m_audioPositionAtMeasurementStart));
    metadata.insert("window_width", window->width());
    metadata.insert("window_height", window->height());

    PlaybackStutterProbe::beginSession(QStringLiteral("playback_benchmark"), metadata);
    QTextStream(stdout) << "BENCHMARK_BEGIN fps=" << m_options.frameRate
                        << " duration_ms=" << m_options.durationMs
                        << " audio_ms=" << m_audioPositionAtMeasurementStart << Qt::endl;

    connect(m_phaseTimer, &QTimer::timeout, this, &PlaybackBenchmarkRunner::finishMeasurement,
            Qt::SingleShotConnection);
    m_phaseTimer->start(m_options.durationMs);
}

QString PlaybackBenchmarkRunner::resolvedOutputPath() const
{
    if (!m_options.outputPath.isEmpty())
        return QFileInfo(m_options.outputPath).absoluteFilePath();

    const QString stem = QFileInfo(m_options.chartPath).completeBaseName();
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    return QDir::current().absoluteFilePath(
        QStringLiteral("artifacts/playback_benchmark_%1_%2fps_%3.json")
            .arg(stem)
            .arg(m_options.frameRate)
            .arg(stamp));
}

void PlaybackBenchmarkRunner::finishMeasurement()
{
    if (m_finished)
        return;

    PlaybackController *controller = m_application->playbackController();
    const qint64 finalAudioPositionMs = controller->audioPlayer()->adjustedPosition();
    controller->pause();
    PlaybackStutterProbe::forceFlush();
    QJsonObject report = PlaybackStutterProbe::endSession();
    QJsonObject metadata = report.value("metadata").toObject();
    metadata.insert("final_audio_position_ms", static_cast<double>(finalAudioPositionMs));
    metadata.insert("measured_audio_advance_ms",
                    static_cast<double>(finalAudioPositionMs - m_audioPositionAtMeasurementStart));
    report.insert("metadata", metadata);

    const QString outputPath = resolvedOutputPath();
    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath()))
    {
        fail(QStringLiteral("Could not create benchmark output directory: %1")
                 .arg(outputInfo.absolutePath()),
             6);
        return;
    }

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0 ||
        !output.commit())
    {
        fail(QStringLiteral("Could not write benchmark report: %1").arg(outputPath), 6);
        return;
    }

    const QJsonObject summary = report.value("summary").toObject();
    Logger::info(QStringLiteral("BENCHMARK_END output=%1 fps_tick=%2 fps_canvas=%3 paint_p95_ms=%4 pulse_p95_ms=%5")
                     .arg(outputPath)
                     .arg(summary.value("fps_tick").toDouble(), 0, 'f', 2)
                     .arg(summary.value("fps_canvas").toDouble(), 0, 'f', 2)
                     .arg(summary.value("paint_time_p95_ms").toDouble(), 0, 'f', 3)
                     .arg(summary.value("pulse_interval_p95_ms").toDouble(), 0, 'f', 3));
    QTextStream(stdout) << "BENCHMARK_END output=\"" << outputPath
                        << "\" fps_tick=" << QString::number(summary.value("fps_tick").toDouble(), 'f', 2)
                        << " fps_canvas=" << QString::number(summary.value("fps_canvas").toDouble(), 'f', 2)
                        << " paint_p95_ms=" << QString::number(summary.value("paint_time_p95_ms").toDouble(), 'f', 3)
                        << Qt::endl;

    m_finished = true;
    m_watchdogTimer->stop();
    emit finished(0);
}

void PlaybackBenchmarkRunner::fail(const QString &message, int exitCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_phaseTimer->stop();
    m_watchdogTimer->stop();
    if (m_application && m_application->playbackController() &&
        m_application->playbackController()->state() == PlaybackController::Playing)
    {
        m_application->playbackController()->pause();
    }
    if (PlaybackStutterProbe::sessionActive())
        PlaybackStutterProbe::endSession();
    Logger::error(QStringLiteral("BENCHMARK_ERROR %1").arg(message));
    QTextStream(stderr) << "BENCHMARK_ERROR " << message << Qt::endl;
    emit finished(exitCode);
}

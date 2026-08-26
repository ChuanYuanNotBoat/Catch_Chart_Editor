#pragma once

#include <QObject>
#include <QString>

class Application;
class AudioPlayer;
class QCommandLineParser;
class QTimer;

struct PlaybackBenchmarkOptions
{
    QString chartPath;
    QString outputPath;
    int frameRate = 120;
    int durationMs = 15000;
    int warmupMs = 3000;
    qint64 startMs = -1;
    double speed = 1.0;
};

class PlaybackBenchmarkRunner : public QObject
{
    Q_OBJECT
public:
    explicit PlaybackBenchmarkRunner(Application *application,
                                     PlaybackBenchmarkOptions options,
                                     QObject *parent = nullptr);

    static void addCommandLineOptions(QCommandLineParser &parser);
    static bool isRequested(const QCommandLineParser &parser);
    static bool optionsFromParser(const QCommandLineParser &parser,
                                  PlaybackBenchmarkOptions *options,
                                  QString *errorMessage);

    void start();

signals:
    void finished(int exitCode);

private:
    void waitForAudio();
    void beginWarmup();
    void beginMeasurement();
    void finishMeasurement();
    void fail(const QString &message, int exitCode = 4);
    qint64 chooseDenseMeasurementStartMs() const;
    QString resolvedOutputPath() const;

    Application *m_application;
    PlaybackBenchmarkOptions m_options;
    QTimer *m_phaseTimer;
    QTimer *m_watchdogTimer;
    qint64 m_measurementStartMs = 0;
    qint64 m_playbackStartMs = 0;
    qint64 m_audioPositionAtMeasurementStart = 0;
    bool m_finished = false;
};

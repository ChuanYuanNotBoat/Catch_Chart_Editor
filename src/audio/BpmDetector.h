#pragma once

#include <QString>
#include <QVector>
#include <functional>

class QObject;
class BpmDetector
{
public:
    struct SegmentResult
    {
        double startMs = 0.0;
        double durationMs = 0.0;
        double bpm = 0.0;
        double estimatedOffsetMs = 0.0;
        double score = 0.0;
        bool valid = false;
    };

    struct DetectionResult
    {
        double bpm = 0.0;
        double estimatedOffsetMs = 0.0;
        QVector<SegmentResult> segments;
    };

    // Detect BPM from audio PCM in [startMs, startMs + durationMs].
    // Returns true when a confident estimate is produced.
    static bool detectFromFile(const QString &audioFilePath,
                               double startMs,
                               double durationMs,
                               double &outBpm,
                               QString *outError = nullptr);

    static bool detectFromFileDetailed(const QString &audioFilePath,
                                       double startMs,
                                       double durationMs,
                                       DetectionResult &outResult,
                                       QString *outError = nullptr);

    using AsyncDetectionCallback = std::function<void(bool,
                                                      DetectionResult,
                                                      const QString &)>;
    static void detectFromFileDetailedAsync(QObject *context,
                                            const QString &audioFilePath,
                                            double startMs,
                                            double durationMs,
                                            AsyncDetectionCallback callback);
};

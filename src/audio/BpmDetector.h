#pragma once

#include "AutoTiming2Bridge.h"

#include <QString>
#include <QVector>
#include <atomic>
#include <functional>
#include <memory>

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

        // AutoTiming 2 analysis summary. Filled only by analyzeFromFileDetailed
        // (legacy bpm/estimatedOffsetMs above keep their existing semantics and
        // are always produced by the legacy AutoTiming pipeline).
        bool hasAnalysis = false;
        AutoTiming2Summary analysis;
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

    // Legacy detection + AutoTiming 2 analysis (autotiming::analyze). Legacy
    // fields behave exactly like detectFromFileDetailed; analysis failures do
    // not invalidate the legacy result (outResult.hasAnalysis stays false).
    // Non-32/44.1/48 kHz inputs are linearly resampled to 44100 Hz first.
    static bool analyzeFromFileDetailed(const QString &audioFilePath,
                                        double startMs,
                                        double durationMs,
                                        DetectionResult &outResult,
                                        QString *outError = nullptr,
                                        const AutoTiming2Options &analysisOptions = {});

    using AsyncDetectionCallback = std::function<void(bool,
                                                      DetectionResult,
                                                      const QString &)>;
    static void detectFromFileDetailedAsync(QObject *context,
                                            const QString &audioFilePath,
                                            double startMs,
                                            double durationMs,
                                            AsyncDetectionCallback callback);

    // Asynchronous analyzeFromFileDetailed. cancelFlag (optional) is a
    // task-level cooperative cancel checked before decoding and before the
    // analysis core runs; the vendored analyze() exposes no progress callback,
    // so mid-analysis cancellation is not supported.
    using AsyncAnalysisCallback = AsyncDetectionCallback;
    static void analyzeFromFileDetailedAsync(QObject *context,
                                             const QString &audioFilePath,
                                             double startMs,
                                             double durationMs,
                                             AsyncAnalysisCallback callback,
                                             std::shared_ptr<std::atomic<bool>> cancelFlag = {});
};

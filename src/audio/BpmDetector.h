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

    // Outcome of the AutoTiming 2 analysis stage. Kept separate from the legacy
    // result so callers can tell "no analysis requested" apart from "requested
    // but failed" or "cancelled": hasAnalysis=false alone must not merge these.
    enum class AnalysisStatus
    {
        NotRequested, // legacy-only call, or legacy failed before analysis ran
        Succeeded,
        Failed,       // abstain or error from autotiming::analyze; see analysisError
        Cancelled,    // cancelled after legacy completed, before analysis started
    };

    struct DetectionResult
    {
        double bpm = 0.0;
        double estimatedOffsetMs = 0.0;
        QVector<SegmentResult> segments;

        // AutoTiming 2 analysis summary. Filled only by analyzeFromFileDetailed
        // (legacy bpm/estimatedOffsetMs above keep their existing semantics and
        // are always produced by the legacy AutoTiming pipeline).
        AnalysisStatus analysisStatus = AnalysisStatus::NotRequested;
        QString analysisError;    // non-empty when Failed/Cancelled
        bool hasAnalysis = false; // convenience: true iff analysisStatus == Succeeded
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

    // Legacy detection + AutoTiming 2 analysis (autotiming::analyze). Contract:
    //   - return true  <=> the legacy detection succeeded; the analysis stage
    //     never flips the return value. Check analysisStatus for the V2 outcome:
    //     Succeeded (hasAnalysis=true), Failed (analysisError explains the
    //     abstain/error), or Cancelled (cancelled after legacy completed).
    //   - return false <=> legacy failed or the call was cancelled before the
    //     legacy run; analysisStatus stays NotRequested and *outError explains.
    // Mid-analysis cancellation is not supported (the vendored analyze() has no
    // progress/cancel hook). Non-32/44.1/48 kHz inputs are linearly resampled
    // to 44100 Hz first.
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

    // Asynchronous analyzeFromFileDetailed. analysisOptions mirrors the sync
    // API (captured by value into the worker). cancelFlag (optional) is a
    // task-level cooperative cancel owned by the caller, checked before
    // decoding and before the analysis core runs; the vendored analyze()
    // exposes no progress callback, so mid-analysis cancellation is not
    // supported (status then stays whatever it was when the checks ran).
    // The callback runs on context's thread; success/error semantics follow
    // analyzeFromFileDetailed.
    using AsyncAnalysisCallback = AsyncDetectionCallback;
    static void analyzeFromFileDetailedAsync(QObject *context,
                                             const QString &audioFilePath,
                                             double startMs,
                                             double durationMs,
                                             AsyncAnalysisCallback callback,
                                             const AutoTiming2Options &analysisOptions = {},
                                             std::shared_ptr<std::atomic<bool>> cancelFlag = {});
};

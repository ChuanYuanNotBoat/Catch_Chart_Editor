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
    enum class LegacyStatus
    {
        NotRequested, // common preparation failed before the legacy stage
        Succeeded,
        Failed,
        Cancelled,
    };

    // Outcome of the AutoTiming 2 analysis stage. Succeeded means the analysis
    // completed and returned a structurally valid summary; it does NOT imply a
    // tempo candidate or useful confidence. Callers must inspect candidates and
    // confidence before presenting a recommendation.
    enum class AnalysisStatus
    {
        NotRequested, // legacy-only call, or common preparation failed
        Succeeded,
        Failed,       // invalid options or an analysis exception; see analysisError
        Cancelled,
    };

    struct DetectionResult
    {
        // Legacy AutoTiming result. BPM and offset keep their original meaning
        // and are valid only when legacyStatus == Succeeded.
        LegacyStatus legacyStatus = LegacyStatus::NotRequested;
        QString legacyError;
        double bpm = 0.0;
        double estimatedOffsetMs = 0.0;

        // AutoTiming 2 analysis summary. All absolute location fields exposed
        // here are translated to whole-file audio time. Durations and periods
        // remain unchanged. analysisStartMs records the cropped PCM origin.
        AnalysisStatus analysisStatus = AnalysisStatus::NotRequested;
        QString analysisError; // non-empty when Failed/Cancelled
        double analysisStartMs = 0.0;
        AutoTiming2Summary analysis;

        bool hasLegacyResult() const noexcept
        {
            return legacyStatus == LegacyStatus::Succeeded;
        }

        bool hasAnalysis() const noexcept
        {
            return analysisStatus == AnalysisStatus::Succeeded;
        }
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

    // Legacy detection + AutoTiming 2 analysis (autotiming::analyze). The file
    // is decoded once and both engines receive PCM derived from that decode.
    // Contract:
    //   - return true means common preparation completed and the independent
    //     stage statuses in outResult are authoritative. It does NOT mean that
    //     either engine produced a usable tempo.
    //   - return false means validation, decode, or early cancellation stopped
    //     the common pipeline; *outError explains the shared failure.
    // Legacy and V2 errors never overwrite each other or the shared error.
    // Mid-analysis cancellation is not supported (the vendored analyze() has no
    // progress/cancel hook). Non-32/44.1/48 kHz inputs are linearly resampled
    // to 44100 Hz for each engine without changing the legacy input contract.
    static bool analyzeFromFileDetailed(const QString &audioFilePath,
                                        double startMs,
                                        double durationMs,
                                        DetectionResult &outResult,
                                        QString *outError = nullptr,
                                        const AutoTiming2Options &analysisOptions = {});

    // Low-level decoded-mono variants used by the file pipeline and deterministic
    // regression tests. detectFromMonoDetailed keeps the legacy-only bool
    // contract; analyzeFromMonoDetailed follows the rich aggregate contract
    // above and translates V2 locations by analysisStartMs.
    static bool detectFromMonoDetailed(const QVector<float> &mono,
                                       int sampleRate,
                                       DetectionResult &outResult,
                                       QString *outError = nullptr);

    static bool analyzeFromMonoDetailed(const QVector<float> &mono,
                                        int sampleRate,
                                        double analysisStartMs,
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
    // The callback runs on context's thread; its bool/error semantics follow
    // analyzeFromFileDetailed and must not be interpreted as "tempo detected".
    using AsyncAnalysisCallback = AsyncDetectionCallback;
    static void analyzeFromFileDetailedAsync(QObject *context,
                                             const QString &audioFilePath,
                                             double startMs,
                                             double durationMs,
                                             AsyncAnalysisCallback callback,
                                             const AutoTiming2Options &analysisOptions = {},
                                             std::shared_ptr<std::atomic<bool>> cancelFlag = {});
};

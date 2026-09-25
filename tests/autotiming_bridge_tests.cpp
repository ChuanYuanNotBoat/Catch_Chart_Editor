// Tests for the CCE AutoTiming 2 bridge (AutoTiming2Bridge) using synthetic
// pulse trains. Follows the repo convention of a plain main() returning a
// non-zero code on failure. Does NOT test UI paths.
#include "audio/AutoTiming2Bridge.h"
#include "audio/BpmDetector.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QVector>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    int g_failures = 0;

    void require(bool condition, const std::string &message)
    {
        if (!condition)
        {
            std::fprintf(stderr, "FAIL: %s\n", message.c_str());
            ++g_failures;
        }
    }

    // Deterministic pulse train with a low-level noise bed (same recipe idea
    // as the upstream legacy_baseline_tests makePulseTrain).
    QVector<float> makePulseTrainFloat(int sampleRate, double durationSeconds,
                                       double bpm, double firstPulseSeconds)
    {
        const qsizetype sampleCount =
            static_cast<qsizetype>(std::ceil(durationSeconds * sampleRate));
        QVector<float> signal(static_cast<int>(sampleCount), 0.0f);

        std::uint32_t noiseState = 0x6d2b79f5U;
        for (qsizetype i = 0; i < sampleCount; ++i)
        {
            noiseState = noiseState * 1664525U + 1013904223U;
            const double noise =
                static_cast<double>((noiseState >> 8) & 0xffffU) / 32767.5 - 1.0;
            signal[static_cast<int>(i)] = static_cast<float>(noise * 0.0005);
        }

        const double secondsPerBeat = 60.0 / bpm;
        for (double beat = firstPulseSeconds; beat < durationSeconds; beat += secondsPerBeat)
        {
            const qsizetype onset = static_cast<qsizetype>(std::llround(beat * sampleRate));
            const qsizetype burstLength = static_cast<qsizetype>(0.08 * sampleRate);
            for (qsizetype j = 0; j < burstLength && onset + j < sampleCount; ++j)
            {
                const double t = static_cast<double>(j) / sampleRate;
                const double envelope = std::exp(-t * 55.0);
                const double low = std::sin(2.0 * 3.14159265358979323846 * 90.0 * t);
                const double high = std::sin(2.0 * 3.14159265358979323846 * 1800.0 * t);
                signal[static_cast<int>(onset + j)] +=
                    static_cast<float>(envelope * (0.72 * low + 0.28 * high));
            }
        }
        return signal;
    }
    bool anyCandidateNear(const AutoTiming2Summary &s, double targetBpm, double tol)
    {
        for (const AutoTiming2Candidate &c : s.tempoCandidates)
            if (std::fabs(c.bpm - targetBpm) <= tol)
                return true;
        for (const AutoTiming2Window &w : s.windows)
            for (const AutoTiming2Candidate &c : w.tempoCandidates)
                if (std::fabs(c.bpm - targetBpm) <= tol)
                    return true;
        return false;
    }

    bool inUnitInterval(double v) { return v >= 0.0 && v <= 1.0; }

    bool nearlyEqual(double left, double right, double tolerance = 1e-9)
    {
        return std::fabs(left - right) <= tolerance;
    }

    AutoTiming2Options fastAnalysisOptions()
    {
        AutoTiming2Options options;
        options.windowSpecs = {{8.0, 4.0}};
        options.minimumWindowSeconds = 4.0;
        return options;
    }

    void testPulseTrainPipeline()
    {
        const int sampleRate = 44100;
        const QVector<float> mono = makePulseTrainFloat(sampleRate, 12.0, 120.0, 0.5);

        // Keep the unit test cheap: a single short evidence scale.
        const AutoTiming2Options options = fastAnalysisOptions();

        AutoTiming2Summary summary;
        QString error;
        const bool ok = AutoTiming2Bridge::analyzeMono(mono, sampleRate, options, summary, &error);
        require(ok, std::string("analyzeMono failed on pulse train: ") + error.toStdString());
        if (!ok)
            return;

        require(summary.valid, "summary.valid must be set on success");
        require(!summary.tempoCandidates.isEmpty(), "global tempo candidates must not be empty");
        require(!summary.tempoFamilies.isEmpty(), "tempo families must not be empty");
        require(!summary.tempoHypotheses.isEmpty(), "tempo hypotheses must not be empty");
        require(summary.tempoMap.available,
                "bridge must expose the core phase-anchored tempo map");
        require(summary.tempoMap.bpmListAvailable && !summary.tempoMap.bpmList.isEmpty(),
                "bridge must expose the core bounded-error BPM list");
        require(summary.tempoMap.maximumBpmListModelErrorMilliseconds <=
                    options.tempoMapMaximumTimeErrorMilliseconds + 1e-9,
                "mapped BPM list exceeded the bridge-requested error bound");
        require(!summary.windows.isEmpty(), "diagnostics windows must not be empty");
        for (const AutoTiming2Candidate &candidate : summary.tempoCandidates)
            require(!candidate.hasPulseTime,
                    "global tempo candidates must not invent phase data");
        for (const AutoTiming2Window &window : summary.windows)
            for (const AutoTiming2Candidate &candidate : window.tempoCandidates)
                require(candidate.hasPulseTime,
                        "window candidates must identify their pulse time as present");
        require(inUnitInterval(summary.confidence.tempo), "confidence.tempo must be in [0,1]");
        require(inUnitInterval(summary.confidence.overall), "confidence.overall must be in [0,1]");
        require(inUnitInterval(summary.confidence.reliableCoverage),
                "confidence.reliableCoverage must be in [0,1]");
        require(summary.durationSeconds > 11.0 && summary.durationSeconds < 13.0,
                "duration metadata must match input length");
        require(anyCandidateNear(summary, 120.0, 1.5) ||
                    anyCandidateNear(summary, 60.0, 1.5) ||
                    anyCandidateNear(summary, 240.0, 1.5),
                "some tempo candidate must match the pulse train or a harmonic");

        bool hasSelectedHypothesis = false;
        for (const AutoTiming2Hypothesis &h : summary.tempoHypotheses)
            hasSelectedHypothesis = hasSelectedHypothesis || h.selected;
        require(hasSelectedHypothesis, "one hypothesis must be selected");

        int mappedStableGridCount = 0;
        for (const AutoTiming2TrackPoint &p : summary.tempoTrack)
            if (p.propagationReason == QStringLiteral("stable_grid_regularization"))
                ++mappedStableGridCount;
        require(summary.stableGridRegularizedCount == mappedStableGridCount,
                "stable-grid diagnostics count must match mapped tracker reasons");
        require(summary.stableGridRegularizedCount <= summary.multiScalePhaseRefinedCount,
                "stable-grid corrections must be included in phase-refined tracker points");

        // pulseTimeSeconds stays an absolute audio time, separate from the
        // legacy offset semantics.
        for (const AutoTiming2TrackPoint &p : summary.tempoTrack)
            require(p.pulseTimeSeconds >= -1e-9 && p.pulseTimeSeconds <= summary.durationSeconds + 1e-9,
                    "track pulseTimeSeconds must be an absolute audio time");
    }
    void testShortAudioAbstains()
    {
        const int sampleRate = 44100;
        const QVector<float> mono = makePulseTrainFloat(sampleRate, 1.5, 120.0, 0.0);

        const AutoTiming2Options options = fastAnalysisOptions();

        AutoTiming2Summary summary;
        QString error;
        const bool ok = AutoTiming2Bridge::analyzeMono(mono, sampleRate, options, summary, &error);
        // Either the core abstains (ok==false with an error) or it returns a
        // result that must not claim confident knowledge.
        if (ok)
        {
            require(summary.tempoCandidates.isEmpty() ||
                        summary.confidence.overall <= 0.5,
                    "short audio must not produce a confident analysis");
        }
        else
        {
            require(!error.isEmpty(), "abstain must carry an error message");
        }
    }

    void testSilenceAbstains()
    {
        const int sampleRate = 44100;
        QVector<float> mono(static_cast<int>(10.0 * sampleRate), 0.0f);

        const AutoTiming2Options options = fastAnalysisOptions();

        AutoTiming2Summary summary;
        QString error;
        const bool ok = AutoTiming2Bridge::analyzeMono(mono, sampleRate, options, summary, &error);
        if (ok)
        {
            require(summary.tempoCandidates.isEmpty() || summary.confidence.overall <= 0.5,
                    "silence must not produce a confident analysis");
        }
        else
        {
            require(!error.isEmpty(), "silence abstain must carry an error message");
        }
    }
    void testInvalidParameters()
    {
        AutoTiming2Summary summary;
        QString error;

        require(!AutoTiming2Bridge::analyzeMono(QVector<float>{}, 44100, AutoTiming2Options{}, summary, &error),
                "empty input must fail");
        require(!error.isEmpty(), "empty input must carry an error");

        const QVector<float> mono = makePulseTrainFloat(44100, 12.0, 120.0, 0.5);
        error.clear();
        require(!AutoTiming2Bridge::analyzeMono(mono, 22050, AutoTiming2Options{}, summary, &error),
                "unsupported sample rate must fail");
        require(error.contains(QStringLiteral("采样率")),
                "unsupported sample rate error must mention 采样率");
    }

    void testBpmDetectorValidation()
    {
        BpmDetector::DetectionResult result;
        QString error;
        require(!BpmDetector::analyzeFromFileDetailed(QString(), 0.0, 10000.0, result, &error),
                "empty path must fail");
        require(!error.isEmpty(), "empty path must carry an error");

        error.clear();
        require(!BpmDetector::analyzeFromFileDetailed(QStringLiteral("whatever.ogg"), 0.0, 0.0, result, &error),
                "non-positive duration must fail");
        require(!error.isEmpty(), "non-positive duration must carry an error");
    }

    void testCommonDecodeFailureLeavesStagesUnrequested()
    {
        // The decoder cannot open a nonexistent file, so neither engine starts.
        BpmDetector::DetectionResult result;
        QString error;
        require(!BpmDetector::analyzeFromFileDetailed(QStringLiteral("_definitely_missing_audio_.ogg"),
                                                      0.0, 10000.0, result, &error),
                "nonexistent file must fail");
        require(result.legacyStatus == BpmDetector::LegacyStatus::NotRequested,
                "decode failure must leave legacy unrequested");
        require(result.analysisStatus == BpmDetector::AnalysisStatus::NotRequested,
                "decode failure must leave analysis unrequested");
        require(!result.hasLegacyResult(), "decode failure must not expose a legacy result");
        require(!result.hasAnalysis(), "decode failure must not expose an analysis");
        require(result.legacyError.isEmpty(),
                "decode failure must not be mislabeled as a legacy engine error");
        require(result.analysisError.isEmpty(),
                "decode failure must not carry an analysis engine error");
        require(!error.isEmpty(), "decode failure must explain itself via the shared error");
    }

    void testLegacyParityInRichPipeline()
    {
        // 22050 Hz exercises the pre-existing legacy linear-resample path. The
        // rich call must not alter the legacy PCM derivation or result.
        const int sampleRate = 22050;
        const QVector<float> mono = makePulseTrainFloat(sampleRate, 16.0, 120.0, 0.5);

        BpmDetector::DetectionResult legacyOnly;
        QString legacyCallError;
        const bool legacyOk = BpmDetector::detectFromMonoDetailed(
            mono, sampleRate, legacyOnly, &legacyCallError);
        require(legacyOk,
                std::string("legacy-only PCM detection failed: ") + legacyCallError.toStdString());
        if (!legacyOk)
            return;

        BpmDetector::DetectionResult rich;
        QString sharedError;
        const bool completed = BpmDetector::analyzeFromMonoDetailed(
            mono, sampleRate, 60000.0, rich, &sharedError, fastAnalysisOptions());
        require(completed, "rich PCM pipeline must complete on the same pulse train");
        require(sharedError.isEmpty(), "completed rich pipeline must not report a shared error");
        require(rich.legacyStatus == BpmDetector::LegacyStatus::Succeeded,
                "rich pipeline legacy stage must succeed");
        require(rich.hasLegacyResult(), "legacy success accessor must follow status");
        require(nearlyEqual(legacyOnly.bpm, rich.bpm),
                "legacy-only and rich legacy BPM must be identical");
        require(nearlyEqual(legacyOnly.estimatedOffsetMs, rich.estimatedOffsetMs),
                "legacy-only and rich legacy offset must be identical");
        require(nearlyEqual(rich.analysisStartMs, 60000.0),
                "rich pipeline must retain its non-zero analysis origin");
        if (rich.hasAnalysis() && !rich.analysis.windows.isEmpty())
            require(rich.analysis.windows.first().startSeconds >= 60.0,
                    "rich pipeline must expose V2 windows on the whole-file timeline");
    }

    void testLegacyFailureDoesNotBlockCompletedAnalysis()
    {
        const int sampleRate = 44100;
        const QVector<float> silence(static_cast<int>(10.0 * sampleRate), 0.0f);

        BpmDetector::DetectionResult result;
        QString sharedError;
        const bool completed = BpmDetector::analyzeFromMonoDetailed(
            silence, sampleRate, 0.0, result, &sharedError, fastAnalysisOptions());

        require(completed, "engine failures must not turn a prepared rich pipeline into shared failure");
        require(sharedError.isEmpty(), "engine-local outcomes must not overwrite the shared error");
        require(result.legacyStatus == BpmDetector::LegacyStatus::Failed,
                "silence must produce a legacy engine failure");
        require(!result.hasLegacyResult(), "failed legacy status must not expose a result");
        require(!result.legacyError.isEmpty(), "legacy failure must retain its own error");
        require(result.analysisStatus == BpmDetector::AnalysisStatus::Succeeded,
                "V2 completion must not be blocked by legacy failure");
        require(result.hasAnalysis(), "analysis completion accessor must follow status");
        require(result.analysis.tempoCandidates.isEmpty(),
                "silence analysis must not fabricate a global tempo candidate");
        require(nearlyEqual(result.analysis.confidence.overall, 0.0),
                "candidate-free silence analysis must retain zero confidence");
        require(result.analysisError.isEmpty(),
                "completed-but-uncertain analysis must not carry an error");
    }

    void testAnalysisFailureDoesNotDestroyLegacyResult()
    {
        const int sampleRate = 44100;
        const QVector<float> mono = makePulseTrainFloat(sampleRate, 16.0, 120.0, 0.5);
        AutoTiming2Options invalidOptions = fastAnalysisOptions();
        invalidOptions.minimumTempoBpm = 200.0;
        invalidOptions.maximumTempoBpm = 100.0;

        BpmDetector::DetectionResult result;
        QString sharedError;
        const bool completed = BpmDetector::analyzeFromMonoDetailed(
            mono, sampleRate, 0.0, result, &sharedError, invalidOptions);

        require(completed, "invalid V2 options must remain an engine-local rich outcome");
        require(sharedError.isEmpty(), "V2 error must not overwrite the shared error");
        require(result.legacyStatus == BpmDetector::LegacyStatus::Succeeded,
                "V2 failure must preserve successful legacy status");
        require(result.hasLegacyResult() && result.bpm > 0.0,
                "V2 failure must preserve legacy BPM");
        require(result.analysisStatus == BpmDetector::AnalysisStatus::Failed,
                "invalid V2 options must report analysis failure");
        require(!result.hasAnalysis(), "failed V2 status must not expose completed analysis");
        require(!result.analysisError.isEmpty(), "V2 failure must retain its own error");
        require(result.legacyError.isEmpty(), "successful legacy stage must not acquire the V2 error");
    }

    void testTimelineTranslationMovesLocationsOnly()
    {
        AutoTiming2Summary summary;
        summary.durationSeconds = 12.0;
        summary.multiScalePhaseRefinedCount = 3;
        summary.stableGridRegularizedCount = 2;

        AutoTiming2Candidate globalCandidate;
        globalCandidate.bpm = 120.0;
        globalCandidate.pulseTimeSeconds = 0.0;
        globalCandidate.hasPulseTime = false;
        summary.tempoCandidates.append(globalCandidate);

        AutoTiming2TrackPoint trackPoint;
        trackPoint.timeSeconds = 4.0;
        trackPoint.pulseTimeSeconds = 4.25;
        summary.tempoTrack.append(trackPoint);

        AutoTiming2Hypothesis hypothesis;
        hypothesis.track.append(trackPoint);
        hypothesis.tempoMap.available = true;
        hypothesis.tempoMap.startSeconds = 1.0;
        hypothesis.tempoMap.endSeconds = 9.0;
        summary.tempoHypotheses.append(hypothesis);

        summary.tempoMap.available = true;
        summary.tempoMap.startSeconds = 0.25;
        summary.tempoMap.endSeconds = 10.25;
        AutoTiming2TempoMapAnchor mapAnchor;
        mapAnchor.timeSeconds = 4.25;
        summary.tempoMap.anchors.append(mapAnchor);
        AutoTiming2TempoCurveSegment mapSegment;
        mapSegment.startSeconds = 0.25;
        mapSegment.endSeconds = 10.25;
        summary.tempoMap.segments.append(mapSegment);
        summary.tempoMap.bpmList.append({0.0, 120.0});

        AutoTiming2Layer layer;
        layer.startSeconds = 1.0;
        layer.endSeconds = 3.0;
        summary.periodicityLayers.append(layer);

        AutoTiming2Region region;
        region.startSeconds = 6.0;
        region.endSeconds = 8.0;
        summary.uncertainRegions.append(region);

        AutoTiming2Anchor anchor;
        anchor.startSeconds = 2.0;
        anchor.endSeconds = 5.0;
        summary.anchors.append(anchor);

        AutoTiming2Candidate localCandidate;
        localCandidate.bpm = 120.0;
        localCandidate.periodSeconds = 0.5;
        localCandidate.pulseTimeSeconds = 2.5;
        localCandidate.hasPulseTime = true;
        localCandidate.legacyOffsetMilliseconds = 125.0;
        AutoTiming2Window window;
        window.startSeconds = 0.0;
        window.endSeconds = 8.0;
        window.tempoCandidates.append(localCandidate);
        summary.windows.append(window);

        AutoTiming2Bridge::translateTimeline(summary, 60.0);

        require(nearlyEqual(summary.durationSeconds, 12.0),
                "timeline translation must not change analysis duration");
        require(summary.multiScalePhaseRefinedCount == 3 &&
                    summary.stableGridRegularizedCount == 2,
                "timeline translation must not change phase diagnostic counters");
        require(nearlyEqual(summary.tempoCandidates.first().pulseTimeSeconds, 0.0),
                "phase-less global candidate must not gain a fake absolute pulse");
        require(nearlyEqual(summary.tempoTrack.first().timeSeconds, 64.0) &&
                    nearlyEqual(summary.tempoTrack.first().pulseTimeSeconds, 64.25),
                "tempo track position and pulse must become whole-file times");
        require(nearlyEqual(summary.tempoHypotheses.first().track.first().timeSeconds, 64.0) &&
                    nearlyEqual(summary.tempoHypotheses.first().track.first().pulseTimeSeconds, 64.25),
                "hypothesis track times must become whole-file times");
        require(nearlyEqual(summary.tempoMap.startSeconds, 60.25) &&
                    nearlyEqual(summary.tempoMap.endSeconds, 70.25) &&
                    nearlyEqual(summary.tempoMap.anchors.first().timeSeconds, 64.25) &&
                    nearlyEqual(summary.tempoMap.segments.first().startSeconds, 60.25) &&
                    nearlyEqual(summary.tempoMap.bpmList.first().beat, 0.0),
                "tempo-map audio locations must translate while beat coordinates stay local");
        require(nearlyEqual(summary.tempoHypotheses.first().tempoMap.startSeconds, 61.0) &&
                    nearlyEqual(summary.tempoHypotheses.first().tempoMap.endSeconds, 69.0),
                "hypothesis tempo-map locations must become whole-file times");
        require(nearlyEqual(summary.periodicityLayers.first().startSeconds, 61.0) &&
                    nearlyEqual(summary.periodicityLayers.first().endSeconds, 63.0),
                "periodicity interval locations must be translated");
        require(nearlyEqual(summary.uncertainRegions.first().startSeconds, 66.0) &&
                    nearlyEqual(summary.uncertainRegions.first().endSeconds, 68.0),
                "uncertain region locations must be translated");
        require(nearlyEqual(summary.anchors.first().startSeconds, 62.0) &&
                    nearlyEqual(summary.anchors.first().endSeconds, 65.0),
                "anchor locations must be translated");
        require(nearlyEqual(summary.windows.first().startSeconds, 60.0) &&
                    nearlyEqual(summary.windows.first().endSeconds, 68.0),
                "window locations must be translated");
        const AutoTiming2Candidate &translatedCandidate = summary.windows.first().tempoCandidates.first();
        require(nearlyEqual(translatedCandidate.pulseTimeSeconds, 62.5),
                "window candidate pulse must be translated");
        require(nearlyEqual(translatedCandidate.periodSeconds, 0.5),
                "timeline translation must not change a period");
        require(nearlyEqual(translatedCandidate.legacyOffsetMilliseconds, 125.0),
                "timeline translation must not change a legacy delay offset");
    }

    void testAsyncCancelBeforeStart()
    {
        // Deterministic: the flag is set before the worker runs, so the call
        // is rejected before any decoding happens.
        auto cancelFlag = std::make_shared<std::atomic<bool>>(true);

        bool callbackInvoked = false;
        bool success = true;
        BpmDetector::DetectionResult result;
        QString error;

        QObject context;
        QEventLoop loop;
        QTimer::singleShot(10000, &loop, [&loop]() { loop.quit(); }); // watchdog

        BpmDetector::analyzeFromFileDetailedAsync(
            &context, QStringLiteral("_definitely_missing_audio_.ogg"), 0.0, 10000.0,
            [&](bool ok, BpmDetector::DetectionResult r, const QString &e)
            {
                callbackInvoked = true;
                success = ok;
                result = std::move(r);
                error = e;
                loop.quit();
            },
            AutoTiming2Options{}, cancelFlag);

        loop.exec();

        require(callbackInvoked, "cancel before start must still deliver the callback");
        require(!success, "cancelled call must report failure");
        require(error.contains(QStringLiteral("取消")),
                "cancel error must explain the cancellation");
        require(result.legacyStatus == BpmDetector::LegacyStatus::Cancelled,
                "cancel before decode must mark the requested legacy stage cancelled");
        require(result.analysisStatus == BpmDetector::AnalysisStatus::Cancelled,
                "cancel before decode must mark the requested analysis stage cancelled");
        require(!result.hasLegacyResult(), "cancelled call must not expose a legacy result");
        require(!result.hasAnalysis(), "cancelled call must not expose an analysis");
    }
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const auto runTest = [](const char *name, void (*fn)())
    {
        std::printf("[bridge-test] %s ...\n", name);
        std::fflush(stdout);
        fn();
    };

    runTest("pulse_train_pipeline", testPulseTrainPipeline);
    runTest("short_audio_abstains", testShortAudioAbstains);
    runTest("silence_abstains", testSilenceAbstains);
    runTest("invalid_parameters", testInvalidParameters);
    runTest("bpm_detector_validation", testBpmDetectorValidation);
    runTest("common_decode_failure", testCommonDecodeFailureLeavesStagesUnrequested);
    runTest("legacy_parity_in_rich_pipeline", testLegacyParityInRichPipeline);
    runTest("legacy_failure_does_not_block_analysis", testLegacyFailureDoesNotBlockCompletedAnalysis);
    runTest("analysis_failure_preserves_legacy", testAnalysisFailureDoesNotDestroyLegacyResult);
    runTest("timeline_translation", testTimelineTranslationMovesLocationsOnly);
    runTest("async_cancel_before_start", testAsyncCancelBeforeStart);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("autotiming bridge tests passed\n");
    return 0;
}

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

    void testPulseTrainPipeline()
    {
        const int sampleRate = 44100;
        const QVector<float> mono = makePulseTrainFloat(sampleRate, 12.0, 120.0, 0.5);

        AutoTiming2Options options;
        // Keep the unit test cheap: a single short evidence scale.
        options.windowSpecs = {{8.0, 4.0}};
        options.minimumWindowSeconds = 4.0;

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
        require(!summary.windows.isEmpty(), "diagnostics windows must not be empty");
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

        AutoTiming2Options options;
        options.windowSpecs = {{8.0, 4.0}};
        options.minimumWindowSeconds = 4.0;

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

        AutoTiming2Options options;
        options.windowSpecs = {{8.0, 4.0}};
        options.minimumWindowSeconds = 4.0;

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
    void testAnalysisStatusOnLegacyFailure()
    {
        // Deterministic: the decoder cannot open a nonexistent file, so the
        // legacy stage fails and the analysis stage must never be entered.
        BpmDetector::DetectionResult result;
        QString error;
        require(!BpmDetector::analyzeFromFileDetailed(QStringLiteral("_definitely_missing_audio_.ogg"),
                                                      0.0, 10000.0, result, &error),
                "nonexistent file must fail");
        require(result.analysisStatus == BpmDetector::AnalysisStatus::NotRequested,
                "legacy failure must leave analysisStatus == NotRequested");
        require(!result.hasAnalysis, "legacy failure must not set hasAnalysis");
        require(result.analysisError.isEmpty(),
                "legacy failure must not carry an analysis error");
        require(!error.isEmpty(), "legacy failure must explain itself via outError");
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
        require(result.analysisStatus == BpmDetector::AnalysisStatus::NotRequested,
                "cancel before legacy run must leave analysisStatus == NotRequested");
        require(!result.hasAnalysis, "cancelled call must not set hasAnalysis");
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
    runTest("analysis_status_on_legacy_failure", testAnalysisStatusOnLegacyFailure);
    runTest("async_cancel_before_start", testAsyncCancelBeforeStart);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("autotiming bridge tests passed\n");
    return 0;
}
